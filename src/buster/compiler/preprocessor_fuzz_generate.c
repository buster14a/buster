// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original, bounded preprocessing cases, included after preprocessor_fuzz.c. No fixtures or grammar implementation
// are imported. The case id chooses one of 32 families, ensuring coverage independently of the random seed.
// The seed varies a small name alphabet, macro chains and collisions, redefinitions, nesting, operands and spacing.
// @a through @h in templates mean case-prefixed names; $0 through $2 mean positive integer operands. Rendering
// replaces only those explicit placeholders, never scans an unbounded source string, and shares the core's builder.
//
// Every definition is removed before another case can see it. All conditionals terminate, even in rejected cases.
// __COUNTER__ cases are marked isolated because clang's counter is process-wide and cannot be reset with #undef.

#include <inttypes.h>

#define BUSTER_PPF_GENERATOR_ALPHABET 8
#define BUSTER_PPF_GENERATOR_PREFIX_BYTES 48
#define BUSTER_PPF_GENERATOR_TEXT(text) ((String8){ \
    .pointer = (char*)(text), \
    .length = sizeof(text) - 1, \
})

static const char* const preprocessor_fuzz_construct_names[BUSTER_PPF_CONSTRUCT_COUNT] =
{
    "object macros",
    "function macros",
    "variadic macros",
    "named variadic macros",
    "empty and parameterless macros",
    "stringification and suppressed expansion",
    "chained token paste",
    "empty arguments around paste",
    "invalid paste and stringification positions",
    "variadic forwarding",
    "VA_OPT and expansion to empty",
    "GNU comma variadics",
    "self reference",
    "mutual reference",
    "invocation crossing a replacement boundary",
    "nested invocations and protected commas",
    "unprotected bracket commas",
    "unprotected brace commas",
    "empty arguments",
    "missing arguments",
    "multiline arguments",
    "undefinition and redefinition",
    "conditional directives and defined",
    "macro-produced defined",
    "arithmetic and bitwise conditional operators",
    "logical and conditional operators",
    "short circuit and dead division",
    "Pragma",
    "LINE in multiline invocations",
    "COUNTER",
    "splices and comments at token boundaries",
    "redefinition identity",
};

typedef struct PreprocessorFuzzGenerator PreprocessorFuzzGenerator;
struct PreprocessorFuzzGenerator
{
    PreprocessorFuzzBuffer* buffer;
    String8 prefix;
    uint64_t state;
    uint32_t rotation;
    uint32_t numbers[3];
};

static uint32_t preprocessor_fuzz_generator_random(PreprocessorFuzzGenerator* generator)
{
    generator->state = generator->state * 1664525u + 1013904223u;
    return (uint32_t)(generator->state >> 32);
}

static void preprocessor_fuzz_generator_text(PreprocessorFuzzGenerator* generator, String8 text)
{
    uint64_t position = 0;
    while (generator->buffer->valid && position < text.length)
    {
        const char current = text.pointer[position];
        const char next = position + 1 < text.length ? text.pointer[position + 1] : '\0';
        if (current == '@' && next >= 'a' && next <= 'h')
        {
            const char name = (char)('a' + ((uint32_t)(next - 'a') + generator->rotation) % BUSTER_PPF_GENERATOR_ALPHABET);
            preprocessor_fuzz_append(generator->buffer, generator->prefix);
            preprocessor_fuzz_format(generator->buffer, "%c", name);
            position += 2;
        }
        else if (current == '$' && next >= '0' && next <= '2')
        {
            preprocessor_fuzz_format(generator->buffer, "%u", generator->numbers[next - '0']);
            position += 2;
        }
        else
        {
            const uint64_t begin = position;
            position += 1;
            while (position < text.length && text.pointer[position] != '@' && text.pointer[position] != '$')
            {
                position += 1;
            }
            const String8 part =
            {
                .pointer = text.pointer + begin,
                .length = position - begin,
            };
            preprocessor_fuzz_append(generator->buffer, part);
        }
    }
}

static void preprocessor_fuzz_generator_cleanup(PreprocessorFuzzGenerator* generator)
{
    for (uint32_t name = 0; name < BUSTER_PPF_GENERATOR_ALPHABET; name += 1)
    {
        preprocessor_fuzz_format(generator->buffer, "#undef %.*s%c\n", (int)generator->prefix.length, generator->prefix.pointer, (int)('a' + name));
    }
}

static void preprocessor_fuzz_generator_chain(PreprocessorFuzzGenerator* generator)
{
    // A small pool is deliberately reused. Different rounds can create a forward alias, a mutual alias cycle,
    // an expansion to nothing or an arithmetic replacement; those choices then interact with nested invocations.
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
        "#define @a $0\n"
        "#define @b $1\n"
        "#define @f(@c) (@c + @a)\n"
        "#define @g(@c,@d) @f(@c) + @d\n"));
    const uint32_t rounds = 1 + preprocessor_fuzz_generator_random(generator) % 5;
    for (uint32_t round = 0; round < rounds; round += 1)
    {
        const uint32_t choice = preprocessor_fuzz_generator_random(generator) % 4;
        switch (choice)
        {
            case 0:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#undef @a\n#define @a (@b + $2)\n"));
            } break;
            case 1:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#undef @b\n#define @b (@a + $0)\n"));
            } break;
            case 2:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#undef @a\n#define @a @b\n"));
            } break;
            case 3:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#undef @b\n#define @b\n"));
            } break;
        }
        const uint32_t depth = 1 + preprocessor_fuzz_generator_random(generator) % 6;
        for (uint32_t level = 0; level < depth; level += 1)
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("@f("));
        }
        const uint32_t argument = preprocessor_fuzz_generator_random(generator) % 3;
        switch (argument)
        {
            case 0:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("$0"));
            } break;
            case 1:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("@a"));
            } break;
            case 2:
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("@g($1,($2,$0))"));
            } break;
        }
        for (uint32_t level = 0; level < depth; level += 1)
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(")"));
        }
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("\n"));
    }
    preprocessor_fuzz_generator_cleanup(generator);
}

static void preprocessor_fuzz_generator_nested(PreprocessorFuzzGenerator* generator)
{
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
        "#define @f(@a) (@a)\n"
        "#define @g(@a,@b) @f(@a) @f(@b)\n"
        "#define @e()\n"));
    const uint32_t depth = 1 + preprocessor_fuzz_generator_random(generator) % 12;
    for (uint32_t level = 0; level < depth; level += 1)
    {
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("@f("));
    }
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("@g(($0,$1),@f($2))"));
    for (uint32_t level = 0; level < depth; level += 1)
    {
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(")"));
    }
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("\n@f(@e()) @f([$0]) @f({$1})\n"));
}

static void preprocessor_fuzz_generator_boundaries(PreprocessorFuzzGenerator* generator)
{
    static const String8 gaps[] =
    {
        BUSTER_PPF_GENERATOR_TEXT(" "),
        BUSTER_PPF_GENERATOR_TEXT("/**/"),
        BUSTER_PPF_GENERATOR_TEXT(" \\\n"),
        BUSTER_PPF_GENERATOR_TEXT("\\\n/**/"),
        BUSTER_PPF_GENERATOR_TEXT("/**/\\\n"),
        BUSTER_PPF_GENERATOR_TEXT("\t/*\n*/"),
    };
    static const String8 tokens[] =
    {
        BUSTER_PPF_GENERATOR_TEXT("#"),
        BUSTER_PPF_GENERATOR_TEXT("define"),
        BUSTER_PPF_GENERATOR_TEXT("@f"),
        BUSTER_PPF_GENERATOR_TEXT("("),
        BUSTER_PPF_GENERATOR_TEXT("@a"),
        BUSTER_PPF_GENERATOR_TEXT(","),
        BUSTER_PPF_GENERATOR_TEXT("@b"),
        BUSTER_PPF_GENERATOR_TEXT(")"),
        BUSTER_PPF_GENERATOR_TEXT("@a"),
        BUSTER_PPF_GENERATOR_TEXT("+"),
        BUSTER_PPF_GENERATOR_TEXT("("),
        BUSTER_PPF_GENERATOR_TEXT("@b"),
        BUSTER_PPF_GENERATOR_TEXT(")"),
    };
    const uint32_t token_count = sizeof(tokens) / sizeof(tokens[0]);
    const uint32_t gap_count = sizeof(gaps) / sizeof(gaps[0]);
    for (uint32_t token = 0; token < token_count; token += 1)
    {
        preprocessor_fuzz_generator_text(generator, tokens[token]);
        if (token + 1 < token_count)
        {
            if (token == 2)
            {
                // The splice has no logical whitespace: this stays function-like. The second definition below
                // inserts a comment at the same boundary, making an object-like replacement instead.
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("\\\n"));
            }
            else
            {
                const uint32_t gap = preprocessor_fuzz_generator_random(generator) % gap_count;
                preprocessor_fuzz_generator_text(generator, gaps[gap]);
            }
        }
    }
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("\n@f"));
    const uint32_t argument_gap = preprocessor_fuzz_generator_random(generator) % gap_count;
    preprocessor_fuzz_generator_text(generator, gaps[argument_gap]);
    preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
        "(\\\n$0/**/,\\\n$1/**/)\\\n\n"
        "#define @g/**/(@a) @a\n"
        "@g($2)\n"
        "#define @h(@a) #@a\n"
        "@h(@a/**/@b\\\n+@c)\n"
        "@h(@a// ppf_comment_splice\\\ncontinued\n"
        "+@b)\n"));
}

static void preprocessor_fuzz_generator_family(PreprocessorFuzzGenerator* generator, uint32_t family, bool c23)
{
    switch (family)
    {
        case 0:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @a $0\n#define @b @a + $1\n@a @b\n"));
        } break;
        case 1:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) (@a * @b + @a)\n@f($0,$1)\n"));
        } break;
        case 2:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,...) @a __VA_ARGS__\n@f($0,($1,$2)) @f($1,)\n"));
        } break;
        case 3:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a...) @a\n@f($0,($1,$2)) @f()\n"));
        } break;
        case 4:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @e\n#define @f() @e\n@f() @e @d\n"));
        } break;
        case 5:
        {
            // The wrong-arity invocation must remain unexpanded when its spelling is stringified.
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "#define @g(@a,@b) @a + @b\n"
                "#define @f(@a) #@a\n"
                "@f(@g($0)) @f(@a/**/@b\\\n+($1,$2))\n"
                "#define @h(@a,@b) #@a @a #@b\n"
                "@h($0,$1)\n"));
        } break;
        case 6:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b,@c) @a ## @b ## @c\n@f(@d,1,2)\n"));
        } break;
        case 7:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) @a ## @b\n@f(,@d) @f(@e,) @f(,)\n"));
        } break;
        case 8:
        {
            // Keep both illegal paste endpoints in every case; the seed changes their order, not their coverage.
            if ((preprocessor_fuzz_generator_random(generator) & 1) == 0)
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) ## @a\n#define @g(@a) @a ##\n"));
            }
            else
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @g(@a) @a ##\n#define @f(@a) ## @a\n"));
            }
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "#define @h(@a) @a #\n"
                "#define @e(@a) # 1\n"
                "#define @d(@a,@b) @a ## @b\n"
                "@d(+,*)\n"));
        } break;
        case 9:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(...) __VA_ARGS__\n#define @g(@a,...) @f(@a,__VA_ARGS__)\n@g($0) @g($0,$1,$2)\n"));
        } break;
        case 10:
        {
            if (c23)
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @e\n#define @f(...) $0 __VA_OPT__(+ __VA_ARGS__)\n@f() @f(@e) @f($1)\n"));
            }
            else
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @e\n#define @f(@a) @a\n@f(@e) @f($1)\n"));
            }
        } break;
        case 11:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,...) @a , ## __VA_ARGS__\n@f($0) @f($1,) @f($2,$0)\n"));
        } break;
        case 12:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @a @a\n#define @f(@b) @f(@b)\n@a @f($0)\n"));
        } break;
        case 13:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @a @b\n#define @b @a\n#define @f(@c) @g(@c)\n#define @g(@c) @f(@c)\n@a @f($0)\n"));
        } break;
        case 14:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a\n#define @g @f\n#define @h() @f\n#define @e @f(\n@g($0) @h()($1) @e $2)\n"));
        } break;
        case 15:
        {
            preprocessor_fuzz_generator_nested(generator);
        } break;
        case 16:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a\n@f([$0,$1])\n"));
        } break;
        case 17:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a\n@f({$0,$1})\n"));
        } break;
        case 18:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @e()\n#define @f(@a,@b) #@a #@b\n#define @g(@a) @a\n@f(,) @f($0,) @f(,$1) @g(@e())\n"));
        } break;
        case 19:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) @a + @b\n@f($0)\n"));
        } break;
        case 20:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) @a @b\n@f(\n($0,\n$1),\n($2)\n)\n"));
        } break;
        case 21:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @a $0\n@a\n#undef @a\n@a\n#define @a $1\n@a\n#undef @a\n#define @a(@b) @b\n@a($2)\n"));
        } break;
        case 22:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "#define @a $0\n"
                "#if defined(@a) && !defined(@b)\n$0\n"
                "#elif defined @b\n$1\n#else\n$2\n#endif\n"
                "#ifdef @a\n$1\n#endif\n"
                "#ifndef @b\n$2\n#endif\n"));
        } break;
        case 23:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @a $0\n#define @d defined(@a)\n#define @e defined @a\n#if @d && @e\n$1\n#else\n$2\n#endif\n"));
        } break;
        case 24:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "#if ((+$0 * $1 / $2 % 7 - 3 + 1) << 2 >> 1) & (~0 ^ 3) | 1\n$0\n#endif\n"
                "#if ('A' != '\\n') && (0x10ULL >= 010u) && (-1 < 0)\n$1\n#endif\n"
                "#if ($0 > 0) && ($1 <= 31) && ($2 == $2)\n$2\n#endif\n"));
        } break;
        case 25:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#if (!0 && $0) || 0\n$0\n#endif\n#if (1 ? $1 : $2) == $1\n$2\n#endif\n#if (0,1)\n$1\n#endif\n"));
            if (c23)
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#if true && !false\n$0\n#endif\n"));
            }
        } break;
        case 26:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "#define @f(@a) (@a + 1)\n"
                "#if 0 && (@f($0) / 0)\n$0\n#else\n$1\n#endif\n"
                "#if 1 || (@f($1) / 0)\n$2\n#endif\n"
                "#if (1 ? @f($0) : (1 / 0)) && (0 ? (1 / 0) : @f($1))\n$0\n#endif\n"));
        } break;
        case 27:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(
                "_Pragma(\"clang diagnostic push\")\n"
                "_Pragma(\"clang diagnostic ignored \\\"-Wunused-variable\\\"\")\n"
                "@a $0\n"
                "_Pragma(\"clang diagnostic pop\")\n"));
        } break;
        case 28:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) @a @b __LINE__\n@f(\n__LINE__,\n__LINE__)\n__LINE__\n"));
        } break;
        case 29:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a @a\n__COUNTER__ @f(__COUNTER__) __COUNTER__\n"));
        } break;
        case 30:
        {
            preprocessor_fuzz_generator_boundaries(generator);
        } break;
        case 31:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a + $0\n#define @f(@a) @a/**/+\\\n $0\n@f($1)\n"));
            if ((preprocessor_fuzz_generator_random(generator) & 1) != 0)
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a+$0\n@f($2)\n"));
            }
            else
            {
                preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@b) @b + $0\n@f($2)\n"));
            }
        } break;
    }
}

static bool preprocessor_fuzz_generate(Arena* arena, uint64_t seed, uint64_t id, bool c23, PreprocessorFuzzCase* result)
{
    const uint64_t saved_length = arena->length;
    PreprocessorFuzzBuffer buffer =
    {
        .data = arena_push(arena, BUSTER_PPF_MAX_CASE_BYTES, 1),
        .length = 0,
        .capacity = BUSTER_PPF_MAX_CASE_BYTES,
        .valid = false,
    };
    buffer.valid = buffer.data != 0;
    char prefix[BUSTER_PPF_GENERATOR_PREFIX_BYTES];
    const int prefix_length = snprintf(prefix, sizeof(prefix), "ppf_%" PRIu64 "_", id);
    buffer.valid = buffer.valid && prefix_length > 0 && (uint64_t)prefix_length < sizeof(prefix);
    if (buffer.valid)
    {
        PreprocessorFuzzGenerator generator =
        {
            .buffer = &buffer,
            .prefix =
            {
                .pointer = prefix,
                .length = (uint64_t)prefix_length,
            },
            .state = seed ^ (id * 1099511628211ull),
        };
        generator.rotation = preprocessor_fuzz_generator_random(&generator) % BUSTER_PPF_GENERATOR_ALPHABET;
        for (uint32_t number = 0; number < 3; number += 1)
        {
            generator.numbers[number] = 1 + preprocessor_fuzz_generator_random(&generator) % 31;
        }
        const uint32_t family = id % BUSTER_PPF_CONSTRUCT_COUNT;
        preprocessor_fuzz_format(&buffer, "/* ppf_%" PRIu64 "_ seed=%" PRIu64 " family=%u std=c%u */\n", id, seed, family, c23 ? 23 : 17);
        preprocessor_fuzz_generator_chain(&generator);
        preprocessor_fuzz_generator_family(&generator, family, c23);
        preprocessor_fuzz_generator_cleanup(&generator);
        if (buffer.valid)
        {
            // Keep only the bytes actually generated; the builder's spare capacity is not part of the source.
            arena_pop_to(arena, saved_length + buffer.length);
            *result = (PreprocessorFuzzCase)
            {
                .source =
                {
                    .pointer = buffer.data,
                    .length = buffer.length,
                },
                .id = id,
                .constructs = 1ull << family,
                .isolated = family == 29,
            };
        }
    }
    if (!buffer.valid)
    {
        arena_pop_to(arena, saved_length);
    }
    return buffer.valid;
}
