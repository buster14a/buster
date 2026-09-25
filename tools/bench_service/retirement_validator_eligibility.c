/* Standalone exercise of the B-owned #508 schema-2 eligibility projection.
 * The Python fixture creates genuine validator evidence and supplies its
 * temporary profile pins; this probe never synthesizes classifications. */
#define main bq_service_cli_main
#include "main.c"
#undef main
#include "retirement_correctness.c"
#include "retirement_correctness_service.c"
#include <stdlib.h>

int main(int argc, char** argv)
{
    bool ok = argc == 10;
    int descriptors[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int profile_file = -1;
    u8* profile_bytes = NULL;
    u64 profile_length = 0;
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
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(descriptors); index += 1)
        {
            descriptors[index] = open(argv[index + 2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            ok = descriptors[index] >= 3;
        }
    }
    BqRetirementValidatorEligibility projection = {0};
    if (ok)
    {
        String8 profile = {(char8*)profile_bytes, profile_length};
        ok = bq_retirement_validator_eligibility_projection(descriptors[0], descriptors[1],
            descriptors[2], descriptors[3], descriptors[4], descriptors[5], descriptors[6], descriptors[7],
            profile, &projection);
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
    }
    else fprintf(stderr, "VALIDATOR_ELIGIBILITY rejected\n");
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(descriptors); index += 1)
        if (descriptors[index] >= 0) close(descriptors[index]);
    if (profile_file >= 0) close(profile_file);
    free(profile_bytes);
    free(projection.compiler_eligible);
    free(projection.classification);
    free(projection.skip_proof_sha256);
    int result = ok ? 0 : 1;
    return result;
}
