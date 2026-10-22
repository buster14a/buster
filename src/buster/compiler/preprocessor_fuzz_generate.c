// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original, bounded preprocessing cases, included after preprocessor_fuzz.c. No fixtures or grammar implementation
// are imported. The case id chooses one of 48 families (preprocessor_fuzz_family), ensuring coverage independently of
// the random seed: 36 templates and 12 random programs (preprocessor_fuzz_random_program), each with a theme.
// The seed varies a small name alphabet, macro chains and collisions, redefinitions, nesting, operands and spacing.
// @a through @h in templates mean case-prefixed names; $0 through $2 mean positive integer operands. Rendering
// replaces only those explicit placeholders, never scans an unbounded source string, and shares the core's builder.
//
// Every definition is removed before another case can see it. All conditionals terminate, even in rejected cases.
// __COUNTER__ cases are marked isolated because clang's counter is process-wide and cannot be reset with #undef, and so
// are cases with a missing include, a fatal error after which clang reads nothing of the cases after it.

#include <inttypes.h>

#define BUSTER_PPF_GENERATOR_ALPHABET 8
#define BUSTER_PPF_GENERATOR_PREFIX_BYTES 48
#define BUSTER_PPF_ARITHMETIC_COUNT 13
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
    "leading paste operator",
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
    "trailing paste operator",
    "trailing stringification operator",
    "stringification of a non-parameter",
    "paste producing an invalid token",
    "random macro programs",
    "random token pastes",
    "random stringification",
    "random conditional expressions",
    "random directives",
    "random lexical corner cases",
    "random variadic macros and VA_OPT",
    "random Pragma operators",
    "random programs with spliced tokens",
    "random builtin macros",
    "random invocations across boundaries",
    "random directives among macro arguments",
};

// The family of case `id`: each cycle of BUSTER_PPF_FAMILY_CYCLE ids has every template family once and every random
// one BUSTER_PPF_RANDOM_WEIGHT times, the first of each at its own number, so that id `cycle * BUSTER_PPF_FAMILY_CYCLE +
// family` is of `family`.
static uint32_t preprocessor_fuzz_family(uint64_t id)
{
    const uint32_t slot = (uint32_t)(id % BUSTER_PPF_FAMILY_CYCLE);
    const uint32_t result = slot < BUSTER_PPF_TEMPLATE_FAMILIES ? slot : BUSTER_PPF_TEMPLATE_FAMILIES + (slot - BUSTER_PPF_TEMPLATE_FAMILIES) % BUSTER_PPF_RANDOM_FAMILIES;
    return result;
}

typedef struct PreprocessorFuzzGenerator PreprocessorFuzzGenerator;
struct PreprocessorFuzzGenerator
{
    PreprocessorFuzzBuffer* buffer;
    String8 prefix;
    uint64_t state;
    uint32_t rotation;
    uint32_t numbers[3];
    // The random families' state: the mode, each name's shape (0 undefined, 1 object-like, 2 function-like, 3 unknown)
    // with its named parameters and whether it is variadic, the last character written, and what the case needs.
    bool c23;
    uint8_t kinds[BUSTER_PPF_GENERATOR_ALPHABET];
    uint8_t parameters[BUSTER_PPF_GENERATOR_ALPHABET];
    bool variadic[BUSTER_PPF_GENERATOR_ALPHABET];
    char last;
    // The last text written was a ##, or a paste's operand is being written: no rare token (a ## among them) comes next.
    // clang's paste of a `## ##` whose second goes with an empty argument or __VA_OPT__ at a body's end reads past the
    // expansion's tokens (TokenLexer::pasteTokens), which can hang it.
    bool last_paste;
    bool in_paste;
    // Splice tokens inside (BUSTER_PPF_THEME_SPLICES); let brackets and braces hold a comma; the lexemes' operation
    // (0 either, 1 paste, 2 stringize); the expansions a body has left; the groups so far.
    bool splice_tokens;
    bool comma_in_groups;
    uint32_t lexemes;
    uint32_t expansions_left;
    uint32_t groups;
    // __COUNTER__ and a fatal error (a missing include): the case runs alone.
    bool counter;
    bool fatal;
    // The diagnostic pushes written, which the case's end pops.
    uint32_t pushes;
    char poison_name[BUSTER_PPF_GENERATOR_PREFIX_BYTES + 16];
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

static void preprocessor_fuzz_generator_arithmetic(PreprocessorFuzzGenerator* generator)
{
    // Operands are 1..31: every result is nonzero, all shifts are nonnegative and bounded, and no divisor is zero.
    // Each expression controls both a true and a false predicate, with different output tokens in either arm.
    static const String8 expressions[BUSTER_PPF_ARITHMETIC_COUNT] =
    {
        BUSTER_PPF_GENERATOR_TEXT("+$0"),
        BUSTER_PPF_GENERATOR_TEXT("-$0"),
        BUSTER_PPF_GENERATOR_TEXT("~$0"),
        BUSTER_PPF_GENERATOR_TEXT("$0 * $1"),
        BUSTER_PPF_GENERATOR_TEXT("(32 + $0) / $1"),
        BUSTER_PPF_GENERATOR_TEXT("($0 * 32 + $1) % 32"),
        BUSTER_PPF_GENERATOR_TEXT("$0 + $1"),
        BUSTER_PPF_GENERATOR_TEXT("$0 - 32"),
        BUSTER_PPF_GENERATOR_TEXT("$0 << 1"),
        BUSTER_PPF_GENERATOR_TEXT("($0 << 2) >> 1"),
        BUSTER_PPF_GENERATOR_TEXT("(32 | $0) & 31"),
        BUSTER_PPF_GENERATOR_TEXT("$0 ^ 32"),
        BUSTER_PPF_GENERATOR_TEXT("$0 | 32"),
    };
    const int64_t a = generator->numbers[0];
    const int64_t b = generator->numbers[1];
    const int64_t values[BUSTER_PPF_ARITHMETIC_COUNT] =
    {
        a,
        -a,
        ~a,
        a * b,
        (32 + a) / b,
        (a * 32 + b) % 32,
        a + b,
        a - 32,
        a << 1,
        (a << 2) >> 1,
        (32 | a) & 31,
        a ^ 32,
        a | 32,
    };
    for (uint32_t operation = 0; operation < BUSTER_PPF_ARITHMETIC_COUNT; operation += 1)
    {
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f "));
        preprocessor_fuzz_format(generator->buffer, "/* ppf_arithmetic_%u */ (", operation);
        preprocessor_fuzz_generator_text(generator, expressions[operation]);
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT(")\n"));
        for (uint32_t predicate = 0; predicate < 2; predicate += 1)
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#if @f == "));
            preprocessor_fuzz_format(generator->buffer, "(%" PRId64 ")\n%u\n#else\n%u\n#endif\n",
                values[operation] + predicate, 100 + operation, 200 + operation);
        }
        preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#undef @f\n"));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Random programs: families 36 to 47
//
// Each draws a small program over the case's eight names (ppf_<id>_a to ppf_<id>_h, by index, without the templates'
// rotation): definitions of every shape, text lines with invocations, conditionals with random expressions, and the
// other directives, with a gap at each token boundary (blanks, comments, line splices, and newlines where a line may
// break). The generator follows each name's current shape so that most invocations have the right number of arguments,
// and keeps a case self-contained: every conditional and comment is closed, `/` never meets `/` or `*` without a blank,
// no name is used after a poison but on purpose (pp_poison's rules), the output stays small (bodies of at most BUSTER_PPF_BODY_ITEMS items, invocations at
// most BUSTER_PPF_RANDOM_DEPTH deep), and the cleanup removes every definition. Families differ in what they draw most:
// a family's own construct is in every case it makes, which the self-tests check.

#define BUSTER_PPF_BODY_ITEMS 8
#define BUSTER_PPF_BODY_EXPANSIONS 3
#define BUSTER_PPF_RANDOM_DEPTH 3
#define BUSTER_PPF_RANDOM_STATEMENTS 10

// What a random program draws.
typedef enum PreprocessorFuzzTheme
{
    BUSTER_PPF_THEME_MACROS,
    BUSTER_PPF_THEME_PASTES,
    BUSTER_PPF_THEME_STRINGS,
    BUSTER_PPF_THEME_CONDITIONS,
    BUSTER_PPF_THEME_DIRECTIVES,
    BUSTER_PPF_THEME_LEXEMES,
    BUSTER_PPF_THEME_VARIADIC,
    BUSTER_PPF_THEME_PRAGMAS,
    BUSTER_PPF_THEME_SPLICES,
    BUSTER_PPF_THEME_BUILTINS,
    BUSTER_PPF_THEME_BOUNDARIES,
    BUSTER_PPF_THEME_EMBEDDED,
} PreprocessorFuzzTheme;

static uint32_t preprocessor_fuzz_random_below(PreprocessorFuzzGenerator* generator, uint32_t count)
{
    const uint32_t result = count != 0 ? preprocessor_fuzz_generator_random(generator) % count : 0;
    return result;
}

static bool preprocessor_fuzz_random_one_in(PreprocessorFuzzGenerator* generator, uint32_t count)
{
    const bool result = preprocessor_fuzz_random_below(generator, count) == 0;
    return result;
}

// Appends text as it is (no placeholders), splicing a token's spelling inside with BUSTER_PPF_THEME_SPLICES.
static void preprocessor_fuzz_random_raw(PreprocessorFuzzGenerator* generator, const char* text)
{
    const uint64_t length = strlen(text);
    generator->last_paste = strcmp(text, "##") == 0 || strcmp(text, "%:%:") == 0;
    uint64_t split = length;
    if (generator->splice_tokens && length >= 2 && preprocessor_fuzz_random_one_in(generator, 3))
    {
        split = 1 + preprocessor_fuzz_random_below(generator, (uint32_t)(length - 1));
    }
    preprocessor_fuzz_append(generator->buffer, (String8){
        .pointer = (char*)text,
        .length = split,
    });
    if (split < length)
    {
        const bool trigraph = !generator->c23 && preprocessor_fuzz_random_one_in(generator, 4);
        preprocessor_fuzz_append(generator->buffer, trigraph ? BUSTER_PPF_GENERATOR_TEXT("?\?/\n") : BUSTER_PPF_GENERATOR_TEXT("\\\n"));
        preprocessor_fuzz_append(generator->buffer, (String8){
            .pointer = (char*)text + split,
            .length = length - split,
        });
    }
    generator->last = length != 0 ? text[length - 1] : generator->last;
}

// Name `index` (0 to 7) of the case: its prefix and a letter.
static void preprocessor_fuzz_random_name(PreprocessorFuzzGenerator* generator, uint32_t index)
{
    char name[BUSTER_PPF_GENERATOR_PREFIX_BYTES + 2];
    snprintf(name, sizeof(name), "%.*s%c", (int)generator->prefix.length, generator->prefix.pointer, (int)('a' + index % BUSTER_PPF_GENERATOR_ALPHABET));
    preprocessor_fuzz_random_raw(generator, name);
}

// A gap between two tokens: mostly a blank, else nothing, a comment, a line splice, or with `newline` a line break.
// Inside a directive (no `newline`), a comment may still hold a newline: the directive goes on after it. After `/` or
// `\`, the gap is a blank, so that no comment starts and no line is spliced by accident.
static void preprocessor_fuzz_random_gap(PreprocessorFuzzGenerator* generator, bool newline)
{
    static const char* const gaps[] =
    {
        " ",
        " ",
        " ",
        " ",
        " ",
        " ",
        "",
        "",
        "",
        "  ",
        "\t",
        "/**/",
        "/* c */",
        " /**/ ",
        "\\\n",
        " \\\n",
        "\\\n ",
        "/*\n*/",
        "\\ \n",
        "\f",
    };
    const bool forced = generator->last == '/' || generator->last == '\\';
    const uint32_t choice = forced ? 0 : preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(gaps) + (newline ? 3 : 0) + (generator->c23 ? 0 : 1));
    if (choice < BUSTER_ARRAY_LENGTH(gaps))
    {
        preprocessor_fuzz_append(generator->buffer, (String8){
            .pointer = (char*)gaps[choice],
            .length = strlen(gaps[choice]),
        });
    }
    else if (newline && choice < BUSTER_ARRAY_LENGTH(gaps) + 3)
    {
        preprocessor_fuzz_append(generator->buffer, choice == BUSTER_ARRAY_LENGTH(gaps) ? BUSTER_PPF_GENERATOR_TEXT("\n") : BUSTER_PPF_GENERATOR_TEXT("\n  "));
    }
    else
    {
        // A trigraph's line splice, in C17 (in C23 it would end the line).
        preprocessor_fuzz_append(generator->buffer, BUSTER_PPF_GENERATOR_TEXT("?\?/\n"));
    }
    generator->last = ' ';
}

// A blank, which a token that must be separated from the one before gets (a directive's name and its operand).
static void preprocessor_fuzz_random_blank(PreprocessorFuzzGenerator* generator, bool newline)
{
    if (preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_gap(generator, newline);
    }
    preprocessor_fuzz_append(generator->buffer, BUSTER_PPF_GENERATOR_TEXT(" "));
    generator->last = ' ';
}

static void preprocessor_fuzz_random_newline(PreprocessorFuzzGenerator* generator)
{
    preprocessor_fuzz_append(generator->buffer, BUSTER_PPF_GENERATOR_TEXT("\n"));
    generator->last = '\n';
}

static const char* const preprocessor_fuzz_random_numbers[] =
{
    "0", "1", "2", "7", "42", "0x2a", "0X2A", "052", "0b101", "1u", "1U", "2l", "3L", "4ll", "5LL", "6ul", "7ULL", "8llu",
    "1.", ".5", "1.5e3", "1e+5", "1E-5", "0x1p3", "0x1.8p-1", "1e", "1e+", "1..2", "0xe+1", ".1.2", "1_0", "08", "0x", "1i",
    "1wb", "1uwb", "9223372036854775807", "9223372036854775808", "18446744073709551615", "18446744073709551616",
};

static const char* const preprocessor_fuzz_random_literals[] =
{
    "\"\"", "\"a\"", "\"a b\"", "\"\\\"\"", "\"\\\\\"", "\"\\n\"", "\"'\"", "\"/*\"", "\"//\"", "\"#\"", "\"\\x41\"", "\"\\101\"",
    "\"\\u00e9\"", "L\"w\"", "u\"w\"", "U\"w\"", "u8\"w\"", "\"?\?=\"", "'a'", "'\\''", "'\"'", "'\\\\'", "'\\n'", "'\\0'",
    "'\\x41'", "'\\377'", "'ab'", "L'a'", "u'a'", "U'a'", "u8'a'", "'\\u00e9'", "''", "'?\?/''",
};

static const char* const preprocessor_fuzz_random_punctuators[] =
{
    "[", "]", "{", "}", ".", "->", "++", "--", "&", "*", "+", "-", "~", "!", "/", "%", "<<", ">>", "<", ">", "<=", ">=", "==",
    "!=", "^", "|", "&&", "||", "?", ":", "::", ";", "...", "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=",
    "<:", ":>", "<%", "%>", "?\?(", "?\?)", "?\?<", "?\?>", "?\?'", "?\?!", "?\?-",
};

static const char* const preprocessor_fuzz_random_identifiers[] =
{
    "x", "y", "_z", "int", "L", "u", "U", "u8", "R", "true", "false", "defined", "__VA_ARGS__", "__VA_OPT__", "$d", "d$",
    "\\u00e9t\\u00e9", "\\U000000E9x", "x\\u0301", "\xc3\xa9t\xc3\xa9", "_Pragma", "__has_include",
};

// A token that isn't a name of the case's, a parenthesis or a comma: an identifier, a number, a string or character
// literal, a punctuator, or rarely `#`, `##`, `%:`, `@`, `` ` `` or `\`.
static void preprocessor_fuzz_random_literal(PreprocessorFuzzGenerator* generator)
{
    const uint32_t kind = preprocessor_fuzz_random_below(generator, 20);
    const char* text = "x";
    if (kind < 4)
    {
        text = preprocessor_fuzz_random_identifiers[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_identifiers))];
    }
    else if (kind < 9)
    {
        // A digit separator is C23's: before it, the `'` starts a character constant, to the end of the line.
        text = preprocessor_fuzz_random_numbers[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_numbers))];
        text = preprocessor_fuzz_random_one_in(generator, generator->c23 ? 8 : 32) ? "1'000" : text;
    }
    else if (kind < 13)
    {
        text = preprocessor_fuzz_random_literals[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_literals))];
    }
    else if (kind < 19 || generator->last_paste || generator->in_paste)
    {
        // No rare token right after a ##, which could make another ## with the next (PreprocessorFuzzGenerator.last_paste).
        text = preprocessor_fuzz_random_punctuators[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_punctuators))];
    }
    else
    {
        static const char* const rare[] =
        {
            "#",
            "##",
            "%:",
            "%:%:",
            "@",
            "`",
            "\\",
        };
        text = rare[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(rare))];
    }
    preprocessor_fuzz_random_raw(generator, text);
}

// A parameter's name: one of the first five names, which are macros too at times, so that parameters shadow them.
static uint32_t preprocessor_fuzz_random_parameter_name(PreprocessorFuzzGenerator* generator)
{
    const uint32_t result = preprocessor_fuzz_random_below(generator, 5);
    return result;
}

typedef struct PreprocessorFuzzParameters PreprocessorFuzzParameters;
struct PreprocessorFuzzParameters
{
    uint32_t names[4];
    uint32_t count;
    bool variadic;
    // `name...` (GNU's) rather than `...`: the last name is the variadic one.
    bool named_variadic;
    bool function_like;
};

static void preprocessor_fuzz_random_tokens(PreprocessorFuzzGenerator* generator, uint32_t count, bool newline, bool arguments, uint32_t depth);
static void preprocessor_fuzz_random_invocation(PreprocessorFuzzGenerator* generator, uint32_t name, uint32_t depth, bool newline);

// One operand of a body: a parameter, __VA_ARGS__, a name, or a literal. A body expands each of at most
// BUSTER_PPF_BODY_EXPANSIONS of its operands (names and parameters), so that an expansion's size is bounded.
static void preprocessor_fuzz_random_operand(PreprocessorFuzzGenerator* generator, const PreprocessorFuzzParameters* parameters)
{
    const uint32_t choice = generator->expansions_left != 0 ? preprocessor_fuzz_random_below(generator, 8) : 7;
    if (parameters->count != 0 && choice < 4)
    {
        preprocessor_fuzz_random_name(generator, parameters->names[preprocessor_fuzz_random_below(generator, parameters->count)]);
        generator->expansions_left -= 1;
    }
    else if (parameters->variadic && !parameters->named_variadic && choice < 5)
    {
        preprocessor_fuzz_random_raw(generator, "__VA_ARGS__");
        generator->expansions_left -= 1;
    }
    else if (choice < 6)
    {
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        generator->expansions_left -= 1;
    }
    else
    {
        preprocessor_fuzz_random_literal(generator);
    }
}

// A stringized operand: `#` and a parameter (or __VA_ARGS__, or __VA_OPT__(...) of a variadic macro); rarely something
// else, which clang refuses in a function-like macro.
static void preprocessor_fuzz_random_stringized(PreprocessorFuzzGenerator* generator, const PreprocessorFuzzParameters* parameters)
{
    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 8) ? "%:" : "#");
    if (preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_gap(generator, false);
    }
    const uint32_t choice = preprocessor_fuzz_random_below(generator, 12);
    if (parameters->count != 0 && choice < 9)
    {
        preprocessor_fuzz_random_name(generator, parameters->names[preprocessor_fuzz_random_below(generator, parameters->count)]);
    }
    else if (parameters->variadic && !parameters->named_variadic && choice < 10)
    {
        preprocessor_fuzz_random_raw(generator, "__VA_ARGS__");
    }
    else if (parameters->variadic && !parameters->named_variadic && choice < 11)
    {
        preprocessor_fuzz_random_raw(generator, "__VA_OPT__(");
        preprocessor_fuzz_random_operand(generator, parameters);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_operand(generator, parameters);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else
    {
        preprocessor_fuzz_random_operand(generator, parameters);
    }
}

// A replacement list: up to BUSTER_PPF_BODY_ITEMS items, each a body operand, a stringized operand, a paste of operands,
// __VA_OPT__ with content, GNU's `, ## __VA_ARGS__`, an invocation of a name, a balanced group, or rarely a lone
// parenthesis, so that an invocation can start in the body and end in the text after it.
static void preprocessor_fuzz_random_body(PreprocessorFuzzGenerator* generator, const PreprocessorFuzzParameters* parameters, PreprocessorFuzzTheme theme)
{
    const uint32_t items = preprocessor_fuzz_random_below(generator, BUSTER_PPF_BODY_ITEMS + 1);
    generator->expansions_left = BUSTER_PPF_BODY_EXPANSIONS;
    for (uint32_t item = 0; item < items; item += 1)
    {
        if (item != 0)
        {
            preprocessor_fuzz_random_gap(generator, false);
        }
        uint32_t choice = preprocessor_fuzz_random_below(generator, 20);
        choice = theme == BUSTER_PPF_THEME_PASTES && preprocessor_fuzz_random_one_in(generator, 2) ? 8 : choice;
        choice = theme == BUSTER_PPF_THEME_STRINGS && preprocessor_fuzz_random_one_in(generator, 2) ? 6 : choice;
        choice = theme == BUSTER_PPF_THEME_VARIADIC && parameters->variadic && preprocessor_fuzz_random_one_in(generator, 2) ? 11 : choice;
        if (choice < 6)
        {
            preprocessor_fuzz_random_operand(generator, parameters);
        }
        else if (choice < 8 && parameters->function_like)
        {
            preprocessor_fuzz_random_stringized(generator, parameters);
        }
        else if (choice < 10)
        {
            // A paste of two or three operands, with blanks or nothing around each ##.
            const uint32_t operands = 2 + preprocessor_fuzz_random_one_in(generator, 3);
            generator->in_paste = true;
            for (uint32_t operand = 0; operand < operands; operand += 1)
            {
                if (operand != 0)
                {
                    preprocessor_fuzz_random_gap(generator, false);
                    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 8) ? "%:%:" : "##");
                    preprocessor_fuzz_random_gap(generator, false);
                }
                if (parameters->function_like && preprocessor_fuzz_random_one_in(generator, 6))
                {
                    preprocessor_fuzz_random_stringized(generator, parameters);
                }
                else
                {
                    preprocessor_fuzz_random_operand(generator, parameters);
                }
            }
            generator->in_paste = false;
        }
        else if (choice < 13 && parameters->variadic)
        {
            const uint32_t form = preprocessor_fuzz_random_below(generator, 4);
            if (form == 0)
            {
                preprocessor_fuzz_random_raw(generator, ",");
                preprocessor_fuzz_random_gap(generator, false);
                preprocessor_fuzz_random_raw(generator, "##");
                preprocessor_fuzz_random_gap(generator, false);
                if (parameters->named_variadic)
                {
                    preprocessor_fuzz_random_name(generator, parameters->names[parameters->count - 1]);
                }
                else
                {
                    preprocessor_fuzz_random_raw(generator, "__VA_ARGS__");
                }
            }
            else
            {
                // __VA_OPT__ with up to three operands, on either side of a ## at times.
                generator->in_paste = true;
                if (form == 1)
                {
                    preprocessor_fuzz_random_operand(generator, parameters);
                    preprocessor_fuzz_random_gap(generator, false);
                    preprocessor_fuzz_random_raw(generator, "##");
                    preprocessor_fuzz_random_gap(generator, false);
                }
                preprocessor_fuzz_random_raw(generator, "__VA_OPT__");
                if (preprocessor_fuzz_random_one_in(generator, 3))
                {
                    preprocessor_fuzz_random_gap(generator, false);
                }
                preprocessor_fuzz_random_raw(generator, "(");
                const uint32_t operands = preprocessor_fuzz_random_below(generator, 4);
                for (uint32_t operand = 0; operand < operands; operand += 1)
                {
                    preprocessor_fuzz_random_gap(generator, false);
                    if (preprocessor_fuzz_random_one_in(generator, 4))
                    {
                        preprocessor_fuzz_random_operand(generator, parameters);
                        preprocessor_fuzz_random_gap(generator, false);
                        preprocessor_fuzz_random_raw(generator, "##");
                        preprocessor_fuzz_random_gap(generator, false);
                    }
                    if (parameters->function_like && preprocessor_fuzz_random_one_in(generator, 5))
                    {
                        preprocessor_fuzz_random_stringized(generator, parameters);
                    }
                    else
                    {
                        preprocessor_fuzz_random_operand(generator, parameters);
                    }
                }
                preprocessor_fuzz_random_raw(generator, ")");
                if (form == 2)
                {
                    preprocessor_fuzz_random_gap(generator, false);
                    preprocessor_fuzz_random_raw(generator, "##");
                    preprocessor_fuzz_random_gap(generator, false);
                    preprocessor_fuzz_random_operand(generator, parameters);
                }
                generator->in_paste = false;
            }
        }
        else if (choice < 15 && generator->expansions_left != 0)
        {
            // A name of the case's, invoked in the body or left for the text after the expansion to invoke.
            const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
            generator->expansions_left -= 1;
            if (generator->kinds[name] == 2 && preprocessor_fuzz_random_one_in(generator, 2))
            {
                preprocessor_fuzz_random_invocation(generator, name, BUSTER_PPF_RANDOM_DEPTH - 1, false);
            }
            else
            {
                preprocessor_fuzz_random_name(generator, name);
            }
        }
        else if (choice < 17)
        {
            preprocessor_fuzz_random_raw(generator, "(");
            preprocessor_fuzz_random_operand(generator, parameters);
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, ",");
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_operand(generator, parameters);
            preprocessor_fuzz_random_raw(generator, ")");
        }
        else if (choice < 18 || theme == BUSTER_PPF_THEME_BOUNDARIES)
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "(" : ")");
        }
        else
        {
            preprocessor_fuzz_random_literal(generator);
        }
    }
}

// A #define of a name of the case's with a random shape and body, rarely with one of the faults clang refuses.
// One of a replacement list's elements for preprocessor_fuzz_random_spacing_body, of `kind` (below 8): a name or a
// parameter (which ## may join), a parenthesized group or a punctuator, or # and a parameter.
static void preprocessor_fuzz_random_spacing_element(PreprocessorFuzzGenerator* generator, uint32_t kind, uint32_t named)
{
    static const char* const operands[] = { "a", "b", "x", "p", "q", "__VA_ARGS__", "__VA_ARGS__" };
    static const char* const plain[] = { "( )", "(a)", ",", "+", "[", "]" };
    const uint32_t operand_count = 3 + named;
    if (kind < 4)
    {
        const uint32_t pick = preprocessor_fuzz_random_below(generator, operand_count + 2);
        preprocessor_fuzz_random_raw(generator, operands[pick < operand_count ? pick : 5]);
    }
    else if (kind < 6)
    {
        preprocessor_fuzz_random_raw(generator, plain[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(plain))]);
    }
    else
    {
        preprocessor_fuzz_random_raw(generator, "#");
        preprocessor_fuzz_random_gap(generator, false);
        const uint32_t pick = 3 + preprocessor_fuzz_random_below(generator, named + 1);
        preprocessor_fuzz_random_raw(generator, operands[pick < 3 + named ? pick : 5]);
    }
}

// The gap before an element: ## at times between two that it can join (`joins` for both), else a gap of any kind, but
// a blank between two names, so that they don't become one. Updates the two flags for the element after.
static void preprocessor_fuzz_random_spacing_gap(PreprocessorFuzzGenerator* generator, bool first, bool joins, bool name, bool* previous_joins, bool* previous_name)
{
    bool pasted = false;
    if (!first && *previous_joins && joins && preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, "##");
        pasted = true;
    }
    if (*previous_name && !pasted && name)
    {
        preprocessor_fuzz_random_blank(generator, false);
    }
    else
    {
        preprocessor_fuzz_random_gap(generator, false);
    }
    *previous_joins = joins;
}

// A replacement list for preprocessor_fuzz_random_spacing: `length` elements (preprocessor_fuzz_random_spacing_element),
// and __VA_OPT__ and # __VA_OPT__ with content of up to three of their own, with gaps of every kind between them
// (preprocessor_fuzz_random_spacing_gap).
static void preprocessor_fuzz_random_spacing_body(PreprocessorFuzzGenerator* generator, uint32_t length, uint32_t named)
{
    bool previous_joins = false;
    bool previous_name = false;
    for (uint32_t k = 0; k < length; k += 1)
    {
        const uint32_t kind = preprocessor_fuzz_random_below(generator, 10);
        const bool joins = kind < 4 || kind == 8;
        preprocessor_fuzz_random_spacing_gap(generator, k == 0, joins, kind < 4 || kind == 8, &previous_joins, &previous_name);
        if (kind < 8)
        {
            preprocessor_fuzz_random_spacing_element(generator, kind, named);
        }
        else
        {
            preprocessor_fuzz_random_raw(generator, kind == 8 ? "__VA_OPT__" : "#__VA_OPT__");
            if (preprocessor_fuzz_random_one_in(generator, 4))
            {
                preprocessor_fuzz_random_raw(generator, " ");
            }
            preprocessor_fuzz_random_raw(generator, "(");
            const uint32_t content = preprocessor_fuzz_random_below(generator, 4);
            bool content_joins = false;
            bool content_name = false;
            for (uint32_t c = 0; c < content; c += 1)
            {
                const uint32_t inner = preprocessor_fuzz_random_below(generator, 8);
                preprocessor_fuzz_random_spacing_gap(generator, c == 0, inner < 4, inner < 4, &content_joins, &content_name);
                preprocessor_fuzz_random_spacing_element(generator, inner, named);
                content_name = inner < 4 || inner >= 6;
            }
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, ")");
        }
        previous_name = kind < 4 || (kind >= 6 && kind < 8);
    }
}

// A variadic macro whose replacement list mixes __VA_OPT__, ## and # (preprocessor_fuzz_random_spacing_body), invoked
// with empty and other arguments inside a stringizing macro, so that the expansion's spaces show in the string.
static void preprocessor_fuzz_random_spacing(PreprocessorFuzzGenerator* generator)
{
    const int prefix = (int)generator->prefix.length;
    const char* const text = generator->prefix.pointer;
    const uint32_t named = preprocessor_fuzz_random_below(generator, 3);
    char line[256];
    snprintf(line, sizeof(line), "#define %.*svs(...) #__VA_ARGS__\n#define %.*svt(...) %.*svs(__VA_ARGS__)\n#define %.*svm(%s", prefix, text, prefix, text, prefix, text,
             prefix, text, named == 0 ? "..." : named == 1 ? "p, ..." : "p, q, ...");
    preprocessor_fuzz_random_raw(generator, line);
    preprocessor_fuzz_random_raw(generator, ")");
    preprocessor_fuzz_random_spacing_body(generator, 1 + preprocessor_fuzz_random_below(generator, 6), named);
    preprocessor_fuzz_random_raw(generator, "\n");
    static const char* const arguments[] = { "", "", "u", "u v", " u ", "/**/u", "u/**/" };
    const uint32_t uses = 1 + preprocessor_fuzz_random_below(generator, 3);
    for (uint32_t use = 0; use < uses; use += 1)
    {
        snprintf(line, sizeof(line), "%.*svt(%.*svm(", prefix, text, prefix, text);
        preprocessor_fuzz_random_raw(generator, line);
        for (uint32_t parameter = 0; parameter < named; parameter += 1)
        {
            preprocessor_fuzz_random_raw(generator, arguments[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(arguments))]);
            preprocessor_fuzz_random_raw(generator, ",");
        }
        // The variadic argument: omitted (after the named ones, when there are), empty, or one or two.
        const uint32_t variadic = preprocessor_fuzz_random_below(generator, 4);
        if (variadic == 0 && named != 0)
        {
            generator->buffer->length -= 1;
        }
        else if (variadic == 2)
        {
            preprocessor_fuzz_random_raw(generator, arguments[2 + preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(arguments) - 2)]);
        }
        else if (variadic == 3)
        {
            preprocessor_fuzz_random_raw(generator, "u,v");
        }
        preprocessor_fuzz_random_raw(generator, "))\n");
    }
    generator->last = '\n';
}

static void preprocessor_fuzz_random_define(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme)
{
    const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
    PreprocessorFuzzParameters parameters = { 0 };
    const uint32_t shape = preprocessor_fuzz_random_below(generator, 10);
    parameters.function_like = shape >= 3 || theme == BUSTER_PPF_THEME_VARIADIC || theme == BUSTER_PPF_THEME_STRINGS;
    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 16) ? "%:" : "#");
    if (preprocessor_fuzz_random_one_in(generator, 4))
    {
        preprocessor_fuzz_random_gap(generator, false);
    }
    preprocessor_fuzz_random_raw(generator, "define");
    preprocessor_fuzz_random_blank(generator, false);
    preprocessor_fuzz_random_name(generator, name);
    if (parameters.function_like)
    {
        // No blank before the '(' (a line splice isn't one).
        if (preprocessor_fuzz_random_one_in(generator, 12))
        {
            preprocessor_fuzz_random_raw(generator, "\\\n");
        }
        preprocessor_fuzz_random_raw(generator, "(");
        parameters.count = preprocessor_fuzz_random_below(generator, 4);
        uint32_t used = 0;
        for (uint32_t i = 0; i < parameters.count; i += 1)
        {
            uint32_t chosen = preprocessor_fuzz_random_parameter_name(generator);
            while ((used & (1u << chosen)) != 0 && !preprocessor_fuzz_random_one_in(generator, 64))
            {
                chosen = (chosen + 1) % 5;
            }
            used |= 1u << chosen;
            parameters.names[i] = chosen;
        }
        parameters.variadic = theme == BUSTER_PPF_THEME_VARIADIC ? !preprocessor_fuzz_random_one_in(generator, 4) : preprocessor_fuzz_random_one_in(generator, 3);
        parameters.named_variadic = parameters.variadic && parameters.count != 0 && preprocessor_fuzz_random_one_in(generator, 4);
        for (uint32_t i = 0; i < parameters.count; i += 1)
        {
            if (i != 0)
            {
                preprocessor_fuzz_random_gap(generator, false);
                preprocessor_fuzz_random_raw(generator, ",");
            }
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_name(generator, parameters.names[i]);
        }
        if (parameters.named_variadic)
        {
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, "...");
        }
        else if (parameters.variadic)
        {
            if (parameters.count != 0)
            {
                preprocessor_fuzz_random_gap(generator, false);
                preprocessor_fuzz_random_raw(generator, ",");
            }
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, "...");
        }
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    if (preprocessor_fuzz_random_one_in(generator, 8))
    {
        // No blank after an object-like name: clang warns and takes the rest as the body.
        preprocessor_fuzz_random_gap(generator, false);
    }
    else
    {
        preprocessor_fuzz_random_blank(generator, false);
    }
    preprocessor_fuzz_random_body(generator, &parameters, theme);
    preprocessor_fuzz_random_newline(generator);
    generator->kinds[name] = parameters.function_like ? 2 : 1;
    generator->parameters[name] = (uint8_t)(parameters.count - (parameters.named_variadic ? 1 : 0));
    generator->variadic[name] = parameters.variadic;
}

// An argument: up to three items, each a literal, a name (invoked at times), or a parenthesized, bracketed or braced
// group, whose commas only parentheses protect.
static void preprocessor_fuzz_random_argument(PreprocessorFuzzGenerator* generator, uint32_t depth, bool newline)
{
    const uint32_t items = preprocessor_fuzz_random_below(generator, 4);
    for (uint32_t item = 0; item < items; item += 1)
    {
        if (item != 0)
        {
            preprocessor_fuzz_random_gap(generator, newline);
        }
        const uint32_t choice = preprocessor_fuzz_random_below(generator, 12);
        if (choice < 5)
        {
            preprocessor_fuzz_random_literal(generator);
        }
        else if (choice < 9)
        {
            const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
            if (generator->kinds[name] == 2 && depth != 0 && preprocessor_fuzz_random_one_in(generator, 2))
            {
                preprocessor_fuzz_random_invocation(generator, name, depth - 1, newline);
            }
            else
            {
                preprocessor_fuzz_random_name(generator, name);
            }
        }
        else if (choice < 11)
        {
            preprocessor_fuzz_random_raw(generator, "(");
            preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 3), newline, true, depth != 0 ? depth - 1 : 0);
            if (preprocessor_fuzz_random_one_in(generator, 2))
            {
                preprocessor_fuzz_random_raw(generator, ",");
                preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 3), newline, true, depth != 0 ? depth - 1 : 0);
            }
            preprocessor_fuzz_random_raw(generator, ")");
        }
        else
        {
            // Brackets and braces don't protect a comma: with one, the argument is two.
            const bool bracket = preprocessor_fuzz_random_one_in(generator, 2);
            preprocessor_fuzz_random_raw(generator, bracket ? "[" : "{");
            preprocessor_fuzz_random_literal(generator);
            if (generator->comma_in_groups)
            {
                preprocessor_fuzz_random_raw(generator, ",");
                preprocessor_fuzz_random_literal(generator);
            }
            preprocessor_fuzz_random_raw(generator, bracket ? "]" : "}");
        }
    }
}

// An invocation of a function-like name with its number of arguments (more for a variadic one, and rarely a wrong
// number), split across lines where `newline` allows.
static void preprocessor_fuzz_random_invocation(PreprocessorFuzzGenerator* generator, uint32_t name, uint32_t depth, bool newline)
{
    preprocessor_fuzz_random_name(generator, name);
    if (preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_gap(generator, newline);
    }
    preprocessor_fuzz_random_raw(generator, "(");
    uint32_t count = generator->parameters[name];
    count += generator->variadic[name] ? preprocessor_fuzz_random_below(generator, 3) : 0;
    if (preprocessor_fuzz_random_one_in(generator, 32))
    {
        count = count + 1 - 2 * (count != 0 && preprocessor_fuzz_random_one_in(generator, 2));
    }
    const bool commas = generator->comma_in_groups;
    for (uint32_t argument = 0; argument < (count != 0 ? count : 1); argument += 1)
    {
        generator->comma_in_groups = generator->variadic[name] && preprocessor_fuzz_random_one_in(generator, 2);
        if (argument != 0)
        {
            preprocessor_fuzz_random_gap(generator, newline);
            preprocessor_fuzz_random_raw(generator, ",");
        }
        if (preprocessor_fuzz_random_one_in(generator, 2))
        {
            preprocessor_fuzz_random_gap(generator, newline);
        }
        if (count != 0)
        {
            preprocessor_fuzz_random_argument(generator, depth, newline);
        }
    }
    generator->comma_in_groups = commas;
    if (preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_gap(generator, newline);
    }
    preprocessor_fuzz_random_raw(generator, ")");
}

// `count` tokens of a text line (or, with `arguments`, of an argument: no top-level commas): literals, names, and
// invocations of the function-like ones.
static void preprocessor_fuzz_random_tokens(PreprocessorFuzzGenerator* generator, uint32_t count, bool newline, bool arguments, uint32_t depth)
{
    for (uint32_t token = 0; token < count; token += 1)
    {
        preprocessor_fuzz_random_gap(generator, newline);
        const uint32_t choice = preprocessor_fuzz_random_below(generator, 10);
        if (choice < 4)
        {
            preprocessor_fuzz_random_literal(generator);
        }
        else if (choice < 9 || arguments)
        {
            // An invocation, or a parenthesized group below, is one level shallower: none at depth 0.
            const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
            if (generator->kinds[name] == 2 && depth != 0 && !preprocessor_fuzz_random_one_in(generator, 4))
            {
                preprocessor_fuzz_random_invocation(generator, name, depth - 1, newline);
            }
            else
            {
                preprocessor_fuzz_random_name(generator, name);
            }
        }
        else
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "," : "(");
            if (generator->last == '(')
            {
                if (depth != 0)
                {
                    preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 3), newline, false, depth - 1);
                }
                preprocessor_fuzz_random_raw(generator, ")");
            }
        }
    }
}

// A text line of up to eight tokens, which may break where an invocation's arguments do.
static void preprocessor_fuzz_random_text(PreprocessorFuzzGenerator* generator)
{
    preprocessor_fuzz_random_tokens(generator, 1 + preprocessor_fuzz_random_below(generator, 8), preprocessor_fuzz_random_one_in(generator, 3), false, BUSTER_PPF_RANDOM_DEPTH);
    preprocessor_fuzz_random_newline(generator);
}

static const char* const preprocessor_fuzz_random_conditions[] =
{
    "0", "1", "2", "-1", "0x7fffffffffffffff", "9223372036854775807", "18446744073709551615", "0u", "1u", "-1u", "0x10ULL", "010",
    "0b11", "'a'", "'\\377'", "'\\n'", "'ab'", "L'a'", "u'a'", "U'\\xff'", "u8'a'", "true", "false", "x", "__LINE__",
    "__STDC__", "__STDC_VERSION__", "__STDC_HOSTED__", "__STDC_EMBED_FOUND__", "__INCLUDE_LEVEL__",
};

static const char* const preprocessor_fuzz_random_queries[] =
{
    "__has_include(\"ppf_missing.h\")", "__has_include(<ppf_missing.h>)", "__has_include_next(<ppf_missing.h>)",
    "__has_attribute(noreturn)", "__has_attribute(__packed__)", "__has_attribute(ppf_none)", "__has_builtin(__builtin_expect)",
    "__has_builtin(ppf_none)", "__has_feature(c_static_assert)", "__has_feature(c_fixed_enum)", "__has_extension(c_atomic)",
    "__has_c_attribute(nodiscard)", "__has_c_attribute(gnu::unused)", "__has_c_attribute(ppf::none)", "__has_warning(\"-Wall\")",
    "__has_warning(\"-Wppf-none\")", "__is_identifier(int)", "__is_identifier(x)", "__is_identifier(_Bool)",
    "__is_target_arch(x86_64)", "__is_target_os(linux)", "__is_target_vendor(unknown)", "__is_target_environment(gnu)",
    "__building_module(x)", "__has_embed(\"ppf_missing.h\")", "__has_embed(\"/dev/null\")", "__has_embed(\"/dev/null\" limit(1) prefix(x))",
    "__has_embed(\"/dev/null\" ppf::none)", "__has_embed(<ppf_missing.h> if_empty(1))", "__has_embed(\"/dev/null\" clang::offset(0) __suffix__())",
};

// The attribute queries, which clang reads expanded a token at a time to the ')' that closes them.
static const char* const preprocessor_fuzz_random_attribute_queries[] =
{
    "__has_attribute(", "__has_c_attribute(", "__has_cpp_attribute(", "__has_declspec_attribute(",
};

// An attribute query whose operand has a shape of its own, on one line: two names, a scope (as it is and around a
// macro's name), a _Pragma before or after the name, a query in the operand, parentheses, or a comma.
static void preprocessor_fuzz_random_attribute_query(PreprocessorFuzzGenerator* generator)
{
    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_attribute_queries[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_attribute_queries))]);
    const uint32_t shape = preprocessor_fuzz_random_below(generator, 8);
    const uint32_t first = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
    const uint32_t second = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
    if (shape == 0 || shape == 1 || shape == 7)
    {
        static const char* const between[] = { " ", " :: ", "::", ", " };
        preprocessor_fuzz_random_name(generator, first);
        preprocessor_fuzz_random_raw(generator, between[shape == 7 ? 3 : shape == 0 ? 0 : 1 + preprocessor_fuzz_random_below(generator, 2)]);
        preprocessor_fuzz_random_name(generator, second);
    }
    else if (shape == 2)
    {
        preprocessor_fuzz_random_raw(generator, "_Pragma(\"ppf_query\") ");
        preprocessor_fuzz_random_name(generator, first);
    }
    else if (shape == 3)
    {
        preprocessor_fuzz_random_name(generator, first);
        preprocessor_fuzz_random_raw(generator, " _Pragma(\"ppf_query\")");
    }
    else if (shape == 4)
    {
        preprocessor_fuzz_random_raw(generator, "__has_attribute(");
        preprocessor_fuzz_random_name(generator, first);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else if (shape == 5)
    {
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_name(generator, first);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else
    {
        preprocessor_fuzz_random_raw(generator, "gnu::");
        preprocessor_fuzz_random_name(generator, first);
    }
    preprocessor_fuzz_random_raw(generator, ")");
}

// A #if expression of up to `depth` levels: constants, names (macros or not), `defined` in both forms, feature queries,
// and every operator. A divisor is `(e | 1)`, which isn't 0, unless rarely; a shift's right operand is a constant.
static void preprocessor_fuzz_random_expression(PreprocessorFuzzGenerator* generator, uint32_t depth)
{
    const uint32_t choice = depth == 0 ? preprocessor_fuzz_random_below(generator, 4) : preprocessor_fuzz_random_below(generator, 12);
    if (choice < 2)
    {
        const uint32_t constant = preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_conditions) + 4);
        if (constant < BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_conditions))
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_conditions[constant]);
        }
        else
        {
            preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        }
    }
    else if (choice < 3)
    {
        preprocessor_fuzz_random_raw(generator, "defined");
        const bool parenthesized = preprocessor_fuzz_random_one_in(generator, 2);
        preprocessor_fuzz_random_gap(generator, false);
        if (parenthesized)
        {
            preprocessor_fuzz_random_raw(generator, "(");
            preprocessor_fuzz_random_gap(generator, false);
        }
        else
        {
            preprocessor_fuzz_random_blank(generator, false);
        }
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        if (parenthesized)
        {
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, ")");
        }
    }
    else if (choice < 4)
    {
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            // A _Pragma in the line runs, and its pragma comes before the group.
            preprocessor_fuzz_random_raw(generator, "_Pragma(\"ppf_if\") ");
        }
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            preprocessor_fuzz_random_attribute_query(generator);
        }
        else if (preprocessor_fuzz_random_one_in(generator, 2))
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_queries[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_queries))]);
        }
        else
        {
            const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
            if (generator->kinds[name] == 2)
            {
                preprocessor_fuzz_random_invocation(generator, name, 1, false);
            }
            else
            {
                preprocessor_fuzz_random_name(generator, name);
            }
        }
    }
    else if (choice < 6)
    {
        static const char* const unary[] =
        {
            "+",
            "-",
            "~",
            "!",
        };
        preprocessor_fuzz_random_raw(generator, unary[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(unary))]);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_expression(generator, depth - 1);
    }
    else if (choice < 7)
    {
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_expression(generator, depth - 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else if (choice < 8)
    {
        preprocessor_fuzz_random_expression(generator, depth - 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, "?");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_expression(generator, depth - 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, ":");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_expression(generator, depth - 1);
    }
    else
    {
        static const char* const binary[] =
        {
            "*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&", "^", "|", "&&", "||", ",",
        };
        const uint32_t operator = preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(binary));
        preprocessor_fuzz_random_expression(generator, depth - 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, binary[operator]);
        preprocessor_fuzz_random_gap(generator, false);
        if ((operator == 1 || operator == 2) && !preprocessor_fuzz_random_one_in(generator, 16))
        {
            preprocessor_fuzz_random_raw(generator, "(");
            preprocessor_fuzz_random_expression(generator, depth - 1);
            preprocessor_fuzz_random_raw(generator, " | 1)");
        }
        else if ((operator == 5 || operator == 6) && !preprocessor_fuzz_random_one_in(generator, 16))
        {
            char amount[8];
            snprintf(amount, sizeof(amount), "%u", preprocessor_fuzz_random_below(generator, 64));
            preprocessor_fuzz_random_raw(generator, amount);
        }
        else
        {
            preprocessor_fuzz_random_expression(generator, depth - 1);
        }
    }
}

static void preprocessor_fuzz_random_statement(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme, uint32_t depth);

// A directive's start: `#` (or `%:`) at a line's start, gaps at times, and its name.
static void preprocessor_fuzz_random_directive_name(PreprocessorFuzzGenerator* generator, const char* name)
{
    if (preprocessor_fuzz_random_one_in(generator, 8))
    {
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? " " : "/**/");
    }
    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 16) ? "%:" : "#");
    if (preprocessor_fuzz_random_one_in(generator, 4))
    {
        preprocessor_fuzz_random_gap(generator, false);
    }
    preprocessor_fuzz_random_raw(generator, name);
}

// A group's statements, as many as `count`, one level deeper.
static void preprocessor_fuzz_random_group(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme, uint32_t depth)
{
    const uint32_t count = preprocessor_fuzz_random_below(generator, 3);
    for (uint32_t statement = 0; statement < count; statement += 1)
    {
        preprocessor_fuzz_random_statement(generator, theme, depth + 1);
    }
    if (count == 0 || preprocessor_fuzz_random_one_in(generator, 2))
    {
        // A token the group gives when it is taken.
        char marker[32];
        snprintf(marker, sizeof(marker), "ppf_g%u", generator->groups);
        generator->groups += 1;
        preprocessor_fuzz_random_raw(generator, marker);
        preprocessor_fuzz_random_newline(generator);
    }
}

// A conditional: #if, #ifdef or #ifndef, then #elif, #elifdef or #elifndef groups, #else at times, and #endif. In a
// group that isn't taken, a line of what clang doesn't read there at times: an unknown directive, or a bad expression.
static void preprocessor_fuzz_random_conditional(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme, uint32_t depth)
{
    const uint32_t opener = preprocessor_fuzz_random_below(generator, 4);
    preprocessor_fuzz_random_directive_name(generator, opener < 2 ? "if" : opener == 2 ? "ifdef" : "ifndef");
    preprocessor_fuzz_random_blank(generator, false);
    if (opener < 2)
    {
        preprocessor_fuzz_random_expression(generator, 1 + preprocessor_fuzz_random_below(generator, 4));
    }
    else
    {
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
    }
    preprocessor_fuzz_random_newline(generator);
    preprocessor_fuzz_random_group(generator, theme, depth);
    const uint32_t continuations = preprocessor_fuzz_random_below(generator, 3);
    for (uint32_t continuation = 0; continuation < continuations; continuation += 1)
    {
        const uint32_t kind = preprocessor_fuzz_random_below(generator, 4);
        preprocessor_fuzz_random_directive_name(generator, kind < 2 ? "elif" : kind == 2 ? "elifdef" : "elifndef");
        preprocessor_fuzz_random_blank(generator, false);
        if (kind < 2)
        {
            preprocessor_fuzz_random_expression(generator, 1 + preprocessor_fuzz_random_below(generator, 3));
        }
        else
        {
            preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        }
        preprocessor_fuzz_random_newline(generator);
        if (preprocessor_fuzz_random_one_in(generator, 6))
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "#ppf_unknown x\n" : "#if 1 +\n#endif\n");
        }
        preprocessor_fuzz_random_group(generator, theme, depth);
    }
    if (preprocessor_fuzz_random_one_in(generator, 2))
    {
        preprocessor_fuzz_random_directive_name(generator, "else");
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            preprocessor_fuzz_random_raw(generator, " extra");
        }
        preprocessor_fuzz_random_newline(generator);
        preprocessor_fuzz_random_group(generator, theme, depth);
    }
    preprocessor_fuzz_random_directive_name(generator, "endif");
    if (preprocessor_fuzz_random_one_in(generator, 8))
    {
        preprocessor_fuzz_random_raw(generator, " /* c */ extra");
    }
    preprocessor_fuzz_random_newline(generator);
}

// Pragmas, as clang -E prints them (most as they are) or acts on them. A diagnostic pragma's severity other than
// `ignored` only names a group no preprocessing diagnostic is in, so that it changes nothing clang reports; a push is
// popped before the case ends (preprocessor_fuzz_random_program), so that it can't reach the next case in a batch.
static const char* const preprocessor_fuzz_random_pragmas[] =
{
    "once", "once extra", "GCC system_header", "clang system_header", "GCC warning \"ppf\"", "GCC warning (\"ppf\")", "GCC warning", "GCC error \"ppf\"",
    "GCC error", "message(\"ppf\")", "message \"ppf\"", "message(\"a\" \"b\")", "message(\"ppf\") x", "message x", "message(L\"ppf\")",
    "message(\"a\\x41\\n\")", "STDC FP_CONTRACT ON", "STDC FENV_ACCESS OFF", "STDC CX_LIMITED_RANGE DEFAULT", "ppf_unknown x y",
    "GCC dependency \"ppf_missing.h\"", "GCC dependency", "clang dependency <ppf_missing.h>", "pack(push, 1)", "pack(pop)", "weak ppf_w",
    "redefine_extname a b", "GCC visibility push(default)", "GCC visibility pop", "clang diagnostic push", "clang diagnostic pop",
    "GCC diagnostic push", "GCC diagnostic pop", "clang diagnostic pop extra", "clang diagnostic", "clang diagnostic remark \"-Wall\"",
    "clang diagnostic ignored \"-Wunused-macros\"", "GCC diagnostic ignored \"-W\" \"unused-macros\"", "clang diagnostic ignored \"-Wppf-none\"",
    "clang diagnostic warning \"-Wunused-variable\"", "clang diagnostic error \"-Wunused-variable\"", "clang diagnostic fatal \"-Wshadow\"",
    "clang diagnostic ignored \"-Rpass\"", "clang diagnostic ignored \"-Weverything\"", "clang diagnostic ignored \"-W\"", "clang diagnostic ignored",
    "clang diagnostic ignored L\"-Wall\"", "clang diagnostic ignored \"-Wall\" x", "region", "endregion", "region x", "mark", "mark - ppf", "",
    "omp parallel", "clang poison", "clang max_tokens_here 1", "clang loop vectorize(enable)", "clang assume_nonnull end", "clang arc_cf_code_audited end",
    "clang unsafe_buffer_usage end", "clang assume_nonnull", "clang deprecated", "clang final(x)", "clang restrict_expansion(1)", "push_macro(x)",
    "push_macro(\"x\"", "pop_macro", "ppf_unknown caf\\u00e9 x\\u0301", "omp parallel \\U000000E9x", "STDC FP_CONTRACT x\\u0301",
};

// A directive other than a conditional or a #define: #undef, #line, #pragma (and push_macro, pop_macro and poison),
// #ident, #sccs, #warning, #error, #include of a missing file, an unknown or a null directive.
static void preprocessor_fuzz_random_directive(PreprocessorFuzzGenerator* generator)
{
    const uint32_t choice = preprocessor_fuzz_random_below(generator, 24);
    if (choice < 6)
    {
        preprocessor_fuzz_random_directive_name(generator, "undef");
        preprocessor_fuzz_random_blank(generator, false);
        const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
        preprocessor_fuzz_random_name(generator, name);
        generator->kinds[name] = 0;
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            preprocessor_fuzz_random_raw(generator, " extra");
        }
    }
    else if (choice < 9)
    {
        preprocessor_fuzz_random_directive_name(generator, "line");
        preprocessor_fuzz_random_blank(generator, false);
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            preprocessor_fuzz_random_raw(generator, "_Pragma(\"ppf_line\") ");
        }
        static const char* const lines[] =
        {
            // Not 4294967295 or 4294967294, after which clang's diagnostics' carets run to gigabytes of spaces (a 32-bit
            // line's overflow); 4294967296 is too big for clang.
            "1", "7", "100", "0", "2147483647", "4294967296", "010", "1\\\n0",
        };
        preprocessor_fuzz_random_raw(generator, lines[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(lines))]);
        if (preprocessor_fuzz_random_one_in(generator, 2))
        {
            char name[96];
            snprintf(name, sizeof(name), " \"%.*s.c\"", (int)generator->prefix.length - 1, generator->prefix.pointer);
            preprocessor_fuzz_random_raw(generator, name);
        }
    }
    else if (choice < 15)
    {
        const uint32_t pragma = preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_pragmas) + 4);
        // A region's text, a statement of its own.
        generator->last = '\n';
        preprocessor_fuzz_random_directive_name(generator, "pragma");
        preprocessor_fuzz_random_blank(generator, false);
        if (pragma < BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_pragmas))
        {
            const char* text = preprocessor_fuzz_random_pragmas[pragma];
            preprocessor_fuzz_random_raw(generator, text);
            // A dependency on a missing file is a fatal error too.
            generator->fatal |= strstr(text, "dependency \"") != 0 || strstr(text, "dependency <") != 0;
            generator->pushes += strstr(text, "diagnostic push") != 0;
        }
        else
        {
            // push_macro, pop_macro or poison of a name of the case's (a poisoned name is never used again), omp with
            // a name to expand, a message from a macro, a note on a name, or a region begun and ended.
            const uint32_t name = 5 + preprocessor_fuzz_random_below(generator, 3);
            const uint32_t action = (pragma - BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_pragmas)) * 2 + preprocessor_fuzz_random_below(generator, 2);
            if (action < 3)
            {
                preprocessor_fuzz_random_raw(generator, action == 0 ? "pop_macro(\"" : "push_macro(\"");
                preprocessor_fuzz_random_name(generator, name);
                preprocessor_fuzz_random_raw(generator, "\")");
                generator->kinds[name] = action == 0 ? 3 : generator->kinds[name];
            }
            else if (action == 3)
            {
                // A name of the case's own that nothing else uses (the cleanup doesn't), at times used after: read from
                // the file, in a directive's line, among a macro's arguments or made by a paste, which are errors.
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "GCC poison " : "clang poison ");
                preprocessor_fuzz_random_raw(generator, generator->poison_name);
                const uint32_t use = preprocessor_fuzz_random_below(generator, 8);
                char line[3 * (BUSTER_PPF_GENERATOR_PREFIX_BYTES + 16) + 64];
                const int prefix = (int)generator->prefix.length;
                const char* prefix_text = generator->prefix.pointer;
                if (use == 4)
                {
                    snprintf(line, sizeof(line), "\n%s", generator->poison_name);
                    preprocessor_fuzz_random_raw(generator, line);
                }
                else if (use == 5)
                {
                    snprintf(line, sizeof(line), "\n#ifdef %s\n#endif", generator->poison_name);
                    preprocessor_fuzz_random_raw(generator, line);
                }
                else if (use == 6)
                {
                    snprintf(line, sizeof(line), "\n#define %.*sq(x) 1\n%.*sq(%s)\n#undef %.*sq", prefix, prefix_text, prefix, prefix_text, generator->poison_name, prefix,
                             prefix_text);
                    preprocessor_fuzz_random_raw(generator, line);
                }
                else if (use == 7)
                {
                    snprintf(line, sizeof(line), "\n#define %.*sq(a, b) a ## b\n%.*sq(%.*spoi, soned)\n#undef %.*sq", prefix, prefix_text, prefix, prefix_text, prefix,
                             prefix_text, prefix, prefix_text);
                    preprocessor_fuzz_random_raw(generator, line);
                }
            }
            else if (action == 4)
            {
                preprocessor_fuzz_random_raw(generator, "omp parallel num_threads(");
                preprocessor_fuzz_random_name(generator, name);
                preprocessor_fuzz_random_raw(generator, ")");
            }
            else if (action == 5)
            {
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "message(" : "GCC warning ");
                preprocessor_fuzz_random_name(generator, name);
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? ")" : "");
            }
            else if (action == 6)
            {
                static const char* const notes[] =
                {
                    "clang deprecated(",
                    "clang final(",
                    "clang restrict_expansion(",
                };
                preprocessor_fuzz_random_raw(generator, notes[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(notes))]);
                preprocessor_fuzz_random_name(generator, name);
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? ")" : ", \"ppf\")");
            }
            else
            {
                // A region begun and ended in this line's place (so that it can't reach the next case in a batch).
                static const char* const regions[] =
                {
                    "clang assume_nonnull ",
                    "clang arc_cf_code_audited ",
                    "clang unsafe_buffer_usage ",
                };
                const char* region = regions[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(regions))];
                preprocessor_fuzz_random_raw(generator, region);
                preprocessor_fuzz_random_raw(generator, "begin\n");
                preprocessor_fuzz_random_text(generator);
                preprocessor_fuzz_random_raw(generator, "#pragma ");
                preprocessor_fuzz_random_raw(generator, region);
                preprocessor_fuzz_random_raw(generator, "end");
            }
        }
    }
    else if (choice < 17)
    {
        static const char* const idents[] =
        {
            "\"ppf\"",
            "\"ppf\"",
            "ppf",
            "",
            "L\"ppf\"",
            "u8\"ppf\"",
            "\"a\" \"b\"",
        };
        preprocessor_fuzz_random_directive_name(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "ident" : "sccs");
        preprocessor_fuzz_random_blank(generator, false);
        if (preprocessor_fuzz_random_one_in(generator, 4))
        {
            preprocessor_fuzz_random_name(generator, 5 + preprocessor_fuzz_random_below(generator, 3));
        }
        else
        {
            preprocessor_fuzz_random_raw(generator, idents[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(idents))]);
        }
    }
    else if (choice < 19)
    {
        preprocessor_fuzz_random_directive_name(generator, "warning");
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? " ppf 'text" : " \"ppf\"");
    }
    else if (choice < 20)
    {
        preprocessor_fuzz_random_directive_name(generator, preprocessor_fuzz_random_one_in(generator, 4) ? "error" : "ppf_unknown");
        preprocessor_fuzz_random_raw(generator, " ppf");
    }
    else if (choice < 21)
    {
        // A missing file is a fatal error, after which clang reads nothing more: the case runs alone.
        preprocessor_fuzz_random_directive_name(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "include" : "include_next");
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? " \"ppf_missing.h\"" : " <ppf_missing.h>");
        generator->fatal = true;
    }
    else if (choice < 22)
    {
        preprocessor_fuzz_random_directive_name(generator, "assert");
        preprocessor_fuzz_random_raw(generator, " ppf(x)");
    }
    else
    {
        // The null directive, alone or with a comment.
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "#" : "# /* c */");
    }
    preprocessor_fuzz_random_newline(generator);
}

static const char* const preprocessor_fuzz_random_pragma_operands[] =
{
    "\"ppf_unknown x\"", "\"message(\\\"ppf\\\")\"", "\"STDC FP_CONTRACT ON\"", "L\"ppf_unknown\"", "u8\"ppf_unknown\"",
    "\"once\"", "\"GCC warning \\\"ppf\\\"\"", "\"GCC error \\\"ppf\\\"\"", "\"\"", "\"pack(push, 2)\"",
    "\"pack(pop)\"", "\"ppf \\\\ \\\"q\\\"\"", "\"clang diagnostic push\"", "\"clang diagnostic pop\"",
    "\"ppf_unknown caf\\\\u00e9\"", "\"omp x\\\\u0301 y\"",
};

// _Pragma with a string, from a macro, with gaps around its parentheses and the string split by splices at times; or
// in an argument that is expanded before its substitution, where it doesn't run (clang only checks it), so that its
// tokens are stringized or pasted, and it runs when the substitution is rescanned; or poisoning a name that a macro
// defined before gives, whose expansion isn't an error.
static void preprocessor_fuzz_random_pragma(PreprocessorFuzzGenerator* generator)
{
    const uint32_t choice = preprocessor_fuzz_random_below(generator, 7);
    const char* operand = preprocessor_fuzz_random_pragma_operands[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_pragma_operands))];
    generator->pushes += strstr(operand, "diagnostic push") != 0;
    if (choice < 2)
    {
        preprocessor_fuzz_random_raw(generator, "_Pragma");
        preprocessor_fuzz_random_gap(generator, true);
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_gap(generator, true);
        preprocessor_fuzz_random_raw(generator, operand);
        preprocessor_fuzz_random_gap(generator, true);
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else if (choice == 6)
    {
        char line[3 * (BUSTER_PPF_GENERATOR_PREFIX_BYTES + 16) + 64];
        const int prefix = (int)generator->prefix.length;
        snprintf(line, sizeof(line), "#define %.*sr %s\n_Pragma(\"GCC poison %s\")\n%.*sr\n#undef %.*sr", prefix, generator->prefix.pointer, generator->poison_name,
                 generator->poison_name, prefix, generator->prefix.pointer, prefix, generator->prefix.pointer);
        preprocessor_fuzz_random_raw(generator, line);
    }
    else if (choice >= 4)
    {
        // `inner` stringizes or pastes what `outer` gives it: `outer`'s argument, expanded. The paste runs the pragma
        // once more (the stringized one doesn't), and its pushes are counted for the case's end.
        const bool paste = choice == 5;
        generator->pushes += paste && strstr(operand, "diagnostic push") != 0;
        const uint32_t inner = 5 + preprocessor_fuzz_random_below(generator, 3);
        const uint32_t outer = 5 + (inner - 5 + 1 + preprocessor_fuzz_random_below(generator, 2)) % 3;
        preprocessor_fuzz_random_raw(generator, "#define ");
        preprocessor_fuzz_random_name(generator, inner);
        preprocessor_fuzz_random_raw(generator, paste ? "(a, b) a ## b\n#define " : "(x) #x\n#define ");
        preprocessor_fuzz_random_name(generator, outer);
        preprocessor_fuzz_random_raw(generator, paste ? "(x, y) " : "(x) ");
        preprocessor_fuzz_random_name(generator, inner);
        preprocessor_fuzz_random_raw(generator, paste ? "(x, y) [x]\n" : "(x) [x]\n");
        preprocessor_fuzz_random_name(generator, outer);
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 2), false, false, 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, "_Pragma");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, operand);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, ")");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_raw(generator, paste ? "ppf_l, ppf_r)" : "x)");
        generator->kinds[inner] = 2;
        generator->parameters[inner] = paste ? 2 : 1;
        generator->variadic[inner] = false;
        generator->kinds[outer] = 2;
        generator->parameters[outer] = paste ? 2 : 1;
        generator->variadic[outer] = false;
    }
    else
    {
        // A macro whose expansion is the operator: stringized, or a parameter that the argument fills.
        const uint32_t name = 5 + preprocessor_fuzz_random_below(generator, 3);
        preprocessor_fuzz_random_raw(generator, "#define ");
        preprocessor_fuzz_random_name(generator, name);
        preprocessor_fuzz_random_raw(generator, choice == 2 ? "(x) _Pragma(#x)\n" : "(x) _Pragma(x)\n");
        preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 2), false, false, 1);
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_name(generator, name);
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_raw(generator, choice == 2 ? "ppf_unknown(\"s\") x" : operand);
        preprocessor_fuzz_random_raw(generator, ")");
        generator->kinds[name] = 2;
        generator->parameters[name] = 1;
        generator->variadic[name] = false;
    }
    preprocessor_fuzz_random_tokens(generator, preprocessor_fuzz_random_below(generator, 2), false, false, 1);
    preprocessor_fuzz_random_newline(generator);
}

static const char* const preprocessor_fuzz_random_builtins[] =
{
    "__LINE__", "__FILE__", "__FILE_NAME__", "__BASE_FILE__", "__INCLUDE_LEVEL__", "__STDC__", "__STDC_VERSION__", "__STDC_HOSTED__",
    "__STDC_UTF_16__", "__STDC_EMBED_EMPTY__", "__DATE__", "__TIME__", "__TIMESTAMP__", "__COUNTER__", "__has_feature(c_atomic)",
    "__has_extension(c_generic_selections)", "__has_attribute(cold)", "__has_builtin(__builtin_trap)", "__is_identifier(if)",
    "__has_c_attribute(deprecated)", "__has_warning(\"-Wextra\")", "__is_target_os(linux)", "__has_include(\"ppf_missing.h\")",
    "_Pragma(\"ppf\")", "__clang__", "__BUSTER__", "__has_builtin(__is_target_os)", "__has_builtin(__has_feature)", "__has_builtin(__LINE__)",
};

// The feature queries a builtin line gives a name of the case's to, a macro at times: clang expands only an attribute
// query's operand.
static const char* const preprocessor_fuzz_random_named_queries[] =
{
    "__has_builtin(", "__has_feature(", "__has_extension(", "__has_attribute(", "__has_c_attribute(", "__is_identifier(", "__is_target_os(",
    "__has_warning(",
};

// An attribute query on lines of its own: closed by a macro's ')' (with tokens after it in the expansion), given its
// scope by a macro, or with a directive in its operand, which runs there.
static void preprocessor_fuzz_random_attribute_lines(PreprocessorFuzzGenerator* generator)
{
    const int prefix = (int)generator->prefix.length;
    const char* const text = generator->prefix.pointer;
    const char* query = preprocessor_fuzz_random_attribute_queries[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_attribute_queries))];
    const uint32_t shape = preprocessor_fuzz_random_below(generator, 4);
    char line[512];
    if (shape == 0)
    {
        static const char* const closers[] = { ")", ") + 1", ") )", ") (", "x )" };
        snprintf(line, sizeof(line), "#define %.*srp %s\n%s", prefix, text, closers[preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(closers))], query);
        preprocessor_fuzz_random_raw(generator, line);
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        snprintf(line, sizeof(line), " %.*srp", prefix, text);
        preprocessor_fuzz_random_raw(generator, line);
    }
    else if (shape == 1)
    {
        snprintf(line, sizeof(line), "#define %.*ssc gnu::\n%s%.*ssc ", prefix, text, query, prefix, text);
        preprocessor_fuzz_random_raw(generator, line);
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        preprocessor_fuzz_random_raw(generator, ")");
    }
    else if (shape == 2)
    {
        preprocessor_fuzz_random_raw(generator, query);
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        snprintf(line, sizeof(line), "\n#define %.*sdy 1\n) %.*sdy", prefix, text, prefix, text);
        preprocessor_fuzz_random_raw(generator, line);
    }
    else
    {
        preprocessor_fuzz_random_raw(generator, query);
        preprocessor_fuzz_random_raw(generator, "\n#if 1\n");
        preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
        preprocessor_fuzz_random_raw(generator, "\n#endif\n)");
    }
    preprocessor_fuzz_random_newline(generator);
}

// A line of builtins, plain and in macro arguments that span lines (their __LINE__ is the invocation's end's line).
static void preprocessor_fuzz_random_builtin_line(PreprocessorFuzzGenerator* generator)
{
    if (preprocessor_fuzz_random_one_in(generator, 4))
    {
        preprocessor_fuzz_random_attribute_lines(generator);
    }
    const uint32_t count = 1 + preprocessor_fuzz_random_below(generator, 4);
    for (uint32_t i = 0; i < count; i += 1)
    {
        preprocessor_fuzz_random_gap(generator, true);
        const uint32_t builtin = preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_builtins));
        generator->counter |= builtin == 13;
        if (preprocessor_fuzz_random_one_in(generator, 8))
        {
            preprocessor_fuzz_random_attribute_query(generator);
        }
        else if (preprocessor_fuzz_random_one_in(generator, 4))
        {
            const uint32_t query = preprocessor_fuzz_random_below(generator, BUSTER_ARRAY_LENGTH(preprocessor_fuzz_random_named_queries));
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_named_queries[query]);
            preprocessor_fuzz_random_name(generator, preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET));
            preprocessor_fuzz_random_raw(generator, ")");
        }
        else if (preprocessor_fuzz_random_one_in(generator, 2))
        {
            const uint32_t name = preprocessor_fuzz_random_below(generator, BUSTER_PPF_GENERATOR_ALPHABET);
            if (generator->kinds[name] == 2)
            {
                preprocessor_fuzz_random_name(generator, name);
                preprocessor_fuzz_random_raw(generator, "(\n");
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_builtins[builtin]);
                for (uint32_t argument = 1; argument < generator->parameters[name]; argument += 1)
                {
                    preprocessor_fuzz_random_raw(generator, ",\n__LINE__");
                }
                preprocessor_fuzz_random_raw(generator, "\n)");
            }
            else
            {
                preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_builtins[builtin]);
            }
        }
        else
        {
            preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_builtins[builtin]);
        }
    }
    preprocessor_fuzz_random_newline(generator);
}

// A line whose tokens spell the lexer's corner cases, plainly, stringized and pasted.
static void preprocessor_fuzz_random_lexemes(PreprocessorFuzzGenerator* generator)
{
    const uint32_t name = 5 + preprocessor_fuzz_random_below(generator, 3);
    const bool stringized = generator->lexemes != 0 ? generator->lexemes == 2 : preprocessor_fuzz_random_one_in(generator, 2);
    preprocessor_fuzz_random_raw(generator, "#define ");
    preprocessor_fuzz_random_name(generator, name);
    preprocessor_fuzz_random_raw(generator, stringized ? "(...) #__VA_ARGS__\n" : "(a, b) a ## b\n");
    generator->kinds[name] = 2;
    generator->parameters[name] = stringized ? 0 : 2;
    generator->variadic[name] = stringized;
    const uint32_t count = 1 + preprocessor_fuzz_random_below(generator, 4);
    for (uint32_t i = 0; i < count; i += 1)
    {
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_name(generator, name);
        preprocessor_fuzz_random_raw(generator, "(");
        preprocessor_fuzz_random_literal(generator);
        if (!stringized || preprocessor_fuzz_random_one_in(generator, 2))
        {
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_raw(generator, ",");
            preprocessor_fuzz_random_gap(generator, false);
            preprocessor_fuzz_random_literal(generator);
        }
        preprocessor_fuzz_random_raw(generator, ")");
        preprocessor_fuzz_random_gap(generator, false);
        preprocessor_fuzz_random_literal(generator);
    }
    if (preprocessor_fuzz_random_one_in(generator, 4))
    {
        // An unterminated literal ends the line.
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? " 'x" : " \"x");
    }
    preprocessor_fuzz_random_newline(generator);
}

// Directives among a macro's arguments, which clang runs there: a definition, an #undef, a conditional or a #line,
// each on a line of its own between two arguments.
static void preprocessor_fuzz_random_embedded(PreprocessorFuzzGenerator* generator)
{
    const uint32_t name = 5 + preprocessor_fuzz_random_below(generator, 3);
    const uint32_t other = preprocessor_fuzz_random_below(generator, 5);
    preprocessor_fuzz_random_raw(generator, "#define ");
    preprocessor_fuzz_random_name(generator, name);
    preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "(a, b) [a|b]\n" : "(a, b) #a #b\n");
    generator->kinds[name] = 2;
    generator->parameters[name] = 2;
    generator->variadic[name] = false;
    preprocessor_fuzz_random_name(generator, name);
    preprocessor_fuzz_random_raw(generator, "(");
    preprocessor_fuzz_random_name(generator, other);
    preprocessor_fuzz_random_raw(generator, "\n");
    const uint32_t choice = preprocessor_fuzz_random_below(generator, 5);
    if (choice == 0)
    {
        preprocessor_fuzz_random_define(generator, BUSTER_PPF_THEME_MACROS);
    }
    else if (choice == 1)
    {
        preprocessor_fuzz_random_raw(generator, "#undef ");
        preprocessor_fuzz_random_name(generator, other);
        preprocessor_fuzz_random_raw(generator, "\n");
        generator->kinds[other] = 0;
    }
    else if (choice == 2)
    {
        preprocessor_fuzz_random_raw(generator, "#if ");
        preprocessor_fuzz_random_expression(generator, 2);
        preprocessor_fuzz_random_raw(generator, "\nppf_then\n#else\nppf_else\n#endif\n");
    }
    else if (choice == 3)
    {
        preprocessor_fuzz_random_raw(generator, "#line 50\n__LINE__\n");
    }
    else
    {
        preprocessor_fuzz_random_raw(generator, preprocessor_fuzz_random_one_in(generator, 2) ? "#pragma ppf_unknown\n" : "#ifdef ppf_none\n#endif\n");
    }
    preprocessor_fuzz_random_raw(generator, ",");
    preprocessor_fuzz_random_name(generator, other);
    preprocessor_fuzz_random_raw(generator, ")\n");
}

// One statement of a random program; a theme makes its own kind likelier.
static void preprocessor_fuzz_random_statement(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme, uint32_t depth)
{
    uint32_t choice = preprocessor_fuzz_random_below(generator, 16);
    const bool themed = preprocessor_fuzz_random_one_in(generator, 2);
    choice = themed && theme == BUSTER_PPF_THEME_CONDITIONS ? 10 : choice;
    choice = themed && theme == BUSTER_PPF_THEME_DIRECTIVES ? 12 : choice;
    choice = themed && theme == BUSTER_PPF_THEME_PRAGMAS ? 13 : choice;
    choice = themed && theme == BUSTER_PPF_THEME_BUILTINS ? 14 : choice;
    choice = themed && theme == BUSTER_PPF_THEME_LEXEMES ? 15 : choice;
    // No conditional two levels down, which bounds the nesting (and the recursion through preprocessor_fuzz_random_group).
    choice = choice >= 10 && choice < 12 && depth >= 2 ? 6 : choice;
    if (choice < 5 && (theme == BUSTER_PPF_THEME_VARIADIC || theme == BUSTER_PPF_THEME_STRINGS) && preprocessor_fuzz_random_one_in(generator, 3))
    {
        preprocessor_fuzz_random_spacing(generator);
    }
    else if (choice < 5)
    {
        preprocessor_fuzz_random_define(generator, theme);
    }
    else if (choice < 10)
    {
        preprocessor_fuzz_random_text(generator);
    }
    else if (choice < 12)
    {
        preprocessor_fuzz_random_conditional(generator, theme, depth);
    }
    else if (choice < 13)
    {
        preprocessor_fuzz_random_directive(generator);
    }
    else if (choice < 14)
    {
        preprocessor_fuzz_random_pragma(generator);
    }
    else if (choice < 15)
    {
        preprocessor_fuzz_random_builtin_line(generator);
    }
    else
    {
        preprocessor_fuzz_random_lexemes(generator);
    }
}

// A random program of family BUSTER_PPF_TEMPLATE_FAMILIES + theme, after a comment that names it (the self-tests'
// witness). Its first statement is the family's own construct.
static void preprocessor_fuzz_random_program(PreprocessorFuzzGenerator* generator, PreprocessorFuzzTheme theme)
{
    for (uint32_t name = 0; name < BUSTER_PPF_GENERATOR_ALPHABET; name += 1)
    {
        generator->kinds[name] = 0;
    }
    preprocessor_fuzz_format(generator->buffer, "/* ppf_random %u */\n", (uint32_t)theme);
    snprintf(generator->poison_name, sizeof(generator->poison_name), "%.*spoisoned", (int)generator->prefix.length, generator->prefix.pointer);
    generator->last = '\n';
    generator->splice_tokens = theme == BUSTER_PPF_THEME_SPLICES;
    switch (theme)
    {
        case BUSTER_PPF_THEME_CONDITIONS:
        {
            preprocessor_fuzz_random_conditional(generator, theme, 0);
        } break;
        case BUSTER_PPF_THEME_DIRECTIVES:
        {
            preprocessor_fuzz_random_directive(generator);
        } break;
        case BUSTER_PPF_THEME_PASTES:
        case BUSTER_PPF_THEME_STRINGS:
        case BUSTER_PPF_THEME_LEXEMES:
        {
            generator->lexemes = theme == BUSTER_PPF_THEME_PASTES ? 1 : theme == BUSTER_PPF_THEME_STRINGS ? 2 : 0;
            preprocessor_fuzz_random_lexemes(generator);
            generator->lexemes = 0;
        } break;
        case BUSTER_PPF_THEME_PRAGMAS:
        {
            preprocessor_fuzz_random_pragma(generator);
        } break;
        case BUSTER_PPF_THEME_BUILTINS:
        {
            preprocessor_fuzz_random_builtin_line(generator);
        } break;
        case BUSTER_PPF_THEME_EMBEDDED:
        {
            preprocessor_fuzz_random_embedded(generator);
        } break;
        case BUSTER_PPF_THEME_VARIADIC:
        {
            if (preprocessor_fuzz_random_one_in(generator, 2))
            {
                preprocessor_fuzz_random_spacing(generator);
            }
            else
            {
                preprocessor_fuzz_random_define(generator, theme);
            }
        } break;
        default:
        {
            preprocessor_fuzz_random_define(generator, theme);
        } break;
    }
    const uint32_t statements = 2 + preprocessor_fuzz_random_below(generator, BUSTER_PPF_RANDOM_STATEMENTS);
    for (uint32_t statement = 0; statement < statements; statement += 1)
    {
        preprocessor_fuzz_random_statement(generator, theme, 0);
        if (theme == BUSTER_PPF_THEME_EMBEDDED && preprocessor_fuzz_random_one_in(generator, 3))
        {
            preprocessor_fuzz_random_embedded(generator);
        }
    }
    generator->splice_tokens = false;
    // As many pops as pushes were written, taken or not: a pop past the pushes taken pops nothing.
    for (uint32_t push = 0; push < generator->pushes; push += 1)
    {
        preprocessor_fuzz_format(generator->buffer, "#pragma clang diagnostic pop\n");
    }
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
            // A rejecting case has one fault: another rejection must not hide incorrect acceptance of this form.
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) ## @a\n"));
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
                "#if ('A' != '\\n') && (0x10ULL >= 010u) && (-1 < 0)\n$1\n#endif\n"
                "#if ($0 > 0) && ($1 <= 31) && ($2 == $2)\n$2\n#endif\n"));
            preprocessor_fuzz_generator_arithmetic(generator);
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
        case 32:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a ##\n"));
        } break;
        case 33:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) @a #\n"));
        } break;
        case 34:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a) # 1\n"));
        } break;
        case 35:
        {
            preprocessor_fuzz_generator_text(generator, BUSTER_PPF_GENERATOR_TEXT("#define @f(@a,@b) @a ## @b\n@f(+,*)\n"));
        } break;
        default:
        {
            preprocessor_fuzz_random_program(generator, (PreprocessorFuzzTheme)(family - BUSTER_PPF_TEMPLATE_FAMILIES));
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
            .c23 = c23,
        };
        generator.rotation = preprocessor_fuzz_generator_random(&generator) % BUSTER_PPF_GENERATOR_ALPHABET;
        for (uint32_t number = 0; number < 3; number += 1)
        {
            generator.numbers[number] = 1 + preprocessor_fuzz_generator_random(&generator) % 31;
        }
        const uint32_t family = preprocessor_fuzz_family(id);
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
                .isolated = family == 29 || generator.counter || generator.fatal,
            };
        }
    }
    if (!buffer.valid)
    {
        arena_pop_to(arena, saved_length);
    }
    return buffer.valid;
}
