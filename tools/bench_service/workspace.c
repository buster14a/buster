/* Installed-input and per-attempt workspace boundary for #437.
 *
 * Entry points: bq_materialize reserves the existing FIFO authority before it
 * validates/copies an installed source snapshot; bq_workspace_reconcile removes
 * only a seal-matched attempt before allowing that authority to finish.
 *
 * Layout: installed recipe/source validation, bounded manifest copying,
 * sealed workspace identity, mode-read-only evidence, and fail-closed cleanup. This
 * is not a process supervisor, build recipe runner, transport, or host lease.
 */
#include "queue.h"

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#define BQ_WORKSPACE_TRAVERSE_MODE 02710
#define BQ_WORKSPACE_PRIVATE_BUILD_MODE 02700
#define BQ_SOURCE_MANIFEST_CAP (64u * 1024u)
/* #1018: the tracked #923 tree has 2,842 entries and a 341,377-byte
 * manifest. Retirement gets a separately pinned inventory and a bounded
 * preflight before this larger buffer can be used. Smoke retains 64 KiB. */
#define BQ_RETIREMENT_SOURCE_MANIFEST_CAP (512u * 1024u)
#define BQ_SOURCE_FILE_CAP (64u * 1024u * 1024u)
#define BQ_SOURCE_TOTAL_CAP (512u * 1024u * 1024u)
#define BQ_SOURCE_COUNT_CAP 4096u
#define BQ_SOURCE_DIRECTORY_CAP 480u
#define BQ_DIRECTORY_CAP 1024u
#define BQ_CLEANUP_ENTRY_CAP 16384u
#define BQ_CLEANUP_DEPTH_CAP 256u
#include "retirement_prepare.h"
#include "retirement_binaries.h"

typedef struct BqDirectoryList
{
    u32 count;
    char paths[BQ_DIRECTORY_CAP][BQ_PATH_CAP + 1];
} BqDirectoryList;

BUSTER_GLOBAL_LOCAL bool bq_string_path(String8 path, char result[BQ_PATH_CAP + 1])
{
    bool ok = path.length > 1 && path.length <= BQ_PATH_CAP && path.pointer[0] == '/';
    u64 component = 1;
    for (u64 i = 1; ok && i <= path.length; i += 1)
    {
        bool end = i == path.length || path.pointer[i] == '/';
        if (end)
        {
            u64 length = i - component;
            ok = length > 0 && !(length == 1 && path.pointer[component] == '.') &&
                 !(length == 2 && path.pointer[component] == '.' && path.pointer[component + 1] == '.');
            component = i + 1;
        }
        else
        {
            ok = path.pointer[i] != 0;
        }
    }
    if (ok)
    {
        memcpy(result, path.pointer, (size_t)path.length);
        result[path.length] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_append(char* output, u32 capacity, u32* offset,
                                              char const* bytes, u64 length)
{
    bool ok = output && offset && bytes && *offset < capacity && length < capacity - *offset && length <= UINT32_MAX;
    if (ok)
    {
        memcpy(output + *offset, bytes, (size_t)length);
        *offset += (u32)length;
        output[*offset] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_open_absolute_directory(String8 path)
{
    int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u64 offset = 1;
    while (current >= 0 && offset < path.length)
    {
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/')
        {
            end += 1;
        }
        char name[BQ_PATH_CAP + 1];
        u64 length = end - offset;
        memcpy(name, path.pointer + offset, (size_t)length);
        name[length] = 0;
        int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(current);
        current = next;
        offset = end + 1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL bool bq_owned_directory(int fd, bool private_directory, bool immutable_directory)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && (info.st_uid == 0 || info.st_uid == geteuid());
    if (ok && private_directory)
    {
        ok = (info.st_mode & 077) == 0;
    }
    if (ok && immutable_directory)
    {
        ok = (info.st_mode & 0222) == 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_root_directory(int fd)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
              (info.st_mode & 067) == 0;
#ifdef __APPLE__
    /* Darwin applies BSD directory group inheritance without requiring the
     * Linux setgid contract. Pin the root to the service's effective group;
     * descendants are independently checked for inherited group identity. */
    ok = ok && info.st_gid == getegid();
#else
    ok = ok && (info.st_mode & S_ISGID) != 0;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_reconcile_root_directory(int fd)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) &&
              (info.st_uid == 0 || info.st_uid == geteuid()) && (info.st_mode & 027) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_read_file(int fd, u8* bytes, u32 capacity, u32* size)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 && (u64)info.st_size <= capacity;
    *size = ok ? (u32)info.st_size : 0;
    u32 done = 0;
    while (ok && done < *size)
    {
        ssize_t count = pread(fd, bytes + done, *size - done, done);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        ok = count > 0;
        if (ok)
        {
            done += (u32)count;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_write_all(int fd, u8 const* bytes, u32 size)
{
    u32 done = 0;
    bool ok = true;
    while (ok && done < size)
    {
        ssize_t count = write(fd, bytes + done, size - done);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        ok = count > 0;
        if (ok)
        {
            done += (u32)count;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_next_line(String8 text, u64* offset, String8* line)
{
    u64 start = *offset;
    bool found = false;
    for (u64 i = start; !found && i < text.length; i += 1)
    {
        if (text.pointer[i] == '\n')
        {
            *line = (String8){text.pointer + start, i - start};
            *offset = i + 1;
            found = true;
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool bq_relative_source_path(String8 path)
{
    bool ok = path.length > 0 && path.length <= BQ_PATH_CAP && path.pointer[0] != '/' &&
              !string_equal(path, S8(".source-manifest"));
    u64 component = 0;
    for (u64 i = 0; ok && i <= path.length; i += 1)
    {
        bool end = i == path.length || path.pointer[i] == '/';
        if (end)
        {
            u64 length = i - component;
            ok = length > 0 && !(length == 1 && path.pointer[component] == '.') &&
                 !(length == 2 && path.pointer[component] == '.' && path.pointer[component + 1] == '.');
            component = i + 1;
        }
        else
        {
            u8 c = (u8)path.pointer[i];
            ok = c >= 0x21 && c <= 0x7e && c != '\\';
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_open_directory_path(int root, String8 path)
{
    int current = dup(root);
    u64 offset = 0;
    while (current >= 0 && offset < path.length)
    {
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/')
        {
            end += 1;
        }
        char name[BQ_PATH_CAP + 1];
        u64 length = end - offset;
        memcpy(name, path.pointer + offset, (size_t)length);
        name[length] = 0;
        if (current >= 0)
        {
            int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(current);
            current = next;
        }
        offset = end + 1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_open_destination_directory(int root, String8 path, u32* directories)
{
    int current = dup(root);
    u64 offset = 0;
    while (current >= 0 && offset < path.length)
    {
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/')
        {
            end += 1;
        }
        char name[BQ_PATH_CAP + 1];
        u64 length = end - offset;
        memcpy(name, path.pointer + offset, (size_t)length);
        name[length] = 0;
        int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && *directories < BQ_SOURCE_DIRECTORY_CAP && mkdirat(current, name, 0700) == 0)
        {
            next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            *directories += next >= 0;
        }
        close(current);
        current = next;
        offset = end + 1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_open_installed_directory(int root, String8 path)
{
    int current = dup(root);
    u64 offset = 0;
    while (current >= 0 && offset < path.length)
    {
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/')
        {
            end += 1;
        }
        char name[BQ_PATH_CAP + 1];
        u64 length = end - offset;
        memcpy(name, path.pointer + offset, (size_t)length);
        name[length] = 0;
        int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(current);
        current = next >= 0 && bq_owned_directory(next, false, true) ? next : -1;
        if (next >= 0 && current < 0)
        {
            close(next);
        }
        offset = end + 1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_open_source_file(int root, String8 path)
{
    u64 slash = path.length;
    while (slash && path.pointer[slash - 1] != '/')
    {
        slash -= 1;
    }
    String8 directory = slash ? (String8){path.pointer, slash - 1} : (String8){0};
    String8 leaf = {path.pointer + slash, path.length - slash};
    int parent = directory.length ? bq_open_installed_directory(root, directory) : dup(root);
    int result = -1;
    if (parent >= 0)
    {
        char name[BQ_PATH_CAP + 1];
        memcpy(name, leaf.pointer, (size_t)leaf.length);
        name[leaf.length] = 0;
        result = openat(parent, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        close(parent);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_create_destination_file(int root, String8 path, u32* directories)
{
    u64 slash = path.length;
    while (slash && path.pointer[slash - 1] != '/')
    {
        slash -= 1;
    }
    String8 directory = slash ? (String8){path.pointer, slash - 1} : (String8){0};
    String8 leaf = {path.pointer + slash, path.length - slash};
    int parent = directory.length ? bq_open_destination_directory(root, directory, directories) : dup(root);
    int result = -1;
    if (parent >= 0)
    {
        char name[BQ_PATH_CAP + 1];
        memcpy(name, leaf.pointer, (size_t)leaf.length);
        name[leaf.length] = 0;
        result = openat(parent, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0440);
        close(parent);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_copy_verified_file(int source_root, int destination_root, String8 path, String8 expected,
                                                u64* total, u32* directories)
{
    int source = bq_open_source_file(source_root, path);
    int destination = -1;
    struct stat info;
    bool ok = source >= 0 && fstat(source, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              (info.st_mode & 0222) == 0 && info.st_size >= 0 && (u64)info.st_size <= BQ_SOURCE_FILE_CAP &&
              (u64)info.st_size <= BQ_SOURCE_TOTAL_CAP - *total;
    if (ok)
    {
        destination = bq_create_destination_file(destination_root, path, directories);
        ok = destination >= 0;
    }
    Sha256 hash;
    sha256_init(&hash);
    u8 buffer[64u * 1024u];
    u64 copied = 0;
    while (ok && copied < (u64)info.st_size)
    {
        u32 wanted = (u64)info.st_size - copied > sizeof(buffer) ? (u32)sizeof(buffer) : (u32)((u64)info.st_size - copied);
        ssize_t count = read(source, buffer, wanted);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        ok = count > 0;
        if (ok)
        {
            sha256_add(&hash, buffer, (u32)count);
            ok = bq_write_all(destination, buffer, (u32)count);
            copied += (u32)count;
        }
    }
    char8 digest[SHA256_HEX_CAPACITY];
    sha256_finish_hex(&hash, digest);
    ok = ok && copied == (u64)info.st_size && expected.length == 64 && !memcmp(digest, expected.pointer, 64) && fsync(destination) == 0;
    if (ok)
    {
        *total += copied;
    }
    if (destination >= 0)
    {
        close(destination);
    }
    if (source >= 0)
    {
        close(source);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_manifest_header(String8 manifest, String8 revision, u64* offset)
{
    String8 line;
    char expected[BQ_PATH_CAP + 1];
    u32 used = 0;
    bool expected_ok = bq_workspace_append(expected, sizeof(expected), &used, "revision=", 9) &&
                       bq_workspace_append(expected, sizeof(expected), &used, (char const*)revision.pointer, revision.length);
    int length = expected_ok && used <= INT_MAX ? (int)used : -1;
    bool ok = bq_next_line(manifest, offset, &line) && string_equal(line, S8("BQ-SOURCE-V1")) &&
              bq_next_line(manifest, offset, &line) && string_equal(line, S8("repository=buster14a/buster")) &&
              length > 0 && (u32)length < sizeof(expected) && bq_next_line(manifest, offset, &line) &&
              line.length == (u64)length && !memcmp(line.pointer, expected, (size_t)length);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_copy_manifest(int installed, int destination, String8 revision, u32 manifest_cap)
{
    char name[80];
    u32 name_size = 0;
    bool name_ok = bq_workspace_append(name, sizeof(name), &name_size, "sources/", 8) &&
                   bq_workspace_append(name, sizeof(name), &name_size, (char const*)revision.pointer, revision.length);
    int length = name_ok && name_size <= INT_MAX ? (int)name_size : -1;
    int source_root = length > 0 && (u32)length < sizeof(name) ?
                      bq_open_installed_directory(installed, string_from_pointer(name)) : -1;
    int manifest_fd = source_root >= 0 ?
                      openat(source_root, "source.manifest", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info;
    u8 manifest_bytes[BQ_RETIREMENT_SOURCE_MANIFEST_CAP];
    u32 manifest_size = 0;
    bool ok = source_root >= 0 && bq_owned_directory(source_root, false, true) && manifest_fd >= 0 &&
              fstat(manifest_fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
              manifest_cap <= sizeof(manifest_bytes) &&
              bq_read_file(manifest_fd, manifest_bytes, manifest_cap, &manifest_size);
    String8 manifest = {(char8*)manifest_bytes, manifest_size};
    u64 offset = 0;
    if (ok)
    {
        ok = bq_manifest_header(manifest, revision, &offset);
    }
    int copy = ok ? openat(destination, ".source-manifest", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0440) : -1;
    if (ok)
    {
        ok = copy >= 0 && bq_write_all(copy, manifest_bytes, manifest_size) && fsync(copy) == 0;
    }
    if (copy >= 0)
    {
        close(copy);
    }
    String8 previous = {0};
    u32 count = 0;
    u32 directories = 1;
    u64 total = 0;
    while (ok && offset < manifest.length)
    {
        String8 line;
        ok = bq_next_line(manifest, &offset, &line) && line.length > 65 && line.pointer[64] == ' ';
        String8 digest = ok ? (String8){line.pointer, 64} : (String8){0};
        String8 path = ok ? (String8){line.pointer + 65, line.length - 65} : (String8){0};
        for (u64 i = 0; ok && i < digest.length; i += 1)
        {
            u8 c = (u8)digest.pointer[i];
            ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        if (ok)
        {
            bool ordered = !previous.length;
            for (u64 i = 0; !ordered && i < previous.length && i < path.length; i += 1)
            {
                ordered = (u8)previous.pointer[i] < (u8)path.pointer[i];
                if ((u8)previous.pointer[i] > (u8)path.pointer[i])
                {
                    break;
                }
                if (i + 1 == previous.length && previous.length < path.length)
                {
                    ordered = true;
                }
            }
            ok = ordered && bq_relative_source_path(path) && count < BQ_SOURCE_COUNT_CAP &&
                 bq_copy_verified_file(source_root, destination, path, digest, &total, &directories);
            previous = path;
            count += ok;
        }
    }
    ok = ok && count > 0 && offset == manifest.length;
    if (manifest_fd >= 0)
    {
        close(manifest_fd);
    }
    if (source_root >= 0)
    {
        close(source_root);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_collect_directories(int root, BqDirectoryList* list)
{
    *list = (BqDirectoryList){0};
    list->count = 1;
    bool ok = true;
    for (u32 index = 0; ok && index < list->count; index += 1)
    {
        String8 path = string_from_pointer(list->paths[index]);
        int fd = path.length ? bq_open_directory_path(root, path) : openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        DIR* stream = fd >= 0 ? fdopendir(fd) : NULL;
        ok = stream != NULL;
        struct dirent* entry = NULL;
        while (ok && (entry = readdir(stream)) != NULL)
        {
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            {
                struct stat info;
                ok = fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0;
                if (ok && S_ISDIR(info.st_mode))
                {
                    u64 prefix = path.length ? path.length + 1 : 0;
                    u64 leaf = strlen(entry->d_name);
                    ok = list->count < BQ_DIRECTORY_CAP && prefix + leaf <= BQ_PATH_CAP;
                    if (ok)
                    {
                        char* next = list->paths[list->count];
                        if (path.length)
                        {
                            memcpy(next, path.pointer, (size_t)path.length);
                            next[path.length] = '/';
                        }
                        memcpy(next + prefix, entry->d_name, leaf + 1);
                        list->count += 1;
                    }
                }
            }
        }
        if (stream)
        {
            closedir(stream);
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_make_sources_read_only(int root)
{
    BqDirectoryList list;
    bool ok = bq_collect_directories(root, &list);
    for (u32 i = list.count; ok && i > 0; i -= 1)
    {
        String8 path = string_from_pointer(list.paths[i - 1]);
        int fd = path.length ? bq_open_directory_path(root, path) : openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        ok = fd >= 0 && fchmod(fd, 0550) == 0 && fsync(fd) == 0;
        if (fd >= 0)
        {
            close(fd);
        }
    }
    return ok;
}

typedef struct BqCleanupFrame
{
    DIR* stream;
    dev_t device;
    ino_t inode;
    char name[256];
    bool dirty;
} BqCleanupFrame;

BUSTER_GLOBAL_LOCAL bool bq_entry_identity(int parent, char const* name, dev_t device, ino_t inode)
{
    struct stat info;
    bool ok = fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(info.st_mode) &&
              info.st_dev == device && info.st_ino == inode;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_remove_workspace_payload(int workspace)
{
    BqCleanupFrame frames[BQ_CLEANUP_DEPTH_CAP];
    memset(frames, 0, sizeof(frames));
    int root = openat(workspace, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat root_info;
    bool ok = root >= 0 && fstat(root, &root_info) == 0;
    frames[0].stream = ok ? fdopendir(root) : NULL;
    if (!frames[0].stream && root >= 0)
    {
        close(root);
    }
    if (ok)
    {
        frames[0].device = root_info.st_dev;
        frames[0].inode = root_info.st_ino;
        ok = frames[0].stream != NULL && fchmod(dirfd(frames[0].stream), 0700) == 0;
    }
    if (!ok && frames[0].stream)
    {
        closedir(frames[0].stream);
        frames[0].stream = NULL;
    }
    u32 depth = ok ? 1 : 0;
    u32 directories = depth;
    u32 entries = 0;
    while (ok && depth)
    {
        BqCleanupFrame* frame = frames + depth - 1;
        errno = 0;
        struct dirent* entry = readdir(frame->stream);
        if (entry)
        {
            int directory = dirfd(frame->stream);
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") &&
                !(depth == 1 && !strcmp(entry->d_name, ".identity")))
            {
                entries += 1;
                ok = entries <= BQ_CLEANUP_ENTRY_CAP;
                struct stat info;
                ok = ok && fstatat(directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0;
                if (ok && S_ISDIR(info.st_mode))
                {
                    ok = depth < BQ_CLEANUP_DEPTH_CAP && directories < BQ_DIRECTORY_CAP;
                    int child = ok ? openat(directory, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
                    struct stat child_info;
                    ok = child >= 0 && fstat(child, &child_info) == 0 && child_info.st_dev == info.st_dev &&
                         child_info.st_ino == info.st_ino;
                    DIR* stream = ok ? fdopendir(child) : NULL;
                    if (!stream && child >= 0)
                    {
                        close(child);
                    }
                    ok = stream != NULL && fchmod(dirfd(stream), 0700) == 0;
                    if (ok)
                    {
                        BqCleanupFrame* next = frames + depth;
                        next->stream = stream;
                        next->device = info.st_dev;
                        next->inode = info.st_ino;
                        size_t length = strlen(entry->d_name);
                        ok = length < sizeof(next->name);
                        if (ok)
                        {
                            memcpy(next->name, entry->d_name, length + 1);
                            depth += 1;
                            directories += 1;
                        }
                    }
                    if (!ok && stream)
                    {
                        closedir(stream);
                    }
                }
                else if (ok)
                {
                    ok = unlinkat(directory, entry->d_name, 0) == 0;
                    frame->dirty = frame->dirty || ok;
                }
            }
        }
        else if (errno != 0)
        {
            ok = false;
        }
        else
        {
            int directory = dirfd(frame->stream);
            if (frame->dirty)
            {
                ok = fsync(directory) == 0;
            }
            if (ok && depth > 1)
            {
                BqCleanupFrame* parent = frames + depth - 2;
                int parent_fd = dirfd(parent->stream);
                ok = bq_entry_identity(parent_fd, frame->name, frame->device, frame->inode) &&
                     unlinkat(parent_fd, frame->name, AT_REMOVEDIR) == 0 && fsync(parent_fd) == 0;
                if (ok)
                {
                    parent->dirty = true;
                }
            }
            closedir(frame->stream);
            frame->stream = NULL;
            depth -= 1;
        }
    }
    while (depth)
    {
        closedir(frames[depth - 1].stream);
        depth -= 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_remove_workspace(int parent, char const* name, int workspace, dev_t device, ino_t inode)
{
    bool ok = bq_remove_workspace_payload(workspace) && bq_entry_identity(parent, name, device, inode);
    if (ok && unlinkat(workspace, ".identity", 0) != 0)
    {
        ok = errno == ENOENT;
    }
    ok = ok && fsync(workspace) == 0 && bq_entry_identity(parent, name, device, inode) &&
         unlinkat(parent, name, AT_REMOVEDIR) == 0 && fsync(parent) == 0;
    return ok;
}

bool bq_workspace_name(char result[64], u64 id, u64 token)
{
    int length = snprintf(result, 64, "job-%" PRIu64 "-attempt-%" PRIu64, (uint64_t)id, (uint64_t)token);
    bool ok = length > 0 && length < 64;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_seal_bytes(BqJob const* job, char bytes[512], u32* size)
{
    String8 recipe = bq_field(&job->request, 2);
    String8 base = bq_field(&job->request, 3);
    String8 candidate = bq_field(&job->request, 4);
    char number[32];
    int job_length = snprintf(number, sizeof(number), "%" PRIu64, (uint64_t)job->id);
    u32 used = 0;
    bool ok = job_length > 0 && (u32)job_length < sizeof(number) &&
              bq_workspace_append(bytes, 512, &used, "BQ-WORKSPACE-V1\njob=", sizeof("BQ-WORKSPACE-V1\njob=") - 1) &&
              bq_workspace_append(bytes, 512, &used, number, (u32)job_length) &&
              bq_workspace_append(bytes, 512, &used, "\ntoken=", sizeof("\ntoken=") - 1);
    int token_length = ok ? snprintf(number, sizeof(number), "%" PRIu64, (uint64_t)job->token) : -1;
    ok = ok && token_length > 0 && (u32)token_length < sizeof(number) &&
         bq_workspace_append(bytes, 512, &used, number, (u32)token_length) &&
         bq_workspace_append(bytes, 512, &used, "\nrequest=", sizeof("\nrequest=") - 1) &&
         bq_workspace_append(bytes, 512, &used, job->digest, SHA256_HEX_CAPACITY - 1) &&
         bq_workspace_append(bytes, 512, &used, "\nrecipe=", sizeof("\nrecipe=") - 1) &&
         bq_workspace_append(bytes, 512, &used, (char const*)recipe.pointer, recipe.length) &&
         bq_workspace_append(bytes, 512, &used, "\nbase=", sizeof("\nbase=") - 1) &&
         bq_workspace_append(bytes, 512, &used, (char const*)base.pointer, base.length) &&
         bq_workspace_append(bytes, 512, &used, "\ncandidate=", sizeof("\ncandidate=") - 1) &&
         bq_workspace_append(bytes, 512, &used, (char const*)candidate.pointer, candidate.length) &&
         bq_workspace_append(bytes, 512, &used, "\n", sizeof("\n") - 1);
    *size = ok ? used : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_seal(int workspace, BqJob const* job, bool create)
{
    char expected[512];
    u32 expected_size = 0;
    bool ok = bq_workspace_seal_bytes(job, expected, &expected_size);
    int fd = ok ? openat(workspace, ".identity", (create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY | O_NONBLOCK) |
                         O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    if (create)
    {
        ok = fd >= 0 && bq_write_all(fd, (u8 const*)expected, expected_size) && fsync(fd) == 0 && fsync(workspace) == 0;
    }
    else
    {
        u8 actual[512];
        u32 actual_size = 0;
        struct stat info;
        ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
             info.st_uid == geteuid() && (info.st_mode & 0222) == 0 &&
             bq_read_file(fd, actual, sizeof(actual), &actual_size) && actual_size == expected_size && !memcmp(actual, expected, expected_size);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_record_name(char result[48], char const* prefix, u64 id)
{
    int length = snprintf(result, 48, "%s-%" PRIu64, prefix, (uint64_t)id);
    bool ok = length > 0 && length < 48;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_record_read(BqQueue* queue, char const* name, u8* bytes, u32 capacity, u32* size)
{
    int fd = openat(queue->directory_fd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    BqError error = BQ_NOT_FOUND;
    if (fd >= 0)
    {
        struct stat info;
        error = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_uid == geteuid() &&
                (info.st_mode & 0222) == 0 &&
                bq_read_file(fd, bytes, capacity, size) ? BQ_OK : BQ_CORRUPT;
        close(fd);
    }
    else if (errno != ENOENT)
    {
        error = BQ_IO;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_record_write(BqQueue* queue, char const* name, u8 const* bytes, u32 size, bool existing_ok)
{
    int fd = openat(queue->directory_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
    BqError error = BQ_IO;
    if (fd >= 0)
    {
        if (bq_write_all(fd, bytes, size) && fsync(fd) == 0 && fsync(queue->directory_fd) == 0)
        {
            error = BQ_OK;
        }
        close(fd);
    }
    else if (errno == EEXIST && existing_ok)
    {
        u8 actual[640];
        u32 actual_size = 0;
        error = bq_record_read(queue, name, actual, sizeof(actual), &actual_size);
        if (error == BQ_OK && (actual_size != size || memcmp(actual, bytes, size)))
        {
            error = BQ_CORRUPT;
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_attempt_bytes(BqJob const* job, String8 installed_path, String8 workspace_path,
                                           struct stat const* installed, struct stat const* workspaces,
                                           u8 bytes[640], u32* size)
{
    u32 fixed = 136;
    bool ok = installed_path.length <= BQ_PATH_CAP && workspace_path.length <= BQ_PATH_CAP &&
              fixed + installed_path.length + workspace_path.length <= 640;
    if (ok)
    {
        memset(bytes, 0, 640);
        memcpy(bytes, "BQATTEMPT0000001", 16);
        bq_put64(bytes + 16, job->id);
        bq_put64(bytes + 24, job->token);
        bq_put64(bytes + 32, (u64)workspaces->st_dev);
        bq_put64(bytes + 40, (u64)workspaces->st_ino);
        bq_put64(bytes + 48, (u64)installed->st_dev);
        bq_put64(bytes + 56, (u64)installed->st_ino);
        bq_put32(bytes + 64, (u32)installed_path.length);
        bq_put32(bytes + 68, (u32)workspace_path.length);
        memcpy(bytes + 72, job->digest, 64);
        memcpy(bytes + fixed, installed_path.pointer, (size_t)installed_path.length);
        memcpy(bytes + fixed + installed_path.length, workspace_path.pointer, (size_t)workspace_path.length);
        *size = fixed + (u32)installed_path.length + (u32)workspace_path.length;
    }
    else
    {
        *size = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_attempt_write(BqQueue* queue, BqJob const* job, String8 installed_path,
                                              String8 workspace_path, struct stat const* installed,
                                              struct stat const* workspaces)
{
    char name[48];
    u8 bytes[640] = {0};
    u32 size = 0;
    BqError error = bq_record_name(name, "attempt", job->id) &&
                    bq_attempt_bytes(job, installed_path, workspace_path, installed, workspaces, bytes, &size) ?
                    bq_record_write(queue, name, bytes, size, false) : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_attempt_validate(BqQueue* queue, BqJob const* job, String8 workspace_path,
                                                 struct stat const* workspaces)
{
    char name[48], checked[BQ_PATH_CAP + 1];
    u8 bytes[640] = {0};
    u32 size = 0;
    BqError error = bq_record_name(name, "attempt", job->id) ? bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    if (error == BQ_OK)
    {
        u32 installed_length = size >= 72 ? bq_u32(bytes + 64) : BQ_PATH_CAP + 1;
        u32 workspace_length = size >= 72 ? bq_u32(bytes + 68) : BQ_PATH_CAP + 1;
        String8 installed_path = {(char8*)bytes + 136, installed_length};
        bool ok = size >= 136 && installed_length <= BQ_PATH_CAP && workspace_length <= BQ_PATH_CAP &&
                  size == 136 + installed_length + workspace_length && !memcmp(bytes, "BQATTEMPT0000001", 16) &&
                  bq_u64(bytes + 16) == job->id && bq_u64(bytes + 24) == job->token &&
                  bq_u64(bytes + 32) == (u64)workspaces->st_dev && bq_u64(bytes + 40) == (u64)workspaces->st_ino &&
                  !memcmp(bytes + 72, job->digest, 64) && bq_string_path(installed_path, checked) &&
                  workspace_length == workspace_path.length &&
                  !memcmp(bytes + 136 + installed_length, workspace_path.pointer, (size_t)workspace_length);
        error = ok ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    }
    else if (error == BQ_NOT_FOUND)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_attempt_presence(BqQueue* queue, BqJob const* job)
{
    char name[48];
    u8 bytes[640];
    u32 size = 0;
    BqError error = bq_record_name(name, "attempt", job->id) ?
                    bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_cleanup_bytes(BqJob const* job, struct stat const* workspaces,
                                           struct stat const* workspace, u8 bytes[128])
{
    memset(bytes, 0, 128);
    memcpy(bytes, "BQCLEANUP0000001", 16);
    bq_put64(bytes + 16, job->id);
    bq_put64(bytes + 24, job->token);
    bq_put64(bytes + 32, (u64)workspaces->st_dev);
    bq_put64(bytes + 40, (u64)workspaces->st_ino);
    bq_put64(bytes + 48, (u64)workspace->st_dev);
    bq_put64(bytes + 56, (u64)workspace->st_ino);
    memcpy(bytes + 64, job->digest, 64);
    return true;
}

BUSTER_GLOBAL_LOCAL BqError bq_cleanup_record(BqQueue* queue, BqJob const* job, struct stat const* workspaces,
                                               struct stat const* workspace, bool create)
{
    char name[48];
    u8 expected[128], actual[128];
    u32 actual_size = 0;
    bq_cleanup_bytes(job, workspaces, workspace, expected);
    BqError error = !bq_record_name(name, "cleanup", job->id) ? BQ_IO : create ?
                    bq_record_write(queue, name, expected, sizeof(expected), true) :
                    bq_record_read(queue, name, actual, sizeof(actual), &actual_size);
    if (error == BQ_OK && !create && (actual_size != sizeof(expected) || memcmp(actual, expected, sizeof(expected))))
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_cleanup_record_missing(BqQueue* queue, BqJob const* job,
                                                       struct stat const* workspaces)
{
    char name[48];
    u8 actual[128];
    u32 size = 0;
    BqError error = bq_record_name(name, "cleanup", job->id) ?
                    bq_record_read(queue, name, actual, sizeof(actual), &size) : BQ_CORRUPT;
    if (error == BQ_OK)
    {
        bool ok = size == sizeof(actual) && !memcmp(actual, "BQCLEANUP0000001", 16) &&
                  bq_u64(actual + 16) == job->id && bq_u64(actual + 24) == job->token &&
                  bq_u64(actual + 32) == (u64)workspaces->st_dev && bq_u64(actual + 40) == (u64)workspaces->st_ino &&
                  !memcmp(actual + 64, job->digest, 64);
        error = ok ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_failure_bytes(BqJob const* job, BqError reason, char bytes[384], u32* size)
{
    int length = snprintf(bytes, 384, "BQ-FAILURE-V1\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\nreason=%s\n",
                          (uint64_t)job->id, (uint64_t)job->token, job->digest, bq_error_name(reason));
    bool ok = length > 0 && length < 384;
    *size = ok ? (u32)length : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_failure_name(char result[48], u64 id)
{
    bool ok = bq_record_name(result, "failure", id);
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_failure_write(BqQueue* queue, BqJob const* job, BqError reason)
{
    char name[48], expected[384];
    u32 expected_size = 0;
    bool ok = bq_failure_name(name, job->id) && bq_failure_bytes(job, reason, expected, &expected_size);
    BqError error = ok ? bq_record_write(queue, name, (u8 const*)expected, expected_size, true) : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_cleanup_failure_write(BqQueue* queue, BqJob const* job)
{
    char name[48], expected[384];
    u32 expected_size = 0;
    BqError error = bq_record_name(name, "cleanup-failure", job->id) &&
                    bq_failure_bytes(job, BQ_CLEANUP_FAILED, expected, &expected_size) ?
                    bq_record_write(queue, name, (u8 const*)expected, expected_size, true) : BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_failure_record(BqQueue* queue, BqJob const* job, char const* prefix,
                                               BqError first, BqError last)
{
    BqError result = BQ_NOT_FOUND;
    char name[48];
    if (job && bq_record_name(name, prefix, job->id))
    {
        u8 actual[384];
        u32 actual_size = 0;
        result = bq_record_read(queue, name, actual, sizeof(actual), &actual_size);
        if (result == BQ_OK)
        {
            result = BQ_CORRUPT;
            for (u32 reason = first; reason <= (u32)last && result == BQ_CORRUPT; reason += 1)
            {
                char expected[384];
                u32 expected_size = 0;
                if (bq_failure_bytes(job, (BqError)reason, expected, &expected_size) && actual_size == expected_size &&
                    !memcmp(actual, expected, expected_size))
                {
                    result = (BqError)reason;
                }
            }
        }
    }
    return result;
}

BqError bq_failure_evidence(BqQueue* queue, BqJob const* job)
{
    BqError result = bq_failure_record(queue, job, "cleanup-failure", BQ_CLEANUP_FAILED, BQ_CLEANUP_FAILED);
    if (result == BQ_NOT_FOUND)
    {
        result = bq_failure_record(queue, job, "failure", BQ_RECIPE_MISMATCH, BQ_BOOT_INTERRUPTED);
    }
    if (result == BQ_NOT_FOUND && job && bq_recipe_real(&job->request) && job->outcome == BQ_FAILED)
    {
        result = BQ_CORRUPT;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_real_advance(BqQueue* queue, BqJob const* job, BqPhase phase, BqOutcome outcome)
{
    u8 body[24];
    bq_put64(body, job->id);
    bq_put64(body + 8, job->token);
    bq_put32(body + 16, phase);
    bq_put32(body + 20, outcome);
    BqError result = bq_append(queue, BQ_ADVANCE, body, sizeof(body));
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_installed_recipe(int installed, BqRecipe selected)
{
    BqRecipeFiles files;
    String8 expected = bq_recipe_profile(selected);
    bool described = bq_recipe_service(selected) && bq_recipe_files(selected, &files) && expected.length > 0 &&
                     expected.length <= BQ_RECIPE_PROFILE_CAP;
    int recipes = openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int recipe = described && recipes >= 0 ?
                 openat(recipes, files.profile, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info;
    u8 bytes[BQ_RECIPE_PROFILE_CAP + 1];
    u32 size = 0;
    bool ok = described && recipes >= 0 && bq_owned_directory(recipes, false, true) && recipe >= 0 &&
              fstat(recipe, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
              bq_read_file(recipe, bytes, sizeof(bytes), &size) && size == expected.length &&
              !memcmp(bytes, expected.pointer, expected.length);
    if (recipe >= 0)
    {
        close(recipe);
    }
    if (recipes >= 0)
    {
        close(recipes);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_materialization_finish_failure(BqQueue* queue, BqJob const* job, BqError reason,
                                                               int workspaces, char const* name, int workspace,
                                                               struct stat const* workspaces_info,
                                                               struct stat const* workspace_info, bool created)
{
    BqError error = bq_failure_write(queue, job, reason);
    if (error == BQ_OK && created)
    {
        error = workspace >= 0 && bq_workspace_seal(workspace, job, false) ?
                bq_cleanup_record(queue, job, workspaces_info, workspace_info, true) : BQ_WORKSPACE_MISMATCH;
    }
    if (error == BQ_OK)
    {
        error = bq_real_advance(queue, job, BQ_CLEANING, BQ_FAILED);
    }
    if (error == BQ_OK && created && !bq_remove_workspace(workspaces, name, workspace, workspace_info->st_dev, workspace_info->st_ino))
    {
        error = BQ_CLEANUP_FAILED;
        if (bq_cleanup_failure_write(queue, bq_job(&queue->state, job->id)) != BQ_OK)
        {
            error = BQ_IO;
        }
        queue->needs_reconciliation = true;
    }
    if (error == BQ_OK)
    {
        BqJob* current = bq_job(&queue->state, job->id);
        error = bq_real_advance(queue, current, BQ_FINISHED, BQ_FAILED);
    }
    if (error != BQ_OK)
    {
        queue->needs_reconciliation = true;
    }
    return error;
}

BqError bq_materialize(BqQueue* queue, String8 installed_root, String8 workspace_root, u64* id, u64* token)
{
    *id = 0;
    *token = 0;
    BqError error = queue->poisoned ? BQ_IO : queue->needs_reconciliation ? BQ_RECONCILIATION_REQUIRED : BQ_NOT_FOUND;
    for (u32 i = 0; error == BQ_NOT_FOUND && i < queue->state.job_count; i += 1)
    {
        if (queue->state.jobs[i].phase == BQ_QUEUED)
        {
            error = bq_recipe_real(&queue->state.jobs[i].request) ? BQ_OK : BQ_UNSUPPORTED;
        }
    }
    char installed_path[BQ_PATH_CAP + 1], workspaces_path[BQ_PATH_CAP + 1];
    if (error == BQ_OK && (!bq_string_path(installed_root, installed_path) || !bq_string_path(workspace_root, workspaces_path)))
    {
        error = BQ_BAD_REQUEST;
    }
    if (error == BQ_OK)
    {
        error = bq_reserve(queue, id, token);
    }
    BqJob* job = error == BQ_OK ? bq_job(&queue->state, *id) : NULL;
    bool retirement = job && string_equal(bq_field(&job->request, 2), S8("native-retirement-performance-v1"));
    bool retirement_started = false;
    u32 completed_subjects = 0;
    BqRetirementPreparation preparation = {0};
    int installed = error == BQ_OK ? bq_open_absolute_directory(installed_root) : -1;
    int workspaces = error == BQ_OK ? bq_open_absolute_directory(workspace_root) : -1;
    struct stat installed_info = {0}, workspaces_info = {0}, workspace_info = {0};
    char name[64] = {0};
    bool created = false;
    bool collision = false;
    if (error == BQ_OK && (installed < 0 || workspaces < 0 || fstat(installed, &installed_info) != 0 ||
                           fstat(workspaces, &workspaces_info) != 0 || !bq_owned_directory(installed, false, true) ||
                           !bq_workspace_root_directory(workspaces) || !bq_workspace_name(name, *id, *token)))
    {
        error = BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK)
    {
        error = bq_attempt_write(queue, job, installed_root, workspace_root, &installed_info, &workspaces_info);
    }
    if (error == BQ_OK && !bq_installed_recipe(installed, bq_request_recipe(&job->request)))
    {
        error = BQ_RECIPE_MISMATCH;
    }
    if (error == BQ_OK && retirement)
    {
        retirement_started = true;
        error = bq_retirement_preflight(installed, workspaces, &job->request, &preparation);
    }
    if (error == BQ_OK)
    {
        created = mkdirat(workspaces, name, BQ_WORKSPACE_TRAVERSE_MODE) == 0;
        if (created && fchmodat(workspaces, name, BQ_WORKSPACE_TRAVERSE_MODE, 0) != 0)
        {
            created = false;
        }
        if (!created)
        {
            collision = errno == EEXIST;
            error = BQ_WORKSPACE_MISMATCH;
        }
    }
    int workspace = created ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (error == BQ_OK && (workspace < 0 || fstat(workspace, &workspace_info) != 0 ||
                           !bq_workspace_seal(workspace, job, true) || fsync(workspaces) != 0))
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    char const* subjects[] = {"base", "candidate"};
    for (u32 subject = 0; error == BQ_OK && subject < 2; subject += 1)
    {
        mode_t subject_mode = subject == 0 ? 02750 : BQ_WORKSPACE_TRAVERSE_MODE;
        mode_t build_mode = BQ_WORKSPACE_PRIVATE_BUILD_MODE;
        bool made = mkdirat(workspace, subjects[subject], subject_mode) == 0;
        if (made && fchmodat(workspace, subjects[subject], subject_mode, 0) != 0)
        {
            made = false;
        }
        int subject_fd = made ? openat(workspace, subjects[subject], O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        made = subject_fd >= 0 && mkdirat(subject_fd, "source", 02750) == 0 &&
               fchmodat(subject_fd, "source", 02750, 0) == 0 && mkdirat(subject_fd, "build", build_mode) == 0 &&
               fchmodat(subject_fd, "build", build_mode, 0) == 0;
        int source = made ? openat(subject_fd, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        if (made)
        {
            String8 revision = bq_field(&job->request, 3 + subject);
            made = bq_copy_manifest(installed, source, revision,
                                    retirement ? BQ_RETIREMENT_SOURCE_MANIFEST_CAP : BQ_SOURCE_MANIFEST_CAP) &&
                   bq_make_sources_read_only(source) &&
                   (!retirement || bq_retirement_verify_subject(installed, subject_fd, source, revision,
                                                                &preparation.subjects[subject])) && fsync(subject_fd) == 0;
        }
        if (source >= 0)
        {
            close(source);
        }
        if (subject_fd >= 0)
        {
            close(subject_fd);
        }
        if (!made)
        {
            error = BQ_SOURCE_MISMATCH;
        }
        else
        {
            completed_subjects += 1;
        }
    }
    if (error == BQ_OK && fsync(workspace) != 0)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    if (retirement_started && !bq_retirement_preparation_record(queue, job, &preparation,
                                                                error, completed_subjects))
    {
        error = BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK)
    {
        error = bq_real_advance(queue, job, BQ_PREPARING, BQ_NO_OUTCOME);
    }
    BqError reported = error;
    if (((error >= BQ_RECIPE_MISMATCH && error <= BQ_CONFIGURATION_MISMATCH) || error == BQ_RESOURCE_MISMATCH) &&
        !collision)
    {
        if (!created || workspace >= 0)
        {
            BqError finish = bq_materialization_finish_failure(queue, job, error, workspaces, name, workspace,
                                                                &workspaces_info, &workspace_info, created);
            if (finish != BQ_OK)
            {
                error = finish;
            }
            else
            {
                error = reported;
            }
        }
        else
        {
            BqError evidence = bq_failure_write(queue, job, error);
            if (evidence != BQ_OK)
            {
                error = evidence;
            }
            queue->needs_reconciliation = true;
        }
    }
    else if (collision)
    {
        BqError evidence = bq_failure_write(queue, job, BQ_WORKSPACE_MISMATCH);
        if (evidence != BQ_OK)
        {
            error = evidence;
        }
        queue->needs_reconciliation = true;
    }
    if (workspace >= 0)
    {
        close(workspace);
    }
    if (job && error != BQ_OK && queue->state.active_id == job->id)
    {
        queue->needs_reconciliation = true;
    }
    if (workspaces >= 0)
    {
        close(workspaces);
    }
    if (installed >= 0)
    {
        close(installed);
    }
    return error;
}

typedef BqError (*BqWorkspaceBeforeTerminal)(BqQueue*, BqJob*, void*);

BUSTER_GLOBAL_LOCAL BqError bq_workspace_reconcile_controlled(BqQueue* queue, String8 workspace_root,
                                                               u64 id, u64 token,
                                                               BqOutcome terminal_outcome,
                                                               BqWorkspaceBeforeTerminal before_terminal,
                                                               void* terminal_context)
{
    BqJob* job = bq_job(&queue->state, id);
    bool was_reconciling = queue->needs_reconciliation;
    BqError error = queue->poisoned ? BQ_IO : !was_reconciling ? BQ_INVALID_TRANSITION :
                    !job ? BQ_NOT_FOUND : !bq_recipe_real(&job->request) ? BQ_UNSUPPORTED :
                    queue->state.active_id != id || job->token != token ? BQ_INVALID_TRANSITION : BQ_OK;
    char path[BQ_PATH_CAP + 1], name[64];
    if (error == BQ_OK && (!bq_string_path(workspace_root, path) || !bq_workspace_name(name, id, token)))
    {
        error = BQ_BAD_REQUEST;
    }
    BqError failure = error == BQ_OK ? bq_failure_evidence(queue, job) : BQ_NOT_FOUND;
    if (error == BQ_OK && failure != BQ_NOT_FOUND && failure != BQ_RECIPE_MISMATCH &&
        failure != BQ_SOURCE_MISMATCH && failure != BQ_WORKSPACE_MISMATCH &&
        failure != BQ_CLEANUP_FAILED && failure != BQ_CONFIGURATION_MISMATCH &&
        failure != BQ_WORKER_MISMATCH && failure != BQ_RESOURCE_MISMATCH &&
        failure != BQ_WORKER_FAILED && failure != BQ_WORKER_OOM_FAILURE &&
        failure != BQ_WORKER_TIMEOUT && failure != BQ_WORKER_INTERRUPTED &&
        failure != BQ_BOOT_INTERRUPTED)
    {
        error = failure;
    }
    BqError attempt = error == BQ_OK ? bq_attempt_presence(queue, job) : BQ_NOT_FOUND;
    bool has_attempt = attempt == BQ_OK;
    if (error == BQ_OK && attempt != BQ_OK && attempt != BQ_NOT_FOUND)
    {
        error = attempt;
    }
    int workspaces = error == BQ_OK ? bq_open_absolute_directory(workspace_root) : -1;
    struct stat workspaces_info = {0}, workspace_info = {0};
    if (error == BQ_OK && (workspaces < 0 || fstat(workspaces, &workspaces_info) != 0 ||
                           !bq_workspace_reconcile_root_directory(workspaces)))
    {
        bool recoverable_configuration = !has_attempt && failure == BQ_CONFIGURATION_MISMATCH &&
                                         (job->phase == BQ_RESERVED || job->phase == BQ_CLEANING);
        error = recoverable_configuration ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK && has_attempt)
    {
        error = bq_attempt_validate(queue, job, workspace_root, &workspaces_info);
    }
    errno = 0;
    int workspace = error == BQ_OK && workspaces >= 0 ?
                    openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int workspace_error = errno;
    bool exists = workspace >= 0;
    if (error == BQ_OK && workspaces >= 0 && !exists && workspace_error != ENOENT)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    if (error == BQ_OK && !has_attempt && (exists || (job->phase != BQ_RESERVED && job->phase != BQ_CLEANING) ||
                                           (job->phase == BQ_CLEANING && failure != BQ_CONFIGURATION_MISMATCH) ||
                                           (failure != BQ_NOT_FOUND && failure != BQ_CONFIGURATION_MISMATCH)))
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    if (error == BQ_OK && exists && fstat(workspace, &workspace_info) != 0)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    BqError cleanup = BQ_NOT_FOUND;
    if (error == BQ_OK && has_attempt && exists)
    {
        cleanup = bq_cleanup_record(queue, job, &workspaces_info, &workspace_info, false);
        bool seal = bq_workspace_seal(workspace, job, false);
        if (!seal)
        {
            struct stat seal_info;
            errno = 0;
            bool missing_seal = fstatat(workspace, ".identity", &seal_info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
            if (!missing_seal || cleanup != BQ_OK)
            {
                error = cleanup != BQ_NOT_FOUND && cleanup != BQ_OK ? cleanup : BQ_WORKSPACE_MISMATCH;
            }
        }
        else if (cleanup != BQ_OK && cleanup != BQ_NOT_FOUND)
        {
            error = cleanup;
        }
    }
    if (error == BQ_OK && has_attempt && !exists)
    {
        cleanup = bq_cleanup_record_missing(queue, job, &workspaces_info);
        bool absent_before_cleanup = cleanup == BQ_NOT_FOUND && job->phase == BQ_RESERVED && failure != BQ_NOT_FOUND;
        if (cleanup != BQ_OK && !absent_before_cleanup)
        {
            error = cleanup == BQ_NOT_FOUND ? BQ_WORKSPACE_MISMATCH : cleanup;
        }
    }
    if (error == BQ_OK && has_attempt && exists && cleanup == BQ_NOT_FOUND)
    {
        error = bq_cleanup_record(queue, job, &workspaces_info, &workspace_info, true);
        cleanup = error == BQ_OK ? BQ_OK : cleanup;
    }
    if (error == BQ_OK && job->phase == BQ_RESERVED && failure != BQ_NOT_FOUND)
    {
        BqOutcome outcome = job->cancel_requested ? BQ_CANCELLED : BQ_FAILED;
        error = bq_real_advance(queue, job, BQ_CLEANING, outcome);
        job = bq_job(&queue->state, id);
    }
    if (error == BQ_OK && exists && !bq_remove_workspace(workspaces, name, workspace,
                                                          workspace_info.st_dev, workspace_info.st_ino))
    {
        error = BQ_CLEANUP_FAILED;
        if (bq_cleanup_failure_write(queue, job) != BQ_OK)
        {
            error = BQ_IO;
        }
    }
    if (workspace >= 0)
    {
        close(workspace);
    }
    if (error == BQ_OK && before_terminal)
    {
        error = before_terminal(queue, job, terminal_context);
        job = bq_job(&queue->state, id);
        if (!job) error = BQ_CORRUPT;
    }
    if (error == BQ_OK && job->phase == BQ_CLEANING)
    {
        BqOutcome final_outcome = job->cancel_requested ? BQ_CANCELLED :
                                  terminal_outcome != BQ_NO_OUTCOME ? terminal_outcome : job->outcome;
        error = job->outcome == BQ_FAILED && failure == BQ_NOT_FOUND ? BQ_CORRUPT :
                bq_real_advance(queue, job, BQ_FINISHED, final_outcome);
    }
    else if (error == BQ_OK)
    {
        u8 body[16];
        bq_put64(body, id);
        bq_put64(body + 8, token);
        error = bq_append(queue, BQ_RECONCILE, body, sizeof(body));
    }
    if (error == BQ_OK)
    {
        queue->needs_reconciliation = false;
    }
    else if (was_reconciling)
    {
        queue->needs_reconciliation = true;
    }
    if (workspaces >= 0)
    {
        close(workspaces);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_workspace_reconcile_outcome(BqQueue* queue, String8 workspace_root,
                                                            u64 id, u64 token,
                                                            BqOutcome terminal_outcome)
{
    BqError error = bq_workspace_reconcile_controlled(queue, workspace_root, id, token,
                                                       terminal_outcome, NULL, NULL);
    return error;
}

BqError bq_workspace_reconcile(BqQueue* queue, String8 workspace_root, u64 id, u64 token)
{
    BqError error = bq_workspace_reconcile_outcome(queue, workspace_root, id, token, BQ_NO_OUTCOME);
    return error;
}

#include "retirement_prepare.c"
#include "retirement_binaries.c"
#include "retirement_matched_build.c"

#else
bool bq_workspace_name(char result[64], u64 id, u64 token)
{
    (void)result;
    (void)id;
    (void)token;
    return false;
}

BqError bq_failure_evidence(BqQueue* queue, BqJob const* job)
{
    (void)queue;
    (void)job;
    return BQ_UNSUPPORTED;
}

BqError bq_materialize(BqQueue* queue, String8 installed_root, String8 workspace_root, u64* id, u64* token)
{
    (void)queue;
    (void)installed_root;
    (void)workspace_root;
    *id = 0;
    *token = 0;
    return BQ_UNSUPPORTED;
}

BqError bq_workspace_reconcile(BqQueue* queue, String8 workspace_root, u64 id, u64 token)
{
    (void)queue;
    (void)workspace_root;
    (void)id;
    (void)token;
    return BQ_UNSUPPORTED;
}
#endif
