/* Service-side A -> B entry for #881.
 * begin_service_pinned is the deterministic test seam for the compiled
 * profile; production uses bq_recipe_profile. Acquire replays the durable A
 * and binary records and holds both exact executable inodes. The caller must
 * separately authenticate Clang provenance and the complete #508/#509 facts.
 */
#include "retirement_correctness_service.h"
#include <stdlib.h>
#include <string.h>

#define BQ_RETIREMENT_SUPPORT_BYTES_CAP (128u * 1024u)
#define BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT (12u * 2u * 2u * 4u)

/* #508 nrc_targets iteration order mapped to the TARGETS order used by the
 * correctness gate, code reader and performance schema. The row ordinal is
 * in census order; its target field is in performance order. */
BUSTER_GLOBAL_LOCAL u8 const bq_retirement_census_target_ids[12] = {
    11, 5, 10, 4, 8, 2, 9, 3, 7, 1, 12, 6
};

/* The profile pins the complete #508 support declaration, including every
 * subject and control. Copy it once through a held read-only descriptor before
 * counting; neither a request nor a B declaration can choose the population.
 * The matrix dimensions are the current #508 target/frontend/PIC/allocator
 * axes. Object rows are also joined to the reviewed source digest and target
 * for their #508 census ordinal. The independent rows/validator replay remains
 * the importer's job. The null-row form is only a cardinality test seam. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_support_projection(int file, String8 profile,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows)
{
    char pinned[SHA256_HEX_CAPACITY] = {0}, actual[SHA256_HEX_CAPACITY] = {0};
    struct stat before = {0}, after = {0};
    int descriptor = file >= 3 ? fcntl(file, F_GETFD) : -1;
    int flags = descriptor >= 0 ? fcntl(file, F_GETFL) : -1;
    bool ok = prepared && bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), pinned) &&
              !memcmp(prepared->support_sha256, pinned, SHA256_HEX_CAPACITY) &&
              descriptor >= 0 && (descriptor & FD_CLOEXEC) &&
              flags >= 0 && (flags & O_ACCMODE) == O_RDONLY &&
              fstat(file, &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1 &&
              (before.st_uid == 0 || before.st_uid == geteuid()) &&
              !(before.st_mode & 0222) && before.st_size > 0 &&
              (u64)before.st_size <= BQ_RETIREMENT_SUPPORT_BYTES_CAP;
    u8* bytes = ok ? malloc((size_t)before.st_size) : NULL;
    ok = ok && bytes != NULL;
    u64 offset = 0;
    while (ok && offset < (u64)before.st_size)
    {
        size_t wanted = (size_t)((u64)before.st_size - offset);
        ssize_t count = pread(file, bytes + offset, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) offset += (u64)count;
    }
    if (ok)
    {
        bq_digest(bytes, (u32)offset, (char8*)actual);
        ok = !memcmp(actual, pinned, SHA256_HEX_CAPACITY) &&
             fstat(file, &after) == 0 && bq_retirement_binary_stable(&before, &after);
    }
    u64 length = offset;
    offset = 0;
    char (*subject_sha256)[SHA256_HEX_CAPACITY] = rows ?
        calloc(BQ_RETIREMENT_INVENTORY_CAP, SHA256_HEX_CAPACITY) : NULL;
    if (rows) ok = ok && subject_sha256 != NULL;
    u32 subjects = 0, inputs = 0;
    String8 remaining = {(char8*)bytes, length}, line = {0}, previous = {0};
    ok = ok && bq_next_line(remaining, &offset, &line) &&
         string_equal(line, S8("path\trole\tcompile_obligation\tbytes\tsha256"));
    while (ok && offset < remaining.length)
    {
        ok = bq_next_line(remaining, &offset, &line);
        String8 fields[5] = {0};
        u32 field = 0;
        u64 start = 0;
        for (u64 i = 0; ok && i <= line.length; i += 1)
            if (i == line.length || line.pointer[i] == '\t')
            {
                ok = field < BUSTER_ARRAY_LENGTH(fields) && i > start;
                if (ok) fields[field++] = (String8){line.pointer + start, i - start};
                start = i + 1;
            }
        u64 size = 0;
        bool subject = string_equal(fields[1], S8("subject"));
        u64 common = previous.length < fields[0].length ? previous.length : fields[0].length;
        int order = previous.length && fields[0].length ?
                    memcmp(previous.pointer, fields[0].pointer, (size_t)common) : -1;
        ok = ok && field == BUSTER_ARRAY_LENGTH(fields) &&
             fields[0].length > 6 && fields[0].length <= BQ_PATH_CAP &&
             !memcmp(fields[0].pointer, "tests/", 6) &&
             (!previous.length || order < 0 || (!order && previous.length < fields[0].length)) &&
             bq_retirement_number(fields[3], &size) && size > 0 &&
             bq_retirement_hex(fields[4], 64) &&
             (subject ? (string_equal(fields[2], S8("supported-object-zero-fallback")) ||
                         string_equal(fields[2], S8("registered-non-object-control"))) :
              ((string_equal(fields[1], S8("support-file")) &&
                string_equal(fields[2], S8("dependency-only"))) ||
               (string_equal(fields[1], S8("dormant-custom-language")) &&
                string_equal(fields[2], S8("preserved-not-active"))) ||
               (string_equal(fields[1], S8("negative-diagnostic-fixture")) &&
                string_equal(fields[2], S8("registered-rejection-control")))));
        if (ok) ok = inputs < BQ_RETIREMENT_INVENTORY_CAP &&
                     (!subject || subjects < BQ_RETIREMENT_INVENTORY_CAP);
        if (ok)
        {
            previous = fields[0];
            if (subject && subject_sha256)
            {
                memcpy(subject_sha256[subjects], fields[4].pointer, 64);
                subject_sha256[subjects][64] = 0;
            }
            subjects += subject;
            inputs += 1;
        }
    }
    if (ok) ok = subjects && subjects <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP /
                                       BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT &&
                  prepared->object_rows == subjects * BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT &&
                  prepared->rows >= prepared->object_rows + 2u &&
                  prepared->rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP;
    u8* seen = ok && rows ? calloc(prepared->object_rows, 1) : NULL;
    if (ok && rows) ok = seen != NULL;
    u32 object_count = 0, stage_kinds = 0;
    for (u32 i = 0; ok && rows && i < prepared->rows; i += 1)
    {
        BqRetirementTrustedRow const* row = rows + i;
        ok = row->row == i && row->census_row < prepared->object_rows &&
             row->stage >= BQ_RETIREMENT_STAGE_OBJECT && row->stage <= BQ_RETIREMENT_STAGE_SELF_HOST;
        if (ok && row->stage == BQ_RETIREMENT_STAGE_OBJECT)
        {
            u32 ordinal = row->census_row;
            u32 subject = ordinal / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT;
            u32 target_index = (ordinal % BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT) / (2u * 2u * 4u);
            u32 target = bq_retirement_census_target_ids[target_index];
            ok = !seen[ordinal] && row->target == target &&
                 !memcmp(row->source_sha256, subject_sha256[subject], SHA256_HEX_CAPACITY);
            if (ok)
            {
                seen[ordinal] = 1;
                object_count += 1;
            }
        }
        if (ok) stage_kinds |= 1u << row->stage;
    }
    if (ok && rows)
        ok = object_count == prepared->object_rows &&
             stage_kinds == ((1u << BQ_RETIREMENT_STAGE_OBJECT) |
                             (1u << BQ_RETIREMENT_STAGE_LINK) |
                             (1u << BQ_RETIREMENT_STAGE_SELF_HOST));
    free(seen);
    free(subject_sha256);
    free(bytes);
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_correctness_begin_service_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished;
    BqError result = fresh && held && !held->owned && prepared ? BQ_OK : BQ_RECIPE_MISMATCH;
    bool acquired = false;
    if (result == BQ_OK)
    {
        result = bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, profile,
                                                        preparation_sha256, record_sha256, held);
        acquired = result == BQ_OK;
    }
    if (result == BQ_OK)
    {
        BqRetirementBinaries const* verified = &held->verified;
        char support[SHA256_HEX_CAPACITY] = {0};
        bool same = bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support) &&
                    !memcmp(prepared->support_sha256, support, SHA256_HEX_CAPACITY) &&
                    !memcmp(prepared->preparation_sha256, verified->preparation_sha256, SHA256_HEX_CAPACITY);
        for (u32 side = 0; same && side < 2; side += 1)
            same = !memcmp(prepared->source_sha256[side], verified->source_sha256[side], SHA256_HEX_CAPACITY) &&
                   !memcmp(prepared->binary_sha256[side], verified->binary_sha256[side], SHA256_HEX_CAPACITY);
        result = same ? BQ_OK : BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK && !bq_retirement_correctness_begin(gate, prepared, rows, checks, check_count,
                                                              check_facts, facts, identity_workspace, identity_slots,
                                                              census_workspace, census_slots))
        result = BQ_RECIPE_MISMATCH;
    if (result != BQ_OK)
    {
        if (acquired) bq_retirement_binaries_release(held);
        if (fresh) gate->failed = 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_correctness_begin_service_built_pinned(
    BqQueue* queue, BqJob const* job, int installed, int workspaces, String8 workspace_root,
    String8 profile, char const* fixed_driver, char const* fixed_toolchain,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished;
    BqRetirementMatchedBuild build = {0};
    BqError result = fresh && held && !held->owned ?
        bq_retirement_matched_build_import_pinned(queue, job, installed, workspaces, workspace_root,
            profile, fixed_driver, fixed_toolchain, preparation_sha256, record_sha256, build_record_sha256,
            &build) : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces, profile,
            preparation_sha256, record_sha256, prepared, rows, checks, check_count,
            check_facts, facts, identity_workspace, identity_slots, census_workspace, census_slots,
            held, gate);
    else if (fresh) gate->failed = 1;
    return result;
}

BqError bq_retirement_correctness_begin_service(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, int support_declaration, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished;
    BqError result = !fresh ? BQ_RECIPE_MISMATCH :
        rows && bq_retirement_support_projection(support_declaration, profile, prepared, rows) ?
        bq_retirement_correctness_begin_service_built_pinned(queue, job,
            installed, workspaces, workspace_root, profile, BQ_RETIREMENT_BUILD_DRIVER,
            BQ_RETIREMENT_TOOLCHAIN_ROOT, preparation_sha256, record_sha256, build_record_sha256,
            prepared, rows, checks, check_count, check_facts, facts,
            identity_workspace, identity_slots, census_workspace, census_slots, held, gate) :
        BQ_SOURCE_MISMATCH;
    if (result != BQ_OK && fresh) gate->failed = 1;
    return result;
}
