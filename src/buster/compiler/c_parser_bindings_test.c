// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-level checks for #128. Include after the parser tests and c_parser_bindings.c. The handwritten fixtures
// name declaration tokens by their occurrence in the source; the random generator keeps a separate lexical scope
// stack while writing C. Neither expected answer reads the adapter's events. Every successful source also compares
// the producer's immediate answers, fresh scalar replay and the batch on every token, including unresolved names.

#include <stdarg.h>

typedef struct ParseBindingTest ParseBindingTest;
struct ParseBindingTest
{
    uint64_t checks;
    uint64_t identifiers;
    uint64_t walks;
    uint64_t walked_uses;
    uint64_t fallback_uses;
    uint32_t failures;
    uint32_t files;
    uint32_t rejected;
    uint32_t reports;
    bool keep;
};

static void parse_binding_test_check(ParseBindingTest* test, bool condition, const char* path, const char* message, uint32_t token)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->reports < 40)
        {
            fprintf(stderr, "%s: %s at token %u\n", path, message, token);
            test->reports += 1;
        }
    }
}

typedef struct ParseBindingExpected ParseBindingExpected;
struct ParseBindingExpected
{
    const char* name;
    uint32_t count;
    // One-based occurrence of the declaration; zero is unresolved, -1 has no ordinary/tag/label binding.
    const int32_t* declarations;
    const uint8_t* kinds;
};

typedef struct ParseBindingFixture ParseBindingFixture;
struct ParseBindingFixture
{
    const char* source;
    const char* name;
    uint32_t count;
    int32_t declarations[16];
    uint8_t kinds[16];
    bool check_kinds;
    bool clang_rejects;
};

static const ParseBindingFixture parse_binding_fixtures[] =
{
    {
        .source = "int x; void f(void) { int x=x; { int x=x; } x=1; } int g(void) { return x; }\n",
        .name = "x",
        .count = 7,
        .declarations = { 1, 2, 2, 4, 4, 2, 1 },
        .kinds = { C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT },
        .check_kinds = true,
    },
    {
        .source = "typedef int T; void f(void) { int T[sizeof((T)0)]; T[0]=1; } T g;\n",
        .name = "T",
        .count = 5,
        .declarations = { 1, 2, 1, 2, 1 },
        .kinds = { C_BINDING_TYPEDEF, C_BINDING_OBJECT, C_BINDING_TYPEDEF, C_BINDING_OBJECT, C_BINDING_TYPEDEF },
        .check_kinds = true,
    },
    {
        .source = "typedef int T, *P; P x;\n",
        .name = "P",
        .count = 2,
        .declarations = { 1, 1 },
    },
    {
        .source = "int E; void f(void) { enum { E=sizeof E, F=E }; int x=F; }\n",
        .name = "E",
        .count = 4,
        .declarations = { 1, 2, 1, 2 },
        .kinds = { C_BINDING_OBJECT, C_BINDING_ENUMERATOR, C_BINDING_OBJECT, C_BINDING_ENUMERATOR },
        .check_kinds = true,
    },
    {
        .source = "int E; void f(void) { enum { E=sizeof E, F=E }; int x=F; }\n",
        .name = "F",
        .count = 2,
        .declarations = { 1, 1 },
    },
    {
        .source = "struct S { struct S *next; }; void f(void) { struct S; struct S *p; } struct S *q;\n",
        .name = "S",
        .count = 5,
        .declarations = { 1, 1, 3, 3, 1 },
        .kinds = { C_BINDING_TAG_KIND, C_BINDING_TAG_KIND, C_BINDING_TAG_KIND, C_BINDING_TAG_KIND, C_BINDING_TAG_KIND },
        .check_kinds = true,
    },
    {
        .source = "struct S; struct S *before; struct S { struct S *next; }; struct S *after; struct S; struct S *last;\n",
        .name = "S",
        .count = 7,
        .declarations = { 1, 1, 3, 3, 3, 6, 6 },
    },
    {
        .source = "typedef int T; int (*f(int T))(T) { T=1; return 0; } T g;\n",
        .name = "T",
        .count = 5,
        .declarations = { 1, 2, 1, 2, 1 },
    },
    {
        .source = "typedef int T; void f(int T, int a[sizeof T]); T g;\n",
        .name = "T",
        .count = 4,
        .declarations = { 1, 2, 2, 1 },
    },
    {
        .source = "int n; void f(int n[n]);\n",
        .name = "n",
        .count = 3,
        .declarations = { 1, 2, 1 },
    },
    {
        .source = "typedef int T; void f(int (*g)(int T)) { T x; }\n",
        .name = "T",
        .count = 3,
        .declarations = { 1, 2, 1 },
    },
    {
        .source = "int x; void f(int x) { x++; } void g(void) { x++; }\n",
        .name = "x",
        .count = 4,
        .declarations = { 1, 2, 2, 1 },
    },
    {
        .source = "typedef int T; void f(void) { for (int T=0; T<3; ++T) { T+=1; } T x; }\n",
        .name = "T",
        .count = 6,
        .declarations = { 1, 2, 2, 2, 2, 1 },
    },
    {
        .source = "typedef int T; void f(void) { if (sizeof(enum { T=1 })) { int x=T; } else { int y=T; } T z; }\n",
        .name = "T",
        .count = 5,
        .declarations = { 1, 2, 2, 2, 1 },
    },
    {
        .source = "void f(struct S *p, struct S *q); struct S *r;\n",
        .name = "S",
        .count = 3,
        .declarations = { 1, 1, 3 },
    },
    {
        .source = "void f(struct S *p) { struct S *q; } struct S *r;\n",
        .name = "S",
        .count = 3,
        .declarations = { 1, 1, 3 },
    },
    {
        .source = "typedef int T; void f(void) { goto T; T:; T x; }\n",
        .name = "T",
        .count = 4,
        .declarations = { 1, 3, 3, 1 },
        .kinds = { C_BINDING_TYPEDEF, C_BINDING_LABEL_KIND, C_BINDING_LABEL_KIND, C_BINDING_TYPEDEF },
        .check_kinds = true,
    },
    {
        .source = "void f(void) { goto L; L:; } void g(void) { goto L; L:; }\n",
        .name = "L",
        .count = 4,
        .declarations = { 2, 2, 4, 4 },
    },
    {
        .source = "typedef int T; struct S { int T; }; void f(struct S s) { s.T=0; T x; struct S t={.T=1}; }\n",
        .name = "T",
        .count = 5,
        .declarations = { 1, -1, -1, 1, -1 },
        .kinds = { C_BINDING_TYPEDEF, UINT8_MAX, UINT8_MAX, C_BINDING_TYPEDEF, UINT8_MAX },
        .check_kinds = true,
    },
    {
        .source = "struct S { enum { E=1 } x; int arr[E]; }; int y=E;\n",
        .name = "E",
        .count = 3,
        .declarations = { 1, 1, 1 },
    },
    {
        .source = "int f(void) { return missing; }\n",
        .name = "missing",
        .count = 1,
        .declarations = { 0 },
        .clang_rejects = true,
    },
    {
        .source = "int f(void) { return later; } int later;\n",
        .name = "later",
        .count = 2,
        .declarations = { 0, 2 },
        .clang_rejects = true,
    },
    {
        .source = "typedef int T; void f(void) { int T=1; T++; { typedef int T; T x; } T++; } T g;\n",
        .name = "T",
        .count = 7,
        .declarations = { 1, 2, 2, 4, 4, 2, 1 },
    },
    {
        .source = "typedef int T, (*F)(T); F f;\n",
        .name = "T",
        .count = 2,
        .declarations = { 1, 1 },
    },
    {
        .source = "typedef int S; struct S { S x; }; S a; struct S b;\n",
        .name = "S",
        .count = 5,
        .declarations = { 1, 2, 1, 1, 2 },
    },
    {
        .source = "int x; void f(void) { for (int x=x; x; x--) if (x) { int x=x; x++; } x++; }\n",
        .name = "x",
        .count = 10,
        .declarations = { 1, 2, 2, 2, 2, 2, 7, 7, 7, 1 },
    },
    {
        .source = "void f(void) { __label__ L; goto L; { __label__ L; goto L; L:; } L:; }\n",
        .name = "L",
        .count = 6,
        .declarations = { 1, 1, 3, 3, 3, 1 },
    },
    {
        .source = "void f(void) { void *p=&&L; goto *p; L:; }\n",
        .name = "L",
        .count = 2,
        .declarations = { 2, 2 },
    },
    {
        .source = "struct S { int a; }; void f(void) { if (sizeof(struct S { int b; })) (void)sizeof(struct S { char c; }); "
                  "else (void)sizeof(struct S); (void)sizeof(struct S); }\n",
        .name = "S",
        .count = 5,
        .declarations = { 1, 2, 3, 2, 1 },
    },
    {
        .source = "struct S { int a; }; void f(void) { while (sizeof(struct S { int b; })) (void)sizeof(struct S { char c; }); "
                  "(void)sizeof(struct S); }\n",
        .name = "S",
        .count = 4,
        .declarations = { 1, 2, 3, 1 },
    },
    {
        .source = "struct S { int a; }; void f(void) { for (; sizeof(struct S { int b; });) (void)sizeof(struct S { char c; }); "
                  "(void)sizeof(struct S); }\n",
        .name = "S",
        .count = 4,
        .declarations = { 1, 2, 3, 1 },
    },
    {
        .source = "struct S { int a; }; void f(void) { do (void)sizeof(struct S { char c; }); while (sizeof(struct S { int b; })); "
                  "(void)sizeof(struct S); }\n",
        .name = "S",
        .count = 4,
        .declarations = { 1, 2, 3, 1 },
    },
    {
        .source = "int oldstyle(a) int a; { return a; }\n",
        .name = "a",
        .count = 3,
        .declarations = { 1, 2, 2 },
    },
    {
        .source = "int labels(void) { goto L; { __label__ L; goto L; L:; } L: return 0; }\n",
        .name = "L",
        .count = 5,
        .declarations = { 5, 2, 2, 2, 5 },
    },
    {
        .source = "int f(void); int g(void) { return f(); }\n",
        .name = "f",
        .count = 2,
        .declarations = { 1, 1 },
        .kinds = { C_BINDING_FUNCTION, C_BINDING_FUNCTION },
        .check_kinds = true,
    },
    {
        // The GNU17 trace must retain the spelling table that distinguishes statement attributes from declarations.
        .source = "void target(void); void f(void) { __attribute__((noinline)) target(); __attribute__((__always_inline__)) target(); }\n",
        .name = "target",
        .count = 3,
        .declarations = { 1, 1, 1 },
        .kinds = { C_BINDING_FUNCTION, C_BINDING_FUNCTION, C_BINDING_FUNCTION },
        .check_kinds = true,
    },
    {
        .source = "void f(int callback(void)) { callback(); }\n",
        .name = "callback",
        .count = 2,
        .declarations = { 1, 1 },
        .kinds = { C_BINDING_OBJECT, C_BINDING_OBJECT },
        .check_kinds = true,
    },
    {
        .source = "",
    },
};

static void parse_binding_test_expected(ParseBindingTest* test, const char* path, const Parser* parser, const InternTable* table,
                                        const ParseBindings* bindings, const ParseBindingExpected* expected, uint32_t expected_count)
{
    for (uint32_t e = 0; e < expected_count; e += 1)
    {
        const ParseBindingExpected* item = &expected[e];
        const IdentId name = intern_find(table, (const uint8_t*)item->name, (uint32_t)strlen(item->name));
        uint32_t* tokens = malloc(((uint64_t)item->count + 1) * sizeof(uint32_t));
        uint32_t count = 0;
        for (uint32_t t = 0; tokens != 0 && t < parser->tokens.count; t += 1)
        {
            if (parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == name)
            {
                if (count < item->count)
                {
                    tokens[count] = t;
                }
                count += 1;
            }
        }
        parse_binding_test_check(test, tokens != 0 && name != 0 && count == item->count, path, "expected identifier occurrence count differs", 0);
        for (uint32_t i = 0; tokens != 0 && count == item->count && i < count; i += 1)
        {
            const uint32_t token = tokens[i];
            const int32_t declaration = item->declarations[i];
            const bool in_range = declaration <= 0 || (uint32_t)declaration <= count;
            const uint32_t wanted = declaration > 0 && in_range ? tokens[declaration - 1] : UINT32_MAX;
            const bool active = bindings->token_events[token] != 0;
            const bool same = in_range && bindings->declarations[token] == wanted && active == (declaration >= 0);
            parse_binding_test_check(test, same, path, "independent source binding differs", token);
            if (item->kinds != 0)
            {
                parse_binding_test_check(test, bindings->kinds[token] == item->kinds[i], path, "independent binding kind differs", token);
            }
        }
        free(tokens);
    }
}

static void parse_binding_test_columns(ParseBindingTest* test, const char* path, ParseBindings* bindings, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint32_t* direct = arena_push_array(scratch, (uint64_t)bindings->count + 1, sizeof(uint32_t), 64);
    uint8_t* kinds = direct != 0 ? arena_push_array(scratch, (uint64_t)bindings->count + 1, 1, 64) : 0;
    const bool allocated = kinds != 0;
    parse_binding_test_check(test, allocated, path, "could not allocate oracle columns", 0);
    if (allocated)
    {
        memcpy(direct, bindings->declarations, (uint64_t)bindings->count * sizeof(uint32_t));
        memcpy(kinds, bindings->kinds, bindings->count);
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            const bool resolved = parse_bindings_resolve(bindings, mode == 0, scratch);
            parse_binding_test_check(test, resolved, path, mode == 0 ? "scalar replay failed" : "batch resolution failed", 0);
            for (uint32_t t = 0; resolved && t < bindings->count; t += 1)
            {
                parse_binding_test_check(test, bindings->declarations[t] == direct[t], path,
                                         mode == 0 ? "scalar replay differs from source producer" : "batch differs from source producer", t);
                const uint32_t event = bindings->token_events[t];
                const bool index_valid = event == 0 || event <= bindings->events.count;
                parse_binding_test_check(test, index_valid, path, "token event is outside the stream", t);
                bool metadata = bindings->kinds[t] == kinds[t];
                if (event == 0)
                {
                    metadata &= bindings->namespaces[t] == UINT8_MAX && bindings->kinds[t] == UINT8_MAX && bindings->declarations[t] == UINT32_MAX;
                }
                else if (index_valid)
                {
                    const uint32_t space = bindings->events.key[event - 1] / bindings->events.name_capacity;
                    metadata &= space == bindings->namespaces[t] && space < C_BINDING_NAMESPACES && bindings->kinds[t] < C_BINDING_KINDS;
                }
                parse_binding_test_check(test, metadata, path, "binding namespace/kind metadata differs", t);
                if (mode == 1 && event != 0)
                {
                    test->identifiers += 1;
                }
            }
        }
        test->walks += bindings->statistics.walks;
        test->walked_uses += bindings->statistics.walked_uses;
        test->fallback_uses += bindings->statistics.fallback_uses;
    }
    arena_pop_to(scratch, mark);
}

// Both parser schedules must produce the same source event meaning. A rejected source is allowed only when an
// external oracle rejected it too; it never contributes to the resolved-identifier acceptance count.
static void parse_binding_test_file(ParseBindingTest* test, const char* path, const PPOptions* options,
                                    const ParseBindingExpected* expected, uint32_t expected_count, bool allow_rejection)
{
    static InternTable table;
    static Preprocessor pp;
    static Parser parser;
    Arena arena = arena_reserve(1ull << 32);
    Arena scratch = arena_reserve(1ull << 32);
    bool loaded = intern_initialize(&table) && pp_initialize(&pp, options, &table) && pp_run(&pp, path);
    parse_binding_test_check(test, loaded || allow_rejection, path, "preprocessing failed", 0);
    uint32_t* previous = 0;
    uint32_t previous_count = 0;
    bool previous_success = false;
    bool accepted = false;
    for (uint32_t mode = 0; loaded && mode < 2; mode += 1)
    {
        const ParseOptions parse_options = { .scalar = mode != 0 };
        const bool initialized = parse_initialize(&parser, &parse_options, options->language);
        const bool parsed = initialized && parse_translation_unit(&parser, &pp);
        parse_binding_test_check(test, parsed || allow_rejection, path, "source did not parse", 0);
        if (mode != 0)
        {
            parse_binding_test_check(test, parsed == previous_success, path, "parser acceptance differs", 0);
        }
        if (parsed)
        {
            const uint64_t mark = arena.length;
            ParseBindings bindings = { 0 };
            const bool built = parse_bindings_build(&bindings, &parser, &arena);
            parse_binding_test_check(test, built && bindings.valid && bindings.count == parser.tokens.count, path, "source event production failed", 0);
            if (built)
            {
                parse_binding_test_expected(test, path, &parser, &table, &bindings, expected, expected_count);
                parse_binding_test_columns(test, path, &bindings, &scratch);
                if (mode == 0)
                {
                    previous_count = bindings.count;
                    previous = malloc(((uint64_t)bindings.count + 1) * sizeof(uint32_t));
                    parse_binding_test_check(test, previous != 0, path, "could not save parser comparison", 0);
                    if (previous != 0)
                    {
                        memcpy(previous, bindings.declarations, (uint64_t)bindings.count * sizeof(uint32_t));
                    }
                }
                else
                {
                    const bool same = previous != 0 && previous_count == bindings.count &&
                                      memcmp(previous, bindings.declarations, (uint64_t)bindings.count * sizeof(uint32_t)) == 0;
                    parse_binding_test_check(test, same, path, "parser paths produce different declaration columns", 0);
                }
                accepted = true;
            }
            arena_pop_to(&arena, mark);
        }
        else if (initialized)
        {
            // Invalid parser output must not be silently admitted as a complete binding result.
            const uint64_t mark = arena.length;
            ParseBindings bindings = { 0 };
            const bool built = parse_bindings_build(&bindings, &parser, &arena);
            parse_binding_test_check(test, !built && !bindings.valid && arena.length == mark, path, "invalid source was not rejected transactionally", 0);
        }
        previous_success = parsed;
        parse_release(&parser);
    }
    test->files += accepted;
    test->rejected += !accepted;
    free(previous);
    pp_release(&pp);
    intern_release(&table);
    arena_release(&scratch);
    arena_release(&arena);
}

static void parse_binding_test_fixtures(ParseBindingTest* test, const char* directory, const char* clang)
{
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    char path[4096];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(parse_binding_fixtures); f += 1)
    {
        const uint32_t before = test->failures;
        const ParseBindingFixture* fixture = &parse_binding_fixtures[f];
        snprintf(path, sizeof(path), "%s/fixture_%u.c", directory, f);
        const bool written = parse_test_write(path, fixture->source, strlen(fixture->source));
        parse_binding_test_check(test, written, path, "could not write fixture", 0);
        if (written)
        {
            const ParseBindingExpected expected = {
                .name = fixture->name,
                .count = fixture->count,
                .declarations = fixture->declarations,
                .kinds = fixture->check_kinds ? fixture->kinds : 0,
            };
            parse_binding_test_file(test, path, &options, &expected, fixture->name != 0, false);
            if (clang != 0)
            {
                const char* arguments[] = { "-std=gnu17" };
                char* paths[] = { path };
                ParseTestClang outcome;
                const bool ran = parse_test_clang(clang, arguments, 1, paths, 1, &outcome);
                parse_binding_test_check(test, ran && outcome.rejected == fixture->clang_rejects, path, "Clang fixture acceptance differs", 0);
            }
        }
        if (test->failures == before && !test->keep)
        {
            remove(path);
        }
    }
    const char* bad = "int ok; void f(void) { int x = ; } int after;\n";
    snprintf(path, sizeof(path), "%s/invalid.c", directory);
    const bool written = parse_test_write(path, bad, strlen(bad));
    parse_binding_test_check(test, written, path, "could not write recovery fixture", 0);
    if (written)
    {
        parse_binding_test_file(test, path, &options, 0, 0, true);
    }
    if (!test->keep)
    {
        remove(path);
    }
}

#define BUSTER_PARSE_BINDING_GENERATED_NAMES 8
#define BUSTER_PARSE_BINDING_GENERATED_OCCURRENCES 1024
#define BUSTER_PARSE_BINDING_GENERATED_DEPTH 32

typedef struct ParseBindingGenerator ParseBindingGenerator;
struct ParseBindingGenerator
{
    char source[131072];
    size_t length;
    bool valid;
    uint32_t counts[BUSTER_PARSE_BINDING_GENERATED_NAMES];
    uint32_t heads[BUSTER_PARSE_BINDING_GENERATED_NAMES];
    int32_t expected[BUSTER_PARSE_BINDING_GENERATED_NAMES][BUSTER_PARSE_BINDING_GENERATED_OCCURRENCES];
    uint32_t saved[BUSTER_PARSE_BINDING_GENERATED_DEPTH][BUSTER_PARSE_BINDING_GENERATED_NAMES];
    uint32_t declared[BUSTER_PARSE_BINDING_GENERATED_DEPTH];
    uint32_t depth;
};

static void parse_binding_generate_text(ParseBindingGenerator* generator, const char* format, ...) __attribute__((format(printf, 2, 3)));

static void parse_binding_generate_text(ParseBindingGenerator* generator, const char* format, ...)
{
    if (generator->valid)
    {
        va_list arguments;
        va_start(arguments, format);
        const int length = vsnprintf(generator->source + generator->length, sizeof(generator->source) - generator->length, format, arguments);
        va_end(arguments);
        generator->valid = length >= 0 && (size_t)length < sizeof(generator->source) - generator->length;
        generator->length += generator->valid ? (size_t)length : 0;
    }
}

static void parse_binding_generate_name(ParseBindingGenerator* generator, uint32_t name, bool declaration)
{
    const uint32_t index = generator->counts[name];
    generator->valid &= index < BUSTER_PARSE_BINDING_GENERATED_OCCURRENCES;
    if (generator->valid)
    {
        if (declaration)
        {
            generator->heads[name] = index + 1;
        }
        generator->expected[name][index] = (int32_t)generator->heads[name];
        generator->counts[name] += 1;
        parse_binding_generate_text(generator, "T%u", name);
    }
}

static void parse_binding_test_generate(ParseBindingTest* test, const char* directory, uint64_t seed, uint32_t count)
{
    static const char* const names[BUSTER_PARSE_BINDING_GENERATED_NAMES] = { "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7" };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    ParseBindingGenerator* generator = malloc(sizeof(ParseBindingGenerator));
    parse_binding_test_check(test, generator != 0, "generated", "could not allocate source generator", 0);
    char path[4096];
    for (uint32_t program = 0; generator != 0 && program < count; program += 1)
    {
        const uint32_t before = test->failures;
        snprintf(path, sizeof(path), "%s/generated_%u.c", directory, program);
        memset(generator, 0, sizeof(*generator));
        generator->valid = true;
        for (uint32_t n = 0; n < BUSTER_PARSE_BINDING_GENERATED_NAMES; n += 1)
        {
            parse_binding_generate_text(generator, "typedef int ");
            parse_binding_generate_name(generator, n, true);
            parse_binding_generate_text(generator, ";\n");
        }
        parse_binding_generate_text(generator, "unsigned long sink; void f(void) {\n");
        for (uint32_t step = 0; generator->valid && step < 256; step += 1)
        {
            const uint64_t random = parse_test_random(&seed);
            const uint32_t name = (uint32_t)(random >> 8) % BUSTER_PARSE_BINDING_GENERATED_NAMES;
            const uint32_t action = (uint32_t)random % 16;
            if (action < 3 && generator->depth + 1 < BUSTER_PARSE_BINDING_GENERATED_DEPTH)
            {
                memcpy(generator->saved[generator->depth], generator->heads, sizeof(generator->heads));
                generator->depth += 1;
                generator->declared[generator->depth] = 0;
                parse_binding_generate_text(generator, "{\n");
            }
            else if (action < 5 && generator->depth != 0)
            {
                generator->depth -= 1;
                memcpy(generator->heads, generator->saved[generator->depth], sizeof(generator->heads));
                parse_binding_generate_text(generator, "}\n");
            }
            else if (action < 10 && (generator->declared[generator->depth] & (1u << name)) == 0)
            {
                generator->declared[generator->depth] |= 1u << name;
                const bool type = (random & 0x10000) != 0;
                parse_binding_generate_text(generator, type ? "typedef int " : "int ");
                parse_binding_generate_name(generator, name, true);
                parse_binding_generate_text(generator, type ? ";\n" : "=0;\n");
            }
            else
            {
                parse_binding_generate_text(generator, "sink += sizeof(");
                parse_binding_generate_name(generator, name, false);
                parse_binding_generate_text(generator, ");\n");
            }
        }
        while (generator->depth != 0)
        {
            generator->depth -= 1;
            parse_binding_generate_text(generator, "}\n");
        }
        parse_binding_generate_text(generator, "}\n");
        const bool written = generator->valid && parse_test_write(path, generator->source, generator->length);
        parse_binding_test_check(test, written, path, "could not generate scope program", program);
        ParseBindingExpected expected[BUSTER_PARSE_BINDING_GENERATED_NAMES];
        for (uint32_t n = 0; n < BUSTER_PARSE_BINDING_GENERATED_NAMES; n += 1)
        {
            expected[n] = (ParseBindingExpected){ .name = names[n], .count = generator->counts[n], .declarations = generator->expected[n] };
        }
        if (written)
        {
            parse_binding_test_file(test, path, &options, expected, BUSTER_PARSE_BINDING_GENERATED_NAMES, false);
        }
        if (test->failures == before && !test->keep)
        {
            remove(path);
        }
    }
    free(generator);
}

static void parse_binding_test_limits(ParseBindingTest* test, const char* directory)
{
    const char* source = "typedef int T; void f(int x) { T y=x; { int x=y; y=x; } }\n";
    char path[4096];
    snprintf(path, sizeof(path), "%s/capacity.c", directory);
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { 0 };
    static InternTable table;
    static Preprocessor pp;
    static Parser parser;
    Arena arena = arena_reserve(1ull << 20);
    ParseBindings bindings = { 0 };
    const bool ready = parse_test_write(path, source, strlen(source)) && intern_initialize(&table) && pp_initialize(&pp, &options, &table) &&
                       parse_initialize(&parser, &parse_options, options.language) && pp_run(&pp, path) && parse_translation_unit(&parser, &pp) &&
                       parse_bindings_build(&bindings, &parser, &arena);
    parse_binding_test_check(test, ready, path, "capacity fixture could not initialize", 0);
    if (ready)
    {
        const uint64_t required = arena.length;
        const uint64_t limits[] = { 0, 1, 63, 64, 255, required / 4, required / 2, required - 1 };
        const uint64_t scratch_mark = parser.scratch.length;
        ParseBindings aliased = { 0 };
        const bool alias_built = parse_bindings_build(&aliased, &parser, &parser.scratch);
        parse_binding_test_check(test, !alias_built && !aliased.valid && aliased.token_events == 0 && parser.scratch.length == scratch_mark,
                                 path, "source output accepted the parser scratch arena", 0);
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(limits); l += 1)
        {
            Arena limited = arena_reserve(limits[l]);
            if (limits[l] >= 17)
            {
                arena_push(&limited, 17, 1);
            }
            const uint64_t mark = limited.length;
            ParseBindings partial = { 0 };
            const bool built = parse_bindings_build(&partial, &parser, &limited);
            parse_binding_test_check(test, !built && !partial.valid && partial.token_events == 0 && limited.length == mark &&
                                     parser.scratch.length == scratch_mark, path, "source-output capacity failure did not roll back", l);
            arena_release(&limited);
        }
        // Exhaust the trace's scratch after its permanent output has been allocated; both arenas must roll back.
        const uint64_t capacity = parser.scratch.capacity;
        const uint64_t mark = arena.length;
        parser.scratch.capacity = parser.scratch.length;
        ParseBindings partial = { 0 };
        const bool built = parse_bindings_build(&partial, &parser, &arena);
        parser.scratch.capacity = capacity;
        parse_binding_test_check(test, !built && !partial.valid && arena.length == mark && parser.scratch.length == scratch_mark,
                                 path, "trace scratch failure did not roll back", 0);
        Arena unavailable = { 0 };
        uint32_t* saved = malloc((uint64_t)bindings.count * sizeof(uint32_t));
        parse_binding_test_check(test, saved != 0, path, "could not save failure oracle", 0);
        if (saved != 0)
        {
            memcpy(saved, bindings.declarations, (uint64_t)bindings.count * sizeof(uint32_t));
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                const bool resolved = parse_bindings_resolve(&bindings, mode == 0, &unavailable);
                parse_binding_test_check(test, !resolved && bindings.valid && unavailable.length == 0 &&
                                         memcmp(saved, bindings.declarations, (uint64_t)bindings.count * sizeof(uint32_t)) == 0,
                                         path, "failed resolution changed valid output", mode);
            }
            ParseBindings recovered = { 0 };
            const bool recovered_ok = parse_bindings_build(&recovered, &parser, &arena);
            parse_binding_test_check(test, recovered_ok && recovered.count == bindings.count &&
                                     memcmp(saved, recovered.declarations, (uint64_t)bindings.count * sizeof(uint32_t)) == 0,
                                     path, "failed build changed subsequent source bindings", 0);
        }
        free(saved);
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&table);
    arena_release(&arena);
    if (!test->keep)
    {
        remove(path);
    }
}

static void parse_binding_test_k3(ParseBindingTest* test, const char* directory)
{
    static const char* const sources[] =
    {
        "int f(void) { typedef int T; T x=1; return x; }\n",
        "int f(void) { typedef int T; T x=1; { typedef long T; T y=2; x+=y; } return x; }\n",
        "int f(void) { { typedef int T; T x=1; } { typedef long T; T x=2; } return 0; }\n",
        "int f(void) { typedef int T; typedef T U; U x=1; return x; }\n",
        "typedef int T; int f(void) { typedef int T[sizeof((T)0)]; T x; return sizeof(x); }\n",
    };
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { 0 };
    char path[4096];
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(sources); f += 1)
    {
        static InternTable table;
        static Preprocessor pp;
        static Parser parser;
        snprintf(path, sizeof(path), "%s/k3_%u.c", directory, f);
        const bool parsed = parse_test_write(path, sources[f], strlen(sources[f])) && intern_initialize(&table) && pp_initialize(&pp, &options, &table) &&
                            parse_initialize(&parser, &parse_options, options.language) && pp_run(&pp, path) && parse_translation_unit(&parser, &pp);
        parse_binding_test_check(test, parsed, path, "scoped typedef vector fixture did not parse", 0);
        if (parsed)
        {
            parse_binding_test_check(test, !parser.statistics.whole && parser.statistics.fallback_items == 0 && parser.statistics.fallback_tokens == 0,
                                     path, "scoped typedef unexpectedly leaves the vector path", 0);
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&table);
        parse_binding_test_file(test, path, &options, 0, 0, false);
        if (!test->keep)
        {
            remove(path);
        }
    }
}

// Shared by the corpus test and benchmark: keep preprocessing options on their original route through the driver.
typedef struct ParseBindingArguments ParseBindingArguments;
struct ParseBindingArguments
{
    char** options;
    int option_count;
    const char** clang_options;
    uint32_t clang_option_count;
    char** inputs;
    uint32_t input_count;
};

static bool parse_binding_arguments_initialize(ParseBindingArguments* arguments, int argc)
{
    *arguments = (ParseBindingArguments){ 0 };
    arguments->options = calloc((uint64_t)argc + 2, sizeof(char*));
    arguments->clang_options = calloc((uint64_t)argc + 2, sizeof(char*));
    arguments->inputs = calloc((uint64_t)argc + 1, sizeof(char*));
    return arguments->options != 0 && arguments->clang_options != 0 && arguments->inputs != 0;
}

static bool parse_binding_arguments_add(ParseBindingArguments* arguments, int argc, char** argv, int* index)
{
    const char* argument = argv[*index];
    bool valid = true;
    if (argument[0] != '-')
    {
        arguments->inputs[arguments->input_count++] = argv[*index];
    }
    else
    {
        const bool own = pp_own_option_with_value(argument);
        const bool valued = own || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 ||
                            strcmp(argument, "-isystem") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 ||
                            strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
        arguments->options[arguments->option_count++] = argv[*index];
        if (!own && strcmp(argument, "--scalar") != 0)
        {
            arguments->clang_options[arguments->clang_option_count++] = argv[*index];
        }
        valid = !valued || *index + 1 < argc;
        if (valid && valued)
        {
            *index += 1;
            arguments->options[arguments->option_count++] = argv[*index];
            if (!own)
            {
                arguments->clang_options[arguments->clang_option_count++] = argv[*index];
            }
        }
    }
    return valid;
}

static void parse_binding_arguments_release(ParseBindingArguments* arguments)
{
    free(arguments->options);
    free(arguments->clang_options);
    free(arguments->inputs);
    *arguments = (ParseBindingArguments){ 0 };
}

static int run_parser_bindings_tests(int argc, char** argv)
{
    ParseBindingTest test = { 0 };
    ParseBindingArguments arguments;
    bool valid = parse_binding_arguments_initialize(&arguments, argc);
    uint64_t seed = 128;
    uint32_t count = 64;
    const char* clang = 0;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0)
        {
            const bool path = i + 1 < argc && argv[i + 1][0] != '-' && strchr(argv[i + 1], '/') != 0 && strstr(argv[i + 1], "clang") != 0;
            clang = path ? argv[++i] : "clang";
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            test.keep = true;
        }
        else if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "--count") == 0) && i + 1 < argc)
        {
            const bool is_seed = strcmp(argv[i], "--seed") == 0;
            char* end = 0;
            errno = 0;
            const uint64_t value = strtoull(argv[++i], &end, 10);
            valid = errno == 0 && end != argv[i] && *end == 0 && (is_seed || value <= 100000);
            if (is_seed)
            {
                seed = value;
            }
            else
            {
                count = (uint32_t)value;
            }
        }
        else
        {
            valid = parse_binding_arguments_add(&arguments, argc, argv, &i);
        }
    }
    Arena arena = arena_reserve(1ull << 26);
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    if (valid)
    {
        arguments.options[arguments.option_count] = "placeholder.c";
        valid = pp_parse_options(arguments.option_count + 1, arguments.options, &options, &arena, &input, &output, &markers);
    }
    char directory[4096] = { 0 };
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-test-parser-bindings-XXXXXX", temporary != 0 ? temporary : "/tmp");
    valid = valid && mkdtemp(directory) != 0;
    if (valid)
    {
        if (arguments.input_count == 0)
        {
            parse_binding_test_fixtures(&test, directory, clang);
            parse_binding_test_generate(&test, directory, seed, count);
            parse_binding_test_limits(&test, directory);
            parse_binding_test_k3(&test, directory);
        }
        else
        {
            char** files = calloc(1u << 20, sizeof(char*));
            uint32_t file_count = 0;
            valid = files != 0;
            for (uint32_t i = 0; valid && i < arguments.input_count; i += 1)
            {
                const uint32_t before = file_count;
                file_count = parse_test_collect(arguments.inputs[i], files, file_count, 1u << 20);
                parse_binding_test_check(&test, file_count != before, arguments.inputs[i], "no source files found", 0);
            }
            char standard[32];
            snprintf(standard, sizeof(standard), "-std=%s%u", options.language.gnu ? "gnu" : "c", options.language.standard % 100);
            arguments.clang_options[arguments.clang_option_count++] = standard;
            for (uint32_t i = 0; valid && i < file_count; i += 1)
            {
                ParseTestClang outcome = { 0 };
                const bool ran = clang == 0 || parse_test_clang(clang, arguments.clang_options, arguments.clang_option_count, &files[i], 1, &outcome);
                parse_binding_test_check(&test, ran, files[i], "Clang did not answer", 0);
                if (ran)
                {
                    parse_binding_test_file(&test, files[i], &options, 0, 0, clang != 0 && outcome.rejected);
                }
            }
            for (uint32_t i = 0; i < file_count; i += 1)
            {
                free(files[i]);
            }
            free(files);
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: compiler test-parser-bindings [--seed N] [--count N] [--clang [PATH]] [--keep] [-E's options] [file or directory...]\n");
        test.failures += 1;
    }
    printf("parser bindings: %llu checks, %u files, %u rejected, %llu identifier tokens, %u failures (seed %llu)\n",
           (unsigned long long)test.checks, test.files, test.rejected, (unsigned long long)test.identifiers, test.failures, (unsigned long long)seed);
    printf("  walks %llu, walked uses %llu, fallback uses %llu\n", (unsigned long long)test.walks,
           (unsigned long long)test.walked_uses, (unsigned long long)test.fallback_uses);
    if (directory[0] != 0 && !test.keep && test.failures == 0)
    {
        rmdir(directory);
    }
    else if (valid && arguments.input_count == 0)
    {
        printf("  source fixtures retained in %s\n", directory);
    }
    arena_release(&arena);
    parse_binding_arguments_release(&arguments);
    return test.failures != 0;
}
