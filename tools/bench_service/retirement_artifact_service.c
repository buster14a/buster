/* #1020 frozen-artifact readback. This shares the already reviewed independent
 * ELF/COFF/PE/Mach-O reader used by retirement measurement. It reads a single
 * service-owned file through a held descriptor, checks its name and metadata
 * again after parsing, then commits both sides to the correctness gate.
 * Runtime output is hashed from the completed service-owned process log and
 * compared with an independently established oracle by the gate. Process
 * status, command identity and oracle production remain the trusted service
 * runner's responsibility.
 */
#define _POSIX_C_SOURCE 200809L
#include "retirement_artifact_service.h"
#include "../throughput/retirement_artifact.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BQ_RETIREMENT_ARTIFACT_READ_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_RUNTIME_LOG_CAP (16u * 1024u * 1024u)

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_name(char const* name)
{
    bool ok = name && name[0] && !(name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])));
    for (char const* at = name; ok && *at; at += 1) ok = *at != '/';
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_target(uint32_t target, unsigned stage,
    unsigned* format, unsigned* machine)
{
    bool ok = target >= 1 && target <= 12 &&
        stage >= BQ_RETIREMENT_STAGE_OBJECT && stage <= BQ_RETIREMENT_STAGE_SELF_HOST;
    if (ok)
    {
        bool apple = target == 1 || target == 2 || target == 7 || target == 8;
        bool windows = target == 4 || target == 6 || target == 10 || target == 12;
        *format = apple ? 4 : windows ? stage == BQ_RETIREMENT_STAGE_OBJECT ? 2 : 3 : 1;
        *machine = target <= 6 ? 2 : 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_stable(struct stat const* first, struct stat const* second)
{
    bool ok = first->st_dev == second->st_dev && first->st_ino == second->st_ino &&
        first->st_size == second->st_size && first->st_mode == second->st_mode &&
        first->st_nlink == second->st_nlink && first->st_uid == second->st_uid;
#if defined(__APPLE__)
    ok = ok && first->st_mtimespec.tv_sec == second->st_mtimespec.tv_sec &&
        first->st_mtimespec.tv_nsec == second->st_mtimespec.tv_nsec &&
        first->st_ctimespec.tv_sec == second->st_ctimespec.tv_sec &&
        first->st_ctimespec.tv_nsec == second->st_ctimespec.tv_nsec;
#else
    ok = ok && first->st_mtim.tv_sec == second->st_mtim.tv_sec &&
        first->st_mtim.tv_nsec == second->st_mtim.tv_nsec &&
        first->st_ctim.tv_sec == second->st_ctim.tv_sec &&
        first->st_ctim.tv_nsec == second->st_ctim.tv_nsec;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_read(BqRetirementArtifactLocation location,
    unsigned format, unsigned machine, unsigned executable, TpRetirementArtifact* facts)
{
    bool ok = facts && bq_retirement_artifact_name(location.name) &&
        format >= 1 && format <= 4 && machine >= 1 && machine <= 2 && executable <= 1;
    if (facts) *facts = (TpRetirementArtifact){0};
    int file = ok ? openat(location.directory, location.name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat first = {0}, named = {0}, last = {0}, renamed = {0};
    ok = ok && file >= 0 && fstat(file, &first) == 0 &&
        fstatat(location.directory, location.name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(first.st_mode) && first.st_nlink == 1 && first.st_uid == geteuid() &&
        !(first.st_mode & 0222) && first.st_size > 0 &&
        (uint64_t)first.st_size <= BQ_RETIREMENT_ARTIFACT_READ_CAP &&
        bq_retirement_artifact_stable(&first, &named);
    unsigned char* data = ok ? malloc((size_t)first.st_size) : NULL;
    ok = ok && data != NULL;
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)first.st_size)
    {
        size_t wanted = (uint64_t)first.st_size - offset > 16384 ? 16384 :
                        (size_t)((uint64_t)first.st_size - offset);
        ssize_t count = pread(file, data + offset, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) offset += (uint64_t)count;
    }
    TpRetirementArtifact parsed = {0};
    if (ok) ok = tp_retirement_artifact(data, (uint64_t)first.st_size, &parsed) &&
                 parsed.format == format && parsed.machine == machine && parsed.executable == executable;
    if (ok) ok = fstat(file, &last) == 0 &&
                 fstatat(location.directory, location.name, &renamed, AT_SYMLINK_NOFOLLOW) == 0 &&
                 bq_retirement_artifact_stable(&first, &last) &&
                 bq_retirement_artifact_stable(&last, &renamed);
    free(data);
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok) *facts = parsed;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_runtime_read(int descriptor, char digest[65])
{
    struct stat before = {0}, after = {0};
    int descriptor_flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int access_flags = descriptor_flags >= 0 ? fcntl(descriptor, F_GETFL) : -1;
    bool ok = digest && descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) &&
        access_flags >= 0 && (access_flags & O_ACCMODE) == O_RDONLY &&
        fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_uid == geteuid() && before.st_nlink == 1 && !(before.st_mode & 0222) &&
        before.st_size >= 0 && (uint64_t)before.st_size <= BQ_RETIREMENT_RUNTIME_LOG_CAP;
    Sha256 hash;
    if (ok) sha256_init(&hash);
    unsigned char bytes[16384];
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)before.st_size)
    {
        size_t wanted = (uint64_t)before.st_size - offset < sizeof(bytes) ?
                        (size_t)((uint64_t)before.st_size - offset) : sizeof(bytes);
        ssize_t count = pread(descriptor, bytes, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && (size_t)count <= wanted;
        if (ok)
        {
            sha256_add(&hash, bytes, (uint64_t)count);
            offset += (uint64_t)count;
        }
    }
    if (ok) ok = fstat(descriptor, &after) == 0 && bq_retirement_artifact_stable(&before, &after);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation locations[2], int runtime_outputs[2],
    BqRetirementRowFact const* observed)
{
    bool ok = gate && !gate->failed && !gate->finished && observed && locations && runtime_outputs &&
        gate->checks_done == gate->check_count && gate->rows_done < gate->prepared.rows;
    BqRetirementRowFact row = {0};
    if (ok)
    {
        row = *observed;
        BqRetirementTrustedRow const* trusted = &gate->trusted_rows[gate->rows_done];
        unsigned format = 0, machine = 0;
        bool runtime = trusted->compiler_eligible && trusted->execution_obligation &&
            trusted->stage != BQ_RETIREMENT_STAGE_OBJECT && trusted->target == gate->prepared.native_target;
        ok = !row.code_eligible && bq_retirement_artifact_target(trusted->target,
            trusted->stage, &format, &machine);
        for (unsigned side = 0; ok && side < 2; side += 1)
        {
            BqRetirementObservedSide* output = &row.side[side];
            ok = !output->artifact_sha256[0] && !output->code_sha256[0] && !output->code_bytes &&
                 !output->runtime_output_sha256[0];
            if (ok && trusted->compiler_eligible)
            {
                TpRetirementArtifact facts = {0};
                ok = bq_retirement_artifact_read(locations[side], format, machine,
                    trusted->stage != BQ_RETIREMENT_STAGE_OBJECT, &facts);
                if (ok)
                {
                    memcpy(output->artifact_sha256, facts.file_sha256, 65);
                    if (trusted->code_obligation)
                    {
                        memcpy(output->code_sha256, facts.code_sha256, 65);
                        output->code_bytes = facts.code_bytes;
                    }
                }
            }
            else if (ok) ok = locations[side].name == NULL;
            if (ok) ok = runtime ? bq_retirement_runtime_read(runtime_outputs[side],
                                        output->runtime_output_sha256) : runtime_outputs[side] == -1;
        }
        if (ok) row.code_eligible = trusted->compiler_eligible && trusted->code_obligation &&
                                    row.side[0].code_bytes > 0;
        if (ok) ok = bq_retirement_correctness_row(gate, &row);
    }
    if (gate && !ok) gate->failed = 1;
    return ok;
}
