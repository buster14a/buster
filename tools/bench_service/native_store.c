/* Private, bounded immutable native program store. The authenticated transport
 * owns uploads; the single queue writer serializes begin/write/finish. SHA256
 * identifies a canonical manifest, never a caller path. Partial bytes survive
 * restart and only an identical prefix may be retried. Publication renames one
 * complete directory, so no request can observe half a finished bundle.
 * bq_native_upload owns mutations; bq_native_staging_recover accepts only
 * exact interrupted prefixes; bq_native_materialize verifies immutable copies.
 * Lifetime storage is capped; removal is operator maintenance, not execution.
 */
#include <inttypes.h>
BUSTER_GLOBAL_LOCAL bool bq_native_hex(u8 const* text)
{
    bool ok = true;
    for (u32 i = 0; ok && i < 64; i += 1)
        ok = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_native_hash(void const* bytes, u64 size, char output[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, size);
    sha256_finish_hex(&hash, (char8*)output);
}

BUSTER_GLOBAL_LOCAL int bq_native_manifest(char bytes[BQ_NATIVE_MANIFEST_CAP], u8 const* program, u64 size, char identity[65])
{
    int length = snprintf(bytes, BQ_NATIVE_MANIFEST_CAP,
        "BQ-NATIVE-V1\noperation=execute-once\nplatform=linux-x86-64-static-elf\narguments=none\nprogram-sha256=%.64s\nprogram-size=%" PRIu64 "\n", program, (uint64_t)size);
    bool ok = bq_native_hex(program) && size >= 64 && size <= BQ_NATIVE_PROGRAM_CAP &&
              length > 0 && length < (int)BQ_NATIVE_MANIFEST_CAP;
    if (ok) bq_native_hash(bytes, (u64)length, identity);
    return ok ? length : -1;
}

#ifdef __linux__
#include <elf.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <linux/fs.h>
BUSTER_GLOBAL_LOCAL bool bq_native_file(int descriptor, off_t size, mode_t mode)
{
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == geteuid() && info.st_nlink == 1 && (info.st_mode & 07777) == mode &&
              info.st_size == size;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_native_elf(int descriptor, u64 length, off_t start)
{
    Elf64_Ehdr header = {0};
    bool executable = false;
    bool ok = length >= sizeof(header) && length <= BQ_NATIVE_PROGRAM_CAP &&
              pread(descriptor, &header, sizeof(header), start) == sizeof(header) &&
              !memcmp(header.e_ident, ELFMAG, SELFMAG) && header.e_ident[EI_CLASS] == ELFCLASS64 &&
              header.e_ident[EI_DATA] == ELFDATA2LSB && header.e_ident[EI_VERSION] == EV_CURRENT &&
              header.e_machine == EM_X86_64 && header.e_type == ET_EXEC && header.e_version == EV_CURRENT &&
              header.e_ehsize == sizeof(header) && header.e_phentsize == sizeof(Elf64_Phdr) &&
              header.e_phnum > 0 && header.e_phnum <= 128 && header.e_phoff <= length &&
              (u64)header.e_phnum * sizeof(Elf64_Phdr) <= length - header.e_phoff;
    for (u32 i = 0; ok && i < header.e_phnum; i += 1)
    {
        Elf64_Phdr segment = {0};
        ok = pread(descriptor, &segment, sizeof(segment), start + (off_t)(header.e_phoff + i * sizeof(segment))) == sizeof(segment) &&
             segment.p_type != PT_INTERP && segment.p_type != PT_DYNAMIC && segment.p_offset <= length &&
             segment.p_filesz <= length - segment.p_offset && segment.p_filesz <= segment.p_memsz &&
             segment.p_vaddr <= UINT64_MAX - segment.p_memsz &&
             (segment.p_flags & (PF_W | PF_X)) != (PF_W | PF_X);
        if (ok && segment.p_type == PT_LOAD && (segment.p_flags & PF_X) &&
            header.e_entry >= segment.p_vaddr && header.e_entry - segment.p_vaddr < segment.p_filesz)
            executable = true;
    }
    return ok && executable;
}

BUSTER_GLOBAL_LOCAL bool bq_native_digest_fd(int descriptor, off_t start, u64 length, char digest[65], int copy)
{
    Sha256 hash;
    sha256_init(&hash);
    u8 bytes[4096];
    bool ok = true;
    for (u64 offset = 0; ok && offset < length;)
    {
        u64 amount = length - offset < sizeof(bytes) ? length - offset : sizeof(bytes);
        ssize_t count = pread(descriptor, bytes, (size_t)amount, start + (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count == (ssize_t)amount;
        if (ok)
        {
            sha256_add(&hash, bytes, amount);
            for (u64 used = 0; ok && copy >= 0 && used < amount;)
            {
                ssize_t written = write(copy, bytes + used, (size_t)(amount - used));
                if (written < 0 && errno == EINTR) continue;
                ok = written > 0;
                if (ok) used += (u64)written;
            }
            offset += amount;
        }
    }
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_native_store(BqQueue const* queue)
{
    int descriptor = -1;
    bool ok = queue && queue->directory_fd >= 0 && !queue->poisoned;
    if (ok) ok = mkdirat(queue->directory_fd, "native-blobs", 0700) == 0 || errno == EEXIST;
    if (ok) descriptor = openat(queue->directory_fd, "native-blobs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    ok = ok && descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid() &&
         ((info.st_mode & 07777) == BQ_NATIVE_STORE_MODE ||
          /* mkdir under the service umask, or a store created before the broker needed it. */
          ((info.st_mode & 07777) == 0700 && fchmod(descriptor, BQ_NATIVE_STORE_MODE) == 0)) &&
         fsync(queue->directory_fd) == 0;
    if (!ok && descriptor >= 0) { close(descriptor); descriptor = -1; }
    return descriptor;
}

BUSTER_GLOBAL_LOCAL bool bq_native_quota(int store)
{
    int scan = openat(store, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    DIR* directory = scan >= 0 ? fdopendir(scan) : NULL;
    u32 entries = 0;
    bool ok = directory != NULL;
    if (ok)
    {
        struct dirent* entry;
        errno = 0;
        while (entries < BQ_NATIVE_STORE_CAP && (entry = readdir(directory)) != NULL)
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) entries += 1;
        ok = errno == 0 && entries < BQ_NATIVE_STORE_CAP;
        closedir(directory);
    }
    else if (scan >= 0) close(scan);
    return ok;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
/* Interrupted publication prefixes are real filesystem states, not a second
 * store model. No transport operation can configure this fault point. */
BUSTER_GLOBAL_LOCAL u32 bq_native_test_finish_crash;
#endif

BUSTER_GLOBAL_LOCAL bool bq_native_finish_checkpoint(u32 checkpoint)
{
#ifdef BUSTER_BENCH_SERVICE_TEST
    bool ok = bq_native_test_finish_crash != checkpoint;
#else
    (void)checkpoint;
    bool ok = true;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_native_prefix(int input, off_t offset, int previous, u64 size,
                                          char const* expected)
{
    bool ok = true;
    u8 left[4096], right[4096];
    for (u64 cursor = 0; ok && cursor < size;)
    {
        u64 count = size - cursor < sizeof(left) ? size - cursor : sizeof(left);
        ok = pread(input, left, (size_t)count, (off_t)cursor) == (ssize_t)count;
        if (ok && previous >= 0)
            ok = pread(previous, right, (size_t)count, offset + (off_t)cursor) == (ssize_t)count &&
                 !memcmp(left, right, (size_t)count);
        else if (ok) ok = !memcmp(left, expected + cursor, (size_t)count);
        cursor += count;
    }
    return ok;
}

/* Restart can discard only an owned, exact partial prefix of these two fixed
 * files. Foreign, linked, extra or mismatched evidence remains untouched. */
BUSTER_GLOBAL_LOCAL bool bq_native_staging_recover(int store, char const* pending, int input, u64 size,
                                                  char const* manifest, u32 manifest_size)
{
    int directory = openat(store, pending, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat root = {0};
    bool ok = directory >= 0 && fstat(directory, &root) == 0 && root.st_uid == geteuid() &&
              ((root.st_mode & 07777) == 0700 || (root.st_mode & 07777) == BQ_NATIVE_BUNDLE_MODE);
    int scan = ok ? openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = scan >= 0 ? fdopendir(scan) : NULL;
    bool present[2] = {false, false};
    struct stat files[2] = {{0}, {0}};
    char const* names[] = {"program", "manifest"};
    mode_t const sealed[] = {0400, BQ_NATIVE_MANIFEST_MODE};
    ok = ok && stream;
    if (stream)
    {
        struct dirent* entry;
        u32 count = 0;
        errno = 0;
        while (ok && (entry = readdir(stream)) != NULL)
        {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            u32 index = !strcmp(entry->d_name, names[0]) ? 0 : !strcmp(entry->d_name, names[1]) ? 1 : 2;
            ok = ++count <= 2 && index < 2;
            int file = ok ? openat(directory, names[index], O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
            if (ok)
            {
                ok = file >= 0 && fstat(file, files + index) == 0 && files[index].st_size >= 0 &&
                     files[index].st_uid == geteuid() && S_ISREG(files[index].st_mode) && files[index].st_nlink == 1 &&
                     ((files[index].st_mode & 07777) == 0600 || (files[index].st_mode & 07777) == sealed[index]) &&
                     (u64)files[index].st_size <= (index == 0 ? size : manifest_size) &&
                     ((files[index].st_mode & 07777) != sealed[index] || (u64)files[index].st_size == (index == 0 ? size : manifest_size)) &&
                     bq_native_prefix(file, 8, index == 0 ? input : -1, (u64)files[index].st_size, manifest);
                present[index] = ok;
            }
            if (file >= 0) close(file);
            errno = 0;
        }
        if (errno) ok = false;
        closedir(stream);
    }
    else if (scan >= 0) close(scan);
    if (ok && present[1]) ok = present[0] && (files[0].st_mode & 07777) == 0400 && (u64)files[0].st_size == size;
    if (ok && (root.st_mode & 07777) == BQ_NATIVE_BUNDLE_MODE) ok = present[0] && present[1] &&
        (files[0].st_mode & 07777) == sealed[0] && (files[1].st_mode & 07777) == sealed[1];
    if (ok) ok = fchmod(directory, 0700) == 0;
    for (u32 index = 0; ok && index < 2; index += 1)
    {
        struct stat current = {0};
        if (present[index]) ok = fstatat(directory, names[index], &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            current.st_dev == files[index].st_dev && current.st_ino == files[index].st_ino && current.st_nlink == 1 &&
            unlinkat(directory, names[index], 0) == 0;
    }
    struct stat current = {0};
    if (ok) ok = fsync(directory) == 0 && fstatat(store, pending, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
                 current.st_dev == root.st_dev && current.st_ino == root.st_ino &&
                 unlinkat(store, pending, AT_REMOVEDIR) == 0 && fsync(store) == 0;
    if (directory >= 0) close(directory);
    return ok;
}

/* Requests: begin SHA64/size8; write SHA64/size8/offset8/bytes (<=432);
 * finish SHA64/size8. Replies bind manifest identity and durable byte cursor. */
BUSTER_GLOBAL_LOCAL BqError bq_native_upload(BqQueue* queue, u32 kind, u8 const* body, u32 length,
                                              char identity[65], u64* cursor)
{
    *cursor = 0;
    memset(identity, 0, 65);
    bool shape = length >= 72 && bq_native_hex(body) && (kind == 1 || kind == 2 || kind == 3) &&
                 (kind == 2 ? length > 80 && length <= 512 : length == 72);
    u64 size = shape ? bq_u64(body + 64) : 0;
    char manifest[BQ_NATIVE_MANIFEST_CAP] = {0}, part[80] = {0};
    int manifest_size = shape ? bq_native_manifest(manifest, body, size, identity) : -1;
    BqError error = manifest_size > 0 ? BQ_OK : BQ_BAD_REQUEST;
    int store = error == BQ_OK ? bq_native_store(queue) : -1;
    if (error == BQ_OK && store < 0) error = BQ_IO;
    int committed = store >= 0 ? openat(store, identity, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat committed_info = {0};
    if (committed >= 0)
    {
        int file = openat(committed, "manifest", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        char bytes[BQ_NATIVE_MANIFEST_CAP];
        bool same = fstat(committed, &committed_info) == 0 && committed_info.st_uid == geteuid() &&
                    (committed_info.st_mode & 07777) == BQ_NATIVE_BUNDLE_MODE && bq_native_file(file, manifest_size, BQ_NATIVE_MANIFEST_MODE) &&
                    pread(file, bytes, (size_t)manifest_size, 0) == manifest_size && !memcmp(bytes, manifest, (size_t)manifest_size);
        if (file >= 0) close(file);
        int executable = same ? openat(committed, "program", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
        char digest[65];
        same = same && bq_native_file(executable, (off_t)size, 0400) && bq_native_elf(executable, size, 0) &&
               bq_native_digest_fd(executable, 0, size, digest, -1) && !memcmp(digest, body, 64);
        if (executable >= 0) close(executable);
        error = same ? BQ_OK : BQ_CORRUPT;
        if (same) *cursor = size;
        if (same && kind == 3)
        {
            snprintf(part, sizeof(part), "%.64s.part", body);
            int orphan = openat(store, part, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            int orphan_errno = errno;
            u8 header[8];
            bool matching = orphan >= 0 && bq_native_file(orphan, (off_t)(size + 8), 0600) &&
                            pread(orphan, header, 8, 0) == 8 && !memcmp(header, body + 64, 8) &&
                            bq_native_digest_fd(orphan, 8, size, digest, -1) && !memcmp(digest, body, 64);
            struct stat before = {0}, current = {0};
            if (matching) matching = fstat(orphan, &before) == 0 &&
                fstatat(store, part, &current, AT_SYMLINK_NOFOLLOW) == 0 && current.st_dev == before.st_dev &&
                current.st_ino == before.st_ino && current.st_nlink == 1;
            if (orphan >= 0) close(orphan);
            if (matching) error = unlinkat(store, part, 0) == 0 && fsync(store) == 0 ? BQ_OK : BQ_IO;
            else if (orphan >= 0 || orphan_errno != ENOENT) error = BQ_CORRUPT;
        }
    }
    else if (error == BQ_OK)
    {
        snprintf(part, sizeof(part), "%.64s.part", body);
        int file = openat(store, part, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (file < 0 && errno == ENOENT && kind == 1)
        {
            if (!bq_native_quota(store)) error = BQ_FULL;
            if (error == BQ_OK) file = openat(store, part, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (error == BQ_OK && (file < 0 || write(file, body + 64, 8) != 8 || fsync(file) != 0 || fsync(store) != 0)) error = BQ_IO;
        }
        struct stat info = {0};
        u8 header[8];
        if (error == BQ_OK && (file < 0 || fstat(file, &info) != 0 || info.st_size < 8 ||
            info.st_size > (off_t)(size + 8) || !bq_native_file(file, info.st_size, 0600) ||
            pread(file, header, 8, 0) != 8 || memcmp(header, body + 64, 8))) error = BQ_CONFLICT;
        if (error == BQ_OK) *cursor = (u64)info.st_size - 8;
        if (error == BQ_OK && kind == 2)
        {
            u64 offset = bq_u64(body + 72), amount = length - 80;
            bool range = offset <= *cursor && offset <= size && amount <= size - offset;
            if (!range || (offset < *cursor && amount > *cursor - offset)) error = BQ_CONFLICT;
            else if (offset < *cursor)
            {
                u8 previous[432];
                if (pread(file, previous, (size_t)amount, (off_t)(offset + 8)) != (ssize_t)amount ||
                    memcmp(previous, body + 80, (size_t)amount)) error = BQ_CONFLICT;
            }
            else
            {
                for (u64 used = 0; error == BQ_OK && used < amount;)
                {
                    ssize_t count = pwrite(file, body + 80 + used, (size_t)(amount - used), (off_t)(offset + 8 + used));
                    if (count < 0 && errno == EINTR) continue;
                    if (count <= 0) error = BQ_IO;
                    else used += (u64)count;
                }
                if (error == BQ_OK && fsync(file) != 0) error = BQ_IO;
                if (error == BQ_OK) *cursor += amount;
            }
        }
        if (error == BQ_OK && kind == 3)
        {
            char digest[65] = {0}, pending[80];
            bool valid = *cursor == size && bq_native_elf(file, size, 8) &&
                         bq_native_digest_fd(file, 8, size, digest, -1) && !memcmp(digest, body, 64);
            if (!valid) error = BQ_SOURCE_MISMATCH;
            snprintf(pending, sizeof(pending), ".%.64s", identity);
            int staging = -1, output = -1, description = -1;
            if (error == BQ_OK && mkdirat(store, pending, 0700) != 0)
            {
                if (errno != EEXIST || !bq_native_staging_recover(store, pending, file, size, manifest, (u32)manifest_size) ||
                    mkdirat(store, pending, 0700) != 0) error = BQ_RECONCILIATION_REQUIRED;
            }
            if (error == BQ_OK && !bq_native_finish_checkpoint(1)) error = BQ_IO;
            if (error == BQ_OK) staging = openat(store, pending, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (error == BQ_OK) output = openat(staging, "program", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (error == BQ_OK && (output < 0 || !bq_native_digest_fd(file, 8, size, digest, output) ||
                memcmp(digest, body, 64) || fchmod(output, 0400) != 0 || fsync(output) != 0)) error = BQ_IO;
            if (error == BQ_OK && !bq_native_finish_checkpoint(2)) error = BQ_IO;
            if (error == BQ_OK) description = openat(staging, "manifest", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (error == BQ_OK && (description < 0 || write(description, manifest, (size_t)manifest_size) != manifest_size ||
                fchmod(description, BQ_NATIVE_MANIFEST_MODE) != 0 || fsync(description) != 0 || fchmod(staging, BQ_NATIVE_BUNDLE_MODE) != 0 || fsync(staging) != 0)) error = BQ_IO;
            if (error == BQ_OK && !bq_native_finish_checkpoint(3)) error = BQ_IO;
            if (description >= 0) close(description);
            if (output >= 0) close(output);
            if (staging >= 0) close(staging);
            if (error == BQ_OK && (syscall(SYS_renameat2, store, pending, store, identity, RENAME_NOREPLACE) != 0 ||
                fsync(store) != 0 || !bq_native_finish_checkpoint(4))) error = BQ_IO;
            if (error == BQ_OK && (unlinkat(store, part, 0) != 0 || fsync(store) != 0 ||
                !bq_native_finish_checkpoint(5))) error = BQ_IO;
        }
        if (file >= 0) close(file);
    }
    if (committed >= 0) close(committed);
    if (store >= 0) close(store);
    return error;
}
/* Decode by regenerating the complete canonical manifest, never by accepting
 * extra keys or caller filenames. Both the manifest and executable are hashed
 * again before materialization and by the candidate helper before fexecve. */
BUSTER_GLOBAL_LOCAL bool bq_native_parse(char const* identity, char manifest[BQ_NATIVE_MANIFEST_CAP],
                                         u32* length, char program[65], u64* size)
{
    bool ok = *length > 0 && *length < BQ_NATIVE_MANIFEST_CAP;
    if (ok)
    {
        manifest[*length] = 0;
        char const* hash = strstr(manifest, "\nprogram-sha256=");
        char const* amount = strstr(manifest, "\nprogram-size=");
        ok = hash && amount && amount == hash + 16 + 64;
        if (ok)
        {
            memcpy(program, hash + 16, 64);
            program[64] = 0;
            u64 value = 0;
            char const* current = amount + 14;
            ok = *current >= '1' && *current <= '9';
            while (ok && *current >= '0' && *current <= '9')
            {
                u32 digit = (u32)(*current - '0');
                ok = value <= (UINT64_MAX - digit) / 10;
                if (ok) value = value * 10 + digit;
                current += 1;
            }
            ok = ok && current[0] == '\n' && current[1] == 0;
            *size = value;
        }
        char canonical[BQ_NATIVE_MANIFEST_CAP] = {0}, digest[65] = {0};
        int canonical_length = ok ? bq_native_manifest(canonical, (u8*)program, *size, digest) : -1;
        ok = canonical_length > 0 && (u32)canonical_length == *length && !memcmp(canonical, manifest, *length) &&
             !memcmp(digest, identity, 64);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_native_description(int directory, char const* identity, char manifest[BQ_NATIVE_MANIFEST_CAP],
                                               u32* length, char program[65], u64* size)
{
    int file = openat(directory, "manifest", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && info.st_size > 0 && info.st_size < BQ_NATIVE_MANIFEST_CAP &&
              bq_native_file(file, info.st_size, BQ_NATIVE_MANIFEST_MODE);
    *length = ok ? (u32)info.st_size : 0;
    if (ok) ok = pread(file, manifest, *length, 0) == *length;
    if (file >= 0) close(file);
    if (ok) ok = bq_native_parse(identity, manifest, length, program, size);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_native_materialize(BqQueue const* queue, int source, String8 identity)
{
    char name[65] = {0}, manifest[BQ_NATIVE_MANIFEST_CAP] = {0}, program[65] = {0}, digest[65] = {0};
    u32 length = 0;
    u64 size = 0;
    bool ok = identity.length == 64 && bq_native_hex((u8*)identity.pointer);
    if (ok) memcpy(name, identity.pointer, 64);
    int store = ok ? bq_native_store(queue) : -1;
    int directory = store >= 0 ? openat(store, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    ok = directory >= 0 && fstat(directory, &info) == 0 && info.st_uid == geteuid() &&
         (info.st_mode & 07777) == BQ_NATIVE_BUNDLE_MODE && bq_native_description(directory, name, manifest, &length, program, &size);
    int input = ok ? openat(directory, "program", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && bq_native_file(input, (off_t)size, 0400) && bq_native_elf(input, size, 0);
    int output = ok ? openat(source, "program", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && output >= 0 && bq_native_digest_fd(input, 0, size, digest, output) && !memcmp(digest, program, 64) &&
         fchmod(output, 0550) == 0 && fsync(output) == 0;
    if (output >= 0) close(output);
    int description = ok ? openat(source, ".native-manifest", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && description >= 0 && write(description, manifest, length) == length &&
         fchmod(description, 0440) == 0 && fsync(description) == 0 && fsync(source) == 0;
    if (description >= 0) close(description);
    if (input >= 0) close(input);
    if (directory >= 0) close(directory);
    if (store >= 0) close(store);
    return ok;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_native_upload(BqQueue* queue, u32 kind, u8 const* body, u32 length,
                                              char identity[65], u64* cursor)
{
    (void)queue; (void)kind; (void)body; (void)length;
    memset(identity, 0, 65); *cursor = 0;
    return BQ_UNSUPPORTED;
}
#ifndef _WIN32
BUSTER_GLOBAL_LOCAL bool bq_native_materialize(BqQueue const* queue, int source, String8 identity)
{
    (void)queue; (void)source; (void)identity;
    return false;
}
#endif
#endif
