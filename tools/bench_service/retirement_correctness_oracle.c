/* #1020 independent native-output producer. The structural ledger joins every
 * eligible native runtime row to a separately built, frozen program and an
 * exact service command. The authority adapter checks the immutable template
 * before binding this attempt; a freshly computed spec digest is not an
 * installed pin. observe checks the held program and completed process log.
 * The caller still owns independent builder provenance, lease, and #509.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "retirement_correctness_oracle.h"
#include "../throughput/retirement_command.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BQ_RETIREMENT_ORACLE_BINARY_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_ORACLE_OUTPUT_CAP (16u * 1024u * 1024u)
#define BQ_RETIREMENT_ORACLE_RECEIPT_CAP (1024u * 1024u)
#define BQ_RETIREMENT_ORACLE_MAX_DEADLINE_NS UINT64_C(3600000000000)

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_hex(char const value[65])
{
    bool ok = value != NULL;
    for (uint32_t i = 0; ok && i < 64; i += 1)
        ok = (value[i] >= '0' && value[i] <= '9') ||
             (value[i] >= 'a' && value[i] <= 'f');
    if (ok) ok = value[64] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_empty(char const value[65])
{
    bool ok = value != NULL;
    for (uint32_t i = 0; ok && i < 65; i += 1) ok = value[i] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_name(
    char const name[BQ_RETIREMENT_OUTPUT_NAME_CAP])
{
    size_t length = name ? strnlen(name, BQ_RETIREMENT_OUTPUT_NAME_CAP) : 0;
    bool ok = length > 0 && length < BQ_RETIREMENT_OUTPUT_NAME_CAP &&
        !(length == 1 && name[0] == '.') &&
        !(length == 2 && name[0] == '.' && name[1] == '.');
    for (size_t i = 0; ok && i < length; i += 1)
        ok = name[i] >= 33 && name[i] <= 126 && name[i] != '/';
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_oracle_number(Sha256* hash, uint32_t value)
{
    uint8_t bytes[4];
    for (uint32_t i = 0; i < 4; i += 1) bytes[i] = (uint8_t)(value >> (i * 8));
    sha256_add(hash, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_same_file(
    struct stat const* first, struct stat const* second)
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

/* pread avoids shared-descriptor seek state. The before/after metadata check
 * also rejects replacement or mutation during the bounded read. */
bool bq_retirement_oracle_file_hash(
    int descriptor, uint64_t cap, bool executable, char digest[65])
{
    struct stat before = {0}, after = {0};
    int fd_flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int file_flags = fd_flags >= 0 ? fcntl(descriptor, F_GETFL) : -1;
    bool ok = digest && fd_flags >= 0 && (fd_flags & FD_CLOEXEC) &&
        file_flags >= 0 && (file_flags & O_ACCMODE) == O_RDONLY &&
        fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_nlink == 1 && (before.st_uid == geteuid() || before.st_uid == 0) &&
        !(before.st_mode & 0222) && before.st_size >= 0 &&
        (uint64_t)before.st_size <= cap &&
        (!executable || (before.st_size > 0 && (before.st_mode & 0111)));
    Sha256 hash;
    if (ok) sha256_init(&hash);
    uint8_t bytes[16384];
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)before.st_size)
    {
        size_t amount = (uint64_t)before.st_size - offset < sizeof(bytes) ?
            (size_t)((uint64_t)before.st_size - offset) : sizeof(bytes);
        ssize_t count = pread(descriptor, bytes, amount, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && (size_t)count <= amount;
        if (ok)
        {
            sha256_add(&hash, bytes, (uint64_t)count);
            offset += (uint64_t)count;
        }
    }
    if (ok) ok = fstat(descriptor, &after) == 0 &&
        bq_retirement_oracle_same_file(&before, &after);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_spec_hash(
    BqRetirementPrepared const* prepared, BqRetirementOracleReference const* references,
    uint32_t count, char digest[65])
{
    bool ok = prepared && references && count && digest &&
        bq_retirement_oracle_hex(prepared->preparation_sha256);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-independent-oracle-ledger-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        sha256_add(&hash, prepared->preparation_sha256, 64);
        sha256_add(&hash, prepared->source_sha256[0], 64);
        sha256_add(&hash, prepared->source_sha256[1], 64);
        sha256_add(&hash, prepared->binary_sha256[0], 64);
        sha256_add(&hash, prepared->binary_sha256[1], 64);
        sha256_add(&hash, prepared->support_sha256, 64);
        sha256_add(&hash, prepared->census_sha256, 64);
        bq_retirement_oracle_number(&hash, prepared->rows);
        bq_retirement_oracle_number(&hash, prepared->object_rows);
        bq_retirement_oracle_number(&hash, prepared->native_target);
        bq_retirement_oracle_number(&hash, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            BqRetirementOracleReference const* reference = references + i;
            bq_retirement_oracle_number(&hash, reference->row);
            bq_retirement_oracle_number(&hash, reference->census_row);
            bq_retirement_oracle_number(&hash, reference->target);
            sha256_add(&hash, reference->preparation_sha256, 64);
            sha256_add(&hash, reference->source_sha256, 64);
            sha256_add(&hash, reference->configuration_sha256, 64);
            sha256_add(&hash, reference->build_receipt_sha256, 64);
            sha256_add(&hash, reference->binary_sha256, 64);
            sha256_add(&hash, reference->command_sha256, 64);
            sha256_add(&hash, reference->build_command_sha256, 65);
            sha256_add(&hash, reference->logical_command_sha256, 65);
            size_t length = strnlen(reference->output_name,
                BQ_RETIREMENT_OUTPUT_NAME_CAP);
            bq_retirement_oracle_number(&hash, (uint32_t)length);
            sha256_add(&hash, reference->output_name, length);
        }
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_begin_internal(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const pinned_sha256[65], char const template_sha256[65],
    uint64_t job_id, uint64_t attempt_token, bool observing)
{
    bool ok = ledger && prepared && rows && references &&
        (observing ? template_sha256 && bq_retirement_oracle_hex(template_sha256) &&
            job_id && attempt_token : pinned_sha256 && bq_retirement_oracle_hex(pinned_sha256)) &&
        count && count <= prepared->rows &&
        prepared->rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP;
    uint32_t matched = 0;
    for (uint32_t index = 0; ok && index < prepared->rows; index += 1)
    {
        BqRetirementTrustedRow const* row = rows + index;
        bool runtime = row->compiler_eligible && row->execution_obligation &&
            row->stage != BQ_RETIREMENT_STAGE_OBJECT &&
            row->target == prepared->native_target;
        ok = row->row == index && bq_retirement_oracle_empty(row->independent_oracle_sha256);
        if (ok && runtime)
        {
            BqRetirementOracleReference const* reference =
                matched < count ? references + matched : NULL;
            ok = reference && reference->row == index &&
                reference->census_row == row->census_row &&
                reference->target == row->target &&
                !memcmp(reference->preparation_sha256, prepared->preparation_sha256, 65) &&
                !memcmp(reference->source_sha256, row->source_sha256, 65) &&
                !memcmp(reference->configuration_sha256, row->configuration_sha256, 65) &&
                (bq_retirement_oracle_hex(reference->build_receipt_sha256) ||
                    (observing && bq_retirement_oracle_empty(reference->build_receipt_sha256))) &&
                (bq_retirement_oracle_hex(reference->binary_sha256) ||
                    (observing && bq_retirement_oracle_empty(reference->binary_sha256))) &&
                (bq_retirement_oracle_hex(reference->command_sha256) ||
                    (observing && bq_retirement_oracle_empty(reference->command_sha256))) &&
                bq_retirement_oracle_name(reference->output_name) &&
                strcmp(reference->binary_sha256, prepared->binary_sha256[0]) &&
                strcmp(reference->binary_sha256, prepared->binary_sha256[1]);
            if (ok) matched += 1;
        }
    }
    if (ok) ok = matched == count;
    char computed[65] = {0};
    if (ok && !observing)
        ok = bq_retirement_oracle_spec_hash(prepared, references, count, computed) &&
            !strcmp(computed, pinned_sha256);
    if (ledger)
    {
        *ledger = (BqRetirementOracleLedger){.failed = !ok};
        if (ok)
        {
            ledger->prepared = *prepared;
            ledger->rows = rows;
            ledger->references = references;
            ledger->count = count;
            ledger->authority_bound = observing;
            ledger->job_id = job_id;
            ledger->attempt_token = attempt_token;
            if (observing) memcpy(ledger->template_sha256, template_sha256, 65);
            else memcpy(ledger->pinned_sha256, pinned_sha256, 65);
        }
    }
    return ok;
}

bool bq_retirement_oracle_begin(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const pinned_sha256[65])
{
    bool ok = bq_retirement_oracle_begin_internal(ledger, prepared, rows,
        references, count, pinned_sha256, NULL, 0, 0, false);
    return ok;
}

/* Called only after the private adapter verifies an independently installed
 * template. The concrete /proc/self/fd command hashes are filled per row. */
bool bq_retirement_oracle_begin_observing(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const template_sha256[65], uint64_t job_id, uint64_t attempt_token)
{
    bool ok = bq_retirement_oracle_begin_internal(ledger, prepared, rows,
        references, count, NULL, template_sha256, job_id, attempt_token, true);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_output(
    BqRetirementRuntimeStart const* start, int descriptor,
    char const name[BQ_RETIREMENT_OUTPUT_NAME_CAP], char digest[65])
{
    struct stat folder = {0}, named = {0}, file = {0}, after = {0};
    int folder_fd = start && start->location.directory >= 3 ?
        fcntl(start->location.directory, F_GETFD) : -1;
    bool ok = start && start->state == BQ_RETIREMENT_RUNTIME_FROZEN &&
        !start->process && start->writer == -1 && start->location.armed == 1 &&
        start->process_group > 0 && kill(-start->process_group, 0) < 0 && errno == ESRCH &&
        bq_retirement_oracle_name(name) &&
        !strcmp(start->location.name, name) &&
        folder_fd >= 0 && (folder_fd & FD_CLOEXEC) &&
        fstat(start->location.directory, &folder) == 0 &&
        S_ISDIR(folder.st_mode) && folder.st_uid == geteuid() &&
        !(folder.st_mode & 0022) &&
        (uint64_t)folder.st_dev == start->location.directory_device &&
        (uint64_t)folder.st_ino == start->location.directory_inode &&
        fstat(descriptor, &file) == 0 &&
        fstatat(start->location.directory, start->location.name, &named,
            AT_SYMLINK_NOFOLLOW) == 0 &&
        bq_retirement_oracle_same_file(&file, &named) &&
        (uint64_t)file.st_dev == start->file_device &&
        (uint64_t)file.st_ino == start->file_inode &&
        file.st_uid == geteuid();
    if (ok) ok = bq_retirement_oracle_file_hash(descriptor,
        BQ_RETIREMENT_ORACLE_OUTPUT_CAP, false, digest);
    if (ok) ok = fstat(descriptor, &after) == 0 &&
        fstatat(start->location.directory, start->location.name, &named,
            AT_SYMLINK_NOFOLLOW) == 0 &&
        bq_retirement_oracle_same_file(&file, &after) &&
        bq_retirement_oracle_same_file(&after, &named);
    return ok;
}

bool bq_retirement_oracle_observe(BqRetirementOracleLedger* ledger,
    int reference_binary, BqRetirementProcessCommand const* command,
    BqRetirementRuntimeStart const* start, int output)
{
    bool ok = ledger && !ledger->failed && !ledger->finished &&
        ledger->done < ledger->count && command && start;
    char binary_sha256[65] = {0}, command_sha256[65] = {0}, output_sha256[65] = {0};
    char executable[64] = {0};
    if (ok) ok = snprintf(executable, sizeof(executable), "/proc/self/fd/%d",
        reference_binary) > 0;
    if (ok) ok = command->arguments && command->argument_count &&
        command->arguments[0] && !strcmp(command->arguments[0], executable) &&
        tp_retirement_command_fields_hash(command->arguments, command->argument_count,
            command->directory, command->environment, command->environment_count,
            command_sha256);
    BqRetirementOracleReference const* reference =
        ok ? ledger->references + ledger->done : NULL;
    if (ok) ok = !strcmp(command_sha256, reference->command_sha256) &&
        !strcmp(command_sha256, start->command_sha256) &&
        bq_retirement_oracle_file_hash(reference_binary,
            BQ_RETIREMENT_ORACLE_BINARY_CAP, true, binary_sha256) &&
        !strcmp(binary_sha256, reference->binary_sha256) &&
        bq_retirement_oracle_output(start, output, reference->output_name,
            output_sha256);
    if (ok)
    {
        BqRetirementTrustedRow* row = ledger->rows + reference->row;
        ok = row->row == reference->row &&
            bq_retirement_oracle_empty(row->independent_oracle_sha256);
        if (ok)
        {
            memcpy(row->independent_oracle_sha256, output_sha256, 65);
            ledger->done += 1;
        }
    }
    if (ledger && !ok) ledger->failed = 1;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_clock(uint64_t* now)
{
    struct timespec time = {0};
    bool ok = now && clock_gettime(CLOCK_MONOTONIC, &time) == 0 &&
        time.tv_sec >= 0 && time.tv_nsec >= 0 && time.tv_nsec < 1000000000L &&
        (uint64_t)time.tv_sec <= (UINT64_MAX - (uint64_t)time.tv_nsec) / UINT64_C(1000000000);
    if (ok) *now = (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_cancellation(int descriptor)
{
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int mode = flags >= 0 ? fcntl(descriptor, F_GETFL) : -1;
    struct stat info = {0};
    struct pollfd input = {.fd = descriptor, .events = POLLIN};
    bool ok = flags >= 0 && (flags & FD_CLOEXEC) && mode >= 0 &&
        (mode & O_ACCMODE) == O_RDONLY && fstat(descriptor, &info) == 0 &&
        (S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode)) &&
        poll(&input, 1, 0) == 0;
    return ok;
}

/* Private to the authority adapter and lower-level fixture. The one-call
 * launcher guard catches an accidental direct-core call; the adapter owns
 * independent builder provenance and immutable template verification. The
 * worker retains whole-job process-group/cgroup cleanup responsibility. */
bool bq_retirement_oracle_produce_next(BqRetirementOracleLedger* ledger,
    int reference_binary, int independent_build_receipt,
    BqRetirementProcessCommand const* command, BqRetirementArtifactLocation output,
    int cancellation_fd, uint64_t absolute_deadline_ns)
{
    bool armed = ledger && ledger->launch_armed == 1 &&
        ledger->launch_row == ledger->done &&
        ledger->launch_count == ledger->count &&
        ledger->launch_job_id == ledger->job_id &&
        ledger->launch_attempt_token == ledger->attempt_token;
    if (ledger)
    {
        ledger->launch_armed = 0;
        ledger->launch_row = 0;
        ledger->launch_count = 0;
        ledger->launch_job_id = 0;
        ledger->launch_attempt_token = 0;
    }
    BqRetirementOracleReference const* reference = armed && ledger->authority_bound && !ledger->failed &&
        !ledger->finished && ledger->done < ledger->count ? ledger->references + ledger->done : NULL;
    uint64_t now = 0;
    char binary_sha256[65] = {0}, receipt_sha256[65] = {0}, command_sha256[65] = {0};
    char executable[64] = {0};
    struct stat receipt = {0};
    bool ok = reference && command && output.name &&
        bq_retirement_oracle_name(output.name) &&
        !strcmp(output.name, reference->output_name) &&
        bq_retirement_oracle_cancellation(cancellation_fd) &&
        bq_retirement_oracle_clock(&now) && absolute_deadline_ns > now &&
        absolute_deadline_ns - now <= BQ_RETIREMENT_ORACLE_MAX_DEADLINE_NS &&
        snprintf(executable, sizeof(executable), "/proc/self/fd/%d", reference_binary) > 0 &&
        command->arguments && command->argument_count && command->arguments[0] &&
        !strcmp(command->arguments[0], executable) &&
        tp_retirement_command_fields_hash(command->arguments, command->argument_count,
            command->directory, command->environment, command->environment_count,
            command_sha256) && !strcmp(command_sha256, reference->command_sha256) &&
        bq_retirement_oracle_file_hash(reference_binary,
            BQ_RETIREMENT_ORACLE_BINARY_CAP, true, binary_sha256) &&
        !strcmp(binary_sha256, reference->binary_sha256) &&
        fstat(independent_build_receipt, &receipt) == 0 && receipt.st_size > 0 &&
        bq_retirement_oracle_file_hash(independent_build_receipt,
            BQ_RETIREMENT_ORACLE_RECEIPT_CAP, false, receipt_sha256) &&
        !strcmp(receipt_sha256, reference->build_receipt_sha256);
    BqRetirementRuntimeStart start = {0};
    int frozen = -1;
    if (ok) ok = bq_retirement_runtime_start(output, &start) &&
                 bq_retirement_oracle_cancellation(cancellation_fd) &&
                 bq_retirement_oracle_clock(&now) && now < absolute_deadline_ns &&
                 bq_retirement_runtime_launch(&start, command);
    int status = 0;
    while (ok && !status)
    {
        status = bq_retirement_runtime_poll(&start);
        ok = status >= 0 && bq_retirement_oracle_clock(&now) &&
             now < absolute_deadline_ns && bq_retirement_oracle_cancellation(cancellation_fd);
        if (ok && !status)
        {
            struct pollfd input = {.fd = cancellation_fd, .events = POLLIN};
            int waited = poll(&input, 1, 10);
            ok = waited == 0 || (waited < 0 && errno == EINTR);
        }
    }
    if (ok) ok = status == 1 && bq_retirement_runtime_finish(&start, &frozen) &&
                 bq_retirement_oracle_observe(ledger, reference_binary, command, &start, frozen);
    if (!ok && start.state == BQ_RETIREMENT_RUNTIME_RUNNING && start.process > 0)
        bq_retirement_runtime_stop_group(&start);
    if (frozen >= 0 && close(frozen) != 0) ok = false;
    bq_retirement_runtime_abort(&start);
    if (ledger && !ok) ledger->failed = 1;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_seal(
    BqRetirementOracleLedger const* ledger, char digest[65])
{
    bool ok = ledger && digest && ledger->count && ledger->done == ledger->count;
    char spec_sha256[65] = {0};
    if (ok) ok = bq_retirement_oracle_spec_hash(&ledger->prepared,
        ledger->references, ledger->count, spec_sha256) &&
        !strcmp(spec_sha256, ledger->pinned_sha256);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-independent-oracle-observed-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        sha256_add(&hash, spec_sha256, 64);
        uint32_t matched = 0;
        for (uint32_t i = 0; ok && i < ledger->prepared.rows; i += 1)
        {
            BqRetirementTrustedRow const* row = ledger->rows + i;
            bool runtime = row->compiler_eligible && row->execution_obligation &&
                row->stage != BQ_RETIREMENT_STAGE_OBJECT &&
                row->target == ledger->prepared.native_target;
            ok = row->row == i;
            if (ok && runtime)
            {
                BqRetirementOracleReference const* reference =
                    matched < ledger->count ? ledger->references + matched : NULL;
                ok = reference && reference->row == i &&
                    row->census_row == reference->census_row &&
                    row->target == reference->target &&
                    !strcmp(row->source_sha256, reference->source_sha256) &&
                    !strcmp(row->configuration_sha256,
                        reference->configuration_sha256) &&
                    bq_retirement_oracle_hex(row->independent_oracle_sha256);
                if (ok) matched += 1;
            }
            else if (ok) ok = bq_retirement_oracle_empty(
                row->independent_oracle_sha256);
            if (ok)
            {
                bq_retirement_oracle_number(&hash, row->row);
                bq_retirement_oracle_number(&hash, row->census_row);
                bq_retirement_oracle_number(&hash, row->target);
                bq_retirement_oracle_number(&hash, row->stage);
                bq_retirement_oracle_number(&hash, row->classification);
                bq_retirement_oracle_number(&hash, row->compiler_eligible);
                bq_retirement_oracle_number(&hash, row->code_obligation);
                bq_retirement_oracle_number(&hash, row->execution_obligation);
                sha256_add(&hash, row->identity_sha256, 65);
                sha256_add(&hash, row->source_sha256, 65);
                sha256_add(&hash, row->configuration_sha256, 65);
                sha256_add(&hash, row->skip_proof_sha256, 65);
                sha256_add(&hash, row->independent_oracle_sha256, 65);
            }
        }
        if (ok) ok = matched == ledger->count;
        if (ok) sha256_finish_hex(&hash, digest);
    }
    if (!ok && digest) digest[0] = 0;
    return ok;
}

bool bq_retirement_oracle_finish(BqRetirementOracleLedger* ledger)
{
    bool spec_ok = true;
    if (ledger && ledger->authority_bound && !ledger->failed && !ledger->finished)
        spec_ok = bq_retirement_oracle_spec_hash(&ledger->prepared, ledger->references,
            ledger->count, ledger->pinned_sha256);
    bool ok = spec_ok && ledger && !ledger->failed && !ledger->finished &&
        bq_retirement_oracle_seal(ledger, ledger->sealed_sha256);
    if (ledger)
    {
        if (ok) ledger->finished = 1;
        else ledger->failed = 1;
    }
    return ok;
}

bool bq_retirement_oracle_ready(BqRetirementOracleLedger const* ledger)
{
    bool ok = ledger && ledger->authority_bound && ledger->finished && !ledger->failed;
    char digest[65] = {0};
    if (ok) ok = bq_retirement_oracle_seal(ledger, digest) &&
        !strcmp(digest, ledger->sealed_sha256);
    return ok;
}
