/* Frozen compiler output readback for #1018.
 *
 * observe rereads A and streams the two fixed service-owned outputs; record
 * durably binds their content and inode identities to that exact attempt;
 * import repeats the observation and compares the canonical record. The
 * integrator must authenticate the trusted Clang stages, tool closure and
 * build logs separately before recording or passing these files to B.
 */
#include "retirement_binaries.h"

#define BQ_RETIREMENT_BINARY_BYTES_CAP (512ull * 1024ull * 1024ull)
#define BQ_RETIREMENT_BINARIES_RECORD_CAP 1024u

BUSTER_GLOBAL_LOCAL bool bq_retirement_binary_stable(struct stat const* before, struct stat const* after)
{
    bool ok = before->st_dev == after->st_dev && before->st_ino == after->st_ino &&
              before->st_size == after->st_size && before->st_mode == after->st_mode &&
              after->st_nlink == 1 && after->st_uid == geteuid();
#if defined(__APPLE__)
    ok = ok && before->st_mtimespec.tv_sec == after->st_mtimespec.tv_sec &&
         before->st_mtimespec.tv_nsec == after->st_mtimespec.tv_nsec &&
         before->st_ctimespec.tv_sec == after->st_ctimespec.tv_sec &&
         before->st_ctimespec.tv_nsec == after->st_ctimespec.tv_nsec;
#else
    ok = ok && before->st_mtim.tv_sec == after->st_mtim.tv_sec &&
         before->st_mtim.tv_nsec == after->st_mtim.tv_nsec &&
         before->st_ctim.tv_sec == after->st_ctim.tv_sec &&
         before->st_ctim.tv_nsec == after->st_ctim.tv_nsec;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_frozen_binary_open(int directory, char const* name,
    char bytes_sha256[SHA256_HEX_CAPACITY], char identity_sha256[SHA256_HEX_CAPACITY], int* held)
{
    *held = -1;
    int file = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    bool raised = true;
    if (file >= 0 && file < 3)
    {
        int higher = fcntl(file, F_DUPFD_CLOEXEC, 3);
        raised = close(file) == 0;
        file = higher;
    }
    struct stat named = {0}, first = {0}, last = {0}, after = {0};
    bool ok = raised && file >= 3 && fstatat(directory, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
              fstat(file, &first) == 0 && S_ISREG(first.st_mode) && first.st_nlink == 1 &&
              first.st_uid == geteuid() && (first.st_mode & 0222) == 0 &&
              (first.st_mode & S_IXUSR) != 0 && first.st_size > 0 &&
              (u64)first.st_size <= BQ_RETIREMENT_BINARY_BYTES_CAP &&
              named.st_dev == first.st_dev && named.st_ino == first.st_ino;
    Sha256 content, identity;
    if (ok) sha256_init(&content);
    u8 buffer[16384];
    u64 offset = 0;
    while (ok && offset < (u64)first.st_size)
    {
        u64 remaining = (u64)first.st_size - offset;
        size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        ssize_t count = pread(file, buffer, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok)
        {
            sha256_add(&content, buffer, (u64)count);
            offset += (u64)count;
        }
    }
    ok = ok && fstat(file, &last) == 0 && fstatat(directory, name, &after, AT_SYMLINK_NOFOLLOW) == 0 &&
         bq_retirement_binary_stable(&first, &last) && bq_retirement_binary_stable(&last, &after);
    if (ok)
    {
        sha256_finish_hex(&content, (char8*)bytes_sha256);
        sha256_init(&identity);
        sha256_add(&identity, &first.st_dev, sizeof(first.st_dev));
        sha256_add(&identity, &first.st_ino, sizeof(first.st_ino));
        sha256_add(&identity, &first.st_size, sizeof(first.st_size));
        sha256_finish_hex(&identity, (char8*)identity_sha256);
    }
    if (ok) *held = file;
    else if (file >= 0) close(file);
    if (!ok)
    {
        bytes_sha256[0] = 0;
        identity_sha256[0] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_frozen_binary(int directory, char const* name,
    char bytes_sha256[SHA256_HEX_CAPACITY], char identity_sha256[SHA256_HEX_CAPACITY])
{
    int held = -1;
    bool ok = bq_retirement_frozen_binary_open(directory, name, bytes_sha256, identity_sha256, &held);
    if (held >= 0 && close(held) != 0) ok = false;
    if (!ok)
    {
        bytes_sha256[0] = 0;
        identity_sha256[0] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_binaries_observe(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    BqRetirementBinaries* observed)
{
    *observed = (BqRetirementBinaries){0};
    BqRetirementPreparation prepared = {0};
    BqError result = bq_retirement_preparation_import_pinned(queue, job, installed, workspaces,
                                                               profile, preparation_sha256, &prepared);
    int attempt = -1, directory = -1;
    if (result == BQ_OK)
    {
        char name[64];
        bool path = bq_workspace_name(name, job->id, job->token);
        attempt = path ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        directory = attempt >= 0 && bq_owned_directory(attempt, true, false) ?
                    openat(attempt, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        result = directory >= 0 && bq_owned_directory(directory, true, true) ? BQ_OK : BQ_SOURCE_MISMATCH;
    }
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        bool ok = bq_retirement_frozen_binary(directory, side ? "candidate-ide" : "base-ide",
            observed->binary_sha256[side], observed->binary_identity_sha256[side]);
        if (!ok) result = BQ_SOURCE_MISMATCH;
    }
    if (directory >= 0 && close(directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        BqRetirementPreparation repeated = {0};
        result = bq_retirement_preparation_import_pinned(queue, job, installed, workspaces,
                                                          profile, preparation_sha256, &repeated);
        if (result == BQ_OK && (memcmp(prepared.inventory_sha256, repeated.inventory_sha256, SHA256_HEX_CAPACITY) ||
            memcmp(prepared.subjects[0].installed_identity_sha256,
                   repeated.subjects[0].installed_identity_sha256, SHA256_HEX_CAPACITY) ||
            memcmp(prepared.subjects[1].installed_identity_sha256,
                   repeated.subjects[1].installed_identity_sha256, SHA256_HEX_CAPACITY) ||
            memcmp(prepared.subjects[0].materialized_identity_sha256,
                   repeated.subjects[0].materialized_identity_sha256, SHA256_HEX_CAPACITY) ||
            memcmp(prepared.subjects[1].materialized_identity_sha256,
                   repeated.subjects[1].materialized_identity_sha256, SHA256_HEX_CAPACITY))) result = BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK)
    {
        memcpy(observed->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
        for (u32 side = 0; side < 2; side += 1)
            memcpy(observed->source_sha256[side], prepared.subjects[side].manifest_sha256, SHA256_HEX_CAPACITY);
    }
    else *observed = (BqRetirementBinaries){0};
    return result;
}

BUSTER_GLOBAL_LOCAL int bq_retirement_binaries_format(char body[BQ_RETIREMENT_BINARIES_RECORD_CAP],
    BqJob const* job, BqRetirementBinaries const* binaries)
{
    int length = snprintf(body, BQ_RETIREMENT_BINARIES_RECORD_CAP,
                          "BQ-RETIREMENT-BINARIES-V1\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\n"
                          "preparation=%.64s\nbase-source=%.64s\ncandidate-source=%.64s\n"
                          "base-binary=%.64s %.64s\ncandidate-binary=%.64s %.64s\n",
                          (uint64_t)job->id, (uint64_t)job->token, job->digest,
                          binaries->preparation_sha256, binaries->source_sha256[0], binaries->source_sha256[1],
                          binaries->binary_sha256[0], binaries->binary_identity_sha256[0],
                          binaries->binary_sha256[1], binaries->binary_identity_sha256[1]);
    return length;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_binaries_record_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char record_sha256[SHA256_HEX_CAPACITY])
{
    if (record_sha256) record_sha256[0] = 0;
    BqRetirementBinaries observed = {0};
    BqError result = record_sha256 ? bq_retirement_binaries_observe(queue, job, installed, workspaces,
                                profile, preparation_sha256, &observed) : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        char name[48], body[BQ_RETIREMENT_BINARIES_RECORD_CAP];
        int length = bq_retirement_binaries_format(body, job, &observed);
        result = length > 0 && (u32)length < sizeof(body) && bq_record_name(name, "binaries", job->id) ?
                 bq_record_write(queue, name, (u8 const*)body, (u32)length, false) : BQ_IO;
        if (result == BQ_OK) bq_digest(body, (u32)length, (char8*)record_sha256);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_binaries_import_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementBinaries* verified)
{
    if (verified) *verified = (BqRetirementBinaries){0};
    bool valid = verified && record_sha256 && strnlen(record_sha256, SHA256_HEX_CAPACITY) == 64 &&
                 bq_retirement_hex((String8){(char8*)record_sha256, 64}, 64);
    BqRetirementBinaries observed = {0};
    BqError result = valid ? bq_retirement_binaries_observe(queue, job, installed, workspaces,
                                profile, preparation_sha256, &observed) : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        char name[48], body[BQ_RETIREMENT_BINARIES_RECORD_CAP], digest[SHA256_HEX_CAPACITY];
        u8 actual[BQ_RETIREMENT_BINARIES_RECORD_CAP];
        u32 size = 0;
        int length = bq_retirement_binaries_format(body, job, &observed);
        result = bq_record_name(name, "binaries", job->id) ?
                 bq_record_read(queue, name, actual, sizeof(actual), &size) : BQ_CORRUPT;
        if (result == BQ_OK)
        {
            bq_digest(actual, size, (char8*)digest);
            result = length > 0 && (u32)length == size && !memcmp(actual, body, size) &&
                     !memcmp(record_sha256, digest, SHA256_HEX_CAPACITY) ? BQ_OK : BQ_CORRUPT;
        }
    }
    if (result == BQ_OK) *verified = observed;
    return result;
}

BqError bq_retirement_binaries_record(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char record_sha256[SHA256_HEX_CAPACITY])
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_binaries_record_pinned(queue, job, installed, workspaces,
                          profile, preparation_sha256, record_sha256);
    return result;
}

BqError bq_retirement_binaries_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementBinaries* verified)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_binaries_import_pinned(queue, job, installed, workspaces,
                          profile, preparation_sha256, record_sha256, verified);
    return result;
}

void bq_retirement_binaries_release(BqRetirementHeldBinaries* held)
{
    if (held)
    {
        for (u32 side = 0; held->owned == 1 && side < 2; side += 1)
        {
            if (held->descriptors[side] >= 0) close(held->descriptors[side]);
        }
        *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    }
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_binaries_acquire_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementHeldBinaries* held)
{
    BqError result = held && !held->owned ? BQ_OK : BQ_RECIPE_MISMATCH;
    bool started = result == BQ_OK;
    if (started)
    {
        *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        held->owned = 1;
    }
    BqRetirementBinaries verified = {0};
    if (result == BQ_OK)
        result = bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, profile,
                                                       preparation_sha256, record_sha256, &verified);
    int attempt = -1, directory = -1;
    if (result == BQ_OK)
    {
        char name[64];
        attempt = bq_workspace_name(name, job->id, job->token) ?
                  openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        directory = attempt >= 0 && bq_owned_directory(attempt, true, false) ?
                    openat(attempt, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        result = directory >= 0 && bq_owned_directory(directory, true, true) ? BQ_OK : BQ_SOURCE_MISMATCH;
    }
    for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
    {
        char bytes[SHA256_HEX_CAPACITY] = {0}, identity[SHA256_HEX_CAPACITY] = {0};
        bool ok = bq_retirement_frozen_binary_open(directory, side ? "candidate-ide" : "base-ide",
                   bytes, identity, &held->descriptors[side]);
        result = ok && !memcmp(bytes, verified.binary_sha256[side], SHA256_HEX_CAPACITY) &&
                 !memcmp(identity, verified.binary_identity_sha256[side], SHA256_HEX_CAPACITY) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    }
    if (directory >= 0 && close(directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
    if (result == BQ_OK)
    {
        BqRetirementBinaries current = {0};
        result = bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, profile,
                                                       preparation_sha256, record_sha256, &current);
        bool same = result == BQ_OK &&
                    !memcmp(current.preparation_sha256, verified.preparation_sha256, SHA256_HEX_CAPACITY);
        for (u32 side = 0; same && side < 2; side += 1)
            same = !memcmp(current.source_sha256[side], verified.source_sha256[side], SHA256_HEX_CAPACITY) &&
                   !memcmp(current.binary_sha256[side], verified.binary_sha256[side], SHA256_HEX_CAPACITY) &&
                   !memcmp(current.binary_identity_sha256[side], verified.binary_identity_sha256[side],
                           SHA256_HEX_CAPACITY);
        if (result == BQ_OK && !same) result = BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK) held->verified = verified;
    else if (started) bq_retirement_binaries_release(held);
    return result;
}

BqError bq_retirement_binaries_acquire(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementHeldBinaries* held)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces,
                          profile, preparation_sha256, record_sha256, held);
    return result;
}
