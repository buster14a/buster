// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-check-units [--executable PATH] [--clang PATH] [--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...]
// [--generate DIR [--units N] [--functions N]]:
// the driver's commands of several source files (#152, check_driver_units), through the executable (this one by
// default), each run in a directory of the test's own.
//
// - Programs: generated programs (--programs, 4 by default, from --seed) of 2 to 7 units. Unit 0's main calls every
//   unit's functions through their tables and prints what they return. A unit's functions call later units' and its own
//   earlier ones, read and write their globals (initialized, zero-filled and thread-local), take their functions'
//   addresses into tables and their globals' into pointers, pass and return structs by value, call printf, and have
//   static functions, statics and static locals of the same names as the other units'; each unit also has a function
//   and a zero-filled global in sections of their own names (#619's). And a program of synth's shape
//   (check_units_test_synth: 12 units of 30 functions, each calling three of any unit's while a depth lasts, and writing
//   another unit's global). Each program compiled:
//   - by clang -O0, whose program's output and exit status are the expected ones (the oracle of what the C means);
//   - each unit alone with -c, linked by cc: a program that must print and exit as clang's does;
//   - all units at once with -c, by --lanes 1, 2, 3 and 8, by default, and by 8 with --poison-arenas (the lanes' arenas
//     overwritten with a pattern between units): each unit's object the same bytes as alone;
//   - all units at once into an executable, by --lanes 1, 2, 3 and 8, by default and by 8 with --poison-arenas, by each
//     path: the same bytes, a program that prints and exits as clang's does, and that defines the same symbols as cc's
//     link of the units' objects (nm: each named symbol the objects define, but the compiler's .L labels, with its
//     binding and its kind: code or data);
//   - all units at once into an executable with -g (#779), by --lanes 1 and 8, by 8 with --poison-arenas and by the
//     scalar path: the same bytes, a program that prints and exits as clang's does;
//   - all units at once with -c -g, by --lanes 8: each unit's object the same bytes as with -c -g alone;
//   - all units at once with -fsyntax-only: exit status 0, and nothing on standard error.
// - Diagnostics: three units, one with a warning, one with an error and one clean, by --lanes 1, 2, 3 and 8, with -c,
//   -fsyntax-only and into an executable: standard error is each unit's own alone, in the command line's order, the
//   exit status 1; -c writes the objects of the units without errors, and the link no executable.
// - Refusals: a name two units define (multiple definition), a name none defines (undefined reference), -c with -o,
//   --dump-typed, --id-order, and two units whose objects have one name: exit status 1, the message, and no output.
//   And the refusal of two units of one object's name in a child whose stack was filled with a mapping's address first
//   (check_units_test_poison): it must exit with status 1 and its message, as with a clean stack, where a local the
//   driver read before setting it would release an arena of that address and unmap the child's code.
// - The merge on lanes (#778) against its twin, which one lane and the scalar path run: four units that define three
//   names twice, by --lanes 1, 2 and 8 and by the scalar path, refused with the same standard error, which names the
//   first duplicate in the units' order (beta, which d3.c defines again, though alpha's first definition comes first)
//   and the first definition's unit; and units of a pointer whose addend doesn't fit 32 bits (a wide addend) and of no
//   symbols, by the same commands into the same bytes, a program in which the pointer holds its addend.
// - A few large units (#777): big.c, of BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS functions, a table of them and their
//   sum, beside small.c's main, which checks the sum against the test's own: with -c by --lanes 8 and --shared-parts
//   (its emission pauses after each part it takes itself), each object its own alone, and the six lanes that take no
//   unit take parts of big.c's functions, by the command's report; linked by --lanes 1, by 8 and by the scalar path on
//   8, the same bytes, a program whose sum is the test's, and on 8 lanes the parts taken so.
// - Reservations: two units that include one header, with -c by --lanes 1, in children whose address space may grow a
//   page less than a run reached (check_units_test_limits, as test-parser fails its driver's): the run's last
//   reservation or mapping fails, then, a page below the peak that run reached, the one before it, down to the first.
//   Each run must exit with status 1 having said why in an error, or, where the reservation has a fallback (the
//   emission's columns, which take the caller's scratch then), write the same objects as without a limit; and either way
//   leave the address space and the descriptors as they were. Among them a lane's setup fails at its reservations
//   (which it says: can't reserve the lane's memory), its file cache's after its intern table and the cache's first
//   arrays were reserved.
// - The jobserver: four units by --lanes 4 under a GNU make jobserver of three tokens (MAKEFLAGS' fifo form), linked into
//   an executable of more than a megabyte whose path is a FIFO, which the test reads: while the writer writes, the
//   jobserver holds none of the tokens, which the command's lanes took; once the command ends, it holds every one
//   again, and the executable the test read runs.
// - Lanes that don't start: in a child limited to the processes it has (RLIMIT_NPROC, which doesn't limit root), so that
//   lane_run starts no thread, the first program's objects and executable as by --lanes 1.
// - With --corpus DIR, every .c file in DIR at once with -c (and each --option), by --lanes 8, and again with
//   --poison-arenas: each file's status and object as with the file alone, and its diagnostics in their place, in the
//   files' order.
// - With --generate DIR, nothing is tested: a program of synth's shape (--units, 512 by default, of --functions, 200) is
//   written in DIR, for measurements.

#define BUSTER_CHECK_UNITS_TEST_SECONDS 120
#define BUSTER_CHECK_UNITS_TEST_MAX_UNITS 16
// The generated programs' units, from 2 to this many; and the synth-shaped program's units and functions.
#define BUSTER_CHECK_UNITS_TEST_PROGRAM_UNITS 7
#define BUSTER_CHECK_UNITS_TEST_SYNTH_UNITS 12
#define BUSTER_CHECK_UNITS_TEST_SYNTH_FUNCTIONS 30
#define BUSTER_CHECK_UNITS_TEST_FUNCTIONS 4
// The large unit's functions in the test of a few large units (#777): enough tokens for 8 function lanes.
#define BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS 800

typedef struct CheckUnitsTestTally CheckUnitsTestTally;
struct CheckUnitsTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void check_units_test_expect(CheckUnitsTestTally* tally, bool condition, const char* what, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 20)
        {
            printf("  failed: %s%s%s\n", what, detail != NULL ? ": " : "", detail != NULL ? detail : "");
        }
    }
}

static uint64_t check_units_test_random(CheckUnitsTestTally* tally)
{
    tally->random ^= tally->random << 13;
    tally->random ^= tally->random >> 7;
    tally->random ^= tally->random << 17;
    return tally->random;
}

// Runs `arguments` (arguments[0] the executable, found on PATH) in `directory`, its standard output into `output` and
// its errors into `errors` (paths, /dev/null for none); with `unstarted`, limited to the processes it has
// (RLIMIT_NPROC), so that it can start no thread. Its exit status, or -1 if it didn't exit; an alarm, which exec keeps,
// ends one that hangs. It writes no core file: a crashing driver's dump, of arenas that reserve tens of gigabytes, took
// half a minute through systemd-coredump.
static int check_units_test_run(const char* directory, char* const* arguments, const char* output, const char* errors, bool unstarted)
{
    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0)
    {
        const struct rlimit limit = {
            .rlim_cur = 0,
            .rlim_max = 0,
        };
        const int out = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0 && err >= 0 && dup2(out, 1) == 1 && dup2(err, 2) == 2 && chdir(directory) == 0 && setrlimit(RLIMIT_CORE, &limit) == 0 &&
            (!unstarted || setrlimit(RLIMIT_NPROC, &limit) == 0))
        {
            alarm(BUSTER_CHECK_UNITS_TEST_SECONDS);
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The bytes of the file at `path`, which the caller frees, and their length; null if it can't be read.
static char* check_units_test_read(const char* path, uint64_t* length)
{
    FILE* file = fopen(path, "rb");
    char* bytes = NULL;
    *length = 0;
    bool reading = file != NULL;
    uint64_t capacity = 0;
    while (reading)
    {
        if (*length == capacity)
        {
            capacity = capacity != 0 ? capacity * 2 : 4096;
            char* grown = realloc(bytes, capacity + 1);
            reading = grown != NULL;
            bytes = grown != NULL ? grown : bytes;
        }
        const size_t got = reading ? fread(bytes + *length, 1, capacity - *length, file) : 0;
        *length += got;
        reading = reading && got != 0;
    }
    const bool failed = file == NULL || ferror(file) || bytes == NULL;
    if (file != NULL)
    {
        fclose(file);
    }
    if (failed)
    {
        free(bytes);
        bytes = NULL;
        *length = 0;
    }
    else
    {
        bytes[*length] = 0;
    }
    return bytes;
}

// Whether the file at `path` exists.
static bool check_units_test_exists(const char* path)
{
    struct stat status;
    return stat(path, &status) == 0;
}

// A generated program of `count` units (see the file's comment): unit u's source into `sources[u]`, written as
// unit_u.c in `directory`. False if a file can't be written.
static bool check_units_test_program(CheckUnitsTestTally* tally, const char* directory, uint32_t count)
{
    bool written = true;
    // Each unit's constants, so that every unit's code differs.
    uint32_t multiplier[BUSTER_CHECK_UNITS_TEST_MAX_UNITS];
    uint32_t addend[BUSTER_CHECK_UNITS_TEST_MAX_UNITS];
    uint32_t initial[BUSTER_CHECK_UNITS_TEST_MAX_UNITS];
    for (uint32_t u = 0; u < count; u += 1)
    {
        multiplier[u] = 3 + (uint32_t)(check_units_test_random(tally) % 29);
        addend[u] = (uint32_t)(check_units_test_random(tally) % 1000);
        initial[u] = 1 + (uint32_t)(check_units_test_random(tally) % 100);
    }
    for (uint32_t u = 0; written && u < count; u += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/unit_%u.c", directory, u);
        FILE* file = fopen(path, "w");
        written = file != NULL;
        if (file != NULL)
        {
            fprintf(file, "#include <stdio.h>\n#include <string.h>\n\nstruct pair { long first; long second; };\n\n");
            // Every unit's names, as a header would declare them.
            for (uint32_t v = 0; v < count; v += 1)
            {
                fprintf(file, "extern unsigned value_%u;\nextern unsigned zero_%u;\nextern _Thread_local unsigned local_%u;\nextern const char* name_%u;\n"
                              "extern unsigned* pointer_%u;\nextern unsigned (*table_%u[%u])(unsigned);\nstruct pair swap_%u(struct pair p);\n"
                              "extern unsigned placed_%u;\nunsigned placed_function_%u(unsigned x);\n",
                        v, v, v, v, v, v, BUSTER_CHECK_UNITS_TEST_FUNCTIONS, v, v, v);
                for (uint32_t f = 0; f < BUSTER_CHECK_UNITS_TEST_FUNCTIONS; f += 1)
                {
                    fprintf(file, "unsigned function_%u_%u(unsigned x);\n", v, f);
                }
            }
            // The unit's own: statics of the same names in every unit, and its globals.
            fprintf(file, "\nstatic unsigned counter;\nstatic unsigned helper(unsigned x)\n{\n    counter += 1;\n    return x * %uu + %uu;\n}\n\n", multiplier[u], addend[u]);
            fprintf(file, "unsigned value_%u = %uu;\nunsigned zero_%u;\n_Thread_local unsigned local_%u = %uu;\nconst char* name_%u = \"unit %u\";\n", u,
                    initial[u], u, u, initial[u] + 1, u, u);
            fprintf(file, "unsigned* pointer_%u = &value_%u;\n\n", u, (u + 1) % count);
            // A zero-filled global in a data section of its own name, which an executable's data holds as zeros, and a
            // function in a text section of its own.
            fprintf(file, "__attribute__((section(\".data.units_%u\"))) unsigned placed_%u;\n\n", u, u);
            fprintf(file, "__attribute__((section(\".text.units_%u\"))) unsigned placed_function_%u(unsigned x)\n{\n    placed_%u += x;\n    return placed_%u * %uu;\n}\n\n",
                    u, u, u, u, multiplier[u]);
            for (uint32_t f = 0; f < BUSTER_CHECK_UNITS_TEST_FUNCTIONS; f += 1)
            {
                fprintf(file, "unsigned function_%u_%u(unsigned x)\n{\n    static unsigned calls;\n    calls += 1;\n    zero_%u += x;\n    local_%u += calls;\n", u, f, u, u);
                fprintf(file, "    unsigned result = helper(x) ^ value_%u ^ *pointer_%u;\n", (uint32_t)(check_units_test_random(tally) % count),
                        (uint32_t)(check_units_test_random(tally) % count));
                // A call of a later unit's function, and of one of its own earlier ones: a call graph without cycles.
                if (u + 1 < count)
                {
                    const uint32_t later = u + 1 + (uint32_t)(check_units_test_random(tally) % (count - u - 1));
                    fprintf(file, "    result += function_%u_%u(x + %uu);\n", later, (uint32_t)(check_units_test_random(tally) % BUSTER_CHECK_UNITS_TEST_FUNCTIONS),
                            f + 1);
                }
                if (f != 0)
                {
                    fprintf(file, "    result = result * 31u + function_%u_%u(x ^ %uu);\n", u, (uint32_t)(check_units_test_random(tally) % f), u + 7);
                }
                fprintf(file, "    return result + (unsigned)strlen(name_%u) + calls;\n}\n\n", (uint32_t)(check_units_test_random(tally) % count));
            }
            fprintf(file, "unsigned (*table_%u[%u])(unsigned) = {", u, BUSTER_CHECK_UNITS_TEST_FUNCTIONS);
            for (uint32_t f = 0; f < BUSTER_CHECK_UNITS_TEST_FUNCTIONS; f += 1)
            {
                fprintf(file, "%s function_%u_%u", f != 0 ? "," : "", (uint32_t)(check_units_test_random(tally) % count),
                        (uint32_t)(check_units_test_random(tally) % BUSTER_CHECK_UNITS_TEST_FUNCTIONS));
            }
            fprintf(file, " };\n\nstruct pair swap_%u(struct pair p)\n{\n    struct pair result = { p.second + (long)value_%u, p.first - %ld };\n    return result;\n}\n",
                    u, u, (long)addend[u]);
            if (u == 0)
            {
                fprintf(file, "\nint main(void)\n{\n    unsigned checksum = 0;\n    struct pair p = { 1, 2 };\n");
                for (uint32_t v = 0; v < count; v += 1)
                {
                    fprintf(file, "    for (unsigned f = 0; f < %u; f += 1)\n    {\n        checksum = checksum * 33u + table_%u[f](f + %uu);\n    }\n",
                            BUSTER_CHECK_UNITS_TEST_FUNCTIONS, v, v);
                    fprintf(file, "    checksum = checksum * 7u + placed_function_%u(%uu);\n", v, v + 3);
                    fprintf(file, "    p = swap_%u(p);\n    printf(\"%%s %%u %%u %%u %%u\\n\", name_%u, value_%u, zero_%u, local_%u, placed_%u);\n", v, v, v, v, v, v);
                }
                fprintf(file, "    printf(\"%%u %%ld %%ld %%u\\n\", checksum, p.first, p.second, counter);\n    return (int)(checksum & 63u);\n}\n");
            }
            written = fclose(file) == 0;
        }
    }
    return written;
}

// The arguments of a driver command in `arguments` (room for 64): the executable, then each of `options` (a null
// terminated list), then the units' sources, unit_0.c to unit_{count - 1}.c; returns their count. `names` holds the
// sources' names.
static uint32_t check_units_test_command(char** arguments, char names[][32], const char* executable, const char* const* options, uint32_t count)
{
    uint32_t used = 0;
    arguments[used++] = (char*)executable;
    for (uint32_t o = 0; options[o] != NULL; o += 1)
    {
        arguments[used++] = (char*)options[o];
    }
    for (uint32_t u = 0; u < count; u += 1)
    {
        snprintf(names[u], 32, "unit_%u.c", u);
        arguments[used++] = names[u];
    }
    arguments[used] = NULL;
    return used;
}

// The named symbols `nm --defined-only` lists of the files at `paths` (relative to `directory`), each a line of its
// name, its binding (G or L) and its kind (C for code, D for anything else), sorted, into `listing`, which the caller
// frees; but the compiler's .L labels, and with `only` (a sorted listing), only the names `only` has. False if nm fails.
static bool check_units_test_symbols(const char* directory, char* const* paths, uint32_t count, const char* only, char** listing)
{
    char output[PATH_MAX];
    snprintf(output, sizeof(output), "%s/symbols.txt", directory);
    char* arguments[BUSTER_CHECK_UNITS_TEST_MAX_UNITS + 8] = { "nm", "--defined-only", "--format=posix" };
    uint32_t used = 3;
    for (uint32_t i = 0; i < count; i += 1)
    {
        arguments[used++] = paths[i];
    }
    arguments[used] = NULL;
    const bool listed = check_units_test_run(directory, arguments, output, "/dev/null", false) == 0;
    uint64_t length = 0;
    char* text = listed ? check_units_test_read(output, &length) : NULL;
    unlink(output);
    // At most a line of each line nm printed, each at most as long.
    char** lines = text != NULL ? calloc(length + 1, sizeof(char*)) : NULL;
    char* result = text != NULL ? malloc(2 * length + 2) : NULL;
    uint64_t line_count = 0;
    for (char* line = lines != NULL && result != NULL ? text : NULL; line != NULL && *line != 0;)
    {
        char* end = strchr(line, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        char name[512];
        char type = 0;
        // A file's heading ("unit_0.o:") has no type.
        if (sscanf(line, "%511s %c", name, &type) == 2 && !(name[0] == '.' && name[1] == 'L'))
        {
            const bool global = type >= 'A' && type <= 'Z';
            const char lower = global ? (char)(type - 'A' + 'a') : type;
            char entry[600];
            snprintf(entry, sizeof(entry), "%s %c %c", name, global ? 'G' : 'L', lower == 't' ? 'C' : 'D');
            char needle[520];
            snprintf(needle, sizeof(needle), "\n%s ", name);
            if (only == NULL || strstr(only, needle) != NULL || strncmp(only, needle + 1, strlen(needle + 1)) == 0)
            {
                lines[line_count] = strdup(entry);
                line_count += lines[line_count] != NULL;
            }
        }
        line = end != NULL ? end + 1 : NULL;
    }
    // Sorted by insertion: a few hundred lines.
    for (uint64_t i = 1; lines != NULL && i < line_count; i += 1)
    {
        char* moving = lines[i];
        uint64_t j = i;
        while (j != 0 && strcmp(lines[j - 1], moving) > 0)
        {
            lines[j] = lines[j - 1];
            j -= 1;
        }
        lines[j] = moving;
    }
    uint64_t used_bytes = 0;
    for (uint64_t i = 0; result != NULL && i < line_count; i += 1)
    {
        const uint64_t bytes = strlen(lines[i]);
        if (used_bytes + bytes + 2 <= 2 * length + 2)
        {
            memcpy(result + used_bytes, lines[i], bytes);
            used_bytes += bytes;
            result[used_bytes++] = '\n';
        }
        free(lines[i]);
    }
    if (result != NULL)
    {
        result[used_bytes] = 0;
    }
    free(lines);
    free(text);
    *listing = result;
    return result != NULL;
}

// A generated program (see the file's comment), its units unit_0.c to unit_{count - 1}.c in `directory`.
static void check_units_test_one_program(CheckUnitsTestTally* tally, const char* executable, const char* clang, const char* directory, uint32_t count,
                                         uint32_t program)
{
    static const char* const lane_counts[] = { "1", "2", "3", "8", NULL };
    char names[BUSTER_CHECK_UNITS_TEST_MAX_UNITS][32];
    char* arguments[64];
    char detail[PATH_MAX + 128];
    char expected_path[PATH_MAX];
    char actual_path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(expected_path, sizeof(expected_path), "%s/expected.txt", directory);
    snprintf(actual_path, sizeof(actual_path), "%s/actual.txt", directory);
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", directory);
    // clang's program: the expected output and exit status.
    const char* clang_options[] = { "-O0", "-w", "-o", "reference_clang", NULL };
    check_units_test_command(arguments, names, clang, clang_options, count);
    const bool clang_built = check_units_test_run(directory, arguments, "/dev/null", errors_path, false) == 0;
    char* reference_arguments[] = { "./reference_clang", NULL };
    const int expected_status = clang_built ? check_units_test_run(directory, reference_arguments, expected_path, "/dev/null", false) : -1;
    uint64_t expected_length = 0;
    char* expected = check_units_test_read(expected_path, &expected_length);
    snprintf(detail, sizeof(detail), "program %u, %u units, in %s", program, count, directory);
    check_units_test_expect(tally, clang_built && expected_status >= 0 && expected != NULL && expected_length != 0, "clang -O0 builds and runs the program", detail);
    // Each unit alone with -c, and cc's link of their objects.
    bool alone = true;
    for (uint32_t u = 0; alone && u < count; u += 1)
    {
        char source[32];
        char object[32];
        snprintf(source, sizeof(source), "unit_%u.c", u);
        snprintf(object, sizeof(object), "alone_%u.o", u);
        char* unit_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
        alone = check_units_test_run(directory, unit_arguments, "/dev/null", errors_path, false) == 0;
    }
    char objects[BUSTER_CHECK_UNITS_TEST_MAX_UNITS][32];
    char* link_arguments[BUSTER_CHECK_UNITS_TEST_MAX_UNITS + 4] = { "cc", "-o", "reference_objects" };
    for (uint32_t u = 0; u < count; u += 1)
    {
        snprintf(objects[u], sizeof(objects[u]), "alone_%u.o", u);
        link_arguments[3 + u] = objects[u];
    }
    link_arguments[3 + count] = NULL;
    const bool cc_linked = alone && check_units_test_run(directory, link_arguments, "/dev/null", errors_path, false) == 0;
    char* objects_arguments[] = { "./reference_objects", NULL };
    const int objects_status = cc_linked ? check_units_test_run(directory, objects_arguments, actual_path, "/dev/null", false) : -1;
    uint64_t actual_length = 0;
    char* actual = cc_linked ? check_units_test_read(actual_path, &actual_length) : NULL;
    check_units_test_expect(tally, cc_linked && objects_status == expected_status && actual != NULL && actual_length == expected_length &&
                                       memcmp(actual, expected, expected_length) == 0,
                            "the units compiled alone, linked by cc, run as clang's program", detail);
    free(actual);
    // The program's symbols: those its units' objects define, by name, binding and kind.
    char* object_paths[BUSTER_CHECK_UNITS_TEST_MAX_UNITS];
    for (uint32_t u = 0; u < count; u += 1)
    {
        object_paths[u] = objects[u];
    }
    char* defined = NULL;
    const bool listed = alone && check_units_test_symbols(directory, object_paths, count, NULL, &defined);
    char* reference_path[] = { "reference_objects" };
    char* reference_symbols = NULL;
    const bool reference_listed = listed && cc_linked && check_units_test_symbols(directory, reference_path, 1, defined, &reference_symbols);
    check_units_test_expect(tally, reference_listed && strcmp(reference_symbols, defined) == 0, "cc's link defines its objects' symbols", detail);
    // All units at once with -c: each unit's object as alone, and with the lanes' arenas poisoned between units.
    for (uint32_t c = 0; c <= BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
    {
        const bool poisoned = c == BUSTER_ARRAY_LENGTH(lane_counts);
        const char* options[] = { "--lanes", poisoned ? "8" : lane_counts[c], "-c", NULL };
        const char* poisoned_options[] = { "--poison-arenas", "--lanes", "8", "-c", NULL };
        check_units_test_command(arguments, names, executable, poisoned ? poisoned_options : lane_counts[c] != NULL ? options : options + 2, count);
        const int status = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
        uint64_t error_bytes = 0;
        char* error_text = check_units_test_read(errors_path, &error_bytes);
        snprintf(detail, sizeof(detail), "program %u, %u units, -c by --lanes %s%s", program, count, poisoned ? "8" : lane_counts[c] != NULL ? lane_counts[c] : "by default",
                 poisoned ? ", the lanes' arenas poisoned" : "");
        check_units_test_expect(tally, status == 0 && error_bytes == 0, "the units at once write their objects, without a diagnostic", detail);
        free(error_text);
        for (uint32_t u = 0; u < count; u += 1)
        {
            char together[PATH_MAX];
            char apart[PATH_MAX];
            snprintf(together, sizeof(together), "%s/unit_%u.o", directory, u);
            snprintf(apart, sizeof(apart), "%s/alone_%u.o", directory, u);
            check_units_test_expect(tally, check_test_driver_same_files(together, apart), "each unit's object at once is its object alone", detail);
            unlink(together);
        }
    }
    // All units at once into an executable, by each path.
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        for (uint32_t c = 0; c <= BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
        {
            const bool poisoned = c == BUSTER_ARRAY_LENGTH(lane_counts);
            char output[64];
            snprintf(output, sizeof(output), "program_%u_%u", scalar, c);
            const char* options[8];
            uint32_t used = 0;
            if (scalar != 0)
            {
                options[used++] = "--scalar";
            }
            if (poisoned)
            {
                options[used++] = "--poison-arenas";
                options[used++] = "--lanes";
                options[used++] = "8";
            }
            else if (lane_counts[c] != NULL)
            {
                options[used++] = "--lanes";
                options[used++] = lane_counts[c];
            }
            options[used++] = "-o";
            options[used++] = output;
            options[used] = NULL;
            check_units_test_command(arguments, names, executable, options, count);
            const int status = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
            snprintf(detail, sizeof(detail), "program %u, %u units, %s path, --lanes %s%s", program, count, scalar != 0 ? "scalar" : "vector",
                     poisoned ? "8" : lane_counts[c] != NULL ? lane_counts[c] : "by default", poisoned ? ", the lanes' arenas poisoned" : "");
            char run[80];
            snprintf(run, sizeof(run), "./%s", output);
            char* run_arguments[] = { run, NULL };
            const int run_status = status == 0 ? check_units_test_run(directory, run_arguments, actual_path, "/dev/null", false) : -1;
            actual = status == 0 ? check_units_test_read(actual_path, &actual_length) : NULL;
            check_units_test_expect(tally, status == 0 && run_status == expected_status && actual != NULL && expected != NULL && actual_length == expected_length &&
                                               memcmp(actual, expected, expected_length) == 0,
                                    "the units' executable runs as clang's program", detail);
            free(actual);
            char path[PATH_MAX];
            char first[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", directory, output);
            snprintf(first, sizeof(first), "%s/program_%u_0", directory, scalar);
            check_units_test_expect(tally, c == 0 || check_test_driver_same_files(path, first), "the units' executable is the same bytes at every lane count", detail);
            if (c == 0)
            {
                char* program_path[] = { output };
                char* symbols = NULL;
                const bool program_listed = listed && check_units_test_symbols(directory, program_path, 1, defined, &symbols);
                check_units_test_expect(tally, program_listed && strcmp(symbols, defined) == 0, "the units' executable defines its units' symbols", detail);
                free(symbols);
            }
        }
        char vector[PATH_MAX];
        char scalar_path[PATH_MAX];
        snprintf(vector, sizeof(vector), "%s/program_0_0", directory);
        snprintf(scalar_path, sizeof(scalar_path), "%s/program_1_0", directory);
        check_units_test_expect(tally, scalar == 0 || check_test_driver_same_files(vector, scalar_path), "both paths link the units into the same bytes", detail);
    }
    // -g (#779): all units at once into an executable with their DWARF, by --lanes 1 and 8, by 8 with the lanes' arenas
    // poisoned and by the scalar path: the same bytes, which run as clang's program does.
    static const char* const debug_options[][8] = {
        { "-g", "--lanes", "1", "-o", "debug_1", NULL },
        { "-g", "--lanes", "8", "-o", "debug_8", NULL },
        { "-g", "--poison-arenas", "--lanes", "8", "-o", "debug_poisoned", NULL },
        { "-g", "--scalar", "--lanes", "3", "-o", "debug_scalar", NULL },
    };
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(debug_options); d += 1)
    {
        check_units_test_command(arguments, names, executable, debug_options[d], count);
        const int status = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
        const char* output = NULL;
        for (uint32_t o = 0; debug_options[d][o] != NULL; o += 1)
        {
            output = strcmp(debug_options[d][o], "-o") == 0 ? debug_options[d][o + 1] : output;
        }
        snprintf(detail, sizeof(detail), "program %u, %u units, -g into %s", program, count, output);
        char run[80];
        snprintf(run, sizeof(run), "./%s", output);
        char* run_arguments[] = { run, NULL };
        const int run_status = status == 0 ? check_units_test_run(directory, run_arguments, actual_path, "/dev/null", false) : -1;
        actual = status == 0 ? check_units_test_read(actual_path, &actual_length) : NULL;
        check_units_test_expect(tally, status == 0 && run_status == expected_status && actual != NULL && expected != NULL && actual_length == expected_length &&
                                           memcmp(actual, expected, expected_length) == 0,
                                "the units' executable with -g runs as clang's program", detail);
        free(actual);
        char path[PATH_MAX];
        char first[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", directory, output);
        snprintf(first, sizeof(first), "%s/debug_1", directory);
        check_units_test_expect(tally, d == 0 || check_test_driver_same_files(path, first), "the units' executable with -g is the same bytes by every lane count and path",
                                detail);
    }
    // -c -g of all units at once by 8 lanes: each unit's object, with its DWARF, the same bytes as with -c -g alone.
    bool debug_alone = true;
    for (uint32_t u = 0; debug_alone && u < count; u += 1)
    {
        char source[32];
        char object[32];
        snprintf(source, sizeof(source), "unit_%u.c", u);
        snprintf(object, sizeof(object), "alone_g_%u.o", u);
        char* unit_arguments[] = { (char*)executable, "-c", "-g", "-o", object, source, NULL };
        debug_alone = check_units_test_run(directory, unit_arguments, "/dev/null", errors_path, false) == 0;
    }
    snprintf(detail, sizeof(detail), "program %u, %u units, -c -g by --lanes 8", program, count);
    check_units_test_expect(tally, debug_alone, "each unit alone writes its object with -c -g", detail);
    const char* debug_objects[] = { "-c", "-g", "--lanes", "8", NULL };
    check_units_test_command(arguments, names, executable, debug_objects, count);
    const bool debug_written = debug_alone && check_units_test_run(directory, arguments, "/dev/null", errors_path, false) == 0;
    check_units_test_expect(tally, debug_written, "the units at once write their objects with -c -g", detail);
    for (uint32_t u = 0; debug_written && u < count; u += 1)
    {
        char together[PATH_MAX];
        char apart[PATH_MAX];
        snprintf(together, sizeof(together), "%s/unit_%u.o", directory, u);
        snprintf(apart, sizeof(apart), "%s/alone_g_%u.o", directory, u);
        check_units_test_expect(tally, check_test_driver_same_files(together, apart), "each unit's object with -c -g at once is its object alone", detail);
        unlink(together);
    }
    // -fsyntax-only.
    const char* syntax[] = { "-fsyntax-only", NULL };
    check_units_test_command(arguments, names, executable, syntax, count);
    const int checked = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
    uint64_t syntax_bytes = 0;
    char* syntax_errors = check_units_test_read(errors_path, &syntax_bytes);
    check_units_test_expect(tally, checked == 0 && syntax_errors != NULL && syntax_bytes == 0, "-fsyntax-only checks every unit, without a diagnostic", detail);
    free(syntax_errors);
    free(reference_symbols);
    free(defined);
    free(expected);
}

// A program of synth's shape (docs/object-writer-and-linker.md's "synth": 512 units of 200 functions) of `count` units
// of `functions` functions each, written as unit_0.c to unit_{count - 1}.c in `directory`: every function reads its
// unit's table of values and its string literals, writes a global of another unit's, and while its depth lasts, calls
// three functions of any units; every unit has a table of its functions' addresses, which unit 0's main calls each of
// with a depth of 2, printing the sum of what they return. False if a file can't be written.
static bool check_units_test_synth(CheckUnitsTestTally* tally, const char* directory, uint32_t count, uint32_t functions)
{
    bool written = true;
    for (uint32_t u = 0; written && u < count; u += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/unit_%u.c", directory, u);
        FILE* file = fopen(path, "w");
        written = file != NULL;
        // The units and functions each function calls, and the unit whose global it writes: declared first.
        uint32_t* callees = written ? malloc((uint64_t)functions * 4 * sizeof(uint32_t)) : NULL;
        written = written && callees != NULL;
        for (uint32_t f = 0; written && f < functions; f += 1)
        {
            for (uint32_t c = 0; c < 3; c += 1)
            {
                callees[4 * f + c] = (uint32_t)(check_units_test_random(tally) % ((uint64_t)count * functions));
            }
            callees[4 * f + 3] = (uint32_t)(check_units_test_random(tally) % count);
        }
        if (written)
        {
            fprintf(file, "#include <stdio.h>\n\n");
            for (uint32_t f = 0; f < functions; f += 1)
            {
                for (uint32_t c = 0; c < 3; c += 1)
                {
                    fprintf(file, "unsigned function_%u_%u(unsigned depth, unsigned x);\n", callees[4 * f + c] / functions, callees[4 * f + c] % functions);
                }
                fprintf(file, "extern unsigned counter_%u;\n", callees[4 * f + 3]);
            }
            fprintf(file, "\nunsigned counter_%u;\nunsigned values_%u[8] = { %uu, %uu, %uu, %uu, %uu, %uu, %uu, %uu };\n", u, u, u, u + 1, u + 2, u + 3, u * 7,
                    u * 11, u * 13, u * 17);
            fprintf(file, "static const char* const strings_%u[4] = { \"synth %u\", \"unit %u of %u\", \"functions %u\", \"table %u\" };\n\n", u, u, u, count,
                    functions, u);
            for (uint32_t f = 0; f < functions; f += 1)
            {
                const uint32_t* calls = callees + 4 * f;
                fprintf(file, "unsigned function_%u_%u(unsigned depth, unsigned x)\n{\n", u, f);
                fprintf(file, "    unsigned result = x + values_%u[%u] + (unsigned char)strings_%u[%u][%u];\n", u, f & 7, u, f & 3, f % 6);
                fprintf(file, "    counter_%u += 1;\n    if (depth != 0)\n    {\n", calls[3]);
                fprintf(file, "        result += function_%u_%u(depth - 1, result ^ %uu);\n", calls[0] / functions, calls[0] % functions, f);
                fprintf(file, "        result += function_%u_%u(depth - 1, result + %uu);\n", calls[1] / functions, calls[1] % functions, u);
                fprintf(file, "        result -= function_%u_%u(depth - 1, result);\n    }\n    return result * 33u + %uu;\n}\n\n", calls[2] / functions,
                        calls[2] % functions, f * 3 + 1);
            }
            fprintf(file, "unsigned (*const table_%u[%u])(unsigned, unsigned) = {", u, functions);
            for (uint32_t f = 0; f < functions; f += 1)
            {
                fprintf(file, "%s function_%u_%u", f != 0 ? "," : "", u, f);
            }
            fprintf(file, " };\n");
            if (u == 0)
            {
                fprintf(file, "\n");
                for (uint32_t v = 0; v < count; v += 1)
                {
                    fprintf(file, "extern unsigned (*const table_%u[%u])(unsigned, unsigned);\n", v, functions);
                }
                fprintf(file, "\nint main(void)\n{\n    unsigned sum = 0;\n");
                for (uint32_t v = 0; v < count; v += 1)
                {
                    fprintf(file, "    sum = sum * 31u + table_%u[%u](2, %uu);\n", v, v % functions, v);
                }
                fprintf(file, "    printf(\"%%u\\n\", sum);\n    return 0;\n}\n");
            }
        }
        free(callees);
        if (file != NULL)
        {
            written = fclose(file) == 0 && written;
        }
    }
    return written;
}

// The programs (see the file's comment), each in a directory of its own under `directory`.
static void check_units_test_programs(CheckUnitsTestTally* tally, const char* executable, const char* clang, const char* directory, uint32_t programs)
{
    for (uint32_t p = 0; p < programs; p += 1)
    {
        char program_directory[PATH_MAX];
        snprintf(program_directory, sizeof(program_directory), "%s/program_%u", directory, p);
        const uint32_t count = 2 + (uint32_t)(check_units_test_random(tally) % (BUSTER_CHECK_UNITS_TEST_PROGRAM_UNITS - 1));
        const bool made = mkdir(program_directory, 0755) == 0 && check_units_test_program(tally, program_directory, count);
        check_units_test_expect(tally, made, "write a generated program", program_directory);
        if (made)
        {
            check_units_test_one_program(tally, executable, clang, program_directory, count, p);
        }
    }
    char synth_directory[PATH_MAX];
    snprintf(synth_directory, sizeof(synth_directory), "%s/synth", directory);
    const bool synth = mkdir(synth_directory, 0755) == 0 &&
                       check_units_test_synth(tally, synth_directory, BUSTER_CHECK_UNITS_TEST_SYNTH_UNITS, BUSTER_CHECK_UNITS_TEST_SYNTH_FUNCTIONS);
    check_units_test_expect(tally, synth, "write a program of synth's shape", synth_directory);
    if (synth)
    {
        check_units_test_one_program(tally, executable, clang, synth_directory, BUSTER_CHECK_UNITS_TEST_SYNTH_UNITS, programs);
    }
    printf("  programs: %u generated programs of several units, and one of synth's shape of %u units of %u functions\n", programs,
           BUSTER_CHECK_UNITS_TEST_SYNTH_UNITS, BUSTER_CHECK_UNITS_TEST_SYNTH_FUNCTIONS);
}

// The diagnostics of three units (see the file's comment), in `directory`.
static void check_units_test_diagnostics(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    static const char* const lane_counts[] = { "1", "2", "3", "8" };
    static const char warned[] = "int warned(void)\n{\n    return 2147483647 + 1;\n}\n";
    static const char failed[] = "int failed(void)\n{\n    return missing;\n}\n";
    static const char clean[] = "int clean(void)\n{\n    return 3;\n}\n";
    static const char* const names[] = { "warned.c", "failed.c", "clean.c" };
    static const char* const objects[] = { "warned.o", "failed.o", "clean.o" };
    char path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", directory);
    bool written = true;
    const char* const texts[] = { warned, failed, clean };
    for (uint32_t u = 0; u < 3; u += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, names[u]);
        written = written && parse_test_write(path, texts[u], strlen(texts[u]));
    }
    check_units_test_expect(tally, written, "write the diagnostics' units", directory);
    // Each unit's diagnostics alone, -fsyntax-only's and -c's alike.
    char* alone = NULL;
    uint64_t alone_length = 0;
    bool alone_ready = written;
    for (uint32_t u = 0; alone_ready && u < 3; u += 1)
    {
        char* arguments[] = { (char*)executable, "-fsyntax-only", (char*)names[u], NULL };
        const int status = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
        uint64_t length = 0;
        char* text = check_units_test_read(errors_path, &length);
        alone_ready = text != NULL && status == (u == 1 ? 1 : 0) && (u == 2 ? length == 0 : length != 0);
        char* grown = alone_ready ? realloc(alone, alone_length + length + 1) : NULL;
        alone_ready = grown != NULL;
        if (grown != NULL)
        {
            alone = grown;
            memcpy(alone + alone_length, text, length);
            alone_length += length;
            alone[alone_length] = 0;
        }
        free(text);
    }
    check_units_test_expect(tally, alone_ready, "each unit alone: the warning, the error, and nothing", directory);
    for (uint32_t c = 0; alone_ready && c < BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
    {
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            for (uint32_t u = 0; u < 3; u += 1)
            {
                snprintf(path, sizeof(path), "%s/%s", directory, objects[u]);
                unlink(path);
            }
            snprintf(path, sizeof(path), "%s/linked", directory);
            unlink(path);
            const char* option = mode == 0 ? "-fsyntax-only" : mode == 1 ? "-c" : "-o";
            char* arguments[] = { (char*)executable, "--lanes", (char*)lane_counts[c], (char*)option, mode == 2 ? "linked" : (char*)names[0],
                                  mode == 2 ? (char*)names[0] : (char*)names[1], mode == 2 ? (char*)names[1] : (char*)names[2], mode == 2 ? (char*)names[2] : NULL, NULL };
            const int status = check_units_test_run(directory, arguments, "/dev/null", errors_path, false);
            uint64_t length = 0;
            char* text = check_units_test_read(errors_path, &length);
            char detail[256];
            snprintf(detail, sizeof(detail), "%s by --lanes %s", option, lane_counts[c]);
            check_units_test_expect(tally, status == 1 && text != NULL && length == alone_length && memcmp(text, alone, alone_length) == 0,
                                    "the units' diagnostics are each one's alone, in the command line's order", detail);
            free(text);
            char object_path[PATH_MAX];
            snprintf(object_path, sizeof(object_path), "%s/warned.o", directory);
            const bool warned_object = check_units_test_exists(object_path);
            snprintf(object_path, sizeof(object_path), "%s/failed.o", directory);
            const bool failed_object = check_units_test_exists(object_path);
            snprintf(object_path, sizeof(object_path), "%s/clean.o", directory);
            const bool clean_object = check_units_test_exists(object_path);
            check_units_test_expect(tally, warned_object == (mode == 1) && !failed_object && clean_object == (mode == 1),
                                    "-c writes the objects of the units without errors alone", detail);
            check_units_test_expect(tally, !check_units_test_exists(path), "a unit's error leaves no executable", detail);
        }
    }
    free(alone);
    printf("  diagnostics: each unit's alone, in the command line's order, by %u lane counts\n", (uint32_t)BUSTER_ARRAY_LENGTH(lane_counts));
}

// The words of stack check_units_test_poison fills: a quarter of a megabyte below its caller's frame.
#define BUSTER_CHECK_UNITS_TEST_POISON_WORDS 32768

// Fills the stack below its caller's frame with `pattern`, where the frames of what the caller calls next start: their
// locals that nothing sets read it.
static __attribute__((noinline)) void check_units_test_poison(uint64_t pattern)
{
    volatile uint64_t words[BUSTER_CHECK_UNITS_TEST_POISON_WORDS];
    for (uint32_t w = 0; w < BUSTER_CHECK_UNITS_TEST_POISON_WORDS; w += 1)
    {
        words[w] = pattern;
    }
}

// The driver's command `arguments` (run_check_command's), in this process, over a stack check_units_test_poison filled
// with `pattern`.
static __attribute__((noinline)) int check_units_test_poisoned_command(uint64_t pattern, int count, char** arguments)
{
    check_units_test_poison(pattern);
    return run_check_command(count, arguments, true);
}

// The refusal of two units of one object's name over a poisoned stack (see the file's comment), in `directory`, the
// refusals'. The pattern is a mapping's address: an arena read uninitialized would have it as its base, its capacity and
// its mapping's offset, and its release would unmap everything below it, the child's code among it.
static void check_units_test_poisoned_refusal(CheckUnitsTestTally* tally, const char* directory)
{
    char errors_path[PATH_MAX];
    char object[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/poisoned.txt", directory);
    snprintf(object, sizeof(object), "%s/other.o", directory);
    unlink(object);
    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0)
    {
        void* mapping = mmap(NULL, 1u << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const struct rlimit limit = { 0 };
        const int err = open(errors_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (mapping != MAP_FAILED && err >= 0 && dup2(err, 2) == 2 && chdir(directory) == 0 && setrlimit(RLIMIT_CORE, &limit) == 0)
        {
            alarm(BUSTER_CHECK_UNITS_TEST_SECONDS);
            char* arguments[] = { "-c", "other.c", "nested/other.c", NULL };
            _exit(check_units_test_poisoned_command((uint64_t)(uintptr_t)mapping, 3, arguments));
        }
        _exit(127);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    uint64_t length = 0;
    char* text = check_units_test_read(errors_path, &length);
    check_units_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 1 && text != NULL && strstr(text, "would both write other.o") != NULL,
                            "the refusal over a stack poisoned with a mapping's address exits with its message", errors_path);
    check_units_test_expect(tally, !check_units_test_exists(object), "the refusal over a poisoned stack writes no object", object);
    free(text);
}

// The refusals (see the file's comment), in `directory`.
static void check_units_test_refusals(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    static const char first[] = "int shared = 1;\nint missing_function(void);\nint main(void)\n{\n    return shared;\n}\n";
    static const char second[] = "int shared = 2;\n";
    static const char other[] = "int other(void)\n{\n    return 4;\n}\n";
    static const char caller[] = "int missing_function(void);\nint main(void)\n{\n    return missing_function();\n}\n";
    char path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", directory);
    char nested[PATH_MAX];
    snprintf(nested, sizeof(nested), "%s/nested", directory);
    bool written = mkdir(nested, 0755) == 0 || errno == EEXIST;
    const struct
    {
        const char* name;
        const char* text;
    } files[] = { { "first.c", first }, { "second.c", second }, { "other.c", other }, { "caller.c", caller }, { "nested/other.c", other } };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[f].name);
        written = written && parse_test_write(path, files[f].text, strlen(files[f].text));
    }
    check_units_test_expect(tally, written, "write the refusals' units", directory);
    const struct
    {
        const char* name;
        const char* message;
        const char* output;
        char* arguments[8];
    } refusals[] = {
        { "a name two units define", "multiple definition of 'shared'", "refused", { (char*)executable, "-o", "refused", "first.c", "second.c", NULL } },
        { "a name no unit defines", "undefined reference to 'missing_function'", "refused", { (char*)executable, "-o", "refused", "caller.c", "other.c", NULL } },
        { "-c with -o", "cannot specify -o when generating multiple output files", "refused.o", { (char*)executable, "-c", "-o", "refused.o", "other.c", "second.c", NULL } },
        { "a dump", "take one source file", NULL, { (char*)executable, "--dump-typed", "other.c", "second.c", NULL } },
        { "--id-order", "take one source file", NULL, { (char*)executable, "--id-order", "153", "-c", "other.c", "second.c", NULL } },
        { "two units of one object's name", "would both write other.o", "other.o", { (char*)executable, "-c", "other.c", "nested/other.c", NULL } },
    };
    for (uint32_t r = 0; written && r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        if (refusals[r].output != NULL)
        {
            snprintf(path, sizeof(path), "%s/%s", directory, refusals[r].output);
            unlink(path);
        }
        const int status = check_units_test_run(directory, refusals[r].arguments, "/dev/null", errors_path, false);
        uint64_t length = 0;
        char* text = check_units_test_read(errors_path, &length);
        check_units_test_expect(tally, status == 1 && text != NULL && strstr(text, refusals[r].message) != NULL, "refused, with its message", refusals[r].name);
        check_units_test_expect(tally, refusals[r].output == NULL || !check_units_test_exists(path), "refused, without an output", refusals[r].name);
        free(text);
    }
    check_units_test_poisoned_refusal(tally, directory);
    printf("  refusals: %u commands refused, and one over a poisoned stack\n", (uint32_t)BUSTER_ARRAY_LENGTH(refusals));
}

// The merge on lanes against its twin (see the file's comment), in a directory of its own under `directory`.
static void check_units_test_merge(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    static const char d1[] = "int alpha = 1;\nint beta = 1;\nint gamma_function(void);\nint main(void)\n{\n    return alpha + beta + gamma_function();\n}\n";
    static const char d2[] = "int delta = 2;\nint gamma_function(void)\n{\n    return 3;\n}\n";
    static const char d3[] = "int beta = 3;\nint delta = 3;\n";
    static const char d4[] = "int alpha = 4;\nint gamma_function(void)\n{\n    return 4;\n}\n";
    static const char w1[] = "extern char far_array[];\n"
                             "extern char* const far_pointer;\n"
                             "int main(void)\n"
                             "{\n"
                             "    return (unsigned long long)far_pointer - (unsigned long long)far_array == 0x123456789ull ? 0 : 1;\n"
                             "}\n";
    static const char w2[] = "char far_array[16];\n";
    static const char w3[] = "extern char far_array[];\nchar* const far_pointer = far_array + 0x123456789ll;\n";
    static const char w4[] = "typedef int unused_type;\n";
    static const char* const names[] = { "d1.c", "d2.c", "d3.c", "d4.c", "w1.c", "w2.c", "w3.c", "w4.c" };
    const char* const texts[] = { d1, d2, d3, d4, w1, w2, w3, w4 };
    char merge_directory[PATH_MAX];
    char path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(merge_directory, sizeof(merge_directory), "%s/merge", directory);
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", merge_directory);
    bool written = mkdir(merge_directory, 0755) == 0;
    for (uint32_t f = 0; written && f < BUSTER_ARRAY_LENGTH(names); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", merge_directory, names[f]);
        written = parse_test_write(path, texts[f], strlen(texts[f]));
    }
    check_units_test_expect(tally, written, "write the merge's units", merge_directory);
    // Each command's options before its units: one lane and the scalar path run the twin, the others the merge on lanes.
    static const char* const options[][5] = {
        { "--lanes", "1", NULL },
        { "--lanes", "2", NULL },
        { "--lanes", "8", NULL },
        { "--scalar", "--lanes", "8", NULL },
    };
    static const char duplicate[] = "error: multiple definition of 'beta': d3.c defines it, and d1.c first\n";
    char* first_errors = NULL;
    uint64_t first_length = 0;
    for (uint32_t o = 0; written && o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        char* arguments[16];
        uint32_t used = 0;
        arguments[used++] = (char*)executable;
        for (uint32_t i = 0; options[o][i] != NULL; i += 1)
        {
            arguments[used++] = (char*)options[o][i];
        }
        char detail[160];
        snprintf(detail, sizeof(detail), "%s %s%s", options[o][0], options[o][1], options[o][2] != NULL ? " 8" : "");
        // The duplicates: refused with the same standard error, the first duplicate in the units' order.
        const uint32_t base = used;
        arguments[used++] = "-o";
        arguments[used++] = "duplicates";
        for (uint32_t f = 0; f < 4; f += 1)
        {
            arguments[used++] = (char*)names[f];
        }
        arguments[used] = NULL;
        snprintf(path, sizeof(path), "%s/duplicates", merge_directory);
        const int status = check_units_test_run(merge_directory, arguments, "/dev/null", errors_path, false);
        uint64_t length = 0;
        char* text = check_units_test_read(errors_path, &length);
        check_units_test_expect(tally, status == 1 && text != NULL && strstr(text, duplicate) != NULL && !check_units_test_exists(path),
                                "names several units define: refused, naming the first duplicate in the units' order, without an output", detail);
        check_units_test_expect(tally, o == 0 || (text != NULL && first_errors != NULL && length == first_length && memcmp(text, first_errors, length) == 0),
                                "names several units define: the same standard error by every lane count and path", detail);
        if (o == 0)
        {
            first_errors = text;
            first_length = length;
        }
        else
        {
            free(text);
        }
        // The wide addend and the unit without symbols: the same bytes, which run.
        used = base;
        char output[32];
        snprintf(output, sizeof(output), "wide_%u", o);
        arguments[used++] = "-o";
        arguments[used++] = output;
        for (uint32_t f = 4; f < BUSTER_ARRAY_LENGTH(names); f += 1)
        {
            arguments[used++] = (char*)names[f];
        }
        arguments[used] = NULL;
        const int linked = check_units_test_run(merge_directory, arguments, "/dev/null", errors_path, false);
        char run[48];
        snprintf(run, sizeof(run), "./%s", output);
        char* run_arguments[] = { run, NULL };
        const int ran = linked == 0 ? check_units_test_run(merge_directory, run_arguments, "/dev/null", "/dev/null", false) : -1;
        check_units_test_expect(tally, linked == 0 && ran == 0, "a wide addend and a unit without symbols: linked, and the pointer holds its addend", detail);
        char first[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", merge_directory, output);
        snprintf(first, sizeof(first), "%s/wide_0", merge_directory);
        check_units_test_expect(tally, o == 0 || check_test_driver_same_files(path, first),
                                "a wide addend and a unit without symbols: the same bytes by every lane count and path", detail);
    }
    free(first_errors);
    printf("  merge: duplicates and a wide addend by %u commands, the twin's\n", (uint32_t)BUSTER_ARRAY_LENGTH(options));
}

// A few large units (see the file's comment), in a directory of its own under `directory`: the shared parts' count from
// the command's report, or 0 without one.
static uint64_t check_units_test_shared_count(const char* errors)
{
    static const char prefix[] = "shared parts: ";
    const char* line = errors != NULL ? strstr(errors, prefix) : NULL;
    return line != NULL ? strtoull(line + strlen(prefix), NULL, 10) : 0;
}

static void check_units_test_shared(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    char shared_directory[PATH_MAX];
    char path[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(shared_directory, sizeof(shared_directory), "%s/shared", directory);
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", shared_directory);
    bool written = mkdir(shared_directory, 0755) == 0;
    // big.c: BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS functions, a table of them and their sum; small.c: main, which
    // checks the sum against the test's own.
    FILE* big = NULL;
    if (written)
    {
        snprintf(path, sizeof(path), "%s/big.c", shared_directory);
        big = fopen(path, "w");
        written = big != NULL;
    }
    int64_t expected = 0;
    for (uint32_t f = 0; written && f < BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS; f += 1)
    {
        fprintf(big, "int big_%u(int x)\n{\n    int s = x + %u;\n    for (int i = 0; i < 4; i += 1)\n    {\n        s = (s * 3 + i) & 0xffff;\n    }\n    return s;\n}\n", f, f);
        int64_t s = 3 + (int64_t)f;
        for (int64_t i = 0; i < 4; i += 1)
        {
            s = (s * 3 + i) & 0xffff;
        }
        expected += s;
    }
    if (written)
    {
        fprintf(big, "static int (*const big_table[%u])(int) = {\n", BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS);
        for (uint32_t f = 0; f < BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS; f += 1)
        {
            fprintf(big, "    big_%u,\n", f);
        }
        fprintf(big, "};\nint big_sum(int x)\n{\n    int total = 0;\n    for (int i = 0; i < %u; i += 1)\n    {\n        total += big_table[i](x);\n    }\n    return total;\n}\n",
                BUSTER_CHECK_UNITS_TEST_SHARED_FUNCTIONS);
    }
    written = big != NULL && fclose(big) == 0 && written;
    char small[256];
    snprintf(small, sizeof(small), "int big_sum(int x);\nint main(void)\n{\n    return big_sum(3) == %lld ? 0 : 1;\n}\n", (long long)expected);
    snprintf(path, sizeof(path), "%s/small.c", shared_directory);
    written = written && parse_test_write(path, small, strlen(small));
    check_units_test_expect(tally, written, "write the few large units", shared_directory);
    // Each unit's object alone, by its default lanes.
    char* alone_big[] = { (char*)executable, "-c", "-o", "alone_big.o", "big.c", NULL };
    char* alone_small[] = { (char*)executable, "-c", "-o", "alone_small.o", "small.c", NULL };
    const bool alone = written && check_units_test_run(shared_directory, alone_big, "/dev/null", errors_path, false) == 0 &&
                       check_units_test_run(shared_directory, alone_small, "/dev/null", errors_path, false) == 0;
    check_units_test_expect(tally, alone, "each of the few large units writes its object alone", shared_directory);
    // Both at once with -c by 8 lanes, six of which take no unit: their objects their own alone, and the large one's
    // functions' parts taken by those lanes too.
    char* together[] = { (char*)executable, "-c", "--lanes", "8", "--shared-parts", "big.c", "small.c", NULL };
    const int status = alone ? check_units_test_run(shared_directory, together, "/dev/null", errors_path, false) : -1;
    uint64_t length = 0;
    char* text = check_units_test_read(errors_path, &length);
    const uint64_t taken = check_units_test_shared_count(text);
    free(text);
    check_units_test_expect(tally, status == 0 && taken != 0, "-c of the few large units by 8 lanes: the lanes without units take the large one's parts",
                            shared_directory);
    const char* const objects[][2] = { { "big.o", "alone_big.o" }, { "small.o", "alone_small.o" } };
    for (uint32_t o = 0; status == 0 && o < BUSTER_ARRAY_LENGTH(objects); o += 1)
    {
        char first[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", shared_directory, objects[o][0]);
        snprintf(first, sizeof(first), "%s/%s", shared_directory, objects[o][1]);
        check_units_test_expect(tally, check_test_driver_same_files(path, first), "-c of the few large units by 8 lanes: each object its own alone", objects[o][0]);
    }
    // Linked by one lane (no board), by 8 and by the scalar path on 8: the same bytes, which run.
    static const char* const options[][5] = {
        { "--lanes", "1", NULL },
        { "--lanes", "8", "--shared-parts", NULL },
        { "--scalar", "--lanes", "8", "--shared-parts", NULL },
    };
    for (uint32_t o = 0; alone && o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        char output[32];
        snprintf(output, sizeof(output), "shared_%u", o);
        char* arguments[16];
        uint32_t used = 0;
        arguments[used++] = (char*)executable;
        for (uint32_t i = 0; options[o][i] != NULL; i += 1)
        {
            arguments[used++] = (char*)options[o][i];
        }
        arguments[used++] = "-o";
        arguments[used++] = output;
        arguments[used++] = "big.c";
        arguments[used++] = "small.c";
        arguments[used] = NULL;
        const int linked = check_units_test_run(shared_directory, arguments, "/dev/null", errors_path, false);
        text = check_units_test_read(errors_path, &length);
        const uint64_t link_taken = check_units_test_shared_count(text);
        free(text);
        char run[48];
        snprintf(run, sizeof(run), "./%s", output);
        char* run_arguments[] = { run, NULL };
        const int ran = linked == 0 ? check_units_test_run(shared_directory, run_arguments, "/dev/null", "/dev/null", false) : -1;
        char detail[64];
        snprintf(detail, sizeof(detail), "linked into %s", output);
        check_units_test_expect(tally, linked == 0 && ran == 0 && (o == 0 || link_taken != 0),
                                "the few large units linked: the program's sum, and on 8 lanes the lanes without units take parts", detail);
        char first[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", shared_directory, output);
        snprintf(first, sizeof(first), "%s/shared_0", shared_directory);
        check_units_test_expect(tally, o == 0 || check_test_driver_same_files(path, first), "the few large units linked: the same bytes by every lane count and path",
                                detail);
    }
    printf("  shared parts: %llu of the large unit's taken by lanes without units, its objects and executables one lane's\n", (unsigned long long)taken);
}

// The most runs check_units_test_limits makes, past which its sweep has gone wrong.
#define BUSTER_CHECK_UNITS_TEST_LIMIT_RUNS 10000

// A run of the driver's `arguments` (run_check_command's) in a child whose address space may grow `limit` bytes past
// `base` (this process's), in `directory` and with its standard error into `capture`: its exit status (-1 if it didn't
// report), how far past `base` its address space grew, and whether it left the address space and the descriptors as
// they were. The child writes them to `shared`, a page both see. With `warm`, run in the parent before its baseline is
// frozen, through this same command/observation frame; the caller restores the parent's limits, directory and stderr.
// Keep the frame out of line so a direct warmup cannot use a shallower call path than the measured child.
typedef struct CheckUnitsTestProbe CheckUnitsTestProbe;
struct CheckUnitsTestProbe
{
    int status;
    uint64_t peak;
    uint64_t before_size;
    uint64_t after_command_size;
    uint64_t after_size;
    uint64_t before_stack;
    uint64_t after_stack;
    uint64_t before_data;
    uint64_t after_data;
    int before_descriptor;
    int after_descriptor;
    bool clean;
};

static __attribute__((noinline)) CheckUnitsTestProbe check_units_test_probe(char** arguments, int count, const char* directory, int capture, uint64_t base, uint64_t limit,
                                                                         bool warm, CheckUnitsTestProbe* shared)
{
    *shared = (CheckUnitsTestProbe){ .status = -1 };
    fflush(stdout);
    fflush(stderr);
    const pid_t child = warm ? 0 : fork();
    if (child == 0)
    {
        struct rlimit core = { 0 };
        struct rlimit space;
        bool limited = getrlimit(RLIMIT_AS, &space) == 0 && base + limit <= space.rlim_max;
        // A child can drop its hard core limit; the parent must keep it so the saved limit can be restored.
        if (warm)
        {
            limited = limited && getrlimit(RLIMIT_CORE, &core) == 0;
            core.rlim_cur = 0;
        }
        space.rlim_cur = base + limit;
        limited = limited && chdir(directory) == 0 && dup2(capture, 2) == 2 && setrlimit(RLIMIT_CORE, &core) == 0 && setrlimit(RLIMIT_AS, &space) == 0;
        if (!warm)
        {
            alarm(BUSTER_CHECK_UNITS_TEST_SECONDS);
        }
        const int before = parse_test_lowest_descriptor();
        // These observations can themselves change stack use; keep the whole-process cleanup equality unchanged.
        shared->before_stack = parse_test_status_bytes("VmStk:");
        shared->before_data = parse_test_status_bytes("VmData:");
        shared->before_size = parse_test_status_bytes("VmSize:");
        const int status = limited ? run_check_command(count, arguments, true) : -1;
        shared->after_command_size = parse_test_status_bytes("VmSize:");
        const int after = parse_test_lowest_descriptor();
        shared->peak = parse_test_status_bytes("VmPeak:") - base;
        shared->after_size = parse_test_status_bytes("VmSize:");
        shared->after_stack = parse_test_status_bytes("VmStk:");
        shared->after_data = parse_test_status_bytes("VmData:");
        shared->before_descriptor = before;
        shared->after_descriptor = after;
        shared->clean = shared->after_size == base && after == before;
        shared->status = status;
        if (!warm)
        {
            _exit(0);
        }
    }
    int child_status = 0;
    pid_t waited = child;
    while (child > 0 && (waited = waitpid(child, &child_status, 0)) < 0 && errno == EINTR)
    {
    }
    CheckUnitsTestProbe result = *shared;
    result.status = warm || (child > 0 && waited == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) ? result.status : -1;
    return result;
}

// Every reservation and mapping of two units' -c by one lane failed in turn (see the file's comment), in a directory of
// its own under `directory`. The command and observations run once through the probe in this process first, so that
// what only a process's first run makes (the encoder's tables, the stack's and the heap's growth) is the children's
// already. The parent baseline is frozen only after that same-frame warmup and restoration of the parent's state.
static void check_units_test_limits(CheckUnitsTestTally* tally, const char* directory)
{
    static const char header[] = "int limits_square(int value);\n";
    static const char caller[] = "#include \"square.h\"\nint main(void)\n{\n    return limits_square(3) == 9 ? 0 : 1;\n}\n";
    static const char callee[] = "#include \"square.h\"\nint limits_square(int value)\n{\n    return value * value;\n}\n";
    char limits[PATH_MAX];
    char path[PATH_MAX];
    snprintf(limits, sizeof(limits), "%s/limits", directory);
    bool written = mkdir(limits, 0755) == 0 || errno == EEXIST;
    const struct
    {
        const char* name;
        const char* text;
    } files[] = { { "square.h", header }, { "caller.c", caller }, { "callee.c", callee } };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", limits, files[f].name);
        written = written && parse_test_write(path, files[f].text, strlen(files[f].text));
    }
    snprintf(path, sizeof(path), "%s/errors.txt", limits);
    const int capture = written ? open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
    CheckUnitsTestProbe* shared = mmap(NULL, sizeof(CheckUnitsTestProbe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check_units_test_expect(tally, capture >= 0 && shared != MAP_FAILED, "write the reservations' units", limits);
    // The jobserver's tokens, which MAKEFLAGS would give the run, aren't what this checks.
    const char* makeflags = getenv("MAKEFLAGS");
    char* saved_makeflags = makeflags != NULL ? strdup(makeflags) : NULL;
    // Leave the environment untouched if its value could not be saved; do not run an unisolated warmup.
    const bool isolated_makeflags = (makeflags == NULL || saved_makeflags != NULL) && unsetenv("MAKEFLAGS") == 0;
    char* arguments[] = { "--lanes", "1", "-c", "caller.c", "callee.c", NULL };
    const int count = (int)BUSTER_ARRAY_LENGTH(arguments) - 1;
    char text[1024];
    text[0] = 0;
    int status = -1;
    const int saved_directory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int saved_errors = dup(2);
    struct rlimit saved_space = { 0 };
    struct rlimit saved_core = { 0 };
    const bool saved_limits = getrlimit(RLIMIT_AS, &saved_space) == 0 && getrlimit(RLIMIT_CORE, &saved_core) == 0;
    if (capture >= 0 && shared != MAP_FAILED && saved_directory >= 0 && saved_errors >= 0 && saved_limits && isolated_makeflags)
    {
        const uint64_t warm_base = parse_test_status_bytes("VmSize:");
        const CheckUnitsTestProbe warmed = warm_base != 0 ? check_units_test_probe(arguments, count, limits, capture, warm_base, 1ull << 46, true, shared) :
                                                         (CheckUnitsTestProbe){ .status = -1 };
        // Attempt every restoration even if an earlier one failed. The warmup must not calibrate over a leaked descriptor.
        const bool restored_space = setrlimit(RLIMIT_AS, &saved_space) == 0;
        const bool restored_core = setrlimit(RLIMIT_CORE, &saved_core) == 0;
        const bool restored_errors = dup2(saved_errors, 2) == 2;
        const bool restored_directory = fchdir(saved_directory) == 0;
        status = restored_space && restored_core && restored_errors && restored_directory && warmed.before_descriptor == warmed.after_descriptor ? warmed.status : -1;
        parse_test_read_capture(capture, text, sizeof(text));
    }
    if (saved_errors >= 0)
    {
        close(saved_errors);
    }
    if (saved_directory >= 0)
    {
        close(saved_directory);
    }
    check_units_test_expect(tally, status == 0 && text[0] == 0, "the reservations' units compile in this process", text);
    // The objects of a run without a limit, which a run whose failed reservation had a fallback must write again.
    char objects[2][PATH_MAX];
    char wholes[2][PATH_MAX];
    for (uint32_t o = 0; o < 2; o += 1)
    {
        snprintf(objects[o], sizeof(objects[o]), "%s/%s", limits, o == 0 ? "caller.o" : "callee.o");
        snprintf(wholes[o], sizeof(wholes[o]), "%s/%s", limits, o == 0 ? "whole_caller.o" : "whole_callee.o");
        unlink(objects[o]);
    }
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t base = parse_test_status_bytes("VmSize:");
    const CheckUnitsTestProbe whole = status == 0 ? check_units_test_probe(arguments, count, limits, capture, base, 1ull << 46, false, shared) : (CheckUnitsTestProbe){ 0 };
    parse_test_read_capture(capture, text, sizeof(text));
    char detail[1400];
    snprintf(detail, sizeof(detail), "status %d, peak %llu, VmSize %llu -> %llu (parent %llu), descriptors %d -> %d, %s, printed \"%s\"",
             whole.status, (unsigned long long)whole.peak, (unsigned long long)whole.before_size, (unsigned long long)whole.after_size,
             (unsigned long long)base, whole.before_descriptor, whole.after_descriptor,
             whole.clean ? "clean" : "left memory or a descriptor behind", text);
    fprintf(stderr, "  reservations' baseline: %s\n", detail);
    fprintf(stderr, "  reservations' baseline observations: VmSize after command %llu, VmStk %llu -> %llu, VmData %llu -> %llu\n",
            (unsigned long long)whole.after_command_size, (unsigned long long)whole.before_stack, (unsigned long long)whole.after_stack,
            (unsigned long long)whole.before_data, (unsigned long long)whole.after_data);
    bool released = whole.status == 0 && whole.clean && whole.peak != 0 && base != 0 && text[0] == 0 && rename(objects[0], wholes[0]) == 0 &&
                    rename(objects[1], wholes[1]) == 0;
    check_units_test_expect(tally, released, "the reservations' units compile in a child, leaving its address space as it was", detail);
    // From the last reservation to the first: a run with a page less than the last one reached fails where it grew to
    // that, and reaches its own peak before it. A reservation that has a fallback (the emission's columns,
    // CheckEmitRun.columns, in the caller's scratch) doesn't fail the run, which must then write the same objects.
    uint64_t peak = whole.peak;
    uint32_t runs = 0;
    uint32_t setups = 0;
    uint32_t fallbacks = 0;
    while (released && peak != 0 && runs < BUSTER_CHECK_UNITS_TEST_LIMIT_RUNS)
    {
        const uint64_t limit = peak - page;
        unlink(objects[0]);
        unlink(objects[1]);
        const CheckUnitsTestProbe probe = check_units_test_probe(arguments, count, limits, capture, base, limit, false, shared);
        parse_test_read_capture(capture, text, sizeof(text));
        const bool failed = probe.status == 1 && strstr(text, "error: ") != NULL;
        const bool fallen_back = probe.status == 0 && text[0] == 0 && check_test_driver_same_files(objects[0], wholes[0]) &&
                                 check_test_driver_same_files(objects[1], wholes[1]);
        released = (failed || fallen_back) && probe.peak <= limit && probe.clean;
        snprintf(detail, sizeof(detail), "with %llu bytes of address space: status %d, peak %llu, VmSize %llu -> %llu (parent %llu), descriptors %d -> %d, %s, printed \"%s\"",
                 (unsigned long long)limit, probe.status, (unsigned long long)probe.peak, (unsigned long long)probe.before_size,
                 (unsigned long long)probe.after_size, (unsigned long long)base, probe.before_descriptor, probe.after_descriptor,
                 probe.clean ? "clean" : "left memory or a descriptor behind", text);
        check_units_test_expect(tally, released, "a run whose reservation failed says why or writes the same objects, and releases what it reserved", detail);
        if (!released)
        {
            fprintf(stderr, "  reservations' observations with %llu bytes: VmSize after command %llu, VmStk %llu -> %llu, VmData %llu -> %llu\n",
                    (unsigned long long)limit, (unsigned long long)probe.after_command_size, (unsigned long long)probe.before_stack,
                    (unsigned long long)probe.after_stack, (unsigned long long)probe.before_data, (unsigned long long)probe.after_data);
        }
        // A lane's setup that failed fails both units, with its message.
        setups += strstr(text, "can't reserve the lane's memory") != NULL;
        fallbacks += fallen_back;
        peak = probe.peak;
        runs += 1;
    }
    check_units_test_expect(tally, !released || (peak == 0 && setups != 0), "every reservation failed in turn, a lane's setup's among them", limits);
    if (saved_makeflags != NULL)
    {
        const bool restored_makeflags = setenv("MAKEFLAGS", saved_makeflags, 1) == 0;
        check_units_test_expect(tally, restored_makeflags, "restore the reservations' caller's MAKEFLAGS", limits);
        free(saved_makeflags);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(CheckUnitsTestProbe));
    }
    if (capture >= 0)
    {
        close(capture);
    }
    printf("  reservations: %u failed in turn (%u of them a lane's setup's, %u with a fallback), each reported or with the same objects, "
           "and released\n",
           runs, setups, fallbacks);
}

// The jobserver's tokens while the link writes (see the file's comment), in a directory of its own under `directory`.
static void check_units_test_jobserver(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    static const char main_text[] = "#include <stdio.h>\n"
                                    "int job_a(int v);\n"
                                    "int job_b(int v);\n"
                                    "int job_c(int v);\n"
                                    "extern const unsigned char job_table[1 << 20];\n"
                                    "int main(void)\n"
                                    "{\n"
                                    "    const int total = job_a(1) + job_b(2) + job_c(3) + job_table[12345];\n"
                                    "    printf(\"%d\\n\", total);\n"
                                    "    return total == 19 ? 0 : 1;\n"
                                    "}\n";
    static const char a_text[] = "int job_a(int v)\n{\n    return v + 1;\n}\n";
    static const char b_text[] = "int job_b(int v)\n{\n    return v * 2;\n}\n";
    static const char c_text[] = "const unsigned char job_table[1 << 20] = { [12345] = 7 };\nint job_c(int v)\n{\n    return v + 3;\n}\n";
    static const char* const names[] = { "job_main.c", "job_a.c", "job_b.c", "job_c.c" };
    const char* const texts[] = { main_text, a_text, b_text, c_text };
    char jobs_directory[PATH_MAX];
    char path[PATH_MAX];
    snprintf(jobs_directory, sizeof(jobs_directory), "%s/jobserver", directory);
    bool written = mkdir(jobs_directory, 0755) == 0;
    for (uint32_t f = 0; written && f < BUSTER_ARRAY_LENGTH(names); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", jobs_directory, names[f]);
        written = parse_test_write(path, texts[f], strlen(texts[f]));
    }
    // The jobserver, a FIFO of three tokens the test holds open both ways, and the executable's path, a FIFO.
    char jobs[PATH_MAX];
    char program[PATH_MAX];
    char errors_path[PATH_MAX];
    snprintf(jobs, sizeof(jobs), "%s/jobs", jobs_directory);
    snprintf(program, sizeof(program), "%s/program", jobs_directory);
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", jobs_directory);
    written = written && mkfifo(jobs, 0600) == 0 && mkfifo(program, 0600) == 0;
    const int read_end = written ? open(jobs, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    const int write_end = read_end >= 0 ? open(jobs, O_WRONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    const uint8_t tokens[] = { 'j', 0, 'x' };
    const bool ready = write_end >= 0 && write(write_end, tokens, sizeof(tokens)) == sizeof(tokens);
    check_units_test_expect(tally, ready, "write the jobserver's units, its FIFO of three tokens and the executable's FIFO", jobs_directory);
    char flags[PATH_MAX + 64];
    snprintf(flags, sizeof(flags), " -j4 --jobserver-auth=fifo:%s", jobs);
    fflush(stdout);
    fflush(stderr);
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        const struct rlimit limit = { 0 };
        const int err = open(errors_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (err >= 0 && dup2(err, 1) == 1 && dup2(err, 2) == 2 && chdir(jobs_directory) == 0 && setrlimit(RLIMIT_CORE, &limit) == 0 &&
            setenv("MAKEFLAGS", flags, 1) == 0)
        {
            alarm(BUSTER_CHECK_UNITS_TEST_SECONDS);
            char* arguments[] = { (char*)executable, "--lanes", "4", "-o", "program", (char*)names[0], (char*)names[1], (char*)names[2], (char*)names[3], NULL };
            execv(executable, arguments);
        }
        _exit(127);
    }
    // The executable's FIFO, read as its writer writes it: its first bytes come while the writer holds the rest.
    const int output = child > 0 ? open(program, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    bool writing = false;
    int status = 0;
    bool ended = false;
    for (uint32_t tick = 0; output >= 0 && !writing && !ended && tick < BUSTER_CHECK_UNITS_TEST_SECONDS * 10; tick += 1)
    {
        struct pollfd ready_output = { .fd = output, .events = POLLIN };
        writing = poll(&ready_output, 1, 100) == 1 && (ready_output.revents & POLLIN) != 0;
        ended = !writing && waitpid(child, &status, WNOHANG) == child;
    }
    const int held = writing ? worker_budget_test_available(read_end) : -1;
    check_units_test_expect(tally, writing && held == 0, "while the link writes, the jobserver holds none of the tokens its lanes took", errors_path);
    // The rest of the executable, then the command's end.
    uint64_t length = 0;
    uint64_t capacity = 4u << 20;
    uint8_t* bytes = writing ? malloc(capacity) : NULL;
    bool drained = bytes != NULL && fcntl(output, F_SETFL, 0) == 0;
    for (ssize_t got = 1; drained && got > 0;)
    {
        got = length < capacity ? read(output, bytes + length, capacity - length) : -1;
        drained = got >= 0 || errno == EINTR;
        length += got > 0 ? (uint64_t)got : 0;
    }
    if (output >= 0)
    {
        close(output);
    }
    const bool waited = ended || (child > 0 && waitpid(child, &status, 0) == child);
    check_units_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0 && drained && length > (1u << 20),
                            "the link under the jobserver succeeds, its executable of more than a megabyte read whole", errors_path);
    check_units_test_expect(tally, read_end >= 0 && worker_budget_test_tokens(read_end, tokens, sizeof(tokens)),
                            "once the command ends, the jobserver holds every token again, each once", jobs);
    // The executable the test read runs.
    char copy[PATH_MAX];
    snprintf(copy, sizeof(copy), "%s/program.copy", jobs_directory);
    const int copied = drained ? open(copy, O_WRONLY | O_CREAT | O_TRUNC, 0755) : -1;
    const bool saved = copied >= 0 && write(copied, bytes, length) == (ssize_t)length;
    if (copied >= 0)
    {
        close(copied);
    }
    char* run[] = { "./program.copy", NULL };
    snprintf(path, sizeof(path), "%s/run.txt", jobs_directory);
    const int ran = saved ? check_units_test_run(jobs_directory, run, path, "/dev/null", false) : -1;
    uint64_t output_length = 0;
    char* printed = ran == 0 ? check_units_test_read(path, &output_length) : NULL;
    check_units_test_expect(tally, ran == 0 && printed != NULL && strcmp(printed, "19\n") == 0, "the executable the jobserver's link wrote runs", copy);
    free(printed);
    free(bytes);
    if (write_end >= 0)
    {
        close(write_end);
    }
    if (read_end >= 0)
    {
        close(read_end);
    }
    printf("  jobserver: its tokens held while the link writes, every one back after\n");
}

// The first program's objects and executable in a child whose lanes can't start (see the file's comment).
static void check_units_test_unstarted(CheckUnitsTestTally* tally, const char* executable, const char* directory)
{
    char program_directory[PATH_MAX];
    snprintf(program_directory, sizeof(program_directory), "%s/program_0", directory);
    char source[PATH_MAX];
    uint32_t count = 0;
    for (uint32_t u = 0; u < BUSTER_CHECK_UNITS_TEST_MAX_UNITS; u += 1)
    {
        snprintf(source, sizeof(source), "%s/unit_%u.c", program_directory, u);
        count = check_units_test_exists(source) ? u + 1 : count;
    }
    char names[BUSTER_CHECK_UNITS_TEST_MAX_UNITS][32];
    char* arguments[64];
    char errors_path[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/errors.txt", program_directory);
    const char* options[] = { "--lanes", "8", "-o", "unstarted", NULL };
    check_units_test_command(arguments, names, executable, options, count);
    const int status = count != 0 ? check_units_test_run(program_directory, arguments, "/dev/null", errors_path, true) : -1;
    char unstarted[PATH_MAX];
    char one[PATH_MAX];
    snprintf(unstarted, sizeof(unstarted), "%s/unstarted", program_directory);
    snprintf(one, sizeof(one), "%s/program_0_0", program_directory);
    check_units_test_expect(tally, status == 0 && check_test_driver_same_files(unstarted, one), "lanes that don't start link one lane's executable",
                            program_directory);
    const char* object_options[] = { "--lanes", "8", "-c", NULL };
    check_units_test_command(arguments, names, executable, object_options, count);
    const int objected = count != 0 ? check_units_test_run(program_directory, arguments, "/dev/null", errors_path, true) : -1;
    bool same = objected == 0;
    for (uint32_t u = 0; u < count; u += 1)
    {
        char together[PATH_MAX];
        char apart[PATH_MAX];
        snprintf(together, sizeof(together), "%s/unit_%u.o", program_directory, u);
        snprintf(apart, sizeof(apart), "%s/alone_%u.o", program_directory, u);
        same = same && check_test_driver_same_files(together, apart);
    }
    check_units_test_expect(tally, same, "lanes that don't start write each unit's object as alone", program_directory);
    printf("  unstarted: lanes that don't start, one lane's outputs\n");
}

// --corpus: every .c file of `corpus` at once with -c, and each alone (see the file's comment), in `directory`.
static void check_units_test_corpus(CheckUnitsTestTally* tally, const char* executable, const char* corpus, char* const* options, uint32_t option_count,
                                    const char* directory)
{
    DIR* listing = opendir(corpus);
    check_units_test_expect(tally, listing != NULL, "open the corpus", corpus);
    uint32_t count = 0;
    uint32_t capacity = 0;
    char** files = NULL;
    for (struct dirent* entry = listing != NULL ? readdir(listing) : NULL; entry != NULL; entry = readdir(listing))
    {
        const uint64_t length = strlen(entry->d_name);
        if (length > 2 && strcmp(entry->d_name + length - 2, ".c") == 0)
        {
            if (count == capacity)
            {
                capacity = capacity != 0 ? capacity * 2 : 64;
                char** grown = realloc(files, capacity * sizeof(char*));
                files = grown != NULL ? grown : files;
                capacity = grown != NULL ? capacity : count;
            }
            if (count < capacity)
            {
                char* file = malloc(strlen(corpus) + length + 2);
                if (file != NULL)
                {
                    sprintf(file, "%s/%s", corpus, entry->d_name);
                    files[count++] = file;
                }
            }
        }
    }
    if (listing != NULL)
    {
        closedir(listing);
    }
    // In the files' order by name, as a sorted listing gives them.
    for (uint32_t i = 1; i < count; i += 1)
    {
        char* moving = files[i];
        uint32_t j = i;
        while (j != 0 && strcmp(files[j - 1], moving) > 0)
        {
            files[j] = files[j - 1];
            j -= 1;
        }
        files[j] = moving;
    }
    char together[PATH_MAX];
    char apart[PATH_MAX];
    snprintf(together, sizeof(together), "%s/corpus_together", directory);
    snprintf(apart, sizeof(apart), "%s/corpus_apart", directory);
    const bool made = mkdir(together, 0755) == 0 && mkdir(apart, 0755) == 0;
    check_units_test_expect(tally, made, "make the corpus's directories", directory);
    char errors_path[PATH_MAX];
    snprintf(errors_path, sizeof(errors_path), "%s/corpus_errors.txt", directory);
    // Each file alone: its status and its diagnostics, which the command of all of them prints in their order.
    uint32_t compiled = 0;
    char* diagnostics = NULL;
    uint64_t diagnostic_length = 0;
    bool any_failed = false;
    int* statuses = count != 0 ? calloc(count, sizeof(int)) : NULL;
    char** arguments = calloc((uint64_t)count + option_count + 8, sizeof(char*));
    for (uint32_t f = 0; made && arguments != NULL && statuses != NULL && f < count; f += 1)
    {
        uint32_t used = 0;
        arguments[used++] = (char*)executable;
        arguments[used++] = "-c";
        for (uint32_t o = 0; o < option_count; o += 1)
        {
            arguments[used++] = options[o];
        }
        arguments[used++] = files[f];
        arguments[used] = NULL;
        statuses[f] = check_units_test_run(apart, arguments, "/dev/null", errors_path, false);
        any_failed |= statuses[f] != 0;
        compiled += statuses[f] == 0;
        uint64_t length = 0;
        char* text = check_units_test_read(errors_path, &length);
        char* grown = text != NULL ? realloc(diagnostics, diagnostic_length + length + 1) : NULL;
        if (grown != NULL)
        {
            diagnostics = grown;
            memcpy(diagnostics + diagnostic_length, text, length);
            diagnostic_length += length;
            diagnostics[diagnostic_length] = 0;
        }
        free(text);
    }
    for (uint32_t poisoned = 0; made && arguments != NULL && statuses != NULL && poisoned < 2; poisoned += 1)
    {
        uint32_t used = 0;
        arguments[used++] = (char*)executable;
        if (poisoned != 0)
        {
            arguments[used++] = "--poison-arenas";
        }
        arguments[used++] = "--lanes";
        arguments[used++] = "8";
        arguments[used++] = "-c";
        for (uint32_t o = 0; o < option_count; o += 1)
        {
            arguments[used++] = options[o];
        }
        for (uint32_t f = 0; f < count; f += 1)
        {
            arguments[used++] = files[f];
        }
        arguments[used] = NULL;
        const int status = check_units_test_run(together, arguments, "/dev/null", errors_path, false);
        uint64_t length = 0;
        char* text = check_units_test_read(errors_path, &length);
        const char* what = poisoned != 0 ? "the corpus at once, the lanes' arenas poisoned" : "the corpus at once";
        check_units_test_expect(tally, status == (any_failed ? 1 : 0), "the corpus at once exits as its files alone do", what);
        check_units_test_expect(tally, text != NULL && length == diagnostic_length && (length == 0 || memcmp(text, diagnostics, length) == 0),
                                "the corpus's diagnostics at once are its files' alone, in order", what);
        free(text);
        uint32_t same = 0;
        for (uint32_t f = 0; f < count; f += 1)
        {
            const char* slash = strrchr(files[f], '/');
            char object[PATH_MAX];
            snprintf(object, sizeof(object), "%s", slash != NULL ? slash + 1 : files[f]);
            object[strlen(object) - 1] = 'o';
            char first[PATH_MAX];
            char second[PATH_MAX];
            snprintf(first, sizeof(first), "%s/%s", together, object);
            snprintf(second, sizeof(second), "%s/%s", apart, object);
            const bool matched = statuses[f] == 0 ? check_test_driver_same_files(first, second) : !check_units_test_exists(first);
            same += matched;
            check_units_test_expect(tally, matched, "a corpus file's object at once is its object alone", files[f]);
            unlink(first);
        }
        printf("  corpus: %u files, %u compiled, %u objects as alone%s\n", count, compiled, same, poisoned != 0 ? ", the lanes' arenas poisoned" : "");
    }
    for (uint32_t f = 0; f < count; f += 1)
    {
        free(files[f]);
    }
    free(files);
    free(arguments);
    free(statuses);
    free(diagnostics);
}

static int run_check_units_tests(int argc, char** argv)
{
    CheckUnitsTestTally tally = { .random = 0x9e3779b97f4a7c15ull };
    const char* executable = "/proc/self/exe";
    const char* clang = "clang";
    const char* corpus = NULL;
    char* options[32];
    uint32_t option_count = 0;
    const char* generate = NULL;
    uint32_t synth_units = 512;
    uint32_t synth_functions = 200;
    bool keep = false;
    uint32_t programs = 4;
    bool parsed = true;
    for (int i = 0; parsed && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            tally.random ^= strtoull(argv[++i], 0, 0) * 0x2545f4914f6cdd1dull;
            tally.random |= 1;
        }
        else if (strcmp(argv[i], "--programs") == 0 && i + 1 < argc)
        {
            programs = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc)
        {
            corpus = argv[++i];
        }
        else if (strcmp(argv[i], "--option") == 0 && i + 1 < argc && option_count < BUSTER_ARRAY_LENGTH(options))
        {
            options[option_count++] = argv[++i];
        }
        else if (strcmp(argv[i], "--generate") == 0 && i + 1 < argc)
        {
            generate = argv[++i];
        }
        else if (strcmp(argv[i], "--units") == 0 && i + 1 < argc)
        {
            synth_units = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            synth_functions = (uint32_t)strtoul(argv[++i], 0, 0);
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            parsed = false;
        }
    }
    check_units_test_expect(&tally, parsed && synth_units != 0 && synth_functions != 0,
                            "options: [--executable PATH] [--clang PATH] [--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...] "
                            "[--generate DIR [--units N] [--functions N]]",
                            NULL);
    char resolved[PATH_MAX];
    const ssize_t resolved_length = strcmp(executable, "/proc/self/exe") == 0 ? readlink(executable, resolved, sizeof(resolved) - 1) : -1;
    if (resolved_length > 0)
    {
        resolved[resolved_length] = 0;
        executable = resolved;
    }
    char absolute[PATH_MAX];
    if (executable[0] != '/' && realpath(executable, absolute) != NULL)
    {
        executable = absolute;
    }
    if (parsed && generate != NULL)
    {
        // A program of synth's shape for measurements (docs/translation-units.md), written where asked, and nothing else.
        const bool written = (mkdir(generate, 0755) == 0 || errno == EEXIST) && check_units_test_synth(&tally, generate, synth_units, synth_functions);
        check_units_test_expect(&tally, written, "write a program of synth's shape", generate);
        printf("check units: %llu checks, %llu failures\n", (unsigned long long)tally.checks, (unsigned long long)tally.failures);
        return tally.failures == 0 ? 0 : 1;
    }
    char directory[PATH_MAX];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-check-units-XXXXXX", temporary != NULL && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = parsed && mkdtemp(directory) != NULL;
    check_units_test_expect(&tally, made, "make the test's directory", directory);
    if (made && corpus != NULL)
    {
        check_units_test_corpus(&tally, executable, corpus, options, option_count, directory);
    }
    else if (made)
    {
        check_units_test_programs(&tally, executable, clang, directory, programs != 0 ? programs : 1);
        check_units_test_diagnostics(&tally, executable, directory);
        check_units_test_refusals(&tally, executable, directory);
        check_units_test_merge(&tally, executable, directory);
        check_units_test_shared(&tally, executable, directory);
        check_units_test_limits(&tally, directory);
        check_units_test_jobserver(&tally, executable, directory);
        check_units_test_unstarted(&tally, executable, directory);
    }
    if (made && !keep && tally.failures == 0)
    {
        char* remove[] = { "rm", "-rf", directory, NULL };
        check_units_test_run("/", remove, "/dev/null", "/dev/null", false);
    }
    else if (made)
    {
        printf("  kept %s\n", directory);
    }
    printf("check units: %llu checks, %llu failures\n", (unsigned long long)tally.checks, (unsigned long long)tally.failures);
    return tally.failures == 0 ? 0 : 1;
}
