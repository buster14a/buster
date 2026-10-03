// Research-only #313 source generator. No compiler or allocator code is copied.
// Each file preserves logical inputs/outputs while varying source/value order.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_VALUES 4097u

static int write_source(char const* directory, unsigned shape)
{
    char const* names[] = {"late-hot", "early-hot", "equal-forward", "equal-reverse"};
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s.c", directory, names[shape]);
    FILE* output = length > 0 && (size_t)length < sizeof(path) ? fopen(path, "wb") : NULL;
    int ok = output != NULL;
    if (output)
    {
        fputs("/* Frozen #313 stress shape; unsigned arithmetic, initialized host oracle. */\n"
              "void quality313_probe(volatile unsigned long long const* input, volatile unsigned long long* output)\n{\n", output);
        for (unsigned position = 0; position < PROBE_VALUES; position += 1)
        {
            unsigned value = shape == 1 ? (position ? position - 1 : PROBE_VALUES - 1) :
                shape == 3 ? PROBE_VALUES - position - 1 : position;
            int hot = shape < 2 && value == PROBE_VALUES - 1;
            fprintf(output, "    { unsigned long long value = input[%u] ^ (%uULL * 7ULL + 123456789ULL);\n", value % 16, value);
            for (unsigned pass = 0; pass < (hot ? 10u : 2u); pass += 1)
            {
                // Empty assembly has declared physical effects but emits no
                // instructions. Cold rows foreclose the whole allocatable file;
                // the hot row leaves the callee-saved pin file untouched.
                fprintf(output, "      __asm__ volatile(\"\" : : : \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\","
                    "\"r8\",\"r9\",\"r10\",\"r11\"%s,\"cc\",\"memory\");\n"
                    "      output[%u] = value;\n", hot ? "" : ",\"rbx\",\"r12\",\"r13\",\"r14\",\"r15\"", value);
            }
            fputs("    }\n", output);
        }
        fputs("}\n", output);
        ok = !ferror(output);
        ok = fclose(output) == 0 && ok;
    }
    return ok;
}

int main(int argc, char** argv)
{
    int ok = argc == 2;
    if (ok)
    {
        for (unsigned shape = 0; shape < 4 && ok; shape += 1) ok = write_source(argv[1], shape);
    }
    if (!ok) fprintf(stderr, "quality313 generate requires an existing output directory and writable files\n");
    return ok ? 0 : 1;
}
