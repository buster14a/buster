/* Retirement source preparation for #1018.
 *
 * bq_retirement_preflight checks the compiled recipe's inventory pin, both
 * complete manifests and their source bytes, and two-copy storage capacity.
 * bq_retirement_tree_closed checks the entire installed/copied directory
 * closure independently of the manifest paths.
 * bq_retirement_verify_subject makes a second independent copy, checks both
 * copies against that same pin, and removes the temporary copy by inode.
 * bq_retirement_preparation_record retains the verified identities for the
 * later correctness producer. bq_retirement_preparation_ready rereads them
 * under the service lease before unit launch. No executable is selected here.
 * Included from workspace.c after its descriptor and cleanup helpers.
 */
#include "retirement_prepare.h"
#include <dirent.h>
#include <sys/statvfs.h>

#define BQ_RETIREMENT_INVENTORY_CAP 4096u
#define BQ_RETIREMENT_PREPARATION_RECORD_CAP 1024u
#define BQ_RETIREMENT_COPY_OVERHEAD (1024ull * 1024ull)

typedef struct BqRetirementWalk
{
    DIR* stream;
    int parent;
    char name[BQ_PATH_CAP + 1];
    dev_t device;
    ino_t inode;
    u32 path_bytes;
    u32 depth;
} BqRetirementWalk;

/* A manifest must enumerate the entire installed tree. Verify the directory
 * closure independently of the per-path manifest scan, using held descriptors
 * and checking names again before releasing each directory. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_tree_closed(int root, char const* manifest_name,
                                                   BqRetirementSource const* expected)
{
    BqRetirementWalk* stack = calloc(BQ_CLEANUP_DEPTH_CAP + 1, sizeof(*stack));
    struct stat root_info = {0};
    bool ok = stack && fstat(root, &root_info) == 0 && S_ISDIR(root_info.st_mode);
    u32 active = 0, files = 0, directories = 0, manifests = 0;
    if (ok)
    {
        /* A duplicated descriptor shares the directory offset with root.
         * Open the held directory again so repeated readback starts at zero
         * without moving a cursor owned by another consumer. */
        int held = openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        stack[0].stream = held >= 0 ? fdopendir(held) : NULL;
        if (!stack[0].stream && held >= 0) close(held);
        ok = stack[0].stream != NULL;
        stack[0].parent = -1;
        stack[0].device = root_info.st_dev;
        stack[0].inode = root_info.st_ino;
        directories = 1;
        active = ok ? 1 : 0;
    }
    while (ok && active)
    {
        BqRetirementWalk* current = &stack[active - 1];
        errno = 0;
        struct dirent* entry = readdir(current->stream);
        if (!entry)
        {
            ok = errno == 0;
            if (ok && current->parent >= 0)
            {
                struct stat named = {0};
                ok = fstatat(current->parent, current->name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                     S_ISDIR(named.st_mode) && named.st_dev == current->device && named.st_ino == current->inode;
            }
            if (closedir(current->stream) != 0) ok = false;
            current->stream = NULL;
            active -= 1;
        }
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            size_t length = strlen(entry->d_name);
            u32 path_bytes = current->path_bytes + (current->depth ? 1u : 0u) + (u32)length;
            struct stat named = {0};
            int parent = dirfd(current->stream);
            bool manifest = !current->depth && !strcmp(entry->d_name, manifest_name);
            ok = length > 0 && length <= BQ_PATH_CAP && (manifest || path_bytes <= expected->max_path) &&
                 fstatat(parent, entry->d_name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                 named.st_dev == root_info.st_dev &&
                 (named.st_uid == 0 || named.st_uid == geteuid()) && (named.st_mode & 0222) == 0;
            if (ok && S_ISDIR(named.st_mode))
            {
                int child = openat(parent, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                struct stat held_info = {0};
                ok = current->depth + 1 <= expected->max_depth && active <= BQ_CLEANUP_DEPTH_CAP &&
                     ++directories <= expected->directories && child >= 0 && fstat(child, &held_info) == 0 &&
                     held_info.st_dev == named.st_dev && held_info.st_ino == named.st_ino;
                if (ok)
                {
                    BqRetirementWalk* next = &stack[active];
                    next->stream = fdopendir(child);
                    ok = next->stream != NULL;
                    if (ok)
                    {
                        next->parent = parent;
                        memcpy(next->name, entry->d_name, length + 1);
                        next->device = named.st_dev;
                        next->inode = named.st_ino;
                        next->path_bytes = path_bytes;
                        next->depth = current->depth + 1;
                        active += 1;
                    }
                }
                if (!ok && child >= 0 && (!stack[active].stream)) close(child);
            }
            else if (ok && S_ISREG(named.st_mode) && named.st_nlink == 1)
            {
                if (manifest) manifests += 1;
                else files += 1;
                ok = files <= expected->entries && manifests <= 1;
            }
            else ok = false;
        }
    }
    while (active)
    {
        if (closedir(stack[--active].stream) != 0) ok = false;
    }
    free(stack);
    return ok && files == expected->entries && directories == expected->directories && manifests == 1;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_hex(String8 input, u32 size)
{
    bool ok = input.length == size;
    for (u32 index = 0; ok && index < size; index += 1)
    {
        u8 c = input.pointer[index];
        ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_number(String8 input, u64* output)
{
    IntegerParsingU64 parsed = string8_parse_u64_decimal(input);
    bool ok = input.length > 0 && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == input.length;
    *output = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_profile_sha(String8 profile, String8 key, char output[SHA256_HEX_CAPACITY])
{
    u64 offset = 0;
    String8 line = {0};
    bool found = false;
    while (bq_next_line(profile, &offset, &line))
    {
        if (line.length >= key.length && !memcmp(line.pointer, key.pointer, (size_t)key.length))
        {
            String8 digest = {line.pointer + key.length, line.length - key.length};
            if (found || !bq_retirement_hex(digest, 64))
            {
                found = false;
                break;
            }
            memcpy(output, digest.pointer, 64);
            output[64] = 0;
            found = true;
        }
    }
    return found && offset == profile.length;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_subject_line(String8 line, String8 prefix, BqRetirementSource* subject)
{
    String8 fields[8] = {0};
    bool ok = line.length > prefix.length && !memcmp(line.pointer, prefix.pointer, (size_t)prefix.length);
    u32 count = 0;
    u64 start = prefix.length;
    for (u64 index = start; ok && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == ' ')
        {
            ok = count < BUSTER_ARRAY_LENGTH(fields) && index > start;
            if (ok) fields[count++] = (String8){line.pointer + start, index - start};
            start = index + 1;
        }
    }
    ok = ok && count == BUSTER_ARRAY_LENGTH(fields) &&
         (bq_retirement_hex(fields[0], 40) || bq_retirement_hex(fields[0], 64)) &&
         fields[1].length == fields[0].length && bq_retirement_hex(fields[1], (u32)fields[1].length) &&
         bq_retirement_hex(fields[2], 64);
    u64 entries = 0, bytes = 0, directories = 0, path = 0, depth = 0;
    if (ok)
    {
        ok = bq_retirement_number(fields[3], &entries) && bq_retirement_number(fields[4], &bytes) &&
             bq_retirement_number(fields[5], &directories) && bq_retirement_number(fields[6], &path) &&
             bq_retirement_number(fields[7], &depth) &&
             entries > 0 && entries <= BQ_SOURCE_COUNT_CAP && bytes > 0 && bytes <= BQ_SOURCE_TOTAL_CAP &&
             directories > 0 && directories <= BQ_SOURCE_DIRECTORY_CAP &&
             path > 0 && path <= BQ_PATH_CAP && depth > 0 && depth <= BQ_CLEANUP_DEPTH_CAP;
    }
    if (ok)
    {
        memcpy(subject->commit, fields[0].pointer, (size_t)fields[0].length);
        subject->commit[fields[0].length] = 0;
        memcpy(subject->tree, fields[1].pointer, (size_t)fields[1].length);
        subject->tree[fields[1].length] = 0;
        memcpy(subject->manifest_sha256, fields[2].pointer, 64);
        subject->manifest_sha256[64] = 0;
        subject->entries = (u32)entries;
        subject->bytes = bytes;
        subject->directories = (u32)directories;
        subject->max_path = (u32)path;
        subject->max_depth = (u32)depth;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_inventory(String8 text, String8 profile, BqRequest const* request,
                                                 BqRetirementPreparation* preparation)
{
    u64 offset = 0;
    String8 line = {0};
    bool ok = bq_next_line(text, &offset, &line) && string_equal(line, S8("BQ-RETIREMENT-INPUTS-V1")) &&
              bq_next_line(text, &offset, &line) && string_equal(line, S8("repository=buster14a/buster"));
    char support[SHA256_HEX_CAPACITY] = {0}, contract[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support) &&
         bq_retirement_profile_sha(profile, S8("contract-sha256="), contract);
    if (ok)
    {
        ok = bq_next_line(text, &offset, &line) && line.length == 79 &&
             !memcmp(line.pointer, "support-sha256=", 15) && !memcmp(line.pointer + 15, support, 64) &&
             bq_next_line(text, &offset, &line) && line.length == 80 &&
             !memcmp(line.pointer, "contract-sha256=", 16) && !memcmp(line.pointer + 16, contract, 64) &&
             bq_next_line(text, &offset, &line) &&
             bq_retirement_subject_line(line, S8("base="), &preparation->subjects[0]) &&
             bq_next_line(text, &offset, &line) &&
             bq_retirement_subject_line(line, S8("candidate="), &preparation->subjects[1]) &&
             offset == text.length;
    }
    for (u32 subject = 0; ok && subject < 2; subject += 1)
    {
        String8 selected = bq_field(request, 3 + subject);
        String8 expected = string_from_pointer(preparation->subjects[subject].commit);
        ok = string_equal(selected, expected);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_scan(int root, char const* manifest_name, String8 revision,
                                            BqRetirementSource* observed, bool hash_files)
{
    int file = openat(root, manifest_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat manifest_info = {0};
    struct stat root_info = {0};
    u8 bytes[BQ_RETIREMENT_SOURCE_MANIFEST_CAP];
    u32 length = 0;
    bool ok = fstat(root, &root_info) == 0 && S_ISDIR(root_info.st_mode) &&
              file >= 0 && fstat(file, &manifest_info) == 0 && S_ISREG(manifest_info.st_mode) &&
              manifest_info.st_nlink == 1 && (manifest_info.st_uid == 0 || manifest_info.st_uid == geteuid()) &&
              (manifest_info.st_mode & 0222) == 0 && bq_read_file(file, bytes, sizeof(bytes), &length);
    String8 manifest = {(char8*)bytes, length};
    Sha256 identity;
    sha256_init(&identity);
    u64 offset = 0;
    ok = ok && bq_manifest_header(manifest, revision, &offset);
    if (ok)
    {
        sha256_add(&identity, &root_info.st_dev, sizeof(root_info.st_dev));
        sha256_add(&identity, &root_info.st_ino, sizeof(root_info.st_ino));
        sha256_add(&identity, &manifest_info.st_dev, sizeof(manifest_info.st_dev));
        sha256_add(&identity, &manifest_info.st_ino, sizeof(manifest_info.st_ino));
        bq_digest(bytes, length, (char8*)observed->manifest_sha256);
        observed->manifest_bytes = length;
        observed->directories = 1;
    }
    String8 previous = {0};
    while (ok && offset < manifest.length)
    {
        String8 line = {0};
        ok = bq_next_line(manifest, &offset, &line) && line.length > 65 && line.pointer[64] == ' ';
        String8 digest = ok ? (String8){line.pointer, 64} : (String8){0};
        String8 path = ok ? (String8){line.pointer + 65, line.length - 65} : (String8){0};
        u64 common = previous.length < path.length ? previous.length : path.length;
        int compared = ok && previous.length ? memcmp(previous.pointer, path.pointer, (size_t)common) : -1;
        ok = ok && bq_retirement_hex(digest, 64) && bq_relative_source_path(path) &&
             (!previous.length || compared < 0 || (!compared && previous.length < path.length));
        u32 depth = 1;
        for (u64 i = 0; ok && i < path.length; i += 1)
        {
            if (path.pointer[i] == '/')
            {
                depth += 1;
                if (previous.length <= i || memcmp(previous.pointer, path.pointer, (size_t)i) || previous.pointer[i] != '/')
                    observed->directories += 1;
            }
        }
        int input = ok ? bq_open_source_file(root, path) : -1;
        struct stat info = {0}, again = {0};
        ok = ok && input >= 0 && fstat(input, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
             (info.st_uid == 0 || info.st_uid == geteuid()) && (info.st_mode & 0222) == 0 &&
             info.st_size >= 0 && (u64)info.st_size <= BQ_SOURCE_FILE_CAP &&
             (u64)info.st_size <= BQ_SOURCE_TOTAL_CAP - observed->bytes;
        if (ok && hash_files)
        {
            Sha256 hash;
            sha256_init(&hash);
            u8 buffer[64u * 1024u];
            u64 read_bytes = 0;
            while (ok && read_bytes < (u64)info.st_size)
            {
                ssize_t count = read(input, buffer, sizeof(buffer));
                if (count < 0 && errno == EINTR) continue;
                ok = count > 0 && (u64)count <= (u64)info.st_size - read_bytes;
                if (ok)
                {
                    sha256_add(&hash, buffer, (u64)count);
                    read_bytes += (u64)count;
                }
            }
            char8 actual[SHA256_HEX_CAPACITY];
            sha256_finish_hex(&hash, actual);
            ok = ok && !memcmp(actual, digest.pointer, 64) && fstat(input, &again) == 0 &&
                 again.st_dev == info.st_dev && again.st_ino == info.st_ino &&
                 again.st_size == info.st_size && again.st_mtime == info.st_mtime;
        }
        if (ok)
        {
            /* A digest of the held fd alone would accept replacement of its
             * directory entry while the copy is in progress. */
            int named = bq_open_source_file(root, path);
            struct stat named_info = {0};
            ok = named >= 0 && fstat(named, &named_info) == 0 &&
                 named_info.st_dev == info.st_dev && named_info.st_ino == info.st_ino;
            if (named >= 0 && close(named) != 0) ok = false;
        }
        if (input >= 0 && close(input) != 0) ok = false;
        if (ok)
        {
            sha256_add(&identity, &path.length, sizeof(path.length));
            sha256_add(&identity, path.pointer, path.length);
            sha256_add(&identity, &info.st_dev, sizeof(info.st_dev));
            sha256_add(&identity, &info.st_ino, sizeof(info.st_ino));
            observed->entries += 1;
            observed->bytes += (u64)info.st_size;
            if (path.length > observed->max_path) observed->max_path = (u32)path.length;
            if (depth > observed->max_depth) observed->max_depth = depth;
            ok = observed->entries <= BQ_SOURCE_COUNT_CAP && observed->directories <= BQ_SOURCE_DIRECTORY_CAP;
            previous = path;
        }
    }
    ok = ok && observed->entries > 0 && offset == manifest.length &&
         bq_retirement_tree_closed(root, manifest_name, observed);
    if (ok)
    {
        struct stat named = {0};
        ok = fstatat(root, manifest_name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
             S_ISREG(named.st_mode) && named.st_dev == manifest_info.st_dev && named.st_ino == manifest_info.st_ino;
    }
    if (ok) sha256_finish_hex(&identity, (char8*)observed->installed_identity_sha256);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_same_source(BqRetirementSource const* expected,
                                                   BqRetirementSource const* actual)
{
    bool ok = expected->entries == actual->entries && expected->bytes == actual->bytes &&
              expected->directories == actual->directories && expected->max_path == actual->max_path &&
              expected->max_depth == actual->max_depth &&
              !memcmp(expected->manifest_sha256, actual->manifest_sha256, 64);
    return ok;
}

/* The test seam supplies a synthetic compiled profile; production always uses
 * bq_recipe_profile for the exact admitted recipe in the public wrapper. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_preflight_pinned_impl(int installed, int workspaces,
                                                                BqRequest const* request, String8 profile,
                                                                BqRetirementPreparation* preparation, bool reserve_space)
{
    *preparation = (BqRetirementPreparation){0};
    char pinned[SHA256_HEX_CAPACITY] = {0};
    BqError result = bq_retirement_profile_sha(profile, S8("inventory-sha256="), pinned) ? BQ_OK : BQ_RECIPE_MISMATCH;
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int file = recipes >= 0 ? openat(recipes, "native-retirement-performance-v1.inventory",
                                      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    u8 bytes[BQ_RETIREMENT_INVENTORY_CAP];
    u32 length = 0;
    if (result == BQ_OK)
    {
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) && file >= 0 &&
                 fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
                 (info.st_uid == 0 || info.st_uid == geteuid()) && (info.st_mode & 0222) == 0 &&
                 bq_read_file(file, bytes, sizeof(bytes), &length) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    }
    if (result == BQ_OK)
    {
        bq_digest(bytes, length, (char8*)preparation->inventory_sha256);
        result = !memcmp(preparation->inventory_sha256, pinned, 64) &&
                 bq_retirement_inventory((String8){(char8*)bytes, length}, profile, request, preparation) ?
                 BQ_OK : BQ_RECIPE_MISMATCH;
    }
    for (u32 index = 0; result == BQ_OK && index < 2; index += 1)
    {
        String8 revision = bq_field(request, index + 3);
        char name[80];
        u32 size = 0;
        bool path = bq_workspace_append(name, sizeof(name), &size, "sources/", 8) &&
                    bq_workspace_append(name, sizeof(name), &size, (char const*)revision.pointer, revision.length);
        int root = path ? bq_open_installed_directory(installed, string_from_pointer(name)) : -1;
        BqRetirementSource actual = {0};
        bool matches = root >= 0 && bq_owned_directory(root, false, true) &&
                       bq_retirement_scan(root, "source.manifest", revision, &actual, true) &&
                       bq_retirement_same_source(&preparation->subjects[index], &actual);
        if (matches)
        {
            preparation->subjects[index].manifest_bytes = actual.manifest_bytes;
            memcpy(preparation->subjects[index].installed_identity_sha256,
                   actual.installed_identity_sha256, SHA256_HEX_CAPACITY);
        }
        if (root >= 0 && close(root) != 0) matches = false;
        if (!matches) result = BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK)
    {
        u64 sum = preparation->subjects[0].bytes + preparation->subjects[1].bytes +
                  preparation->subjects[0].manifest_bytes + preparation->subjects[1].manifest_bytes;
        u64 entries = preparation->subjects[0].entries + preparation->subjects[1].entries;
        preparation->source_reservation_bytes = 2 * sum + entries * 8192 + BQ_RETIREMENT_COPY_OVERHEAD;
        struct statvfs space = {0};
        result = !reserve_space || (fstatvfs(workspaces, &space) == 0 && space.f_frsize != 0 &&
                 space.f_bavail >= (preparation->source_reservation_bytes + space.f_frsize - 1) / space.f_frsize) ?
                 BQ_OK : BQ_RESOURCE_MISMATCH;
    }
    if (file >= 0 && close(file) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_preflight_pinned(int installed, int workspaces,
                                                           BqRequest const* request, String8 profile,
                                                           BqRetirementPreparation* preparation)
{
    BqError result = bq_retirement_preflight_pinned_impl(installed, workspaces, request, profile,
                                                         preparation, true);
    return result;
}

BqError bq_retirement_preflight(int installed, int workspaces, BqRequest const* request,
                                BqRetirementPreparation* preparation)
{
    String8 profile = bq_recipe_profile(bq_request_recipe(request));
    BqError result = bq_retirement_preflight_pinned(installed, workspaces, request, profile, preparation);
    return result;
}

bool bq_retirement_verify_subject(int installed, int subject, int source, String8 revision,
                                  BqRetirementSource* expected)
{
    BqRetirementSource actual = {0};
    char copied_identity[SHA256_HEX_CAPACITY] = {0};
    struct stat source_info = {0};
    bool ok = fstat(source, &source_info) == 0 &&
              bq_entry_identity(subject, "source", source_info.st_dev, source_info.st_ino) &&
              bq_retirement_scan(source, ".source-manifest", revision, &actual, true) &&
              bq_retirement_same_source(expected, &actual);
    if (ok) memcpy(copied_identity, actual.installed_identity_sha256, SHA256_HEX_CAPACITY);
    bool created = false;
    int second = -1;
    struct stat second_info = {0};
    if (ok)
    {
        created = mkdirat(subject, "verification-source", 0700) == 0;
        second = created ? openat(subject, "verification-source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = second >= 0 && fstat(second, &second_info) == 0 &&
             bq_copy_manifest(installed, second, revision, BQ_RETIREMENT_SOURCE_MANIFEST_CAP) &&
             bq_make_sources_read_only(second);
    }
    if (ok)
    {
        actual = (BqRetirementSource){0};
        char name[80];
        u32 size = 0;
        bool path = bq_workspace_append(name, sizeof(name), &size, "sources/", 8) &&
                    bq_workspace_append(name, sizeof(name), &size, (char const*)revision.pointer, revision.length);
        int installed_source = path ? bq_open_installed_directory(installed, string_from_pointer(name)) : -1;
        BqRetirementSource installed_now = {0};
        ok = bq_retirement_scan(second, ".source-manifest", revision, &actual, true) &&
             bq_retirement_same_source(expected, &actual) &&
             installed_source >= 0 &&
             bq_retirement_scan(installed_source, "source.manifest", revision, &installed_now, true) &&
             bq_retirement_same_source(expected, &installed_now) &&
             !memcmp(expected->installed_identity_sha256, installed_now.installed_identity_sha256, 64) &&
             bq_entry_identity(subject, "source", source_info.st_dev, source_info.st_ino) &&
             bq_entry_identity(subject, "verification-source", second_info.st_dev, second_info.st_ino) &&
             bq_remove_workspace_payload(second) &&
             bq_entry_identity(subject, "verification-source", second_info.st_dev, second_info.st_ino) &&
             unlinkat(subject, "verification-source", AT_REMOVEDIR) == 0 && fsync(subject) == 0;
        if (installed_source >= 0 && close(installed_source) != 0) ok = false;
    }
    if (second >= 0 && close(second) != 0) ok = false;
    if (ok) memcpy(expected->materialized_identity_sha256, copied_identity, SHA256_HEX_CAPACITY);
    /* A failed second copy stays in the sealed attempt for the normal
     * materialization failure cleanup; it cannot be mistaken for readiness. */
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_retirement_preparation_format(char body[BQ_RETIREMENT_PREPARATION_RECORD_CAP], BqJob const* job,
                                                         BqRetirementPreparation const* preparation,
                                                         BqError outcome, u32 completed_subjects)
{
    int length = snprintf(body, BQ_RETIREMENT_PREPARATION_RECORD_CAP,
                          "BQ-RETIREMENT-PREP-V1\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\n"
                          "status=%s\nreason=%s\ncompleted-subjects=%u\n"
                          "inventory=%.64s\nbase=%.64s %.64s %.64s %u %" PRIu64 " %u %u %u %u\n"
                          "candidate=%.64s %.64s %.64s %u %" PRIu64 " %u %u %u %u\n"
                          "base-copy-identity=%.64s\ncandidate-copy-identity=%.64s\n"
                          "source-reservation-bytes=%" PRIu64 "\n",
                          (uint64_t)job->id, (uint64_t)job->token, job->digest,
                          outcome == BQ_OK && completed_subjects == 2 ? "ready" : "failed",
                          bq_error_name(outcome), completed_subjects,
                          preparation->inventory_sha256,
                          preparation->subjects[0].commit, preparation->subjects[0].tree,
                          preparation->subjects[0].manifest_sha256, preparation->subjects[0].entries,
                          (uint64_t)preparation->subjects[0].bytes, preparation->subjects[0].directories,
                          preparation->subjects[0].max_path, preparation->subjects[0].max_depth,
                          preparation->subjects[0].manifest_bytes,
                          preparation->subjects[1].commit, preparation->subjects[1].tree,
                          preparation->subjects[1].manifest_sha256, preparation->subjects[1].entries,
                          (uint64_t)preparation->subjects[1].bytes, preparation->subjects[1].directories,
                          preparation->subjects[1].max_path, preparation->subjects[1].max_depth,
                          preparation->subjects[1].manifest_bytes,
                          preparation->subjects[0].materialized_identity_sha256,
                          preparation->subjects[1].materialized_identity_sha256,
                          (uint64_t)preparation->source_reservation_bytes);
    return length;
}

bool bq_retirement_preparation_record(BqQueue* queue, BqJob const* job,
                                      BqRetirementPreparation const* preparation, BqError outcome, u32 completed_subjects)
{
    char name[48], body[BQ_RETIREMENT_PREPARATION_RECORD_CAP];
    int length = bq_retirement_preparation_format(body, job, preparation, outcome, completed_subjects);
    bool ready = outcome == BQ_OK && completed_subjects == 2;
    bool identities = !ready || (bq_retirement_hex(string_from_pointer(preparation->subjects[0].materialized_identity_sha256), 64) &&
                                 bq_retirement_hex(string_from_pointer(preparation->subjects[1].materialized_identity_sha256), 64));
    bool ok = identities && length > 0 && (u32)length < sizeof(body) &&
              bq_record_name(name, "preparation", job->id) &&
              bq_record_write(queue, name, (u8 const*)body, (u32)length, false) == BQ_OK;
    return ok;
}

/* Read the durable service record again before the worker is launched. A
 * matching inventory alone is insufficient: the already-copied source trees
 * must still be the ones the preparation producer verified. This readback is
 * the service-owned A handoff, not a correctness or build-provenance verdict. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_preparation_ready_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char record_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementPreparation expected = {0};
    char name[48], body[BQ_RETIREMENT_PREPARATION_RECORD_CAP];
    u8 actual[BQ_RETIREMENT_PREPARATION_RECORD_CAP];
    u32 size = 0;
    char8 request_digest[SHA256_HEX_CAPACITY];
    if (record_sha256) record_sha256[0] = 0;
    bool valid = queue && job && record_sha256 && job->id && job->token &&
                 string_equal(bq_field(&job->request, 2), S8("native-retirement-performance-v1"));
    if (valid) bq_request_digest(&job->request, request_digest);
    valid = valid && !memcmp(job->digest, request_digest, 64);
    BqError result = valid ? bq_retirement_preflight_pinned_impl(installed, workspaces, &job->request,
                                                                  profile, &expected, false) : BQ_RECIPE_MISMATCH;
    int length = -1;
    if (result == BQ_OK)
    {
        result = bq_record_name(name, "preparation", job->id) ?
                 bq_record_read(queue, name, actual, sizeof(actual), &size) : BQ_CORRUPT;
        if (result == BQ_OK)
        {
            String8 text = {(char8*)actual, size};
            String8 keys[] = {S8("base-copy-identity="), S8("candidate-copy-identity=")};
            u64 offset = 0;
            String8 line = {0};
            u32 found = 0;
            while (bq_next_line(text, &offset, &line))
            {
                for (u32 side = 0; side < 2; side += 1)
                {
                    if (line.length >= keys[side].length &&
                        !memcmp(line.pointer, keys[side].pointer, (size_t)keys[side].length))
                    {
                        String8 identity = {line.pointer + keys[side].length, line.length - keys[side].length};
                        if ((found & (1u << side)) || !bq_retirement_hex(identity, 64)) result = BQ_CORRUPT;
                        else
                        {
                            memcpy(expected.subjects[side].materialized_identity_sha256,
                                   identity.pointer, 64);
                            expected.subjects[side].materialized_identity_sha256[64] = 0;
                            found |= 1u << side;
                        }
                    }
                }
            }
            length = bq_retirement_preparation_format(body, job, &expected, BQ_OK, 2);
            if (result == BQ_OK && (found != 3 || offset != size || length <= 0 ||
                                    (u32)length != size || memcmp(actual, body, size))) result = BQ_CORRUPT;
        }
    }
    for (u32 subject = 0; result == BQ_OK && subject < 2; subject += 1)
    {
        char path[128];
        int path_length = snprintf(path, sizeof(path), "job-%" PRIu64 "-attempt-%" PRIu64 "/%s/source",
                                   (uint64_t)job->id, (uint64_t)job->token, subject ? "candidate" : "base");
        int source = path_length > 0 && (u32)path_length < sizeof(path) ?
                     bq_open_directory_path(workspaces, string_from_pointer(path)) : -1;
        BqRetirementSource observed = {0};
        String8 revision = bq_field(&job->request, 3 + subject);
        bool match = source >= 0 && bq_owned_directory(source, false, true) &&
                     bq_retirement_scan(source, ".source-manifest", revision, &observed, true) &&
                     bq_retirement_same_source(&expected.subjects[subject], &observed) &&
                     !memcmp(expected.subjects[subject].materialized_identity_sha256,
                             observed.installed_identity_sha256, 64);
        if (source >= 0 && close(source) != 0) match = false;
        if (!match) result = BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK) bq_digest(actual, size, (char8*)record_sha256);
    return result;
}

BqError bq_retirement_preparation_ready(BqQueue* queue, BqJob const* job, int installed, int workspaces,
                                        char record_sha256[SHA256_HEX_CAPACITY])
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_preparation_ready_pinned(queue, job, installed, workspaces,
                                                              profile, record_sha256);
    return result;
}
