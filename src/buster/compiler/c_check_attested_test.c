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
    const bool clean = census->unlisted_declares == 0 && census->unlisted_completions == 0 && census->unlisted_uses == 0 && census->contradictions == 0 &&
                       census->twin_differences == 0;
    if (valid && (result != status || !clean))
    {
        fprintf(stderr, "test-checker: attested: the command's status %d (expected %d), unlisted declares %" PRIu64 " completions %" PRIu64 " uses %" PRIu64
                        ", contradictions %" PRIu64 ", twins' differences %" PRIu64 "\n",
                result, status, census->unlisted_declares, census->unlisted_completions, census->unlisted_uses, census->contradictions, census->twin_differences);
    }
    return valid && result == status && clean;
}

// ---------------------------------------------------------------------------------------------------------------------
// The compile's own (#2531's steps 1 and 2, --attested): a command's objects and diagnostics by --attested=on and verify
// are --attested=off's bytes, which reads every declaration.

// The compile fixture's header: the main one's kinds of declaration, with the runtime functions a struct's copy and its
// zeroing call by name (memcpy, memset), a prototype whose name a unit's undeclared call is a typo of, and pragmas
// between declarations, whose places a unit's view moves down past the skipped ones (#2531's step 3).
static const char check_attested_test_compile_header[] =
    "typedef unsigned long size_t;\n"
    "typedef struct _IO_FILE FILE;\n"
    "struct _IO_FILE { int fd; size_t count; };\n"
    "int puts(const char *);\n"
    "int fclose(FILE *);\n"
    "int abs(int);\n"
    "void *memcpy(void *, const void *, size_t);\n"
    "void *memset(void *, int, size_t);\n"
    "#pragma pack(push, 2)\n"
    "enum color { RED, GREEN = RED + 2, BLUE };\n"
    "struct later;\n"
    "struct later { int a; };\n"
    "#pragma pack(pop)\n"
    "struct later *make_later(void);\n"
    "struct made *make(void);\n"
    "static int hidden(void);\n"
    "int tentative;\n"
    "int labelled(void) __asm__(\"labelled_symbol\");\n"
    "inline int twice(int x) { return 2 * x; }\n"
    "int twice(int);\n"
    "void die(void);\n"
    "int prototyped(void);\n"
    "int visible(struct invisible *);\n"
    "extern int shared_counter;\n";

// Its units: the first learns; the others call by name, by a builtin, by an asm label, copy and zero a struct, name an
// enumerator, and name string literals, __func__ and labels by address after skipped declarations (whose local symbols a
// token's index numbers, the parse's in a view, Parser.origins) under a pragma that a view moves; the last two call an
// undeclared function whose name is a typo of a skipped prototype's (clang's note suggests it, which only a reading with
// nothing skipped finds) and name an undeclared identifier.
static const char* const check_attested_test_compile_units[] =
{
    "int one(void) { return puts(\"one\") + shared_counter; }\n",
    "int two(FILE *f) { return fclose(f) + __builtin_abs(-2) + labelled(); }\n",
    "struct big { char b[300]; };\nvoid copy(struct big *x, const struct big *y) { *x = *y; }\nstruct big zero(void) { struct big z = { 0 }; return z; }\n",
    "int four(int x) { if (x == BLUE) die(); return make_later() != 0; }\n",
    "#pragma pack(push, 1)\nstruct packed_here { char c; int i; };\n#pragma pack(pop)\n"
    "int seven(int x) { static void *where[] = { &&a, &&b }; goto *where[x & 1]; a: return puts(\"seven\"); b: return (int)sizeof(struct packed_here) + (int)sizeof(__func__); }\n",
    "int five(void) { return prototypex(); }\n",
    "int six(void) { return undeclared_name; }\n",
};

// Whether files `first` and `second` hold the same bytes.
static bool check_attested_test_same(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    bool equal = a != NULL && b != NULL;
    bool complete = false;
    while (equal && !complete)
    {
        unsigned char left[4096];
        unsigned char right[4096];
        const size_t count_a = fread(left, 1, sizeof(left), a);
        const size_t count_b = fread(right, 1, sizeof(right), b);
        equal = count_a == count_b && memcmp(left, right, count_a) == 0 && !ferror(a) && !ferror(b);
        complete = count_a < sizeof(left);
    }
    if (a != NULL)
    {
        fclose(a);
    }
    if (b != NULL)
    {
        fclose(b);
    }
    return equal;
}

// The compile fixture's units [`first`, `first` + `count`) (written in `directory`, padded so that the first is the
// costliest) compiled as one command with `options` and --attested=`mode`, in `directory`'s subdirectory `name`, its
// diagnostics appended to diagnostics.err there: whether it ran with status `status`. The command's counts stay in
// check_units_attested_last.
static bool check_attested_test_unit_range(const char* directory, const char* name, const char* mode, const char* const* options, uint32_t option_count, uint32_t first,
                                      uint32_t count, int status)
{
    char paths[8][24];
    char* arguments[24];
    char attested[32];
    uint32_t argument_count = 0;
    bool valid = first + count <= 8 && option_count + count + 1 < 24;
    snprintf(attested, sizeof(attested), "--attested=%s", mode);
    arguments[argument_count] = attested;
    argument_count += 1;
    for (uint32_t o = 0; valid && o < option_count; o += 1)
    {
        arguments[argument_count] = (char*)options[o];
        argument_count += 1;
    }
    for (uint32_t u = first; valid && u < first + count; u += 1)
    {
        char text[4096];
        const int padding = (int)(count - u) * 256;
        const int length = snprintf(text, sizeof(text), "/*%*s*/\n#include \"p.h\"\n%s", padding, "", check_attested_test_compile_units[u]);
        char path[640];
        snprintf(path, sizeof(path), "%s/unit%u.c", directory, u + 1);
        valid = length > 0 && (size_t)length < sizeof(text) && parse_test_write(path, text, (size_t)length);
        snprintf(paths[u], sizeof(paths[u]), "../unit%u.c", u + 1);
        arguments[argument_count] = paths[u];
        argument_count += 1;
    }
    char subdirectory[640];
    snprintf(subdirectory, sizeof(subdirectory), "%s/%s", directory, name);
    valid = valid && (mkdir(subdirectory, 0700) == 0 || errno == EEXIST);
    const int saved = valid ? open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    valid = valid && saved >= 0 && chdir(subdirectory) == 0;
    fflush(stderr);
    const int errors = valid ? dup(STDERR_FILENO) : -1;
    const int captured = valid ? open("diagnostics.err", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600) : -1;
    const bool redirected = errors >= 0 && captured >= 0 && dup2(captured, STDERR_FILENO) >= 0;
    const int result = valid && redirected ? run_check_command_mode((int)argument_count, arguments, true, true) : -1;
    fflush(stderr);
    if (errors >= 0)
    {
        dup2(errors, STDERR_FILENO);
        close(errors);
    }
    if (captured >= 0)
    {
        close(captured);
    }
    if (saved >= 0)
    {
        valid = fchdir(saved) == 0 && valid;
        close(saved);
    }
    if (valid && result != status)
    {
        fprintf(stderr, "test-checker: attested: --attested=%s's command %s ended with status %d (expected %d)\n", mode, name, result, status);
    }
    return valid && result == status;
}

// The compile fixture's first `count` units as one command (check_attested_test_unit_range), into a subdirectory `name` whose
// diagnostics.err starts empty.
static bool check_attested_test_compile(const char* directory, const char* name, const char* mode, const char* const* options, uint32_t option_count, uint32_t count,
                                        int status)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/%s/diagnostics.err", directory, name);
    unlink(path);
    return check_attested_test_unit_range(directory, name, mode, options, option_count, 0, count, status);
}

// Whether directory `first`'s `count` objects and diagnostics are directory `second`'s bytes (both in `directory`).
static bool check_attested_test_compiled_same(const char* directory, const char* first, const char* second, uint32_t count, bool objects)
{
    bool same = true;
    char a[700];
    char b[700];
    for (uint32_t u = 0; objects && u < count; u += 1)
    {
        snprintf(a, sizeof(a), "%s/%s/unit%u.o", directory, first, u + 1);
        snprintf(b, sizeof(b), "%s/%s/unit%u.o", directory, second, u + 1);
        const bool equal = check_attested_test_same(a, b);
        if (!equal)
        {
            fprintf(stderr, "test-checker: attested: unit%u.o of %s isn't %s's\n", u + 1, first, second);
        }
        same = same && equal;
    }
    snprintf(a, sizeof(a), "%s/%s/diagnostics.err", directory, first);
    snprintf(b, sizeof(b), "%s/%s/diagnostics.err", directory, second);
    const bool diagnostics = check_attested_test_same(a, b);
    if (!diagnostics)
    {
        fprintf(stderr, "test-checker: attested: %s's diagnostics aren't %s's\n", first, second);
    }
    return same && diagnostics;
}

// Removes the compile fixture's subdirectory `name` of `directory` and what its command wrote there.
static void check_attested_test_compiled_remove(const char* directory, const char* name, uint32_t count)
{
    char path[700];
    for (uint32_t u = 0; u < count; u += 1)
    {
        snprintf(path, sizeof(path), "%s/%s/unit%u.o", directory, name, u + 1);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/%s/diagnostics.err", directory, name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    rmdir(path);
}

static __attribute__((noinline)) void check_test_attested_compile(CheckTest* test, const char* directory)
{
    char header[640];
    snprintf(header, sizeof(header), "%s/p.h", directory);
    const bool written = parse_test_write(header, check_attested_test_compile_header, strlen(check_attested_test_compile_header));
    const uint32_t units = BUSTER_ARRAY_LENGTH(check_attested_test_compile_units);
    // Every unit by -c, on one lane: the last two fail the command.
    {
        const char* options[] = { "-nostdinc", "-std=gnu17", "--lanes", "1", "-c" };
        const bool off = written && check_attested_test_compile(directory, "off", "off", options, BUSTER_ARRAY_LENGTH(options), units, 1);
        const bool on = written && check_attested_test_compile(directory, "on", "on", options, BUSTER_ARRAY_LENGTH(options), units, 1);
        uint64_t counts[CHECK_ATTESTED_SUMMARY];
        memcpy(counts, check_units_attested_last, sizeof(counts));
        const bool verify = written && check_attested_test_compile(directory, "verify", "verify", options, BUSTER_ARRAY_LENGTH(options), units, 1);
        check_test_expect(test, off && on && verify && check_attested_test_compiled_same(directory, "on", "off", units - 2, true) &&
                                    check_attested_test_compiled_same(directory, "verify", "off", units - 2, true),
                          "--attested=on's and verify's objects and diagnostics are --attested=off's, two units' errors among them");
        check_test_expect(test, on && counts[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0 && counts[CHECK_ATTESTED_SUMMARY_SKIPPING] >= units - 1 &&
                                    counts[CHECK_ATTESTED_SUMMARY_AGAIN] == 2 && counts[CHECK_ATTESTED_SUMMARY_CONTRADICTIONS] == 0 &&
                                    counts[CHECK_ATTESTED_SUMMARY_UNLISTED] == 0,
                          "--attested=on skips in every unit after the first, and reads each unit with an error again with nothing skipped");
        check_test_expect(test, on && counts[CHECK_ATTESTED_SUMMARY_VIEWS] == counts[CHECK_ATTESTED_SUMMARY_SKIPPING],
                          "--attested=on reads every unit that skips as its view, the parse without its skipped declarations");
        check_test_expect(test, verify && check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPING] == 0 && check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0 &&
                                    check_units_attested_last[CHECK_ATTESTED_SUMMARY_AGAIN] == 0 && check_units_attested_last[CHECK_ATTESTED_SUMMARY_CONTRADICTIONS] == 0,
                          "--attested=verify reads what it would skip, and no hit's reading contradicts its memo");
        check_attested_test_compiled_remove(directory, "off", units);
        check_attested_test_compiled_remove(directory, "on", units);
        check_attested_test_compiled_remove(directory, "verify", units);
    }
    // The units without an error on two lanes, by -c and by -fsyntax-only -Wall -Wextra.
    {
        const char* options[] = { "-nostdinc", "-std=gnu17", "--lanes", "2", "-c" };
        const bool off = written && check_attested_test_compile(directory, "off", "off", options, BUSTER_ARRAY_LENGTH(options), units - 2, 0);
        const bool on = written && check_attested_test_compile(directory, "on", "on", options, BUSTER_ARRAY_LENGTH(options), units - 2, 0);
        check_test_expect(test, off && on && check_attested_test_compiled_same(directory, "on", "off", units - 2, true) &&
                                    check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0,
                          "--attested=on on two lanes: each lane's memo, and the objects --attested=off's");
        check_attested_test_compiled_remove(directory, "off", units);
        check_attested_test_compiled_remove(directory, "on", units);
        const char* warnings[] = { "-nostdinc", "-std=gnu17", "-Wall", "-Wextra", "--lanes", "1", "-fsyntax-only" };
        const bool warned_off = written && check_attested_test_compile(directory, "off", "off", warnings, BUSTER_ARRAY_LENGTH(warnings), units - 2, 0);
        const bool warned_on = written && check_attested_test_compile(directory, "on", "on", warnings, BUSTER_ARRAY_LENGTH(warnings), units - 2, 0);
        check_test_expect(test, warned_off && warned_on && check_attested_test_compiled_same(directory, "on", "off", units - 2, false) &&
                                    check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0,
                          "--attested=on's diagnostics by -fsyntax-only -Wall -Wextra are --attested=off's");
        check_attested_test_compiled_remove(directory, "off", units);
        check_attested_test_compiled_remove(directory, "on", units);
    }
    // -g: the DWARF describes what the unit's definitions reach (check_debug_value_roots), which no skipped declaration is,
    // so -g skips, and its objects are --attested=off's; and --scalar skips nothing.
    {
        // (Each command in the directory `debug`, which its DWARF names, moved aside after it.)
        const char* debug[] = { "-nostdinc", "-std=gnu17", "-g", "--lanes", "1", "-c" };
        char from[700];
        char to[700];
        snprintf(from, sizeof(from), "%s/debug", directory);
        snprintf(to, sizeof(to), "%s/off", directory);
        const bool off = written && check_attested_test_compile(directory, "debug", "off", debug, BUSTER_ARRAY_LENGTH(debug), units - 2, 0) && rename(from, to) == 0;
        snprintf(to, sizeof(to), "%s/on", directory);
        const bool on = written && check_attested_test_compile(directory, "debug", "on", debug, BUSTER_ARRAY_LENGTH(debug), units - 2, 0) && rename(from, to) == 0;
        check_test_expect(test, off && on && check_attested_test_compiled_same(directory, "on", "off", units - 2, true) &&
                                    check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0,
                          "-g skips attested declarations, and its objects (their DWARF) are --attested=off's");
        check_attested_test_compiled_remove(directory, "on", units);
        const char* scalar[] = { "-nostdinc", "-std=gnu17", "--scalar", "--lanes", "1", "-c" };
        const bool scalar_on = written && check_attested_test_compile(directory, "on", "on", scalar, BUSTER_ARRAY_LENGTH(scalar), units - 2, 0);
        check_test_expect(test, scalar_on && check_units_attested_last[CHECK_ATTESTED_SUMMARY_UNITS] == 0, "--scalar runs no attested declarations' passes");
        check_attested_test_compiled_remove(directory, "off", units);
        check_attested_test_compiled_remove(directory, "on", units);
    }
    for (uint32_t u = 0; u < units; u += 1)
    {
        char path[640];
        snprintf(path, sizeof(path), "%s/unit%u.c", directory, u + 1);
        unlink(path);
    }
    unlink(header);
}

// The memo files in `memo` (step 4's directory): how many, and their bytes in all; with `scramble`, each one's header
// overwritten first, as a torn or foreign file's would be.
static uint32_t check_attested_test_memos(const char* memo, bool scramble, uint64_t* bytes)
{
    uint32_t files = 0;
    *bytes = 0;
    DIR* listing = opendir(memo);
    for (struct dirent* entry = listing != NULL ? readdir(listing) : NULL; entry != NULL; entry = readdir(listing))
    {
        char path[800];
        snprintf(path, sizeof(path), "%s/%s", memo, entry->d_name);
        struct stat status;
        const bool file = entry->d_name[0] != '.' && stat(path, &status) == 0 && S_ISREG(status.st_mode);
        files += file ? 1u : 0u;
        *bytes += file ? (uint64_t)status.st_size : 0;
        const int handle = file && scramble ? open(path, O_WRONLY | O_CLOEXEC) : -1;
        if (handle >= 0)
        {
            const char garbage[8] = "garbage!";
            const bool wrote = pwrite(handle, garbage, sizeof(garbage), 0) == (ssize_t)sizeof(garbage);
            (void)wrote;
            close(handle);
        }
    }
    if (listing != NULL)
    {
        closedir(listing);
    }
    return files;
}

// Removes step 4's memo directory `memo` and its files.
static void check_attested_test_memos_remove(const char* memo)
{
    DIR* listing = opendir(memo);
    for (struct dirent* entry = listing != NULL ? readdir(listing) : NULL; entry != NULL; entry = readdir(listing))
    {
        char path[800];
        snprintf(path, sizeof(path), "%s/%s", memo, entry->d_name);
        if (entry->d_name[0] != '.')
        {
            unlink(path);
        }
    }
    if (listing != NULL)
    {
        closedir(listing);
    }
    rmdir(memo);
}

// Step 4, a memo kept across processes (--attested-memo): each of the compile fixture's units without an error compiled
// alone, by -c, first with no memo (--attested=off), then twice with the memo: a command of one source file runs the passes
// with it, and the second time each unit skips what the units before it, and its own first time, attested. Then the units
// as one command with the memo, which its first unit takes too; a memo of other options, a file of its own; and a file
// scrambled, which is a memo of nothing and is written again.
static __attribute__((noinline)) void check_test_attested_memo(CheckTest* test, const char* directory)
{
    char header[640];
    snprintf(header, sizeof(header), "%s/p.h", directory);
    const bool written = parse_test_write(header, check_attested_test_compile_header, strlen(check_attested_test_compile_header));
    const uint32_t units = BUSTER_ARRAY_LENGTH(check_attested_test_compile_units) - 2;
    char memo[700];
    snprintf(memo, sizeof(memo), "%s/memo", directory);
    char memo_option[720];
    snprintf(memo_option, sizeof(memo_option), "--attested-memo=%s", memo);
    const char* plain[] = { "-nostdinc", "-std=gnu17", "-c" };
    const char* kept[] = { "-nostdinc", "-std=gnu17", memo_option, "-c" };
    const char* warned[] = { "-nostdinc", "-std=gnu17", "-Wall", memo_option, "-c" };
    char path[700];
    const char* const names[] = { "off", "cold", "warm", "on", "failed-off", "failed" };
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        snprintf(path, sizeof(path), "%s/%s/diagnostics.err", directory, names[n]);
        unlink(path);
    }
    bool off = written;
    bool cold = written;
    bool warm = written;
    uint64_t skipped_cold = 0;
    uint64_t skipped_warm = 0;
    uint64_t views = 0;
    for (uint32_t u = 0; u < units; u += 1)
    {
        off = off && check_attested_test_unit_range(directory, "off", "off", plain, BUSTER_ARRAY_LENGTH(plain), u, 1, 0);
        cold = cold && check_attested_test_unit_range(directory, "cold", "on", kept, BUSTER_ARRAY_LENGTH(kept), u, 1, 0);
        skipped_cold += check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED];
    }
    uint64_t bytes = 0;
    const uint32_t files = check_attested_test_memos(memo, false, &bytes);
    for (uint32_t u = 0; u < units; u += 1)
    {
        warm = warm && check_attested_test_unit_range(directory, "warm", "on", kept, BUSTER_ARRAY_LENGTH(kept), u, 1, 0);
        skipped_warm += check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED];
        views += check_units_attested_last[CHECK_ATTESTED_SUMMARY_VIEWS];
    }
    check_test_expect(test, off && cold && warm && check_attested_test_compiled_same(directory, "cold", "off", units, true) &&
                                check_attested_test_compiled_same(directory, "warm", "off", units, true),
                      "--attested-memo: each unit's object and diagnostics by its own command, with the memo cold and warm, are --attested=off's");
    check_test_expect(test, files == 1 && bytes > BUSTER_ATTESTED_MEMO_HEADER && (bytes - BUSTER_ATTESTED_MEMO_HEADER) % sizeof(CheckAttestedHash) == 0 &&
                                skipped_cold != 0 && skipped_warm > skipped_cold && views == units,
                      "--attested-memo: one file, whole records; a command of one source file skips what earlier commands learned, every one of them warm");
    // The two units with errors, each by a command of its own with the memo warm: each skips, fails, and is read again
    // whole, its diagnostics the second reading's alone, --attested=off's (a stream of the command's own, into which the
    // preprocessor's and the checker's print too).
    bool failed = written;
    uint64_t failed_again = 0;
    for (uint32_t u = units; u < units + 2; u += 1)
    {
        failed = failed && check_attested_test_unit_range(directory, "failed-off", "off", plain, BUSTER_ARRAY_LENGTH(plain), u, 1, 1);
        failed = failed && check_attested_test_unit_range(directory, "failed", "on", kept, BUSTER_ARRAY_LENGTH(kept), u, 1, 1);
        failed_again += check_units_attested_last[CHECK_ATTESTED_SUMMARY_AGAIN];
    }
    check_test_expect(test, failed && failed_again == 2 && check_attested_test_compiled_same(directory, "failed", "failed-off", 0, false),
                      "--attested-memo: a command of one source file that skipped and failed is read again, its diagnostics --attested=off's");
    // The units as one command: its first unit skips too.
    const bool on = written && check_attested_test_compile(directory, "on", "on", kept, BUSTER_ARRAY_LENGTH(kept), units, 0);
    check_test_expect(test, on && check_attested_test_compiled_same(directory, "on", "off", units, true) &&
                                check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPING] == units,
                      "--attested-memo: a command of several source files loads the memo, so that its first unit skips too");
    // Other options: a file of their own.
    const bool other = written && check_attested_test_compile(directory, "on", "on", warned, BUSTER_ARRAY_LENGTH(warned), 1, 0);
    check_test_expect(test, other && check_attested_test_memos(memo, false, &bytes) == 2 && check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] == 0,
                      "--attested-memo: a command of other options keeps a memo of its own");
    // A scrambled file: a memo of nothing, written again whole.
    check_attested_test_memos(memo, true, &bytes);
    const bool scrambled = written && check_attested_test_unit_range(directory, "on", "on", kept, BUSTER_ARRAY_LENGTH(kept), 1, 1, 0);
    const uint64_t scrambled_skips = check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED];
    const bool again = written && check_attested_test_unit_range(directory, "on", "on", kept, BUSTER_ARRAY_LENGTH(kept), 1, 1, 0);
    check_test_expect(test, scrambled && again && scrambled_skips == 0 && check_units_attested_last[CHECK_ATTESTED_SUMMARY_SKIPPED] != 0,
                      "--attested-memo: a scrambled file is a memo of nothing, and is written again");
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        check_attested_test_compiled_remove(directory, names[n], units);
    }
    check_attested_test_memos_remove(memo);
    for (uint32_t u = 0; u < units + 2; u += 1)
    {
        snprintf(path, sizeof(path), "%s/unit%u.c", directory, u + 1);
        unlink(path);
    }
    unlink(header);
}

// The reader's state by class (docs/attested-declarations.md, "Risks and open questions": an input of a reading that the
// hash doesn't cover). Every field of CheckInput and CheckInputReader is listed here, in its struct's order, by what a
// file-scope declaration's reading may do with it, and check_test_attested_fields fails when a field is added, removed,
// renamed or moved without this list following it, so that whoever adds one says what a skipped reading would leave
// out of it:
// - OWN: a column by token (or by a declaration's token) that a reading writes at its own tokens alone. A skipped
//   declaration leaves its tokens' entries unwritten, and its view leaves its tokens out.
// - NAME: a name's, a tag's or a binding's entry, by key. A skipped declaration's names are ones no read code reaches
//   (the reach), so no reading looks up the entries it would have written.
// - TYPES: the type table and what is derived from types. A skipped declaration's types are named by no read code.
// - CONSEQUENCE: a list, count or flag that something after the reading reads: reports, warnings, constants, the graph,
//   emitted objects, the unit's end. An attested reading wrote none of them (it was silent and pure: the census's and
//   --attested=verify's test, check_attested_events), or it isn't attested.
// - AMBIENT: what the reading reads but doesn't write: options, the parse, pragma state (which the hash's words cover),
//   arenas and inputs.
// - TRANSIENT: the reader's state while one declaration, declarator or statement is read, reset before the next.
// - MEASURE: counts and times that only measurements read.
typedef enum CheckAttestedFieldClass
{
    CHECK_ATTESTED_FIELD_OWN = 1,
    CHECK_ATTESTED_FIELD_NAME,
    CHECK_ATTESTED_FIELD_TYPES,
    CHECK_ATTESTED_FIELD_CONSEQUENCE,
    CHECK_ATTESTED_FIELD_AMBIENT,
    CHECK_ATTESTED_FIELD_TRANSIENT,
    CHECK_ATTESTED_FIELD_MEASURE,
} CheckAttestedFieldClass;

typedef struct CheckAttestedField CheckAttestedField;
struct CheckAttestedField
{
    const char* name;
    uint8_t field_class;
};

static const CheckAttestedField check_attested_input_fields[] =
{
    { "graph", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "environment", CHECK_ATTESTED_FIELD_NAME },
    { "diagnostic_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "nonnull_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "pack_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "visibility_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "symbol_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "weak_names", CHECK_ATTESTED_FIELD_NAME },
    { "weak_name_count", CHECK_ATTESTED_FIELD_NAME },
    { "ms_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "ms_struct", CHECK_ATTESTED_FIELD_AMBIENT },
    { "object_visibility", CHECK_ATTESTED_FIELD_AMBIENT },
    { "visibility_error", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "loop_diagnostic", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "naked_diagnostic", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "sections", CHECK_ATTESTED_FIELD_AMBIENT },
    { "optimize_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "noderef_types", CHECK_ATTESTED_FIELD_TYPES },
    { "noderef_values", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noderef_members", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noderef_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noderef_member_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noderef_declarations", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noderef_declaration_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "pack_log2", CHECK_ATTESTED_FIELD_AMBIENT },
    { "fp_pragmas", CHECK_ATTESTED_FIELD_AMBIENT },
    { "fp_pragma_count", CHECK_ATTESTED_FIELD_AMBIENT },
    { "fp_environments", CHECK_ATTESTED_FIELD_AMBIENT },
    { "cx_limited_ranges", CHECK_ATTESTED_FIELD_AMBIENT },
    { "deferred_tokens", CHECK_ATTESTED_FIELD_AMBIENT },
    { "intrinsic_declarations", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_declaration_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_declaration_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_calls", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_call_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_call_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "intrinsic_ids", CHECK_ATTESTED_FIELD_NAME },
    { "overload_heads", CHECK_ATTESTED_FIELD_NAME },
    { "reports", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "report_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "report_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "recovered_reports", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "recovered_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "recovered_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "recovered_published", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "recovered_valid", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "invalid_declarations", CHECK_ATTESTED_FIELD_OWN },
    { "invalid_count", CHECK_ATTESTED_FIELD_OWN },
    { "ambiguous_index", CHECK_ATTESTED_FIELD_OWN },
    { "multiversion_reports", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "multiversion_report_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "multiversion_report_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "implicit_function_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "implicit_function_corrections", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "implicit_function_builtins", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "implicit_function_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "typo_searched", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "corrected_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "corrected_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "parameter_typo_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "parameter_typo_details", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "parameter_typo_types", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "parameter_typo_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "implicit_function_diagnostic", CHECK_ATTESTED_FIELD_AMBIENT },
    { "implicit_library_diagnostic", CHECK_ATTESTED_FIELD_AMBIENT },
    { "implicit_builtin_diagnostic", CHECK_ATTESTED_FIELD_AMBIENT },
    { "implicit_function_c99", CHECK_ATTESTED_FIELD_AMBIENT },
    { "common_tentatives", CHECK_ATTESTED_FIELD_AMBIENT },
    { "implicit_function_system", CHECK_ATTESTED_FIELD_AMBIENT },
    { "declarations", CHECK_ATTESTED_FIELD_OWN },
    { "declared_types", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_kinds", CHECK_ATTESTED_FIELD_OWN },
    { "enum_values", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "enum_value_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "enum_definitions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "enum_definition_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "enum_operands", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "enum_names", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "function_flags", CHECK_ATTESTED_FIELD_OWN },
    { "implicit_library_types", CHECK_ATTESTED_FIELD_TYPES },
    { "kr_parameter_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "kr_diagnostics", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "kr_diagnostic_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "kr_warning_severities", CHECK_ATTESTED_FIELD_AMBIENT },
    { "kr_warning_policy", CHECK_ATTESTED_FIELD_AMBIENT },
    { "kr_warning_quiet", CHECK_ATTESTED_FIELD_AMBIENT },
    { "kr_warning_system", CHECK_ATTESTED_FIELD_AMBIENT },
    { "relaxed_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "pedantic_escapes", CHECK_ATTESTED_FIELD_AMBIENT },
    { "kr_warning_errors", CHECK_ATTESTED_FIELD_AMBIENT },
    { "x86_range_severities", CHECK_ATTESTED_FIELD_AMBIENT },
    { "x86_range_quiet", CHECK_ATTESTED_FIELD_AMBIENT },
    { "x86_range_policy", CHECK_ATTESTED_FIELD_AMBIENT },
    { "odr_severity", CHECK_ATTESTED_FIELD_AMBIENT },
    { "odr_quiet", CHECK_ATTESTED_FIELD_AMBIENT },
    { "odr_system", CHECK_ATTESTED_FIELD_AMBIENT },
    { "fatal_errors", CHECK_ATTESTED_FIELD_AMBIENT },
    { "explicit_abis", CHECK_ATTESTED_FIELD_OWN },
    { "objects", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_bits", CHECK_ATTESTED_FIELD_OWN },
    { "attributes", CHECK_ATTESTED_FIELD_OWN },
    { "typedef_result_contracts", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ignored_asm_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "asm_warnings_prepared", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "sparse_pool", CHECK_ATTESTED_FIELD_AMBIENT },
    { "initializer_nodes", CHECK_ATTESTED_FIELD_OWN },
    { "initializer_begins", CHECK_ATTESTED_FIELD_OWN },
    { "initializer_ends", CHECK_ATTESTED_FIELD_OWN },
    { "token_nodes", CHECK_ATTESTED_FIELD_OWN },
    { "evaluation_nodes", CHECK_ATTESTED_FIELD_OWN },
    { "packed_members", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "packed_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "marked_members", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "marked_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "marked_member_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noreturn_members", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noreturn_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "noreturn_member_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "marked_overloads", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "marked_overload_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "conditions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_declarations", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_declaration_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_declaration_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "type_tags", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "availability_contexts", CHECK_ATTESTED_FIELD_OWN },
    { "availability_texts", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "availability_text_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "availability_text_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "warning_events", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "pragma_warnings", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "const_values", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "constant_warnings", CHECK_ATTESTED_FIELD_OWN },
    { "constant_warning_count", CHECK_ATTESTED_FIELD_OWN },
    { "constant_warnings_prepared", CHECK_ATTESTED_FIELD_OWN },
    { "constant_warning_ignored", CHECK_ATTESTED_FIELD_OWN },
    { "body_declarations", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_bodies", CHECK_ATTESTED_FIELD_OWN },
    { "parameters", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_next", CHECK_ATTESTED_FIELD_OWN },
    { "control_owners", CHECK_ATTESTED_FIELD_OWN },
    { "label_targets", CHECK_ATTESTED_FIELD_OWN },
    { "label_address_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "case_ranges", CHECK_ATTESTED_FIELD_OWN },
    { "regions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "expression_regions", CHECK_ATTESTED_FIELD_OWN },
    { "region_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vlas", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vla_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vla_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cleanups", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cleanup_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cleanup_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "musttails", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "musttail_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "musttail_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "interrupt_calls", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "redeclaration_links", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "redeclaration_link_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "redeclaration_link_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "interrupt_ranges", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "interrupt_range_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "interrupt_range_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "asm_statements", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "asm_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "global_register_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "asm_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "asm_ids", CHECK_ATTESTED_FIELD_OWN },
    { "strings", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "string_ids", CHECK_ATTESTED_FIELD_OWN },
    { "string_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "token_count", CHECK_ATTESTED_FIELD_AMBIENT },
    { "literal_tables", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "literal_tables_last", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "literal_table_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "binding_statistics", CHECK_ATTESTED_FIELD_MEASURE },
    { "binding_event_count", CHECK_ATTESTED_FIELD_MEASURE },
    { "parser_binding_statistics", CHECK_ATTESTED_FIELD_MEASURE },
    { "parser_binding_event_count", CHECK_ATTESTED_FIELD_MEASURE },
    { "visit_misses", CHECK_ATTESTED_FIELD_MEASURE },
    { "resolve_visits", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_visits", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_acted", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_left", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_ticks", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_part_ticks", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_faults", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_system_us", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_usage_failed", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_class_ticks", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_class_overhead", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_numbering_ticks", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_numbering_nodes", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_numbering_checked", CHECK_ATTESTED_FIELD_MEASURE },
    { "expression_numbering_mismatches", CHECK_ATTESTED_FIELD_MEASURE },
    { "node_front", CHECK_ATTESTED_FIELD_MEASURE },
    { "sizeof_queries", CHECK_ATTESTED_FIELD_MEASURE },
    { "sizeof_query_tokens", CHECK_ATTESTED_FIELD_MEASURE },
    { "diagnostic_limits", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "diagnostic_report_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "walk_end", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "diagnostic_graph", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "rare_columns_shared", CHECK_ATTESTED_FIELD_AMBIENT },
    { "cpu_dispatch", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "no_builtin", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_multiversion", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_candidates", CHECK_ATTESTED_FIELD_OWN },
    { "resource", CHECK_ATTESTED_FIELD_AMBIENT },
    { "sugar", CHECK_ATTESTED_FIELD_TYPES },
    { "sugar_tokens", CHECK_ATTESTED_FIELD_TYPES },
    { "sugar_nodes", CHECK_ATTESTED_FIELD_TYPES },
    { "sugar_mask", CHECK_ATTESTED_FIELD_TYPES },
    { "sugar_entries", CHECK_ATTESTED_FIELD_TYPES },
    { "window_summaries", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_rows", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_row_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_row_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "window_nodes", CHECK_ATTESTED_FIELD_MEASURE },
    { "window_condition_candidates", CHECK_ATTESTED_FIELD_MEASURE },
    { "window_condition_bounded", CHECK_ATTESTED_FIELD_MEASURE },
    { "window_condition_count", CHECK_ATTESTED_FIELD_MEASURE },
};

static const CheckAttestedField check_attested_reader_fields[] =
{
    { "output", CHECK_ATTESTED_FIELD_AMBIENT },
    { "parser", CHECK_ATTESTED_FIELD_AMBIENT },
    { "pp", CHECK_ATTESTED_FIELD_AMBIENT },
    { "types", CHECK_ATTESTED_FIELD_TYPES },
    { "arena", CHECK_ATTESTED_FIELD_AMBIENT },
    { "scratch", CHECK_ATTESTED_FIELD_AMBIENT },
    { "warning_events", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "const_values", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "fp_eval_uses", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "fp_eval_use_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "const_warning_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "literal_tables_disabled", CHECK_ATTESTED_FIELD_AMBIENT },
    { "literal_warnings", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "source_warning_keys", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "variable_sized_records", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "union_tokens", CHECK_ATTESTED_FIELD_TYPES },
    { "member_marks", CHECK_ATTESTED_FIELD_OWN },
    { "evaluated_members", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "evaluated_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "evaluated_member_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "warning_bound_open", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "warning_query_arena", CHECK_ATTESTED_FIELD_AMBIENT },
    { "warning_queries", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "bindings", CHECK_ATTESTED_FIELD_NAME },
    { "declared_types", CHECK_ATTESTED_FIELD_OWN },
    { "query_types", CHECK_ATTESTED_FIELD_OWN },
    { "sizeof_types", CHECK_ATTESTED_FIELD_OWN },
    { "sizeof_capture", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_jobs", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_finishes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_owner", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_storage", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_auto_type", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_c23_auto", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_constexpr", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_restrict", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_query_depth", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_pending", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_free_standing", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "tag_attributes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "tag_name_list", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "tag_name_list_first", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "tag_name_list_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "tag_name_list_subjects", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constexpr_values", CHECK_ATTESTED_FIELD_NAME },
    { "constexpr_count", CHECK_ATTESTED_FIELD_NAME },
    { "constexpr_index", CHECK_ATTESTED_FIELD_NAME },
    { "constexpr_indexed", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constexpr_index_owner", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "implicit_function_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "fallthrough_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "fallthrough_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "fallthrough_block_next", CHECK_ATTESTED_FIELD_OWN },
    { "fallthrough_block_last", CHECK_ATTESTED_FIELD_OWN },
    { "fallthrough_block_state", CHECK_ATTESTED_FIELD_OWN },
    { "fallthrough_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "deferred_statements", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "deferred_statement_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "deferred_statement_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "selection_arms", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "selection_arm_base", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "selection_arm_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "kr_diagnostic_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "function_specifiers", CHECK_ATTESTED_FIELD_OWN },
    { "function_name_ids", CHECK_ATTESTED_FIELD_AMBIENT },
    { "file_function_name_ids", CHECK_ATTESTED_FIELD_AMBIENT },
    { "function_name_body", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "return_types", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_kinds", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_bits", CHECK_ATTESTED_FIELD_OWN },
    { "definition_origins", CHECK_ATTESTED_FIELD_OWN },
    { "objects", CHECK_ATTESTED_FIELD_OWN },
    { "ignored", CHECK_ATTESTED_FIELD_OWN },
    { "initializers", CHECK_ATTESTED_FIELD_OWN },
    { "static_initializers", CHECK_ATTESTED_FIELD_OWN },
    { "initializer_declarations", CHECK_ATTESTED_FIELD_OWN },
    { "initializer_finish_heads", CHECK_ATTESTED_FIELD_OWN },
    { "initializer_finish_next", CHECK_ATTESTED_FIELD_OWN },
    { "incomplete_array_types", CHECK_ATTESTED_FIELD_OWN },
    { "inference_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "string_ids", CHECK_ATTESTED_FIELD_OWN },
    { "designator_indices", CHECK_ATTESTED_FIELD_OWN },
    { "designator_valid", CHECK_ATTESTED_FIELD_OWN },
    { "designator_tokens", CHECK_ATTESTED_FIELD_OWN },
    { "designator_ranges", CHECK_ATTESTED_FIELD_OWN },
    { "selected_tokens", CHECK_ATTESTED_FIELD_OWN },
    { "selection_type_ends", CHECK_ATTESTED_FIELD_OWN },
    { "selection_types", CHECK_ATTESTED_FIELD_OWN },
    { "cached_constants", CHECK_ATTESTED_FIELD_OWN },
    { "constant_present", CHECK_ATTESTED_FIELD_OWN },
    { "cached_constant_count", CHECK_ATTESTED_FIELD_OWN },
    { "builtin_names", CHECK_ATTESTED_FIELD_NAME },
    { "intrinsic_names", CHECK_ATTESTED_FIELD_NAME },
    { "intrinsic_source", CHECK_ATTESTED_FIELD_NAME },
    { "library_names", CHECK_ATTESTED_FIELD_NAME },
    { "library_names_mask", CHECK_ATTESTED_FIELD_NAME },
    { "library_names_known", CHECK_ATTESTED_FIELD_NAME },
    { "gnu_rows", CHECK_ATTESTED_FIELD_NAME },
    { "enum_keys", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "enum_closes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "enum_types", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "overload_candidates", CHECK_ATTESTED_FIELD_NAME },
    { "overload_capacity", CHECK_ATTESTED_FIELD_NAME },
    { "overload_index", CHECK_ATTESTED_FIELD_NAME },
    { "declarator_overloadable", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "builtins", CHECK_ATTESTED_FIELD_OWN },
    { "builtin_type_ends", CHECK_ATTESTED_FIELD_OWN },
    { "builtin_syntax_effects", CHECK_ATTESTED_FIELD_OWN },
    { "builtin_syntax_count", CHECK_ATTESTED_FIELD_OWN },
    { "varargs", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vararg_calls", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vararg_types", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vararg_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vararg_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "vla_bounds", CHECK_ATTESTED_FIELD_OWN },
    { "vla_ends", CHECK_ATTESTED_FIELD_OWN },
    { "vla_scope_ends", CHECK_ATTESTED_FIELD_OWN },
    { "vla_sizes", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_vla_begin", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "parameter_vla_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "expression_commas", CHECK_ATTESTED_FIELD_OWN },
    { "conditions", CHECK_ATTESTED_FIELD_OWN },
    { "region_roots", CHECK_ATTESTED_FIELD_OWN },
    { "constant_contexts", CHECK_ATTESTED_FIELD_OWN },
    { "warning_suppressed", CHECK_ATTESTED_FIELD_OWN },
    { "warning_index", CHECK_ATTESTED_FIELD_OWN },
    { "warning_index_base", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "warning_index_ready", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "parallel_warning_ranges", CHECK_ATTESTED_FIELD_AMBIENT },
    { "declaration_specifiers", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_deduced", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_deduced_first", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_bases", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_starts", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_storage", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_flags", CHECK_ATTESTED_FIELD_OWN },
    { "declaration_abis", CHECK_ATTESTED_FIELD_OWN },
    { "attribute_bases", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_attributes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_attributes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_common", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "attribute_completion", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "recovered_pending", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_declarations", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "prototype_depth", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "kr_declarator", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_abi_origin", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_abi_origin", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_swift_places", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_swift_tokens", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_swift_count", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_swift_wrapped", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_swift_deferrable", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_no_prototype", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constant_abi_origin", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_noderef", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_noderef", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constant_noderef", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_thread_local", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_identifier_list", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_recover", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_names", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_names", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "specifier_names_mark", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_invalid", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "vla_initializer_dropped", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_group_invalid", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_lifetimebound", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "declarator_function_list", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "subject_function_list", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "subject_identifier_list", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "activate", CHECK_ATTESTED_FIELD_OWN },
    { "activation_next", CHECK_ATTESTED_FIELD_OWN },
    { "syntax", CHECK_ATTESTED_FIELD_AMBIENT },
    { "visits", CHECK_ATTESTED_FIELD_OWN },
    { "skips", CHECK_ATTESTED_FIELD_AMBIENT },
    { "token_levels", CHECK_ATTESTED_FIELD_AMBIENT },
    { "sparse_marks", CHECK_ATTESTED_FIELD_OWN },
    { "attribute_warned", CHECK_ATTESTED_FIELD_OWN },
    { "parameters", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_next", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_seen", CHECK_ATTESTED_FIELD_NAME },
    { "parameter_previous", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_shapes", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_declarations", CHECK_ATTESTED_FIELD_OWN },
    { "parameter_declaration_next", CHECK_ATTESTED_FIELD_OWN },
    { "member_seen", CHECK_ATTESTED_FIELD_NAME },
    { "linked_declarations", CHECK_ATTESTED_FIELD_NAME },
    { "linked_types", CHECK_ATTESTED_FIELD_NAME },
    { "library_declarations", CHECK_ATTESTED_FIELD_NAME },
    { "tag_types", CHECK_ATTESTED_FIELD_NAME },
    { "tag_ends", CHECK_ATTESTED_FIELD_NAME },
    { "enum_values", CHECK_ATTESTED_FIELD_OWN },
    { "rare_begin", CHECK_ATTESTED_FIELD_AMBIENT },
    { "rare_end", CHECK_ATTESTED_FIELD_AMBIENT },
    { "rare_claims", CHECK_ATTESTED_FIELD_AMBIENT },
    { "rare_marks", CHECK_ATTESTED_FIELD_AMBIENT },
    { "rare_gate", CHECK_ATTESTED_FIELD_AMBIENT },
    { "rare_gate_base", CHECK_ATTESTED_FIELD_AMBIENT },
    { "postorder", CHECK_ATTESTED_FIELD_AMBIENT },
    { "postorder_positions", CHECK_ATTESTED_FIELD_AMBIENT },
    { "subtree_sizes", CHECK_ATTESTED_FIELD_AMBIENT },
    { "tag_completions", CHECK_ATTESTED_FIELD_NAME },
    { "enum_fixed", CHECK_ATTESTED_FIELD_NAME },
    { "tag_declared", CHECK_ATTESTED_FIELD_NAME },
    { "dropped_tag", CHECK_ATTESTED_FIELD_NAME },
    { "transparent_unions", CHECK_ATTESTED_FIELD_NAME },
    { "ends", CHECK_ATTESTED_FIELD_AMBIENT },
    { "first_child", CHECK_ATTESTED_FIELD_AMBIENT },
    { "next_child", CHECK_ATTESTED_FIELD_AMBIENT },
    { "first_value", CHECK_ATTESTED_FIELD_OWN },
    { "last_value", CHECK_ATTESTED_FIELD_OWN },
    { "value_counts", CHECK_ATTESTED_FIELD_OWN },
    { "value_next", CHECK_ATTESTED_FIELD_OWN },
    { "expression_declined", CHECK_ATTESTED_FIELD_OWN },
    { "scope_marks", CHECK_ATTESTED_FIELD_OWN },
    { "scope_ends", CHECK_ATTESTED_FIELD_OWN },
    { "scope_openings", CHECK_ATTESTED_FIELD_OWN },
    { "scope_count", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "function_result", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "function_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "function_body", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "asm_vector_body", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "asm_vector_bits", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "naked_functions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cpu_dispatch", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cpu_invalid", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cpu_invalid_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cpu_invalid_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_versions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_mv_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_mv_since", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_mv_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_mv_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_bind_tokens", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_bind_redeclares", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_bind_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_bind_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "target_candidate_pool", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "section_events", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "section_event_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "section_event_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "section_record_from", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "cleanup_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "statement_attributes", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "weak_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "late_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "deprecated_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "condition_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "tcb_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "merged_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "objc_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "padding_sugar", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "type_tag_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "cf_error", CHECK_ATTESTED_FIELD_TYPES },
    { "salt_attributes", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "attribute_parameters", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "attribute_declaration", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "late_attribute_masks", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "late_attribute_kinds", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "declaration_scope_ends", CHECK_ATTESTED_FIELD_OWN },
    { "resolve_limit", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "resolve_pragma", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "resolve_diagnostic_token", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "resolve_position", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "resolve_message", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "expression_limit", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "fragment_end", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "indirect_gotos", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "predefined_names", CHECK_ATTESTED_FIELD_AMBIENT },
    { "predefined_types", CHECK_ATTESTED_FIELD_TYPES },
    { "builtin_member_names", CHECK_ATTESTED_FIELD_AMBIENT },
    { "position", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "predefined_positions", CHECK_ATTESTED_FIELD_TYPES },
    { "deferred", CHECK_ATTESTED_FIELD_AMBIENT },
    { "lane_reason", CHECK_ATTESTED_FIELD_AMBIENT },
    { "lane_tags", CHECK_ATTESTED_FIELD_AMBIENT },
    { "lane", CHECK_ATTESTED_FIELD_AMBIENT },
    { "lane_unsupported", CHECK_ATTESTED_FIELD_AMBIENT },
    { "c23_tags", CHECK_ATTESTED_FIELD_NAME },
    { "c23_earlier", CHECK_ATTESTED_FIELD_NAME },
    { "c23_earlier_keywords", CHECK_ATTESTED_FIELD_NAME },
    { "c23_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_member_tokens", CHECK_ATTESTED_FIELD_NAME },
    { "c23_member_attributes", CHECK_ATTESTED_FIELD_NAME },
    { "c23_member_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_member_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_failed", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_failed_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "c23_failed_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_declarations", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_partners", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_ends", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_index", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "ambiguous_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "invalid_redefinitions", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "invalid_redefinition_count", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "invalid_redefinition_capacity", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "typo", CHECK_ATTESTED_FIELD_CONSEQUENCE },
    { "typo_disabled", CHECK_ATTESTED_FIELD_AMBIENT },
    { "constant_names", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constant_named", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constant_missing", CHECK_ATTESTED_FIELD_TRANSIENT },
    { "constant_failure", CHECK_ATTESTED_FIELD_TRANSIENT },
};

// The fields __builtin_dump_struct finds, in order: the names of its lines at the struct's own level (an indent of two
// spaces; a nested record's fields are deeper).
typedef struct CheckAttestedFieldNames CheckAttestedFieldNames;
struct CheckAttestedFieldNames
{
    const char* names[512];
    uint32_t count;
};

static int check_attested_field_name(CheckAttestedFieldNames* names, const char* format, ...)
{
    va_list list;
    va_start(list, format);
    if (strncmp(format, "%s%s %s", 7) == 0)
    {
        const char* indent = va_arg(list, const char*);
        const char* type = va_arg(list, const char*);
        const char* name = va_arg(list, const char*);
        (void)type;
        if (strlen(indent) == 2 && names->count < BUSTER_ARRAY_LENGTH(names->names))
        {
            names->names[names->count] = name;
            names->count += 1;
        }
    }
    va_end(list);
    return 0;
}

// Whether `names` are `fields`' names, in order, each with a class; the first difference to stderr.
static bool check_attested_fields_same(const char* label, const CheckAttestedFieldNames* names, const CheckAttestedField* fields, uint32_t count)
{
    uint32_t f = 0;
    while (f < count && f < names->count && strcmp(names->names[f], fields[f].name) == 0 && fields[f].field_class != 0)
    {
        f += 1;
    }
    const bool same = f == count && f == names->count;
    if (!same)
    {
        fprintf(stderr, "test-checker: attested: %s's field %u is %s, its class list's %s: list each field with its class "
                        "(c_check_attested_test.c, check_attested_input_fields and check_attested_reader_fields)\n",
                label, f, f < names->count ? names->names[f] : "(none)", f < count ? fields[f].name : "(none)");
    }
    return same;
}

static __attribute__((noinline)) void check_test_attested_fields(CheckTest* test)
{
    CheckInput* input = calloc(1, sizeof(CheckInput));
    CheckInputReader* reader = calloc(1, sizeof(CheckInputReader));
    CheckAttestedFieldNames* names = calloc(2, sizeof(CheckAttestedFieldNames));
    const bool ready = input != NULL && reader != NULL && names != NULL;
    if (ready)
    {
        __builtin_dump_struct(input, check_attested_field_name, &names[0]);
        __builtin_dump_struct(reader, check_attested_field_name, &names[1]);
    }
    const bool same = ready && check_attested_fields_same("CheckInput", &names[0], check_attested_input_fields, BUSTER_ARRAY_LENGTH(check_attested_input_fields)) &&
                      check_attested_fields_same("CheckInputReader", &names[1], check_attested_reader_fields, BUSTER_ARRAY_LENGTH(check_attested_reader_fields));
    check_test_expect(test, same, "attested declarations: every field of the reader's state (CheckInput, CheckInputReader) has a class, in its struct's order");
    free(names);
    free(reader);
    free(input);
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
        CheckAttested census = { .passes = 2, .twins = true, .records = records, .record_capacity = 1024 };
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
        CheckAttested census = { .passes = 1, .twins = true, .records = records, .record_capacity = 1024 };
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
        CheckAttested census = { .passes = 1, .twins = true, .records = records, .record_capacity = 1024 };
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
        CheckAttested census = { .passes = 1, .twins = true, .records = records, .record_capacity = 1024 };
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
        CheckAttested census = { .passes = 1, .twins = true, .records = records, .record_capacity = 1024 };
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
        check_test_attested_compile(test, directory);
        check_test_attested_memo(test, directory);
        check_test_attested_fields(test);
        rmdir(directory);
    }
}

#endif
