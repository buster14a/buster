/* #1020 private authority adapter.
 * template_hash/population_hash bind installed static policy; begin/next/finish
 * join one observed attempt to an opaque independently issued build token.
 * This file intentionally has no production token issuer or policy pin.
 */
#define _POSIX_C_SOURCE 200809L
#include "retirement_oracle_authority.h"
#include "../throughput/retirement_command.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BQ_ORACLE_SOURCE_CAP (512u * 1024u * 1024u)
#define BQ_ORACLE_BINARY_CAP (512u * 1024u * 1024u)
#define BQ_ORACLE_RECEIPT_CAP (1024u * 1024u)

/* This type is deliberately incomplete in the header. A reviewed independent
 * builder must gain a production issuer in a later change. */
struct BqRetirementOracleVerifiedBuild
{
    uint64_t job_id, attempt_token;
    uint32_t row, census_row, target;
    int source, binary, receipt;
    char preparation_sha256[65], source_sha256[65];
    char configuration_sha256[65];
    char toolchain_identity_sha256[65], build_command_sha256[65];
    char binary_sha256[65], receipt_sha256[65];
};

/* Core entries are private to the B producer and are not public recipe APIs. */
bool bq_retirement_oracle_begin_observing(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const template_sha256[65], uint64_t job_id, uint64_t attempt_token);
bool bq_retirement_oracle_file_hash(int descriptor, uint64_t cap,
    bool executable, char digest[65]);

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_hex(char const* value, size_t length)
{
    bool ok = value != NULL;
    for (size_t i = 0; ok && i < length; i += 1)
        ok = (value[i] >= '0' && value[i] <= '9') ||
             (value[i] >= 'a' && value[i] <= 'f');
    if (ok) ok = value[length] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_empty(char const value[65])
{
    bool ok = value != NULL;
    for (unsigned i = 0; ok && i < 65; i += 1) ok = value[i] == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_oracle_authority_u32(Sha256* hash, uint32_t number)
{
    unsigned char bytes[4];
    for (unsigned i = 0; i < 4; i += 1) bytes[i] = (unsigned char)(number >> (i * 8));
    sha256_add(hash, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL void bq_oracle_authority_u64(Sha256* hash, uint64_t number)
{
    unsigned char bytes[8];
    for (unsigned i = 0; i < 8; i += 1) bytes[i] = (unsigned char)(number >> (i * 8));
    sha256_add(hash, bytes, sizeof(bytes));
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_name(char const* name)
{
    size_t length = name ? strnlen(name, BQ_RETIREMENT_OUTPUT_NAME_CAP) : 0;
    bool ok = length > 0 && length < BQ_RETIREMENT_OUTPUT_NAME_CAP &&
        !(length == 1 && name[0] == '.') &&
        !(length == 2 && name[0] == '.' && name[1] == '.');
    for (size_t i = 0; ok && i < length; i += 1)
        ok = name[i] >= 33 && name[i] <= 126 && name[i] != '/';
    return ok;
}

bool bq_retirement_oracle_population_hash(BqRetirementTrustedRow const* rows,
    uint32_t count, char digest[65])
{
    bool ok = rows && digest && count && count <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP;
    Sha256 hash;
    if (ok)
    {
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-oracle-population-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_oracle_authority_u32(&hash, count);
    }
    for (uint32_t i = 0; ok && i < count; i += 1)
    {
        BqRetirementTrustedRow const* row = rows + i;
        ok = row->row == i && bq_oracle_authority_hex(row->identity_sha256, 64) &&
            bq_oracle_authority_hex(row->source_sha256, 64) &&
            bq_oracle_authority_hex(row->configuration_sha256, 64) &&
            (bq_oracle_authority_hex(row->skip_proof_sha256, 64) ||
                bq_oracle_authority_empty(row->skip_proof_sha256)) &&
            row->target >= 1 && row->target <= 12 &&
            row->stage >= BQ_RETIREMENT_STAGE_OBJECT &&
            row->stage <= BQ_RETIREMENT_STAGE_SELF_HOST &&
            row->classification >= 1 && row->classification <= 5 &&
            row->compiler_eligible <= 1 && row->code_obligation <= 1 &&
            row->execution_obligation <= 1 &&
            (row->compiler_eligible ?
                bq_oracle_authority_empty(row->skip_proof_sha256) :
                bq_oracle_authority_hex(row->skip_proof_sha256, 64));
        if (ok)
        {
            bq_oracle_authority_u32(&hash, row->row);
            bq_oracle_authority_u32(&hash, row->census_row);
            bq_oracle_authority_u32(&hash, row->target);
            bq_oracle_authority_u32(&hash, row->stage);
            bq_oracle_authority_u32(&hash, row->classification);
            bq_oracle_authority_u32(&hash, row->compiler_eligible);
            bq_oracle_authority_u32(&hash, row->code_obligation);
            bq_oracle_authority_u32(&hash, row->execution_obligation);
            sha256_add(&hash, row->identity_sha256, 64);
            sha256_add(&hash, row->source_sha256, 64);
            sha256_add(&hash, row->configuration_sha256, 64);
            sha256_add(&hash, row->skip_proof_sha256, 65);
        }
    }
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

bool bq_retirement_oracle_template_hash(BqRetirementOracleTemplate const* source,
    char digest[65])
{
    bool ok = source && digest && source->references && source->reference_count &&
        source->population_rows && source->population_rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
        source->reference_count <= source->population_rows &&
        source->object_rows && source->object_rows <= source->population_rows &&
        source->native_target >= 1 && source->native_target <= 12 &&
        bq_oracle_authority_hex(source->support_sha256, 64) &&
        bq_oracle_authority_hex(source->census_sha256, 64) &&
        bq_oracle_authority_hex(source->population_sha256, 64) &&
        bq_oracle_authority_hex(source->toolchain_identity_sha256, 64);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = bq_oracle_authority_hex(source->source_commit[side], 40) &&
             bq_oracle_authority_hex(source->source_tree[side], 40) &&
             bq_oracle_authority_hex(source->source_sha256[side], 64);
    if (ok) ok = strcmp(source->source_commit[0], source->source_commit[1]) &&
        strcmp(source->source_tree[0], source->source_tree[1]);
    Sha256 hash;
    if (ok)
    {
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-oracle-template-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        for (unsigned side = 0; side < 2; side += 1)
        {
            sha256_add(&hash, source->source_commit[side], 40);
            sha256_add(&hash, source->source_tree[side], 40);
            sha256_add(&hash, source->source_sha256[side], 64);
        }
        sha256_add(&hash, source->support_sha256, 64);
        sha256_add(&hash, source->census_sha256, 64);
        sha256_add(&hash, source->population_sha256, 64);
        sha256_add(&hash, source->toolchain_identity_sha256, 64);
        bq_oracle_authority_u32(&hash, source->population_rows);
        bq_oracle_authority_u32(&hash, source->object_rows);
        bq_oracle_authority_u32(&hash, source->native_target);
        bq_oracle_authority_u32(&hash, source->reference_count);
    }
    for (uint32_t i = 0; ok && i < source->reference_count; i += 1)
    {
        BqRetirementOracleTemplateRow const* row = source->references + i;
        ok = row->row < source->population_rows && (!i || row->row > source->references[i - 1].row) &&
            row->target == source->native_target &&
            bq_oracle_authority_hex(row->source_sha256, 64) &&
            bq_oracle_authority_hex(row->configuration_sha256, 64) &&
            bq_oracle_authority_hex(row->build_command_sha256, 64) &&
            bq_oracle_authority_hex(row->logical_command_sha256, 64) &&
            bq_oracle_authority_name(row->output_name);
        if (ok)
        {
            bq_oracle_authority_u32(&hash, row->row);
            bq_oracle_authority_u32(&hash, row->census_row);
            bq_oracle_authority_u32(&hash, row->target);
            sha256_add(&hash, row->source_sha256, 64);
            sha256_add(&hash, row->configuration_sha256, 64);
            sha256_add(&hash, row->build_command_sha256, 64);
            sha256_add(&hash, row->logical_command_sha256, 64);
            bq_oracle_authority_u32(&hash, (uint32_t)strlen(row->output_name));
            sha256_add(&hash, row->output_name, strlen(row->output_name));
        }
    }
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

bool bq_retirement_oracle_logical_command_hash(
    BqRetirementProcessCommand const* command, char digest[65])
{
    char* logical[TP_RETIREMENT_COMMAND_ARGUMENTS + 1] = {0};
    static char const executable[] = "/bq-retirement-reference-executable-v1";
    static char const directory[] = "/bq-retirement-reference-workdir-v1";
    bool ok = command && command->arguments && command->argument_count &&
        command->argument_count <= TP_RETIREMENT_COMMAND_ARGUMENTS;
    if (ok)
    {
        logical[0] = (char*)executable;
        for (unsigned i = 1; i < command->argument_count; i += 1)
            logical[i] = command->arguments[i];
        ok = tp_retirement_command_fields_hash(logical, command->argument_count,
            directory, command->environment, command->environment_count, digest);
    }
    if (!ok && digest) digest[0] = 0;
    return ok;
}

bool bq_retirement_oracle_authority_begin(BqRetirementOracleAuthority* authority,
    BqRetirementOracleTemplate const* template, char const installed_template_sha256[65],
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference* reference_workspace, uint32_t reference_slots,
    uint64_t job_id, uint64_t attempt_token)
{
    bool fresh = authority && !authority->ledger.count && !authority->ledger.failed &&
        !authority->ledger.finished && !authority->template;
    char template_sha256[65] = {0}, population_sha256[65] = {0};
    bool ok = fresh && template && prepared && rows && reference_workspace &&
        installed_template_sha256 && job_id && attempt_token &&
        reference_slots >= template->reference_count &&
        bq_retirement_oracle_template_hash(template, template_sha256) &&
        !strcmp(template_sha256, installed_template_sha256) &&
        prepared->rows == template->population_rows &&
        prepared->object_rows == template->object_rows &&
        prepared->native_target == template->native_target &&
        bq_oracle_authority_hex(prepared->preparation_sha256, 64) &&
        bq_oracle_authority_hex(prepared->binary_sha256[0], 64) &&
        bq_oracle_authority_hex(prepared->binary_sha256[1], 64) &&
        !strcmp(prepared->support_sha256, template->support_sha256) &&
        !strcmp(prepared->census_sha256, template->census_sha256) &&
        bq_retirement_oracle_population_hash(rows, prepared->rows, population_sha256) &&
        !strcmp(population_sha256, template->population_sha256);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = !strcmp(prepared->source_sha256[side], template->source_sha256[side]);
    uint32_t matched = 0;
    for (uint32_t i = 0; ok && i < prepared->rows; i += 1)
    {
        BqRetirementTrustedRow const* row = rows + i;
        bool native_runtime = row->compiler_eligible && row->execution_obligation &&
            row->stage != BQ_RETIREMENT_STAGE_OBJECT && row->target == prepared->native_target;
        ok = row->census_row < prepared->object_rows;
        if (native_runtime)
        {
            BqRetirementOracleTemplateRow const* approved =
                matched < template->reference_count ? template->references + matched : NULL;
            ok = approved && approved->row == i && approved->census_row == row->census_row &&
                approved->target == row->target &&
                !strcmp(approved->source_sha256, row->source_sha256) &&
                !strcmp(approved->configuration_sha256, row->configuration_sha256);
            if (ok)
            {
                BqRetirementOracleReference* reference = reference_workspace + matched;
                *reference = (BqRetirementOracleReference){0};
                reference->row = approved->row;
                reference->census_row = approved->census_row;
                reference->target = approved->target;
                memcpy(reference->preparation_sha256, prepared->preparation_sha256, 65);
                memcpy(reference->source_sha256, approved->source_sha256, 65);
                memcpy(reference->configuration_sha256, approved->configuration_sha256, 65);
                memcpy(reference->build_command_sha256, approved->build_command_sha256, 65);
                memcpy(reference->logical_command_sha256, approved->logical_command_sha256, 65);
                memcpy(reference->output_name, approved->output_name,
                    strlen(approved->output_name) + 1);
                matched += 1;
            }
        }
    }
    if (ok) ok = matched == template->reference_count &&
        bq_retirement_oracle_begin_observing(&authority->ledger, prepared, rows,
            reference_workspace, matched, template_sha256, job_id, attempt_token);
    if (fresh)
    {
        authority->template = ok ? template : NULL;
        authority->references = ok ? reference_workspace : NULL;
        authority->job_id = ok ? job_id : 0;
        authority->attempt_token = ok ? attempt_token : 0;
        if (ok) memcpy(authority->template_sha256, template_sha256, 65);
        if (ok) memcpy(authority->toolchain_identity_sha256,
            template->toolchain_identity_sha256, 65);
        else authority->ledger.failed = 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_workdir(int descriptor,
    BqRetirementProcessCommand const* command)
{
    char expected[64] = {0};
    struct stat directory = {0};
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    bool ok = command && command->directory && flags >= 0 && (flags & FD_CLOEXEC) &&
        fstat(descriptor, &directory) == 0 && S_ISDIR(directory.st_mode) &&
        directory.st_uid == geteuid() && !(directory.st_mode & 0022) &&
        snprintf(expected, sizeof(expected), "/proc/self/fd/%d", descriptor) > 0 &&
        !strcmp(command->directory, expected);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_top_static(
    BqRetirementOracleAuthority const* authority)
{
    BqRetirementOracleLedger const* ledger = authority ? &authority->ledger : NULL;
    BqRetirementOracleTemplate const* template = authority ? authority->template : NULL;
    bool ok = ledger && template && template->references && authority->references &&
        ledger->references == authority->references &&
        ledger->count == template->reference_count &&
        ledger->job_id == authority->job_id &&
        ledger->attempt_token == authority->attempt_token &&
        !memcmp(ledger->template_sha256, authority->template_sha256, 65) &&
        !memcmp(authority->toolchain_identity_sha256,
            template->toolchain_identity_sha256, 65) &&
        ledger->prepared.rows == template->population_rows &&
        ledger->prepared.object_rows == template->object_rows &&
        ledger->prepared.native_target == template->native_target &&
        !memcmp(ledger->prepared.support_sha256, template->support_sha256, 65) &&
        !memcmp(ledger->prepared.census_sha256, template->census_sha256, 65);
    for (unsigned side = 0; ok && side < 2; side += 1)
        ok = !memcmp(ledger->prepared.source_sha256[side],
            template->source_sha256[side], 65);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_reference_static(
    BqRetirementOracleAuthority const* authority, uint32_t index)
{
    BqRetirementOracleLedger const* ledger = authority ? &authority->ledger : NULL;
    BqRetirementOracleTemplate const* template = authority ? authority->template : NULL;
    BqRetirementOracleReference const* reference = authority && authority->references &&
        ledger && index < ledger->count ? authority->references + index : NULL;
    BqRetirementOracleTemplateRow const* approved = template && template->references &&
        index < template->reference_count ? template->references + index : NULL;
    bool ok = reference && approved && bq_oracle_authority_top_static(authority) &&
        reference->row == approved->row &&
        reference->census_row == approved->census_row &&
        reference->target == approved->target &&
        !memcmp(reference->preparation_sha256, ledger->prepared.preparation_sha256, 65) &&
        !memcmp(reference->source_sha256, approved->source_sha256, 65) &&
        !memcmp(reference->configuration_sha256, approved->configuration_sha256, 65) &&
        !memcmp(reference->build_command_sha256, approved->build_command_sha256, 65) &&
        !memcmp(reference->logical_command_sha256, approved->logical_command_sha256, 65) &&
        bq_oracle_authority_name(reference->output_name) &&
        bq_oracle_authority_name(approved->output_name) &&
        !strcmp(reference->output_name, approved->output_name);
    return ok;
}

bool bq_retirement_oracle_authority_next(BqRetirementOracleAuthority* authority,
    BqRetirementOracleVerifiedBuild const* build,
    BqRetirementProcessCommand const* command, BqRetirementArtifactLocation output,
    int cancellation_fd, uint64_t absolute_deadline_ns)
{
    BqRetirementOracleLedger* ledger = authority ? &authority->ledger : NULL;
    uint32_t index = ledger ? ledger->done : 0;
    BqRetirementOracleReference* reference = authority && authority->references &&
        index < ledger->count ?
        authority->references + index : NULL;
    char source_sha256[65] = {0}, binary_sha256[65] = {0};
    char receipt_sha256[65] = {0}, logical_sha256[65] = {0};
    char concrete_sha256[65] = {0}, executable[64] = {0};
    bool ok = ledger && ledger->authority_bound && !ledger->failed &&
        !ledger->finished && !authority->finished && reference && build &&
        bq_oracle_authority_reference_static(authority, index) &&
        command && command->arguments && command->argument_count &&
        output.name && !strcmp(output.name, reference->output_name) &&
        build->job_id == authority->job_id &&
        build->attempt_token == authority->attempt_token &&
        build->row == reference->row &&
        build->census_row == reference->census_row &&
        build->target == reference->target &&
        !strcmp(build->preparation_sha256, ledger->prepared.preparation_sha256) &&
        !strcmp(build->source_sha256, reference->source_sha256) &&
        !strcmp(build->configuration_sha256, reference->configuration_sha256) &&
        !strcmp(build->toolchain_identity_sha256,
            authority->toolchain_identity_sha256) &&
        !strcmp(build->build_command_sha256, reference->build_command_sha256) &&
        bq_retirement_oracle_file_hash(build->source, BQ_ORACLE_SOURCE_CAP,
            false, source_sha256) && !strcmp(source_sha256, build->source_sha256) &&
        bq_retirement_oracle_file_hash(build->binary, BQ_ORACLE_BINARY_CAP,
            true, binary_sha256) && !strcmp(binary_sha256, build->binary_sha256) &&
        bq_retirement_oracle_file_hash(build->receipt, BQ_ORACLE_RECEIPT_CAP,
            false, receipt_sha256) && !strcmp(receipt_sha256, build->receipt_sha256) &&
        bq_oracle_authority_workdir(output.directory, command) &&
        snprintf(executable, sizeof(executable), "/proc/self/fd/%d", build->binary) > 0 &&
        command->arguments[0] && !strcmp(command->arguments[0], executable) &&
        bq_retirement_oracle_logical_command_hash(command, logical_sha256) &&
        !strcmp(logical_sha256, reference->logical_command_sha256) &&
        tp_retirement_command_fields_hash(command->arguments, command->argument_count,
            command->directory, command->environment, command->environment_count,
            concrete_sha256);
    if (ok)
    {
        memcpy(reference->build_receipt_sha256, receipt_sha256, 65);
        memcpy(reference->binary_sha256, binary_sha256, 65);
        memcpy(reference->command_sha256, concrete_sha256, 65);
        ok = bq_retirement_oracle_produce_next(ledger, build->binary, build->receipt,
            command, output, cancellation_fd, absolute_deadline_ns);
    }
    if (ledger && !ok) ledger->failed = 1;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_oracle_authority_attempt_hash(
    BqRetirementOracleAuthority const* authority, char digest[65])
{
    BqRetirementOracleLedger const* ledger = authority ? &authority->ledger : NULL;
    bool ok = ledger && digest && ledger->authority_bound && ledger->finished &&
        !ledger->failed && bq_retirement_oracle_ready(ledger) &&
        bq_oracle_authority_hex(authority->template_sha256, 64) &&
        bq_oracle_authority_hex(ledger->pinned_sha256, 64) &&
        bq_oracle_authority_hex(ledger->sealed_sha256, 64) &&
        bq_oracle_authority_hex(ledger->prepared.preparation_sha256, 64) &&
        authority->job_id && authority->attempt_token &&
        ledger->job_id == authority->job_id &&
        ledger->attempt_token == authority->attempt_token &&
        !strcmp(ledger->template_sha256, authority->template_sha256);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-oracle-attempt-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        sha256_add(&hash, authority->template_sha256, 64);
        bq_oracle_authority_u64(&hash, authority->job_id);
        bq_oracle_authority_u64(&hash, authority->attempt_token);
        sha256_add(&hash, ledger->prepared.preparation_sha256, 64);
        sha256_add(&hash, ledger->pinned_sha256, 64);
        sha256_add(&hash, ledger->sealed_sha256, 64);
        sha256_finish_hex(&hash, digest);
    }
    else if (digest) digest[0] = 0;
    return ok;
}

bool bq_retirement_oracle_authority_finish(BqRetirementOracleAuthority* authority)
{
    bool ok = authority && authority->template && !authority->finished &&
        bq_retirement_oracle_finish(&authority->ledger) &&
        bq_oracle_authority_attempt_hash(authority, authority->attempt_sha256);
    if (authority)
    {
        if (ok) authority->finished = 1;
        else authority->ledger.failed = 1;
    }
    return ok;
}

bool bq_retirement_oracle_authority_ready(BqRetirementOracleAuthority const* authority)
{
    char template_sha256[65] = {0}, population_sha256[65] = {0};
    char attempt_sha256[65] = {0};
    bool ok = authority && authority->finished && authority->template &&
        bq_retirement_oracle_template_hash(authority->template, template_sha256) &&
        !strcmp(template_sha256, authority->template_sha256) &&
        bq_retirement_oracle_population_hash(authority->ledger.rows,
            authority->ledger.prepared.rows, population_sha256) &&
        !strcmp(population_sha256, authority->template->population_sha256) &&
        bq_oracle_authority_attempt_hash(authority, attempt_sha256) &&
        !strcmp(attempt_sha256, authority->attempt_sha256);
    for (uint32_t i = 0; ok && i < authority->ledger.count; i += 1)
        ok = bq_oracle_authority_reference_static(authority, i);
    return ok;
}

#ifdef BQ_RETIREMENT_ORACLE_AUTHORITY_TEST_ONLY
/* Miniature fixture issuer. Deliberately absent from production builds. */
static bool bq_oracle_authority_fixture_build(BqRetirementOracleVerifiedBuild* build,
    uint64_t job_id, uint64_t attempt_token, char const preparation_sha256[65],
    uint32_t row, uint32_t census_row, uint32_t target,
    char const configuration_sha256[65],
    int source, int binary, int receipt, char const toolchain_identity_sha256[65],
    char const build_command_sha256[65])
{
    bool ok = build && preparation_sha256 && configuration_sha256 &&
        toolchain_identity_sha256 && build_command_sha256 &&
        job_id && attempt_token;
    if (ok)
    {
        *build = (BqRetirementOracleVerifiedBuild){0};
        build->job_id = job_id;
        build->attempt_token = attempt_token;
        build->row = row;
        build->census_row = census_row;
        build->target = target;
        build->source = source;
        build->binary = binary;
        build->receipt = receipt;
        memcpy(build->preparation_sha256, preparation_sha256, 65);
        memcpy(build->configuration_sha256, configuration_sha256, 65);
        memcpy(build->toolchain_identity_sha256, toolchain_identity_sha256, 65);
        memcpy(build->build_command_sha256, build_command_sha256, 65);
        ok = bq_retirement_oracle_file_hash(source, BQ_ORACLE_SOURCE_CAP,
                false, build->source_sha256) &&
            bq_retirement_oracle_file_hash(binary, BQ_ORACLE_BINARY_CAP,
                true, build->binary_sha256) &&
            bq_retirement_oracle_file_hash(receipt, BQ_ORACLE_RECEIPT_CAP,
                false, build->receipt_sha256);
    }
    return ok;
}
#endif
