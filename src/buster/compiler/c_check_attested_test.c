// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-attested's checks (#2531's step 0), on original fixtures: commands of a few units over a header, the census
// beside them, and each declaration's outcome against its line's expectation. A line's `// N:OUTCOME` says what the
// census must make of its declaration in the command's Nth unit (1 first; the units are padded so that their sizes give
// that order, the driver's costliest first). Every command must also have no reading that declares, completes or looks
// up a name its list doesn't hold, and no hit that its reading here contradicts. The fixtures follow the design's list
// ("The twin, the oracle and the checks"): a redeclaration and a tag's completion out of reach and in it, a typedef
// another unit declares otherwise, a declaration after #pragma pack, a prototype that differs by a system header's flag,
// a tentative definition, a static declaration, an alias, a static assertion, an error in one unit only, an inline
// definition whose later declaration makes it external, a later attribute of a called function, a library function's
// prototype that a unit reaches only by its builtin, an enumerator a body names, a tag first mentioned in a
// prototype's return type, and a declaration that warns, which every unit reports again. Include after
// c_check_attested.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_ATTESTED_TEST_INCLUDED
#define BUSTER_C_CHECK_ATTESTED_TEST_INCLUDED

// The main fixture's header: unit 1 learns what it reads silently and purely, unit 2 calls fclose and __builtin_abs,
// unit 3 has an error, unit 4 names BLUE, die and make_later. A tag first declared in a parameter list warns
// (-Wvisibility) in every unit.
static const char check_attested_test_header[] =
    "typedef unsigned long size_t; // 1:learned 2:reached 3:error-unit 4:skipped\n"
    "typedef struct _IO_FILE FILE; // 1:learned 2:reached 3:error-unit 4:skipped\n"
    "struct _IO_FILE { int fd; size_t count; }; // 1:learned 2:reached 3:error-unit 4:skipped\n"
    "int puts(const char *); // 1:learned 2:skipped 3:error-unit 4:skipped\n"
    "int fclose(FILE *); // 1:learned 2:reached 4:skipped\n"
    "int abs(int); // 1:learned 2:reached 4:skipped\n"
    "enum color { RED, GREEN = RED + 2, BLUE }; // 1:learned 2:skipped 4:reached\n"
    "struct later; // 1:learned 2:skipped 4:reached\n"
    "struct later { int a; }; // 1:learned 2:skipped 4:reached\n"
    "struct later *make_later(void); // 1:learned 2:skipped 4:reached\n"
    "struct made *make(void); // 1:learned 2:skipped 4:skipped\n"
    "static int hidden(void); // 1:internal 2:internal 4:internal\n"
    "int tentative; // 1:tentative 2:tentative 4:tentative\n"
    "int initialized = 3; // 1:defines 2:defines 4:defines\n"
    "_Static_assert(sizeof(int) == 4, \"int\"); // 1:learned 2:skipped 4:skipped\n"
    "int target(void) { return 0; } // 1:definition 2:definition 4:definition\n"
    "int aliased(void) __attribute__((alias(\"target\"))); // 1:alias 2:alias 4:alias\n"
    "int labelled(void) __asm__(\"labelled_symbol\"); // 1:learned 2:skipped 4:skipped\n"
    "inline int twice(int x) { return 2 * x; } // 1:definition 2:definition 4:definition\n"
    "int twice(int); // 1:learned 2:reached 4:reached\n"
    "void die(void); // 1:learned 2:skipped 4:reached\n"
    "int prototyped(void); // 1:learned 2:skipped 4:skipped\n"
    "int visible(struct invisible *); // 1:diagnostic 2:diagnostic 4:diagnostic\n";

// Included after each unit's own function: a later declaration that gives a called function an attribute. A body counts
// as declaring every name it spells, so after unit 4's, which calls die, the declaration is another hash: read.
static const char check_attested_test_late_header[] =
    "void die(void) __attribute__((noreturn)); // 1:learned 2:skipped 4:learned\n"
    "int after(void); // 1:learned 2:skipped 4:skipped\n";

// Each unit's own text after the header, and the bytes of padding that give the units their order.
static const char* const check_attested_test_units[] =
{
    "int one(void) { return puts(\"one\"); }\n",
    "int two(FILE *f) { return fclose(f) + __builtin_abs(-2); }\n",
    "int three(void) { return undeclared_name; }\n",
    "int four(int x) { if (x == BLUE) die(); return make_later() != 0; }\n",
};

// A typedef that another unit declares otherwise, and #pragma pack: unit 1 and unit 3 read q.h as unit 1 does; unit 2
// with another T and unit 4 after a pack pragma read their declarations afresh.
static const char check_attested_test_otherwise_header[] =
    "int use_t(T); // 1:learned 2:learned 3:skipped 4:learned\n"
    "struct packed_me { char c; int i; }; // 1:learned 2:skipped 3:skipped 4:learned\n"
    "int plain(void); // 1:learned 2:skipped 3:skipped 4:learned\n";

static const char* const check_attested_test_otherwise_units[] =
{
    "typedef int T;\n#include \"q.h\"\nint a(void) { return 1; }\n",
    "typedef long T;\n#include \"q.h\"\nint b(void) { return 2; }\n",
    "typedef int T;\n#include \"q.h\"\nint c(void) { return 3; }\n",
    "#pragma pack(1)\ntypedef int T;\n#include \"q.h\"\nint d(void) { return 4; }\n",
};

// A prototype that differs by a system header's flag: the same text in a system header (units 1 and 3) and a user's
// (unit 2).
static const char check_attested_test_system_header[] = "int sysfn(void); // 1:learned 3:skipped\n";
static const char check_attested_test_user_header[] = "int sysfn(void); // 2:learned\n";

static const char* const check_attested_test_system_units[] =
{
    "#include <s.h>\nint a(void) { return 1; }\n",
    "#include \"u.h\"\nint b(void) { return 2; }\n",
    "#include <s.h>\nint c(void) { return 3; }\n",
};

// The outcome named `name` of `length` bytes, or CHECK_ATTESTED_OUTCOMES.
static uint32_t check_attested_test_outcome(const char* name, uint32_t length)
{
    uint32_t found = CHECK_ATTESTED_OUTCOMES;
    for (uint32_t o = 0; o < CHECK_ATTESTED_OUTCOMES && found == CHECK_ATTESTED_OUTCOMES; o += 1)
    {
        found = strlen(check_attested_outcome_names[o]) == length && memcmp(check_attested_outcome_names[o], name, length) == 0 ? o : found;
    }
    return found;
}

// Each `// N:OUTCOME` of file `name`'s `text`, whose unit `units` has (a bit a unit, 1 the first), against the census's
// records of that line of that file in unit N - 1: whether every one held. A line that fails is reported on stderr.
static bool check_attested_test_lines(const CheckAttested* census, const char* name, const char* text, uint32_t units)
{
    bool valid = true;
    uint32_t line = 1;
    const char* at = text;
    while (*at != 0)
    {
        const char* end = strchr(at, '\n');
        end = end != NULL ? end : at + strlen(at);
        const char* marks = NULL;
        for (const char* c = at; c + 3 < end && marks == NULL; c += 1)
        {
            marks = c[0] == '/' && c[1] == '/' && c[2] == ' ' && c[3] >= '1' && c[3] <= '9' ? c + 3 : NULL;
        }
        while (marks != NULL && marks < end)
        {
            const uint32_t unit = (uint32_t)(marks[0] - '1');
            const char* outcome_name = marks + 2;
            const char* outcome_end = outcome_name;
            while (outcome_end < end && *outcome_end != ' ')
            {
                outcome_end += 1;
            }
            const uint32_t expected = check_attested_test_outcome(outcome_name, (uint32_t)(outcome_end - outcome_name));
            uint32_t found = 0;
            uint32_t matched = 0;
            uint32_t seen = CHECK_ATTESTED_OUTCOMES;
            for (uint32_t r = 0; r < census->record_count; r += 1)
            {
                const CheckAttestedRecord* record = census->records + r;
                const bool here = record->unit == unit && record->line == line && !record->main_file && strncmp(record->file, name, sizeof(record->file)) == 0;
                found += here ? 1u : 0u;
                matched += here && record->outcome == expected ? 1u : 0u;
                seen = here ? record->outcome : seen;
            }
            const bool held = (units >> unit & 1) == 0 || (expected != CHECK_ATTESTED_OUTCOMES && found == 1 && matched == 1);
            if (!held)
            {
                fprintf(stderr, "test-checker: attested: %s line %u unit %u: expected %.*s, found %u declarations%s%s\n", name, line, unit + 1,
                        (int)(outcome_end - outcome_name), outcome_name, found, seen != CHECK_ATTESTED_OUTCOMES ? ", the last " : "",
                        seen != CHECK_ATTESTED_OUTCOMES ? check_attested_outcome_names[seen] : "");
            }
            valid = valid && held;
            marks = outcome_end < end ? outcome_end + 1 : end;
            marks = marks < end && marks[0] >= '1' && marks[0] <= '9' && marks + 1 < end && marks[1] == ':' ? marks : end;
        }
        line += 1;
        at = *end != 0 ? end + 1 : end;
    }
    return valid;
}

// A command of `count` units in `directory`, each unit `i` the text `bodies[i]` between `prefix` and `suffix` after
// padding that orders it, run with `options` and the census beside it (in `directory`, where -c writes the objects):
// whether its status was `status` and its census found no unlisted name and no contradiction. The census's records stay
// in `census`.
static bool check_attested_test_command(CheckAttested* census, const char* directory, const char* prefix, const char* const* bodies, const char* suffix,
                                        uint32_t count, const char* const* options, uint32_t option_count, int status)
{
    char paths[8][64];
    char* arguments[24];
    uint32_t argument_count = 0;
    bool valid = count <= 8 && option_count + count < 24;
    for (uint32_t o = 0; valid && o < option_count; o += 1)
    {
        arguments[argument_count] = (char*)options[o];
        argument_count += 1;
    }
    for (uint32_t u = 0; valid && u < count; u += 1)
    {
        // Padding: the first unit the largest, by 256 bytes a place.
        char text[4096];
        const int padding = (int)(count - u) * 256;
        const int length = snprintf(text, sizeof(text), "/*%*s*/\n%s%s%s", padding, "", prefix, bodies[u], suffix);
        snprintf(paths[u], sizeof(paths[u]), "unit%u.c", u + 1);
        char path[640];
        snprintf(path, sizeof(path), "%s/%s", directory, paths[u]);
        valid = length > 0 && (size_t)length < sizeof(text) && parse_test_write(path, text, (size_t)length);
        arguments[argument_count] = paths[u];
        argument_count += 1;
    }
    const int saved = valid ? open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    valid = valid && saved >= 0 && chdir(directory) == 0;
    // The units' diagnostics (a warning a unit, an error) go to /dev/null.
    fflush(stderr);
    const int errors = valid ? dup(STDERR_FILENO) : -1;
    const int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    const bool silenced = errors >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    const int result = valid && silenced ? check_attested_run(census, (int)argument_count, arguments, true) : -1;
    fflush(stderr);
    if (errors >= 0)
    {
        dup2(errors, STDERR_FILENO);
        close(errors);
    }
    if (null >= 0)
    {
        close(null);
    }
    if (saved >= 0)
    {
        valid = fchdir(saved) == 0 && valid;
        close(saved);
    }
    for (uint32_t u = 0; u < count; u += 1)
    {
        char path[640];
        snprintf(path, sizeof(path), "%s/unit%u.c", directory, u + 1);
        unlink(path);
        snprintf(path, sizeof(path), "%s/unit%u.o", directory, u + 1);
        unlink(path);
    }
    const bool clean = census->unlisted_declares == 0 && census->unlisted_completions == 0 && census->unlisted_uses == 0 && census->contradictions == 0;
    if (valid && (result != status || !clean))
    {
        fprintf(stderr, "test-checker: attested: the command's status %d (expected %d), unlisted declares %" PRIu64 " completions %" PRIu64 " uses %" PRIu64
                        ", contradictions %" PRIu64 "\n",
                result, status, census->unlisted_declares, census->unlisted_completions, census->unlisted_uses, census->contradictions);
    }
    return valid && result == status && clean;
}

static __attribute__((noinline)) void check_test_attested(CheckTest* test)
{
    char directory[] = "/tmp/buster-attested-XXXXXX";
    const bool ready = mkdtemp(directory) != 0;
    check_test_expect(test, ready, "attested fixtures' directory is made");
    char header[640];
    snprintf(header, sizeof(header), "%s/h.h", directory);
    char late[640];
    snprintf(late, sizeof(late), "%s/late.h", directory);
    bool written = ready && parse_test_write(header, check_attested_test_header, strlen(check_attested_test_header)) &&
                   parse_test_write(late, check_attested_test_late_header, strlen(check_attested_test_late_header));
    CheckAttestedRecord* records = calloc(1024, sizeof(CheckAttestedRecord));
    // The main fixture, by -c: unit 3's error fails the command.
    {
        CheckAttested census = { .passes = 2, .records = records, .record_capacity = 1024 };
        const char* options[] = { "-nostdinc", "-std=gnu17", "-c" };
        const bool ran = written && records != NULL && check_attested_test_command(&census, directory, "#include \"h.h\"\n", check_attested_test_units,
                                                                                  "#include \"late.h\"\n", 4, options, BUSTER_ARRAY_LENGTH(options), 1);
        check_test_expect(test, ran && census.units_observed == 4 && census.units_failed == 1,
                          "measure-attested runs a command's units on one lane, its census beside them, with no unlisted name and no contradiction");
        check_test_expect(test, ran && check_attested_test_lines(&census, "h.h", check_attested_test_header, 0xf) &&
                                    check_attested_test_lines(&census, "late.h", check_attested_test_late_header, 0xf),
                          "measure-attested: each declaration's outcome by unit (learned, skipped, reached, impure, a root, a diagnostic, an error's unit)");
        check_test_expect(test, ran && census.memo_count != 0 && census.outcome_declarations[CHECK_ATTESTED_SKIPPED] != 0 && census.pass_tokens != 0,
                          "measure-attested's memo learns, and its later units skip");
    }
    // The same units by -fsyntax-only, with unit 3's error out of the command: the same outcomes.
    {
        CheckAttested census = { .passes = 1, .records = records, .record_capacity = 1024 };
        const char* options[] = { "-nostdinc", "-std=gnu17", "-fsyntax-only" };
        const char* const units[] = { check_attested_test_units[0], check_attested_test_units[1], check_attested_test_units[3] };
        const bool ran = written && records != NULL && check_attested_test_command(&census, directory, "#include \"h.h\"\n", units, "#include \"late.h\"\n", 3, options,
                                                                                  BUSTER_ARRAY_LENGTH(options), 0);
        // Its third unit is the main fixture's fourth: the lines say unit 4, the records unit 3.
        for (uint32_t r = 0; ran && r < census.record_count; r += 1)
        {
            records[r].unit += records[r].unit == 2 ? 1u : 0u;
        }
        check_test_expect(test, ran && check_attested_test_lines(&census, "h.h", check_attested_test_header, 0xb) &&
                                    check_attested_test_lines(&census, "late.h", check_attested_test_late_header, 0xb),
                          "measure-attested: -fsyntax-only's outcomes are -c's");
    }
    // The main fixture by the scalar path (--scalar's parser and reader, the twins): the same outcomes.
    {
        CheckAttested census = { .passes = 1, .records = records, .record_capacity = 1024 };
        const char* options[] = { "-nostdinc", "-std=gnu17", "--scalar", "-c" };
        const bool ran = written && records != NULL && check_attested_test_command(&census, directory, "#include \"h.h\"\n", check_attested_test_units,
                                                                                  "#include \"late.h\"\n", 4, options, BUSTER_ARRAY_LENGTH(options), 1);
        check_test_expect(test, ran && check_attested_test_lines(&census, "h.h", check_attested_test_header, 0xf) &&
                                    check_attested_test_lines(&census, "late.h", check_attested_test_late_header, 0xf),
                          "measure-attested: --scalar's outcomes are the vector path's");
    }
    unlink(header);
    unlink(late);
    // A typedef another unit declares otherwise, and a declaration after #pragma pack.
    snprintf(header, sizeof(header), "%s/q.h", directory);
    written = ready && parse_test_write(header, check_attested_test_otherwise_header, strlen(check_attested_test_otherwise_header));
    {
        CheckAttested census = { .passes = 1, .records = records, .record_capacity = 1024 };
        const char* options[] = { "-nostdinc", "-c" };
        const bool ran = written && records != NULL &&
                         check_attested_test_command(&census, directory, "", check_attested_test_otherwise_units, "", 4, options, BUSTER_ARRAY_LENGTH(options), 0);
        check_test_expect(test, ran && check_attested_test_lines(&census, "q.h", check_attested_test_otherwise_header, 0xf),
                          "measure-attested: a declaration under another typedef or after #pragma pack is another hash");
    }
    unlink(header);
    // A prototype in a system header and the same text in a user's.
    char system[640];
    snprintf(system, sizeof(system), "%s/system", directory);
    snprintf(header, sizeof(header), "%s/s.h", system);
    char user[640];
    snprintf(user, sizeof(user), "%s/u.h", directory);
    written = ready && mkdir(system, 0700) == 0 && parse_test_write(header, check_attested_test_system_header, strlen(check_attested_test_system_header)) &&
              parse_test_write(user, check_attested_test_user_header, strlen(check_attested_test_user_header));
    {
        CheckAttested census = { .passes = 1, .records = records, .record_capacity = 1024 };
        const char* options[] = { "-nostdinc", "-isystem", system, "-c" };
        const bool ran = written && records != NULL &&
                         check_attested_test_command(&census, directory, "", check_attested_test_system_units, "", 3, options, BUSTER_ARRAY_LENGTH(options), 0);
        check_test_expect(test, ran && check_attested_test_lines(&census, "s.h", check_attested_test_system_header, 0x7) &&
                                    check_attested_test_lines(&census, "u.h", check_attested_test_user_header, 0x7),
                          "measure-attested: a system header's declaration and a user's of the same text are two hashes");
    }
    unlink(header);
    unlink(user);
    rmdir(system);
    free(records);
    // The command refuses its own lanes' options and a run without a file.
    {
        CheckAttested census = { .passes = 1 };
        char* lanes[] = { "--lanes", "2", "unit.c" };
        check_test_expect(test, check_attested_run(&census, BUSTER_ARRAY_LENGTH(lanes), lanes, true) == 2, "measure-attested refuses --lanes, which are its own");
        CheckAttested empty = { .passes = 1 };
        check_test_expect(test, check_attested_run(&empty, 0, NULL, true) == 2, "measure-attested refuses a run without a file");
    }
    if (ready)
    {
        rmdir(directory);
    }
}

#endif
