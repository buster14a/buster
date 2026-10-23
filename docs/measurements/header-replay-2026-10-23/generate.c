// Generate frozen #109 programs as immutable headers for repeated-inclusion checks.
#define main unused_probe_main
#include "probe.c"
#undef main
typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};
#define BUSTER_COMPILE_TIME_STRING_LENGTH(text) (sizeof(text) - 1)
#define BUSTER_S8(text) ((String8){ .pointer = (char*)(text), .length = sizeof(text) - 1 })
#include "buster/compiler/preprocessor_fuzz.c"
#include "buster/compiler/preprocessor_fuzz_generate.c"

static PreprocessorFuzzDifference preprocessor_fuzz_difference(Arena* scratch, const PreprocessorFuzzOracle* oracle, String8 source)
{
    (void)scratch;
    (void)oracle;
    (void)source;
    return BUSTER_PPF_INVALID;
}

#ifndef BUSTER_GENERATE_MAIN
#define BUSTER_GENERATE_MAIN main
#endif
int BUSTER_GENERATE_MAIN(int argc, char** argv)
{
    Arena arena = arena_reserve(8ull << 20);
    bool ok = argc == 2 && arena.base != 0;
    for (uint32_t mode = 0; ok && mode < 2; mode += 1)
    {
        for (uint32_t id = 0; ok && id < 500; id += 1)
        {
            arena_pop_to(&arena, 0);
            PreprocessorFuzzCase generated;
            ok = preprocessor_fuzz_generate(&arena, 111, id, mode != 0, &generated);
            char header[4096];
            char root[4096];
            const char* standard = mode != 0 ? "c23" : "c17";
            ok = ok && snprintf(header, sizeof(header), "%s/%s-%u.h", argv[1], standard, id) < (int)sizeof(header);
            ok = ok && snprintf(root, sizeof(root), "%s/%s-%u.c", argv[1], standard, id) < (int)sizeof(root);
            FILE* stream = ok ? fopen(header, "w") : 0;
            ok = stream != 0 && fwrite(generated.source.pointer, 1, generated.source.length, stream) == generated.source.length;
            if (stream != 0)
            {
                ok &= fclose(stream) == 0;
            }
            stream = ok ? fopen(root, "w") : 0;
            ok = stream != 0 && fprintf(stream, "#include \"%s-%u.h\"\n", standard, id) > 0;
            if (stream != 0)
            {
                ok &= fclose(stream) == 0;
            }
        }
    }
    arena_release(&arena);
    return ok ? 0 : 1;
}
