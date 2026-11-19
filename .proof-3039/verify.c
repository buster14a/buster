// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Check the emitted pairs against GNU readelf's independent .eh_frame walk.
// For equal starts the earlier input FDE has the lower record address.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct entry
{
    int32_t start;
    int32_t fde;
};

static int compare(const void *a, const void *b)
{
    const struct entry *left = a;
    const struct entry *right = b;
    int order = (left->start > right->start) - (left->start < right->start);
    if (order == 0)
    {
        order = (left->fde > right->fde) - (left->fde < right->fde);
    }
    return order;
}

int main(int argc, char **argv)
{
    FILE *header = argc == 5 ? fopen(argv[1], "rb") : NULL;
    FILE *frames = argc == 5 ? fopen(argv[2], "r") : NULL;
    const int64_t header_address = argc == 5 ? strtoll(argv[3], NULL, 16) : 0;
    const int64_t frame_address = argc == 5 ? strtoll(argv[4], NULL, 16) : 0;
    const unsigned char encoding[] = { 1, 0x1b, 3, 0x3b };
    unsigned char actual[4];
    int32_t pointer = 0;
    uint32_t count = 0;
    int ok = header != NULL && frames != NULL && fread(actual, 1, 4, header) == 4 &&
             memcmp(encoding, actual, 4) == 0 && fread(&pointer, 4, 1, header) == 1 &&
             header_address + 4 + pointer == frame_address && fread(&count, 4, 1, header) == 1;
    struct entry *entries = ok ? calloc(count ? count : 1, sizeof(*entries)) : NULL;
    ok = entries != NULL;
    char line[1024];
    uint32_t records = 0;
    while (ok && fgets(line, sizeof(line), frames) != NULL)
    {
        if (strstr(line, " FDE cie=") != NULL)
        {
            const char *pc = strstr(line, " pc=");
            const int64_t fde = frame_address + strtoll(line, NULL, 16) - header_address;
            const int64_t start = pc != NULL ? strtoll(pc + 4, NULL, 16) - header_address : INT64_MAX;
            ok = records < count && fde >= INT32_MIN && fde <= INT32_MAX && start >= INT32_MIN && start <= INT32_MAX;
            if (ok)
            {
                entries[records++] = (struct entry){ .start = (int32_t)start, .fde = (int32_t)fde };
            }
        }
    }
    if (ok)
    {
        ok = !ferror(frames) && count == records;
        qsort(entries, records, sizeof(*entries), compare);
    }
    for (uint32_t i = 0; ok && i < records; i += 1)
    {
        int32_t pair[2];
        ok = fread(pair, sizeof(pair), 1, header) == 1 && pair[0] == entries[i].start && pair[1] == entries[i].fde;
    }
    ok = ok && fgetc(header) == EOF && !ferror(header);
    if (!ok)
    {
        fprintf(stderr, "header and readelf disagree: header has %" PRIu32 " entries, read %" PRIu32 " records\n", count, records);
    }
    free(entries);
    if (frames != NULL) fclose(frames);
    if (header != NULL) fclose(header);
    return ok ? 0 : 1;
}
