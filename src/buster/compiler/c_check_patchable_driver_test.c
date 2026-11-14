// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// patchable_function_entry's code (#1447), by the driver against clang, on an original program: each patched function's
// NOP sled (its first M bytes one-byte NOPs before its symbol, the rest the longest NOPs at its entry), its entry in
// __patchable_function_entries (or the section its attribute names) and a redeclaration's merge. -c's object must have
// clang's entries: a section per function, writable, allocated and linked to the function's section (SHF_LINK_ORDER),
// 8-byte aligned, whose R_X86_64_64 points to the sled's start (check_test_patchable_describe); its program, linked by
// clang, must print what clang's does: each sled's bytes, how many entries of each section point to it (through the
// linker's __start_ and __stop_), and the depth glibc's backtrace finds from two patched functions, which their unwind
// tables (FDEs from the symbols, the sleds' NOPs under the CIE's rules) give, through a helper that, as the static patched
// one, is noinline, so that --tier1's inlining (#2541) leaves each its frame; and so must the direct path's executable,
// by both checker paths. -fpatchable-function-entry=N,M[,SECTION]'s default the same way, an attribute's 0 turning it off,
// and the option's errors, clang's.

// Orders the lines of check_test_patchable_describe.
static int check_test_patchable_order(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

// The object at `path`'s patchable function entries, as lines in `text` (`capacity` bytes), one per relocation of each
// section named `section` or `custom`, sorted (clang emits a static function where its unit first uses it, the checker
// where its definition is): the function it points into (the function symbol at or after the address, within 15 bytes),
// the section's name, flags, alignment and size and the section its sh_link names, the relocation's type, how far before
// the function's symbol it points and the bytes from there to the symbol. Whether it read.
static bool check_test_patchable_describe(const char* path, const char* section, const char* custom, char* text, size_t capacity)
{
    enum { LINES = 64, LINE = 256 };
    Arena arena = arena_reserve(UINT64_C(1) << 30);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    char lines[LINES][LINE];
    char* sorted[LINES];
    uint32_t count = 0;
    for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* header = &elf.sections[s];
        const char* name = check_test_elf_section_name(&elf, s);
        const bool entries = name != 0 && (strcmp(name, section) == 0 || strcmp(name, custom) == 0);
        const char* linked = entries ? check_test_elf_section_name(&elf, header->sh_link) : 0;
        for (uint32_t r = 1; entries && valid && r < elf.section_count; r += 1)
        {
            const Elf64_Shdr* relocations = &elf.sections[r];
            const bool owned = relocations->sh_type == SHT_RELA && relocations->sh_info == s &&
                               check_test_elf_range(elf.file, relocations->sh_offset, relocations->sh_size);
            const uint64_t relocation_count = owned ? relocations->sh_size / sizeof(Elf64_Rela) : 0;
            for (uint64_t k = 0; valid && k < relocation_count; k += 1)
            {
                const Elf64_Rela* relocation = (const Elf64_Rela*)(elf.file.pointer + relocations->sh_offset) + k;
                const uint64_t index = ELF64_R_SYM(relocation->r_info);
                valid = index < elf.symbol_count && count < LINES;
                const Elf64_Sym* target = valid ? &elf.symbols[index] : 0;
                // The address in the target's section: a section symbol's offset, or a symbol's value, plus the addend.
                const uint32_t place = valid ? target->st_shndx : 0;
                const uint64_t address = valid ? (ELF64_ST_TYPE(target->st_info) == STT_SECTION ? 0 : target->st_value) + (uint64_t)relocation->r_addend : 0;
                const char* function = 0;
                uint64_t before = 16;
                for (uint64_t f = 1; valid && f < elf.symbol_count; f += 1)
                {
                    const Elf64_Sym* symbol = &elf.symbols[f];
                    const bool candidate = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx == place && symbol->st_value >= address &&
                                           symbol->st_value - address < before;
                    function = candidate ? check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name) : function;
                    before = candidate ? symbol->st_value - address : before;
                }
                const Elf64_Shdr* code = place < elf.section_count ? &elf.sections[place] : 0;
                valid = valid && function != 0 && code != 0 && check_test_elf_range(elf.file, code->sh_offset + address, before);
                int length = valid ? snprintf(lines[count], LINE, "%s %s flags=%#llx align=%llu size=%llu link=%s type=%u before=%llu prefix=", function, name,
                                              (unsigned long long)header->sh_flags, (unsigned long long)header->sh_addralign, (unsigned long long)header->sh_size,
                                              linked != 0 ? linked : "?", (unsigned)ELF64_R_TYPE(relocation->r_info), (unsigned long long)before) : 0;
                for (uint64_t b = 0; valid && b < before && length > 0 && length < LINE; b += 1)
                {
                    length += snprintf(lines[count] + length, (size_t)(LINE - length), "%02x", (uint8_t)elf.file.pointer[code->sh_offset + address + b]);
                }
                valid = valid && length > 0 && length < LINE;
                sorted[count] = lines[count];
                count += valid ? 1 : 0;
            }
        }
    }
    qsort(sorted, count, sizeof(char*), check_test_patchable_order);
    size_t written = 0;
    text[0] = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const int length = snprintf(text + written, capacity - written, "%s\n", sorted[i]);
        valid = length > 0 && (size_t)length < capacity - written;
        written += valid ? (size_t)length : 0;
    }
    arena_release(&arena);
    return valid;
}

// The fixture's program: each function's sled and entries, and the backtraces' depths, as its output.
static const char check_test_patchable_program[] =
    "#include <execinfo.h>\n"
    "#include <stdio.h>\n"
    "extern char __start___patchable_function_entries[] __attribute__((weak));\n"
    "extern char __stop___patchable_function_entries[] __attribute__((weak));\n"
    "extern char __start_custom_entries[] __attribute__((weak));\n"
    "extern char __stop_custom_entries[] __attribute__((weak));\n"
    "__attribute__((noinline)) static int depth(void) { void *frames[64]; return backtrace(frames, 64); }\n"
    "__attribute__((patchable_function_entry(3))) int f(int x) { return x + 1; }\n"
    "__attribute__((patchable_function_entry(5, 2))) int g(int x) { return depth() + x * 2; }\n"
    "__attribute__((patchable_function_entry(0))) int h(int x) { return x - 1; }\n"
    "__attribute__((patchable_function_entry(15, 15))) int k(int x) { return x * 3; }\n"
    "__attribute__((patchable_function_entry(20, 4), noinline)) static int s(int x) { return depth() + x; }\n"
    "__attribute__((patchable_function_entry(2, 1, \"custom_entries\"))) int t(int x) { return x ^ 5; }\n"
    "__attribute__((patchable_function_entry(3))) int u(int x);\n"
    "__attribute__((patchable_function_entry(4))) int u(int x);\n"
    "int u(int x) { return x << 1; }\n"
    "int w(int x) __attribute__((patchable_function_entry(31, 7)));\n"
    "int w(int x) { return x + 7; }\n"
    "int plain(int x) { return x; }\n"
    "static int entries(char *first, char *last, unsigned char *sled)\n"
    "{\n"
    "    int found = 0;\n"
    "    for (unsigned char **entry = (unsigned char **)first; entry < (unsigned char **)last; entry += 1) { found += *entry == sled; }\n"
    "    return found;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    struct { const char *name; unsigned char *address; unsigned count; unsigned offset; } functions[] = {\n"
    "        { \"f\", (unsigned char *)f, 3, 0 }, { \"g\", (unsigned char *)g, 5, 2 }, { \"h\", (unsigned char *)h, 0, 0 },\n"
    "        { \"k\", (unsigned char *)k, 15, 15 }, { \"s\", (unsigned char *)s, 20, 4 }, { \"t\", (unsigned char *)t, 2, 1 },\n"
    "        { \"u\", (unsigned char *)u, 4, 0 }, { \"w\", (unsigned char *)w, 31, 7 }, { \"plain\", (unsigned char *)plain, 0, 0 },\n"
    "    };\n"
    "    for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); i += 1)\n"
    "    {\n"
    "        unsigned char *sled = functions[i].address - functions[i].offset;\n"
    "        printf(\"%s\", functions[i].name);\n"
    "        for (unsigned b = 0; b < functions[i].count; b += 1) { printf(\" %02x\", sled[b]); }\n"
    "        printf(\" entries=%d custom=%d\\n\", entries(__start___patchable_function_entries, __stop___patchable_function_entries, sled),\n"
    "               entries(__start_custom_entries, __stop_custom_entries, sled));\n"
    "    }\n"
    "    printf(\"%d %d\\n\", g(1) - 2, s(0));\n"
    "    return f(1) + h(3) + k(4) + t(6) + u(7) + w(1) + plain(0) - 2 - 2 - 12 - 3 - 14 - 8;\n"
    "}\n";

// A unit compiled with -fpatchable-function-entry's default (N and M, the option's, defined first): each function without
// the attribute gets the option's NOPs, one with patchable_function_entry(0) none, and one with its own its own.
static const char check_test_patchable_flagged[] =
    "#include <stdio.h>\n"
    "extern char __start_flagged_entries[] __attribute__((weak));\n"
    "extern char __stop_flagged_entries[] __attribute__((weak));\n"
    "extern char __start___patchable_function_entries[] __attribute__((weak));\n"
    "extern char __stop___patchable_function_entries[] __attribute__((weak));\n"
    "int a(int x) { return x + 1; }\n"
    "__attribute__((patchable_function_entry(0))) int b(int x) { return x + 2; }\n"
    "__attribute__((patchable_function_entry(6, 3))) int c(int x) { return x + 3; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned char *places[] = { (unsigned char *)a - M, (unsigned char *)b, (unsigned char *)c - 3, (unsigned char *)main - M };\n"
    "    const unsigned counts[] = { N, 0, 6, N };\n"
    "    for (unsigned i = 0; i < 4; i += 1)\n"
    "    {\n"
    "        int flagged = 0;\n"
    "        int plain = 0;\n"
    "        for (unsigned char **e = (unsigned char **)__start_flagged_entries; e < (unsigned char **)__stop_flagged_entries; e += 1) { flagged += *e == places[i]; }\n"
    "        for (unsigned char **e = (unsigned char **)__start___patchable_function_entries; e < (unsigned char **)__stop___patchable_function_entries; e += 1)\n"
    "        {\n"
    "            plain += *e == places[i];\n"
    "        }\n"
    "        printf(\"%u\", i);\n"
    "        for (unsigned k = 0; k < counts[i]; k += 1) { printf(\" %02x\", places[i][k]); }\n"
    "        printf(\" flagged=%d plain=%d\\n\", flagged, plain);\n"
    "    }\n"
    "    return a(1) + b(1) + c(1) - 9;\n"
    "}\n";

// One fixture's source compiled by clang and by both checker paths with `option` (or none), by Tier 0 and by --tier1's:
// each object's entries (check_test_patchable_describe) clang's, each object linked by clang and the direct path's
// executables running as clang's program does.
static void check_test_patchable_compare(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* name, const char* program,
                                         const char* option, const char* custom)
{
    enum { ROUTES = 6 };
    char source[512];
    snprintf(source, sizeof(source), "%s/patchable-%s.c", directory, name);
    const bool written = parse_test_write(source, program, strlen(program));
    char descriptions[ROUTES][4096];
    char outputs[ROUTES][1024];
    for (uint32_t route = 0; written && route < ROUTES; route += 1)
    {
        // Routes: clang's -c; -c by the vector and the scalar path, and by --tier1; the direct path's executables by the
        // vector and the scalar path.
        const bool object = route < 4;
        const bool scalar = route == 2 || route == 5;
        char built[512];
        char linked[512];
        snprintf(built, sizeof(built), "%s/patchable-%s-%u%s", directory, name, route, object ? ".o" : "");
        snprintf(linked, sizeof(linked), "%s/patchable-%s-%u-run", directory, name, route);
        char* arguments[12] = { (char*)(route == 0 ? clang : executable), "-std=gnu17", "-O0", object ? "-c" : "-w", source, "-o", built };
        uint32_t count = 7;
        arguments[count] = (char*)option;
        count += option != NULL ? 1 : 0;
        arguments[count] = scalar ? "--scalar" : route == 3 ? "--tier1" : NULL;
        count += arguments[count] != NULL ? 1 : 0;
        arguments[count] = NULL;
        char* text = NULL;
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        char description[1536];
        snprintf(description, sizeof(description), "patchable %s, route %u: compiles (%s)", name, route, text != NULL ? text : "");
        free(text);
        text = NULL;
        check_test_expect(test, compiled, description);
        const bool described = compiled && object && check_test_patchable_describe(built, "__patchable_function_entries", custom, descriptions[route], sizeof(descriptions[route]));
        check_test_expect(test, !compiled || !object || described, "patchable entries: the object's sections read");
        char* link[] = { (char*)clang, built, "-o", linked, NULL };
        const bool runs = compiled && (!object || check_test_driver_spawn(link, &text) == 0);
        free(text);
        outputs[route][0] = 0;
        const bool ran = runs && check_test_driver_capture(object ? linked : built, outputs[route], sizeof(outputs[route]));
        snprintf(description, sizeof(description), "patchable %s, route %u: links and runs", name, route);
        check_test_expect(test, ran, description);
        if (route != 0 && object && described)
        {
            snprintf(description, sizeof(description), "patchable %s, route %u: the object's entries are clang's:\n%s(clang's\n%s)", name, route, descriptions[route],
                     descriptions[0]);
            check_test_expect(test, strcmp(descriptions[route], descriptions[0]) == 0 && descriptions[0][0] != 0, description);
        }
        if (route != 0 && ran)
        {
            snprintf(description, sizeof(description), "patchable %s, route %u: runs as clang's program:\n%s(clang's\n%s)", name, route, outputs[route], outputs[0]);
            check_test_expect(test, strcmp(outputs[route], outputs[0]) == 0, description);
        }
        unlink(built);
        unlink(linked);
    }
    unlink(source);
}

// The "error:" lines of a driver's output, `prefix` (clang's driver's name, "clang: ") taken off each, into `lines`.
static void check_test_patchable_errors(const char* text, const char* prefix, char* lines, size_t capacity)
{
    size_t length = 0;
    const size_t skip = strlen(prefix);
    lines[0] = 0;
    for (const char* line = text; line != NULL && *line != 0 && length < capacity;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) + 1 : strlen(line);
        const size_t taken = size >= skip && strncmp(line, prefix, skip) == 0 ? skip : 0;
        const bool error = size - taken >= 6 && strncmp(line + taken, "error:", 6) == 0;
        length += error ? (size_t)snprintf(lines + length, capacity - length, "%.*s", (int)(size - taken), line + taken) : 0;
        line += size;
    }
}

// -fpatchable-function-entry='s errors, the last one's, against clang's driver: its error lines, which the driver follows
// with its usage, as after any option's error.
static void check_test_patchable_options(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const values[][2] = {
        { "-fpatchable-function-entry=x", NULL },
        { "-fpatchable-function-entry=", NULL },
        { "-fpatchable-function-entry=-1", NULL },
        { "-fpatchable-function-entry=+3", NULL },
        { "-fpatchable-function-entry=4294967296", NULL },
        { "-fpatchable-function-entry=3,", NULL },
        { "-fpatchable-function-entry=3,1,", NULL },
        { "-fpatchable-function-entry=3,,a", NULL },
        { "-fpatchable-function-entry=3,x", NULL },
        { "-fpatchable-function-entry=2,3", NULL },
        { "-fpatchable-function-entry=0,1", NULL },
        { "-fpatchable-function-entry=x", "-fpatchable-function-entry=2,1" },
        { "-fpatchable-function-entry=2,1", "-fpatchable-function-entry=x" },
        { "-fpatchable-function-entry=x", "-fpatchable-function-entry=2,3" },
        { "-fpatchable-function-entry=03,1,a,b", NULL },
        { "-fpatchable-function-entry=4294967295,4294967295", NULL },
    };
    char source[512];
    snprintf(source, sizeof(source), "%s/patchable-options.c", directory);
    static const char program[] = "int f(void) { return 0; }\n";
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    // clang's driver names itself by its program's name, argv[0]'s last component (`clang-23: ` for /usr/bin/clang-23,
    // #1790), so the oracle's prefix is that of the clang it runs.
    const char* slash = strrchr(clang, '/');
    char prefix[256];
    snprintf(prefix, sizeof(prefix), "%s: ", slash != NULL ? slash + 1 : clang);
    for (uint32_t v = 0; written && v < BUSTER_ARRAY_LENGTH(values); v += 1)
    {
        char* theirs = NULL;
        char* ours = NULL;
        char* clang_arguments[] = { (char*)clang, "-fsyntax-only", (char*)values[v][0], (char*)values[v][1], NULL, NULL };
        char* arguments[] = { (char*)executable, "-fsyntax-only", (char*)values[v][0], (char*)values[v][1], NULL, NULL };
        clang_arguments[values[v][1] != NULL ? 4 : 3] = source;
        arguments[values[v][1] != NULL ? 4 : 3] = source;
        const int clang_status = check_test_driver_spawn(clang_arguments, &theirs);
        const int status = check_test_driver_spawn(arguments, &ours);
        char expected[1024];
        char actual[1024];
        check_test_patchable_errors(theirs, prefix, expected, sizeof(expected));
        check_test_patchable_errors(ours, "", actual, sizeof(actual));
        char description[2048];
        snprintf(description, sizeof(description), "%s %s: the driver's status %d and errors\n%s(clang's %d,\n%s)", values[v][0], values[v][1] != NULL ? values[v][1] : "",
                 status, actual, clang_status, expected);
        check_test_expect(test, ours != NULL && theirs != NULL && (status != 0) == (clang_status != 0) && strcmp(actual, expected) == 0, description);
        free(theirs);
        free(ours);
    }
    unlink(source);
}

static void check_test_driver_patchable(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_patchable_compare(test, clang, executable, directory, "attributes", check_test_patchable_program, NULL, "custom_entries");
    char flagged[4096];
    snprintf(flagged, sizeof(flagged), "#define N 2\n#define M 1\n%s", check_test_patchable_flagged);
    check_test_patchable_compare(test, clang, executable, directory, "flagged", flagged, "-fpatchable-function-entry=2,1,flagged_entries", "flagged_entries");
    snprintf(flagged, sizeof(flagged), "#define N 17\n#define M 0\n%s", check_test_patchable_flagged);
    check_test_patchable_compare(test, clang, executable, directory, "option", flagged, "-fpatchable-function-entry=17", "flagged_entries");
    check_test_patchable_options(test, clang, executable, directory);
}
