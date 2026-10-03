// Research-only source slicer. The workflow verifies pinned Git blob identities
// before this tool copies exact helper/interval bytes; no algorithm is vendored.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Slice Slice;
struct Slice { char const* bytes; size_t count; };

static char* read_file(char const* path)
{
    FILE* file = fopen(path, "rb");
    char* result = NULL;
    if (file)
    {
        int ok = fseek(file, 0, SEEK_END) == 0;
        long size = ok ? ftell(file) : -1;
        ok = size > 0 && size <= 2 * 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0;
        if (ok)
        {
            result = malloc((size_t)size + 1);
            ok = result && fread(result, 1, (size_t)size, file) == (size_t)size && !ferror(file);
            if (ok) result[size] = 0;
        }
        ok = fclose(file) == 0 && ok;
        if (!ok) { free(result); result = NULL; }
    }
    return result;
}

static Slice slice_between(char const* text, char const* first, char const* last)
{
    Slice result = {0};
    char const* begin = text ? strstr(text, first) : NULL;
    char const* end = begin ? strstr(begin, last) : NULL;
    if (begin && end && end > begin && !strstr(begin + 1, first))
    {
        result.bytes = begin;
        result.count = (size_t)(end - begin);
    }
    return result;
}

static int write_slice(char const* directory, char const* name, Slice slice)
{
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = slice.count && length > 0 && (size_t)length < sizeof(path) ? fopen(path, "wb") : NULL;
    int ok = file != NULL;
    if (file)
    {
        ok = fwrite(slice.bytes, 1, slice.count, file) == slice.count && !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    return ok;
}

int main(int argc, char** argv)
{
    int ok = argc == 6;
    char* files[4] = {0};
    if (ok)
    {
        for (unsigned index = 0; index < 4; index += 1)
        {
            files[index] = read_file(argv[index + 1]);
            ok = files[index] != NULL && ok;
        }
        Slice baseline = slice_between(files[0], "void machine_quality_heap_sift(", "// Enumerate positive region costs");
        Slice candidate = slice_between(files[1], "BUSTER_GLOBAL_LOCAL bool machine_quality_interval_better(", "// Enumerate positive region costs");
        Slice old_struct = slice_between(files[2], "typedef u64 MachineQualityTraffic;", "BUSTER_UNUSED_DECL");
        Slice new_struct = slice_between(files[3], "typedef u64 MachineQualityTraffic;", "BUSTER_UNUSED_DECL");
        ok = ok && baseline.count && candidate.count && old_struct.count == new_struct.count && old_struct.count &&
             !memcmp(old_struct.bytes, new_struct.bytes, old_struct.count);
        if (ok) ok = write_slice(argv[5], "baseline.inc", baseline) && write_slice(argv[5], "candidate.inc", candidate) &&
                     write_slice(argv[5], "interval.inc", old_struct);
    }
    for (unsigned index = 0; index < 4; index += 1) free(files[index]);
    if (!ok) fprintf(stderr, "quality313 extraction failed: missing/nonunique boundary or changed interval layout\n");
    return ok ? 0 : 1;
}
