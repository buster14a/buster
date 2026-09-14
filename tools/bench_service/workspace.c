/* Installed-input and per-attempt workspace boundary for #437.
 *
 * Entry points: bq_materialize reserves the existing FIFO authority before it
 * validates/copies an installed source snapshot; bq_workspace_reconcile removes
 * only a seal-matched attempt before allowing that authority to finish.
 *
 * Layout: installed recipe/source validation, bounded manifest copying,
 * immutable workspace identity/failure evidence, and fail-closed cleanup. This
 * is not a process supervisor, build recipe runner, transport, or host lease.
 */
#include "queue.h"
BUSTER_GLOBAL_LOCAL char const bq_real_recipe[] =
    "schema=1\n"
    "recipe=validate-buster-v1\n"
    "repository=buster14a/buster\n"
    "source-manifest=BQ-SOURCE-V1\n"
    "layout=separate-source-build-v1\n";

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#define BQ_SOURCE_MANIFEST_CAP (64u * 1024u)
#define BQ_SOURCE_FILE_CAP (64u * 1024u * 1024u)
#define BQ_SOURCE_TOTAL_CAP (512u * 1024u * 1024u)
#define BQ_SOURCE_COUNT_CAP 4096u
#define BQ_DIRECTORY_CAP 1024u

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

BUSTER_GLOBAL_LOCAL int bq_open_directory_path(int root, String8 path, bool create)
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
        if (create && mkdirat(current, name, 0700) != 0 && errno != EEXIST)
        {
            close(current);
            current = -1;
        }
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

BUSTER_GLOBAL_LOCAL int bq_open_source_file(int root, String8 path)
{
    u64 slash = path.length;
    while (slash && path.pointer[slash - 1] != '/')
    {
        slash -= 1;
    }
    String8 directory = slash ? (String8){path.pointer, slash - 1} : (String8){0};
    String8 leaf = {path.pointer + slash, path.length - slash};
    int parent = directory.length ? bq_open_directory_path(root, directory, false) : dup(root);
    int result = -1;
    if (parent >= 0)
    {
        char name[BQ_PATH_CAP + 1];
        memcpy(name, leaf.pointer, (size_t)leaf.length);
        name[leaf.length] = 0;
        result = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        close(parent);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_create_destination_file(int root, String8 path)
{
    u64 slash = path.length;
    while (slash && path.pointer[slash - 1] != '/')
    {
        slash -= 1;
    }
    String8 directory = slash ? (String8){path.pointer, slash - 1} : (String8){0};
    String8 leaf = {path.pointer + slash, path.length - slash};
    int parent = directory.length ? bq_open_directory_path(root, directory, true) : dup(root);
    int result = -1;
    if (parent >= 0)
    {
        char name[BQ_PATH_CAP + 1];
        memcpy(name, leaf.pointer, (size_t)leaf.length);
        name[leaf.length] = 0;
        result = openat(parent, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
        close(parent);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_copy_verified_file(int source_root, int destination_root, String8 path, String8 expected, u64* total)
{
    int source = bq_open_source_file(source_root, path);
    int destination = -1;
    struct stat info;
    bool ok = source >= 0 && fstat(source, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              (info.st_mode & 0222) == 0 && info.st_size >= 0 && (u64)info.st_size <= BQ_SOURCE_FILE_CAP &&
              (u64)info.st_size <= BQ_SOURCE_TOTAL_CAP - *total;
    if (ok)
    {
        destination = bq_create_destination_file(destination_root, path);
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
    int length = snprintf(expected, sizeof(expected), "revision=%.*s", (int)revision.length, revision.pointer);
    bool ok = bq_next_line(manifest, offset, &line) && string_equal(line, S8("BQ-SOURCE-V1")) &&
              bq_next_line(manifest, offset, &line) && string_equal(line, S8("repository=buster14a/buster")) &&
              length > 0 && (u32)length < sizeof(expected) && bq_next_line(manifest, offset, &line) &&
              line.length == (u64)length && !memcmp(line.pointer, expected, (size_t)length);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_copy_manifest(int installed, int destination, String8 revision)
{
    char name[80];
    int length = snprintf(name, sizeof(name), "sources/%.*s", (int)revision.length, revision.pointer);
    int source_root = length > 0 && (u32)length < sizeof(name) ? bq_open_directory_path(installed, string_from_pointer(name), false) : -1;
    int manifest_fd = source_root >= 0 ? openat(source_root, "source.manifest", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info;
    u8 manifest_bytes[BQ_SOURCE_MANIFEST_CAP];
    u32 manifest_size = 0;
    bool ok = source_root >= 0 && bq_owned_directory(source_root, false, true) && manifest_fd >= 0 &&
              fstat(manifest_fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
              bq_read_file(manifest_fd, manifest_bytes, sizeof(manifest_bytes), &manifest_size);
    String8 manifest = {(char8*)manifest_bytes, manifest_size};
    u64 offset = 0;
    if (ok)
    {
        ok = bq_manifest_header(manifest, revision, &offset);
    }
    int copy = ok ? openat(destination, ".source-manifest", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
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
                 bq_copy_verified_file(source_root, destination, path, digest, &total);
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
        int fd = path.length ? bq_open_directory_path(root, path, false) : openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
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
        int fd = path.length ? bq_open_directory_path(root, path, false) : openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        ok = fd >= 0 && fchmod(fd, 0500) == 0 && fsync(fd) == 0;
        if (fd >= 0)
        {
            close(fd);
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_remove_tree_at(int parent, char const* name)
{
    int root = openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BqDirectoryList list;
    bool ok = root >= 0 && bq_collect_directories(root, &list);
    for (u32 index = 0; ok && index < list.count; index += 1)
    {
        String8 path = string_from_pointer(list.paths[index]);
        int fd = path.length ? bq_open_directory_path(root, path, false) : openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        ok = fd >= 0 && fchmod(fd, 0700) == 0;
        DIR* stream = ok ? fdopendir(fd) : NULL;
        if (!stream && fd >= 0)
        {
            close(fd);
        }
        ok = stream != NULL;
        struct dirent* entry = NULL;
        while (ok && (entry = readdir(stream)) != NULL)
        {
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            {
                struct stat info;
                ok = fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0;
                if (ok && !S_ISDIR(info.st_mode))
                {
                    ok = unlinkat(fd, entry->d_name, 0) == 0;
                }
            }
        }
        if (stream)
        {
            closedir(stream);
        }
    }
    for (u32 i = list.count; ok && i > 1; i -= 1)
    {
        ok = unlinkat(root, list.paths[i - 1], AT_REMOVEDIR) == 0;
    }
    if (root >= 0)
    {
        close(root);
    }
    if (ok)
    {
        ok = unlinkat(parent, name, AT_REMOVEDIR) == 0 && fsync(parent) == 0;
    }
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
    String8 base = bq_field(&job->request, 3);
    String8 candidate = bq_field(&job->request, 4);
    int length = snprintf(bytes, 512, "BQ-WORKSPACE-V1\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\nrecipe=validate-buster-v1\nbase=%.*s\ncandidate=%.*s\n",
                          (uint64_t)job->id, (uint64_t)job->token, job->digest, (int)base.length, base.pointer,
                          (int)candidate.length, candidate.pointer);
    bool ok = length > 0 && length < 512;
    *size = ok ? (u32)length : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_workspace_seal(int workspace, BqJob const* job, bool create)
{
    char expected[512];
    u32 expected_size = 0;
    bool ok = bq_workspace_seal_bytes(job, expected, &expected_size);
    int fd = ok ? openat(workspace, ".identity", (create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    if (create)
    {
        ok = fd >= 0 && bq_write_all(fd, (u8 const*)expected, expected_size) && fsync(fd) == 0 && fsync(workspace) == 0;
    }
    else
    {
        u8 actual[512];
        u32 actual_size = 0;
        struct stat info;
        ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
             bq_read_file(fd, actual, sizeof(actual), &actual_size) && actual_size == expected_size && !memcmp(actual, expected, expected_size);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return ok;
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
    int length = snprintf(result, 48, "failure-%" PRIu64, (uint64_t)id);
    bool ok = length > 0 && length < 48;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_failure_write(BqQueue* queue, BqJob const* job, BqError reason)
{
    char name[48], expected[384];
    u32 expected_size = 0;
    bool ok = bq_failure_name(name, job->id) && bq_failure_bytes(job, reason, expected, &expected_size);
    int fd = ok ? openat(queue->directory_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    BqError error = BQ_IO;
    if (fd >= 0 && bq_write_all(fd, (u8 const*)expected, expected_size) && fsync(fd) == 0 && fsync(queue->directory_fd) == 0)
    {
        error = BQ_OK;
    }
    if (fd >= 0)
    {
        close(fd);
    }
    return error;
}

BqError bq_failure_evidence(BqQueue* queue, BqJob const* job)
{
    BqError result = BQ_NOT_FOUND;
    char name[48];
    if (job && bq_failure_name(name, job->id))
    {
        int fd = openat(queue->directory_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0)
        {
            u8 actual[384];
            u32 actual_size = 0;
            struct stat info;
            result = BQ_CORRUPT;
            if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
                bq_read_file(fd, actual, sizeof(actual), &actual_size))
            {
                for (u32 reason = BQ_RECIPE_MISMATCH; reason <= BQ_CONFIGURATION_MISMATCH && result == BQ_CORRUPT; reason += 1)
                {
                    char expected[384];
                    u32 expected_size = 0;
                    if (bq_failure_bytes(job, (BqError)reason, expected, &expected_size) && actual_size == expected_size && !memcmp(actual, expected, expected_size))
                    {
                        result = (BqError)reason;
                    }
                }
            }
            close(fd);
        }
        else if (errno != ENOENT)
        {
            result = BQ_IO;
        }
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

BUSTER_GLOBAL_LOCAL bool bq_installed_recipe(int installed)
{
    int recipes = openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int recipe = recipes >= 0 ? openat(recipes, "validate-buster-v1.recipe", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info;
    u8 bytes[sizeof(bq_real_recipe)];
    u32 size = 0;
    bool ok = recipes >= 0 && bq_owned_directory(recipes, false, true) && recipe >= 0 &&
              fstat(recipe, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 0222) == 0 &&
              bq_read_file(recipe, bytes, sizeof(bytes), &size) && size == sizeof(bq_real_recipe) - 1 &&
              !memcmp(bytes, bq_real_recipe, sizeof(bq_real_recipe) - 1);
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

BUSTER_GLOBAL_LOCAL BqError bq_materialization_finish_failure(BqQueue* queue, BqJob const* job, BqError reason, int workspaces, char const* name, bool created)
{
    BqError error = bq_failure_write(queue, job, reason);
    if (error == BQ_OK)
    {
        error = bq_real_advance(queue, job, BQ_CLEANING, BQ_FAILED);
    }
    if (error == BQ_OK && created && !bq_remove_tree_at(workspaces, name))
    {
        error = BQ_CLEANUP_FAILED;
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
    int installed = error == BQ_OK ? bq_open_absolute_directory(installed_root) : -1;
    int workspaces = error == BQ_OK ? bq_open_absolute_directory(workspace_root) : -1;
    char name[64] = {0};
    bool created = false;
    bool collision = false;
    if (error == BQ_OK && (installed < 0 || workspaces < 0 || !bq_owned_directory(installed, false, true) ||
                           !bq_owned_directory(workspaces, true, false) || !bq_workspace_name(name, *id, *token)))
    {
        error = BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK && !bq_installed_recipe(installed))
    {
        error = BQ_RECIPE_MISMATCH;
    }
    if (error == BQ_OK)
    {
        created = mkdirat(workspaces, name, 0700) == 0;
        if (!created)
        {
            collision = errno == EEXIST;
            error = BQ_WORKSPACE_MISMATCH;
        }
    }
    int workspace = created ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (error == BQ_OK && workspace < 0)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    char const* subjects[] = {"base", "candidate"};
    for (u32 subject = 0; error == BQ_OK && subject < 2; subject += 1)
    {
        bool made = mkdirat(workspace, subjects[subject], 0700) == 0;
        int subject_fd = made ? openat(workspace, subjects[subject], O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        made = subject_fd >= 0 && mkdirat(subject_fd, "source", 0700) == 0 && mkdirat(subject_fd, "build", 0700) == 0;
        int source = made ? openat(subject_fd, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        if (made)
        {
            String8 revision = bq_field(&job->request, 3 + subject);
            made = bq_copy_manifest(installed, source, revision) && bq_make_sources_read_only(source) && fsync(subject_fd) == 0;
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
    }
    if (error == BQ_OK && !bq_workspace_seal(workspace, job, true))
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    if (error == BQ_OK)
    {
        error = bq_real_advance(queue, job, BQ_PREPARING, BQ_NO_OUTCOME);
    }
    BqError reported = error;
    if (error >= BQ_RECIPE_MISMATCH && error <= BQ_CONFIGURATION_MISMATCH && !collision)
    {
        if (!created || workspace >= 0)
        {
            BqError finish = bq_materialization_finish_failure(queue, job, error, workspaces, name, created);
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

BqError bq_workspace_reconcile(BqQueue* queue, String8 workspace_root, u64 id, u64 token)
{
    BqJob* job = bq_job(&queue->state, id);
    BqError error = queue->poisoned ? BQ_IO : !job ? BQ_NOT_FOUND : !bq_recipe_real(&job->request) ? BQ_UNSUPPORTED :
                    queue->state.active_id != id || job->token != token ? BQ_INVALID_TRANSITION : BQ_OK;
    char path[BQ_PATH_CAP + 1], name[64];
    if (error == BQ_OK && (!bq_string_path(workspace_root, path) || !bq_workspace_name(name, id, token)))
    {
        error = BQ_BAD_REQUEST;
    }
    int workspaces = error == BQ_OK ? bq_open_absolute_directory(workspace_root) : -1;
    int workspace = workspaces >= 0 ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool exists = workspace >= 0;
    if (error == BQ_OK && (workspaces < 0 || !bq_owned_directory(workspaces, true, false)))
    {
        error = BQ_CONFIGURATION_MISMATCH;
    }
    if (error == BQ_OK && exists && !bq_workspace_seal(workspace, job, false))
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    if (error == BQ_OK && !exists && job->phase == BQ_PREPARING)
    {
        error = BQ_WORKSPACE_MISMATCH;
    }
    BqError failure = error == BQ_OK ? bq_failure_evidence(queue, job) : BQ_NOT_FOUND;
    if (error == BQ_OK && failure != BQ_NOT_FOUND &&
        (failure < BQ_RECIPE_MISMATCH || failure > BQ_CONFIGURATION_MISMATCH))
    {
        error = failure;
    }
    if (error == BQ_OK && job->phase == BQ_RESERVED && failure != BQ_NOT_FOUND)
    {
        error = bq_real_advance(queue, job, BQ_CLEANING, BQ_FAILED);
        job = bq_job(&queue->state, id);
    }
    if (workspace >= 0)
    {
        close(workspace);
    }
    if (error == BQ_OK && exists && !bq_remove_tree_at(workspaces, name))
    {
        error = BQ_CLEANUP_FAILED;
    }
    if (error == BQ_OK && job->phase == BQ_CLEANING)
    {
        error = job->outcome == BQ_FAILED && failure == BQ_NOT_FOUND ? BQ_CORRUPT :
                bq_real_advance(queue, job, BQ_FINISHED, job->outcome);
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
    else
    {
        queue->needs_reconciliation = true;
    }
    if (workspaces >= 0)
    {
        close(workspaces);
    }
    return error;
}

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
