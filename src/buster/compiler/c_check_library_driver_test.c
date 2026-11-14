// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's library builtins through the driver, against clang as an external oracle (c_builtin_library.c): the implicit
// declarations of every plain library name, in each language mode and with -fno-builtin, -fno-builtin-NAME and
// -ffreestanding, whose diagnostics must be clang's line for line; that calls of strlcat and strlcpy refer to the
// library's symbols rather than folding; programs that call library functions declared so, fold plain declarations'
// calls or not as those options say, and abort, which must run as clang's builds do. The plain names are pinned to
// BUSTER_CLANG_REFERENCE_VERSION's official build: a found clang of another build reports both, and the implicit
// comparisons skip only the names its library-builtin table is known to classify differently (Debian's and Ubuntu's
// packages drop strlcat and strlcpy). Include after c_check_fortify_driver_test.c, whose runner it takes.

// The lines of a compiler's diagnostics that implicit declarations make (clang's ext_implicit_lib_function_decl and its
// C99 form, the header's note, and the ordinary implicit declarations' and undeclared identifiers' errors), in order,
// into `lines`; false where they don't fit.
static bool check_test_library_lines(const char* text, char* lines, size_t capacity)
{
    static const char* const parts[] = {
        "library function",
        "include the header",
        "undeclared function",
        "implicit declaration of function",
        "undeclared identifier",
    };
    size_t length = 0;
    bool fits = capacity != 0;
    lines[0] = 0;
    for (const char* line = text; fits && line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        bool kept = false;
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(parts); p += 1)
        {
            const char* found = strstr(line, parts[p]);
            kept |= found != NULL && found < line + size;
        }
        fits = !kept || length + size + 1 < capacity;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

// The first line where two texts differ, for a failure's description.
static void check_test_library_difference(const char* a, const char* b, char* difference, size_t capacity)
{
    size_t at = 0;
    while (a[at] != 0 && a[at] == b[at])
    {
        at += 1;
    }
    while (at > 0 && a[at - 1] != '\n')
    {
        at -= 1;
    }
    const char* a_end = strchr(a + at, '\n');
    const char* b_end = strchr(b + at, '\n');
    snprintf(difference, capacity, "clang \"%.*s\", ours \"%.*s\"", (int)(a_end != NULL ? a_end - (a + at) : (ptrdiff_t)strlen(a + at)), a + at,
             (int)(b_end != NULL ? b_end - (b + at) : (ptrdiff_t)strlen(b + at)), b + at);
}

// The plain library names whose __has_builtin answer under a configuration differs between the found clang and buster's
// table and that the found build's table is known to classify differently (cbuiltin_library_table_differs: Debian's and
// Ubuntu's packages drop strlcat and strlcpy): those are the names the configuration's comparisons skip when the found
// build isn't the reference, since the answer is the build's table's. A difference no named build explains is no
// skip's — it stays in the comparisons and fails. `pinned` has one entry per plain name, `names` lists them, and the
// result is false when clang's probe doesn't run.
static bool check_test_library_pinned(const char* clang, const char* standard, const char* first, const char* second, const char* probe,
                                     bool* pinned, uint32_t* pinned_count, char* names, size_t names_capacity)
{
    *pinned_count = 0;
    names[0] = 0;
    memset(pinned, 0, BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT * sizeof(bool));
    char* arguments[9];
    uint32_t argument_count = 0;
    arguments[argument_count++] = (char*)clang;
    arguments[argument_count++] = (char*)standard;
    if (first != NULL)
    {
        arguments[argument_count++] = (char*)first;
    }
    if (second != NULL)
    {
        arguments[argument_count++] = (char*)second;
    }
    arguments[argument_count++] = (char*)"-E";
    arguments[argument_count++] = (char*)"-P";
    arguments[argument_count++] = (char*)probe;
    arguments[argument_count] = NULL;
    char* text = NULL;
    bool result = check_test_driver_spawn_stream(arguments, &text, true) == 0 && text != NULL;
    bool clang_answer[BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT];
    uint32_t index = 0;
    for (const char* line = text; result && line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        if (length == 4 && memcmp(line, "HB ", 3) == 0 && (line[3] == '0' || line[3] == '1'))
        {
            result = index < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT;
            if (result)
            {
                clang_answer[index] = line[3] == '1';
                index += 1;
            }
        }
        else if (length != 0)
        {
            result = false;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    result &= index == BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT;
    // Buster's answers for the same names and options.
    Arena arena = arena_reserve(1ull << 20);
    char* parsed_arguments[5];
    uint32_t parsed_count = 0;
    parsed_arguments[parsed_count++] = (char*)standard;
    if (first != NULL)
    {
        parsed_arguments[parsed_count++] = (char*)first;
    }
    if (second != NULL)
    {
        parsed_arguments[parsed_count++] = (char*)second;
    }
    parsed_arguments[parsed_count++] = (char*)probe;
    PPOptions options;
    const char* input = NULL;
    const char* output = NULL;
    bool markers = true;
    const bool parsed =
        result && arena.base != NULL && pp_parse_options((int)parsed_count, parsed_arguments, &options, &arena, &input, &output, &markers) && output == NULL;
    result &= parsed;
    for (uint32_t p = 0; result && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        const CBuiltinLibraryPlain* plain = cbuiltin_library_plains + p;
        const uint32_t ours = pp_feature(&options, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)plain->name, plain->length);
        if ((ours != 0) != clang_answer[p] && cbuiltin_library_table_differs(check_driver_clang_version, plain->name, plain->length))
        {
            pinned[p] = true;
            *pinned_count += 1;
            const size_t used = strlen(names);
            if (used != 0 && used + 2 < names_capacity)
            {
                memcpy(names + used, ", ", 2);
            }
            const size_t at = strlen(names);
            if (at + plain->length < names_capacity)
            {
                memcpy(names + at, plain->name, plain->length + 1);
            }
            else
            {
                snprintf(names + at, names_capacity - at, "...");
            }
        }
    }
    arena_release(&arena);
    free(text);
    if (!result)
    {
        // The fixture then compares nothing rather than a partly computed skip.
        memset(pinned, 1, BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT * sizeof(bool));
        *pinned_count = BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT;
        names[0] = 0;
    }
    return result;
}

// Every plain library name called once, undeclared, in each configuration, then non-call uses of names before their
// calls, then the names of FILE, jmp_buf and ucontext_t after their typedefs: clang creates the builtin's declaration where its name is looked up, call or not, and its diagnostic says
// "call" either way. Each configuration's lines must be clang's, and its status, by both paths: some lines, but none
// where -Wno-implicit-function-declaration turns the diagnostics off (`quiet`). The configurations include strlcat's and
// strlcpy's -fno-builtin-NAME gate and C89's silent ordinary implicit declaration, whose library wording depends on the
// build: when the found clang isn't the reference, the names it is known to classify differently are left out and
// reported.
static void check_test_library_implicit(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
        bool quiet;
    } configurations[] = {
        { "-std=gnu89", NULL, NULL },
        { "-std=c89", NULL, NULL },
        // C89's ordinary implicit declaration is silent by default: -Wimplicit-function-declaration shows its wording,
        // which names no library and no header.
        { "-std=c89", "-Wimplicit-function-declaration", NULL },
        { "-std=gnu89", "-fno-builtin-strlcat", "-Wimplicit-function-declaration" },
        { "-std=c99", NULL, NULL },
        { "-std=gnu17", NULL, NULL },
        { "-std=c17", NULL, NULL },
        { "-std=gnu23", NULL, NULL },
        { "-std=gnu17", "-fno-builtin", NULL },
        { "-std=gnu89", "-fno-builtin-strlcat", NULL },
        { "-std=c17", "-fno-builtin-strlcat", NULL },
        { "-std=gnu17", "-fno-builtin-strlcat", NULL },
        { "-std=gnu23", "-fno-builtin-strlcat", NULL },
        { "-std=gnu17", "-fno-builtin-strlcpy", NULL },
        { "-std=gnu89", "-fno-builtin-sin", "-fno-builtin-strlen" },
        { "-std=gnu17", "-ffreestanding", NULL },
        { "-std=gnu17", "-ffreestanding", "-fbuiltin" },
        { "-std=gnu17", "-fno-builtin", "-fbuiltin" },
        { "-std=gnu17", "-Wno-implicit-function-declaration", NULL, true },
    };
    static const char uses[] = "double (*pointer)(double) = cos;\nint f(void)\n{\n    unsigned long (*length)(const char *) = &strlen;\n"
                               "    return (int)length(\"abc\") + (int)pointer(0.0) + (int)strlen(\"de\") + abs(-1);\n}\n";
    const uint64_t capacity = 64 + (uint64_t)BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT * (BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME + 8);
    const uint64_t probe_capacity = (uint64_t)BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT * (BUSTER_CBUILTIN_LIBRARY_PLAIN_MAX_NAME + 48);
    char* calls = malloc(capacity);
    char* probe = malloc(probe_capacity);
    uint64_t probe_length = 0;
    for (uint32_t p = 0; probe != NULL && probe_length < probe_capacity && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
    {
        const CBuiltinLibraryPlain* plain = cbuiltin_library_plains + p;
        const int written = snprintf(probe + probe_length, probe_capacity - probe_length, "#if __has_builtin(%s)\nHB 1\n#else\nHB 0\n#endif\n", plain->name);
        probe_length += written > 0 ? (uint64_t)written : 0;
    }
    // And the names whose prototypes take the unit's FILE, jmp_buf, sigjmp_buf or ucontext_t, after typedefs of them, which
    // the diagnostics spell (CBuiltinLibraryNamed); each configuration writes its own, leaving out the names it skips.
    char* typed = malloc(capacity);
    char paths[4][512];
    snprintf(paths[0], sizeof(paths[0]), "%s/library-calls.c", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/library-uses.c", directory);
    snprintf(paths[2], sizeof(paths[2]), "%s/library-typed.c", directory);
    snprintf(paths[3], sizeof(paths[3]), "%s/library-builtins.c", directory);
    const bool written = calls != NULL && typed != NULL && probe != NULL && probe_length < probe_capacity && parse_test_write(paths[3], probe, probe_length) &&
                         parse_test_write(paths[1], uses, sizeof(uses) - 1);
    check_test_expect(test, written, "library implicit declarations: fixtures written");
    enum { LINES = 1 << 20 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    for (uint32_t c = 0; written && expected_lines != NULL && actual_lines != NULL && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        bool pinned[BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT];
        uint32_t pinned_count = 0;
        char pinned_names[192] = { 0 };
        bool probe_ready = true;
        if (check_driver_clang_reference)
        {
            memset(pinned, 0, sizeof(pinned));
        }
        else
        {
            probe_ready = check_test_library_pinned(clang, configurations[c].standard, configurations[c].first, configurations[c].second, paths[3], pinned,
                                                    &pinned_count, pinned_names, sizeof(pinned_names));
            check_test_expect(test, probe_ready, "library implicit declarations: the found clang's __has_builtin probe runs");
        }
        if (!probe_ready)
        {
            continue;
        }
        uint64_t length = (uint64_t)snprintf(calls, capacity, "void f(void)\n{\n");
        for (uint32_t p = 0; length < capacity && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
        {
            if (!pinned[p])
            {
                length += (uint64_t)snprintf(calls + length, capacity - length, "    %s();\n", cbuiltin_library_plains[p].name);
            }
        }
        if (length < capacity)
        {
            length += (uint64_t)snprintf(calls + length, capacity - length, "}\n");
        }
        uint64_t typed_length = (uint64_t)snprintf(typed, capacity, "typedef struct F FILE;\ntypedef long jmp_buf[8];\ntypedef int* sigjmp_buf[3];\n"
                                                                    "typedef struct U { int x; } ucontext_t;\nvoid f(void)\n{\n");
        for (uint32_t p = 0; typed_length < capacity && p < BUSTER_CBUILTIN_LIBRARY_PLAIN_COUNT; p += 1)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup(cbuiltin_library_plains[p].name, cbuiltin_library_plains[p].length);
            const bool named = !pinned[p] && info != NULL &&
                               (memchr(info->encoding, 'P', info->encoding_length) != NULL || memchr(info->encoding, 'J', info->encoding_length) != NULL ||
                                memchr(info->encoding, 'K', info->encoding_length) != NULL);
            typed_length += named ? (uint64_t)snprintf(typed + typed_length, capacity - typed_length, "    %s();\n", cbuiltin_library_plains[p].name) : 0;
        }
        if (typed_length < capacity)
        {
            typed_length += (uint64_t)snprintf(typed + typed_length, capacity - typed_length, "}\n");
        }
        const bool calls_written = length < capacity && parse_test_write(paths[0], calls, length) && typed_length < capacity &&
                                   parse_test_write(paths[2], typed, typed_length);
        check_test_expect(test, calls_written, "library implicit declarations: calls fixture written");
        if (!calls_written)
        {
            continue;
        }
        if (pinned_count != 0)
        {
            printf("library implicit declarations %s %s %s: %u plain names skipped, as the found clang's library builtin table differs from the reference %s's: "
                   "%s\n",
                   configurations[c].standard, configurations[c].first != NULL ? configurations[c].first : "",
                   configurations[c].second != NULL ? configurations[c].second : "", pinned_count, BUSTER_CLANG_REFERENCE_VERSION, pinned_names);
        }
        for (uint32_t f = 0; f < 3; f += 1)
        {
            char* arguments[12] = { (char*)clang, (char*)configurations[c].standard, "-fno-caret-diagnostics", "-ferror-limit=0", "-fsyntax-only", paths[f] };
            uint32_t count = 6;
            if (configurations[c].first != NULL)
            {
                arguments[count++] = (char*)configurations[c].first;
            }
            if (configurations[c].second != NULL)
            {
                arguments[count++] = (char*)configurations[c].second;
            }
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            const bool kept = expected != NULL && check_test_library_lines(expected, expected_lines, LINES);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[count] = scalar != 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = kept && actual != NULL && check_test_library_lines(actual, actual_lines, LINES) &&
                                  strcmp(expected_lines, actual_lines) == 0 && (expected_lines[0] == 0) == configurations[c].quiet;
                char difference[512] = { 0 };
                if (kept && actual != NULL && !same)
                {
                    check_test_library_difference(expected_lines, actual_lines, difference, sizeof(difference));
                }
                char description[768];
                snprintf(description, sizeof(description), "library implicit declarations %s %s %s %s, %s: status %d (clang %d), %s",
                         configurations[c].standard, configurations[c].first != NULL ? configurations[c].first : "",
                         configurations[c].second != NULL ? configurations[c].second : "", f == 0 ? "calls" : f == 1 ? "uses" : "typed", scalar != 0 ? "scalar" : "vector",
                         status, oracle, difference);
                check_test_expect(test, oracle >= 0 && status == oracle && same, description);
                free(actual);
            }
            free(expected);
        }
    }
    free(actual_lines);
    free(expected_lines);
    free(probe);
    free(calls);
    free(typed);
    unlink(paths[0]);
    unlink(paths[1]);
    unlink(paths[2]);
    unlink(paths[3]);
}

// Whether the object at `path` has an undefined symbol named `name`: a call of the library function has one, and a fold
// of the call would leave none.
static bool check_test_library_undefined(const char* path, const char* name)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shoff <= length &&
                header.e_shnum <= (length - header.e_shoff) / sizeof(Elf64_Shdr);
    }
    bool found = false;
    for (uint32_t s = 0; valid && !found && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr table;
        memcpy(&table, bytes + header.e_shoff + (uint64_t)s * sizeof(table), sizeof(table));
        if (table.sh_type == SHT_SYMTAB)
        {
            valid = table.sh_link < header.e_shnum && table.sh_entsize == sizeof(Elf64_Sym) && table.sh_offset <= length &&
                    table.sh_size <= length - table.sh_offset && table.sh_size % sizeof(Elf64_Sym) == 0;
            Elf64_Shdr strings = { 0 };
            if (valid)
            {
                memcpy(&strings, bytes + header.e_shoff + table.sh_link * sizeof(strings), sizeof(strings));
                valid = strings.sh_offset <= length && strings.sh_size <= length - strings.sh_offset;
            }
            for (uint64_t i = 0; valid && !found && i < table.sh_size / sizeof(Elf64_Sym); i += 1)
            {
                Elf64_Sym symbol;
                memcpy(&symbol, bytes + table.sh_offset + i * sizeof(symbol), sizeof(symbol));
                valid = symbol.st_name < strings.sh_size;
                const char* spelling = valid ? (const char*)bytes + strings.sh_offset + symbol.st_name : NULL;
                const char* end = valid ? memchr(spelling, 0, (size_t)(strings.sh_size - symbol.st_name)) : NULL;
                valid = valid && end != NULL;
                found = valid && symbol.st_shndx == SHN_UNDEF && (size_t)(end - spelling) == strlen(name) && memcmp(spelling, name, strlen(name)) == 0;
            }
        }
    }
    free(bytes);
    return valid && found;
}

// strlcat and strlcpy are plain calls, never folds: buster's and clang's objects for a call of each name must refer to
// the library's function (an undefined symbol) in each mode and with -fno-builtin and -fno-builtin-NAME too. Buster's
// plain fold list (c_builtin_library.c) has only comparisons, lengths and memory copies, so a call here that referred
// to no function would be an unearned fold.
static void check_test_library_lowering(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const names[] = { "strlcat", "strlcpy" };
    // The function is declared in every case: a call clang leaves a call must be a call from a declaration too, and C23
    // and a release whose table drops the name have no implicit declaration to fall back on.
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
    } configurations[] = {
        { "-std=gnu89", NULL, NULL },
        { "-std=gnu17", NULL, NULL },
        { "-std=gnu23", NULL, NULL },
        { "-std=c17", NULL, NULL },
        { "-std=gnu17", "-fno-builtin", NULL },
        { "-std=gnu17", "-fno-builtin-strlcat", NULL },
        { "-std=gnu17", "-fno-builtin-strlcpy", NULL },
    };
    char source[512];
    char objects[3][512];
    snprintf(source, sizeof(source), "%s/library-lowering.c", directory);
    for (uint32_t b = 0; b < 3; b += 1)
    {
        snprintf(objects[b], sizeof(objects[b]), "%s/library-lowering-%u.o", directory, b);
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            char text[512];
            const int declaration = snprintf(text, sizeof(text), "unsigned long %s(char*,const char*,unsigned long);\n", names[n]);
            const int body = declaration >= 0 && (size_t)declaration < sizeof(text)
                                 ? snprintf(text + declaration, sizeof(text) - (size_t)declaration,
                                            "unsigned long f(void){char b[8]=\"a\";return %s(b,\"b\",sizeof b);}\n", names[n])
                                 : -1;
            const bool written = declaration >= 0 && body > 0 && (size_t)(declaration + body) < sizeof(text) &&
                                 parse_test_write(source, text, (uint64_t)(declaration + body));
            bool symboled[3] = { false, false, false };
            for (uint32_t b = 0; written && b < 3; b += 1)
            {
                char* arguments[10];
                uint32_t count = 0;
                arguments[count++] = (char*)(b == 0 ? clang : executable);
                arguments[count++] = (char*)configurations[c].standard;
                if (configurations[c].first != NULL)
                {
                    arguments[count++] = (char*)configurations[c].first;
                }
                if (configurations[c].second != NULL)
                {
                    arguments[count++] = (char*)configurations[c].second;
                }
                arguments[count++] = (char*)"-c";
                arguments[count++] = (char*)"-o";
                arguments[count++] = objects[b];
                arguments[count++] = source;
                if (b == 2)
                {
                    arguments[count++] = (char*)"--scalar";
                }
                arguments[count] = NULL;
                char* diagnostics = NULL;
                unlink(objects[b]);
                const int status = check_test_driver_spawn(arguments, &diagnostics);
                free(diagnostics);
                symboled[b] = status == 0 && check_test_library_undefined(objects[b], names[n]);
            }
            char description[384];
            snprintf(description, sizeof(description), "library builtins: a %s call refers to the function's symbol %s %s %s (clang %s, vector %s, scalar %s)", names[n],
                     configurations[c].standard, configurations[c].first != NULL ? configurations[c].first : "",
                     configurations[c].second != NULL ? configurations[c].second : "", symboled[0] ? "yes" : "no", symboled[1] ? "yes" : "no",
                     symboled[2] ? "yes" : "no");
            check_test_expect(test, written && symboled[0] && symboled[1] && symboled[2], description);
            for (uint32_t b = 0; b < 3; b += 1)
            {
                unlink(objects[b]);
            }
        }
    }
    unlink(source);
}

// Programs built by clang and by this executable, by both paths, that must print the same and exit alike: C89's calls
// of library functions declared implicitly, with their rows' prototypes (sin's double, sprintf's variadic call);
// strlcat and strlcpy's calls, which reach the library's functions; a plain declaration's strcmp of constants, which
// folds to clang's -1 where the name is a builtin and calls the library's otherwise, by -fno-builtin,
// -fno-builtin-strcmp and -ffreestanding; __builtin_strcmp's fold, which no option turns off; and __builtin_abort.
static void check_test_library_run(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* name,
                                   const char* program_source, const char* standard, const char* option);

// __builtin_cpu_supports and __builtin_cpu_is of every name of the table (c_builtin_cpu.inc) against clang's code (#148):
// a program that defines libgcc's model itself, sets each bit of each feature word alone and each value from 0 to 63 of
// the vendor, the type and the subtype, and prints which make each name's test true, built by clang and by this
// executable (check_test_library_run), so that any name's word, bit, field or value other than clang's shows.
static void check_test_cpu_model_runs(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const features[] = {
#define BUSTER_CBUILTIN_CPU_FEATURE(name, word, bit) name,
#define BUSTER_CBUILTIN_CPU_NAME(name, field, value)
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_NAME
#undef BUSTER_CBUILTIN_CPU_FEATURE
    };
    static const char* const names[] = {
#define BUSTER_CBUILTIN_CPU_FEATURE(name, word, bit)
#define BUSTER_CBUILTIN_CPU_NAME(name, field, value) name,
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_NAME
#undef BUSTER_CBUILTIN_CPU_FEATURE
    };
    enum { CAPACITY = 32768 };
    char* source = malloc(CAPACITY);
    uint32_t length = source != NULL ? (uint32_t)snprintf(source, CAPACITY,
        "int printf(const char*,...);struct M{unsigned vendor,type,subtype;unsigned features[1];}__cpu_model;unsigned __cpu_features2[3];\n"
        "static void clear(void){__cpu_model.vendor=__cpu_model.type=__cpu_model.subtype=__cpu_model.features[0]=0;"
        "__cpu_features2[0]=__cpu_features2[1]=__cpu_features2[2]=0;}\nstatic int feature(int i){switch(i){\n") : 0;
    for (uint32_t i = 0; source != NULL && i < BUSTER_ARRAY_LENGTH(features) && length < CAPACITY; i += 1)
    {
        length += (uint32_t)snprintf(source + length, CAPACITY - length, "case %u:return __builtin_cpu_supports(\"%s\");\n", i, features[i]);
    }
    length += source != NULL && length < CAPACITY ? (uint32_t)snprintf(source + length, CAPACITY - length, "}return -1;}\nstatic int name(int i){switch(i){\n") : 0;
    for (uint32_t i = 0; source != NULL && i < BUSTER_ARRAY_LENGTH(names) && length < CAPACITY; i += 1)
    {
        length += (uint32_t)snprintf(source + length, CAPACITY - length, "case %u:return __builtin_cpu_is(\"%s\");\n", i, names[i]);
    }
    length += source != NULL && length < CAPACITY ? (uint32_t)snprintf(source + length, CAPACITY - length,
        "}return -1;}\nint main(void){for(int i=0;i<%u;i++){int found=0;for(int w=0;w<4;w++)for(int b=0;b<32;b++){clear();"
        "if(w==0)__cpu_model.features[0]=1u<<b;else __cpu_features2[w-1]=1u<<b;if(feature(i)){printf(\"f%%d %%d %%d\\n\",i,w,b);found+=1;}}"
        "if(found!=1)printf(\"f%%d found %%d\\n\",i,found);}\nfor(int i=0;i<%u;i++){int found=0;for(int f=0;f<3;f++)for(unsigned v=0;v<64;v++){"
        "clear();if(f==0)__cpu_model.vendor=v;else if(f==1)__cpu_model.type=v;else __cpu_model.subtype=v;if(name(i)){printf(\"n%%d %%d %%u\\n\",i,f,v);found+=1;}}"
        "if(found!=1)printf(\"n%%d found %%d\\n\",i,found);}\nclear();printf(\"%%d %%d\\n\",__builtin_cpu_supports(\"not-a-feature\"),"
        "(int)sizeof(__builtin_cpu_is(\"intel\")));return 0;}\n", (uint32_t)BUSTER_ARRAY_LENGTH(features), (uint32_t)BUSTER_ARRAY_LENGTH(names)) : 0;
    check_test_expect(test, source != NULL && length < CAPACITY, "the cpu builtins' program fits");
    if (source != NULL && length < CAPACITY)
    {
        check_test_library_run(test, clang, executable, directory, "the cpu builtins of every name over libgcc's model", source, "-std=gnu17", NULL);
    }
    free(source);
}

// An option of this executable's alone, which check_test_library_run passes to its builds but not to clang's (`--tier1`,
// #1918), or NULL.
static const char* check_test_library_own_option = NULL;

// One program built by clang and by this executable, by both paths, which must exit alike and print the same: `source`
// in `directory`, with `standard` and `option` (NULL for none), and this executable's builds with
// check_test_library_own_option.
static void check_test_library_run(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* name,
                                   const char* program_source, const char* standard, const char* option)
{
    char source[512];
    char built[3][512];
    snprintf(source, sizeof(source), "%s/library-run.c", directory);
    for (uint32_t b = 0; b < 3; b += 1)
    {
        snprintf(built[b], sizeof(built[b]), "%s/library-run-%u", directory, b);
    }
    const bool written = parse_test_write(source, program_source, strlen(program_source));
    int statuses[3] = { -1, -1, -1 };
    char* outputs[3] = { NULL, NULL, NULL };
    for (uint32_t b = 0; written && b < 3; b += 1)
    {
        char* arguments[12] = { (char*)(b == 0 ? clang : executable), (char*)standard, "-w", source, "-o", built[b], "-lm" };
        uint32_t count = 7;
        if (option != NULL)
        {
            arguments[count++] = (char*)option;
        }
        if (b != 0 && check_test_library_own_option != NULL)
        {
            arguments[count++] = (char*)check_test_library_own_option;
        }
        if (b == 2)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count] = NULL;
        char* diagnostics = NULL;
        unlink(built[b]);
        const int compiled = check_test_driver_spawn(arguments, &diagnostics);
        free(diagnostics);
        if (compiled == 0)
        {
            // The status of a run, then, for a program that exits (not __builtin_abort's, whose core the runner turns
            // off), what a second run prints.
            char* run[] = { built[b], NULL };
            statuses[b] = check_test_fortify_execute(built[b]);
            if (statuses[b] >= 0 && statuses[b] < 128)
            {
                check_test_driver_spawn_stream(run, &outputs[b], true);
            }
            else
            {
                outputs[b] = strdup("");
            }
        }
    }
    for (uint32_t b = 1; written && b < 3; b += 1)
    {
        char description[384];
        snprintf(description, sizeof(description), "%s, %s: exit %d and output \"%s\" (clang %d, \"%s\")", name, b == 2 ? "scalar" : "vector",
                 statuses[b], outputs[b] != NULL ? outputs[b] : "", statuses[0], outputs[0] != NULL ? outputs[0] : "");
        check_test_expect(test, statuses[0] >= 0 && statuses[b] == statuses[0] && outputs[0] != NULL && outputs[b] != NULL &&
                                strcmp(outputs[0], outputs[b]) == 0, description);
    }
    check_test_expect(test, written, "library programs: source written");
    for (uint32_t b = 0; b < 3; b += 1)
    {
        free(outputs[b]);
        unlink(built[b]);
    }
    unlink(source);
}

// C23's <stdbit.h> functions (cbuiltin_stdc_library): each of the 70 declared, by a macro over their operations and
// types, called on boundary values converted to its parameter's type, and the unit's own definition of one, which clang's
// calls of the name don't reach (they compute inline) where its address does; then folds in an enumerator and a static
// initializer, a double's and a negative argument's conversions, and an argument whose effect runs once (counted in a full
// expression of its own, as printf's arguments are in no order).
#define BUSTER_CHECK_TEST_STDC_PROGRAM \
    "int printf(const char*,...);\n" \
    "#define U(t) unsigned int\n#define B(t) _Bool\n#define S(t) t\n" \
    "#define EACH(X,op,R) X(op,R(unsigned char),unsigned char,uc) X(op,R(unsigned short),unsigned short,us) X(op,R(unsigned int),unsigned int,ui) " \
    "X(op,R(unsigned long),unsigned long,ul) X(op,R(unsigned long long),unsigned long long,ull)\n" \
    "#define OPS(X) EACH(X,leading_zeros,U) EACH(X,leading_ones,U) EACH(X,trailing_zeros,U) EACH(X,trailing_ones,U) EACH(X,first_leading_zero,U) " \
    "EACH(X,first_leading_one,U) EACH(X,first_trailing_zero,U) EACH(X,first_trailing_one,U) EACH(X,count_zeros,U) EACH(X,count_ones,U) " \
    "EACH(X,has_single_bit,B) EACH(X,bit_width,U) EACH(X,bit_floor,S) EACH(X,bit_ceil,S)\n" \
    "#define DECLARE(op,r,t,s) r stdc_##op##_##s(t);\nOPS(DECLARE)\n" \
    "unsigned int stdc_leading_zeros_ui(unsigned int x){(void)x;return 99;}\n" \
    "static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}\n" \
    "static int calls;static unsigned next(unsigned v){calls++;return v;}\n" \
    "enum{E=stdc_bit_ceil_ul(1000)+stdc_count_ones_ull(-1)+(stdc_count_ones_ui)(7)*10000+((stdc_bit_width_ul))(5)*100000};\n" \
    "static unsigned s=stdc_first_trailing_one_us(8)+(stdc_count_ones_us)(7)*100;\n" \
    "#define CALL(op,r,t,s) mix(stdc_##op##_##s(v));\n" \
    "int main(void){static const unsigned long long values[]={0,1,2,3,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x12345678,0x7fffffff,0x80000000,0xffffffff," \
    "0x100000000ull,0x123456789abcdef0ull,0x7fffffffffffffffull,0x8000000000000000ull,0xffffffffffffffffull,0x5555555555555555ull,37};\n" \
    "for(unsigned i=0;i<sizeof values/sizeof values[0];i++){unsigned long long v=values[i];OPS(CALL)}\n" \
    "unsigned (*p)(unsigned)=stdc_leading_zeros_ui;volatile double d=3.9;volatile int m=-1;\n" \
    "unsigned effects=stdc_count_ones_ui(next(7))+stdc_bit_floor_us(next(300))+(stdc_count_ones_ui)(next(15))*1000;int seen=calls;\n" \
    "unsigned (*q)(unsigned)=(stdc_leading_zeros_ui);unsigned through=(stdc_leading_zeros_ui)(1)+((stdc_leading_zeros_ui))(2)*1000+q(1)*1000000+(*q)(1);\n" \
    "printf(\"%llx %u %u %u %d %u %u %u %d %u\\n\",h,stdc_leading_zeros_ui(1),p(1),stdc_bit_width_ui(d),E,s,stdc_count_ones_uc(m)," \
    "effects,seen,through);return 0;}\n"

// One plain declaration of a <stdbit.h> function and the unit's definition of it, whose call is the builtin's (31) in
// C23 and the definition's (99) where the options or the mode make the name no builtin, the name in parentheses too
// (#1242's review), and a pointer to it the definition's always.
#define BUSTER_CHECK_TEST_STDC_DEFINED \
    "int printf(const char*,...);\nunsigned int stdc_leading_zeros_ui(unsigned int x){(void)x;return 99;}\n" \
    "int main(void){volatile unsigned v=1;unsigned (*p)(unsigned)=(stdc_leading_zeros_ui);\n" \
    "printf(\"%u %u %u %u %u\\n\",stdc_leading_zeros_ui(1),stdc_leading_zeros_ui(v),(stdc_leading_zeros_ui)(v),((stdc_leading_zeros_ui))(1),p(v));return 0;}\n"

// fmax and fmin of zeros of both signs and of a signaling NaN beside a number (#967), whose results tell clang's inline
// llvm.maxnum and minnum (the first zero, the number) from glibc's functions (the second zero, a quiet NaN), through a
// plain declaration of fmax that `label` follows (an asm label, or nothing).
#define BUSTER_CHECK_TEST_EXTREMES_PROGRAM(label) \
    "int printf(const char*,...);double fmax(double,double)" label ";double fmin(double,double);\n" \
    "static unsigned long long bits(double d){union{double d;unsigned long long u;}x={d};return x.u;}\n" \
    "int main(void){volatile double z=0.0,n=-0.0;union{unsigned long long u;double d;}s={0x7ff0000000000001ull};volatile double v=s.d;\n" \
    "printf(\"%llx %llx %llx %llx\\n\",bits(fmax(n,z)),bits(fmin(z,n)),bits(fmax(v,1.0)),bits(fmin(1.0,v)));return 0;}\n"

// The unit's own definitions of memcpy, memmove, abs, labs, fabs and copysign, which count their calls (#1373): clang's
// code computes a call of a declaration that is the builtin in its place, memcpy's and memmove's of a constant size of at
// most 32 bytes, so that the counts tell computed calls from called ones; memcpy of 33 bytes and of a size that is no
// constant, and memmove of one, call the definitions. memset stays out: clang -O0's code calls it whatever its size, where
// this compiler stores a small one's bytes.
#define BUSTER_CHECK_TEST_COMPUTED_PROGRAM \
    "int printf(const char*,...);typedef unsigned long size_t;static int calls;\n" \
    "void*memcpy(void*d,const void*s,size_t n){calls+=1;char*a=d;const char*b=s;while(n--)*a++=*b++;return d;}\n" \
    "void*memmove(void*d,const void*s,size_t n){calls+=10;char*a=d;const char*b=s;if(a<b)while(n--)*a++=*b++;else while(n--)a[n]=b[n];return d;}\n" \
    "int abs(int x){calls+=100;return x<0?-x:x;}long labs(long x){calls+=1000;return x<0?-x:x;}\n" \
    "double fabs(double x){calls+=10000;return x<0?-x:x;}double copysign(double x,double y){calls+=100000;return (y<0)!=(x<0)?-x:x;}\n" \
    "int main(void){char a[48],b[48];for(int i=0;i<48;i++){a[i]=(char)('a'+i%26);b[i]='.';}volatile size_t n=4;volatile int v=-3;volatile double d=-2.5;\n" \
    "memcpy(b,a,8);memcpy(b+8,a,n);memcpy(b+12,a+5,33);memmove(b+1,b,12);memmove(b+30,b+29,n);memmove(b+3,b+4,32);\n" \
    "int r=abs(v)+(int)labs(v)*10;double f=fabs(d)+copysign(1.0,d);printf(\"%d %d %.1f %.45s\\n\",calls,r,f,b);return 0;}\n"

static void check_test_library_runs(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        const char* standard;
        const char* option;
    } programs[] = {
        {
            "C89's implicit library declarations",
            "int main(void){char b[64];double d=sin(0.5)+sqrt(2.0);long n=labs(-7);unsigned long l=strlen(\"hello\");\n"
            "sprintf(b,\"%.6f %ld %lu %d\",d,n,l,abs(-3));puts(b);return (int)l+(int)fmax(1.0,2.0)*10;}\n",
            "-std=gnu89",
            NULL,
        },
        {
            "C99's implicit library declarations, made warnings",
            "int main(void){char b[64];double d=cos(0.25)*pow(2.0,10.0);snprintf(b,sizeof(b),\"%.3f\",d);puts(b);return (int)strlen(b);}\n",
            "-std=gnu17",
            "-Wno-error=implicit-function-declaration",
        },
        {
            // strlcat and strlcpy are GNU library builtins in upstream's table and plain C89 implicit calls in Debian's:
            // both compile a call of the named function, which the run's result checks. Local definitions keep the run
            // off the host's C library, which only has the names since glibc 2.38.
            "strlcat and strlcpy calls reach their functions",
            "unsigned long strlcat(char*d,const char*s,unsigned long n){unsigned long a=0;while(a<n&&d[a])a++;unsigned long i=0;while(s[i]&&a+i+1<n){d[a+i]=s[i];i++;}if(a<n)d[a+i]=0;while(s[i])i++;return a+i;}\n"
            "unsigned long strlcpy(char*d,const char*s,unsigned long n){unsigned long i=0;while(s[i]&&i+1<n){d[i]=s[i];i++;}if(n)d[i]=0;while(s[i])i++;return i;}\n"
            "int main(void){char b[8]=\"ab\";unsigned long n=strlcat(b,\"cde\",sizeof b);char c[8];unsigned long m=strlcpy(c,\"hello\",sizeof c);return (int)(n+m);}\n",
            "-std=gnu89",
            NULL,
        },
        {
            "a plain declaration's strcmp of constants folds",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "-fno-builtin keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "-fno-builtin-strcmp keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin-strcmp",
        },
        {
            "-ffreestanding keeps a plain declaration's strcmp a call",
            "int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-ffreestanding",
        },
        {
            "a plain declaration beside overloads folds",
            "int ov(int) __attribute__((overloadable));int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "-fno-builtin keeps a plain declaration beside overloads a call",
            "int ov(int) __attribute__((overloadable));int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "-fno-builtin-strcmp keeps a plain declaration beside overloads a call",
            "int ov(int) __attribute__((overloadable));int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin-strcmp",
        },
        {
            "-ffreestanding keeps a plain declaration beside overloads a call",
            "int ov(int) __attribute__((overloadable));int strcmp(const char*,const char*);int main(void){return strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-ffreestanding",
        },
        {
            "a plain declaration's fmax and fmin compute inline (#967)",
            BUSTER_CHECK_TEST_EXTREMES_PROGRAM(""),
            "-std=gnu17",
            NULL,
        },
        {
            "-fno-builtin keeps a plain declaration's fmax and fmin calls",
            BUSTER_CHECK_TEST_EXTREMES_PROGRAM(""),
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "-fno-builtin-fmax keeps a plain declaration's fmax a call, its fmin inline",
            BUSTER_CHECK_TEST_EXTREMES_PROGRAM(""),
            "-std=gnu17",
            "-fno-builtin-fmax",
        },
        {
            "an asm label keeps fmax a call of the label's function (glibc's fmin)",
            BUSTER_CHECK_TEST_EXTREMES_PROGRAM(" __asm__(\"fmin\")"),
            "-std=gnu17",
            NULL,
        },
        {
            "no_builtin(\"fmax\") keeps the function's fmax a call, its fmin inline (#1958)",
            "int printf(const char*,...);double fmax(double,double);double fmin(double,double);\n"
            "__attribute__((no_builtin(\"fmax\"))) int main(void){volatile double z=0.0,n=-0.0;"
            "printf(\"%d %d\\n\",__builtin_signbit(fmax(n,z))!=0,__builtin_signbit(fmin(z,n))!=0);return 0;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "a plain declaration's memcpy, memmove, abs, labs, fabs and copysign compute inline (#1373)",
            BUSTER_CHECK_TEST_COMPUTED_PROGRAM,
            "-std=gnu17",
            NULL,
        },
        {
            "-fno-builtin keeps a plain declaration's memcpy, memmove, abs and fabs calls",
            BUSTER_CHECK_TEST_COMPUTED_PROGRAM,
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "-fno-builtin-memcpy keeps a plain declaration's memcpy a call, its memmove and abs inline",
            BUSTER_CHECK_TEST_COMPUTED_PROGRAM,
            "-std=gnu17",
            "-fno-builtin-memcpy",
        },
        {
            "-fno-builtin-labs keeps a plain declaration's labs a call, its abs inline",
            BUSTER_CHECK_TEST_COMPUTED_PROGRAM,
            "-std=gnu17",
            "-fno-builtin-labs",
        },
        {
            "-ffreestanding keeps a plain declaration's memcpy, memmove, abs and fabs calls",
            BUSTER_CHECK_TEST_COMPUTED_PROGRAM,
            "-std=gnu17",
            "-ffreestanding",
        },
        {
            "no_builtin keeps every plain library builtin's call a call, but its __builtin_ form's",
            "int printf(const char*,...);int strcmp(const char*,const char*);unsigned long strlen(const char*);double fmin(double,double);\n"
            "static int other(void){return strcmp(\"a\",\"c\");}\n"
            "__attribute__((no_builtin)) int main(void){volatile double z=0.0,n=-0.0;"
            "printf(\"%d %d %d %d %d\\n\",strcmp(\"a\",\"c\"),__builtin_strcmp(\"a\",\"c\"),(int)strlen(\"abc\"),__builtin_signbit(fmin(z,n))!=0,other());return 0;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "no_builtin of another name or \"*\" leaves strcmp a fold",
            "int strcmp(const char*,const char*);\n"
            "__attribute__((no_builtin(\"strlen\",\"memcpy\"))) static int f(void){return strcmp(\"a\",\"c\");}\n"
            "__attribute__((no_builtin(\"*\"))) int main(void){return strcmp(\"a\",\"c\")+f()+4;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "no_builtin keeps a <stdbit.h> function's call the definition's",
            "int printf(const char*,...);\nunsigned int stdc_leading_zeros_ui(unsigned int x){(void)x;return 99;}\n"
            "__attribute__((no_builtin(\"stdc_leading_zeros_ui\"))) int main(void){volatile unsigned v=1;"
            "printf(\"%u %u\\n\",stdc_leading_zeros_ui(v),__builtin_stdc_leading_zeros(v));return 0;}\n",
            "-std=gnu23",
            NULL,
        },
        {
            "__builtin_strcmp folds whatever the options",
            "int main(void){return __builtin_strcmp(\"a\",\"c\")+2;}\n",
            "-std=gnu17",
            "-fno-builtin",
        },
        {
            "__builtin_abort",
            "int main(int argc,char**argv){if(argc>0)__builtin_abort();return 3;}\n",
            "-std=gnu17",
            NULL,
        },
        {
            "C23's <stdbit.h> functions compute inline",
            BUSTER_CHECK_TEST_STDC_PROGRAM,
            "-std=gnu23",
            NULL,
        },
        {
            "a <stdbit.h> function's definition is no builtin's call in C23",
            BUSTER_CHECK_TEST_STDC_DEFINED,
            "-std=gnu23",
            NULL,
        },
        {
            "-fno-builtin keeps a <stdbit.h> function's call the definition's",
            BUSTER_CHECK_TEST_STDC_DEFINED,
            "-std=gnu23",
            "-fno-builtin",
        },
        {
            "-fno-builtin-NAME keeps a <stdbit.h> function's call the definition's",
            BUSTER_CHECK_TEST_STDC_DEFINED,
            "-std=gnu23",
            "-fno-builtin-stdc_leading_zeros_ui",
        },
        {
            "C17 keeps a <stdbit.h> function's call the definition's",
            BUSTER_CHECK_TEST_STDC_DEFINED,
            "-std=gnu17",
            NULL,
        },
    };
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(programs); p += 1)
    {
        check_test_library_run(test, clang, executable, directory, programs[p].name, programs[p].source, programs[p].standard, programs[p].option);
    }
}

// The non-reference build path under a fake clang (#1131): a wrapper script reports an Ubuntu build and answers its
// library-builtin questions as Debian's patched table does (no strlcat or strlcpy, which -fno-builtin-NAME options
// give), so the implicit comparisons under it must pass while leaving out only those names — every other name's
// diagnostics still compare. It reaches the configured clang through a path with a space and the shell's
// metacharacters (clang_release_wrapper), as a --clang path may have. CI's clang is the reference, whose own run skips
// nothing and can't reach this path otherwise.
static void check_test_library_release(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char wrapper[512];
    char trampoline[1024];
    char folder[768];
    const bool written = clang_release_wrapper(directory, clang, "Ubuntu clang version 23.1.6 (fake)", "-fno-builtin-strlcat -fno-builtin-strlcpy", wrapper,
                                               sizeof(wrapper), trampoline, sizeof(trampoline), folder, sizeof(folder));
    check_test_expect(test, written, "library release: the fake clang is written");
    char version[64] = "";
    // The wrapper isn't the reference: the check must say so with both builds, which enables the build's known
    // differences below.
    const bool detected = written && !clang_reference_report(wrapper, version, sizeof(version)) && strcmp(version, "Ubuntu 23.1.6") == 0;
    check_test_expect(test, detected, "library release: the fake clang reports Ubuntu 23.1.6 to the reference check");
    const bool reference = check_driver_clang_reference;
    char saved_version[sizeof(check_driver_clang_version)];
    memcpy(saved_version, check_driver_clang_version, sizeof(saved_version));
    check_driver_clang_reference = false;
    snprintf(check_driver_clang_version, sizeof(check_driver_clang_version), "%s", version);
    if (detected)
    {
        check_test_library_implicit(test, wrapper, executable, directory);
    }
    check_driver_clang_reference = reference;
    memcpy(check_driver_clang_version, saved_version, sizeof(check_driver_clang_version));
    unlink(wrapper);
    unlink(trampoline);
    rmdir(folder);
}

static void check_test_driver_library(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_library_implicit(test, clang, executable, directory);
    check_test_library_lowering(test, clang, executable, directory);
    check_test_library_runs(test, clang, executable, directory);
    check_test_cpu_model_runs(test, clang, executable, directory);
    check_test_library_release(test, clang, executable, directory);
}
