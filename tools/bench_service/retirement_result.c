/* #1023: Linux service-owned immutable evidence store.
 * tp_retirement_store_begin anchors a pending file to a private directory;
 * publish syncs its bytes, links the final name without replacement, syncs
 * both directory transitions and independently rereads the sealed inode.
 * validate reopens every file before service completion or receipt handoff.
 * The owner supplies the external job/attempt/plan/context authority through
 * the private phase channel; bundle bytes cannot authorize themselves.
 * tp_retirement_store_read reopens one sealed inode for the #881-E composer,
 * tp_retirement_store_bound/tp_retirement_store_retain fix (before any
 * publication) the upper-bounded slack and the retained declaration digest,
 * tp_retirement_store_settle lowers the pre-timing reservation to the exact
 * final inventory releasing only that slack, receipt_authority binds the
 * retained manifest (tp_retirement_store_retained_verify), and
 * tp_retirement_store_authority_handoff /
 * tp_retirement_store_authority_state are the coordinator's queue-private
 * copy, journal and restart classification before its final ACK.
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

/* Private authority and journal records (seven or eight short lines). */
#define TP_RETIREMENT_STORE_AUTHORITY_BYTES 640u
#define TP_RETIREMENT_STORE_JOURNAL_BYTES 768u
/* A retained manifest line: kind, digest, byte count and store path. */
#define TP_RETIREMENT_STORE_RETAINED_KIND_BYTES 16u
#define TP_RETIREMENT_STORE_RECEIPT_BYTES_MAX UINT64_C(1048576)

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

int tp_retirement_store_plan(TpRetirementStore* store, unsigned owned_files, uint64_t owned_bytes,
                             unsigned external_entries, uint64_t external_bytes)
{
    int valid = store && !store->failed && !store->planned && !store->active &&
                !store->count && owned_files && owned_files <= store->capacity &&
                owned_files <= TP_RETIREMENT_STORE_FILES &&
                external_entries >= 3 && external_entries <= TP_RETIREMENT_STORE_FILES - owned_files &&
                owned_bytes && owned_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES &&
                external_bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES - owned_bytes &&
                tp_retirement_store_root(store);
    if (valid)
    {
        store->planned_files = owned_files;
        store->planned_bytes = owned_bytes;
        store->external_entries = external_entries;
        store->external_bytes = external_bytes;
        store->planned = 1;
    }
    else if (store) store->failed = 1;
    return valid;
}

int tp_retirement_store_bound(TpRetirementStore* store, unsigned bounded_files)
{
    int valid = store && !store->failed && store->planned && !store->active && !store->count &&
                !store->bounded_files && bounded_files <= store->planned_files;
    if (valid) store->bounded_files = bounded_files;
    else if (store) store->failed = 1;
    return valid;
}

int tp_retirement_store_retain(TpRetirementStore* store, char const* declaration_sha256)
{
    int valid = store && !store->failed && store->planned && !store->active && !store->count &&
                !store->retained_bound && tp_retirement_store_digest(declaration_sha256);
    if (valid)
    {
        memcpy(store->retained_sha256, declaration_sha256, TP_RETIREMENT_STORE_SHA256_CAPACITY);
        store->retained_bound = 1;
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
                before.st_ctim.tv_sec == file->changed.tv_sec &&
                before.st_ctim.tv_nsec == file->changed.tv_nsec &&
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
                bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES - store->external_bytes - store->total &&
                (!store->planned || (store->total <= store->planned_bytes &&
                                     bytes <= store->planned_bytes - store->total));
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
        valid = fstatat(parent, leaf, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                named.st_dev == published.device && named.st_ino == published.inode;
        if (valid) published.changed = named.st_ctim;
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
             total <= store->planned_bytes &&
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

/* A restarted consumer has no producer-side inventory. Reconstruct each
 * descriptor from the named, independently opened inode before replaying the
 * existing receipt parser. Never use bundle bytes as the authority reference. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_reopen_file(TpRetirementStore* store, char const* path,
    char const* digest, uint64_t maximum, TpRetirementStoredFile* reopened, uint64_t* lines);

/* The private authority binds inode closure at sealing time. A digest of
 * candidate file bytes alone cannot distinguish an unlinked/replaced inode. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_identity(Sha256* hash, TpRetirementStoredFile const* file)
{
    char record[512];
    int length = snprintf(record, sizeof(record), "%s\n%s\n%" PRIu64 "\n%ju\n%ju\n%ju\n%ju\n%ju\n%jd\n%jd\n",
        file->path, file->sha256, file->bytes, (uintmax_t)file->device,
        (uintmax_t)file->inode, (uintmax_t)file->parent_device,
        (uintmax_t)file->parent_inode, (uintmax_t)file->owner,
        (intmax_t)file->changed.tv_sec, (intmax_t)file->changed.tv_nsec);
    int valid = length > 0 && (size_t)length < sizeof(record);
    if (valid) sha256_add(hash, record, (u64)length);
    return valid;
}

/* Reject self-consistent but unbound candidate receipts before writing any
 * private service reference. This accepts only the existing encoder's sorted,
 * unescaped JSON and joins every shard to a previously sealed store entry. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_receipt_validate(TpRetirementStore* store,
    char const* job, uint64_t attempt, char const* plan, char const* context,
    int reopen_shards, char identity_sha256[65])
{
    TpRetirementStoredFile const* receipt = NULL;
    for (unsigned i = 0; store && i < store->count; ++i)
        if (!strcmp(store->files[i].path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH)) receipt = store->files + i;
    int valid = receipt && receipt->bytes && receipt->bytes <= TP_RETIREMENT_STORE_RECEIPT_BYTES_MAX;
    Sha256 identity;
    sha256_init(&identity);
    if (valid) valid = tp_retirement_store_identity(&identity, receipt);
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
    uint64_t previous_records = TP_RETIREMENT_STORE_RECEIPT_SHARD_RECORDS;
    char previous[TP_RETIREMENT_STORE_PATH_BYTES + 1] = {0};
    while (valid && offset < used && bytes[offset] == '{' &&
           shards < (reopen_shards ? store->capacity - 1 : store->count))
    {
        uint64_t shard_bytes = 0, shard_records = 0;
        char path[TP_RETIREMENT_STORE_PATH_BYTES + 1], shard_digest[65];
        valid = tp_retirement_store_receipt_literal(bytes, used, &offset, "{\"bytes\":") &&
                tp_retirement_store_receipt_number(bytes, used, &offset, &shard_bytes) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"path\":\"") &&
                tp_retirement_store_receipt_text(bytes, used, &offset, path, sizeof(path)) &&
                tp_retirement_store_path(path) &&
                strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) &&
                (!shards || (strcmp(previous, path) < 0 &&
                             previous_records == TP_RETIREMENT_STORE_RECEIPT_SHARD_RECORDS)) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"records\":") &&
                tp_retirement_store_receipt_number(bytes, used, &offset, &shard_records) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, ",\"sha256\":\"") &&
                tp_retirement_store_receipt_text(bytes, used, &offset, shard_digest, sizeof(shard_digest)) &&
                tp_retirement_store_digest(shard_digest) &&
                tp_retirement_store_receipt_literal(bytes, used, &offset, "}") &&
                shard_bytes && shard_bytes <= TP_RETIREMENT_STORE_FILE_BYTES &&
                shard_records && shard_records <= TP_RETIREMENT_STORE_RECEIPT_SHARD_RECORDS &&
                records <= invocations && shard_records <= invocations - records;
        if (valid && reopen_shards)
        {
            TpRetirementStoredFile* entry = store->files + store->count;
            uint64_t lines = 0;
            valid = tp_retirement_store_reopen_file(store, path, shard_digest, shard_bytes, entry, &lines) &&
                    entry->bytes == shard_bytes && lines == shard_records &&
                    store->total <= TP_RETIREMENT_STORE_TOTAL_BYTES - entry->bytes;
            if (valid)
            {
                ++store->count;
                store->total += entry->bytes;
            }
        }
        unsigned matches = 0;
        TpRetirementStoredFile const* matched = NULL;
        for (unsigned i = 0; valid && i < store->count; ++i)
            if (!strcmp(store->files[i].path, path) && store->files[i].bytes == shard_bytes &&
                !strcmp(store->files[i].sha256, shard_digest))
            {
                uint64_t lines = 0;
                if ((reopen_shards || tp_retirement_store_file(store, store->files + i, &lines)) &&
                    (reopen_shards || lines == shard_records))
                {
                    ++matches;
                    matched = store->files + i;
                }
            }
        valid = valid && matches == 1;
        if (valid)
        {
            valid = tp_retirement_store_identity(&identity, matched);
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
    if (valid && identity_sha256) sha256_finish_hex(&identity, (char8*)identity_sha256);
    if (bytes != MAP_FAILED) munmap(bytes, (size_t)receipt->bytes);
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_authority_bytes(TpRetirementReceiptAuthority const* authority,
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], char body[TP_RETIREMENT_STORE_AUTHORITY_BYTES], size_t* length)
{
    int valid = authority && tp_retirement_store_token(authority->job) && authority->attempt &&
                tp_retirement_store_digest(authority->plan_sha256) &&
                tp_retirement_store_digest(authority->context_sha256) &&
                tp_retirement_store_digest(authority->receipt_sha256) &&
                tp_retirement_store_digest(authority->identity_sha256) &&
                tp_retirement_store_digest(authority->retained_sha256);
    int count = valid ? snprintf(name, TP_RETIREMENT_STORE_PATH_BYTES + 1,
                                 "authority-%s-%" PRIu64 ".txt", authority->job, authority->attempt) : -1;
    valid = valid && count > 0 && count <= (int)TP_RETIREMENT_STORE_PATH_BYTES - 8;
    count = valid ? snprintf(body, TP_RETIREMENT_STORE_AUTHORITY_BYTES,
        "BQ-RETIREMENT-AUTHORITY-V3\n%s\n%" PRIu64 "\n%s\n%s\n%s\n%s\n%s\n",
        authority->job, authority->attempt, authority->plan_sha256, authority->context_sha256,
        authority->receipt_sha256, authority->identity_sha256, authority->retained_sha256) : -1;
    valid = valid && count > 0 && count < (int)TP_RETIREMENT_STORE_AUTHORITY_BYTES;
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

/* Read one named file after reopen_file proved its identity and digest, and
 * require the second read to hash identically. The caller munmaps. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_slurp(TpRetirementStore* store, char const* path, char const* digest,
    uint64_t maximum, char** output, size_t* output_length)
{
    TpRetirementStoredFile file = {0};
    char leaf[TP_RETIREMENT_STORE_PATH_BYTES + 1] = {0};
    struct stat directory = {0};
    int valid = tp_retirement_store_reopen_file(store, path, digest, maximum, &file, NULL);
    int parent = valid ? tp_retirement_store_parent(store, path, leaf, &directory) : -1;
    int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
    char* bytes = fd >= 0 ? mmap(NULL, (size_t)file.bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) :
                  MAP_FAILED;
    valid = bytes != MAP_FAILED;
    size_t used = 0;
    while (valid && used < file.bytes)
    {
        ssize_t count = read(fd, bytes + used, (size_t)(file.bytes - used));
        if (count < 0 && errno == EINTR) continue;
        valid = count > 0;
        if (valid) used += (size_t)count;
    }
    if (valid)
    {
        char again[TP_RETIREMENT_STORE_SHA256_CAPACITY], extra = 0;
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, bytes, (u64)used);
        sha256_finish_hex(&hash, (char8*)again);
        valid = read(fd, &extra, 1) == 0 && !strcmp(again, digest);
    }
    if (fd >= 0 && close(fd) != 0) valid = 0;
    if (parent >= 0 && close(parent) != 0) valid = 0;
    if (!valid && bytes != MAP_FAILED) munmap(bytes, (size_t)file.bytes);
    *output = valid ? bytes : NULL;
    *output_length = valid ? used : 0;
    return valid;
}

/* The composer's retained manifest: a header, then per retained file
 * `kind sha256 bytes path` in strictly increasing path order (possibly none:
 * the header alone still binds an empty retained set). live joins each
 * line to the producer's sealed store entries; otherwise every listed file is
 * independently reopened from its name. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_retained_verify(TpRetirementStore* store, char const* bytes,
                                                             size_t length, int live)
{
    size_t header = strlen(TP_RETIREMENT_RETAINED_MANIFEST_HEADER);
    int valid = bytes && length >= header && !memcmp(bytes, TP_RETIREMENT_RETAINED_MANIFEST_HEADER, header) &&
                bytes[length - 1] == '\n' && !memchr(bytes, 0, length);
    size_t offset = header;
    char previous[TP_RETIREMENT_STORE_PATH_BYTES + 1] = {0};
    unsigned entries = 0;
    while (valid && offset < length)
    {
        char kind[TP_RETIREMENT_STORE_RETAINED_KIND_BYTES + 1], digest[TP_RETIREMENT_STORE_SHA256_CAPACITY];
        char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
        uint64_t size = 0;
        size_t start = offset;
        while (offset < length && bytes[offset] >= 'a' && bytes[offset] <= 'z') ++offset;
        valid = offset > start && offset - start <= TP_RETIREMENT_STORE_RETAINED_KIND_BYTES &&
                tp_retirement_store_receipt_literal(bytes, length, &offset, " ");
        if (valid)
        {
            memcpy(kind, bytes + start, offset - 1 - start);
            kind[offset - 1 - start] = 0;
            start = offset;
            while (offset < length && bytes[offset] != ' ') ++offset;
            valid = offset - start == 64;
        }
        if (valid)
        {
            memcpy(digest, bytes + start, 64);
            digest[64] = 0;
            valid = tp_retirement_store_digest(digest) &&
                    tp_retirement_store_receipt_literal(bytes, length, &offset, " ") &&
                    tp_retirement_store_receipt_number(bytes, length, &offset, &size) && size &&
                    size <= TP_RETIREMENT_STORE_FILE_BYTES &&
                    tp_retirement_store_receipt_literal(bytes, length, &offset, " ");
            start = offset;
            while (valid && offset < length && bytes[offset] != '\n') ++offset;
            valid = valid && offset < length && offset - start <= TP_RETIREMENT_STORE_PATH_BYTES;
        }
        if (valid)
        {
            memcpy(path, bytes + start, offset - start);
            path[offset - start] = 0;
            ++offset;
            valid = tp_retirement_store_path(path) && strcmp(path, TP_RETIREMENT_EXECUTION_RECEIPT_PATH) &&
                    strcmp(path, TP_RETIREMENT_RETAINED_MANIFEST_PATH) && (!entries || strcmp(previous, path) < 0);
        }
        if (valid && live)
        {
            unsigned matches = 0;
            for (unsigned i = 0; i < store->count; ++i)
                matches += !strcmp(store->files[i].path, path) && !strcmp(store->files[i].sha256, digest) &&
                           store->files[i].bytes == size;
            valid = matches == 1;
        }
        else if (valid)
        {
            TpRetirementStoredFile reopened = {0};
            valid = tp_retirement_store_reopen_file(store, path, digest, size, &reopened, NULL) &&
                    reopened.bytes == size;
        }
        if (valid)
        {
            strcpy(previous, path);
            ++entries;
        }
    }
    return valid;
}

/* The producer's retained reference: the manifest's digest, or the explicit
 * no-manifest value when the store bound no retained declaration. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_retained_reference(TpRetirementStore* store,
    char output[TP_RETIREMENT_STORE_SHA256_CAPACITY])
{
    TpRetirementStoredFile const* manifest = NULL;
    for (unsigned i = 0; i < store->count; ++i)
        if (!strcmp(store->files[i].path, TP_RETIREMENT_RETAINED_MANIFEST_PATH)) manifest = store->files + i;
    char* bytes = NULL;
    size_t length = 0;
    int valid = manifest ? tp_retirement_store_slurp(store, manifest->path, manifest->sha256,
                                                     TP_RETIREMENT_STORE_FILE_BYTES, &bytes, &length) &&
                           tp_retirement_store_retained_verify(store, bytes, length, 1) :
                           !store->retained_bound;
    if (bytes) munmap(bytes, length);
    if (valid) strcpy(output, manifest ? manifest->sha256 : TP_RETIREMENT_STORE_NO_RETAINED);
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
    if (authority) *authority = (TpRetirementReceiptAuthority){0};
    valid = valid && receipt &&
            tp_retirement_store_receipt_validate(store, job, attempt, plan_sha256, context_sha256,
                0, authority->identity_sha256) &&
            tp_retirement_store_retained_reference(store, authority->retained_sha256);
    if (valid)
    {
        strcpy(authority->job, job);
        strcpy(authority->plan_sha256, plan_sha256);
        strcpy(authority->context_sha256, context_sha256);
        strcpy(authority->receipt_sha256, receipt->sha256);
        authority->attempt = attempt;
        char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[TP_RETIREMENT_STORE_AUTHORITY_BYTES], digest[65];
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
    char const* digest, uint64_t maximum, TpRetirementStoredFile* reopened, uint64_t* lines)
{
    char leaf[193] = {0};
    struct stat info = {0}, directory = {0};
    int parent = store && tp_retirement_store_digest(digest) ?
                 tp_retirement_store_parent(store, path, leaf, &directory) : -1;
    int valid = parent >= 0 && fstatat(parent, leaf, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
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
        file.parent_device = directory.st_dev;
        file.parent_inode = directory.st_ino;
        file.changed = info.st_ctim;
        valid = tp_retirement_store_file(store, &file, lines);
        if (valid && reopened) *reopened = file;
    }
    if (parent >= 0 && close(parent) != 0) valid = 0;
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
                tp_retirement_store_digest(trusted->identity_sha256) &&
                tp_retirement_store_digest(trusted->retained_sha256) &&
                tp_retirement_store_digest(trusted->authority_sha256) &&
                !strcmp(trusted->job, job) && trusted->attempt == attempt &&
                !strcmp(trusted->plan_sha256, plan_sha256) &&
                !strcmp(trusted->context_sha256, context_sha256);
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[TP_RETIREMENT_STORE_AUTHORITY_BYTES], expected[65];
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
    TpRetirementStoredFile private_file;
    TpRetirementStoredFile* result_files = valid ? mmap(NULL, sizeof(*result_files) * TP_RETIREMENT_STORE_FILES,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    int result_open = valid && result_files != MAP_FAILED &&
        tp_retirement_store_open(&result_store, result_root, result_files, TP_RETIREMENT_STORE_FILES);
    int private_open = result_open && tp_retirement_store_open(&private_store, authority_root, &private_file, 1);
    valid = private_open && tp_retirement_store_private_root(&result_store, &private_store);
    if (valid) valid = tp_retirement_store_reopen_file(&private_store, name, expected, length, NULL, NULL) &&
                       tp_retirement_store_reopen_file(&result_store, TP_RETIREMENT_EXECUTION_RECEIPT_PATH,
                                                       trusted->receipt_sha256, TP_RETIREMENT_STORE_RECEIPT_BYTES_MAX, result_files, NULL);
    if (valid)
    {
        result_store.count = 1;
        result_store.total = result_files[0].bytes;
        char identity[65];
        valid = tp_retirement_store_receipt_validate(&result_store, job, attempt,
            plan_sha256, context_sha256, 1, identity) && !strcmp(identity, trusted->identity_sha256);
    }
    /* The retained manifest and every file it lists, or its proven absence. */
    if (valid && !strcmp(trusted->retained_sha256, TP_RETIREMENT_STORE_NO_RETAINED))
    {
        struct stat absent = {0};
        valid = fstatat(result_store.root, TP_RETIREMENT_RETAINED_MANIFEST_PATH, &absent, AT_SYMLINK_NOFOLLOW) != 0 &&
                errno == ENOENT;
    }
    else if (valid)
    {
        char* manifest = NULL;
        size_t manifest_length = 0;
        valid = tp_retirement_store_slurp(&result_store, TP_RETIREMENT_RETAINED_MANIFEST_PATH,
                                          trusted->retained_sha256, TP_RETIREMENT_STORE_FILE_BYTES, &manifest,
                                          &manifest_length) &&
                tp_retirement_store_retained_verify(&result_store, manifest, manifest_length, 0);
        if (manifest) munmap(manifest, manifest_length);
    }
    if (private_open) tp_retirement_store_close(&private_store);
    if (result_open) tp_retirement_store_close(&result_store);
    if (result_files != MAP_FAILED) munmap(result_files, sizeof(*result_files) * TP_RETIREMENT_STORE_FILES);
    return valid;
}

/* Whether a final name and its `.pending` temporary exist. A temporary is
 * a crash prefix (before the link, or between link and unlink). */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_entry_state(int root, char const* name, int* final_present,
                                                         int* pending_present)
{
    char pending[TP_RETIREMENT_STORE_PATH_BYTES + 16];
    struct stat info = {0};
    int count = snprintf(pending, sizeof(pending), "%s.pending", name);
    int named = fstatat(root, name, &info, AT_SYMLINK_NOFOLLOW);
    int named_errno = named == 0 ? 0 : errno;
    int temporary = count > 0 && (size_t)count < sizeof(pending) ?
                    fstatat(root, pending, &info, AT_SYMLINK_NOFOLLOW) : 0;
    int temporary_errno = temporary == 0 ? 0 : errno;
    int valid = count > 0 && (size_t)count < sizeof(pending) &&
                (named == 0 || named_errno == ENOENT) && (temporary == 0 || temporary_errno == ENOENT);
    *final_present = valid && named == 0;
    *pending_present = valid && temporary == 0;
    return valid;
}

/* Neither the final name nor its `.pending` temporary exists: a new
 * publication cannot collide with (or leave a temporary beside) an earlier
 * attempt's record. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_entry_absent(int root, char const* name)
{
    int final_present = 0, pending_present = 0;
    int absent = tp_retirement_store_entry_state(root, name, &final_present, &pending_present) && !final_present &&
                 !pending_present;
    return absent;
}

int tp_retirement_store_authority_copy(int result_root, int authority_root,
    int queue_authority_root, char const* job, uint64_t attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted)
{
    /* The producer has already published its private record. Reopen that
     * record and the original result/shard inodes before copying anything. */
    int valid = tp_retirement_store_authority_reopen(result_root, authority_root,
        job, attempt, plan_sha256, final_context_sha256, trusted);
    TpRetirementStore source, destination, result;
    TpRetirementStoredFile source_file, destination_file, result_file;
    int source_open = valid && tp_retirement_store_open(&source, authority_root, &source_file, 1);
    int destination_open = source_open &&
        tp_retirement_store_open(&destination, queue_authority_root, &destination_file, 1);
    int result_open = destination_open && tp_retirement_store_open(&result, result_root, &result_file, 1);
    valid = result_open && tp_retirement_store_private_root(&source, &destination) &&
            tp_retirement_store_private_root(&result, &destination);
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[TP_RETIREMENT_STORE_AUTHORITY_BYTES], digest[65];
    size_t length = 0;
    if (valid) valid = tp_retirement_store_authority_bytes(trusted, name, body, &length) &&
                       tp_retirement_store_entry_absent(queue_authority_root, name);
    if (valid)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, body, length);
        sha256_finish_hex(&hash, (char8*)digest);
        valid = !strcmp(digest, trusted->authority_sha256);
    }
    TpRetirementPending pending = {0};
    if (valid) valid = tp_retirement_store_begin(&destination, name, sizeof(body), &pending);
    if (valid) valid = fwrite(body, 1, length, pending.stream) == length;
    if (valid)
        valid = tp_retirement_store_publish(&destination, &pending, length, digest) &&
                tp_retirement_store_validate(&destination);
    else if (destination_open && destination.active)
        tp_retirement_store_abort(&destination, &pending);
    if (valid) valid = tp_retirement_store_authority_reopen(result_root, queue_authority_root,
        job, attempt, plan_sha256, final_context_sha256, trusted);
    if (result_open) tp_retirement_store_close(&result);
    if (destination_open) tp_retirement_store_close(&destination);
    if (source_open) tp_retirement_store_close(&source);
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
            tp_retirement_store_receipt_validate(store, job, attempt, plan_sha256, context_sha256, 0, NULL) &&
            tp_retirement_store_authority_reopen(store->root, authority_root,
                job, attempt, plan_sha256, context_sha256, trusted);
    if (store && !valid) store->failed = 1;
    return valid;
}

int tp_retirement_store_read(TpRetirementStore* store, char const* path, TpRetirementStoredFile const** entry)
{
    TpRetirementStoredFile const* found = NULL;
    unsigned matches = 0;
    int valid = store && !store->failed && !store->active && path && entry && tp_retirement_store_path(path);
    for (unsigned i = 0; valid && i < store->count; ++i)
        if (!strcmp(store->files[i].path, path))
        {
            found = store->files + i;
            ++matches;
        }
    valid = valid && matches == 1;
    char leaf[193] = {0};
    struct stat parent_info = {0}, opened = {0}, named = {0};
    int parent = valid ? tp_retirement_store_parent(store, path, leaf, &parent_info) : -1;
    int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
    valid = fd >= 0 && parent_info.st_dev == found->parent_device && parent_info.st_ino == found->parent_inode &&
            fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode) && opened.st_dev == found->device &&
            opened.st_ino == found->inode && opened.st_uid == found->owner && opened.st_uid == geteuid() &&
            opened.st_nlink == 1 && (opened.st_mode & 0777) == 0400 && opened.st_size >= 0 &&
            (uint64_t)opened.st_size == found->bytes && opened.st_ctim.tv_sec == found->changed.tv_sec &&
            opened.st_ctim.tv_nsec == found->changed.tv_nsec &&
            fstatat(parent, leaf, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            named.st_dev == opened.st_dev && named.st_ino == opened.st_ino;
    if (parent >= 0 && close(parent) != 0) valid = 0;
    if (!valid && fd >= 0)
    {
        close(fd);
        fd = -1;
    }
    if (entry) *entry = valid ? found : NULL;
    return fd;
}

int tp_retirement_store_settle(TpRetirementStore* store, unsigned exact_files)
{
    int valid = store && !store->failed && !store->active && store->planned && exact_files &&
                store->count <= exact_files && exact_files <= store->planned_files &&
                store->planned_files - exact_files <= store->bounded_files &&
                store->external_entries <= TP_RETIREMENT_STORE_FILES - exact_files;
    if (valid)
    {
        store->bounded_files -= store->planned_files - exact_files;
        store->planned_files = exact_files;
    }
    else if (store) store->failed = 1;
    return valid;
}

/* The queue-private journal record names every authenticated identity the
 * copied authority binds, plus that authority's own digest. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_journal_bytes(TpRetirementReceiptAuthority const* authority,
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], char body[TP_RETIREMENT_STORE_JOURNAL_BYTES], size_t* length)
{
    char authority_name[TP_RETIREMENT_STORE_PATH_BYTES + 1], authority_body[TP_RETIREMENT_STORE_AUTHORITY_BYTES];
    size_t authority_length = 0;
    int valid = tp_retirement_store_authority_bytes(authority, authority_name, authority_body, &authority_length) &&
                tp_retirement_store_digest(authority->authority_sha256);
    int count = valid ? snprintf(name, TP_RETIREMENT_STORE_PATH_BYTES + 1, "authority-%s-%" PRIu64 ".journal",
                                 authority->job, authority->attempt) : -1;
    valid = valid && count > 0 && count <= (int)TP_RETIREMENT_STORE_PATH_BYTES - 8;
    count = valid ? snprintf(body, TP_RETIREMENT_STORE_JOURNAL_BYTES,
        "BQ-RETIREMENT-AUTHORITY-JOURNAL-V2\n%s\n%" PRIu64 "\n%s\n%s\n%s\n%s\n%s\n%s\n",
        authority->job, authority->attempt, authority->plan_sha256, authority->context_sha256,
        authority->receipt_sha256, authority->identity_sha256, authority->retained_sha256,
        authority->authority_sha256) : -1;
    valid = valid && count > 0 && count < (int)TP_RETIREMENT_STORE_JOURNAL_BYTES;
    if (length) *length = valid ? (size_t)count : 0;
    return valid;
}

/* The authenticated handoff carries numeric identities; the authority's job
 * must be their `job-<id>` label and its attempt the same token. */
BUSTER_GLOBAL_LOCAL int tp_retirement_store_handoff_identity(uint64_t job, uint64_t attempt,
    char const* plan_sha256, char const* context_sha256, TpRetirementReceiptAuthority const* trusted)
{
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY];
    int valid = tp_retirement_store_job_label(label, job) && attempt && trusted &&
                tp_retirement_store_digest(plan_sha256) && tp_retirement_store_digest(context_sha256) &&
                !strcmp(trusted->job, label) && trusted->attempt == attempt &&
                !strcmp(trusted->plan_sha256, plan_sha256) && !strcmp(trusted->context_sha256, context_sha256);
    return valid;
}

BUSTER_GLOBAL_LOCAL int tp_retirement_store_journal_reopen(int queue_authority_root,
    TpRetirementReceiptAuthority const* trusted, TpRetirementAuthorityJournal* journal)
{
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[TP_RETIREMENT_STORE_JOURNAL_BYTES], digest[65] = {0};
    size_t length = 0;
    int valid = tp_retirement_store_journal_bytes(trusted, name, body, &length);
    if (valid)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, body, length);
        sha256_finish_hex(&hash, (char8*)digest);
    }
    TpRetirementStore queue;
    TpRetirementStoredFile file;
    int opened = valid && tp_retirement_store_open(&queue, queue_authority_root, &file, 1);
    TpRetirementStoredFile reopened = {0};
    valid = opened && tp_retirement_store_reopen_file(&queue, name, digest, length, &reopened, NULL) &&
            reopened.bytes == length;
    if (opened) tp_retirement_store_close(&queue);
    if (journal)
    {
        *journal = (TpRetirementAuthorityJournal){0};
        if (valid)
        {
            strcpy(journal->path, name);
            strcpy(journal->sha256, digest);
            journal->bytes = length;
        }
    }
    return valid;
}

int tp_retirement_store_authority_handoff(int result_root, int authority_root, int queue_authority_root,
    uint64_t authenticated_job, uint64_t authenticated_attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted, TpRetirementAuthorityJournal* journal)
{
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1], body[TP_RETIREMENT_STORE_JOURNAL_BYTES], digest[65] = {0};
    size_t length = 0;
    int valid = journal && tp_retirement_store_handoff_identity(authenticated_job, authenticated_attempt,
        plan_sha256, final_context_sha256, trusted) && tp_retirement_store_journal_bytes(trusted, name, body, &length) &&
        tp_retirement_store_entry_absent(queue_authority_root, name);
    if (journal) *journal = (TpRetirementAuthorityJournal){0};
    /* A retry after any earlier attempt (complete or not) refuses here,
     * before creating anything: restart classification is the state call's. */
    valid = valid && tp_retirement_store_authority_copy(result_root, authority_root, queue_authority_root,
        trusted->job, authenticated_attempt, plan_sha256, final_context_sha256, trusted);
    TpRetirementStore queue;
    TpRetirementStoredFile file;
    int opened = valid && tp_retirement_store_open(&queue, queue_authority_root, &file, 1);
    TpRetirementPending pending = {0};
    valid = opened && tp_retirement_store_begin(&queue, name, sizeof(body), &pending);
    if (valid) valid = fwrite(body, 1, length, pending.stream) == length;
    if (valid)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, body, length);
        sha256_finish_hex(&hash, (char8*)digest);
        valid = tp_retirement_store_publish(&queue, &pending, length, digest) && tp_retirement_store_validate(&queue);
    }
    else if (opened && queue.active) tp_retirement_store_abort(&queue, &pending);
    if (opened) tp_retirement_store_close(&queue);
    /* Reopen the copy and the journal from their names before any ACK. */
    valid = valid && tp_retirement_store_authority_reopen(result_root, queue_authority_root, trusted->job,
        authenticated_attempt, plan_sha256, final_context_sha256, trusted) &&
        tp_retirement_store_journal_reopen(queue_authority_root, trusted, journal);
    if (!valid && journal) *journal = (TpRetirementAuthorityJournal){0};
    return valid;
}

TpRetirementAuthorityState tp_retirement_store_authority_state(int result_root, int queue_authority_root,
    uint64_t authenticated_job, uint64_t authenticated_attempt,
    char const* plan_sha256, char const* final_context_sha256,
    TpRetirementReceiptAuthority const* trusted)
{
    TpRetirementAuthorityState state = TP_RETIREMENT_AUTHORITY_INVALID;
    char authority_name[TP_RETIREMENT_STORE_PATH_BYTES + 1], authority_body[TP_RETIREMENT_STORE_AUTHORITY_BYTES];
    char journal_name[TP_RETIREMENT_STORE_PATH_BYTES + 1], journal_body[TP_RETIREMENT_STORE_JOURNAL_BYTES];
    size_t authority_length = 0, journal_length = 0;
    int valid = queue_authority_root >= 0 &&
                tp_retirement_store_handoff_identity(authenticated_job, authenticated_attempt,
                    plan_sha256, final_context_sha256, trusted) &&
                tp_retirement_store_authority_bytes(trusted, authority_name, authority_body, &authority_length) &&
                tp_retirement_store_journal_bytes(trusted, journal_name, journal_body, &journal_length);
    int authority_final = 0, authority_pending = 0, journal_final = 0, journal_pending = 0;
    valid = valid && tp_retirement_store_entry_state(queue_authority_root, authority_name, &authority_final,
                                                     &authority_pending) &&
            tp_retirement_store_entry_state(queue_authority_root, journal_name, &journal_final, &journal_pending);
    int any = authority_final || authority_pending || journal_final || journal_pending;
    int sealed = authority_final && journal_final && !authority_pending && !journal_pending;
    if (valid && !any) state = TP_RETIREMENT_AUTHORITY_ABSENT;
    else if (valid && !sealed) state = TP_RETIREMENT_AUTHORITY_INCOMPLETE;
    else if (valid)
    {
        int complete = tp_retirement_store_authority_reopen(result_root, queue_authority_root, trusted->job,
                           authenticated_attempt, plan_sha256, final_context_sha256, trusted) &&
                       tp_retirement_store_journal_reopen(queue_authority_root, trusted, NULL);
        state = complete ? TP_RETIREMENT_AUTHORITY_COMPLETE : TP_RETIREMENT_AUTHORITY_DAMAGED;
    }
    return state;
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
