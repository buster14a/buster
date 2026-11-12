// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Literal include closure, independent of filename suffix. The actual depfile supplies active owner sets.

static bool ownership_fixture_name(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* name = slash != 0 ? slash + 1 : path;
    static const char* markers[] = {
        "_test.",
        "_test_",
        "_benchmark.",
        "_benchmark_",
        "_measure.",
        "_measure_",
        "_fixture.",
        "_fixture_",
    };
    bool fixture = strncmp(name, "test_", 5) == 0 || strncmp(path, "tests/", 6) == 0 || strncmp(path, "fixtures/", 9) == 0 ||
                   strstr(path, "/tests/") != 0 || strstr(path, "/fixtures/") != 0;
    for (uint32_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i += 1)
    {
        fixture = fixture || strstr(name, markers[i]) != 0;
    }
    static const char* special[] = {
        "c_preprocessor_oracle.c",
        "c_testsuite.c",
        "c_check_lower_inventory.c",
        "c_check_node_census.c",
        "differential.c",
        "preprocessor_fuzz.c",
        "preprocessor_fuzz_generate.c",
        "preprocessor_fuzz_clang.c",
    };
    for (uint32_t i = 0; i < sizeof(special) / sizeof(special[0]); i += 1)
    {
        fixture = fixture || strcmp(name, special[i]) == 0;
    }
    return fixture;
}

// Normalize only repository-relative literal include paths; never turn a parent escape into an in-repo path.
static bool ownership_normalize(const char* path, char output[PATH_MAX])
{
    const size_t length = strlen(path);
    size_t at = 0;
    size_t written = 0;
    bool valid = length < PATH_MAX && path[0] != '/';
    while (at < length && valid)
    {
        const size_t bytes = strcspn(path + at, "/");
        if (bytes == 2 && path[at] == '.' && path[at + 1] == '.')
        {
            valid = written != 0;
            while (written != 0 && output[written - 1] != '/')
            {
                written -= 1;
            }
            written -= written != 0;
        }
        else if (bytes != 0 && !(bytes == 1 && path[at] == '.'))
        {
            if (written != 0)
            {
                output[written++] = '/';
            }
            memcpy(output + written, path + at, bytes);
            written += bytes;
        }
        at += bytes + (path[at + bytes] == '/');
    }
    output[written] = 0;
    return valid && written != 0;
}

static void ownership_mark_include(OwnershipGraph* graph, const OwnershipFile* parent, const char* include)
{
    const char* slash = strrchr(parent->path, '/');
    const size_t directory = slash != 0 ? (size_t)(slash - parent->path) + 1 : 0;
    char path[PATH_MAX];
    char normalized[PATH_MAX];
    // Quoted includes search their own directory first, followed by the build's -Isrc. Match both only against
    // paths in the actual active graph: no directory or shared-root exception grants owners to a descendant.
    for (uint32_t search = 0; search < 2; search += 1)
    {
        const int length = search == 0 ? snprintf(path, sizeof(path), "%.*s%s", (int)directory, parent->path, include) :
                                       snprintf(path, sizeof(path), "src/%s", include);
        if (length > 0 && (size_t)length < sizeof(path) && ownership_normalize(path, normalized))
        {
            for (uint32_t i = 0; i < graph->count; i += 1)
            {
                if (strcmp(graph->files[i].path, normalized) == 0)
                {
                    graph->files[i].fixture = true;
                }
            }
        }
    }
}

// Skip comments and literals before accepting an include directive, including continued directives. This is a
// conservative lexical walk: inactive includes can classify an already-active path, but cannot invent owners.
static bool ownership_mark_includes(OwnershipGraph* graph, const OwnershipFile* parent, const char* text)
{
    const char* p = text;
    bool valid = true;
    while (*p != 0 && valid)
    {
        if (p[0] == '/' && p[1] == '/')
        {
            p += strcspn(p, "\n");
        }
        else if (p[0] == '/' && p[1] == '*')
        {
            const char* end = strstr(p + 2, "*/");
            valid = end != 0;
            p = end != 0 ? end + 2 : p + strlen(p);
        }
        else if (*p == '"' || *p == '\'')
        {
            const char quote = *p++;
            while (*p != 0 && *p != quote)
            {
                p += *p == '\\' && p[1] != 0;
                p += 1;
            }
            valid = *p == quote;
            p += *p == quote;
        }
        else if (*p == '#')
        {
            p = ownership_directive_space(p + 1, &valid);
            if (strncmp(p, "include", 7) == 0 && !isalnum((unsigned char)p[7]) && p[7] != '_')
            {
                p = ownership_directive_space(p + 7, &valid);
                if (*p == '"')
                {
                    const char* end = strchr(p + 1, '"');
                    const size_t length = end != 0 ? (size_t)(end - p - 1) : 0;
                    valid = end != 0 && length != 0 && length < PATH_MAX;
                    if (valid)
                    {
                        char include[PATH_MAX];
                        memcpy(include, p + 1, length);
                        include[length] = 0;
                        ownership_mark_include(graph, parent, include);
                        p = end + 1;
                    }
                }
            }
            // Other directives may contain stringification or quoted text. Do not treat their macro bodies as
            // fresh directives; the structural invariant records the macro name separately.
            bool continued = false;
            do
            {
                const size_t bytes = strcspn(p, "\n");
                continued = bytes != 0 && p[bytes - 1] == '\\';
                p += bytes + (p[bytes] != 0);
            } while (continued);
        }
        else
        {
            p += 1;
        }
    }
    return valid;
}
