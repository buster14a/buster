/* Standalone exercise of the B-owned #508 schema-2 eligibility projection.
 * The Python fixture creates genuine validator evidence and supplies its
 * temporary profile pins; this probe never synthesizes classifications. It
 * also prints each row's derived #1020 configuration_sha256 for the fixture's
 * cross-language comparison. Given #508's performance-row artifact and a
 * native target (1..12, performance TARGETS order) as two more arguments, it
 * also imports that population (bq_retirement_performance_rows_derive) and
 * prints each imported row for the Python reference comparison. */
#define BQ_RETIREMENT_CORRECTNESS_TEST_ONLY 1
#define main bq_service_cli_main
#include "main.c"
#undef main
#include <stdlib.h>

int main(int argc, char** argv)
{
    BUSTER_UNUSED(bq_retirement_support_projection);
    BUSTER_UNUSED(bq_retirement_correctness_begin_service_built_pinned);
    BUSTER_UNUSED(bq_retirement_reference_policy_import_pinned);
    BUSTER_UNUSED(bq_retirement_unit_gate_fixture_admit);
    bool population = argc == 12;
    bool ok = argc == 10 || population;
    int descriptors[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    u32 files = population ? 9u : 8u;
    u64 native_target = 0;
    int profile_file = -1;
    u8* profile_bytes = NULL;
    u64 profile_length = 0;
    if (ok && population) ok = bq_decimal(argv[11], true, &native_target) && native_target <= 12;
    if (ok)
    {
        profile_file = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info = {0};
        ok = profile_file >= 3 && fstat(profile_file, &info) == 0 && S_ISREG(info.st_mode) &&
             info.st_nlink == 1 && info.st_size > 0 && info.st_size < 4096;
        if (ok)
        {
            profile_bytes = malloc((size_t)info.st_size);
            ok = profile_bytes != NULL;
            while (ok && profile_length < (u64)info.st_size)
            {
                ssize_t count = read(profile_file, profile_bytes + profile_length,
                                     (size_t)((u64)info.st_size - profile_length));
                if (count < 0 && errno == EINTR) continue;
                ok = count > 0;
                if (ok) profile_length += (u64)count;
            }
        }
        for (u32 index = 0; ok && index < files; index += 1)
        {
            descriptors[index] = open(argv[index + 2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            ok = descriptors[index] >= 3;
        }
    }
    String8 profile = {(char8*)profile_bytes, profile_length};
    BqRetirementValidatorEligibility projection = {0};
    if (ok)
        ok = bq_retirement_validator_eligibility_projection(descriptors[0], descriptors[1],
            descriptors[2], descriptors[3], descriptors[4], descriptors[5], descriptors[6], descriptors[7],
            profile, population, &projection);
    BqRetirementTrustedRow* rows = NULL;
    u32 row_count = 0;
    if (ok && population)
    {
        u8* bytes = NULL;
        u64 length = 0;
        char digest[SHA256_HEX_CAPACITY] = {0};
        ok = bq_retirement_validator_read_pinned(descriptors[8], profile, S8("performance-rows-sha256="),
                                                 BQ_RETIREMENT_POPULATION_BYTES_CAP, &bytes, &length, digest) &&
             bq_retirement_performance_rows_derive((String8){(char8*)bytes, length}, profile, &projection,
                                                   (u32)native_target, &rows, &row_count);
        free(bytes);
    }
    if (ok)
    {
        u32 eligible = 0, skipped = 0;
        for (u32 index = 0; index < projection.row_count; index += 1)
        {
            eligible += projection.compiler_eligible[index] != 0;
            skipped += projection.compiler_eligible[index] == 0;
        }
        printf("VALIDATOR_ELIGIBILITY rows=%u eligible=%u skipped=%u\n",
               projection.row_count, eligible, skipped);
        /* The fixture compares these with row_configuration_digest. */
        for (u32 index = 0; index < projection.row_count; index += 1)
            printf("VALIDATOR_CONFIGURATION row=%u sha256=%s\n", index, projection.configuration_sha256[index]);
        /* And these with expected_population. */
        for (u32 index = 0; index < row_count; index += 1)
        {
            BqRetirementTrustedRow const* row = rows + index;
            bool runtime = row->compiler_eligible && row->execution_obligation &&
                           row->stage != BQ_RETIREMENT_STAGE_OBJECT && row->target == native_target;
            printf("VALIDATOR_POPULATION row=%u census=%u stage=%u runtime=%u configuration=%s\n", row->row,
                   row->census_row, row->stage, runtime ? 1u : 0u, row->configuration_sha256);
        }
    }
    else fprintf(stderr, "VALIDATOR_ELIGIBILITY rejected\n");
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(descriptors); index += 1)
        if (descriptors[index] >= 0) close(descriptors[index]);
    if (profile_file >= 0) close(profile_file);
    free(profile_bytes);
    free(rows);
    bq_retirement_validator_eligibility_release(&projection);
    int result = ok ? 0 : 1;
    return result;
}
