// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_preprocessor_test.c. Original regressions for #616's nested argument slices.

#include <sys/resource.h>

// An independent depth walk checks every indexed opener and comma, including unbalanced streams and protected
// tails. Rewinding and rewriting the stack between runs must invalidate its old links before the new scan.
static bool pp_test_argument_links(void)
{
    const uint32_t page = (uint32_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    Preprocessor pp = { 0 };
    uint64_t seed = 616;
    for (uint32_t mode = 0; result && mode < 2; mode += 1)
    {
        pp.vectorized = mode == 0;
        for (uint32_t n = 0; result && n <= 130; n += 1)
        {
            const uint32_t count = n == 130 ? page : n;
            pp.arguments.count = 0;
            pp.arguments.capacity = page;
            result = pp_room(&pp, &pp.arguments, count);
            pp.arguments.kinds = mapping + page - count;
            pp.arguments.count = count;
            for (uint32_t i = 0; i < count; i += 1)
            {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                static const uint8_t kinds[] = { C_TOKEN_RAW_IDENTIFIER, C_TOKEN_L_PAREN, C_TOKEN_R_PAREN, C_TOKEN_COMMA };
                pp.arguments.kinds[i] = (uint8_t)(kinds[seed >> 62] | ((i & 1) ? 0x80 : 0));
            }
            result = result && pp_index_arguments(&pp);
            for (uint32_t open = 0; result && open < count; open += 1)
            {
                if ((pp.arguments.kinds[open] & 0x7F) == C_TOKEN_L_PAREN)
                {
                    uint32_t depth = 0;
                    uint32_t close = BUSTER_PP_NONE;
                    for (uint32_t i = open + 1; close == BUSTER_PP_NONE && i < count; i += 1)
                    {
                        const uint8_t kind = pp.arguments.kinds[i] & 0x7F;
                        close = kind == C_TOKEN_R_PAREN && depth == 0 ? i : close;
                        depth += kind == C_TOKEN_L_PAREN;
                        depth -= kind == C_TOKEN_R_PAREN;
                    }
                    const uint32_t found = pp.argument_links[open];
                    result = close != BUSTER_PP_NONE ? found == close : found <= open || found >= count;
                    if (result && close != BUSTER_PP_NONE)
                    {
                        uint32_t next = pp.argument_links[close];
                        depth = 0;
                        for (uint32_t i = open + 1; result && i < close; i += 1)
                        {
                            const uint8_t kind = pp.arguments.kinds[i] & 0x7F;
                            if (kind == C_TOKEN_COMMA && depth == 0)
                            {
                                result = next == i;
                                if (result)
                                {
                                    next = pp.argument_links[next];
                                }
                            }
                            depth += kind == C_TOKEN_L_PAREN;
                            depth -= kind == C_TOKEN_R_PAREN;
                        }
                        result = result && next == close;
                    }
                }
            }
            if (!result)
            {
                printf("argument links: mode %u, count %u\n", mode, count);
            }
        }
    }
    arena_release(&pp.argument_link_arena);
    const pid_t child = result ? fork() : -1;
    if (child == 0)
    {
        const struct rlimit limit = { 0, 0 };
        Preprocessor failure = { .options = { .quiet = true } };
        const bool limited = setrlimit(RLIMIT_AS, &limit) == 0;
        const bool indexed = limited && pp_index_arguments(&failure);
        _exit(limited && !indexed && failure.fatal && failure.errors == 1 ? 0 : 1);
    }
    int status = 0;
    result = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && result;
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
    printf("argument links: %s (independent pairing, protected tails, stack reuse, allocation failure, both kernels)\n", result ? "pass" : "FAIL");
    return result;
}

// The retained raw-token budget is linear in the input, including at the formerly refused depth of 8,000. Repeating
// the invocation reuses the same stack slots with different tokens, exercising the delimiter index's invalidation.
static bool pp_test_nested_arguments(void)
{
    static const uint32_t depths[] = { 1, 42, 43, 63, 64, 65, 1000, 2000, 4000, 8000 };
    bool result = true;
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(depths); d += 1)
    {
        const uint32_t depth = depths[d];
        const uint32_t capacity = 6 * depth + 64;
        char* source = malloc(capacity);
        uint32_t length = source != 0 ? (uint32_t)snprintf(source, capacity, "#define F(x) x\n") : 0;
        for (uint32_t repeat = 0; source != 0 && repeat < 2; repeat += 1)
        {
            for (uint32_t i = 0; i < depth; i += 1)
            {
                memcpy(source + length, "F(", 2);
                length += 2;
            }
            source[length] = (char)('1' + repeat);
            length += 1;
            memset(source + length, ')', depth);
            length += depth;
            source[length] = '\n';
            length += 1;
        }
        for (uint32_t run = 0; source != 0 && run < (depth == 1000 ? 12u : 4u); run += 1)
        {
            const uint32_t mode = run % 4;
            const uint32_t fault = run / 4;
            static InternTable table;
            static Preprocessor pp;
            const PPOptions options = {
                .language = { .standard = 2017, .gnu = true },
                .scalar = (mode & 1) != 0,
                .no_history = (mode & 2) != 0,
                .quiet = true,
            };
            const bool initialized = intern_initialize(&table) && pp_initialize(&pp, &options, &table);
            const uint32_t saved = initialized ? pp.arguments.capacity : 0;
            const uint32_t saved_captures = initialized ? pp.captures.capacity : 0;
            pp.arguments.capacity = fault == 1 ? 16 : 3 * depth + 4096;
            pp.captures.capacity = fault == 2 ? 0 : saved_captures;
            const bool ran = initialized && pp_run_source(&pp, "nested-arguments.c", (const uint8_t*)source, length);
            bool passed = fault != 0 ? initialized && !ran && pp.fatal && pp.errors == 1 && pp.output.count == 0
                                    : ran && pp.output.count == 2 && pp.errors == 0 && pp.warnings == 0;
            for (uint32_t i = 0; fault == 0 && passed && i < 2; i += 1)
            {
                uint8_t buffer[32];
                const uint8_t* spelling;
                const uint32_t bytes = pp_spelling(&pp, pp.output.kinds[i], pp.output.values[i], pp.output.locations[i], buffer, &spelling);
                passed = bytes == 1 && spelling[0] == '1' + i;
            }
            if (!passed)
            {
                printf("nested arguments: depth %u, mode %u, fault %u: ran %u, errors %u, output %u\n", depth, mode, fault, ran, pp.errors, pp.output.count);
            }
            result = result && passed;
            pp.arguments.capacity = saved;
            pp.captures.capacity = saved_captures;
            pp_release(&pp);
            intern_release(&table);
        }
        result = result && source != 0;
        free(source);
    }
    printf("nested arguments: %s (depths through 8000, linear raw storage, token limits, both kernels and histories)\n", result ? "pass" : "FAIL");
    return result;
}

// Every fixture is inside 80 identity invocations, so the inner call is collected from an indexed raw slice. The
// existing case oracle compares flags, locations, histories, both printers, deferred replay and optionally Clang.
static bool pp_test_argument_slice_semantics(const char* directory, bool with_clang)
{
    static const struct
    {
        const char* name;
        const char* definitions;
        const char* input;
        const char* expected;
        bool fails;
    } cases[] = {
        { "slice whitespace", "#define S_(x) #x\n#define S(x) S_(x)\n#define P(a,b) a b\n", "S(P(\n a,\n b))", "\"a b\"", false },
        { "slice disabled self", "#define SELF(x) SELF(x)\n", "SELF(7)", "SELF(7)", false },
        { "slice disabled indirect", "#define A(x) B(x)\n#define B(x) A(x)\n", "A(7)", "A(7)", false },
        { "slice raw and expanded", "#define X 3\n#define B(x) #x x ## 4 x\n", "B(X)", "\"X\" X4 3", false },
        { "slice expansion order", "#define B(a,b) b a b\n", "B(__COUNTER__,__COUNTER__)", "0 1 0", false },
        { "slice variadic commas", "#define V(a,...) a __VA_OPT__(+ __VA_ARGS__)\n", "V(1,(2,3),4)", "1 + (2,3),4", false },
        { "slice omitted variadic", "#define V(a,...) a , ## __VA_ARGS__\n", "V(1)", "1", false },
        { "slice empty arguments", "#define CAT(a,b) a ## b\n#define E\n#define B(a,b) [a|b]\n", "B(CAT(,),E)", "[|]", false },
        { "slice context boundary", "#define CALL B\n#define B(a) a\n", "CALL(1)", "1", false },
        { "slice source location", "#line 40 \"slice.c\"\n", "__LINE__", "40", false },
        { "slice pragma capture", "#define D(x) x x\n", "D(_Pragma(\"ppf slice\"))", "#pragma ppf slice\n#pragma ppf slice\n", false },
        { "slice too few arguments", "#define B(a,b) a b\n", "B(1)", "", true },
        { "slice too many arguments", "#define B(a) a\n", "B(1,2)", "", true },
    };
    bool result = true;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char source[4096];
        uint32_t length = (uint32_t)snprintf(source, sizeof(source), "#define ID(x) x\n%s", cases[c].definitions);
        for (uint32_t i = 0; i < 80; i += 1)
        {
            memcpy(source + length, "ID(", 3);
            length += 3;
        }
        length += (uint32_t)snprintf(source + length, sizeof(source) - length, "%s", cases[c].input);
        memset(source + length, ')', 80);
        length += 80;
        source[length] = '\n';
        source[length + 1] = 0;
        const PPTestCase test = {
            .name = cases[c].name,
            .source = source,
            .standard = "gnu23",
            .expected = cases[c].expected,
            .fails = cases[c].fails,
        };
        result = pp_test_case(&test, directory, with_clang) && result;
    }
    printf("argument slice semantics: %s (%lu fixtures)\n", result ? "pass" : "FAIL", (unsigned long)BUSTER_ARRAY_LENGTH(cases));
    return result;
}
