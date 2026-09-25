/* Private #1018 tool input boundary. This verifies the exact pinned, closed
 * bundle selected by the fixed build environment; it cannot prove that a
 * child used only these files until the #923 sandbox binds the same root and
 * excludes ambient compiler, linker, resource and SDK paths. */
#include "retirement_toolchain.h"

#define BQ_RETIREMENT_TOOLCHAIN_MANIFEST_CAP (512u * 1024u)
#define BQ_RETIREMENT_TOOLCHAIN_FILE_CAP (512ull * 1024ull * 1024ull)
#define BQ_RETIREMENT_TOOLCHAIN_TOTAL_CAP (2ull * 1024ull * 1024ull * 1024ull)
#define BQ_RETIREMENT_TOOLCHAIN_FILE_COUNT 4096u

BUSTER_GLOBAL_LOCAL bool bq_retirement_toolchain_scan(int root, String8 pinned,
    BqRetirementToolchain* observed)
{
    int manifest = openat(root, "toolchain.manifest", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat directory_info = {0}, manifest_info = {0}, final_info = {0}, named_info = {0};
    u8 bytes[BQ_RETIREMENT_TOOLCHAIN_MANIFEST_CAP];
    u32 length = 0;
    bool ok = manifest >= 0 && bq_owned_directory(root, false, true) &&
              fstat(root, &directory_info) == 0 && fstat(manifest, &manifest_info) == 0 &&
              S_ISREG(manifest_info.st_mode) && manifest_info.st_nlink == 1 &&
              (manifest_info.st_uid == 0 || manifest_info.st_uid == geteuid()) &&
              !(manifest_info.st_mode & 0222) && (manifest_info.st_mode & S_IROTH) &&
              bq_read_file(manifest, bytes, sizeof(bytes), &length);
    if (ok)
    {
        bq_digest(bytes, length, (char8*)observed->manifest_sha256);
        ok = !memcmp(pinned.pointer, observed->manifest_sha256, 64);
    }
    String8 text = {(char8*)bytes, length}, line = {0}, previous = {0};
    u64 offset = 0;
    ok = ok && bq_next_line(text, &offset, &line) &&
         string_equal(line, S8("BQ-RETIREMENT-TOOLCHAIN-V1")) &&
         bq_next_line(text, &offset, &line) && string_equal(line, S8("platform=linux-x86_64"));
    Sha256 identity;
    sha256_init(&identity);
    if (ok)
    {
        sha256_add(&identity, &directory_info.st_dev, sizeof(directory_info.st_dev));
        sha256_add(&identity, &directory_info.st_ino, sizeof(directory_info.st_ino));
        sha256_add(&identity, &manifest_info.st_dev, sizeof(manifest_info.st_dev));
        sha256_add(&identity, &manifest_info.st_ino, sizeof(manifest_info.st_ino));
    }
    BqRetirementSource closure = {.directories = 1};
    u32 required = 0;
    while (ok && offset < text.length)
    {
        ok = bq_next_line(text, &offset, &line) && line.length > 65 && line.pointer[64] == ' ';
        String8 digest = ok ? (String8){line.pointer, 64} : (String8){0};
        String8 path = ok ? (String8){line.pointer + 65, line.length - 65} : (String8){0};
        u64 shared = previous.length < path.length ? previous.length : path.length;
        int order = previous.length ? memcmp(previous.pointer, path.pointer, (size_t)shared) : -1;
        ok = ok && bq_retirement_hex(digest, 64) && bq_relative_source_path(path) &&
             !string_equal(path, S8("toolchain.manifest")) &&
             (!previous.length || order < 0 || (!order && previous.length < path.length));
        u32 depth = 1;
        for (u64 i = 0; ok && i < path.length; i += 1)
        {
            if (path.pointer[i] == '/')
            {
                depth += 1;
                if (previous.length <= i || memcmp(previous.pointer, path.pointer, (size_t)i) ||
                    previous.pointer[i] != '/')
                {
                    int child = bq_open_installed_directory(root, (String8){path.pointer, i});
                    struct stat child_info = {0};
                    ok = child >= 0 && fstat(child, &child_info) == 0 &&
                         child_info.st_dev == directory_info.st_dev &&
                         (child_info.st_mode & 0005) == 0005;
                    if (ok)
                    {
                        sha256_add(&identity, &i, sizeof(i));
                        sha256_add(&identity, path.pointer, i);
                        sha256_add(&identity, &child_info.st_dev, sizeof(child_info.st_dev));
                        sha256_add(&identity, &child_info.st_ino, sizeof(child_info.st_ino));
                        closure.directories += 1;
                    }
                    if (child >= 0 && close(child) != 0) ok = false;
                }
            }
        }
        int file = ok ? bq_open_source_file(root, path) : -1;
        struct stat first = {0}, after = {0}, named_info = {0};
        ok = ok && file >= 0 && fstat(file, &first) == 0 && S_ISREG(first.st_mode) &&
             first.st_nlink == 1 && (first.st_uid == 0 || first.st_uid == geteuid()) &&
             !(first.st_mode & 0222) && (first.st_mode & S_IROTH) && first.st_size >= 0 &&
             (u64)first.st_size <= BQ_RETIREMENT_TOOLCHAIN_FILE_CAP &&
             (u64)first.st_size <= BQ_RETIREMENT_TOOLCHAIN_TOTAL_CAP - observed->bytes;
        if (ok)
        {
            Sha256 hash;
            sha256_init(&hash);
            u8 buffer[64u * 1024u];
            u64 done = 0;
            while (ok && done < (u64)first.st_size)
            {
                size_t wanted = (u64)sizeof(buffer) < (u64)first.st_size - done ?
                                sizeof(buffer) : (size_t)((u64)first.st_size - done);
                ssize_t count = pread(file, buffer, wanted, (off_t)done);
                if (count < 0 && errno == EINTR) continue;
                ok = count > 0 && (u64)count <= (u64)first.st_size - done;
                if (ok)
                {
                    sha256_add(&hash, buffer, (u64)count);
                    done += (u64)count;
                }
            }
            char actual[SHA256_HEX_CAPACITY];
            sha256_finish_hex(&hash, (char8*)actual);
            ok = ok && !memcmp(actual, digest.pointer, 64) && fstat(file, &after) == 0 &&
                 first.st_dev == after.st_dev && first.st_ino == after.st_ino &&
                 first.st_size == after.st_size && first.st_mode == after.st_mode &&
                 first.st_mtime == after.st_mtime;
        }
        int named = ok ? bq_open_source_file(root, path) : -1;
        ok = ok && named >= 0 && fstat(named, &named_info) == 0 &&
             named_info.st_dev == first.st_dev && named_info.st_ino == first.st_ino;
        if (named >= 0 && close(named) != 0) ok = false;
        if (file >= 0 && close(file) != 0) ok = false;
        char const* tools[] = {"bin/clang", "bin/cmake", "bin/ld", "bin/ninja"};
        for (u32 i = 0; ok && i < BUSTER_ARRAY_LENGTH(tools); i += 1)
        {
            if (string_equal(path, string_from_pointer(tools[i])))
            {
                required |= 1u << i;
                ok = (first.st_mode & (S_IXUSR | S_IXOTH)) == (S_IXUSR | S_IXOTH);
            }
        }
        if (ok)
        {
            sha256_add(&identity, &path.length, sizeof(path.length));
            sha256_add(&identity, path.pointer, path.length);
            sha256_add(&identity, &first.st_dev, sizeof(first.st_dev));
            sha256_add(&identity, &first.st_ino, sizeof(first.st_ino));
            observed->entries += 1;
            observed->bytes += (u64)first.st_size;
            if (path.length > closure.max_path) closure.max_path = (u32)path.length;
            if (depth > closure.max_depth) closure.max_depth = depth;
            ok = observed->entries <= BQ_RETIREMENT_TOOLCHAIN_FILE_COUNT &&
                 closure.directories <= BQ_SOURCE_DIRECTORY_CAP;
            previous = path;
        }
    }
    closure.entries = observed->entries;
    ok = ok && required == 15u && observed->entries && offset == text.length &&
         bq_retirement_tree_closed(root, "toolchain.manifest", &closure, true) &&
         fstat(manifest, &final_info) == 0 &&
         final_info.st_dev == manifest_info.st_dev && final_info.st_ino == manifest_info.st_ino &&
         final_info.st_size == manifest_info.st_size && final_info.st_mode == manifest_info.st_mode &&
         fstatat(root, "toolchain.manifest", &named_info, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISREG(named_info.st_mode) && named_info.st_dev == manifest_info.st_dev &&
         named_info.st_ino == manifest_info.st_ino;
    if (ok) sha256_finish_hex(&identity, (char8*)observed->identity_sha256);
    if (manifest >= 0 && close(manifest) != 0) ok = false;
    return ok;
}

BqError bq_retirement_toolchain_verify(int installed, String8 profile,
    char const* fixed_root, BqRetirementToolchain* observed)
{
    if (observed) *observed = (BqRetirementToolchain){0};
    char pinned[SHA256_HEX_CAPACITY] = {0};
    BqError result = observed && fixed_root &&
        bq_retirement_profile_sha(profile, S8("toolchain-manifest-sha256="), pinned) ?
        BQ_OK : BQ_RECIPE_MISMATCH;
    int root = result == BQ_OK ? bq_open_installed_directory(installed,
        S8("toolchain/native-retirement-performance-v1")) : -1;
    int absolute = root >= 0 ? bq_open_absolute_directory(string_from_pointer(fixed_root)) : -1;
    struct stat first = {0}, second = {0};
    if (result == BQ_OK)
    {
        result = root >= 0 && absolute >= 0 && fstat(root, &first) == 0 &&
            fstat(absolute, &second) == 0 && first.st_dev == second.st_dev &&
            first.st_ino == second.st_ino && bq_owned_directory(root, false, true) &&
            bq_retirement_toolchain_scan(root, string_from_pointer(pinned), observed) ?
            BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (root >= 0 && close(root) != 0 && result == BQ_OK) result = BQ_IO;
    if (absolute >= 0 && close(absolute) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        size_t size = strlen(fixed_root);
        int count = snprintf(observed->path, sizeof(observed->path), "PATH=%s/bin", fixed_root);
        if (size >= sizeof(observed->root) || count <= 0 || (size_t)count >= sizeof(observed->path))
            result = BQ_CONFIGURATION_MISMATCH;
        else memcpy(observed->root, fixed_root, size + 1);
    }
    if (result != BQ_OK && observed) *observed = (BqRetirementToolchain){0};
    return result;
}

bool bq_retirement_toolchain_recheck(BqRetirementToolchain const* expected)
{
    int root = expected && expected->root[0] ?
        bq_open_absolute_directory(string_from_pointer(expected->root)) : -1;
    BqRetirementToolchain current = {0};
    bool ok = root >= 0 && bq_retirement_toolchain_scan(root,
        string_from_pointer(expected->manifest_sha256), &current) &&
        !memcmp(expected->identity_sha256, current.identity_sha256, SHA256_HEX_CAPACITY) &&
        expected->entries == current.entries && expected->bytes == current.bytes;
    if (root >= 0 && close(root) != 0) ok = false;
    return ok;
}
