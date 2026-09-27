/* #1020 independent reference-build authority and observed issuer.
 * The inventory encoder fixes source, configuration and literal command bytes
 * before the job. begin joins its installed pin to A's held source roots and
 * Clang closure. next executes the exact held compiler via the bounded service
 * runner, freezes binary/log/receipt and issues a one-use token. The worker
 * must supply its sandbox/cgroup and retain the lease through final sealing.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "retirement_reference_producer.h"
#include "../throughput/retirement_command.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BQ_REF_SOURCE_CAP (512u * 1024u * 1024u)
#define BQ_REF_BINARY_CAP (512u * 1024u * 1024u)
#define BQ_REF_CLANG_CAP (512u * 1024u * 1024u)
#define BQ_REF_LOG_CAP (16u * 1024u * 1024u)
#define BQ_REF_MAX_DEADLINE_NS UINT64_C(3600000000000)

/* Internal readback shared with the B oracle adapter. */
bool bq_retirement_oracle_file_hash(int descriptor, uint64_t cap,
    bool executable, char digest[65]);

typedef struct BqReferenceSink
{
    Sha256 hash;
    int descriptor;
    uint64_t bytes;
    bool ok;
} BqReferenceSink;

BUSTER_GLOBAL_LOCAL bool bq_ref_hex(char const* value, size_t length)
{
    bool ok = value != NULL;
    for (size_t i = 0; ok && i < length; i += 1)
        ok = (value[i] >= '0' && value[i] <= '9') ||
             (value[i] >= 'a' && value[i] <= 'f');
    if (ok) ok = value[length] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_field(char const* value, size_t capacity)
{
    size_t length = value ? strnlen(value, capacity) : 0;
    bool ok = length && length < capacity;
    for (size_t i = 0; ok && i < length; i += 1)
        ok = (unsigned char)value[i] >= 32 && (unsigned char)value[i] <= 126;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_path(char const* value)
{
    size_t length = value ? strnlen(value, BQ_RETIREMENT_REFERENCE_FIELD_CAP) : 0;
    bool ok = length && length < BQ_RETIREMENT_REFERENCE_FIELD_CAP && value[0] != '/';
    size_t segment = 0;
    for (size_t i = 0; ok && i <= length; i += 1)
    {
        if (i == length || value[i] == '/')
        {
            size_t size = i - segment;
            ok = size && !(size == 1 && value[segment] == '.') &&
                !(size == 2 && value[segment] == '.' && value[segment + 1] == '.');
            segment = i + 1;
        }
        else ok = (value[i] >= 'a' && value[i] <= 'z') ||
            (value[i] >= 'A' && value[i] <= 'Z') ||
            (value[i] >= '0' && value[i] <= '9') ||
            value[i] == '-' || value[i] == '_' || value[i] == '.' || value[i] == '+';
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_folder(int descriptor)
{
    struct stat info = {0};
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    bool ok = flags >= 0 && (flags & FD_CLOEXEC) &&
        fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
        info.st_uid == geteuid() && !(info.st_mode & 0022);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_same(struct stat const* a, struct stat const* b)
{
    bool ok = a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mode == b->st_mode &&
        a->st_nlink == b->st_nlink && a->st_uid == b->st_uid &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
        a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
    return ok;
}

/* Walk each component beneath A's held immutable root: O_NOFOLLOW on only
 * the final file would still follow a replaced intermediate directory. */
BUSTER_GLOBAL_LOCAL int bq_ref_open_source(int root, char const* path)
{
    int current = bq_ref_path(path) && bq_ref_folder(root) ?
        fcntl(root, F_DUPFD_CLOEXEC, 3) : -1;
    size_t start = 0, length = path ?
        strnlen(path, BQ_RETIREMENT_REFERENCE_FIELD_CAP) : 0;
    for (size_t i = 0; current >= 3 && i <= length; i += 1)
    {
        if (i == length || path[i] == '/')
        {
            char segment[BQ_RETIREMENT_REFERENCE_FIELD_CAP] = {0};
            memcpy(segment, path + start, i - start);
            int next = openat(current, segment, O_RDONLY | O_NONBLOCK |
                O_CLOEXEC | O_NOFOLLOW | (i == length ? 0 : O_DIRECTORY));
            if (i != length && next >= 3 && !bq_ref_folder(next))
            {
                close(next);
                next = -1;
            }
            if (close(current) != 0 && next >= 0)
            {
                close(next);
                next = -1;
            }
            current = next;
            start = i + 1;
        }
    }
    return current;
}

BUSTER_GLOBAL_LOCAL void bq_ref_sink_add(BqReferenceSink* sink, void const* bytes, size_t length)
{
    if (sink->ok)
    {
        sink->ok = length <= BQ_RETIREMENT_REFERENCE_INVENTORY_CAP - sink->bytes;
        if (sink->ok) sha256_add(&sink->hash, bytes, length);
        size_t done = 0;
        while (sink->ok && sink->descriptor >= 3 && done < length)
        {
            ssize_t written = pwrite(sink->descriptor, (char const*)bytes + done,
                length - done, (off_t)(sink->bytes + done));
            if (written < 0 && errno == EINTR) continue;
            sink->ok = written > 0;
            if (sink->ok) done += (size_t)written;
        }
        if (sink->ok) sink->bytes += length;
    }
}

BUSTER_GLOBAL_LOCAL void bq_ref_sink_u32(BqReferenceSink* sink, uint32_t value)
{
    unsigned char bytes[4];
    for (unsigned i = 0; i < 4; i += 1) bytes[i] = (unsigned char)(value >> (i * 8));
    bq_ref_sink_add(sink, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void bq_ref_sink_field(BqReferenceSink* sink, char const* field)
{
    size_t length = strlen(field);
    bq_ref_sink_u32(sink, (uint32_t)length);
    bq_ref_sink_add(sink, field, length);
}

BUSTER_GLOBAL_LOCAL bool bq_ref_plan_row(BqRetirementReferencePlanRow const* row,
    BqRetirementOracleTemplateRow const* approved, char digest[65])
{
    bool ok = row && approved && digest && row->row == approved->row &&
        row->source_side < 2 && bq_ref_path(row->source_path) &&
        bq_ref_hex(row->source_sha256, 64) &&
        !strcmp(row->source_sha256, approved->source_sha256) &&
        row->flag_count <= BQ_RETIREMENT_REFERENCE_FLAGS_CAP &&
        row->build_environment_count &&
        row->build_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP &&
        row->runtime_argument_count >= 1 &&
        row->runtime_argument_count <= BQ_RETIREMENT_REFERENCE_ARGS_CAP &&
        row->runtime_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP;
    BqReferenceSink sink = {.descriptor = -1, .ok = ok};
    if (ok)
    {
        sha256_init(&sink.hash);
        static char const domain[] = "bq-reference-build-logical-command-v1";
        bq_ref_sink_add(&sink, domain, sizeof(domain) - 1);
        bq_ref_sink_u32(&sink, row->row);
        bq_ref_sink_u32(&sink, row->source_side);
        bq_ref_sink_field(&sink, row->source_path);
        bq_ref_sink_add(&sink, row->source_sha256, 64);
        bq_ref_sink_u32(&sink, row->flag_count);
        for (uint32_t i = 0; sink.ok && i < row->flag_count; i += 1)
        {
            sink.ok = bq_ref_field(row->flags[i], BQ_RETIREMENT_REFERENCE_FIELD_CAP);
            if (sink.ok) bq_ref_sink_field(&sink, row->flags[i]);
        }
        bq_ref_sink_u32(&sink, row->build_environment_count);
        for (uint32_t i = 0; sink.ok && i < row->build_environment_count; i += 1)
        {
            sink.ok = bq_ref_field(row->build_environment[i],
                BQ_RETIREMENT_REFERENCE_FIELD_CAP) &&
                strchr(row->build_environment[i], '=') != NULL;
            if (sink.ok) bq_ref_sink_field(&sink, row->build_environment[i]);
        }
    }
    if (digest) digest[0] = 0;
    if (sink.ok) sha256_finish_hex(&sink.hash, digest);
    return sink.ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_runtime_command(BqRetirementReferencePlanRow const* row,
    char digest[65])
{
    char* arguments[BQ_RETIREMENT_REFERENCE_ARGS_CAP + 1] = {0};
    char* environment[BQ_RETIREMENT_REFERENCE_ENV_CAP + 1] = {0};
    bool ok = row && digest && row->runtime_argument_count >= 1 &&
        row->runtime_argument_count <= BQ_RETIREMENT_REFERENCE_ARGS_CAP &&
        row->runtime_environment_count <= BQ_RETIREMENT_REFERENCE_ENV_CAP;
    if (ok)
    {
        arguments[0] = (char*)"/bq-retirement-reference-executable-v1";
        for (uint32_t i = 1; ok && i < row->runtime_argument_count; i += 1)
        {
            ok = bq_ref_field(row->runtime_arguments[i - 1],
                BQ_RETIREMENT_REFERENCE_FIELD_CAP);
            arguments[i] = (char*)row->runtime_arguments[i - 1];
        }
        for (uint32_t i = 0; ok && i < row->runtime_environment_count; i += 1)
        {
            ok = bq_ref_field(row->runtime_environment[i],
                BQ_RETIREMENT_REFERENCE_FIELD_CAP) &&
                strchr(row->runtime_environment[i], '=') != NULL;
            environment[i] = (char*)row->runtime_environment[i];
        }
        BqRetirementProcessCommand command = {arguments, environment,
            "/bq-retirement-reference-workdir-v1", row->runtime_argument_count,
            row->runtime_environment_count};
        if (ok) ok = bq_retirement_oracle_logical_command_hash(&command, digest);
    }
    if (!ok && digest) digest[0] = 0;
    return ok;
}

bool bq_retirement_reference_inventory_encode(BqRetirementReferencePlan const* plan,
    BqRetirementReferenceSourceIdentity const* source,
    char const toolchain_identity_sha256[65], int descriptor, char digest[65])
{
    char template_sha256[65] = {0};
    bool ok = plan && source && digest && plan->template && plan->rows &&
        plan->count && plan->count == plan->template->reference_count &&
        bq_ref_hex(toolchain_identity_sha256, 64) &&
        !strcmp(toolchain_identity_sha256,
            plan->template->toolchain_identity_sha256) &&
        bq_ref_hex(plan->clang_sha256, 64) &&
        bq_retirement_oracle_template_hash(plan->template, template_sha256);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = bq_ref_hex(source[side].commit, 40) &&
            bq_ref_hex(source[side].tree, 40) &&
            bq_ref_hex(source[side].manifest_sha256, 64) &&
            !strcmp(source[side].commit, plan->template->source_commit[side]) &&
            !strcmp(source[side].tree, plan->template->source_tree[side]) &&
            !strcmp(source[side].manifest_sha256,
                plan->template->source_sha256[side]);
    if (descriptor >= 0)
    {
        struct stat file = {0};
        int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
        int access = flags >= 0 ? fcntl(descriptor, F_GETFL) : -1;
        ok = ok && flags >= 0 && (flags & FD_CLOEXEC) && access >= 0 &&
            (access & O_ACCMODE) == O_WRONLY && fstat(descriptor, &file) == 0 &&
            S_ISREG(file.st_mode) && file.st_uid == geteuid() &&
            file.st_nlink == 1 && file.st_size == 0;
    }
    BqReferenceSink sink = {.descriptor = descriptor, .ok = ok};
    if (ok)
    {
        sha256_init(&sink.hash);
        static char const domain[] = "BQ-RETIREMENT-REFERENCE-INVENTORY-V1\n";
        bq_ref_sink_add(&sink, domain, sizeof(domain) - 1);
        bq_ref_sink_add(&sink, template_sha256, 64);
        for (unsigned side = 0; side < 2; side += 1)
        {
            bq_ref_sink_add(&sink, source[side].commit, 40);
            bq_ref_sink_add(&sink, source[side].tree, 40);
            bq_ref_sink_add(&sink, source[side].manifest_sha256, 64);
        }
        bq_ref_sink_add(&sink, toolchain_identity_sha256, 64);
        bq_ref_sink_add(&sink, plan->clang_sha256, 64);
        bq_ref_sink_u32(&sink, plan->count);
    }
    for (uint32_t i = 0; sink.ok && i < plan->count; i += 1)
    {
        BqRetirementReferencePlanRow const* row = plan->rows + i;
        BqRetirementOracleTemplateRow const* approved = plan->template->references + i;
        char build_sha256[65] = {0}, runtime_sha256[65] = {0};
        sink.ok = bq_ref_plan_row(row, approved, build_sha256) &&
            !strcmp(build_sha256, approved->build_command_sha256) &&
            bq_ref_runtime_command(row, runtime_sha256) &&
            !strcmp(runtime_sha256, approved->logical_command_sha256);
        if (sink.ok)
        {
            bq_ref_sink_u32(&sink, row->row);
            bq_ref_sink_u32(&sink, row->source_side);
            bq_ref_sink_field(&sink, row->source_path);
            bq_ref_sink_add(&sink, row->source_sha256, 64);
            bq_ref_sink_u32(&sink, row->flag_count);
            for (uint32_t j = 0; j < row->flag_count; j += 1)
                bq_ref_sink_field(&sink, row->flags[j]);
            bq_ref_sink_u32(&sink, row->build_environment_count);
            for (uint32_t j = 0; j < row->build_environment_count; j += 1)
                bq_ref_sink_field(&sink, row->build_environment[j]);
            bq_ref_sink_u32(&sink, row->runtime_argument_count);
            for (uint32_t j = 1; j < row->runtime_argument_count; j += 1)
                bq_ref_sink_field(&sink, row->runtime_arguments[j - 1]);
            bq_ref_sink_u32(&sink, row->runtime_environment_count);
            for (uint32_t j = 0; j < row->runtime_environment_count; j += 1)
                bq_ref_sink_field(&sink, row->runtime_environment[j]);
        }
    }
    if (digest) digest[0] = 0;
    if (sink.ok)
    {
        sha256_finish_hex(&sink.hash, digest);
        if (descriptor >= 3) sink.ok = fsync(descriptor) == 0;
    }
    return sink.ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_source_matches(int root, char const* path,
    char const expected_sha256[65], int* held)
{
    int descriptor = bq_ref_open_source(root, path);
    char actual[65] = {0};
    bool ok = held && descriptor >= 3 &&
        bq_retirement_oracle_file_hash(descriptor, BQ_REF_SOURCE_CAP,
            false, actual) && !strcmp(actual, expected_sha256);
    if (held) *held = ok ? descriptor : -1;
    if (!ok && descriptor >= 0) close(descriptor);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_source_current(
    BqRetirementReferenceProducer const* producer, uint32_t index,
    bool contents)
{
    BqRetirementReferencePlanRow const* row = producer->plan->rows + index;
    int reopened = bq_ref_open_source(producer->source_roots[row->source_side],
        row->source_path);
    struct stat current = {0};
    char sha256[65] = {0};
    bool ok = reopened >= 3 && producer->source_files &&
        producer->source_slots > index && fstat(reopened, &current) == 0 &&
        (uint64_t)current.st_dev == producer->source_files[index].device &&
        (uint64_t)current.st_ino == producer->source_files[index].inode;
    if (ok && contents) ok = bq_retirement_oracle_file_hash(reopened,
        BQ_REF_SOURCE_CAP, false, sha256) &&
        !strcmp(sha256, row->source_sha256);
    if (reopened >= 0 && close(reopened) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_named_file(int folder, char const* name,
    int descriptor, bool executable, char digest[65])
{
    struct stat held = {0}, named = {0}, after = {0};
    bool ok = bq_ref_folder(folder) && descriptor >= 3 &&
        fstat(descriptor, &held) == 0 &&
        fstatat(folder, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        bq_ref_same(&held, &named) && held.st_uid == geteuid() &&
        S_ISREG(held.st_mode) && held.st_nlink == 1 &&
        bq_retirement_oracle_file_hash(descriptor,
            executable ? BQ_REF_BINARY_CAP : BQ_REF_LOG_CAP,
            executable, digest) &&
        fstat(descriptor, &after) == 0 &&
        fstatat(folder, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        bq_ref_same(&held, &after) && bq_ref_same(&after, &named);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_deadline(int cancellation_fd,
    uint64_t deadline_ns)
{
    struct timespec current = {0};
    struct pollfd input = {.fd = cancellation_fd, .events = POLLIN};
    int flags = cancellation_fd >= 3 ? fcntl(cancellation_fd, F_GETFD) : -1;
    int mode = flags >= 0 ? fcntl(cancellation_fd, F_GETFL) : -1;
    struct stat pipe = {0};
    bool ok = flags >= 0 && (flags & FD_CLOEXEC) && mode >= 0 &&
        (mode & O_ACCMODE) == O_RDONLY && fstat(cancellation_fd, &pipe) == 0 &&
        (S_ISFIFO(pipe.st_mode) || S_ISSOCK(pipe.st_mode)) &&
        poll(&input, 1, 0) == 0 &&
        clock_gettime(CLOCK_MONOTONIC, &current) == 0 &&
        current.tv_sec >= 0 && current.tv_nsec >= 0 &&
        current.tv_nsec < 1000000000L &&
        (uint64_t)current.tv_sec <=
            (UINT64_MAX - (uint64_t)current.tv_nsec) / UINT64_C(1000000000);
    if (ok)
    {
        uint64_t now = (uint64_t)current.tv_sec * UINT64_C(1000000000) +
            (uint64_t)current.tv_nsec;
        ok = now < deadline_ns && deadline_ns - now <= BQ_REF_MAX_DEADLINE_NS;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_ref_close(int* descriptor, bool* ok)
{
    if (*descriptor >= 0 && close(*descriptor) != 0) *ok = false;
    *descriptor = -1;
}

bool bq_retirement_reference_producer_begin(BqRetirementReferenceProducer* producer,
    BqRetirementReferencePlan const* plan,
    char const installed_inventory_sha256[65],
    char const installed_template_sha256[65], int inventory,
    BqRetirementReferenceSourceIdentity const* verified_source,
    int const* source_roots, char const verified_toolchain_identity_sha256[65],
    int held_clang, int output_directory,
    BqRetirementReferenceSourceFile* source_workspace, uint32_t source_slots,
    BqRetirementOracleAuthority* authority)
{
    bool fresh = producer && !producer->plan && !producer->failed &&
        !producer->pending && !producer->next;
    char raw_sha256[65] = {0}, encoded_sha256[65] = {0};
    char template_sha256[65] = {0}, clang_sha256[65] = {0};
    BqRetirementOracleLedger const* ledger = authority ? &authority->ledger : NULL;
    bool ok = fresh && plan && plan->template && plan->rows &&
        verified_source && source_roots &&
        installed_inventory_sha256 && installed_template_sha256 &&
        bq_ref_hex(installed_inventory_sha256, 64) &&
        bq_ref_hex(installed_template_sha256, 64) &&
        bq_ref_hex(verified_toolchain_identity_sha256, 64) &&
        authority->template == plan->template && ledger &&
        source_workspace && source_slots >= plan->count &&
        ledger->authority_bound && !ledger->failed && !ledger->finished &&
        ledger->done == 0 && authority->observed_rows == 0 &&
        authority->job_id && authority->attempt_token &&
        plan->count == ledger->count &&
        bq_retirement_oracle_template_hash(plan->template, template_sha256) &&
        !strcmp(template_sha256, installed_template_sha256) &&
        !strcmp(template_sha256, authority->template_sha256) &&
        !strcmp(verified_toolchain_identity_sha256,
            plan->template->toolchain_identity_sha256) &&
        !strcmp(verified_toolchain_identity_sha256,
            authority->toolchain_identity_sha256) &&
        bq_retirement_reference_inventory_encode(plan, verified_source,
            verified_toolchain_identity_sha256, -1, encoded_sha256) &&
        !strcmp(encoded_sha256, installed_inventory_sha256) &&
        bq_retirement_oracle_file_hash(inventory,
            BQ_RETIREMENT_REFERENCE_INVENTORY_CAP, false, raw_sha256) &&
        !strcmp(raw_sha256, installed_inventory_sha256) &&
        bq_ref_folder(source_roots[0]) && bq_ref_folder(source_roots[1]) &&
        bq_ref_folder(output_directory) &&
        bq_retirement_oracle_file_hash(held_clang, BQ_REF_CLANG_CAP,
            true, clang_sha256) && !strcmp(clang_sha256, plan->clang_sha256);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = !strcmp(verified_source[side].commit,
                 plan->template->source_commit[side]) &&
             !strcmp(verified_source[side].tree,
                 plan->template->source_tree[side]) &&
             !strcmp(verified_source[side].manifest_sha256,
                 ledger->prepared.source_sha256[side]);
    for (uint32_t i = 0; ok && i < plan->count; i += 1)
    {
        BqRetirementReferencePlanRow const* row = plan->rows + i;
        int file = bq_ref_open_source(source_roots[row->source_side],
            row->source_path);
        struct stat info = {0};
        ok = file >= 3 && fstat(file, &info) == 0 &&
            S_ISREG(info.st_mode) && info.st_nlink == 1 &&
            (info.st_uid == geteuid() || info.st_uid == 0) &&
            !(info.st_mode & 0222);
        if (ok)
            source_workspace[i] = (BqRetirementReferenceSourceFile){
                (uint64_t)info.st_dev, (uint64_t)info.st_ino};
        if (file >= 0 && close(file) != 0) ok = false;
    }
    if (fresh)
    {
        if (ok)
        {
            producer->plan = plan;
            producer->authority = authority;
            memcpy(producer->source, verified_source, sizeof(producer->source));
            producer->source_roots[0] = source_roots[0];
            producer->source_roots[1] = source_roots[1];
            producer->inventory = inventory;
            producer->clang = held_clang;
            producer->output_directory = output_directory;
            producer->source_files = source_workspace;
            producer->source_slots = source_slots;
            producer->source_file = -1;
            producer->binary_file = -1;
            producer->receipt_file = -1;
            producer->job_id = authority->job_id;
            producer->attempt_token = authority->attempt_token;
            memcpy(producer->installed_inventory_sha256,
                installed_inventory_sha256, 65);
            memcpy(producer->installed_template_sha256,
                installed_template_sha256, 65);
            memcpy(producer->toolchain_identity_sha256,
                verified_toolchain_identity_sha256, 65);
        }
        else producer->failed = 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_command(BqRetirementReferenceProducer const* producer,
    BqRetirementReferencePlanRow const* row, int child_output_directory,
    char const* binary_name,
    char executable[64], char cwd[64], char output[128],
    char* arguments[BQ_RETIREMENT_REFERENCE_FLAGS_CAP + 7],
    char* environment[BQ_RETIREMENT_REFERENCE_ENV_CAP + 1],
    BqRetirementProcessCommand* command)
{
    int exe_length = snprintf(executable, 64, "/proc/self/fd/%d",
        producer->clang);
    int cwd_length = snprintf(cwd, 64, "/proc/self/fd/%d",
        producer->source_roots[row->source_side]);
    int out_length = snprintf(output, 128, "/proc/self/fd/%d/%s",
        child_output_directory, binary_name);
    bool ok = exe_length > 0 && exe_length < 64 &&
        cwd_length > 0 && cwd_length < 64 &&
        out_length > 0 && out_length < 128;
    if (ok)
    {
        uint32_t offset = 0;
        arguments[offset++] = executable;
        for (uint32_t i = 0; i < row->flag_count; i += 1)
            arguments[offset++] = (char*)row->flags[i];
        arguments[offset++] = (char*)"-x";
        arguments[offset++] = (char*)"c";
        arguments[offset++] = (char*)row->source_path;
        arguments[offset++] = (char*)"-o";
        arguments[offset++] = output;
        arguments[offset] = NULL;
        for (uint32_t i = 0; i < row->build_environment_count; i += 1)
            environment[i] = (char*)row->build_environment[i];
        environment[row->build_environment_count] = NULL;
        *command = (BqRetirementProcessCommand){arguments, environment, cwd,
            offset, row->build_environment_count};
        char hash[65] = {0};
        ok = tp_retirement_command_fields_hash(command->arguments,
            command->argument_count, command->directory, command->environment,
            command->environment_count, hash);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_receipt(BqRetirementReferenceProducer const* producer,
    BqRetirementOracleTemplateRow const* approved,
    char const command_sha256[65], char const binary_sha256[65],
    char const log_sha256[65], char const* name,
    int* descriptor, char digest[65])
{
    char bytes[1024] = {0};
    int length = snprintf(bytes, sizeof(bytes),
        "BQ-RETIREMENT-REFERENCE-BUILD-V1\n"
        "job=%llu\nattempt=%llu\nrow=%u\ncensus-row=%u\ntarget=%u\n"
        "preparation=%s\ntemplate=%s\ninventory=%s\ntoolchain=%s\n"
        "clang=%s\nsource=%s\nbuild-command=%s\n"
        "observed-command=%s\nbinary=%s\nlog=%s\n",
        (unsigned long long)producer->job_id,
        (unsigned long long)producer->attempt_token,
        approved->row, approved->census_row, approved->target,
        producer->authority->ledger.prepared.preparation_sha256,
        producer->installed_template_sha256,
        producer->installed_inventory_sha256,
        producer->toolchain_identity_sha256, producer->plan->clang_sha256,
        approved->source_sha256, approved->build_command_sha256,
        command_sha256, binary_sha256, log_sha256);
    bool ok = descriptor && digest && length > 0 &&
        (size_t)length < sizeof(bytes);
    int writer = ok ? openat(producer->output_directory, name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 3;
    size_t done = 0;
    while (ok && done < (size_t)length)
    {
        ssize_t count = write(writer, bytes + done, (size_t)length - done);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) done += (size_t)count;
    }
    if (ok) ok = fsync(writer) == 0 && fchmod(writer, 0400) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    int file = ok ? openat(producer->output_directory, name,
        O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (ok) ok = file >= 3 && bq_ref_named_file(producer->output_directory,
        name, file, false, digest);
    if (descriptor) *descriptor = ok ? file : -1;
    if (!ok && file >= 0) close(file);
    return ok;
}

bool bq_retirement_reference_producer_next(BqRetirementReferenceProducer* producer,
    int cancellation_fd, uint64_t absolute_deadline_ns,
    BqRetirementOracleVerifiedBuild const** issued)
{
    if (issued) *issued = NULL;
    bool ok = producer && producer->plan && producer->authority &&
        !producer->pending && !producer->failed &&
        producer->next < producer->plan->count && issued &&
        producer->authority->ledger.done == producer->next &&
        producer->authority->observed_rows == producer->next &&
        !producer->authority->ledger.failed &&
        bq_ref_deadline(cancellation_fd, absolute_deadline_ns);
    if (ok)
    {
        bq_ref_close(&producer->source_file, &ok);
        bq_ref_close(&producer->binary_file, &ok);
        bq_ref_close(&producer->receipt_file, &ok);
    }
    uint32_t index = ok ? producer->next : 0;
    BqRetirementReferencePlanRow const* row = ok ? producer->plan->rows + index : NULL;
    BqRetirementOracleTemplateRow const* approved = ok ?
        producer->plan->template->references + index : NULL;
    char build_sha256[65] = {0}, runtime_sha256[65] = {0};
    char source_sha256[65] = {0}, clang_sha256[65] = {0};
    char command_sha256[65] = {0}, binary_sha256[65] = {0};
    char log_sha256[65] = {0}, receipt_sha256[65] = {0};
    char binary_name[32] = {0}, log_name[32] = {0}, receipt_name[32] = {0};
    char executable[64] = {0}, cwd[64] = {0}, output[128] = {0};
    char* arguments[BQ_RETIREMENT_REFERENCE_FLAGS_CAP + 7] = {0};
    char* environment[BQ_RETIREMENT_REFERENCE_ENV_CAP + 1] = {0};
    BqRetirementProcessCommand command = {0};
    BqRetirementRuntimeStart start = {0};
    int log_file = -1;
    /* The compiler's linker child needs the output directory across exec.
     * Duplicate only for this bounded process tree; parent keeps its pinned
     * CLOEXEC descriptor and closes the inherited duplicate after wait. */
    int child_output_directory = ok ?
        fcntl(producer->output_directory, F_DUPFD, 3) : -1;
    if (ok) ok = child_output_directory >= 3;
    if (ok) ok = bq_ref_plan_row(row, approved, build_sha256) &&
        !strcmp(build_sha256, approved->build_command_sha256) &&
        bq_ref_runtime_command(row, runtime_sha256) &&
        !strcmp(runtime_sha256, approved->logical_command_sha256) &&
        bq_ref_folder(producer->source_roots[row->source_side]) &&
        bq_ref_folder(producer->output_directory) &&
        bq_retirement_oracle_file_hash(producer->clang,
            BQ_REF_CLANG_CAP, true, clang_sha256) &&
        !strcmp(clang_sha256, producer->plan->clang_sha256) &&
        bq_ref_source_matches(producer->source_roots[row->source_side],
            row->source_path, approved->source_sha256, &producer->source_file);
    struct stat held_source = {0};
    if (ok) ok = producer->source_files &&
        producer->source_slots >= producer->plan->count &&
        fstat(producer->source_file, &held_source) == 0 &&
        (uint64_t)held_source.st_dev == producer->source_files[index].device &&
        (uint64_t)held_source.st_ino == producer->source_files[index].inode &&
        bq_ref_source_current(producer, index, false);
    if (ok) ok = snprintf(binary_name, sizeof(binary_name),
            "reference-%08u", index) > 0 &&
        snprintf(log_name, sizeof(log_name), "reference-log-%08u", index) > 0 &&
        snprintf(receipt_name, sizeof(receipt_name),
            "reference-receipt-%08u", index) > 0 &&
        bq_ref_command(producer, row, child_output_directory,
            binary_name, executable, cwd, output,
            arguments, environment, &command);
    struct stat existing = {0};
    if (ok) ok = fstatat(producer->output_directory, binary_name,
            &existing, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT &&
        fstatat(producer->output_directory, receipt_name,
            &existing, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
    BqRetirementArtifactLocation log = {0};
    if (ok) log = (BqRetirementArtifactLocation){producer->output_directory, log_name};
    if (ok) ok = bq_retirement_runtime_start(log, &start) &&
        bq_ref_deadline(cancellation_fd, absolute_deadline_ns) &&
        bq_retirement_runtime_launch(&start, &command) &&
        tp_retirement_command_fields_hash(command.arguments,
            command.argument_count, command.directory,
            command.environment, command.environment_count, command_sha256) &&
        !strcmp(command_sha256, start.command_sha256);
    int status = 0;
    while (ok && !status)
    {
        status = bq_retirement_runtime_poll(&start);
        ok = status >= 0 && bq_ref_deadline(cancellation_fd, absolute_deadline_ns);
        if (ok && !status)
        {
            struct pollfd pause = {.fd = cancellation_fd, .events = POLLIN};
            int waited = poll(&pause, 1, 10);
            ok = waited == 0 || (waited < 0 && errno == EINTR);
        }
    }
    if (ok) ok = status == 1 && bq_retirement_runtime_finish(&start, &log_file) &&
        bq_ref_named_file(producer->output_directory, log_name,
            log_file, false, log_sha256);
    int binary = ok ? openat(producer->output_directory, binary_name,
        O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat result = {0};
    if (ok) ok = binary >= 3 && fstat(binary, &result) == 0 &&
        S_ISREG(result.st_mode) && result.st_uid == geteuid() &&
        result.st_nlink == 1 && !(result.st_mode & 0022) &&
        result.st_size > 0 && fchmod(binary, 0500) == 0 &&
        bq_ref_named_file(producer->output_directory, binary_name,
            binary, true, binary_sha256);
    if (ok) ok = bq_retirement_oracle_file_hash(producer->source_file,
            BQ_REF_SOURCE_CAP, false, source_sha256) &&
        !strcmp(source_sha256, row->source_sha256) &&
        bq_ref_deadline(cancellation_fd, absolute_deadline_ns) &&
        bq_ref_receipt(producer, approved, command_sha256, binary_sha256,
            log_sha256, receipt_name, &producer->receipt_file,
            receipt_sha256);
    if (!ok && start.state == BQ_RETIREMENT_RUNTIME_RUNNING && start.process > 0)
        bq_retirement_runtime_stop_group(&start);
    bq_retirement_runtime_abort(&start);
    if (child_output_directory >= 0 && close(child_output_directory) != 0) ok = false;
    if (log_file >= 0 && close(log_file) != 0) ok = false;
    if (ok)
    {
        producer->binary_file = binary;
        BqRetirementOracleVerifiedBuild* token = &producer->token;
        *token = (BqRetirementOracleVerifiedBuild){0};
        token->producer = producer;
        token->job_id = producer->job_id;
        token->attempt_token = producer->attempt_token;
        token->row = approved->row;
        token->census_row = approved->census_row;
        token->target = approved->target;
        token->source = producer->source_file;
        token->binary = binary;
        token->receipt = producer->receipt_file;
        memcpy(token->preparation_sha256,
            producer->authority->ledger.prepared.preparation_sha256, 65);
        memcpy(token->source_sha256, source_sha256, 65);
        memcpy(token->configuration_sha256, approved->configuration_sha256, 65);
        memcpy(token->toolchain_identity_sha256,
            producer->toolchain_identity_sha256, 65);
        memcpy(token->build_command_sha256, build_sha256, 65);
        memcpy(token->binary_sha256, binary_sha256, 65);
        memcpy(token->receipt_sha256, receipt_sha256, 65);
        producer->pending = 1;
        *issued = token;
    }
    else
    {
        if (binary >= 0) close(binary);
        if (producer)
        {
            producer->failed = 1;
            bq_ref_close(&producer->source_file, &ok);
            bq_ref_close(&producer->receipt_file, &ok);
            if (producer->authority) producer->authority->ledger.failed = 1;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_ref_token_valid(
    BqRetirementOracleVerifiedBuild const* token,
    BqRetirementOracleAuthority const* authority, bool observed)
{
    BqRetirementReferenceProducer const* producer = token ? token->producer : NULL;
    uint32_t index = producer ? producer->next : 0;
    BqRetirementReferencePlanRow const* row = producer && producer->plan &&
        index < producer->plan->count ? producer->plan->rows + index : NULL;
    BqRetirementOracleTemplateRow const* approved = producer &&
        producer->plan && producer->plan->template &&
        index < producer->plan->template->reference_count ?
        producer->plan->template->references + index : NULL;
    char build_sha256[65] = {0}, runtime_sha256[65] = {0};
    char source_sha256[65] = {0}, binary_sha256[65] = {0};
    char receipt_sha256[65] = {0}, binary_name[32] = {0};
    char receipt_name[32] = {0};
    bool ok = token && producer && authority && row && approved &&
        token == &producer->token && producer->pending == 1 &&
        !producer->failed && producer->authority == authority &&
        authority->template == producer->plan->template &&
        authority->ledger.done == index + (uint32_t)observed &&
        authority->observed_rows == index &&
        !authority->ledger.failed &&
        producer->job_id == authority->job_id &&
        producer->attempt_token == authority->attempt_token &&
        token->job_id == producer->job_id &&
        token->attempt_token == producer->attempt_token &&
        token->row == approved->row &&
        token->census_row == approved->census_row &&
        token->target == approved->target &&
        token->source == producer->source_file &&
        producer->source_files && producer->source_slots >= producer->plan->count &&
        token->binary == producer->binary_file &&
        token->receipt == producer->receipt_file &&
        !strcmp(token->preparation_sha256,
            authority->ledger.prepared.preparation_sha256) &&
        !strcmp(token->configuration_sha256,
            approved->configuration_sha256) &&
        !strcmp(token->toolchain_identity_sha256,
            producer->toolchain_identity_sha256) &&
        bq_ref_plan_row(row, approved, build_sha256) &&
        !strcmp(build_sha256, approved->build_command_sha256) &&
        !strcmp(build_sha256, token->build_command_sha256) &&
        bq_ref_runtime_command(row, runtime_sha256) &&
        !strcmp(runtime_sha256, approved->logical_command_sha256) &&
        !strcmp(token->source_sha256, approved->source_sha256) &&
        bq_retirement_oracle_file_hash(producer->source_file,
            BQ_REF_SOURCE_CAP, false, source_sha256) &&
        bq_ref_source_current(producer, index, false) &&
        !strcmp(source_sha256, approved->source_sha256) &&
        snprintf(binary_name, sizeof(binary_name), "reference-%08u", index) > 0 &&
        snprintf(receipt_name, sizeof(receipt_name),
            "reference-receipt-%08u", index) > 0 &&
        bq_ref_named_file(producer->output_directory, binary_name,
            producer->binary_file, true, binary_sha256) &&
        !strcmp(binary_sha256, token->binary_sha256) &&
        bq_ref_named_file(producer->output_directory, receipt_name,
            producer->receipt_file, false, receipt_sha256) &&
        !strcmp(receipt_sha256, token->receipt_sha256);
    return ok;
}

bool bq_retirement_reference_producer_token_valid(
    BqRetirementOracleVerifiedBuild const* token,
    BqRetirementOracleAuthority const* authority)
{
    bool ok = bq_ref_token_valid(token, authority, false);
    return ok;
}

bool bq_retirement_reference_producer_consume(
    BqRetirementOracleVerifiedBuild const* token)
{
    BqRetirementReferenceProducer* producer = token ? token->producer : NULL;
    bool ok = producer && bq_ref_token_valid(token,
        producer->authority, true);
    if (ok)
    {
        producer->pending = 0;
        producer->next += 1;
    }
    else if (producer) producer->failed = 1;
    return ok;
}

bool bq_retirement_reference_producer_ready(
    BqRetirementReferenceProducer const* producer)
{
    char raw_sha256[65] = {0}, encoded_sha256[65] = {0};
    char clang_sha256[65] = {0};
    bool ok = producer && producer->plan && producer->authority &&
        !producer->failed && !producer->pending &&
        producer->next == producer->plan->count &&
        producer->job_id == producer->authority->job_id &&
        producer->attempt_token == producer->authority->attempt_token &&
        bq_retirement_oracle_authority_ready(producer->authority) &&
        bq_retirement_reference_inventory_encode(producer->plan,
            producer->source, producer->toolchain_identity_sha256,
            -1, encoded_sha256) &&
        !strcmp(encoded_sha256, producer->installed_inventory_sha256) &&
        bq_retirement_oracle_file_hash(producer->inventory,
            BQ_RETIREMENT_REFERENCE_INVENTORY_CAP, false, raw_sha256) &&
        !strcmp(raw_sha256, producer->installed_inventory_sha256) &&
        bq_retirement_oracle_file_hash(producer->clang,
            BQ_REF_CLANG_CAP, true, clang_sha256) &&
        !strcmp(clang_sha256, producer->plan->clang_sha256) &&
        bq_ref_folder(producer->source_roots[0]) &&
        bq_ref_folder(producer->source_roots[1]) &&
        bq_ref_folder(producer->output_directory);
    for (uint32_t i = 0; ok && i < producer->plan->count; i += 1)
        ok = bq_ref_source_current(producer, i, true);
    return ok;
}

bool bq_retirement_reference_producer_release(
    BqRetirementReferenceProducer* producer)
{
    bool ok = producer != NULL;
    if (producer)
    {
        bq_ref_close(&producer->source_file, &ok);
        bq_ref_close(&producer->binary_file, &ok);
        bq_ref_close(&producer->receipt_file, &ok);
        producer->pending = 0;
        producer->failed = 1;
    }
    return ok;
}
