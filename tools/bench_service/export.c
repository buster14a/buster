/* Immutable result transport for #878 (included after worker_linux.c).
 * bq_export_prepare snapshots a validated, terminal service result under a
 * hard child-process deadline. bq_export_read authenticates bounded chunks
 * against its durable receipt. bq_export_unpack reuses the worker validator
 * on independently reconstructed bytes; it never executes bundle contents.
 * All names on the server are derived from queue state. No request path is
 * used. Export artifacts live beside, never inside, the finalized result.
 */
#define BQ_EXPORT_RECEIPT_CAP 1024u
#define BQ_EXPORT_CHUNK_CAP (64u * 1024u)
#define BQ_EXPORT_REPLY_HEADER 112u
#define BQ_EXPORT_REQUEST_CAP 152u
#define BQ_EXPORT_BODY_CAP (BQ_EXPORT_REPLY_HEADER + BQ_EXPORT_CHUNK_CAP)
#define BQ_EXPORT_TOTAL_CAP (BQ_WORKER_BUNDLE_TOTAL_CAP + BQ_WORKER_BUNDLE_CAP + \
                            2ull * 32768 + BQ_WORKER_BUNDLE_ENTRY_CAP * (16ull + BQ_PATH_CAP))
#define BQ_EXPORT_RETIREMENT_TOTAL_CAP (BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP + BQ_WORKER_BUNDLE_CAP + \
                                       2ull * 32768 + BQ_WORKER_BUNDLE_ENTRY_CAP * (16ull + BQ_PATH_CAP))
#define BQ_EXPORT_CHUNKS ((BQ_EXPORT_TOTAL_CAP + BQ_EXPORT_CHUNK_CAP - 1) / BQ_EXPORT_CHUNK_CAP)
#define BQ_EXPORT_INDEX_CAP (BQ_EXPORT_CHUNKS * 64u)
#define BQ_EXPORT_DATA_OFFSET (BQ_EXPORT_RECEIPT_CAP + BQ_EXPORT_INDEX_CAP)
#define BQ_EXPORT_RETIREMENT_CHUNKS ((BQ_EXPORT_RETIREMENT_TOTAL_CAP + BQ_EXPORT_CHUNK_CAP - 1) / BQ_EXPORT_CHUNK_CAP)
#define BQ_EXPORT_RETIREMENT_DATA_OFFSET (BQ_EXPORT_RECEIPT_CAP + BQ_EXPORT_RETIREMENT_CHUNKS * 64u)
#define BQ_EXPORT_PREPARE_MILLISECONDS 300000u
#define BQ_EXPORT_RETIREMENT_PREPARE_MILLISECONDS 86400000u
#define BQ_EXPORT_READ_MILLISECONDS 30000u
#define BQ_EXPORT_PRINCIPAL "github-actions"

#ifdef __linux__
#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL int bq_export_test_mutation_fd = -1;
BUSTER_GLOBAL_LOCAL bool bq_export_test_stall;
#endif
typedef struct BqExportEntry
{
    char path[BQ_PATH_CAP + 1];
    struct stat info;
} BqExportEntry;

typedef struct BqExportInventory
{
    BqExportEntry entries[BQ_WORKER_BUNDLE_ENTRY_CAP];
    u32 count;
    u32 files;
    u64 bytes;
} BqExportInventory;

/* Control requests are dispatched serially by the service. A sealed spool is
 * immutable, but it can be replaced by the trusted service on a later job or
 * after recovery. Reuse the full index check only for the same receipt and
 * exact inode/metadata identity. Every response still checks its own chunk
 * digest and brackets the read with fstat/fstatat. A restart starts cold. */
typedef struct BqExportIndexCache
{
    struct stat identity;
    char receipt_sha256[SHA256_HEX_CAPACITY];
    bool valid;
} BqExportIndexCache;

BUSTER_GLOBAL_LOCAL BqExportIndexCache bq_export_index_cache;
#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL u64 bq_export_index_full_checks;
#endif

BUSTER_GLOBAL_LOCAL u64 bq_export_total_cap(BqRecipe recipe)
{
    u64 result = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ?
                 BQ_EXPORT_RETIREMENT_TOTAL_CAP : BQ_EXPORT_TOTAL_CAP;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 bq_export_data_offset(BqRecipe recipe)
{
    u64 result = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ?
                 BQ_EXPORT_RETIREMENT_DATA_OFFSET : BQ_EXPORT_DATA_OFFSET;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 bq_export_prepare_milliseconds(BqRecipe recipe)
{
    u64 result = recipe == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED ?
                 BQ_EXPORT_RETIREMENT_PREPARE_MILLISECONDS : BQ_EXPORT_PREPARE_MILLISECONDS;
    return result;
}

BUSTER_GLOBAL_LOCAL BqRecipe bq_export_receipt_recipe(u8 const receipt[BQ_EXPORT_RECEIPT_CAP])
{
    BqRequest request = {0};
    u32 size = bq_u32(receipt + 992);
    if (size <= BQ_REQUEST_CAP)
    {
        request.size = size;
        memcpy(request.bytes, receipt + 672, size);
    }
    BqRecipe result = bq_request_valid(&request) ? bq_request_recipe(&request) : BQ_RECIPE_UNKNOWN;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_export_same(struct stat const* a, struct stat const* b)
{
    bool same = a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
                a->st_mode == b->st_mode && a->st_uid == b->st_uid && a->st_gid == b->st_gid &&
                a->st_nlink == b->st_nlink && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
                a->st_mtim.tv_nsec == b->st_mtim.tv_nsec && a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
                a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
    return same;
}

BUSTER_GLOBAL_LOCAL BqError bq_export_io(int fd, void* bytes, u64 size, u64 offset, bool writing, u64 deadline)
{
    u64 done = 0;
    BqError error = BQ_OK;
    while (error == BQ_OK && done < size)
    {
        if (!bq_worker_remaining(deadline)) error = BQ_EXPORT_TIMEOUT;
        else
        {
            u64 remaining = size - done;
            size_t count = remaining < BQ_EXPORT_CHUNK_CAP ? (size_t)remaining : BQ_EXPORT_CHUNK_CAP;
            ssize_t transferred = writing ? pwrite(fd, (u8*)bytes + done, count, (off_t)(offset + done)) :
                                           pread(fd, (u8*)bytes + done, count, (off_t)(offset + done));
            if (transferred < 0 && errno == EINTR) continue;
            if (transferred <= 0) error = writing ? BQ_IO : BQ_EXPORT_CORRUPT;
            else done += (u64)transferred;
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_export_inventory(int root, BqExportInventory* inventory, u64 total_cap, u64 deadline)
{
    memset(inventory, 0, sizeof(*inventory));
    /* A bounded breadth-first walk also inventories empty directories. Every
     * component is reopened without symlinks; each entry is rechecked after
     * validation and after copying, including directory timestamps. */
    u32 next = 0;
    BqError error = BQ_OK;
    bool first = true;
    while (error == BQ_OK && (first || next < inventory->count))
    {
        char const* prefix = first ? "" : inventory->entries[next].path;
        bool directory = first || S_ISDIR(inventory->entries[next].info.st_mode);
        int fd = directory ? (first ? openat(root, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) :
                             bq_worker_bundle_open_relative(root, prefix)) : -1;
        DIR* stream = fd >= 0 ? fdopendir(fd) : NULL;
        if (directory && !stream) error = BQ_EXPORT_CORRUPT;
        if (fd >= 0 && !stream) close(fd);
        while (error == BQ_OK && stream)
        {
            if (!bq_worker_remaining(deadline)) { error = BQ_EXPORT_TIMEOUT; break; }
            errno = 0;
            struct dirent* item = readdir(stream);
            if (!item) { if (errno) error = BQ_IO; break; }
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            if (inventory->count == BQ_WORKER_BUNDLE_ENTRY_CAP) { error = BQ_EXPORT_OVERSIZED; break; }
            BqExportEntry* entry = inventory->entries + inventory->count;
            if (bq_worker_bundle_relative(entry->path, prefix, item->d_name) <= 0)
                error = BQ_EXPORT_OVERSIZED;
            else if (!bq_worker_bundle_path_valid(entry->path) ||
                     fstatat(dirfd(stream), item->d_name, &entry->info, AT_SYMLINK_NOFOLLOW) != 0)
                error = BQ_EXPORT_CORRUPT;
            else
            {
                u32 depth = 1;
                for (char const* c = entry->path; *c; c += 1) if (*c == '/') depth += 1;
                struct stat const* info = &entry->info;
                if (depth >= BQ_WORKER_BUNDLE_DEPTH_CAP) error = BQ_EXPORT_OVERSIZED;
                else if ((!S_ISDIR(info->st_mode) && !S_ISREG(info->st_mode)) ||
                         (info->st_uid != 0 && info->st_uid != geteuid()) || (info->st_mode & 022) ||
                         (S_ISREG(info->st_mode) && info->st_nlink != 1)) error = BQ_EXPORT_CORRUPT;
                else if (info->st_size < 0 || (S_ISREG(info->st_mode) &&
                         (u64)info->st_size > BQ_WORKER_BUNDLE_FILE_CAP)) error = BQ_EXPORT_OVERSIZED;
                else
                {
                    if (S_ISREG(info->st_mode))
                    {
                        inventory->files += 1;
                        inventory->bytes += (u64)info->st_size;
                    }
                    if (inventory->bytes > total_cap) error = BQ_EXPORT_OVERSIZED;
                    inventory->count += 1;
                }
            }
        }
        if (stream && closedir(stream) != 0 && error == BQ_OK) error = BQ_IO;
        if (first) first = false;
        else next += 1;
    }
    /* Insertion sort avoids callback dispatch. The maximum is fixed, and
     * paths are short; no ordering depends on filesystem enumeration. */
    for (u32 i = 1; error == BQ_OK && i < inventory->count; i += 1)
    {
        BqExportEntry value = inventory->entries[i];
        u32 j = i;
        while (j && strcmp(inventory->entries[j - 1].path, value.path) > 0)
        {
            inventory->entries[j] = inventory->entries[j - 1];
            j -= 1;
        }
        inventory->entries[j] = value;
        if (j && !strcmp(inventory->entries[j - 1].path, value.path)) error = BQ_EXPORT_CORRUPT;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_export_inventory_unchanged(int root, BqExportInventory const* inventory)
{
    bool same = true;
    for (u32 i = 0; same && i < inventory->count; i += 1)
    {
        int fd = bq_worker_bundle_open_relative(root, inventory->entries[i].path);
        struct stat info = {0};
        same = fd >= 0 && fstat(fd, &info) == 0 && bq_export_same(&info, &inventory->entries[i].info);
        if (fd >= 0 && close(fd) != 0) same = false;
    }
    return same;
}

BUSTER_GLOBAL_LOCAL bool bq_export_receipt_valid(u8 const receipt[BQ_EXPORT_RECEIPT_CAP])
{
    u64 bytes = bq_u64(receipt + 24);
    u32 entries = bq_u32(receipt + 44);
    BqRequest request = {.size = bq_u32(receipt + 992)};
    bool valid = !memcmp(receipt, "BQEXP001", 8) && bq_u64(receipt + 8) && bq_u64(receipt + 16) &&
                 bytes > 0 && bytes <= BQ_EXPORT_RETIREMENT_TOTAL_CAP && bq_u64(receipt + 32) <= bytes &&
                 bq_u32(receipt + 40) <= entries && entries && entries <= BQ_WORKER_BUNDLE_ENTRY_CAP &&
                 request.size <= BQ_REQUEST_CAP && bq_u32(receipt + 1012) >= BQ_SUCCEEDED &&
                 bq_u32(receipt + 1012) <= BQ_INTERRUPTED && bq_u32(receipt + 1016) != BQ_INVALID &&
                 bq_u32(receipt + 1016) <= BQ_INVALID && bq_u32(receipt + 1020) == BQ_FINISHED;
    for (u32 offset = 48; valid && offset < 496; offset += 64) valid = bq_result_digest_valid(receipt + offset);
    if (valid)
    {
        memcpy(request.bytes, receipt + 672, request.size);
        char digest[SHA256_HEX_CAPACITY];
        bq_request_digest(&request, digest);
        String8 principal = bq_field(&request, 0), recipe = bq_field(&request, 2);
        String8 profile = bq_recipe_profile(bq_request_recipe(&request));
        valid = bq_request_valid(&request) && bq_recipe_service(bq_request_recipe(&request)) &&
                bytes <= bq_export_total_cap(bq_request_recipe(&request)) &&
                !memcmp(digest, receipt + 48, 64) && string_equal(principal, S8(BQ_EXPORT_PRINCIPAL)) &&
                principal.length < 64 && recipe.length < 48 &&
                !memcmp(receipt + 496, principal.pointer, (size_t)principal.length) && !receipt[496 + principal.length] &&
                !memcmp(receipt + 560, recipe.pointer, (size_t)recipe.length) && !receipt[560 + recipe.length] &&
                bq_result_digest_valid(receipt + 608);
        bq_digest(profile.pointer, (u32)profile.length, digest);
        valid = valid && !memcmp(receipt + 608, digest, 64);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL BqError bq_export_snapshot(BqJob const* job, int output, u64 deadline)
{
    BqRecipe selected = bq_request_recipe(&job->request);
    u64 total_cap = bq_export_total_cap(selected);
    u64 data_offset = bq_export_data_offset(selected);
    int root = bq_worker_open_trusted_directory(string_from_pointer(job->result_root), true, false);
    struct stat root_before = {0}, root_after = {0};
    BqError error = root < 0 ? BQ_EXPORT_MISSING : fstat(root, &root_before) != 0 ? BQ_IO : BQ_OK;
    BqExportInventory* inventory = mmap(NULL, sizeof(*inventory), PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (inventory == MAP_FAILED) error = BQ_FULL;
    if (error == BQ_OK) error = bq_export_inventory(root, inventory, total_cap, deadline);
    if (error == BQ_OK && bq_worker_result_binding_validate_at(job, root) != BQ_OK) error = BQ_EXPORT_INVALID;
    u64 offset = 0;
    for (u32 i = 0; error == BQ_OK && i < inventory->count; i += 1)
    {
        BqExportEntry const* entry = inventory->entries + i;
        u8 header[16] = {0};
        bool regular = S_ISREG(entry->info.st_mode);
        u64 size = regular ? (u64)entry->info.st_size : 0;
        u32 length = (u32)strlen(entry->path);
        bq_put32(header, regular ? 2 : 1);
        bq_put32(header + 4, length);
        bq_put64(header + 8, size);
        if (size > total_cap || offset > total_cap - size || sizeof(header) + length > total_cap - size - offset)
            error = BQ_EXPORT_OVERSIZED;
        if (error == BQ_OK) error = bq_export_io(output, header, sizeof(header), data_offset + offset, true, deadline);
        offset += sizeof(header);
        if (error == BQ_OK) error = bq_export_io(output, (void*)entry->path, length, data_offset + offset, true, deadline);
        offset += length;
        int input = regular && error == BQ_OK ? bq_worker_bundle_open_relative(root, entry->path) : -1;
        struct stat before = {0}, after = {0};
        if (regular && error == BQ_OK && (input < 0 || fstat(input, &before) != 0 || !bq_export_same(&before, &entry->info)))
            error = BQ_EXPORT_CORRUPT;
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        for (u64 copied = 0; error == BQ_OK && copied < size;)
        {
            u64 count = size - copied < sizeof(bytes) ? size - copied : sizeof(bytes);
            error = bq_export_io(input, bytes, count, copied, false, deadline);
            if (error == BQ_OK) error = bq_export_io(output, bytes, count, data_offset + offset + copied, true, deadline);
            copied += count;
        }
        if (input >= 0)
        {
            if (fstat(input, &after) != 0 || !bq_export_same(&before, &after)) error = BQ_EXPORT_CORRUPT;
            if (close(input) != 0 && error == BQ_OK) error = BQ_IO;
        }
        offset += size;
    }
    if (error == BQ_OK && (bq_worker_result_binding_validate_at(job, root) != BQ_OK ||
        !bq_export_inventory_unchanged(root, inventory) || fstat(root, &root_after) != 0 ||
        !bq_export_same(&root_before, &root_after))) error = BQ_EXPORT_CORRUPT;
    int reopened = error == BQ_OK ? bq_worker_open_trusted_directory(string_from_pointer(job->result_root), true, false) : -1;
    if (error == BQ_OK && (reopened < 0 || fstat(reopened, &root_after) != 0 ||
                          !bq_export_same(&root_before, &root_after))) error = BQ_EXPORT_CORRUPT;
    if (reopened >= 0) close(reopened);
    if (root >= 0) close(root);
    u8 receipt[BQ_EXPORT_RECEIPT_CAP] = {0};
    Sha256 archive_hash, index_hash;
    sha256_init(&archive_hash);
    sha256_init(&index_hash);
    for (u64 cursor = 0; error == BQ_OK && cursor < offset; cursor += BQ_EXPORT_CHUNK_CAP)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u64 count = offset - cursor < sizeof(bytes) ? offset - cursor : sizeof(bytes);
        char digest[SHA256_HEX_CAPACITY];
        error = bq_export_io(output, bytes, count, data_offset + cursor, false, deadline);
        if (error == BQ_OK)
        {
            bq_digest(bytes, (u32)count, digest);
            sha256_add(&archive_hash, bytes, count);
            sha256_add(&index_hash, digest, 64);
            error = bq_export_io(output, digest, 64, BQ_EXPORT_RECEIPT_CAP + cursor / BQ_EXPORT_CHUNK_CAP * 64, true, deadline);
        }
    }
    if (error == BQ_OK)
    {
        memcpy(receipt, "BQEXP001", 8);
        bq_put64(receipt + 8, job->id);
        bq_put64(receipt + 16, job->token);
        bq_put64(receipt + 24, offset);
        bq_put64(receipt + 32, inventory->bytes);
        bq_put32(receipt + 40, inventory->files);
        bq_put32(receipt + 44, inventory->count);
        memcpy(receipt + 48, job->digest, 64);
        memcpy(receipt + 112, job->result_manifest_digest, 64);
        memcpy(receipt + 176, job->result_bundle_digest, 64);
        memcpy(receipt + 240, job->result_full_digest, 64);
        char digest[SHA256_HEX_CAPACITY];
        sha256_finish_hex(&archive_hash, (char8*)digest);
        memcpy(receipt + 304, digest, 64);
        sha256_finish_hex(&index_hash, (char8*)digest);
        memcpy(receipt + 368, digest, 64);
        /* The only proc path is the service's own running executable. */
        int executable = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
        struct stat executable_info = {0};
        Sha256 service_hash;
        sha256_init(&service_hash);
        if (executable < 0 || fstat(executable, &executable_info) != 0 || executable_info.st_size <= 0 ||
            (u64)executable_info.st_size > BQ_WORKER_BUNDLE_FILE_CAP) error = BQ_CONFIGURATION_MISMATCH;
        for (u64 cursor = 0; error == BQ_OK && cursor < (u64)executable_info.st_size;)
        {
            u8 bytes[BQ_EXPORT_CHUNK_CAP];
            u64 count = (u64)executable_info.st_size - cursor;
            if (count > sizeof(bytes)) count = sizeof(bytes);
            error = bq_export_io(executable, bytes, count, cursor, false, deadline);
            if (error == BQ_OK) sha256_add(&service_hash, bytes, count);
            cursor += count;
        }
        struct stat executable_after = {0};
        if (error == BQ_OK && (fstat(executable, &executable_after) != 0 ||
                              !bq_export_same(&executable_info, &executable_after))) error = BQ_EXPORT_CORRUPT;
        if (executable >= 0) close(executable);
        sha256_finish_hex(&service_hash, (char8*)digest);
        memcpy(receipt + 432, digest, 64);
        String8 principal = bq_field(&job->request, 0), recipe = bq_field(&job->request, 2);
        String8 profile = bq_recipe_profile(bq_request_recipe(&job->request));
        if (principal.length >= 64 || recipe.length >= 48) error = BQ_EXPORT_INVALID;
        else
        {
            memcpy(receipt + 496, principal.pointer, (size_t)principal.length);
            memcpy(receipt + 560, recipe.pointer, (size_t)recipe.length);
            bq_digest(profile.pointer, (u32)profile.length, digest);
            memcpy(receipt + 608, digest, 64);
        }
        memcpy(receipt + 672, job->request.bytes, job->request.size);
        bq_put32(receipt + 992, job->request.size);
        bq_put64(receipt + 996, (u64)geteuid());
        bq_put64(receipt + 1004, (u64)getegid());
        bq_put32(receipt + 1012, (u32)job->outcome);
        bq_put32(receipt + 1016, (u32)job->validity);
        bq_put32(receipt + 1020, BQ_FINISHED);
        if (error == BQ_OK && !bq_export_receipt_valid(receipt)) error = BQ_EXPORT_INVALID;
        if (error == BQ_OK) error = bq_export_io(output, receipt, sizeof(receipt), 0, true, deadline);
        if (error == BQ_OK && (fchmod(output, 0400) != 0 || fsync(output) != 0)) error = BQ_IO;
    }
    if (inventory != MAP_FAILED) munmap(inventory, sizeof(*inventory));
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_export_name(char name[80], u64 id, u64 token, bool temporary)
{
    int count = snprintf(name, 80, "export-%llu-%llu%s", (unsigned long long)id, (unsigned long long)token,
                         temporary ? ".pending" : ".sealed");
    bool valid = count > 0 && count < 80;
    return valid;
}

BUSTER_GLOBAL_LOCAL BqError bq_export_prepare(BqQueue* queue, BqJob const* job)
{
    char name[80], temporary[80];
    BqError error = bq_export_name(name, job->id, job->token, false) &&
                    bq_export_name(temporary, job->id, job->token, true) ? BQ_OK : BQ_BAD_REQUEST;
    struct stat present = {0};
    if (error == BQ_OK && fstatat(queue->directory_fd, name, &present, AT_SYMLINK_NOFOLLOW) == 0)
    {
        /* Recover the sole safe publication crash prefix: both names refer
         * to the same service-owned sealed inode. Never replace a receipt. */
        struct stat pending = {0};
        if (fstatat(queue->directory_fd, temporary, &pending, AT_SYMLINK_NOFOLLOW) == 0)
        {
            bool same = S_ISREG(present.st_mode) && present.st_uid == geteuid() &&
                        (present.st_mode & 0777) == 0400 && present.st_nlink == 2 &&
                        present.st_dev == pending.st_dev && present.st_ino == pending.st_ino;
            error = !same ? BQ_EXPORT_INTERRUPTED :
                    unlinkat(queue->directory_fd, temporary, 0) != 0 ? BQ_IO : BQ_OK;
        }
        else if (errno != ENOENT) error = BQ_IO;
        if (error == BQ_OK && fsync(queue->directory_fd) != 0) error = BQ_IO;
    }
    else if (error == BQ_OK && errno != ENOENT) error = BQ_IO;
    else if (error == BQ_OK)
    {
        int output = openat(queue->directory_fd, temporary, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        error = output < 0 ? BQ_EXPORT_INTERRUPTED : BQ_OK;
        pid_t child = error == BQ_OK ? fork() : -1;
        if (child == 0)
        {
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(),
                                              bq_export_prepare_milliseconds(bq_request_recipe(&job->request)));
#ifdef BUSTER_BENCH_SERVICE_TEST
            if (bq_export_test_stall) while (true) pause();
#endif
            BqError result = bq_export_snapshot(job, output, deadline);
            _exit((int)result);
        }
        if (error == BQ_OK && child < 0) error = BQ_IO;
        if (error == BQ_OK)
        {
            int status = 0;
            u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(),
                                              bq_export_prepare_milliseconds(bq_request_recipe(&job->request)));
#ifdef BUSTER_BENCH_SERVICE_TEST
            if (bq_export_test_stall) deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 30);
#endif
            if (bq_worker_waitpid_until(child, &status, deadline) != BQ_OK)
            {
                kill(child, SIGKILL);
                u64 reap_deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000);
                bool reaped = bq_worker_waitpid_until(child, &status, reap_deadline) == BQ_OK;
                /* An uninterruptible child retains the inherited queue lock;
                 * stop the daemon instead of admitting a concurrent worker. */
                if (!reaped) queue->poisoned = true;
                error = reaped ? BQ_EXPORT_TIMEOUT : BQ_IO;
            }
            else error = WIFEXITED(status) && WEXITSTATUS(status) <= BQ_EXPORT_TIMEOUT ?
                         (BqError)WEXITSTATUS(status) : BQ_EXPORT_INTERRUPTED;
        }
        if (error == BQ_OK && (linkat(queue->directory_fd, temporary, queue->directory_fd, name, 0) != 0 ||
                              fsync(queue->directory_fd) != 0)) error = BQ_IO;
        if (output >= 0)
        {
            struct stat opened = {0}, named = {0};
            bool owned = fstat(output, &opened) == 0 &&
                         fstatat(queue->directory_fd, temporary, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                         opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
            if (!owned || unlinkat(queue->directory_fd, temporary, 0) != 0 || fsync(queue->directory_fd) != 0) error = BQ_IO;
            if (close(output) != 0) error = BQ_IO;
        }
    }
    return error;
}

/* Unknown and foreign jobs are deliberately the same public error. */
BUSTER_GLOBAL_LOCAL BqError bq_export_authorize(BqQueue* queue, u8 const* request, String8 principal, BqJob** output)
{
    BqJob* job = bq_job(&queue->state, bq_u64(request));
    BqError error = !job || !string_equal(bq_field(&job->request, 0), principal) ? BQ_NOT_FOUND :
                    job->token != bq_u64(request + 8) ? BQ_CONFLICT :
                    job->phase != BQ_FINISHED ? BQ_EXPORT_NOT_FINALIZED :
                    !job->result_bound ? (job->outcome == BQ_INTERRUPTED ? BQ_EXPORT_INTERRUPTED : BQ_EXPORT_INVALID) :
                    memcmp(job->result_full_digest, request + 16, 64) ? BQ_CONFLICT :
                    job->validity == BQ_INVALID ? BQ_EXPORT_INVALID : BQ_OK;
    *output = error == BQ_OK ? job : NULL;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_export_read(BqQueue* queue, BqJob const* job, u64 cursor,
                                            u8 const expected_receipt[64], u8* output, u32* size)
{
    u64 data_offset = bq_export_data_offset(bq_request_recipe(&job->request));
    char name[80];
    BqError error = bq_export_name(name, job->id, job->token, false) ? BQ_OK : BQ_BAD_REQUEST;
    int fd = error == BQ_OK ? openat(queue->directory_fd, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1;
    struct stat before = {0}, after = {0}, named = {0};
    if (error == BQ_OK)
    {
        error = fd < 0 ? BQ_EXPORT_MISSING : fstat(fd, &before) != 0 ? BQ_IO :
                !S_ISREG(before.st_mode) || before.st_uid != geteuid() || before.st_nlink != 1 ||
                (before.st_mode & 0777) != 0400 ? BQ_EXPORT_CORRUPT : BQ_OK;
    }
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), BQ_EXPORT_READ_MILLISECONDS);
    u8 receipt[BQ_EXPORT_RECEIPT_CAP];
    if (error == BQ_OK) error = bq_export_io(fd, receipt, sizeof(receipt), 0, false, deadline);
    u64 total = error == BQ_OK ? bq_u64(receipt + 24) : 0;
    char receipt_digest[SHA256_HEX_CAPACITY] = {0};
    if (error == BQ_OK)
    {
        bq_digest(receipt, sizeof(receipt), receipt_digest);
        error = bq_export_receipt_valid(receipt) && (u64)before.st_size == data_offset + total &&
                bq_u64(receipt + 8) == job->id && bq_u64(receipt + 16) == job->token &&
                !memcmp(receipt + 48, job->digest, 64) && !memcmp(receipt + 112, job->result_manifest_digest, 64) &&
                !memcmp(receipt + 176, job->result_bundle_digest, 64) && !memcmp(receipt + 240, job->result_full_digest, 64) &&
                bq_u32(receipt + 1012) == (u32)job->outcome && bq_u32(receipt + 1016) == (u32)job->validity ?
                BQ_OK : BQ_EXPORT_CORRUPT;
        if (error == BQ_OK && cursor != UINT64_MAX && memcmp(expected_receipt, receipt_digest, 64)) error = BQ_CONFLICT;
        if (error == BQ_OK && cursor != UINT64_MAX && (cursor >= total || cursor % BQ_EXPORT_CHUNK_CAP)) error = BQ_BAD_REQUEST;
    }
    bool cached = error == BQ_OK && bq_export_index_cache.valid &&
                  bq_export_same(&before, &bq_export_index_cache.identity) &&
                  !memcmp(receipt_digest, bq_export_index_cache.receipt_sha256, 64);
    Sha256 index;
    if (error == BQ_OK && !cached)
    {
        sha256_init(&index);
#ifdef BUSTER_BENCH_SERVICE_TEST
        bq_export_index_full_checks += 1;
#endif
    }
    char expected_chunk[SHA256_HEX_CAPACITY] = {0};
    u64 index_size = ((total + BQ_EXPORT_CHUNK_CAP - 1) / BQ_EXPORT_CHUNK_CAP) * 64;
    for (u64 offset = 0; error == BQ_OK && !cached && offset < index_size;)
    {
        u8 bytes[BQ_EXPORT_CHUNK_CAP];
        u64 count = index_size - offset < sizeof(bytes) ? index_size - offset : sizeof(bytes);
        error = bq_export_io(fd, bytes, count, BQ_EXPORT_RECEIPT_CAP + offset, false, deadline);
        if (error == BQ_OK)
        {
            sha256_add(&index, bytes, count);
            u64 target = cursor == UINT64_MAX ? UINT64_MAX : cursor / BQ_EXPORT_CHUNK_CAP * 64;
            if (target >= offset && target < offset + count) memcpy(expected_chunk, bytes + target - offset, 64);
        }
        offset += count;
    }
    if (error == BQ_OK && cached && cursor != UINT64_MAX)
    {
        u64 offset = cursor / BQ_EXPORT_CHUNK_CAP * 64;
        error = bq_export_io(fd, expected_chunk, 64, BQ_EXPORT_RECEIPT_CAP + offset, false, deadline);
    }
    if (error == BQ_OK && !cached)
    {
        char actual[SHA256_HEX_CAPACITY];
        sha256_finish_hex(&index, (char8*)actual);
        if (memcmp(actual, receipt + 368, 64)) error = BQ_EXPORT_CORRUPT;
    }
    u32 count = 0;
    if (error == BQ_OK)
    {
        if (cursor == UINT64_MAX)
        {
            count = sizeof(receipt);
            memcpy(output + BQ_EXPORT_REPLY_HEADER, receipt, count);
        }
        else
        {
            count = (u32)(total - cursor < BQ_EXPORT_CHUNK_CAP ? total - cursor : BQ_EXPORT_CHUNK_CAP);
            error = bq_export_io(fd, output + BQ_EXPORT_REPLY_HEADER, count, data_offset + cursor, false, deadline);
            char actual[SHA256_HEX_CAPACITY];
            if (error == BQ_OK) bq_digest(output + BQ_EXPORT_REPLY_HEADER, count, actual);
            if (error == BQ_OK && memcmp(actual, expected_chunk, 64)) error = BQ_EXPORT_CORRUPT;
        }
    }
#ifdef BUSTER_BENCH_SERVICE_TEST
    if (error == BQ_OK && bq_export_test_mutation_fd >= 0)
    {
        u8 changed = 0xff;
        ssize_t written = pwrite(bq_export_test_mutation_fd, &changed, 1, data_offset);
        if (written != 1) error = BQ_IO;
    }
#endif
    if (error == BQ_OK && (fstat(fd, &after) != 0 || !bq_export_same(&before, &after) ||
        fstatat(queue->directory_fd, name, &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        !bq_export_same(&before, &named))) error = BQ_EXPORT_CORRUPT;
    if (fd >= 0 && close(fd) != 0 && error == BQ_OK) error = BQ_IO;
    if (error == BQ_OK && !cached)
    {
        bq_export_index_cache.identity = before;
        memcpy(bq_export_index_cache.receipt_sha256, receipt_digest, sizeof(receipt_digest));
        bq_export_index_cache.valid = true;
    }
    else if (error != BQ_OK) bq_export_index_cache.valid = false;
    if (error == BQ_OK)
    {
        memset(output, 0, BQ_EXPORT_REPLY_HEADER);
        bq_put64(output + 4, job->id);
        bq_put64(output + 12, job->token);
        bq_put64(output + 20, cursor);
        bq_put64(output + 28, cursor == UINT64_MAX ? 0 : cursor + count);
        bq_put64(output + 36, total);
        bq_put32(output + 44, count);
        memcpy(output + 48, receipt_digest, 64);
        *size = BQ_EXPORT_REPLY_HEADER + count;
    }
    else *size = 4;
    bq_put32(output, (u32)error);
    return error;
}
#endif
