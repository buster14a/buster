/* Service-side A -> B entry for #881.
 * begin_service_pinned is the deterministic test seam for the compiled
 * profile; production uses bq_recipe_profile. Acquire replays the durable A
 * and binary records and holds both exact executable inodes. Production also
 * checks the separately profile-pinned #508 inputs.tsv and rows.tsv, including
 * source-ledger joins and canonical row identity. Clang provenance and
 * validator-derived #508/#509 facts still require independent importer replay.
 */
#include "retirement_correctness_service.h"
#include <stdlib.h>
#include <string.h>

#define BQ_RETIREMENT_SUPPORT_BYTES_CAP (128u * 1024u)
#define BQ_RETIREMENT_CENSUS_INPUTS_BYTES_CAP (1024u * 1024u)
#define BQ_RETIREMENT_CENSUS_ROWS_BYTES_CAP (64u * 1024u * 1024u)
#define BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT 4u
#define BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT (12u * 2u * 2u * 4u)

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
    int installed, int workspaces, int support_declaration, int census_inputs, int census_rows,
    String8 workspace_root,
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
    char census_inputs_sha256[SHA256_HEX_CAPACITY] = {0};
    char census_rows_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError result = !fresh ? BQ_RECIPE_MISMATCH :
        rows && bq_retirement_census_projection(support_declaration, census_inputs, census_rows, profile,
                                                 prepared, rows, census_inputs_sha256,
                                                 census_rows_sha256) &&
        !memcmp(prepared->census_sha256, census_rows_sha256, SHA256_HEX_CAPACITY) ?
        bq_retirement_correctness_begin_service_built_pinned(queue, job,
            installed, workspaces, workspace_root, profile, BQ_RETIREMENT_BUILD_DRIVER,
            BQ_RETIREMENT_TOOLCHAIN_ROOT, preparation_sha256, record_sha256, build_record_sha256,
            prepared, rows, checks, check_count, check_facts, facts,
            identity_workspace, identity_slots, census_workspace, census_slots, held, gate) :
        BQ_SOURCE_MISMATCH;
    if (result != BQ_OK && fresh) gate->failed = 1;
    return result;
}
