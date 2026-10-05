/* Immutable native executable descriptor verification, shared by execution
 * and runtime sampling. Production callers derive owner from the fixed NSS
 * service account; upload subjects never select credentials or paths. */
#ifdef __linux__
#include <pwd.h>
/* The candidate account may search the attempt ancestry but not list it, so
 * every component is opened as a path reference. That is enough to stat the
 * source directory and open its two fixed names. */
BUSTER_GLOBAL_LOCAL int bq_native_open_source(char const* path)
{
    int current = path && path[0] == '/' ? open("/", O_PATH | O_DIRECTORY | O_CLOEXEC) : -1;
    size_t offset = 1;
    while (current >= 0 && path[offset])
    {
        size_t end = offset;
        while (path[end] && path[end] != '/') end += 1;
        char name[256];
        size_t length = end - offset;
        bool valid = length > 0 && length < sizeof(name) &&
                     !(length == 1 && path[offset] == '.') &&
                     !(length == 2 && path[offset] == '.' && path[offset + 1] == '.');
        int next = -1;
        if (valid)
        {
            memcpy(name, path + offset, length);
            name[length] = 0;
            next = openat(current, name, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        close(current);
        current = next;
        offset = path[end] ? end + 1 : end;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_native_executable(char const* source, char const* identity, uid_t owner)
{
    int directory = owner != (uid_t)-1 ? bq_native_open_source(source) : -1;
    struct stat info = {0};
    bool ok = directory >= 0 && strlen(identity) == 64 && bq_native_hex((u8 const*)identity) &&
              fstat(directory, &info) == 0 && info.st_uid == owner && (info.st_mode & 07777) == 0550;
    int description = ok ? openat(directory, ".native-manifest", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char manifest[BQ_NATIVE_MANIFEST_CAP] = {0}, program[65] = {0}, digest[65] = {0};
    u32 length = 0;
    u64 size = 0;
    ok = ok && description >= 0 && fstat(description, &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_uid == owner && info.st_nlink == 1 && (info.st_mode & 07777) == 0440 &&
         info.st_size > 0 && info.st_size < BQ_NATIVE_MANIFEST_CAP;
    if (ok)
    {
        length = (u32)info.st_size;
        ok = pread(description, manifest, length, 0) == length &&
             bq_native_parse(identity, manifest, &length, program, &size);
    }
    if (description >= 0) close(description);
    int executable = ok ? openat(directory, "program", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && executable >= 0 && fstat(executable, &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_uid == owner && info.st_nlink == 1 && (info.st_mode & 07777) == 0550 &&
         (u64)info.st_size == size && bq_native_elf(executable, size, 0) &&
         bq_native_digest_fd(executable, 0, size, digest, -1) && !memcmp(digest, program, 64);
    if (directory >= 0) close(directory);
    if (!ok && executable >= 0) { close(executable); executable = -1; }
    return executable;
}

#endif
