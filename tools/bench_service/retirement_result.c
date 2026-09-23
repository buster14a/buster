/* #1023: Linux service-owned immutable evidence store.
 * tp_retirement_store_begin anchors a pending file to a private directory;
 * publish syncs its bytes, links the final name without replacement, syncs
 * both directory transitions and independently rereads the sealed inode.
 * validate reopens every file before service completion or receipt handoff.
 * The owner supplies the external job/attempt/plan/context authority through
 * the private phase channel; bundle bytes cannot authorize themselves.
 */
#define _GNU_SOURCE 1
#include "../throughput/retirement_store.h"
#ifdef __linux__
#include <buster/lib/hash.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef BUSTER_RETIREMENT_STORE_TEST
unsigned tp_retirement_store_test_sync_calls, tp_retirement_store_test_fail_sync;
#endif

BUSTER_GLOBAL_LOCAL int tp_retirement_store_sync(int fd)
{
    int result;
#ifdef BUSTER_RETIREMENT_STORE_TEST
    ++tp_retirement_store_test_sync_calls;
    if (tp_retirement_store_test_sync_calls == tp_retirement_store_test_fail_sync)
    {
        errno = EIO;
        result = -1;
    }
    else
#endif
        result = fsync(fd);
    return result;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_digest(char const* digest)
{
    int valid = digest && strnlen(digest, 65) == 64;
    for (unsigned i = 0; valid && i < 64; ++i)
        valid = (digest[i] >= '0' && digest[i] <= '9') || (digest[i] >= 'a' && digest[i] <= 'f');
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_token(char const* value)
{
    size_t length = value ? strnlen(value, 129) : 0;
    int valid = length && length <= 128;
    for (size_t i = 0; valid && i < length; ++i)
        valid = (value[i] >= 'a' && value[i] <= 'z') || (value[i] >= 'A' && value[i] <= 'Z') ||
                (value[i] >= '0' && value[i] <= '9') || value[i] == '-' || value[i] == '_';
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_path(char const* path)
{
    size_t length = path ? strnlen(path, TP_RETIREMENT_STORE_PATH_BYTES + 1) : 0;
    int valid = length && length <= TP_RETIREMENT_STORE_PATH_BYTES;
    size_t component = 0;
    unsigned depth = 0;
    for (size_t i = 0; valid && i <= length; ++i)
    {
        if (i == length || path[i] == '/')
        {
            size_t size = i - component;
            valid = size && !(size == 1 && path[component] == '.') &&
                    !(size == 2 && path[component] == '.' && path[component + 1] == '.') && ++depth < 256;
            component = i + 1;
        }
        else
        {
            char value = path[i];
            valid = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.';
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_directory(struct stat const* info)
{
    int valid = info && S_ISDIR(info->st_mode) && info->st_uid == geteuid() &&
                (info->st_mode & 0077) == 0 && (info->st_mode & S_IWUSR) != 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_root(TpRetirementStore const* store)
{
    struct stat info = {0};
    int valid = store && store->root >= 0 && fstat(store->root, &info) == 0 &&
                tp_retirement_store_directory(&info) && info.st_dev == store->root_identity.st_dev &&
                info.st_ino == store->root_identity.st_ino;
    return valid;
}

/* Traverse each existing directory from the service descriptor. A replaced
 * path component fails even when the replacement has private permissions. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_parent(TpRetirementStore const* store,
                                                    char const* path, char leaf[193],
                                                    struct stat* identity)
{
    int parent = tp_retirement_store_root(store) && tp_retirement_store_path(path) ?
                 fcntl(store->root, F_DUPFD_CLOEXEC, 3) : -1;
    size_t length = parent >= 0 ? strlen(path) : 0;
    size_t start = 0;
    for (size_t end = 0; parent >= 0 && end <= length; ++end)
    {
        if (end == length || path[end] == '/')
        {
            size_t count = end - start;
            char component[TP_RETIREMENT_STORE_PATH_BYTES + 1];
            memcpy(component, path + start, count);
            component[count] = 0;
            if (end == length)
            {
                memcpy(leaf, component, count + 1);
                if (fstat(parent, identity) != 0 || !tp_retirement_store_directory(identity))
                {
                    close(parent);
                    parent = -1;
                }
            }
            else
            {
                struct stat named = {0}, opened = {0};
                int next = openat(parent, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                int valid = next >= 0 && fstatat(parent, component, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                            fstat(next, &opened) == 0 && tp_retirement_store_directory(&opened) &&
                            named.st_dev == opened.st_dev && named.st_ino == opened.st_ino;
                close(parent);
                parent = valid ? next : -1;
                if (!valid && next >= 0) close(next);
            }
            start = end + 1;
        }
    }
    return parent;
}

int tp_retirement_store_open(TpRetirementStore* store, int root,
                             TpRetirementStoredFile* workspace, unsigned capacity)
{
    int valid = store && root >= 0 && workspace && capacity && capacity <= TP_RETIREMENT_STORE_FILES;
    if (store)
    {
        *store = (TpRetirementStore){.root = -1, .failed = !valid};
        if (valid)
        {
            store->root = fcntl(root, F_DUPFD_CLOEXEC, 3);
            valid = store->root >= 0 && fstat(store->root, &store->root_identity) == 0 &&
                    tp_retirement_store_directory(&store->root_identity);
            if (valid)
            {
                store->files = workspace;
                store->capacity = capacity;
            }
            else
            {
                if (store->root >= 0) close(store->root);
                store->root = -1;
                store->failed = 1;
            }
        }
    }
    return valid;
}

int tp_retirement_store_plan(TpRetirementStore* store, unsigned owned_files,
                             unsigned external_entries, uint64_t external_bytes)
{
    int valid = store && !store->failed && !store->planned && !store->active &&
                !store->count && owned_files && owned_files <= store->capacity &&
                external_entries >= 3 && external_entries <= TP_RETIREMENT_STORE_FILES - owned_files &&
                external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES &&
                tp_retirement_store_root(store);
    if (valid)
    {
        store->planned_files = owned_files;
        store->external_entries = external_entries;
        store->external_bytes = external_bytes;
        store->planned = 1;
    }
    else if (store) store->failed = 1;
    return valid;
}

int tp_retirement_store_begin(TpRetirementStore* store, char const* path,
                              uint64_t limit, TpRetirementPending* pending)
{
    char leaf[193] = {0};
    struct stat directory = {0};
    int valid = store && !store->failed && !store->active && pending && path &&
                limit && limit <= TP_RETIREMENT_STORE_FILE_BYTES &&
                store->count < store->capacity &&
                (!store->planned || store->count < store->planned_files);
    if (pending) *pending = (TpRetirementPending){0};
    for (unsigned i = 0; valid && i < store->count; ++i)
        valid = strcmp(store->files[i].path, path) != 0;
    int parent = valid ? tp_retirement_store_parent(store, path, leaf, &directory) : -1;
    valid = parent >= 0 && strlen(leaf) + sizeof(".pending") <= sizeof(pending->temporary);
    if (valid)
    {
        memcpy(pending->temporary, leaf, strlen(leaf) + 1);
        strcat(pending->temporary, ".pending");
        int fd = openat(parent, pending->temporary, O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
        valid = fd >= 0;
        if (valid)
        {
            /* Persist the pending directory entry before giving the writer a
             * stream. A crash never licenses reuse of this attempt name. */
            valid = tp_retirement_store_sync(parent) == 0;
            if (valid) pending->stream = fdopen(fd, "w+b");
            valid = valid && pending->stream;
            if (!pending->stream) close(fd);
        }
    }
    if (valid)
    {
        memcpy(pending->path, path, strlen(path) + 1);
        pending->parent_device = directory.st_dev;
        pending->parent_inode = directory.st_ino;
        pending->limit = limit;
        store->active = 1;
    }
    else if (store) store->failed = 1;
    if (parent >= 0) close(parent);
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_file(TpRetirementStore const* store,
                                                  TpRetirementStoredFile const* file,
                                                  uint64_t* lines)
{
    char leaf[193] = {0};
    struct stat parent_info = {0}, named = {0}, before = {0}, after = {0}, final = {0};
    int parent = tp_retirement_store_parent(store, file->path, leaf, &parent_info);
    int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
    int valid = fd >= 0 && parent_info.st_dev == file->parent_device &&
                parent_info.st_ino == file->parent_inode &&
                fstatat(parent, leaf, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                fstat(fd, &before) == 0 && S_ISREG(before.st_mode) &&
                before.st_dev == file->device && before.st_ino == file->inode &&
                before.st_uid == file->owner && before.st_uid == geteuid() &&
                before.st_nlink == 1 && (before.st_mode & 0777) == 0400 &&
                before.st_size >= 0 && (uint64_t)before.st_size == file->bytes &&
                named.st_dev == before.st_dev && named.st_ino == before.st_ino;
    uint64_t size = 0;
    uint64_t newlines = 0;
    unsigned char last = 0;
    Sha256 hash;
    sha256_init(&hash);
    unsigned char buffer[65536];
    while (valid && size < file->bytes)
    {
        size_t want = file->bytes - size < sizeof(buffer) ? (size_t)(file->bytes - size) : sizeof(buffer);
        ssize_t read_bytes = read(fd, buffer, want);
        if (read_bytes < 0 && errno == EINTR) continue;
        valid = read_bytes > 0;
        if (valid)
        {
            sha256_add(&hash, buffer, (u64)read_bytes);
            if (lines)
            {
                for (ssize_t i = 0; i < read_bytes; ++i) newlines += buffer[i] == '\n';
                last = buffer[read_bytes - 1];
            }
            size += (uint64_t)read_bytes;
        }
    }
    if (valid)
    {
        char extra = 0, digest[65];
        valid = read(fd, &extra, 1) == 0 && fstat(fd, &after) == 0 &&
                fstatat(parent, leaf, &final, AT_SYMLINK_NOFOLLOW) == 0 &&
                before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
                before.st_size == after.st_size && before.st_mode == after.st_mode &&
                before.st_uid == after.st_uid && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
                before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
                before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
                before.st_ctim.tv_nsec == after.st_ctim.tv_nsec &&
                final.st_dev == before.st_dev && final.st_ino == before.st_ino &&
                final.st_size == before.st_size && final.st_nlink == 1;
        if (valid)
        {
            sha256_finish_hex(&hash, (char8*)digest);
            valid = !strcmp(digest, file->sha256);
        }
    }
    if (fd >= 0 && close(fd) != 0) valid = 0;
    if (parent >= 0 && close(parent) != 0) valid = 0;
    if (lines) *lines = valid && last == '\n' ? newlines : 0;
    return valid;
}

int tp_retirement_store_publish(TpRetirementStore* store, TpRetirementPending* pending,
                                uint64_t bytes, char const* sha256)
{
    int valid = store && !store->failed && store->active && pending && pending->stream &&
                tp_retirement_store_digest(sha256) && bytes <= pending->limit &&
                store->external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES &&
                store->total <= TP_RETIREMENT_STORE_TOTAL_BYTES - store->external_bytes &&
                bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES - store->external_bytes - store->total;
    char leaf[193] = {0};
    struct stat parent_info = {0}, temporary = {0}, named = {0};
    int fd = pending && pending->stream ? fileno(pending->stream) : -1;
    if (valid) valid = fflush(pending->stream) == 0 && !ferror(pending->stream) &&
                       fstat(fd, &temporary) == 0 && S_ISREG(temporary.st_mode) &&
                       temporary.st_nlink == 1 && temporary.st_uid == geteuid() &&
                       temporary.st_size >= 0 && (uint64_t)temporary.st_size == bytes &&
                       fchmod(fd, 0400) == 0 && tp_retirement_store_sync(fd) == 0;
    if (pending && pending->stream)
    {
        if (fclose(pending->stream) != 0) valid = 0;
        pending->stream = NULL;
    }
    int parent = valid ? tp_retirement_store_parent(store, pending->path, leaf, &parent_info) : -1;
    valid = parent >= 0 && parent_info.st_dev == pending->parent_device &&
            parent_info.st_ino == pending->parent_inode &&
            fstatat(parent, pending->temporary, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(named.st_mode) && named.st_dev == temporary.st_dev && named.st_ino == temporary.st_ino &&
            named.st_nlink == 1 && named.st_uid == geteuid() && named.st_size == (off_t)bytes &&
            (named.st_mode & 0777) == 0400;
    if (valid) valid = linkat(parent, pending->temporary, parent, leaf, 0) == 0;
    if (valid) valid = tp_retirement_store_sync(parent) == 0;
    if (valid) valid = unlinkat(parent, pending->temporary, 0) == 0;
    if (valid) valid = tp_retirement_store_sync(parent) == 0;
    TpRetirementStoredFile published = {0};
    if (valid)
    {
        memcpy(published.path, pending->path, strlen(pending->path) + 1);
        memcpy(published.sha256, sha256, 65);
        published.bytes = bytes;
        published.device = temporary.st_dev;
        published.inode = temporary.st_ino;
        published.parent_device = parent_info.st_dev;
        published.parent_inode = parent_info.st_ino;
        published.owner = temporary.st_uid;
    }
    if (parent >= 0 && close(parent) != 0) valid = 0;
    if (valid) valid = tp_retirement_store_file(store, &published, NULL);
    if (valid)
    {
        store->files[store->count++] = published;
        store->total += bytes;
    }
    else if (store) store->failed = 1;
    if (store) store->active = 0;
    return valid;
}

void tp_retirement_store_abort(TpRetirementStore* store, TpRetirementPending* pending)
{
    if (pending && pending->stream)
    {
        fclose(pending->stream);
        pending->stream = NULL;
    }
    if (store)
    {
        store->failed = 1;
        store->active = 0;
    }
}

int tp_retirement_store_validate(TpRetirementStore* store)
{
    int valid = store && !store->failed && !store->active &&
                store->count && store->count <= store->capacity &&
                tp_retirement_store_root(store);
    uint64_t total = 0;
    for (unsigned i = 0; valid && i < store->count; ++i)
    {
        TpRetirementStoredFile const* entry = store->files + i;
        valid = entry->bytes <= TP_RETIREMENT_STORE_FILE_BYTES &&
                total <= TP_RETIREMENT_STORE_TOTAL_BYTES - entry->bytes &&
                tp_retirement_store_file(store, entry, NULL);
        if (valid) total += entry->bytes;
        for (unsigned j = 0; valid && j < i; ++j)
            valid = strcmp(store->files[j].path, entry->path) != 0;
    }
    valid = valid && total == store->total &&
            (!store->planned || (store->count == store->planned_files &&
             store->external_entries <= TP_RETIREMENT_STORE_FILES - store->count &&
             store->external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES - total));
    if (store && !valid) store->failed = 1;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_receipt_literal(char const* bytes, size_t size,
                                                             size_t* offset, char const* literal)
{
    size_t length = strlen(literal);
    int valid = *offset <= size && length <= size - *offset &&
                !memcmp(bytes + *offset, literal, length);
    if (valid) *offset += length;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_receipt_number(char const* bytes, size_t size,
                                                            size_t* offset, uint64_t* output)
{
    size_t start = *offset;
    uint64_t value = 0;
    int valid = start < size && bytes[start] >= '0' && bytes[start] <= '9';
    while (valid && *offset < size && bytes[*offset] >= '0' && bytes[*offset] <= '9')
    {
        unsigned digit = (unsigned)(bytes[*offset] - '0');
        valid = value <= (UINT64_MAX - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
            ++*offset;
        }
    }
    valid = valid && (*offset == start + 1 || bytes[start] != '0');
    if (valid) *output = value;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_receipt_text(char const* bytes, size_t size,
                                                          size_t* offset, char* output, size_t capacity)
{
    size_t start = *offset;
    while (*offset < size && bytes[*offset] != '"' && *offset - start < capacity) ++*offset;
    size_t length = *offset - start;
    int valid = length && length < capacity && *offset < size && bytes[*offset] == '"' &&
                !memchr(bytes + start, 0, length);
    if (valid)
    {
        memcpy(output, bytes + start, length);
        output[length] = 0;
        ++*offset;
    }
    return valid;
}

/* Reject self-consistent but unbound candidate receipts before writing any
 * private service reference. This accepts only the existing encoder's sorted,
 * unescaped JSON and joins every shard to a previously sealed store entry. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_receipt_validate(TpRetirementStore* store,
    char const* job, uint64_t attempt, char const* plan, char const* context)
{
    TpRetirementStoredFile const* receipt = NULL;
    for (unsigned i = 0; store && i < store->count; ++i)
        if (!strcmp(store->files[i].path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH)) receipt = store->files + i;
    int valid = receipt && receipt->bytes && receipt->bytes <= UINT64_C(1048576);
    char leaf[193] = {0};
    struct stat parent_info = {0};
    int parent = valid ? tp_retirement_store_parent(store, receipt->path, leaf, &parent_info) : -1;
    int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
    valid = valid && fd >= 0 && parent_info.st_dev == receipt->parent_device &&
            parent_info.st_ino == receipt->parent_inode;
    char* bytes = valid ? mmap(NULL, (size_t)receipt->bytes, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    valid = valid && bytes != MAP_FAILED;
    size_t used = 0;
    Sha256 hash;
    sha256_init(&hash);
    while (valid && used < receipt->bytes)
    {
        ssize_t count = read(fd, bytes + used, (size_t)(receipt->bytes - used));
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0;
        if (valid)
        {
            sha256_add(&hash, bytes + used, (u64)count);
            used += (size_t)count;
        }
    }
    if (valid)
    {
        char digest[65], extra;
        sha256_finish_hex(&hash, (char8*)digest);
        valid = !strcmp(digest, receipt->sha256) && read(fd, &extra, 1) == 0;
    }
    if (fd >= 0 && close(fd) != 0) valid = 0;
    if (parent >= 0 && close(parent) != 0) valid = 0;

    size_t offset = 0;
    char expected[256], token[129];
    int length = valid ? snprintf(expected, sizeof(expected), "{\"attempt\":%" PRIu64 ",\"boot_id\":\"", attempt) : -1;
    valid = valid && length > 0 && (size_t)length < sizeof(expected) &&
            tp_retirement_store_receipt_literal(bytes, used, &offset, expected) &&
            tp_retirement_store_receipt_text(bytes, used, &offset, token, sizeof(token)) &&
            tp_retirement_store_token(token) &&
            tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"bound_at_ns\":");
    uint64_t bound = 0, completed = 0, invocations = 0, records = 0;
    if (valid) valid = tp_retirement_store_receipt_number(bytes, used, &offset, &bound) && bound &&
                       tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"completed_at_ns\":") &&
                       tp_retirement_store_receipt_number(bytes, used, &offset, &completed) && completed > bound;
    length = valid ? snprintf(expected, sizeof(expected),
        ",\"context_sha256\":\"%s\",\"execution_plan_sha256\":\"%s\",\"invocations\":", context, plan) : -1;
    valid = valid && length > 0 && (size_t)length < sizeof(expected) &&
            tp_retirement_store_receipt_literal(bytes, used, &offset, expected) &&
            tp_retirement_store_receipt_number(bytes, used, &offset, &invocations) && invocations;
    length = valid ? snprintf(expected, sizeof(expected),
        ",\"job_id\":\"%s\",\"schema\":\"buster-native-retirement-execution-receipt-v1\",\"shards\":[", job) : -1;
    valid = valid && length > 0 && (size_t)length < sizeof(expected) &&
            tp_retirement_store_receipt_literal(bytes, used, &offset, expected);
    unsigned shards = 0;
    uint64_t previous_records = 32768;
    char previous[TP_RETIREMENT_STORE_PATH_BYTES + 1] = {0};
    while (valid && offset < used && bytes[offset] == '{' && shards < store->count)
    {
        uint64_t shard_bytes = 0, shard_records = 0;
        char path[TP_RETIREMENT_STORE_PATH_BYTES + 1], shard_digest[65];
        valid = tp_retirement_store_receipt_literal(bytes, used, &offset, "{\"bytes\":") &&
                tp_retirement_store_receipt_number(bytes, used, &offset, &shard_bytes) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"path\":\"") &&
                tp_retirement_store_receipt_text(bytes, used, &offset, path, sizeof(path)) &&
                tp_retirement_store_path(path) &&
                strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) &&
                (!shards || (strcmp(previous, path) < 0 && previous_records == 32768)) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"records\":") &&
                tp_retirement_store_receipt_number(bytes, used, &offset, &shard_records) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"sha256\":\"") &&
                tp_retirement_store_receipt_text(bytes, used, &offset, shard_digest, sizeof(shard_digest)) &&
                tp_retirement_store_digest(shard_digest) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, "}") &&
                shard_bytes && shard_bytes <= TP_RETIREMENT_STORE_FILE_BYTES &&
                shard_records && shard_records <= 32768 &&
                records <= invocations && shard_records <= invocations - records;
        unsigned matches = 0;
        for (unsigned i = 0; valid && i < store->count; ++i)
            if (!strcmp(store->files[i].path, path) && store->files[i].bytes == shard_bytes &&
                !strcmp(store->files[i].sha256, shard_digest))
            {
                uint64_t lines = 0;
                if (tp_retirement_store_file(store, store->files + i, &lines) && lines == shard_records)
                    ++matches;
            }
        valid = valid && matches == 1;
        if (valid)
        {
            records += shard_records;
            previous_records = shard_records;
            strcpy(previous, path);
            ++shards;
            if (offset < used && bytes[offset] == ',') ++offset;
            else break;
        }
    }
    valid = valid && shards && records == invocations &&
            tp_retirement_store_receipt_literal(bytes, used, &offset, "],\"version\":1}\n") &&
            offset == used && tp_retirement_store_validate(store);
    if (bytes != MAP_FAILED) munmap(bytes, (size_t)receipt->bytes);
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_authority_bytes(TpRetirementReceiptAuthority const* authority,
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], char body[512], size_t* length)
{
    int valid = authority && tp_retirement_store_token(authority->job) && authority->attempt &&
                tp_retirement_store_digest(authority->plan_sha256) &&
                tp_retirement_store_digest(authority->context_sha256) &&
                tp_retirement_store_digest(authority->receipt_sha256);
    int count = valid ? snprintf(name, TP_RETIREMENT_STORE_PATH_BYTES + 1,
                                 "authority-%s-%" PRIu64 ".txt", authority->job, authority->attempt) : -1;
    valid = valid && count > 0 && count <= (int)TP_RETIREMENT_STORE_PATH_BYTES - 8;
    count = valid ? snprintf(body, 512, "BQ-RETIREMENT-AUTHORITY-V1\n%s\n%" PRIu64 "\n%s\n%s\n%s\n",
        authority->job, authority->attempt, authority->plan_sha256,
        authority->context_sha256, authority->receipt_sha256) : -1;
    valid = valid && count > 0 && count < 512;
    if (length) *length = valid ? (size_t)count : 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_private_root(TpRetirementStore const* store,
                                                          TpRetirementStore const* private_store)
{
    int valid = tp_retirement_store_root(private_store) &&
                (store->root_identity.st_dev != private_store->root_identity.st_dev ||
                 store->root_identity.st_ino != private_store->root_identity.st_ino);
    return valid;
}

int tp_retirement_store_receipt_authority(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority* authority)
{
    int valid = store && store->planned && !store->authority_issued &&
                path && !strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) && authority &&
                tp_retirement_store_token(job) && attempt &&
                tp_retirement_store_digest(plan_sha256) && tp_retirement_store_digest(context_sha256) &&
                tp_retirement_store_validate(store);
    TpRetirementStoredFile const* receipt = NULL;
    for (unsigned i = 0; valid && i < store->count; ++i)
        if (!strcmp(store->files[i].path, path)) receipt = store->files + i;
    valid = valid && receipt &&
            tp_retirement_store_receipt_validate(store, job, attempt, plan_sha256, context_sha256);
    if (authority) *authority = (TpRetirementReceiptAuthority){0};
    if (valid)
    {
        strcpy(authority->job, job);
        strcpy(authority->plan_sha256, plan_sha256);
        strcpy(authority->context_sha256, context_sha256);
        strcpy(authority->receipt_sha256, receipt->sha256);
        authority->attempt = attempt;
        char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[512], digest[65];
        size_t length = 0;
        valid = tp_retirement_store_authority_bytes(authority, name, body, &length);
        TpRetirementStore private_store;
        TpRetirementStoredFile private_file;
        int opened = valid && tp_retirement_store_open(&private_store, authority_root, &private_file, 1);
        if (opened) valid = tp_retirement_store_private_root(store, &private_store);
        TpRetirementPending pending;
        if (valid) valid = tp_retirement_store_begin(&private_store, name, sizeof(body), &pending);
        if (valid) valid = fwrite(body, 1, length, pending.stream) == length;
        if (valid)
        {
            Sha256 hash;
            sha256_init(&hash);
            sha256_add(&hash, body, length);
            sha256_finish_hex(&hash, (char8*)digest);
            valid = tp_retirement_store_publish(&private_store, &pending, length, digest) &&
                    tp_retirement_store_validate(&private_store);
        }
        else if (opened && private_store.active) tp_retirement_store_abort(&private_store, &pending);
        if (opened) tp_retirement_store_close(&private_store);
        if (valid)
        {
            strcpy(authority->authority_sha256, digest);
            store->authority_issued = 1;
        }
    }
    if (store && !valid) store->failed = 1;
    if (authority && !valid) *authority = (TpRetirementReceiptAuthority){0};
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_reopen_file(TpRetirementStore* store, char const* path,
                                                        char const* digest, uint64_t maximum)
{
    struct stat info = {0};
    int valid = store && tp_retirement_store_root(store) && tp_retirement_store_path(path) &&
                tp_retirement_store_digest(digest) &&
                fstatat(store->root, path, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISREG(info.st_mode) && info.st_size > 0 && (uint64_t)info.st_size <= maximum;
    if (valid)
    {
        TpRetirementStoredFile file = {0};
        strcpy(file.path, path);
        strcpy(file.sha256, digest);
        file.bytes = (uint64_t)info.st_size;
        file.device = info.st_dev;
        file.inode = info.st_ino;
        file.owner = info.st_uid;
        file.parent_device = store->root_identity.st_dev;
        file.parent_inode = store->root_identity.st_ino;
        valid = tp_retirement_store_file(store, &file, NULL);
    }
    return valid;
}

int tp_retirement_store_authority_reopen(int result_root, int authority_root,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority const* trusted)
{
    int valid = trusted && tp_retirement_store_token(job) && attempt &&
                tp_retirement_store_digest(plan_sha256) && tp_retirement_store_digest(context_sha256) &&
                tp_retirement_store_token(trusted->job) &&
                tp_retirement_store_digest(trusted->plan_sha256) &&
                tp_retirement_store_digest(trusted->context_sha256) &&
                tp_retirement_store_digest(trusted->receipt_sha256) &&
                tp_retirement_store_digest(trusted->authority_sha256) &&
                !strcmp(trusted->job, job) && trusted->attempt == attempt &&
                !strcmp(trusted->plan_sha256, plan_sha256) &&
                !strcmp(trusted->context_sha256, context_sha256);
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[512], expected[65];
    size_t length = 0;
    if (valid) valid = tp_retirement_store_authority_bytes(trusted, name, body, &length);
    if (valid)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, body, length);
        sha256_finish_hex(&hash, (char8*)expected);
        valid = !strcmp(expected, trusted->authority_sha256);
    }
    TpRetirementStore result_store, private_store;
    TpRetirementStoredFile result_file, private_file;
    int result_open = valid && tp_retirement_store_open(&result_store, result_root, &result_file, 1);
    int private_open = result_open && tp_retirement_store_open(&private_store, authority_root, &private_file, 1);
    valid = private_open && tp_retirement_store_private_root(&result_store, &private_store);
    if (valid) valid = tp_retirement_store_reopen_file(&private_store, name, expected, length) &&
                       tp_retirement_store_reopen_file(&result_store, TP_RETIREMENT_EXECUTION_RECEIPT_PATH,
                                                       trusted->receipt_sha256, UINT64_C(1048576));
    if (private_open) tp_retirement_store_close(&private_store);
    if (result_open) tp_retirement_store_close(&result_store);
    return valid;
}

int tp_retirement_store_authority_matches(TpRetirementStore* store, int authority_root, char const* path,
    char const* job, uint64_t attempt, char const* plan_sha256, char const* context_sha256,
    TpRetirementReceiptAuthority const* trusted)
{
    int valid = store && trusted && path && !strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) &&
                tp_retirement_store_validate(store);
    unsigned found = 0;
    for (unsigned i = 0; valid && i < store->count; ++i)
        if (!strcmp(store->files[i].path, path) &&
            !strcmp(store->files[i].sha256, trusted->receipt_sha256)) ++found;
    valid = valid && found == 1 &&
            tp_retirement_store_receipt_validate(store, job, attempt, plan_sha256, context_sha256) &&
            tp_retirement_store_authority_reopen(store->root, authority_root,
                job, attempt, plan_sha256, context_sha256, trusted);
    if (store && !valid) store->failed = 1;
    return valid;
}

void tp_retirement_store_close(TpRetirementStore* store)
{
    if (store && store->root >= 0)
    {
        if (close(store->root) != 0 || store->active) store->failed = 1;
        store->root = -1;
    }
}
#endif
