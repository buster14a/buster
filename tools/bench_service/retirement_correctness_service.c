/* Service-side A -> B entry for #881.
 * The _pinned and _built_pinned functions remain lower-level fixture seams for
 * matched-build import and held binaries. Production begin_service reimports
 * the durable A preparation, matched-build and binary records, then validates
 * the raw #508 identity and staged source-ledger eligibility projection. It
 * fails closed before acquiring binaries or beginning correctness because
 * full validator, configuration, command, #509, and oracle authority is not
 * yet imported.
 */
#include "retirement_correctness_service.h"
#include <stdlib.h>
#include <string.h>

#define BQ_RETIREMENT_SUPPORT_BYTES_CAP (128u * 1024u)
#define BQ_RETIREMENT_CENSUS_INPUTS_BYTES_CAP (1024u * 1024u)
#define BQ_RETIREMENT_CENSUS_ROWS_BYTES_CAP (64u * 1024u * 1024u)
#define BQ_RETIREMENT_MANIFEST_BYTES_CAP (1024u * 1024u)
#define BQ_RETIREMENT_VALIDATOR_REPORT_BYTES_CAP (4u * 1024u * 1024u)
#define BQ_RETIREMENT_VALIDATOR_APPLICABILITY_BYTES_CAP (32u * 1024u * 1024u)
#define BQ_RETIREMENT_VALIDATOR_SKIPS_BYTES_CAP (8u * 1024u * 1024u)
#define BQ_RETIREMENT_VALIDATOR_LEDGER_BYTES_CAP (1024u * 1024u)
#define BQ_RETIREMENT_VALIDATOR_LEDGER_RECORD_CAP 4096u
#define BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT 4u
#define BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT (12u * 2u * 2u * 4u)
#define BQ_RETIREMENT_FULL_APPLICABILITY_LEDGER_COUNT 374u
#define BQ_RETIREMENT_FULL_SUPPORTED_GAP_COUNT 192u

BUSTER_GLOBAL_LOCAL char const bq_retirement_full_applicability_ledger_sha256[] =
    "934be981e866fe3dbbdb4a5b9e551c052b4546487bb04245fac24bb271be78fa";
BUSTER_GLOBAL_LOCAL char const bq_retirement_full_supported_gap_sha256[] =
    "0f531b1cf7c7922ea891e15703971bcb2ddf95f398f628e0b2681831d7cbf81e";
BUSTER_GLOBAL_LOCAL char const bq_retirement_full_supported_gap_ledger_sha256[] =
    "e67ef103035b1b99e97ae640de2ef0b7a84add2705758cb2431a4855b303dfc3";
BUSTER_GLOBAL_LOCAL char const bq_retirement_empty_residual_sha256[] =
    "a4b667fab9df2e5e5a1e24f3395904d3ec77fd306a7e4c8e85153dbff6a30818";

typedef struct BqRetirementValidatorEligibility BqRetirementValidatorEligibility;
struct BqRetirementValidatorEligibility
{
    u32 row_count;
    char profile[64];
    u8* compiler_eligible;
    u8* classification;
    char (*skip_proof_sha256)[SHA256_HEX_CAPACITY];
    char compiler_sha256[SHA256_HEX_CAPACITY];
    char baseline_sha256[SHA256_HEX_CAPACITY];
    char evidence_sha256[SHA256_HEX_CAPACITY];
};

typedef struct BqRetirementSupportSubject BqRetirementSupportSubject;
struct BqRetirementSupportSubject
{
    char path[BQ_PATH_CAP + 1];
    char compile_obligation[48];
    char sha256[SHA256_HEX_CAPACITY];
    char fixture_recipe[64];
};

typedef struct BqRetirementFixtureRecipe BqRetirementFixtureRecipe;
struct BqRetirementFixtureRecipe
{
    String8 name, flags;
};

/* #508 nrc_targets iteration order mapped to the TARGETS order used by the
 * correctness gate, code reader and performance schema. The row ordinal is
 * in census order; its target field is in performance order. */
BUSTER_GLOBAL_LOCAL u8 const bq_retirement_census_target_ids[12] = {
    11, 5, 10, 4, 8, 2, 9, 3, 7, 1, 12, 6
};

BUSTER_GLOBAL_LOCAL String8 const bq_retirement_census_targets[12] = {
    S8_INITIALIZER("x86_64-unknown-linux-gnu"), S8_INITIALIZER("aarch64-unknown-linux-gnu"),
    S8_INITIALIZER("x86_64-pc-windows-msvc"), S8_INITIALIZER("aarch64-pc-windows-msvc"),
    S8_INITIALIZER("x86_64-apple-macos"), S8_INITIALIZER("aarch64-apple-macos"),
    S8_INITIALIZER("x86_64-linux-android"), S8_INITIALIZER("aarch64-linux-android"),
    S8_INITIALIZER("x86_64-apple-ios"), S8_INITIALIZER("aarch64-apple-ios"),
    S8_INITIALIZER("x86_64-unknown-uefi"), S8_INITIALIZER("aarch64-unknown-uefi")
};

BUSTER_GLOBAL_LOCAL String8 const bq_retirement_census_target_abis[12] = {
    S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("aapcs64"),
    S8_INITIALIZER("win64-x86_64"), S8_INITIALIZER("windows-aarch64"),
    S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("darwin-aarch64"),
    S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("aapcs64"),
    S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("darwin-aarch64"),
    S8_INITIALIZER("win64-x86_64"), S8_INITIALIZER("aapcs64")
};

BUSTER_GLOBAL_LOCAL String8 const bq_retirement_census_allocators[BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT] = {
    S8_INITIALIZER("none"), S8_INITIALIZER("mir-stack"),
    S8_INITIALIZER("fast"), S8_INITIALIZER("quality")
};

BUSTER_GLOBAL_LOCAL BqRetirementFixtureRecipe bq_retirement_census_fixture_recipe(String8 path)
{
    BqRetirementFixtureRecipe result = {S8("compiler-default"), S8("")};
    if (string_equal(path, S8("tests/basic_c_constexpr.c")) ||
        string_equal(path, S8("tests/basic_c_constexpr_leaf.c")) ||
        string_equal(path, S8("tests/basic_c_nullptr.c")) ||
        string_equal(path, S8("tests/basic_c_typeof.c")))
        result = (BqRetirementFixtureRecipe){S8("c23"), S8("-std=c23")};
    if (string_equal(path, S8("tests/basic_c_dialect.c")))
        result = (BqRetirementFixtureRecipe){S8("c23-dialect-assertions"),
            S8("-std=c23 -DEXPECTED_STDC_VERSION=202311L -DEXPECTED_GNU=0")};
    if (string_equal(path, S8("tests/basic_c_predicate_bank.c")))
        result = (BqRetirementFixtureRecipe){S8("x86-avx512"), S8("")};
    if (string_equal(path, S8("tests/basic_c_atomic_aggregate.c")))
        result = (BqRetirementFixtureRecipe){S8("x86-cx16"), S8("")};
    return result;
}

/* The profile pins the complete #508 support declaration, including every
 * subject and control. Copy it once through a held read-only descriptor before
 * counting; neither a request nor a B declaration can choose the population.
 * The matrix dimensions are the current #508 target/frontend/PIC/allocator
 * axes. Object rows are also joined to the reviewed source digest and target
 * for their #508 census ordinal. The independent rows/validator replay remains
 * the importer's job. The null-row form is only a cardinality test seam. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_support_projection_subjects(int file, String8 profile,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows,
    BqRetirementSupportSubject* subject_output, u32 subject_capacity, u32* subject_count)
{
    char pinned[SHA256_HEX_CAPACITY] = {0}, actual[SHA256_HEX_CAPACITY] = {0};
    struct stat before = {0}, after = {0};
    int descriptor = file >= 3 ? fcntl(file, F_GETFD) : -1;
    int flags = descriptor >= 0 ? fcntl(file, F_GETFL) : -1;
    u32 subject_limit = prepared ? prepared->object_rows / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT : 0;
    if (subject_limit > BQ_RETIREMENT_INVENTORY_CAP) subject_limit = BQ_RETIREMENT_INVENTORY_CAP;
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
    BqRetirementSupportSubject* subjects_found = rows || subject_output ?
        calloc(subject_limit, sizeof(*subjects_found)) : NULL;
    if (rows || subject_output) ok = ok && subject_limit > 0 && subjects_found != NULL;
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
        if (ok) ok = inputs < BQ_RETIREMENT_INVENTORY_CAP && (!subject || subjects < subject_limit);
        if (ok)
        {
            previous = fields[0];
            if (subject && subjects_found)
            {
                memcpy(subjects_found[subjects].path, fields[0].pointer, (size_t)fields[0].length);
                subjects_found[subjects].path[fields[0].length] = 0;
                memcpy(subjects_found[subjects].compile_obligation, fields[2].pointer,
                       (size_t)fields[2].length);
                subjects_found[subjects].compile_obligation[fields[2].length] = 0;
                memcpy(subjects_found[subjects].sha256, fields[4].pointer, 64);
                subjects_found[subjects].sha256[64] = 0;
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
                 !memcmp(row->source_sha256, subjects_found[subject].sha256, SHA256_HEX_CAPACITY);
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
    if (ok && subject_output)
        ok = subject_capacity >= subjects && subject_count != NULL;
    if (ok && subject_output)
    {
        memcpy(subject_output, subjects_found, (size_t)subjects * sizeof(*subject_output));
        *subject_count = subjects;
    }
    else if (subject_count) *subject_count = 0;
    free(seen);
    free(subjects_found);
    free(bytes);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_support_projection(int file, String8 profile,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows)
{
    bool ok = bq_retirement_support_projection_subjects(file, profile, prepared, rows,
                                                         NULL, 0, NULL);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_inputs_line_fields(String8 line, String8 fields[8])
{
    bool ok = line.length > 0;
    u32 field = 0;
    u64 start = 0;
    for (u64 i = 0; ok && i <= line.length; i += 1)
    {
        if (i == line.length || line.pointer[i] == '\t')
        {
            bool empty_flags = field == 7 && i == line.length;
            ok = field < 8 && (i > start || empty_flags);
            if (ok) fields[field++] = (String8){line.pointer + start, i - start};
            start = i + 1;
        }
    }
    ok = ok && field == 8;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_support_line_fields(String8 line, String8 fields[5])
{
    bool ok = line.length > 0;
    u32 field = 0;
    u64 start = 0;
    for (u64 i = 0; ok && i <= line.length; i += 1)
    {
        if (i == line.length || line.pointer[i] == '\t')
        {
            ok = field < 5 && i > start;
            if (ok) fields[field++] = (String8){line.pointer + start, i - start};
            start = i + 1;
        }
    }
    ok = ok && field == 5;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_inputs_projection(int support_file, int inputs_file, String8 profile,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows,
    BqRetirementSupportSubject* subject_output, u32 subject_capacity, u32* subject_count,
    char verified_sha256[SHA256_HEX_CAPACITY])
{
    char support_pin[SHA256_HEX_CAPACITY] = {0}, support_actual[SHA256_HEX_CAPACITY] = {0};
    char inputs_pin[SHA256_HEX_CAPACITY] = {0}, inputs_actual[SHA256_HEX_CAPACITY] = {0};
    struct stat support_before = {0}, support_after = {0}, inputs_before = {0}, inputs_after = {0};
    u8* support_bytes = NULL;
    u8* inputs_bytes = NULL;
    u32 subjects = 0, input_rows = 0, subject_index = 0;
    int support_descriptor = support_file >= 3 ? fcntl(support_file, F_GETFD) : -1;
    int support_flags = support_descriptor >= 0 ? fcntl(support_file, F_GETFL) : -1;
    int inputs_descriptor = inputs_file >= 3 ? fcntl(inputs_file, F_GETFD) : -1;
    int inputs_flags = inputs_descriptor >= 0 ? fcntl(inputs_file, F_GETFL) : -1;
    bool ok = prepared && rows && subject_output && subject_count && verified_sha256 &&
              bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support_pin) &&
              bq_retirement_profile_sha(profile, S8("census-inputs-sha256="), inputs_pin) &&
              support_descriptor >= 0 && (support_descriptor & FD_CLOEXEC) &&
              support_flags >= 0 && (support_flags & O_ACCMODE) == O_RDONLY &&
              fstat(support_file, &support_before) == 0 && S_ISREG(support_before.st_mode) &&
              support_before.st_nlink == 1 &&
              (support_before.st_uid == 0 || support_before.st_uid == geteuid()) &&
              !(support_before.st_mode & 0222) && support_before.st_size > 0 &&
              (u64)support_before.st_size <= BQ_RETIREMENT_SUPPORT_BYTES_CAP &&
              inputs_descriptor >= 0 && (inputs_descriptor & FD_CLOEXEC) &&
              inputs_flags >= 0 && (inputs_flags & O_ACCMODE) == O_RDONLY &&
              fstat(inputs_file, &inputs_before) == 0 && S_ISREG(inputs_before.st_mode) &&
              inputs_before.st_nlink == 1 &&
              (inputs_before.st_uid == 0 || inputs_before.st_uid == geteuid()) &&
              !(inputs_before.st_mode & 0222) && inputs_before.st_size > 0 &&
              (u64)inputs_before.st_size <= BQ_RETIREMENT_CENSUS_INPUTS_BYTES_CAP;
    if (ok) ok = bq_retirement_support_projection_subjects(support_file, profile, prepared, rows,
        subject_output, subject_capacity, &subjects);
    support_bytes = ok ? malloc((size_t)support_before.st_size) : NULL;
    inputs_bytes = ok ? malloc((size_t)inputs_before.st_size) : NULL;
    ok = ok && support_bytes != NULL && inputs_bytes != NULL;
    u64 support_read = 0, inputs_read = 0;
    while (ok && support_read < (u64)support_before.st_size)
    {
        size_t wanted = (size_t)((u64)support_before.st_size - support_read);
        ssize_t count = pread(support_file, support_bytes + support_read, wanted, (off_t)support_read);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) support_read += (u64)count;
    }
    while (ok && inputs_read < (u64)inputs_before.st_size)
    {
        size_t wanted = (size_t)((u64)inputs_before.st_size - inputs_read);
        ssize_t count = pread(inputs_file, inputs_bytes + inputs_read, wanted, (off_t)inputs_read);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) inputs_read += (u64)count;
    }
    if (ok)
    {
        bq_digest(support_bytes, (u32)support_read, (char8*)support_actual);
        bq_digest(inputs_bytes, (u32)inputs_read, (char8*)inputs_actual);
        ok = !memcmp(support_actual, support_pin, SHA256_HEX_CAPACITY) &&
             !memcmp(inputs_actual, inputs_pin, SHA256_HEX_CAPACITY);
    }
    String8 support_text = {(char8*)support_bytes, support_read};
    String8 inputs_text = {(char8*)inputs_bytes, inputs_read};
    u64 support_offset = 0, inputs_offset = 0;
    String8 support_line = {0}, inputs_line = {0};
    static String8 const support_header = S8_INITIALIZER("path\trole\tcompile_obligation\tbytes\tsha256");
    static String8 const inputs_header = S8_INITIALIZER("path\trole\tcompile_obligation\tbytes\tbuster_hash_64\tsha256\tfixture_recipe\tfixture_flags");
    ok = ok && bq_next_line(support_text, &support_offset, &support_line) &&
         bq_next_line(inputs_text, &inputs_offset, &inputs_line) &&
         string_equal(support_line, support_header) && string_equal(inputs_line, inputs_header);
    while (ok && (support_offset < support_text.length || inputs_offset < inputs_text.length))
    {
        ok = bq_next_line(support_text, &support_offset, &support_line) &&
             bq_next_line(inputs_text, &inputs_offset, &inputs_line);
        String8 support_fields[5] = {0}, fields[8] = {0};
        ok = ok && bq_retirement_support_line_fields(support_line, support_fields) &&
             bq_retirement_inputs_line_fields(inputs_line, fields);
        u64 input_bytes = 0, buster_hash = 0;
        ok = ok && string_equal(support_fields[0], fields[0]) &&
             string_equal(support_fields[1], fields[1]) &&
             string_equal(support_fields[2], fields[2]) &&
             string_equal(support_fields[3], fields[3]) &&
             string_equal(support_fields[4], fields[5]) &&
             bq_retirement_number(fields[3], &input_bytes) && input_bytes > 0 &&
             bq_retirement_number(fields[4], &buster_hash) && bq_retirement_hex(fields[5], 64);
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            for (u64 byte = 0; ok && byte < fields[field].length; byte += 1)
            {
                u8 value = (u8)fields[field].pointer[byte];
                ok = value >= 0x20 && value != 0x7f;
            }
        BqRetirementFixtureRecipe recipe = {0};
        if (ok) recipe = bq_retirement_census_fixture_recipe(fields[0]);
        ok = ok && string_equal(fields[6], recipe.name) && string_equal(fields[7], recipe.flags);
        if (ok && string_equal(support_fields[1], S8("subject")))
        {
            ok = subject_index < subjects && subject_output[subject_index].path[0] &&
                 string_equal(fields[0], string_from_pointer(subject_output[subject_index].path)) &&
                 string_equal(fields[2], string_from_pointer(subject_output[subject_index].compile_obligation)) &&
                 recipe.name.length < sizeof(subject_output[subject_index].fixture_recipe);
            if (ok)
            {
                memcpy(subject_output[subject_index].fixture_recipe, recipe.name.pointer,
                       (size_t)recipe.name.length);
                subject_output[subject_index].fixture_recipe[recipe.name.length] = 0;
                subject_index += 1;
            }
        }
        if (ok) input_rows += 1;
    }
    if (ok) ok = input_rows > 0 && subject_index == subjects &&
                 support_offset == support_text.length && inputs_offset == inputs_text.length &&
                 fstat(support_file, &support_after) == 0 &&
                 bq_retirement_binary_stable(&support_before, &support_after) &&
                 fstat(inputs_file, &inputs_after) == 0 &&
                 bq_retirement_binary_stable(&inputs_before, &inputs_after);
    if (ok)
    {
        *subject_count = subjects;
        memcpy(verified_sha256, inputs_actual, SHA256_HEX_CAPACITY);
    }
    else
    {
        if (subject_count) *subject_count = 0;
        if (verified_sha256) memset(verified_sha256, 0, SHA256_HEX_CAPACITY);
    }
    free(support_bytes);
    free(inputs_bytes);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_census_line_fields(String8 line, String8 fields[17])
{
    bool ok = line.length > 0;
    u32 field = 0;
    u64 start = 0;
    for (u64 i = 0; ok && i <= line.length; i += 1)
    {
        if (i == line.length || line.pointer[i] == '\t')
        {
            ok = field < 17 && i > start;
            if (ok) fields[field++] = (String8){line.pointer + start, i - start};
            start = i + 1;
        }
    }
    ok = ok && field == 17;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_census_json_value(Sha256* hash, String8 value)
{
    bool ok = hash != NULL && value.pointer != NULL;
    if (ok) sha256_add(hash, "\"", 1);
    for (u64 i = 0; ok && i < value.length; i += 1)
    {
        u8 byte = value.pointer[i];
        if (byte == '"' || byte == '\\') sha256_add(hash, "\\", 1);
        if (byte < 0x20) ok = false;
        else sha256_add(hash, &byte, 1);
    }
    if (ok) sha256_add(hash, "\"", 1);
    return ok;
}

/* Match #508's approved performance identity serialization: a sorted-key JSON
 * object, UTF-8 strings, compact separators, and the object stage name. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_census_identity_sha256(String8 fields[17], char digest[65])
{
    static String8 const keys[15] = {
        S8_INITIALIZER("PIC"), S8_INITIALIZER("allocator"), S8_INITIALIZER("argv_evidence"),
        S8_INITIALIZER("artifact_stage"), S8_INITIALIZER("compile_obligation"),
        S8_INITIALIZER("cpu"), S8_INITIALIZER("cpu_features"), S8_INITIALIZER("diagnostic_obligation"),
        S8_INITIALIZER("execution_obligation"), S8_INITIALIZER("fixture"),
        S8_INITIALIZER("fixture_recipe"), S8_INITIALIZER("frontend_lowering"),
        S8_INITIALIZER("link_obligation"), S8_INITIALIZER("target"), S8_INITIALIZER("target_abi")
    };
    String8 const values[15] = {
        fields[9], fields[7], fields[16], S8("object"), fields[12], fields[5], fields[6],
        fields[15], fields[14], fields[2], fields[11], fields[8], fields[13], fields[3], fields[4]
    };
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "{", 1);
    bool ok = digest != NULL;
    for (u32 i = 0; ok && i < BUSTER_ARRAY_LENGTH(keys); i += 1)
    {
        if (i) sha256_add(&hash, ",", 1);
        ok = bq_retirement_census_json_value(&hash, keys[i]);
        if (ok) sha256_add(&hash, ":", 1);
        if (ok) ok = bq_retirement_census_json_value(&hash, values[i]);
    }
    if (ok)
    {
        sha256_add(&hash, "}", 1);
        sha256_finish_hex(&hash, (char8*)digest);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_census_row_projection(String8 fields[17], u32 index,
    BqRetirementSupportSubject const* subjects, u32 subject_count)
{
    u64 row = 0, group = 0;
    bool ok = bq_retirement_number(fields[0], &row) && bq_retirement_number(fields[1], &group) &&
              row == index && group == index / BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT &&
              index / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT < subject_count;
    u32 subject = index / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT;
    u32 subject_row = index % BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT;
    u32 target_index = subject_row / 16u;
    u32 within_target = subject_row % 16u;
    u32 frontend = within_target / 8u;
    u32 pic = (within_target % 8u) / 4u;
    u32 allocator = index % BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT;
    String8 expected_frontend = frontend ? S8("direct-ssa") : S8("local-backed-canonical");
    String8 expected_pic = pic ? S8("1") : S8("0");
    String8 expected_compile = string_from_pointer(subjects[subject].compile_obligation);
    ok = ok && string_equal(fields[2], string_from_pointer(subjects[subject].path)) &&
         string_equal(fields[3], bq_retirement_census_targets[target_index]) &&
         string_equal(fields[4], bq_retirement_census_target_abis[target_index]) &&
         fields[5].length > 0 && fields[6].length > 0 &&
         string_equal(fields[7], bq_retirement_census_allocators[allocator]) &&
         string_equal(fields[8], expected_frontend) && string_equal(fields[9], expected_pic) &&
         (string_equal(fields[10], S8("0")) || string_equal(fields[10], S8("1"))) &&
         string_equal(fields[11], string_from_pointer(subjects[subject].fixture_recipe)) &&
         string_equal(fields[12], expected_compile) &&
         fields[13].length > 0 && fields[14].length > 0 && fields[15].length > 0 &&
         fields[16].length > 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_census_projection(int support_file, int inputs_file, int rows_file,
    String8 profile, BqRetirementPrepared const* prepared, BqRetirementTrustedRow const* rows,
    char verified_inputs_sha256[SHA256_HEX_CAPACITY], char verified_rows_sha256[SHA256_HEX_CAPACITY])
{
    char pinned[SHA256_HEX_CAPACITY] = {0}, actual[SHA256_HEX_CAPACITY] = {0};
    struct stat before = {0}, after = {0};
    BqRetirementSupportSubject* subjects = NULL;
    const BqRetirementTrustedRow** by_census = NULL;
    u8* bytes = NULL;
    u32 subject_count = 0;
    u32 expected_subjects = prepared ? prepared->object_rows / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT : 0;
    int descriptor = rows_file >= 3 ? fcntl(rows_file, F_GETFD) : -1;
    int flags = descriptor >= 0 ? fcntl(rows_file, F_GETFL) : -1;
    /* Never let the caller's B digest authorize its own raw rows. */
    bool ok = prepared && rows && verified_inputs_sha256 && verified_rows_sha256 &&
              bq_retirement_profile_sha(profile, S8("census-rows-sha256="), pinned) &&
              bq_retirement_correctness_digest(prepared->census_sha256) &&
              !memcmp(prepared->census_sha256, pinned, SHA256_HEX_CAPACITY) &&
              descriptor >= 0 && (descriptor & FD_CLOEXEC) &&
              flags >= 0 && (flags & O_ACCMODE) == O_RDONLY &&
              fstat(rows_file, &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1 &&
              (before.st_uid == 0 || before.st_uid == geteuid()) && !(before.st_mode & 0222) &&
              before.st_size > 0 && (u64)before.st_size <= BQ_RETIREMENT_CENSUS_ROWS_BYTES_CAP &&
              prepared->object_rows > 0 && prepared->object_rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
              prepared->object_rows % BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT == 0;
    subjects = ok ? calloc(expected_subjects, sizeof(*subjects)) : NULL;
    char verified_inputs[SHA256_HEX_CAPACITY] = {0};
    if (ok) ok = subjects != NULL && bq_retirement_inputs_projection(support_file, inputs_file, profile,
        prepared, rows, subjects, expected_subjects, &subject_count, verified_inputs) &&
        subject_count == expected_subjects;
    by_census = ok ? calloc(prepared->object_rows, sizeof(*by_census)) : NULL;
    if (ok) ok = by_census != NULL;
    for (u32 i = 0; ok && i < prepared->rows; i += 1)
    {
        if (rows[i].stage == BQ_RETIREMENT_STAGE_OBJECT)
        {
            u32 ordinal = rows[i].census_row;
            ok = ordinal < prepared->object_rows && by_census[ordinal] == NULL;
            if (ok) by_census[ordinal] = rows + i;
        }
    }
    bytes = ok ? malloc((size_t)before.st_size) : NULL;
    ok = ok && bytes != NULL;
    u64 read = 0;
    while (ok && read < (u64)before.st_size)
    {
        size_t wanted = (size_t)((u64)before.st_size - read);
        ssize_t count = pread(rows_file, bytes + read, wanted, (off_t)read);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) read += (u64)count;
    }
    if (ok) bq_digest(bytes, (u32)read, (char8*)actual);
    u64 offset = 0;
    String8 text = {(char8*)bytes, read}, line = {0};
    static String8 const header = S8_INITIALIZER("row\tgroup\tfixture\ttarget\ttarget_abi\tcpu\tcpu_features\tallocator\tfrontend_lowering\tPIC\tselected\tfixture_recipe\tcompile_obligation\tlink_obligation\texecution_obligation\tdiagnostic_obligation\targv_evidence");
    ok = ok && bq_next_line(text, &offset, &line) && string_equal(line, header);
    u32 census_rows = 0;
    while (ok && offset < text.length)
    {
        ok = bq_next_line(text, &offset, &line);
        String8 fields[17] = {0};
        ok = ok && bq_retirement_census_line_fields(line, fields) &&
             census_rows < prepared->object_rows &&
             bq_retirement_census_row_projection(fields, census_rows, subjects, subject_count);
        char identity[SHA256_HEX_CAPACITY] = {0};
        ok = ok && bq_retirement_census_identity_sha256(fields, identity) &&
             by_census[census_rows] != NULL &&
             !memcmp(by_census[census_rows]->identity_sha256, identity, SHA256_HEX_CAPACITY);
        census_rows += ok;
    }
    if (ok) ok = census_rows == prepared->object_rows && offset == text.length &&
                 !memcmp(actual, pinned, SHA256_HEX_CAPACITY) &&
                 fstat(rows_file, &after) == 0 && bq_retirement_binary_stable(&before, &after);
    if (ok)
    {
        memcpy(verified_inputs_sha256, verified_inputs, SHA256_HEX_CAPACITY);
        memcpy(verified_rows_sha256, actual, SHA256_HEX_CAPACITY);
    }
    else
    {
        if (verified_inputs_sha256) memset(verified_inputs_sha256, 0, SHA256_HEX_CAPACITY);
        if (verified_rows_sha256) memset(verified_rows_sha256, 0, SHA256_HEX_CAPACITY);
    }
    free(by_census);
    free(subjects);
    free(bytes);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_read_pinned(int file, String8 profile, String8 pin_key,
    u64 byte_cap, u8** output, u64* output_length, char digest[SHA256_HEX_CAPACITY])
{
    char pinned[SHA256_HEX_CAPACITY] = {0};
    struct stat before = {0}, after = {0};
    int descriptor = file >= 3 ? fcntl(file, F_GETFD) : -1;
    int flags = descriptor >= 0 ? fcntl(file, F_GETFL) : -1;
    bool ok = output && output_length && digest && bq_retirement_profile_sha(profile, pin_key, pinned) &&
              descriptor >= 0 && (descriptor & FD_CLOEXEC) && flags >= 0 &&
              (flags & O_ACCMODE) == O_RDONLY && fstat(file, &before) == 0 &&
              S_ISREG(before.st_mode) && before.st_nlink == 1 &&
              (before.st_uid == 0 || before.st_uid == geteuid()) && !(before.st_mode & 0222) &&
              before.st_size > 0 && (u64)before.st_size <= byte_cap;
    u8* bytes = ok ? malloc((size_t)before.st_size) : NULL;
    ok = ok && bytes != NULL;
    u64 read = 0;
    while (ok && read < (u64)before.st_size)
    {
        size_t wanted = (size_t)((u64)before.st_size - read);
        ssize_t count = pread(file, bytes + read, wanted, (off_t)read);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) read += (u64)count;
    }
    if (ok)
    {
        bq_digest(bytes, (u32)read, (char8*)digest);
        ok = !memcmp(digest, pinned, SHA256_HEX_CAPACITY) &&
             fstat(file, &after) == 0 && bq_retirement_binary_stable(&before, &after);
    }
    if (ok)
    {
        *output = bytes;
        *output_length = read;
    }
    else
    {
        free(bytes);
        if (output) *output = NULL;
        if (output_length) *output_length = 0;
        if (digest) memset(digest, 0, SHA256_HEX_CAPACITY);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_string(Sha256* hash, String8 value)
{
    bool ok = hash && value.pointer;
    if (ok) sha256_add(hash, "\"", 1);
    for (u64 index = 0; ok && index < value.length; index += 1)
    {
        u8 byte = value.pointer[index];
        ok = byte >= 0x20 && byte <= 0x7e;
        if (ok && (byte == '"' || byte == '\\')) sha256_add(hash, "\\", 1);
        if (ok) sha256_add(hash, &byte, 1);
    }
    if (ok) sha256_add(hash, "\"", 1);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_map_begin(Sha256* hash, String8 key, bool first)
{
    bool ok = hash != NULL;
    if (ok && !first) sha256_add(hash, ",", 1);
    if (ok) ok = bq_retirement_validator_json_string(hash, key);
    if (ok) sha256_add(hash, ":", 1);
    return ok;
}

typedef struct BqRetirementManifestProperty BqRetirementManifestProperty;
struct BqRetirementManifestProperty
{
    String8 key, value;
};

BUSTER_GLOBAL_LOCAL int bq_retirement_manifest_property_compare(void const* left, void const* right)
{
    BqRetirementManifestProperty const* a = *(BqRetirementManifestProperty const* const*)left;
    BqRetirementManifestProperty const* b = *(BqRetirementManifestProperty const* const*)right;
    u64 common = a->key.length < b->key.length ? a->key.length : b->key.length;
    int order = memcmp(a->key.pointer, b->key.pointer, (size_t)common);
    if (!order) order = a->key.length < b->key.length ? -1 : a->key.length > b->key.length;
    return order;
}

BUSTER_GLOBAL_LOCAL BqRetirementManifestProperty* bq_retirement_manifest_find(
    BqRetirementManifestProperty* properties, u32 count, String8 key)
{
    BqRetirementManifestProperty* result = NULL;
    for (u32 index = 0; index < count; index += 1)
        if (string_equal(properties[index].key, key)) result = properties + index;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_manifest_projection(String8 text,
    BqRetirementManifestProperty properties[128], u32* count_output,
    char digest[SHA256_HEX_CAPACITY])
{
    bool ok = properties && count_output && digest;
    u64 offset = 0;
    u32 count = 0;
    String8 line = {0};
    while (ok && offset < text.length)
    {
        ok = bq_next_line(text, &offset, &line) && line.length > 1 && count < 128;
        u64 split = 0;
        while (ok && split < line.length && line.pointer[split] != '=') split += 1;
        ok = ok && split > 0 && split < line.length;
        if (ok)
        {
            String8 key = {line.pointer, split}, value = {line.pointer + split + 1, line.length - split - 1};
            for (u64 byte = 0; ok && byte < line.length; byte += 1)
                ok = line.pointer[byte] >= 0x20 && line.pointer[byte] <= 0x7e;
            for (u32 previous = 0; ok && previous < count; previous += 1)
                ok = !string_equal(properties[previous].key, key);
            if (ok) properties[count++] = (BqRetirementManifestProperty){key, value};
        }
    }
    ok = ok && offset == text.length && count > 0;
    BqRetirementManifestProperty* ordered[128] = {0};
    for (u32 index = 0; ok && index < count; index += 1) ordered[index] = properties + index;
    if (ok) qsort(ordered, count, sizeof(ordered[0]), bq_retirement_manifest_property_compare);
    Sha256 hash;
    sha256_init(&hash);
    if (ok) sha256_add(&hash, "{", 1);
    bool first = true;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementManifestProperty const* property = ordered[index];
        if (string_equal(property->key, S8("shard_index"))) continue;
        ok = bq_retirement_validator_json_map_begin(&hash, property->key, first) &&
             bq_retirement_validator_json_string(&hash, property->value);
        if (ok) first = false;
    }
    if (ok)
    {
        sha256_add(&hash, "}", 1);
        sha256_finish_hex(&hash, (char8*)digest);
        *count_output = count;
    }
    else
    {
        *count_output = 0;
        memset(digest, 0, SHA256_HEX_CAPACITY);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_inputs_digest(String8 text,
    char digest[SHA256_HEX_CAPACITY], u32* input_count)
{
    static String8 const header = S8_INITIALIZER("path\trole\tcompile_obligation\tbytes\tbuster_hash_64\tsha256\tfixture_recipe\tfixture_flags");
    bool ok = digest != NULL && input_count != NULL;
    u64 offset = 0;
    String8 line = {0}, previous = {0};
    ok = ok && bq_next_line(text, &offset, &line) && string_equal(line, header);
    Sha256 hash;
    sha256_init(&hash);
    if (ok) sha256_add(&hash, "{", 1);
    bool first = true;
    u32 rows = 0;
    while (ok && offset < text.length)
    {
        ok = bq_next_line(text, &offset, &line);
        String8 fields[8] = {0};
        ok = ok && bq_retirement_inputs_line_fields(line, fields);
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            for (u64 byte = 0; ok && byte < fields[field].length; byte += 1)
                ok = fields[field].pointer[byte] >= 0x20 && fields[field].pointer[byte] <= 0x7e;
        u64 common = previous.length < fields[0].length ? previous.length : fields[0].length;
        int order = previous.length ? memcmp(previous.pointer, fields[0].pointer, (size_t)common) : -1;
        ok = ok && fields[0].length > 0 &&
             (!previous.length || order < 0 || (!order && previous.length < fields[0].length));
        if (ok)
        {
            ok = bq_retirement_validator_json_map_begin(&hash, fields[0], first);
            if (ok) sha256_add(&hash, "[", 1);
            for (u32 field = 1; ok && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            {
                if (field > 1) sha256_add(&hash, ",", 1);
                ok = bq_retirement_validator_json_string(&hash, fields[field]);
            }
            if (ok) sha256_add(&hash, "]", 1);
            if (ok)
            {
                first = false;
                previous = fields[0];
                rows += 1;
            }
        }
    }
    ok = ok && rows > 0 && offset == text.length;
    if (ok)
    {
        sha256_add(&hash, "}", 1);
        sha256_finish_hex(&hash, (char8*)digest);
        *input_count = rows;
    }
    else
    {
        memset(digest, 0, SHA256_HEX_CAPACITY);
        if (input_count) *input_count = 0;
    }
    return ok;
}

typedef struct BqRetirementValidatorRawRow BqRetirementValidatorRawRow;
struct BqRetirementValidatorRawRow
{
    String8 fields[17];
};

typedef struct BqRetirementApplicabilityLedgerRecord BqRetirementApplicabilityLedgerRecord;
struct BqRetirementApplicabilityLedgerRecord
{
    String8 fixture, target, fixture_sha256, classification, reason;
};

BUSTER_GLOBAL_LOCAL int bq_retirement_validator_raw_row_compare(void const* left, void const* right)
{
    BqRetirementValidatorRawRow const* a = *(BqRetirementValidatorRawRow const* const*)left;
    BqRetirementValidatorRawRow const* b = *(BqRetirementValidatorRawRow const* const*)right;
    u64 common = a->fields[0].length < b->fields[0].length ? a->fields[0].length : b->fields[0].length;
    int order = memcmp(a->fields[0].pointer, b->fields[0].pointer, (size_t)common);
    if (!order) order = a->fields[0].length < b->fields[0].length ? -1 : a->fields[0].length > b->fields[0].length;
    return order;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_rows_digest(String8 text, u32 expected_rows,
    char digest[SHA256_HEX_CAPACITY], BqRetirementValidatorRawRow** rows_output)
{
    static String8 const header = S8_INITIALIZER("row\tgroup\tfixture\ttarget\ttarget_abi\tcpu\tcpu_features\tallocator\tfrontend_lowering\tPIC\tselected\tfixture_recipe\tcompile_obligation\tlink_obligation\texecution_obligation\tdiagnostic_obligation\targv_evidence");
    bool ok = digest && rows_output && expected_rows > 0 && expected_rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP;
    *rows_output = NULL;
    u64 offset = 0;
    String8 line = {0};
    ok = ok && bq_next_line(text, &offset, &line) && string_equal(line, header);
    BqRetirementValidatorRawRow* rows = ok ? calloc(expected_rows, sizeof(*rows)) : NULL;
    ok = ok && rows != NULL;
    u32 count = 0;
    while (ok && offset < text.length)
    {
        ok = bq_next_line(text, &offset, &line) && count < expected_rows;
        if (ok) ok = bq_retirement_census_line_fields(line, rows[count].fields);
        u64 row_number = 0, group_number = 0;
        if (ok)
        {
            String8* fields = rows[count].fields;
            ok = bq_retirement_number(fields[0], &row_number) && row_number == count &&
                 bq_retirement_number(fields[1], &group_number) && group_number == count / 4u &&
                 (string_equal(fields[10], S8("0")) || string_equal(fields[10], S8("1")));
            for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(rows[count].fields); field += 1)
                for (u64 byte = 0; ok && byte < fields[field].length; byte += 1)
                    ok = fields[field].pointer[byte] >= 0x20 && fields[field].pointer[byte] <= 0x7e;
        }
        if (ok) count += 1;
    }
    ok = ok && count == expected_rows && offset == text.length;
    BqRetirementValidatorRawRow const** ordered = ok ? calloc(count, sizeof(*ordered)) : NULL;
    ok = ok && ordered != NULL;
    for (u32 index = 0; ok && index < count; index += 1) ordered[index] = rows + index;
    if (ok) qsort(ordered, count, sizeof(*ordered), bq_retirement_validator_raw_row_compare);
    Sha256 hash;
    sha256_init(&hash);
    if (ok) sha256_add(&hash, "{", 1);
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementValidatorRawRow const* row = ordered[index];
        ok = bq_retirement_validator_json_map_begin(&hash, row->fields[0], index == 0);
        if (ok) sha256_add(&hash, "[", 1);
        u32 values = 0;
        for (u32 field = 1; ok && field < BUSTER_ARRAY_LENGTH(row->fields); field += 1)
        {
            if (field == 10) continue;
            if (values) sha256_add(&hash, ",", 1);
            ok = bq_retirement_validator_json_string(&hash, row->fields[field]);
            values += ok;
        }
        if (ok) sha256_add(&hash, "]", 1);
    }
    if (ok)
    {
        sha256_add(&hash, "}", 1);
        sha256_finish_hex(&hash, (char8*)digest);
        *rows_output = rows;
    }
    else
    {
        memset(digest, 0, SHA256_HEX_CAPACITY);
        free(rows);
    }
    free(ordered);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_tsv_fields(String8 line, String8* fields, u32 field_count);

BUSTER_GLOBAL_LOCAL int bq_retirement_validator_string_order(String8 left, String8 right)
{
    u64 common = left.length < right.length ? left.length : right.length;
    int order = memcmp(left.pointer, right.pointer, (size_t)common);
    if (!order) order = left.length < right.length ? -1 : left.length > right.length;
    return order;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_applicability_ledger(String8 text,
    char const digest[SHA256_HEX_CAPACITY], String8 census_profile, BqRetirementSupportSubject const* subjects,
    u32 subject_count, BqRetirementApplicabilityLedgerRecord* records, u32 record_capacity,
    u32* record_count)
{
    static String8 const header = S8_INITIALIZER("fixture\ttarget\tfixture_sha256\tapplicability\treason");
    static String8 const authenticated_classes[3] = {
        S8_INITIALIZER("admitted-supported"), S8_INITIALIZER("platform-inapplicable"),
        S8_INITIALIZER("unavailable")
    };
    bool ok = digest && subjects && subject_count > 0 && records && record_capacity > 0 && record_count &&
              bq_retirement_hex((String8){(char8*)digest, 64}, 64) &&
              (string_equal(census_profile, S8("self-test")) ||
               string_equal(census_profile, S8("full-census")));
    u64 offset = 0;
    String8 line = {0}, previous_fixture = {0}, previous_target = {0};
    if (ok) ok = bq_next_line(text, &offset, &line) && string_equal(line, header);
    u32 count = 0;
    while (ok && offset < text.length)
    {
        String8 fields[5] = {0};
        ok = bq_next_line(text, &offset, &line) && count < record_capacity &&
             bq_retirement_validator_tsv_fields(line, fields, BUSTER_ARRAY_LENGTH(fields));
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            for (u64 byte = 0; ok && byte < fields[field].length; byte += 1)
                ok = fields[field].pointer[byte] >= 0x20 && fields[field].pointer[byte] <= 0x7e;
        bool target_found = false, class_found = false, subject_found = false;
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(bq_retirement_census_targets); index += 1)
            target_found = target_found || string_equal(fields[1], bq_retirement_census_targets[index]);
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(authenticated_classes); index += 1)
            class_found = class_found || string_equal(fields[3], authenticated_classes[index]);
        for (u32 index = 0; ok && index < subject_count; index += 1)
            if (string_equal(fields[0], string_from_pointer(subjects[index].path)))
            {
                subject_found = string_equal(fields[2], string_from_pointer(subjects[index].sha256));
                ok = subject_found;
            }
        bool reason_valid = fields[4].length > 0;
        for (u64 index = 0; reason_valid && index < fields[4].length; index += 1)
        {
            u8 byte = fields[4].pointer[index];
            reason_valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_';
        }
        int fixture_order = count ? bq_retirement_validator_string_order(previous_fixture, fields[0]) : -1;
        int target_order = count ? bq_retirement_validator_string_order(previous_target, fields[1]) : -1;
        ok = ok && fields[0].length > 0 && fields[0].length <= BQ_PATH_CAP && target_found &&
             class_found && subject_found && reason_valid &&
             (!count || fixture_order < 0 || (!fixture_order && target_order < 0));
        if (ok)
        {
            records[count++] = (BqRetirementApplicabilityLedgerRecord){
                fields[0], fields[1], fields[2], fields[3], fields[4]
            };
            previous_fixture = fields[0];
            previous_target = fields[1];
        }
    }
    if (ok && string_equal(census_profile, S8("full-census")))
        ok = count == BQ_RETIREMENT_FULL_APPLICABILITY_LEDGER_COUNT &&
             !memcmp(digest, bq_retirement_full_applicability_ledger_sha256, 64);
    if (record_count) *record_count = ok ? count : 0;
    return ok && offset == text.length;
}

BUSTER_GLOBAL_LOCAL BqRetirementApplicabilityLedgerRecord const* bq_retirement_validator_ledger_find(
    BqRetirementApplicabilityLedgerRecord const* records, u32 count, String8 fixture, String8 target)
{
    u32 low = 0, high = count;
    BqRetirementApplicabilityLedgerRecord const* result = NULL;
    while (low < high && !result)
    {
        u32 middle = low + (high - low) / 2u;
        BqRetirementApplicabilityLedgerRecord const* record = records + middle;
        int fixture_order = bq_retirement_validator_string_order(record->fixture, fixture);
        int target_order = !fixture_order ? bq_retirement_validator_string_order(record->target, target) : 0;
        if (!fixture_order && !target_order) result = record;
        else if (fixture_order < 0 || (!fixture_order && target_order < 0)) low = middle + 1u;
        else high = middle;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_top_value(String8 json, String8 key,
    String8* value, u64* value_offset)
{
    bool found = false;
    bool malformed = false;
    u64 offset = 0;
    String8 line = {0};
    while (bq_next_line(json, &offset, &line))
    {
        if (line.length >= key.length + 6 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
            line.pointer[2] == '"' && !memcmp(line.pointer + 3, key.pointer, (size_t)key.length) &&
            line.pointer[3 + key.length] == '"' && line.pointer[4 + key.length] == ':')
        {
            if (found) malformed = true;
            u64 begin = 5 + key.length;
            while (begin < line.length && line.pointer[begin] == ' ') begin += 1;
            if (begin >= line.length) malformed = true;
            if (!malformed)
            {
                if (value) *value = (String8){line.pointer + begin, line.length - begin};
                if (value_offset) *value_offset = offset - line.length - 1 + begin;
                found = true;
            }
        }
    }
    return found && !malformed && offset == json.length;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_top_scalar(String8 json, String8 key, String8* value)
{
    String8 raw = {0};
    bool ok = value && bq_retirement_validator_json_top_value(json, key, &raw, NULL) && raw.length > 0 &&
              raw.pointer[0] != '[' && raw.pointer[0] != '{';
    u64 length = raw.length;
    if (ok && raw.pointer[length - 1] == ',') length -= 1;
    if (ok) ok = length > 0;
    if (ok) *value = (String8){raw.pointer, length};
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_uint(String8 json, String8 key, u64* number)
{
    String8 value = {0};
    bool ok = number && bq_retirement_validator_json_top_scalar(json, key, &value) &&
              bq_retirement_number(value, number);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_bool(String8 json, String8 key, bool* value)
{
    String8 raw = {0};
    bool ok = value && bq_retirement_validator_json_top_scalar(json, key, &raw);
    if (ok && string_equal(raw, S8("true"))) *value = true;
    else if (ok && string_equal(raw, S8("false"))) *value = false;
    else ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_string_value(String8 json, String8 key, String8* value)
{
    String8 raw = {0};
    bool ok = value && bq_retirement_validator_json_top_scalar(json, key, &raw) && raw.length >= 2 &&
              raw.pointer[0] == '"' && raw.pointer[raw.length - 1] == '"';
    for (u64 index = 1; ok && index + 1 < raw.length; index += 1)
        ok = raw.pointer[index] != '"' && raw.pointer[index] != '\\' &&
             raw.pointer[index] >= 0x20 && raw.pointer[index] <= 0x7e;
    if (ok) *value = (String8){raw.pointer + 1, raw.length - 2};
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_row_set(String8 json, String8 key, u32 row_count,
    u8* rows, u32* row_count_output)
{
    u64 value_offset = 0;
    bool ok = rows && row_count_output && row_count > 0 &&
              bq_retirement_validator_json_top_value(json, key, NULL, &value_offset) &&
              value_offset < json.length && json.pointer[value_offset] == '[';
    u32 count = 0;
    u64 cursor = value_offset + 1;
    bool need_value = true;
    u64 previous_row = 0;
    bool closed = false;
    while (ok && cursor < json.length)
    {
        while (cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                         json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        if (cursor < json.length && json.pointer[cursor] == ']')
        {
            ok = !need_value || count == 0;
            cursor += 1;
            closed = true;
            break;
        }
        if (!need_value)
        {
            ok = cursor < json.length && json.pointer[cursor] == ',';
            if (ok)
            {
                cursor += 1;
                need_value = true;
            }
            continue;
        }
        else
        {
            u64 start = cursor;
            while (cursor < json.length && json.pointer[cursor] >= '0' && json.pointer[cursor] <= '9') cursor += 1;
            u64 row = 0;
            ok = cursor > start && count < row_count &&
                 bq_retirement_number((String8){json.pointer + start, cursor - start}, &row) && row < row_count &&
                 !rows[row] && (!count || row > previous_row);
            if (ok)
            {
                rows[row] = 1;
                count += 1;
                previous_row = row;
                need_value = false;
            }
        }
    }
    if (ok && closed)
    {
        while (cursor < json.length && json.pointer[cursor] != '\n')
        {
            ok = json.pointer[cursor] == ' ' || json.pointer[cursor] == '\r' || json.pointer[cursor] == ',';
            if (!ok) break;
            cursor += 1;
        }
    }
    ok = ok && closed && count <= row_count;
    if (ok) *row_count_output = count;
    else *row_count_output = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_skip_rows(String8 json, u32 row_count,
    u8* skips, u32* skip_count)
{
    return bq_retirement_validator_json_row_set(json, S8("applicability_skip_rows"), row_count,
                                                 skips, skip_count);
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_row_set_sha256(u8 const* rows, u32 row_count,
    char digest[SHA256_HEX_CAPACITY])
{
    bool ok = rows && row_count > 0 && digest;
    Sha256 hash;
    sha256_init(&hash);
    if (ok) sha256_add(&hash, "[", 1);
    bool first = true;
    for (u32 row = 0; ok && row < row_count; row += 1)
    {
        if (rows[row])
        {
            if (!first) sha256_add(&hash, ",", 1);
            char digits[16] = {0};
            u32 length = 0, value = row;
            do
            {
                digits[length++] = (char)('0' + value % 10u);
                value /= 10u;
            }
            while (value && length < BUSTER_ARRAY_LENGTH(digits));
            ok = length > 0 && length < BUSTER_ARRAY_LENGTH(digits);
            while (ok && length) sha256_add(&hash, digits + --length, 1);
            first = false;
        }
    }
    if (ok)
    {
        sha256_add(&hash, "]", 1);
        sha256_finish_hex(&hash, (char8*)digest);
    }
    else if (digest) memset(digest, 0, SHA256_HEX_CAPACITY);
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_retirement_validator_class(String8 value)
{
    u32 result = 0;
    if (string_equal(value, S8("admitted-supported"))) result = 1;
    else if (string_equal(value, S8("retained-control"))) result = 2;
    else if (string_equal(value, S8("retained-reference"))) result = 3;
    else if (string_equal(value, S8("platform-inapplicable"))) result = 4;
    else if (string_equal(value, S8("unavailable"))) result = 5;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_classification_expected(
    BqRetirementValidatorRawRow const* raw, BqRetirementApplicabilityLedgerRecord const* auth,
    bool supported_gap, bool baseline_unresolved, bool supplement_resolved, u32 actual_class,
    String8 actual_reason, String8 actual_ownership)
{
    String8 expected_reason = {0}, expected_ownership = {0};
    u32 expected_class = 0;
    bool ok = raw != NULL;
    String8 const* source = raw ? raw->fields : NULL;
    if (ok && supported_gap)
    {
        expected_class = 1;
        expected_reason = S8("supported-object-zero-fallback");
        expected_ownership = S8("candidate-compiler");
        ok = (!auth || string_equal(auth->classification, S8("admitted-supported"))) &&
             string_equal(source[12], S8("supported-object-zero-fallback")) &&
             !string_equal(source[7], S8("none"));
    }
    else if (ok && auth)
    {
        expected_class = bq_retirement_validator_class(auth->classification);
        expected_reason = auth->reason;
        expected_ownership = S8("applicability-manifest");
        ok = expected_class != 0;
    }
    else if (ok && string_equal(source[14], S8("unavailable-platform-control")))
    {
        expected_class = 4;
        expected_reason = S8("native-execution-owner-unavailable");
        expected_ownership = S8("platform-execution");
    }
    else if (ok && string_equal(source[12], S8("registered-non-object-control")))
    {
        expected_class = 2;
        expected_reason = S8("registered-non-object-control");
        expected_ownership = S8("source-registration");
    }
    else if (ok && !string_equal(source[12], S8("supported-object-zero-fallback")))
    {
        expected_class = 5;
        expected_reason = S8("compile-obligation-not-admitted");
        expected_ownership = S8("admission");
    }
    else if (ok && baseline_unresolved)
    {
        expected_class = 3;
        expected_reason = S8("direct-reference-unresolved");
        expected_ownership = S8("reference-compiler");
    }
    else if (ok && string_equal(source[7], S8("none")))
    {
        expected_class = 2;
        expected_reason = S8("direct-reference-control");
        expected_ownership = S8("reference-compiler");
    }
    else if (ok)
    {
        expected_class = 1;
        expected_reason = S8("supported-object-zero-fallback");
        expected_ownership = S8("candidate-compiler");
    }
    /* The aggregate validator retains the original class, then replaces
     * reason/ownership for each independently resolved reference-failure row. */
    if (ok && supplement_resolved)
    {
        expected_reason = S8("direct-reference-unresolved-clang-control-passed");
        expected_ownership = S8("clang-reference");
    }
    ok = ok && actual_class == expected_class && expected_class != 0 &&
         string_equal(actual_reason, expected_reason) && string_equal(actual_ownership, expected_ownership);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_class_counts(String8 json, String8 key,
    u32 counts[5])
{
    static String8 const class_names[5] = {
        S8_INITIALIZER("admitted-supported"), S8_INITIALIZER("retained-control"),
        S8_INITIALIZER("retained-reference"), S8_INITIALIZER("platform-inapplicable"),
        S8_INITIALIZER("unavailable")
    };
    u64 offset = 0;
    bool ok = bq_retirement_validator_json_top_value(json, key, NULL, &offset) &&
              offset < json.length && json.pointer[offset] == '{';
    String8 line = {0};
    bool closed = false;
    if (ok)
    {
        ok = bq_next_line(json, &offset, &line) && line.length == 1 && line.pointer[0] == '{';
    }
    u8 seen[5] = {0};
    while (ok && offset < json.length)
    {
        ok = bq_next_line(json, &offset, &line);
        if (!ok) break;
        if (line.length >= 3 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
            line.pointer[2] == '}')
        {
            ok = line.length == 3 || (line.length == 4 && line.pointer[3] == ',');
            closed = ok;
            break;
        }
        ok = line.length > 8 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
             line.pointer[2] == ' ' && line.pointer[3] == ' ' && line.pointer[4] == '"';
        u64 end = 5;
        while (ok && end < line.length && line.pointer[end] != '"') end += 1;
        ok = ok && end < line.length && end + 2 < line.length && line.pointer[end + 1] == ':' &&
             line.pointer[end + 2] == ' ';
        String8 name = ok ? (String8){line.pointer + 5, end - 5} : (String8){0};
        u32 class_index = 5;
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(class_names); index += 1)
            if (string_equal(name, class_names[index])) class_index = index;
        u64 count = 0;
        u64 value_start = end + 3, value_end = value_start;
        while (ok && value_end < line.length && line.pointer[value_end] >= '0' && line.pointer[value_end] <= '9')
            value_end += 1;
        ok = ok && class_index < BUSTER_ARRAY_LENGTH(class_names) && !seen[class_index] &&
             value_end > value_start &&
             (value_end == line.length || (value_end + 1 == line.length && line.pointer[value_end] == ',')) &&
             bq_retirement_number((String8){line.pointer + value_start, value_end - value_start}, &count) &&
             count <= UINT32_MAX;
        if (ok)
        {
            seen[class_index] = 1;
            counts[class_index] = (u32)count;
        }
    }
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(seen); index += 1) ok = seen[index] == 1;
    ok = ok && closed;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_class_rows(String8 json, String8 key,
    u32 row_count, u8 const* expected_classes)
{
    static String8 const class_names[5] = {
        S8_INITIALIZER("admitted-supported"), S8_INITIALIZER("retained-control"),
        S8_INITIALIZER("retained-reference"), S8_INITIALIZER("platform-inapplicable"),
        S8_INITIALIZER("unavailable")
    };
    u64 offset = 0;
    bool ok = row_count > 0 && expected_classes &&
              bq_retirement_validator_json_top_value(json, key, NULL, &offset) &&
              offset < json.length && json.pointer[offset] == '{';
    String8 line = {0};
    if (ok) ok = bq_next_line(json, &offset, &line) && line.length == 1 && line.pointer[0] == '{';
    u8 seen_classes[5] = {0};
    u8* seen_rows = ok ? calloc(row_count, 1) : NULL;
    ok = ok && seen_rows != NULL;
    bool closed = false;
    while (ok && offset < json.length)
    {
        ok = bq_next_line(json, &offset, &line);
        if (!ok) break;
        if (line.length >= 3 && line.pointer[0] == ' ' && line.pointer[1] == ' ' && line.pointer[2] == '}')
        {
            ok = line.length == 3 || (line.length == 4 && line.pointer[3] == ',');
            closed = ok;
            break;
        }
        ok = line.length > 9 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
             line.pointer[2] == ' ' && line.pointer[3] == ' ' && line.pointer[4] == '"';
        u64 name_end = 5;
        while (ok && name_end < line.length && line.pointer[name_end] != '"') name_end += 1;
        ok = ok && name_end < line.length && name_end + 2 < line.length &&
             line.pointer[name_end + 1] == ':' && line.pointer[name_end + 2] == ' ';
        String8 name = ok ? (String8){line.pointer + 5, name_end - 5} : (String8){0};
        u32 class_index = 5;
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(class_names); index += 1)
            if (string_equal(name, class_names[index])) class_index = index;
        ok = ok && class_index < BUSTER_ARRAY_LENGTH(class_names) && !seen_classes[class_index];
            u64 value = name_end + 3;
        if (ok && value + 2 <= line.length && line.pointer[value] == '[' && line.pointer[value + 1] == ']')
        {
            value += 2;
            if (value < line.length && line.pointer[value] == ',') value += 1;
            ok = value == line.length;
        }
        else if (ok)
        {
            ok = value < line.length && line.pointer[value] == '[' && value + 1 == line.length;
            u64 previous_row = 0;
            bool first_row = true;
            while (ok && offset < json.length)
            {
                ok = bq_next_line(json, &offset, &line);
                if (!ok) break;
                if (line.length >= 5 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
                    line.pointer[2] == ' ' && line.pointer[3] == ' ' && line.pointer[4] == ']')
                {
                    ok = line.length == 5 || (line.length == 6 && line.pointer[5] == ',');
                    break;
                }
                ok = line.length > 6 && line.pointer[0] == ' ' && line.pointer[1] == ' ' &&
                     line.pointer[2] == ' ' && line.pointer[3] == ' ' &&
                     line.pointer[4] == ' ' && line.pointer[5] == ' ';
                u64 row_end = 6;
                while (ok && row_end < line.length && line.pointer[row_end] >= '0' && line.pointer[row_end] <= '9')
                    row_end += 1;
                u64 row_number = 0;
                ok = ok && row_end > 6 &&
                     (row_end == line.length || (row_end + 1 == line.length && line.pointer[row_end] == ',')) &&
                     bq_retirement_number((String8){line.pointer + 6, row_end - 6}, &row_number) &&
                     row_number < row_count && !seen_rows[row_number] &&
                     (first_row || row_number > previous_row) &&
                     expected_classes[row_number] == class_index + 1;
                if (ok)
                {
                    seen_rows[row_number] = 1;
                    previous_row = row_number;
                    first_row = false;
                }
            }
        }
        if (ok) seen_classes[class_index] = 1;
    }
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(seen_classes); index += 1)
        ok = seen_classes[index] == 1;
    for (u32 index = 0; ok && index < row_count; index += 1) ok = seen_rows[index] == 1;
    ok = ok && closed;
    free(seen_rows);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_empty_array(String8 json, String8 key)
{
    u64 offset = 0;
    bool ok = bq_retirement_validator_json_top_value(json, key, NULL, &offset) && offset < json.length &&
              json.pointer[offset] == '[';
    if (ok) offset += 1;
    while (ok && offset < json.length &&
           (json.pointer[offset] == ' ' || json.pointer[offset] == '\n' ||
            json.pointer[offset] == '\r' || json.pointer[offset] == '\t')) offset += 1;
    ok = ok && offset < json.length && json.pointer[offset] == ']';
    if (ok) offset += 1;
    while (ok && offset < json.length && json.pointer[offset] != '\n')
    {
        ok = json.pointer[offset] == ' ' || json.pointer[offset] == '\r' || json.pointer[offset] == ',';
        if (ok) offset += 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_string_array_equals(String8 json, String8 key,
    String8 const* expected, u32 expected_count)
{
    u64 cursor = 0;
    bool ok = expected && bq_retirement_validator_json_top_value(json, key, NULL, &cursor) &&
              cursor < json.length && json.pointer[cursor] == '[';
    if (ok) cursor += 1;
    for (u32 index = 0; ok && index < expected_count; index += 1)
    {
        while (cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                         json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        if (index) ok = cursor < json.length && json.pointer[cursor++] == ',';
        while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                                json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        ok = ok && cursor < json.length && json.pointer[cursor++] == '"';
        u64 start = cursor;
        while (ok && cursor < json.length && json.pointer[cursor] != '"' && json.pointer[cursor] != '\\' &&
               json.pointer[cursor] >= 0x20 && json.pointer[cursor] <= 0x7e) cursor += 1;
        ok = ok && cursor < json.length && json.pointer[cursor] == '"' &&
             string_equal((String8){json.pointer + start, cursor - start}, expected[index]);
        if (ok) cursor += 1;
    }
    while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                            json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
    ok = ok && cursor < json.length && json.pointer[cursor++] == ']';
    while (ok && cursor < json.length && json.pointer[cursor] != '\n')
    {
        ok = json.pointer[cursor] == ' ' || json.pointer[cursor] == '\r' || json.pointer[cursor] == ',';
        if (ok) cursor += 1;
    }
    return ok;
}

/* The official full census retains one supplemental reference digest per
 * shard, including when a direct-reference failure was later resolved. The
 * projection validates only the canonical shape; independent supplement and
 * shard replay remains a separate production authority. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_sha256_array(String8 json, String8 key,
    u32 expected_count)
{
    u64 cursor = 0;
    bool ok = bq_retirement_validator_json_top_value(json, key, NULL, &cursor) &&
              cursor < json.length && json.pointer[cursor++] == '[';
    for (u32 index = 0; ok && index < expected_count; index += 1)
    {
        while (cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                         json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        if (index) ok = cursor < json.length && json.pointer[cursor++] == ',';
        while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                                json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        ok = ok && cursor < json.length && json.pointer[cursor++] == '"' &&
             cursor + 64 < json.length &&
             bq_retirement_hex((String8){json.pointer + cursor, 64}, 64) &&
             json.pointer[cursor + 64] == '"';
        if (ok) cursor += 65;
    }
    while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                            json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
    ok = ok && cursor < json.length && json.pointer[cursor++] == ']';
    while (ok && cursor < json.length && json.pointer[cursor] != '\n')
    {
        ok = json.pointer[cursor] == ' ' || json.pointer[cursor] == '\r' || json.pointer[cursor] == ',';
        if (ok) cursor += 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_rows_match_flags(String8 json, String8 key,
    u32 row_count, u8 const* expected_flags)
{
    u64 cursor = 0;
    bool ok = row_count > 0 && expected_flags &&
              bq_retirement_validator_json_top_value(json, key, NULL, &cursor) &&
              cursor < json.length && json.pointer[cursor] == '[';
    if (ok) cursor += 1;
    u32 expected_row = 0, item_count = 0;
    bool closed = false;
    for (;;)
    {
        while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                               json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        if (ok && cursor < json.length && json.pointer[cursor] == ']')
        {
            cursor += 1;
            closed = true;
            break;
        }
        if (!ok || cursor >= json.length) break;
        if (item_count) ok = json.pointer[cursor++] == ',';
        while (ok && cursor < json.length && (json.pointer[cursor] == ' ' || json.pointer[cursor] == '\n' ||
                                                json.pointer[cursor] == '\r' || json.pointer[cursor] == '\t')) cursor += 1;
        u64 start = cursor;
        while (ok && cursor < json.length && json.pointer[cursor] >= '0' && json.pointer[cursor] <= '9') cursor += 1;
        u64 actual_row = 0;
        ok = ok && cursor > start &&
             bq_retirement_number((String8){json.pointer + start, cursor - start}, &actual_row) &&
             actual_row < row_count;
        while (ok && expected_row < row_count && !expected_flags[expected_row]) expected_row += 1;
        ok = ok && expected_row < row_count && actual_row == expected_row;
        if (ok)
        {
            expected_row += 1;
            item_count += 1;
        }
    }
    while (ok && expected_row < row_count && !expected_flags[expected_row]) expected_row += 1;
    while (ok && cursor < json.length && json.pointer[cursor] != '\n')
    {
        ok = json.pointer[cursor] == ' ' || json.pointer[cursor] == '\r' || json.pointer[cursor] == ',';
        if (ok) cursor += 1;
    }
    return ok && closed && expected_row == row_count;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_tsv_fields(String8 line, String8* fields, u32 field_count)
{
    bool ok = line.length > 0 && fields && field_count > 0;
    u32 field = 0;
    u64 start = 0;
    for (u64 index = 0; ok && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == '\t')
        {
            ok = field < field_count && index > start;
            if (ok) fields[field++] = (String8){line.pointer + start, index - start};
            start = index + 1;
        }
    }
    return ok && field == field_count;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_hex(String8 json, String8 key,
    char output[SHA256_HEX_CAPACITY])
{
    String8 value = {0};
    bool ok = output && bq_retirement_validator_json_string_value(json, key, &value) &&
              bq_retirement_hex(value, 64);
    if (ok) memcpy(output, value.pointer, 64), output[64] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_matches_hex(String8 json, String8 key,
    char const expected[SHA256_HEX_CAPACITY])
{
    char value[SHA256_HEX_CAPACITY] = {0};
    bool ok = bq_retirement_validator_json_hex(json, key, value) &&
              !memcmp(value, expected, SHA256_HEX_CAPACITY);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_matches_string(String8 json, String8 key,
    String8 expected)
{
    String8 value = {0};
    bool ok = bq_retirement_validator_json_string_value(json, key, &value) &&
              bq_retirement_hex(expected, 64) && string_equal(value, expected);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_json_report_scalar(String8 json, String8 key,
    u64 expected)
{
    u64 value = 0;
    bool ok = bq_retirement_validator_json_uint(json, key, &value) && value == expected;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_support_inputs_join(String8 support_text,
    String8 inputs_text, char inputs_digest[SHA256_HEX_CAPACITY], BqRetirementSupportSubject* subjects,
    u32 subject_capacity, u32* subject_count, u32* input_count)
{
    static String8 const support_header = S8_INITIALIZER("path\trole\tcompile_obligation\tbytes\tsha256");
    static String8 const inputs_header = S8_INITIALIZER("path\trole\tcompile_obligation\tbytes\tbuster_hash_64\tsha256\tfixture_recipe\tfixture_flags");
    bool ok = inputs_digest && subjects && subject_count && input_count && subject_capacity > 0;
    u64 support_offset = 0, inputs_offset = 0;
    String8 support_line = {0}, inputs_line = {0};
    ok = ok && bq_next_line(support_text, &support_offset, &support_line) &&
         bq_next_line(inputs_text, &inputs_offset, &inputs_line) &&
         string_equal(support_line, support_header) && string_equal(inputs_line, inputs_header);
    u32 subjects_found = 0, inputs_found = 0;
    String8 previous = {0};
    while (ok && (support_offset < support_text.length || inputs_offset < inputs_text.length))
    {
        ok = bq_next_line(support_text, &support_offset, &support_line) &&
             bq_next_line(inputs_text, &inputs_offset, &inputs_line);
        String8 support_fields[5] = {0}, fields[8] = {0};
        ok = ok && bq_retirement_support_line_fields(support_line, support_fields) &&
             bq_retirement_inputs_line_fields(inputs_line, fields);
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(support_fields); field += 1)
            for (u64 byte = 0; ok && byte < support_fields[field].length; byte += 1)
                ok = support_fields[field].pointer[byte] >= 0x20 && support_fields[field].pointer[byte] <= 0x7e;
        for (u32 field = 0; ok && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            for (u64 byte = 0; ok && byte < fields[field].length; byte += 1)
                ok = fields[field].pointer[byte] >= 0x20 && fields[field].pointer[byte] <= 0x7e;
        u64 common = previous.length < fields[0].length ? previous.length : fields[0].length;
        int order = previous.length ? memcmp(previous.pointer, fields[0].pointer, (size_t)common) : -1;
        u64 input_bytes = 0, buster_hash = 0;
        ok = ok && (!previous.length || order < 0 || (!order && previous.length < fields[0].length)) &&
             string_equal(support_fields[0], fields[0]) && string_equal(support_fields[1], fields[1]) &&
             string_equal(support_fields[2], fields[2]) && string_equal(support_fields[3], fields[3]) &&
             string_equal(support_fields[4], fields[5]) && bq_retirement_number(fields[3], &input_bytes) &&
             input_bytes > 0 && bq_retirement_number(fields[4], &buster_hash) &&
             bq_retirement_hex(fields[5], 64);
        BqRetirementFixtureRecipe recipe = {0};
        if (ok) recipe = bq_retirement_census_fixture_recipe(fields[0]);
        ok = ok && string_equal(fields[6], recipe.name) && string_equal(fields[7], recipe.flags);
        if (ok && string_equal(fields[1], S8("subject")))
        {
            ok = subjects_found < subject_capacity && fields[0].length <= BQ_PATH_CAP &&
                 fields[2].length < sizeof(subjects[subjects_found].compile_obligation) &&
                 recipe.name.length < sizeof(subjects[subjects_found].fixture_recipe);
            if (ok)
            {
                BqRetirementSupportSubject* subject = subjects + subjects_found;
                memcpy(subject->path, fields[0].pointer, (size_t)fields[0].length);
                subject->path[fields[0].length] = 0;
                memcpy(subject->compile_obligation, fields[2].pointer, (size_t)fields[2].length);
                subject->compile_obligation[fields[2].length] = 0;
                memcpy(subject->sha256, fields[5].pointer, 64);
                subject->sha256[64] = 0;
                memcpy(subject->fixture_recipe, recipe.name.pointer, (size_t)recipe.name.length);
                subject->fixture_recipe[recipe.name.length] = 0;
                subjects_found += 1;
            }
        }
        if (ok)
        {
            previous = fields[0];
            inputs_found += 1;
        }
    }
    u32 digest_count = 0;
    bool digest_ok = ok && bq_retirement_validator_inputs_digest(inputs_text, inputs_digest, &digest_count);
    ok = ok && digest_ok && inputs_found > 0 && digest_count == inputs_found &&
         subjects_found > 0 && support_offset == support_text.length && inputs_offset == inputs_text.length;
    *subject_count = ok ? subjects_found : 0;
    *input_count = ok ? inputs_found : 0;
    if (!ok) memset(inputs_digest, 0, SHA256_HEX_CAPACITY);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_applicability_projection(String8 report,
    String8 report_sha256, String8 census_profile, String8 applicability, String8 skips_text,
    BqRetirementValidatorRawRow const* raw_rows, BqRetirementSupportSubject const* subjects,
    u32 subject_count, BqRetirementApplicabilityLedgerRecord const* ledger, u32 ledger_count,
    u32 row_count, BqRetirementValidatorEligibility* projection)
{
    static String8 const app_header = S8_INITIALIZER("row\tgroup\tfixture\ttarget\tcpu\tfrontend\tallocator\tPIC\tapplicability\tadmission\tdisposition\treason\townership\tcandidate_failure\treference_failure\tacceptance_failure");
    static String8 const skips_header = S8_INITIALIZER("row\tgroup\tfixture\ttarget\tallocator\tapplicability\treason");
    static String8 const failure_arrays[] = {
        S8_INITIALIZER("candidate_failure_rows"),
        S8_INITIALIZER("reference_failure_rows"), S8_INITIALIZER("fallback_defect_rows"),
        S8_INITIALIZER("telemetry_defect_rows"), S8_INITIALIZER("execution_defect_rows"),
        S8_INITIALIZER("artifact_defect_rows"), S8_INITIALIZER("unexpected_failure_rows")
    };
    bool ok = report.pointer && report_sha256.length == 64 && applicability.pointer && skips_text.pointer &&
              raw_rows && subjects && subject_count > 0 && ledger && ledger_count <= BQ_RETIREMENT_VALIDATOR_LEDGER_RECORD_CAP &&
              row_count > 0 && projection;
    u8* observed_skips = ok ? calloc(row_count, 1) : NULL;
    u8* candidate_failures = ok ? calloc(row_count, 1) : NULL;
    u8* reference_failures = ok ? calloc(row_count, 1) : NULL;
    u8* acceptance_failures = ok ? calloc(row_count, 1) : NULL;
    u8* supported_gap_rows = ok ? calloc(row_count, 1) : NULL;
    u8* inapplicable_rows = ok ? calloc(row_count, 1) : NULL;
    u8* direct_reference_failures = ok ? calloc(row_count, 1) : NULL;
    ok = ok && observed_skips && candidate_failures && reference_failures && acceptance_failures &&
         supported_gap_rows && inapplicable_rows && direct_reference_failures;
    u32 report_skip_count = 0, supported_gap_count = 0, acceptance_failure_count = 0;
    u32 direct_failure_count = 0;
    if (ok) ok = bq_retirement_validator_json_skip_rows(report, row_count, observed_skips,
                                                         &report_skip_count) &&
                 bq_retirement_validator_json_row_set(report, S8("supported_gap_rows"), row_count,
                    supported_gap_rows, &supported_gap_count) &&
                 bq_retirement_validator_json_row_set(report, S8("direct_reference_failure_rows"),
                    row_count, direct_reference_failures, &direct_failure_count);
    u32 report_counts[5] = {0}, admission_counts[5] = {0}, app_counts[5] = {0};
    if (ok) ok = bq_retirement_validator_class_counts(report, S8("applicability_counts"), report_counts) &&
                 bq_retirement_validator_class_counts(report, S8("admission_counts"), admission_counts);
    char supported_gap_sha256[SHA256_HEX_CAPACITY] = {0};
    if (ok) ok = bq_retirement_validator_row_set_sha256(supported_gap_rows, row_count,
                                                          supported_gap_sha256) &&
                 bq_retirement_validator_json_report_scalar(report, S8("supported_gap_count"),
                                                             supported_gap_count) &&
                 bq_retirement_validator_json_matches_hex(report, S8("supported_gap_sha256"),
                                                          supported_gap_sha256);
    if (ok && string_equal(census_profile, S8("full-census")))
        ok = supported_gap_count == BQ_RETIREMENT_FULL_SUPPORTED_GAP_COUNT &&
             !memcmp(supported_gap_sha256, bq_retirement_full_supported_gap_sha256,
                     SHA256_HEX_CAPACITY) &&
             bq_retirement_validator_json_sha256_array(report, S8("reference_supplement_sha256"), 4);
    else if (ok && string_equal(census_profile, S8("self-test")))
        ok = supported_gap_count == 0 && direct_failure_count == 0 &&
             bq_retirement_validator_json_sha256_array(report, S8("reference_supplement_sha256"), 0);
    else if (ok) ok = false;
    String8 residual_evidence = {0}, residual_tsv = {0};
    bool residual_truncated = true, require_clean_acceptance = false, clean_acceptance = false;
    if (ok) ok = bq_retirement_validator_json_string_value(report, S8("residual_evidence"),
                                                            &residual_evidence) &&
                 bq_retirement_validator_json_string_value(report, S8("residual_tsv"), &residual_tsv) &&
                 residual_evidence.length > 0 && string_equal(residual_evidence, residual_tsv) &&
                 bq_retirement_validator_json_report_scalar(report, S8("residual_rows"), 0) &&
                 bq_retirement_validator_json_report_scalar(report, S8("residual_limit"), 256) &&
                 bq_retirement_validator_json_bool(report, S8("residual_truncated"), &residual_truncated) &&
                 !residual_truncated &&
                 bq_retirement_validator_json_matches_hex(report, S8("residual_sha256"),
                                                          bq_retirement_empty_residual_sha256) &&
                 bq_retirement_validator_json_bool(report, S8("require_clean_acceptance"),
                                                   &require_clean_acceptance) &&
                 bq_retirement_validator_json_bool(report, S8("clean_acceptance"), &clean_acceptance);
    if (ok && string_equal(census_profile, S8("full-census")))
        ok = require_clean_acceptance && clean_acceptance;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(failure_arrays); index += 1)
        ok = bq_retirement_validator_json_empty_array(report, failure_arrays[index]);
    u64 offset = 0;
    String8 line = {0};
    if (ok) ok = bq_next_line(applicability, &offset, &line) && string_equal(line, app_header);
    u32 app_rows = 0;
    while (ok && offset < applicability.length)
    {
        String8 fields[16] = {0};
        ok = bq_next_line(applicability, &offset, &line) && app_rows < row_count &&
             bq_retirement_validator_tsv_fields(line, fields, BUSTER_ARRAY_LENGTH(fields));
        u64 row_number = 0;
        if (ok) ok = bq_retirement_number(fields[0], &row_number) && row_number == app_rows;
        u32 raw_index = app_rows < row_count ? app_rows : row_count - 1u;
        BqRetirementValidatorRawRow const* raw = raw_rows + raw_index;
        String8 const* source = raw->fields;
        if (ok)
            ok = string_equal(fields[1], source[1]) && string_equal(fields[2], source[2]) &&
                 string_equal(fields[3], source[3]) && string_equal(fields[4], source[5]) &&
                 string_equal(fields[5], source[8]) && string_equal(fields[6], source[7]) &&
                 string_equal(fields[7], source[9]) && string_equal(fields[9], fields[8]) &&
                 string_equal(fields[13], S8("0")) && string_equal(fields[14], S8("0"));
        u32 classification = ok ? bq_retirement_validator_class(fields[8]) : 0;
        ok = ok && classification >= 1 && classification <= 5;
        u32 subject_index = raw_index / BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT;
        BqRetirementApplicabilityLedgerRecord const* auth = NULL;
        if (ok && subject_index < subject_count)
            auth = bq_retirement_validator_ledger_find(ledger, ledger_count, source[2], source[3]);
        bool supported_gap = supported_gap_rows[raw_index] != 0;
        bool baseline_unresolved = direct_reference_failures[raw_index / 4u * 4u] != 0;
        bool supplement_resolved = direct_reference_failures[raw_index] != 0;
        bool acceptance_failure = classification == 5 && !supplement_resolved;
        bool inapplicable = string_equal(source[14], S8("unavailable-platform-control")) ||
            (auth && string_equal(auth->classification, S8("platform-inapplicable")));
        ok = ok && bq_retirement_validator_classification_expected(raw, auth, supported_gap,
            baseline_unresolved, supplement_resolved, classification, fields[11], fields[12]) &&
             (direct_reference_failures[raw_index] != 0) ==
                 (baseline_unresolved && !observed_skips[raw_index]) &&
             string_equal(fields[15], acceptance_failure ? S8("1") : S8("0"));
        bool source_control = string_equal(source[12], S8("registered-non-object-control"));
        bool authenticated_skip = source_control ||
            (auth && (string_equal(auth->classification, S8("platform-inapplicable")) ||
                      string_equal(auth->classification, S8("unavailable"))));
        ok = ok && subject_index < subject_count &&
             string_equal(source[2], string_from_pointer(subjects[subject_index].path)) &&
             string_equal(source[12], string_from_pointer(subjects[subject_index].compile_obligation)) &&
             observed_skips[raw_index] == authenticated_skip;
        if (ok)
        {
            app_counts[classification - 1] += 1;
            projection->classification[raw_index] = (u8)classification;
            acceptance_failures[raw_index] = acceptance_failure;
            inapplicable_rows[raw_index] = inapplicable;
            acceptance_failure_count += acceptance_failure;
            app_rows += ok;
        }
    }
    for (u32 index = 0; ok && index < 5; index += 1)
        ok = app_counts[index] == report_counts[index] && report_counts[index] == admission_counts[index];
    ok = ok && app_rows == row_count && offset == applicability.length;
    if (ok) ok = bq_retirement_validator_class_rows(report, S8("applicability_rows_by_class"),
                                                      row_count, projection->classification) &&
                 bq_retirement_validator_class_rows(report, S8("admission_rows_by_class"),
                                                      row_count, projection->classification) &&
                 bq_retirement_validator_json_rows_match_flags(report, S8("candidate_failure_rows"),
                                                               row_count, candidate_failures) &&
                 bq_retirement_validator_json_rows_match_flags(report, S8("reference_failure_rows"),
                                                               row_count, reference_failures) &&
                 bq_retirement_validator_json_rows_match_flags(report, S8("acceptance_failure_rows"),
                                                               row_count, acceptance_failures) &&
                 bq_retirement_validator_json_rows_match_flags(report, S8("inapplicable_rows"),
                                                               row_count, inapplicable_rows) &&
                 clean_acceptance == (acceptance_failure_count == 0) &&
                 (!require_clean_acceptance || clean_acceptance);
    u64 skips_offset = 0;
    if (ok) ok = bq_next_line(skips_text, &skips_offset, &line) && string_equal(line, skips_header);
    u32 skipped = 0;
    for (u32 index = 0; ok && index < row_count; index += 1)
    {
        if (observed_skips[index])
        {
            String8 fields[7] = {0};
            ok = bq_next_line(skips_text, &skips_offset, &line) &&
                 bq_retirement_validator_tsv_fields(line, fields, BUSTER_ARRAY_LENGTH(fields));
            u64 row_number = 0, group_number = 0;
            BqRetirementValidatorRawRow const* raw = raw_rows + index;
            String8 expected_class = {0}, expected_reason = {0};
            if (string_equal(raw->fields[12], S8("registered-non-object-control")))
            {
                expected_class = S8("retained-control");
                expected_reason = S8("registered-non-object-control");
            }
            else
            {
                BqRetirementApplicabilityLedgerRecord const* auth =
                    bq_retirement_validator_ledger_find(ledger, ledger_count, raw->fields[2], raw->fields[3]);
                if (auth)
                {
                    expected_class = auth->classification;
                    expected_reason = auth->reason;
                }
                else ok = false;
            }
            ok = ok && bq_retirement_number(fields[0], &row_number) && row_number == index &&
                 bq_retirement_number(fields[1], &group_number) && group_number == index / 4u &&
                 string_equal(fields[2], raw->fields[2]) && string_equal(fields[3], raw->fields[3]) &&
                 string_equal(fields[4], raw->fields[7]) && string_equal(fields[5], expected_class) &&
                 string_equal(fields[6], expected_reason);
            if (ok)
            {
                Sha256 hash;
                sha256_init(&hash);
                static char const domain[] = "bq-retirement-validator-skip-v1";
                sha256_add(&hash, domain, sizeof(domain) - 1);
                sha256_add(&hash, "\0", 1);
                sha256_add(&hash, report_sha256.pointer, report_sha256.length);
                sha256_add(&hash, "\0", 1);
                sha256_add(&hash, line.pointer, line.length);
                sha256_finish_hex(&hash, (char8*)projection->skip_proof_sha256[index]);
                projection->compiler_eligible[index] = 0;
                skipped += 1;
            }
        }
        else projection->compiler_eligible[index] = 1;
    }
    ok = ok && skipped == report_skip_count && skips_offset == skips_text.length;
    if (!ok && projection)
    {
        free(projection->compiler_eligible);
        free(projection->classification);
        free(projection->skip_proof_sha256);
        memset(projection, 0, sizeof(*projection));
    }
    else if (ok) projection->row_count = row_count;
    free(observed_skips);
    free(candidate_failures);
    free(reference_failures);
    free(acceptance_failures);
    free(supported_gap_rows);
    free(inapplicable_rows);
    free(direct_reference_failures);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_validator_eligibility_projection(int support_file,
    int source_applicability_ledger_file, int inputs_file, int rows_file, int manifest_file,
    int report_file, int applicability_file, int skips_file,
    String8 profile, BqRetirementValidatorEligibility* projection)
{
    static String8 const report_classes[5] = {
        S8_INITIALIZER("admitted-supported"), S8_INITIALIZER("retained-control"),
        S8_INITIALIZER("retained-reference"), S8_INITIALIZER("platform-inapplicable"),
        S8_INITIALIZER("unavailable")
    };
    u8 *support_bytes = NULL, *source_ledger_bytes = NULL, *inputs_bytes = NULL, *rows_bytes = NULL, *manifest_bytes = NULL;
    u8 *report_bytes = NULL, *applicability_bytes = NULL, *skips_bytes = NULL;
    u64 support_length = 0, source_ledger_length = 0, inputs_length = 0, rows_length = 0, manifest_length = 0;
    u64 report_length = 0, applicability_length = 0, skips_length = 0;
    char support_sha[SHA256_HEX_CAPACITY] = {0}, inputs_sha[SHA256_HEX_CAPACITY] = {0};
    char source_ledger_sha[SHA256_HEX_CAPACITY] = {0};
    char rows_sha[SHA256_HEX_CAPACITY] = {0}, manifest_sha[SHA256_HEX_CAPACITY] = {0};
    char report_sha[SHA256_HEX_CAPACITY] = {0}, applicability_sha[SHA256_HEX_CAPACITY] = {0};
    char skips_sha[SHA256_HEX_CAPACITY] = {0}, input_identity[SHA256_HEX_CAPACITY] = {0};
    char row_identity[SHA256_HEX_CAPACITY] = {0}, manifest_identity[SHA256_HEX_CAPACITY] = {0};
    bool ok = projection != NULL;
    if (projection) memset(projection, 0, sizeof(*projection));
    ok = ok && bq_retirement_validator_read_pinned(support_file, profile,
            S8("support-declaration-sha256="), BQ_RETIREMENT_SUPPORT_BYTES_CAP,
            &support_bytes, &support_length, support_sha) &&
         bq_retirement_validator_read_pinned(source_applicability_ledger_file, profile,
            S8("validator-source-applicability-sha256="), BQ_RETIREMENT_VALIDATOR_LEDGER_BYTES_CAP,
            &source_ledger_bytes, &source_ledger_length, source_ledger_sha) &&
         bq_retirement_validator_read_pinned(inputs_file, profile,
            S8("census-inputs-sha256="), BQ_RETIREMENT_CENSUS_INPUTS_BYTES_CAP,
            &inputs_bytes, &inputs_length, inputs_sha) &&
         bq_retirement_validator_read_pinned(rows_file, profile,
            S8("census-rows-sha256="), BQ_RETIREMENT_CENSUS_ROWS_BYTES_CAP,
            &rows_bytes, &rows_length, rows_sha) &&
         bq_retirement_validator_read_pinned(manifest_file, profile,
            S8("census-manifest-sha256="), BQ_RETIREMENT_MANIFEST_BYTES_CAP,
            &manifest_bytes, &manifest_length, manifest_sha) &&
         bq_retirement_validator_read_pinned(report_file, profile,
            S8("validator-report-sha256="), BQ_RETIREMENT_VALIDATOR_REPORT_BYTES_CAP,
            &report_bytes, &report_length, report_sha) &&
         bq_retirement_validator_read_pinned(applicability_file, profile,
            S8("validator-applicability-sha256="), BQ_RETIREMENT_VALIDATOR_APPLICABILITY_BYTES_CAP,
            &applicability_bytes, &applicability_length, applicability_sha) &&
         bq_retirement_validator_read_pinned(skips_file, profile,
            S8("validator-skips-sha256="), BQ_RETIREMENT_VALIDATOR_SKIPS_BYTES_CAP,
            &skips_bytes, &skips_length, skips_sha);
    String8 support_text = {(char8*)support_bytes, support_length};
    String8 source_ledger_text = {(char8*)source_ledger_bytes, source_ledger_length};
    String8 inputs_text = {(char8*)inputs_bytes, inputs_length};
    String8 rows_text = {(char8*)rows_bytes, rows_length};
    String8 manifest_text = {(char8*)manifest_bytes, manifest_length};
    String8 report_text = {(char8*)report_bytes, report_length};
    String8 applicability_text = {(char8*)applicability_bytes, applicability_length};
    String8 skips_text = {(char8*)skips_bytes, skips_length};
    BqRetirementManifestProperty properties[128] = {0};
    u32 property_count = 0, row_count = 0, input_count = 0, subject_count = 0, ledger_count = 0;
    u64 manifest_rows = 0, manifest_inputs = 0, manifest_shards = 0, manifest_ledger_entries = 0;
    String8 manifest_profile = {0}, report_profile = {0};
    BqRetirementSupportSubject* subjects = ok ? calloc(BQ_RETIREMENT_INVENTORY_CAP, sizeof(*subjects)) : NULL;
    BqRetirementValidatorRawRow* raw_rows = NULL;
    BqRetirementApplicabilityLedgerRecord* ledger = ok ?
        calloc(BQ_RETIREMENT_VALIDATOR_LEDGER_RECORD_CAP, sizeof(*ledger)) : NULL;
    ok = ok && subjects != NULL && bq_retirement_manifest_projection(manifest_text, properties,
        &property_count, manifest_identity);
    BqRetirementManifestProperty* profile_property = bq_retirement_manifest_find(
        properties, property_count, S8("profile"));
    BqRetirementManifestProperty* rows_property = bq_retirement_manifest_find(
        properties, property_count, S8("rows"));
    BqRetirementManifestProperty* inputs_property = bq_retirement_manifest_find(
        properties, property_count, S8("inputs"));
    BqRetirementManifestProperty* shards_property = bq_retirement_manifest_find(
        properties, property_count, S8("shard_count"));
    BqRetirementManifestProperty* manifest_support = bq_retirement_manifest_find(
        properties, property_count, S8("support_contract_sha256"));
    BqRetirementManifestProperty* manifest_compiler = bq_retirement_manifest_find(
        properties, property_count, S8("compiler_sha256"));
    BqRetirementManifestProperty* manifest_baseline = bq_retirement_manifest_find(
        properties, property_count, S8("baseline_sha256"));
    BqRetirementManifestProperty* manifest_ledger_path = bq_retirement_manifest_find(
        properties, property_count, S8("applicability_ledger"));
    BqRetirementManifestProperty* manifest_ledger_sha = bq_retirement_manifest_find(
        properties, property_count, S8("applicability_ledger_sha256"));
    BqRetirementManifestProperty* manifest_ledger_entry_property = bq_retirement_manifest_find(
        properties, property_count, S8("applicability_ledger_entries"));
    BqRetirementManifestProperty* manifest_gap_path = bq_retirement_manifest_find(
        properties, property_count, S8("supported_gap_ledger"));
    BqRetirementManifestProperty* manifest_gap_sha = bq_retirement_manifest_find(
        properties, property_count, S8("supported_gap_ledger_sha256"));
    BqRetirementManifestProperty* manifest_gap_count = bq_retirement_manifest_find(
        properties, property_count, S8("supported_gap_count"));
    BqRetirementManifestProperty* manifest_gap_rows_sha = bq_retirement_manifest_find(
        properties, property_count, S8("supported_gap_sha256"));
    u64 manifest_supported_gap_count = 0;
    ok = ok && profile_property && rows_property && inputs_property && shards_property &&
         manifest_support && manifest_compiler && manifest_baseline && manifest_ledger_path &&
         manifest_ledger_sha && manifest_ledger_entry_property && manifest_gap_path && manifest_gap_sha &&
         manifest_gap_count && manifest_gap_rows_sha && ledger &&
         bq_retirement_number(rows_property->value, &manifest_rows) &&
         bq_retirement_number(inputs_property->value, &manifest_inputs) &&
         bq_retirement_number(shards_property->value, &manifest_shards) &&
         bq_retirement_number(manifest_ledger_entry_property->value, &manifest_ledger_entries) &&
         bq_retirement_number(manifest_gap_count->value, &manifest_supported_gap_count) &&
         manifest_rows > 0 && manifest_rows <= BQ_RETIREMENT_CORRECTNESS_ROWS_CAP &&
         manifest_rows % BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT == 0 &&
         manifest_inputs > 0 && manifest_inputs <= BQ_RETIREMENT_INVENTORY_CAP && manifest_shards > 0 &&
         manifest_ledger_entries <= BQ_RETIREMENT_VALIDATOR_LEDGER_RECORD_CAP &&
         string_equal(manifest_ledger_path->value, S8("docs/native-retirement-applicability-v1.tsv")) &&
         bq_retirement_hex(manifest_ledger_sha->value, 64) &&
         string_equal(manifest_ledger_sha->value, string_from_pointer(source_ledger_sha)) &&
         string_equal(manifest_gap_path->value, S8("docs/native-retirement-supported-gaps-v1.tsv")) &&
         bq_retirement_hex(manifest_gap_sha->value, 64) &&
         bq_retirement_hex(manifest_gap_rows_sha->value, 64);
    if (ok && string_equal(profile_property->value, S8("full-census")))
        ok = string_equal(manifest_gap_sha->value,
                          string_from_pointer(bq_retirement_full_supported_gap_ledger_sha256)) &&
             manifest_supported_gap_count == BQ_RETIREMENT_FULL_SUPPORTED_GAP_COUNT &&
             string_equal(manifest_gap_rows_sha->value,
                          string_from_pointer(bq_retirement_full_supported_gap_sha256));
    else if (ok && string_equal(profile_property->value, S8("self-test")))
        ok = manifest_supported_gap_count == 0;
    if (ok)
    {
        manifest_profile = profile_property->value;
        row_count = (u32)manifest_rows;
        ok = bq_retirement_validator_rows_digest(rows_text, row_count, row_identity, &raw_rows) &&
             bq_retirement_validator_support_inputs_join(support_text, inputs_text, input_identity,
                subjects, BQ_RETIREMENT_INVENTORY_CAP, &subject_count, &input_count) &&
             input_count == manifest_inputs && subject_count > 0 &&
             row_count == subject_count * BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT &&
             bq_retirement_validator_applicability_ledger(source_ledger_text, source_ledger_sha,
                manifest_profile, subjects, subject_count, ledger, BQ_RETIREMENT_VALIDATOR_LEDGER_RECORD_CAP,
                &ledger_count) && ledger_count == manifest_ledger_entries &&
             bq_retirement_hex(manifest_support->value, 64) &&
             string_equal(manifest_support->value, string_from_pointer(support_sha)) &&
             bq_retirement_hex(manifest_compiler->value, 64) &&
             bq_retirement_hex(manifest_baseline->value, 64) &&
             bq_retirement_validator_json_string_value(report_text, S8("profile"), &report_profile) &&
             string_equal(report_profile, manifest_profile) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("schema"), 2) &&
             bq_retirement_validator_json_string_array_equals(report_text, S8("applicability_classes"),
                                                               report_classes, BUSTER_ARRAY_LENGTH(report_classes)) &&
             bq_retirement_validator_json_string_array_equals(report_text, S8("admission_classes"),
                                                               report_classes, BUSTER_ARRAY_LENGTH(report_classes)) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("rows_validated"), row_count) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("groups"), row_count / 4u) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("applicability_rows"), row_count) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("shards"), manifest_shards) &&
             bq_retirement_validator_json_matches_string(report_text, S8("compiler_sha256"),
                                                          manifest_compiler->value) &&
             bq_retirement_validator_json_matches_string(report_text, S8("baseline_sha256"),
                                                          manifest_baseline->value) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("support_contract_sha256"), support_sha) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("manifest_identity_sha256"), manifest_identity) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("rows_identity_sha256"), row_identity) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("input_ledger_sha256"), input_identity) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("applicability_ledger_sha256"), source_ledger_sha) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("applicability_ledger_entries"), ledger_count) &&
             bq_retirement_validator_json_matches_string(report_text, S8("supported_gap_ledger_sha256"),
                                                         manifest_gap_sha->value) &&
             bq_retirement_validator_json_report_scalar(report_text, S8("supported_gap_count"),
                                                         manifest_supported_gap_count) &&
             bq_retirement_validator_json_matches_string(report_text, S8("supported_gap_sha256"),
                                                         manifest_gap_rows_sha->value) &&
             bq_retirement_validator_json_matches_hex(report_text, S8("applicability_sha256"), applicability_sha);
        for (u32 index = 0; ok && index < row_count; index += 1)
            ok = bq_retirement_census_row_projection(raw_rows[index].fields, index, subjects, subject_count);
        if (ok && string_equal(manifest_profile, S8("full-census"))) ok = manifest_shards == 4u;
        bool complete = false, unique = false, require_clean = false, clean = false;
        if (ok) ok = bq_retirement_validator_json_bool(report_text, S8("complete_row_partition"), &complete) &&
                     bq_retirement_validator_json_bool(report_text, S8("global_identity_unique"), &unique) &&
                     bq_retirement_validator_json_bool(report_text, S8("require_clean_candidate"), &require_clean) &&
                     bq_retirement_validator_json_bool(report_text, S8("clean_candidate"), &clean) &&
                     complete && unique && require_clean && clean &&
                     bq_retirement_validator_json_empty_array(report_text, S8("candidate_failure_rows"));
        if (ok)
        {
            projection->compiler_eligible = calloc(row_count, 1);
            projection->classification = calloc(row_count, 1);
            projection->skip_proof_sha256 = calloc(row_count, sizeof(*projection->skip_proof_sha256));
            ok = projection->compiler_eligible && projection->classification &&
                 projection->skip_proof_sha256 &&
                 bq_retirement_validator_applicability_projection(report_text,
                    string_from_pointer(report_sha), manifest_profile, applicability_text, skips_text,
                    raw_rows, subjects, subject_count, ledger, ledger_count, row_count, projection);
        }
        if (ok)
        {
            memcpy(projection->compiler_sha256, manifest_compiler->value.pointer, 64);
            projection->compiler_sha256[64] = 0;
            memcpy(projection->baseline_sha256, manifest_baseline->value.pointer, 64);
            projection->baseline_sha256[64] = 0;
            ok = manifest_profile.length < sizeof(projection->profile);
            if (ok)
            {
                memcpy(projection->profile, manifest_profile.pointer, (size_t)manifest_profile.length);
                projection->profile[manifest_profile.length] = 0;
            }
            Sha256 evidence;
            sha256_init(&evidence);
            static char const domain[] = "bq-retirement-validator-projection-v1";
            sha256_add(&evidence, domain, sizeof(domain) - 1);
            char const* hashes[] = {support_sha, source_ledger_sha, inputs_sha, rows_sha, manifest_sha,
                                    report_sha, applicability_sha, skips_sha};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(hashes); index += 1)
            {
                sha256_add(&evidence, hashes[index], 64);
                sha256_add(&evidence, "\0", 1);
            }
            if (ok) sha256_finish_hex(&evidence, (char8*)projection->evidence_sha256);
        }
    }
    free(subjects);
    free(ledger);
    free(raw_rows);
    free(support_bytes);
    free(source_ledger_bytes);
    free(inputs_bytes);
    free(rows_bytes);
    free(manifest_bytes);
    free(report_bytes);
    free(applicability_bytes);
    free(skips_bytes);
    if (!ok && projection)
    {
        free(projection->compiler_eligible);
        free(projection->classification);
        free(projection->skip_proof_sha256);
        memset(projection, 0, sizeof(*projection));
    }
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
    int installed, int workspaces, int support_declaration, int source_applicability_ledger,
    int census_inputs, int census_rows,
    int census_manifest, int validator_report, int validator_applicability, int validator_skips,
    String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    (void)checks;
    (void)check_count;
    (void)check_facts;
    (void)facts;
    (void)identity_workspace;
    (void)identity_slots;
    (void)census_workspace;
    (void)census_slots;
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished &&
        held && !held->owned;
    BqRetirementPreparation preparation = {0};
    BqRetirementMatchedBuild build = {0};
    BqRetirementBinaries binaries = {0};
    BqError a_result = fresh && prepared ?
        bq_retirement_preparation_import(queue, job, installed, workspaces,
            preparation_sha256, &preparation) : BQ_RECIPE_MISMATCH;
    if (a_result == BQ_OK)
        a_result = bq_retirement_matched_build_import(queue, job, installed, workspaces,
            workspace_root, preparation_sha256, record_sha256, build_record_sha256, &build);
    if (a_result == BQ_OK)
        a_result = bq_retirement_binaries_import(queue, job, installed, workspaces,
            preparation_sha256, record_sha256, &binaries);
    bool joined_a = a_result == BQ_OK &&
        !memcmp(prepared->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY) &&
        !memcmp(prepared->preparation_sha256, build.preparation_sha256, SHA256_HEX_CAPACITY) &&
        !memcmp(prepared->preparation_sha256, binaries.preparation_sha256, SHA256_HEX_CAPACITY);
    for (u32 side = 0; joined_a && side < 2; side += 1)
        joined_a = !memcmp(prepared->source_sha256[side],
                           preparation.subjects[side].manifest_sha256, SHA256_HEX_CAPACITY) &&
                   !memcmp(prepared->source_sha256[side],
                           build.prepared_source[side].manifest_sha256, SHA256_HEX_CAPACITY) &&
                   !memcmp(prepared->source_sha256[side],
                           binaries.source_sha256[side], SHA256_HEX_CAPACITY) &&
                   !memcmp(prepared->binary_sha256[side],
                           binaries.binary_sha256[side], SHA256_HEX_CAPACITY);
    char census_inputs_sha256[SHA256_HEX_CAPACITY] = {0};
    char census_rows_sha256[SHA256_HEX_CAPACITY] = {0};
    BqRetirementValidatorEligibility eligibility = {0};
    bool authenticated_raw = joined_a && rows &&
        bq_retirement_census_projection(support_declaration, census_inputs, census_rows, profile,
                                        prepared, rows, census_inputs_sha256, census_rows_sha256) &&
        !memcmp(prepared->census_sha256, census_rows_sha256, SHA256_HEX_CAPACITY);
    bool authenticated_eligibility = authenticated_raw &&
        bq_retirement_validator_eligibility_projection(support_declaration, source_applicability_ledger,
            census_inputs, census_rows,
            census_manifest, validator_report, validator_applicability, validator_skips, profile,
            &eligibility) &&
        string_equal(string_from_pointer(eligibility.profile), S8("full-census")) &&
        eligibility.row_count == prepared->object_rows &&
        !memcmp(prepared->binary_sha256[1], eligibility.compiler_sha256, SHA256_HEX_CAPACITY) &&
        !memcmp(prepared->binary_sha256[0], eligibility.baseline_sha256, SHA256_HEX_CAPACITY);
    bool joined = authenticated_eligibility;
    u8* seen = joined ? calloc(prepared->object_rows, 1) : NULL;
    joined = joined && seen != NULL;
    for (u32 index = 0; joined && index < prepared->rows; index += 1)
    {
        BqRetirementTrustedRow const* row = rows + index;
        u32 ordinal = row->census_row;
        joined = ordinal < eligibility.row_count && eligibility.classification[ordinal] >= 1 &&
                 eligibility.classification[ordinal] <= 5;
        if (joined && row->stage == BQ_RETIREMENT_STAGE_OBJECT)
        {
            joined = !seen[ordinal];
            if (joined) seen[ordinal] = 1;
        }
    }
    u32 seen_count = 0;
    for (u32 index = 0; joined && index < prepared->object_rows; index += 1) seen_count += seen[index] != 0;
    joined = joined && seen_count == prepared->object_rows;
    free(seen);
    /* Eligibility is only one part of B's authority. This staged importer
     * does not independently authenticate configuration identities, #509
     * receipt bytes, command plans, or the oracle. A valid projection still
     * fails closed before caller facts can enter the correctness gate. */
    BqError result = !fresh ? BQ_RECIPE_MISMATCH :
                     a_result != BQ_OK ? a_result :
                     joined ? BQ_RECIPE_MISMATCH : BQ_SOURCE_MISMATCH;
    free(eligibility.compiler_eligible);
    free(eligibility.classification);
    free(eligibility.skip_proof_sha256);
    if (result != BQ_OK && fresh) gate->failed = 1;
    return result;
}
