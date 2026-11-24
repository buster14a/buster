// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Make one deliberately regressed source copy for #2041's standalone work contracts. Each mutation must match
// exactly once: a source change that invalidates the experiment fails instead of silently testing the original code.
// The input is read completely before the output is opened. A temporary sibling is renamed only after a complete
// write and close, so a failed mutation never publishes a partial output or alters an existing output.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct WorkMutation WorkMutation;
struct WorkMutation
{
    const char* kind;
    const char* before;
    const char* after;
};

static const WorkMutation work_mutations[] = {
    {
        .kind = "dependency-linear",
        .before =
            "            uint64_t first = 0;\n"
            "            uint64_t end = index->counts[key];\n"
            "            while (!result && first < end)\n"
            "            {\n"
            "                const uint64_t middle = first + (end - first) / 2;\n"
            "                const int order = dependency_identity_order(entries[middle], identity, (DependencyIdentityKey)key, comparisons);\n"
            "                result = order == 0;\n"
            "                if (order < 0)\n"
            "                {\n"
            "                    first = middle + 1;\n"
            "                }\n"
            "                else\n"
            "                {\n"
            "                    end = middle;\n"
            "                }\n"
            "            }\n",
        .after =
            "            for (uint64_t candidate = 0; !result && candidate < index->counts[key]; candidate += 1)\n"
            "            {\n"
            "                const int order = dependency_identity_order(entries[candidate], identity, (DependencyIdentityKey)key, comparisons);\n"
            "                result = order == 0;\n"
            "            }\n",
    },
    {
        .kind = "dependency-omit",
        .before = "                result = !dependency_index_overlaps(&index, &target, comparisons);\n",
        .after = "                result = true;\n",
    },
    {
        // The dense construction forced, with its rows in one group however wide (#3449's buffer lifted): the blocks
        // times locals tables #1353 bounded.
        .kind = "ssa-dense",
        .before = "        bounded |= !ssa_dense_fits(input);\n",
        .after = "        bounded = false;\n        row_bytes = UINT32_MAX;\n",
    },
};

static char* work_mutation_read(const char* path, size_t* length)
{
    char* result = NULL;
    FILE* stream = fopen(path, "rb");
    if (stream != NULL)
    {
        bool valid = fseeko(stream, 0, SEEK_END) == 0;
        const off_t size = valid ? ftello(stream) : -1;
        valid = valid && size >= 0 && (uintmax_t)size < SIZE_MAX;
        if (valid)
        {
            valid = fseeko(stream, 0, SEEK_SET) == 0;
        }
        if (valid)
        {
            result = malloc((size_t)size + 1);
            valid = result != NULL;
        }
        if (valid)
        {
            const size_t read = fread(result, 1, (size_t)size, stream);
            valid = read == (size_t)size && fgetc(stream) == EOF && !ferror(stream);
            if (valid)
            {
                valid = memchr(result, 0, read) == NULL;
                result[read] = 0;
                *length = read;
            }
        }
        const bool closed = fclose(stream) == 0;
        if (!valid || !closed)
        {
            free(result);
            result = NULL;
        }
    }
    if (result == NULL)
    {
        fprintf(stderr, "work-contracts-mutate: cannot read complete NUL-free source: %s\n", path);
    }
    return result;
}

static bool work_mutation_write(const char* path, const char* input, size_t length, const char* match, const WorkMutation* mutation)
{
    static const char suffix[] = ".work-contract.XXXXXX";
    const size_t path_length = strlen(path);
    char* temporary = path_length <= SIZE_MAX - sizeof(suffix) ? malloc(path_length + sizeof(suffix)) : NULL;
    bool result = temporary != NULL;
    bool created = false;
    if (result)
    {
        memcpy(temporary, path, path_length);
        memcpy(temporary + path_length, suffix, sizeof(suffix));
        const int descriptor = mkstemp(temporary);
        created = descriptor >= 0;
        result = created;
        if (created)
        {
            FILE* stream = fdopen(descriptor, "wb");
            result = stream != NULL;
            if (stream != NULL)
            {
                const size_t prefix = (size_t)(match - input);
                const size_t before = strlen(mutation->before);
                const size_t after = strlen(mutation->after);
                const size_t tail = length - prefix - before;
                result = fwrite(input, 1, prefix, stream) == prefix;
                result = fwrite(mutation->after, 1, after, stream) == after && result;
                result = fwrite(match + before, 1, tail, stream) == tail && result;
                const bool closed = fclose(stream) == 0;
                result = closed && result;
                if (result)
                {
                    result = rename(temporary, path) == 0;
                }
            }
            else
            {
                close(descriptor);
            }
        }
    }
    if (!result)
    {
        fprintf(stderr, "work-contracts-mutate: cannot publish complete mutation: %s\n", path);
    }
    if (created)
    {
        unlink(temporary);
    }
    free(temporary);
    return result;
}

int main(int argc, char** argv)
{
    int result = 1;
    const WorkMutation* mutation = NULL;
    if (argc == 4)
    {
        for (size_t i = 0; i < sizeof(work_mutations) / sizeof(work_mutations[0]); i += 1)
        {
            if (strcmp(argv[1], work_mutations[i].kind) == 0)
            {
                mutation = &work_mutations[i];
            }
        }
    }
    if (mutation == NULL)
    {
        fprintf(stderr, "usage: work-contracts-mutate {dependency-linear|dependency-omit|ssa-dense} INPUT OUTPUT\n");
    }
    else
    {
        size_t length = 0;
        char* input = work_mutation_read(argv[2], &length);
        if (input != NULL)
        {
            const char* match = strstr(input, mutation->before);
            const bool unique = match != NULL && strstr(match + 1, mutation->before) == NULL;
            if (!unique)
            {
                fprintf(stderr, "work-contracts-mutate: %s: expected exactly one match in %s, observed %s\n",
                        mutation->kind, argv[2], match == NULL ? "zero" : "more than one");
            }
            else if (work_mutation_write(argv[3], input, length, match, mutation))
            {
                result = 0;
            }
        }
        free(input);
    }
    return result;
}
