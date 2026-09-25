/* #1020 independent native-output producer. The reference ledger joins every
 * eligible native runtime row to a separately built, frozen program and an
 * exact service command. begin checks the installed-policy ledger pin;
 * observe checks the held program and the actual completed process log;
 * finish seals the resulting expectations before the correctness gate begins.
 * The caller owns provenance for the pinned reference build, deadline, lease,
 * and complete semantic execution; this module cannot grant them alone.
 */
#define _POSIX_C_SOURCE 200809L
#include "retirement_correctness_oracle.h"
#include "../throughput/retirement_command.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BQ_RETIREMENT_ORACLE_BINARY_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_ORACLE_OUTPUT_CAP (16u * 1024u * 1024u)

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
BUSTER_GLOBAL_LOCAL bool bq_retirement_oracle_file_hash(
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

bool bq_retirement_oracle_begin(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const pinned_sha256[65])
{
    bool ok = ledger && prepared && rows && references && pinned_sha256 &&
        count && count <= prepared->rows &&
        prepared->rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
        bq_retirement_oracle_hex(pinned_sha256);
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
                bq_retirement_oracle_hex(reference->build_receipt_sha256) &&
                bq_retirement_oracle_hex(reference->binary_sha256) &&
                bq_retirement_oracle_hex(reference->command_sha256) &&
                bq_retirement_oracle_name(reference->output_name) &&
                strcmp(reference->binary_sha256, prepared->binary_sha256[0]) &&
                strcmp(reference->binary_sha256, prepared->binary_sha256[1]);
            if (ok) matched += 1;
        }
    }
    if (ok) ok = matched == count;
    char computed[65] = {0};
    if (ok) ok = bq_retirement_oracle_spec_hash(prepared, references, count, computed) &&
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
            memcpy(ledger->pinned_sha256, pinned_sha256, 65);
        }
    }
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
    bool ok = ledger && !ledger->failed && !ledger->finished &&
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
    bool ok = ledger && ledger->finished && !ledger->failed;
    char digest[65] = {0};
    if (ok) ok = bq_retirement_oracle_seal(ledger, digest) &&
        !strcmp(digest, ledger->sealed_sha256);
    return ok;
}
