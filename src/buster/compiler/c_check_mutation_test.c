// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker's semantic mutation checks (c_check_mutation.c). check_test_mutation_engine checks the sites a unit
// of its own gives, counted by hand from its parse, the edits some of them make, and the minimizer's helpers. The
// fixtures are differences test-checker-corpus --semantic-mutations found, minimized (checker_mutation_N.c) and
// then fixed in the checker: each is checked as the corpus check checks a file, against clang (which must still give
// the result recorded with it) and with the scalar and vector paths alike. Include after c_check_corpus.c.
#ifndef BUSTER_C_CHECK_MUTATION_TEST_INCLUDED
#define BUSTER_C_CHECK_MUTATION_TEST_INCLUDED

typedef struct CheckMutationFixture CheckMutationFixture;
struct CheckMutationFixture
{
    // The corpus and the mutation it came from.
    const char* name;
    const char* source;
    // The language mode its corpus's configuration gave: 1999 or 2017, and whether GNU's.
    uint32_t standard;
    bool gnu;
    // Clang's verdict and first error's line, which the checker must give.
    bool rejected;
    uint32_t line;
};

static const CheckMutationFixture check_mutation_fixtures[] =
{
    {
        .name = "accepted control: a pointer compared with a null pointer constant",
        .source = "int f(int *p) { return p == 0 || p > (int *)0; }\n",
        .standard = 2017,
        .gnu = true,
        .rejected = false,
        .line = 0,
    },
};

// The unit the engine's checks mutate, and the sites its parse gives, counted by hand: nine declarations with
// declarators (the members a and b, g and its x, f and its p and q, k and the loop's i), two of which start with `*`;
// the members; struct s, g and f at file scope; k and i as locals; x, p and q as parameters (g's `...` stays); the
// operators +, < and = (the loop's i = 0 is a declarator's initializer); and the call of g.
static const char check_mutation_test_unit[] =
    "struct s { int a; char *b; };\n"
    "int g(int x, ...);\n"
    "int f(int p, struct s *q)\n"
    "{\n"
    "    unsigned k = p + 1;\n"
    "    for (int i = 0; i < 2; i++) { k = g(p, q->b); }\n"
    "    return k;\n"
    "}\n";

static const uint32_t check_mutation_test_sites[CHECK_MUTATION_KIND_COUNT] =
{
    [CHECK_MUTATION_POINTER_ADD] = 9,
    [CHECK_MUTATION_POINTER_REMOVE] = 2,
    [CHECK_MUTATION_SIGNEDNESS] = 9,
    [CHECK_MUTATION_WIDTH] = 9,
    [CHECK_MUTATION_CONST] = 9,
    [CHECK_MUTATION_STRUCT] = 9,
    [CHECK_MUTATION_REMOVE_LOCAL] = 2,
    [CHECK_MUTATION_REMOVE_PARAMETER] = 3,
    [CHECK_MUTATION_REMOVE_FILE_SCOPE] = 3,
    [CHECK_MUTATION_REMOVE_MEMBER] = 2,
    [CHECK_MUTATION_SWAP_OPERANDS] = 3,
    [CHECK_MUTATION_SWAP_ARGUMENTS] = 1,
};

// The edit of the unit's site of `kind` whose first token is spelled `first` (the first such site), as the bytes it
// replaces and its replacement.
static bool check_test_mutation_edit(CheckMutationUnit* unit, uint8_t kind, const char* first, CheckMutationEdit* edit, char* replaced, size_t size)
{
    const CheckMutationSite* site = NULL;
    for (uint32_t i = 0; site == NULL && i < unit->site_count; i += 1)
    {
        const CheckMutationSite* candidate = unit->sites + i;
        site = candidate->kind == kind && check_mutation_spelled(unit, candidate->first, first) ? candidate : NULL;
    }
    uint64_t state = 133;
    if (site != NULL)
    {
        check_mutation_edit(edit, unit, site, &state);
        snprintf(replaced, size, "%.*s", (int)(edit->end - edit->begin), unit->text + edit->begin);
    }
    return site != NULL && edit->valid;
}

static bool check_test_mutation_replacement(const CheckMutationEdit* edit, const char* expected)
{
    return edit->replacement_length == strlen(expected) && memcmp(edit->replacement, expected, edit->replacement_length) == 0;
}

static void check_test_mutation_engine(CheckTest* test, const char* directory)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions options =
    {
        .language =
        {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
        .quiet = true,
        .semantic_pragmas = true,
    };
    PPOptions plain = options;
    plain.undefine = true;
    char source[4300];
    char base[4300];
    snprintf(source, sizeof(source), "%s/mutation_unit.c", directory);
    snprintf(base, sizeof(base), "%s/mutation_base.c", directory);
    CheckMutationUnit unit = { 0 };
    const bool written = check_corpus_write(source, check_mutation_test_unit, sizeof(check_mutation_test_unit) - 1);
    const bool prepared = written && check_mutation_prepare(&unit, source, &options, &plain, base, &identifiers, &pp, &parser);
    check_test_expect(test, prepared, "the mutation engine's unit prints, reads back and parses as its base");
    uint32_t counts[CHECK_MUTATION_KIND_COUNT] = { 0 };
    bool main = true;
    for (uint32_t site = 0; prepared && site < unit.site_count; site += 1)
    {
        counts[unit.sites[site].kind] += 1;
        main &= unit.sites[site].main;
    }
    for (uint32_t kind = 0; prepared && kind < CHECK_MUTATION_KIND_COUNT; kind += 1)
    {
        char description[128];
        snprintf(description, sizeof(description), "the unit's %s sites are the ones its parse gives", check_mutation_names[kind]);
        check_test_expect(test, counts[kind] == check_mutation_test_sites[kind], description);
        check_test_expect(test, unit.site_offsets[kind * 2 + 2] - unit.site_offsets[kind * 2] == counts[kind], "sites are grouped by kind");
    }
    check_test_expect(test, !prepared || main, "every site of a unit without includes is in the unit's own file");
    CheckMutationEdit edit = { 0 };
    char replaced[256];
    check_test_expect(test, prepared && check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_ARGUMENTS, "(", &edit, replaced, sizeof(replaced)) &&
                            strcmp(replaced, "p, q->b") == 0 && check_test_mutation_replacement(&edit, " q->b, p "),
                      "an argument swap exchanges a call's two arguments");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_OPERANDS, "p", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "p + 1") == 0 && check_test_mutation_replacement(&edit, " 1 + p "),
                      "an operand swap exchanges an operator's operands");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_PARAMETER, "int", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "int x,") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a parameter goes with the comma after it");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_PARAMETER, ",", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, ", struct s *q") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a last parameter goes with the comma before it");
    check_test_mutation_edit(&unit, CHECK_MUTATION_REMOVE_LOCAL, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "unsigned k = p + 1;") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a local declaration goes whole");
    check_test_mutation_edit(&unit, CHECK_MUTATION_POINTER_ADD, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && unit.text[edit.begin] == 'k' && check_test_mutation_replacement(&edit, " * "),
                      "a pointer goes before the declarator");
    check_test_mutation_edit(&unit, CHECK_MUTATION_POINTER_REMOVE, "char", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "*") == 0 && check_test_mutation_replacement(&edit, " "),
                      "a pointer's `*` goes");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SIGNEDNESS, "unsigned", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && strcmp(replaced, "unsigned") == 0 && check_test_mutation_replacement(&edit, " signed"),
                      "unsigned becomes signed");
    check_test_mutation_edit(&unit, CHECK_MUTATION_SIGNEDNESS, "char", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && check_test_mutation_replacement(&edit, " unsigned "),
                      "a type without a sign gets unsigned");
    check_test_mutation_edit(&unit, CHECK_MUTATION_CONST, "int", &edit, replaced, sizeof(replaced));
    check_test_expect(test, prepared && edit.valid && replaced[0] == 0 && unit.text[edit.begin] == 'i' && check_test_mutation_replacement(&edit, " const "),
                      "const goes before the type");
    check_test_mutation_edit(&unit, CHECK_MUTATION_WIDTH, "int", &edit, replaced, sizeof(replaced));
    const bool width = edit.valid && strcmp(replaced, "int") == 0 && (check_test_mutation_replacement(&edit, " char") || check_test_mutation_replacement(&edit, " short") ||
                                                                      check_test_mutation_replacement(&edit, " long") || check_test_mutation_replacement(&edit, " long long"));
    check_test_expect(test, prepared && width, "a width change gives another integer type");
    check_test_mutation_edit(&unit, CHECK_MUTATION_STRUCT, "unsigned", &edit, replaced, sizeof(replaced));
    const bool tagged = edit.valid && strcmp(replaced, "unsigned") == 0 &&
                        (check_test_mutation_replacement(&edit, " struct s") || check_test_mutation_replacement(&edit, " struct " BUSTER_CHECK_MUTATION_TAG));
    check_test_expect(test, prepared && tagged, "a struct replacement names a struct defined before the declaration, or the incomplete one");
    check_test_mutation_edit(&unit, CHECK_MUTATION_WIDTH, "struct", &edit, replaced, sizeof(replaced));
    check_test_expect(test, !edit.valid, "a struct type has no width to change");
    // The edit made: the base with its bytes replaced.
    check_test_mutation_edit(&unit, CHECK_MUTATION_SWAP_ARGUMENTS, "(", &edit, replaced, sizeof(replaced));
    size_t length = 0;
    char* mutant = prepared ? check_mutation_text(&unit, &edit, &length) : NULL;
    check_test_expect(test, mutant != NULL && length == unit.length + 2 && strstr(mutant, "k = g( q->b, p );") != NULL && strstr(mutant, "p, q->b") == NULL,
                      "the mutant is the base with the edit made");
    free(mutant);
    free(edit.replacement);
    check_mutation_release(&unit);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    const int removed_source = unlink(source);
    const int removed_base = unlink(base);
    check_test_expect(test, removed_source == 0 && (removed_base == 0 || !prepared), "the engine's files are removed");
    // The minimizer's helpers.
    char blanked[] = "ab\ncd";
    const bool changed = check_mutation_blank(blanked, 1, 4);
    check_test_expect(test, changed && strcmp(blanked, "a \n d") == 0 && !check_mutation_blank(blanked, 1, 4), "blanking keeps newlines and says whether it changed anything");
    static const char spaced[] = "a  b \n\n  c\n";
    size_t compact_length = 0;
    char* lines = check_mutation_compact(spaced, sizeof(spaced) - 1, 2, &compact_length);
    check_test_expect(test, lines != NULL && strcmp(lines, "a  b \n  c\n") == 0 && compact_length == 10, "compaction drops lines of spaces alone");
    free(lines);
    char* spaces = check_mutation_compact(spaced, sizeof(spaced) - 1, 3, &compact_length);
    check_test_expect(test, spaces != NULL && strcmp(spaces, "a b\n\n c\n") == 0 && compact_length == 8, "compaction collapses runs of spaces and drops trailing ones");
    free(spaces);
    static const char directives[] = "int x;\n# 1 \"f.c\" 3\nint y;\n";
    uint32_t segments[64];
    const uint32_t tokens = check_mutation_token_segments(directives, sizeof(directives) - 1, false, segments);
    check_test_expect(test, tokens == 6 && segments[6] == 19 && segments[7] == 22, "the token phase passes over directive lines");
    const uint32_t marker_lines = check_mutation_token_segments(directives, sizeof(directives) - 1, true, segments);
    check_test_expect(test, marker_lines == 1 && segments[0] == 7 && segments[1] == 18, "the markers' phase takes a directive's whole line");
    check_test_expect(test, check_mutation_line_segments("a\nb\nc", 5, segments) == 3 && segments[4] == 4 && segments[5] == 5, "a text that doesn't parse is minimized by lines");
    CheckMutationOutcome wanted =
    {
        .valid = true,
        .parsed = true,
        .clang_rejected = true,
        .accepted = false,
        .equal = true,
        .clang_line = 7,
        .line = 5,
        .phase = "expression",
        .clang_file = "x.c",
        .clang_message = "an error",
        .file = "x.c",
        .message = "another error",
    };
    CheckMutationOutcome moved = wanted;
    moved.clang_line = 9;
    moved.line = 6;
    CheckMutationOutcome met = wanted;
    met.line = 7;
    CheckMutationOutcome unparsed = wanted;
    unparsed.parsed = false;
    check_test_expect(test, check_mutation_same(&wanted, &wanted, true) && !check_mutation_same(&wanted, &moved, true) &&
                            check_mutation_same(&wanted, &moved, false) && !check_mutation_same(&wanted, &met, false) && !check_mutation_same(&wanted, &unparsed, false),
                      "a minimizer's candidate keeps its difference: lines while exact, their order after, and the parse");
    check_test_expect(test, check_mutation_different(&wanted) && !check_mutation_different(&met), "a first error's line difference is a difference");
}

static void check_test_mutation_fixture(CheckTest* test, CheckCorpusTally* tally, const CheckMutationFixture* fixture, uint32_t index, const char* directory,
                                        const char* clang)
{
    const PPOptions options =
    {
        .language =
        {
            .standard = fixture->standard,
            .gnu = fixture->gnu,
        },
        .no_standard_includes = true,
        .undefine = true,
        .quiet = true,
        .clang_features = true,
        .semantic_pragmas = true,
    };
    char standard[32];
    snprintf(standard, sizeof(standard), "-std=%s%u", fixture->gnu ? "gnu" : "c", fixture->standard % 100);
    const char* arguments[] =
    {
        "--no-default-config",
        "--target=x86_64-linux-gnu",
        standard,
        "-nostdinc",
        "-x",
        "cpp-output",
    };
    char path[4300];
    snprintf(path, sizeof(path), "%s/mutation_fixture_%u.c", directory, index);
    CheckCorpusOracle oracle;
    bool ready = check_corpus_write(path, fixture->source, strlen(fixture->source)) &&
                 check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), path, &oracle);
    char description[512];
    snprintf(description, sizeof(description), "%s: clang %s at line %u as recorded", fixture->name, fixture->rejected ? "rejects" : "accepts", fixture->line);
    check_test_expect(test, ready && oracle.rejected == fixture->rejected && (!fixture->rejected || oracle.line == fixture->line), description);
    if (ready)
    {
        const bool different = check_corpus_file(tally, path, fixture->name, "mutation-fixture", &options, &oracle, NULL, 64);
        snprintf(description, sizeof(description), "%s: the checker gives clang's result, on both paths alike", fixture->name);
        check_test_expect(test, !different, description);
        ready = unlink(path) == 0;
    }
    check_test_expect(test, ready, "the fixture's file is removed");
}

static void check_test_mutations(CheckTest* test, const char* clang)
{
    char directory[4096];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-mutation-XXXXXX", temporary != NULL ? temporary : "/tmp");
    const bool made = mkdtemp(directory) != NULL;
    check_test_expect(test, made, "the mutation checks' directory");
    if (made)
    {
        check_test_mutation_engine(test, directory);
        CheckCorpusTally tally = { 0 };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_mutation_fixtures); i += 1)
        {
            check_test_mutation_fixture(test, &tally, check_mutation_fixtures + i, i, directory, clang);
        }
        check_test_expect(test, rmdir(directory) == 0, "the mutation checks' directory is removed");
    }
}

#endif
