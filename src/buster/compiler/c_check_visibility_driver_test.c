// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original pragma fixtures and an independent ELF st_other reader, checked against Clang's objects; and -fvisibility='s
// default visibility (#3576), against Clang's symbols, accesses and libraries.

static bool check_test_visibility_symbol(const char* path, const char* name, uint16_t* flags)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) &&
                header.e_shoff <= length && header.e_shnum <= (length - header.e_shoff) / sizeof(Elf64_Shdr);
    }
    bool found = false;
    for (uint32_t s = 0; valid && !found && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr table;
        memcpy(&table, bytes + header.e_shoff + (uint64_t)s * sizeof(table), sizeof(table));
        if (table.sh_type == SHT_SYMTAB)
        {
            valid = table.sh_link < header.e_shnum && table.sh_entsize == sizeof(Elf64_Sym) &&
                    table.sh_offset <= length && table.sh_size <= length - table.sh_offset && table.sh_size % sizeof(Elf64_Sym) == 0;
            Elf64_Shdr strings = { 0 };
            if (valid)
            {
                memcpy(&strings, bytes + header.e_shoff + (uint64_t)table.sh_link * sizeof(strings), sizeof(strings));
                valid = strings.sh_offset <= length && strings.sh_size <= length - strings.sh_offset;
            }
            for (uint64_t i = 0; valid && !found && i < table.sh_size / sizeof(Elf64_Sym); i += 1)
            {
                Elf64_Sym symbol;
                memcpy(&symbol, bytes + table.sh_offset + i * sizeof(symbol), sizeof(symbol));
                valid = symbol.st_name < strings.sh_size;
                if (valid)
                {
                    const char* spelling = (const char*)bytes + strings.sh_offset + symbol.st_name;
                    const char* end = memchr(spelling, 0, (size_t)(strings.sh_size - symbol.st_name));
                    valid = end != NULL;
                    found = valid && (size_t)(end - spelling) == strlen(name) && memcmp(spelling, name, strlen(name)) == 0;
                    if (found)
                    {
                        *flags = (uint16_t)((uint16_t)ELF64_ST_BIND(symbol.st_info) << 8 | (symbol.st_other & 3));
                    }
                }
            }
        }
    }
    free(bytes);
    return valid && found;
}

// #3576's listing of an object, as clang's and -c's are compared: a line per global or weak symbol, "name type binding
// visibility place" (its place UND, COM or DEF), then with `relocations` a line per kind each such symbol is reached
// by, "name kind", but a call's (PLT32, of a call Tier 1 may expand), each line once, all sorted and joined in `text`.
// Whether the object could be read and its lines fit.
#define BUSTER_CHECK_VISIBILITY_LINES 256
#define BUSTER_CHECK_VISIBILITY_LINE 96
#define BUSTER_CHECK_VISIBILITY_LISTING 16384

static int check_test_visibility_order(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

static const char* check_test_visibility_kind(uint32_t type)
{
    return type == R_X86_64_PC32                                                                       ? "pc32" :
           type == R_X86_64_GOTPCREL || type == R_X86_64_GOTPCRELX || type == R_X86_64_REX_GOTPCRELX ? "got" :
           type == R_X86_64_64                                                                         ? "absolute" :
           type == R_X86_64_TLSGD                                                                      ? "general-dynamic" :
           type == R_X86_64_TLSLD                                                                      ? "local-dynamic" :
           type == R_X86_64_DTPOFF32                                                                   ? "dtpoff32" :
           type == R_X86_64_GOTTPOFF                                                                   ? "initial-exec" :
           type == R_X86_64_TPOFF32                                                                    ? "local-exec" :
                                                                                                         "other";
}

static bool check_test_visibility_listing(const char* path, bool relocations, char* text, uint64_t capacity)
{
    static const char* const types[] = { "notype", "object", "func", "section", "file", "common", "tls", "7", "8", "9", "ifunc" };
    static const char* const visibilities[] = { "default", "internal", "hidden", "protected" };
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    char (*lines)[BUSTER_CHECK_VISIBILITY_LINE] = calloc(BUSTER_CHECK_VISIBILITY_LINES, BUSTER_CHECK_VISIBILITY_LINE);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && lines != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shoff <= length &&
                header.e_shnum <= (length - header.e_shoff) / sizeof(Elf64_Shdr);
    }
    const Elf64_Shdr* sections = valid ? (const Elf64_Shdr*)(bytes + header.e_shoff) : NULL;
    uint32_t table = 0;
    for (uint32_t s = 0; valid && s < header.e_shnum; s += 1)
    {
        table = sections[s].sh_type == SHT_SYMTAB ? s : table;
    }
    const Elf64_Shdr* symbols = valid && table != 0 ? sections + table : NULL;
    const Elf64_Shdr* strings = symbols != NULL && symbols->sh_link < header.e_shnum ? sections + symbols->sh_link : NULL;
    valid = strings != NULL && symbols->sh_entsize == sizeof(Elf64_Sym) && symbols->sh_offset <= length && symbols->sh_size <= length - symbols->sh_offset &&
            strings->sh_offset <= length && strings->sh_size <= length - strings->sh_offset;
    const uint64_t symbol_count = valid ? symbols->sh_size / sizeof(Elf64_Sym) : 0;
    uint32_t count = 0;
    // The symbol's name where it is a named global or weak one, else null.
    const Elf64_Sym* entries = valid ? (const Elf64_Sym*)(bytes + symbols->sh_offset) : NULL;
    const char* names = valid ? (const char*)bytes + strings->sh_offset : NULL;
    for (uint64_t i = 1; valid && i < symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = entries + i;
        const uint8_t binding = ELF64_ST_BIND(symbol->st_info);
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* name = symbol->st_name < strings->sh_size && memchr(names + symbol->st_name, 0, strings->sh_size - symbol->st_name) != NULL ? names + symbol->st_name : NULL;
        valid = name != NULL && count < BUSTER_CHECK_VISIBILITY_LINES;
        if (valid && binding != STB_LOCAL && name[0] != 0)
        {
            const char* place = symbol->st_shndx == SHN_UNDEF ? "UND" : symbol->st_shndx == SHN_COMMON ? "COM" : "DEF";
            const int written = snprintf(lines[count], BUSTER_CHECK_VISIBILITY_LINE, "%s %s %s %s %s", name, type < BUSTER_ARRAY_LENGTH(types) ? types[type] : "?",
                                         binding == STB_GLOBAL ? "global" : binding == STB_WEAK ? "weak" : "?", visibilities[symbol->st_other & 3], place);
            valid = written > 0 && written < BUSTER_CHECK_VISIBILITY_LINE;
            count += 1;
        }
    }
    for (uint32_t s = 0; valid && relocations && s < header.e_shnum; s += 1)
    {
        const Elf64_Shdr* section = sections + s;
        const bool rela = section->sh_type == SHT_RELA && section->sh_link == table;
        valid = !rela || (section->sh_entsize == sizeof(Elf64_Rela) && section->sh_offset <= length && section->sh_size <= length - section->sh_offset);
        for (uint64_t r = 0; valid && rela && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            Elf64_Rela relocation;
            memcpy(&relocation, bytes + section->sh_offset + r * sizeof(relocation), sizeof(relocation));
            const uint64_t index = ELF64_R_SYM(relocation.r_info);
            const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
            valid = index < symbol_count && count < BUSTER_CHECK_VISIBILITY_LINES;
            const Elf64_Sym* symbol = valid ? entries + index : NULL;
            if (valid && index != 0 && ELF64_ST_BIND(symbol->st_info) != STB_LOCAL && type != R_X86_64_PLT32)
            {
                const int written = snprintf(lines[count], BUSTER_CHECK_VISIBILITY_LINE, "%s %s", names + symbol->st_name, check_test_visibility_kind(type));
                valid = written > 0 && written < BUSTER_CHECK_VISIBILITY_LINE;
                count += 1;
            }
        }
    }
    if (valid)
    {
        qsort(lines, count, BUSTER_CHECK_VISIBILITY_LINE, check_test_visibility_order);
    }
    uint64_t used = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint64_t line = strlen(lines[i]);
        const bool repeated = i != 0 && strcmp(lines[i], lines[i - 1]) == 0;
        valid = repeated || line + 2 <= capacity - used;
        if (valid && !repeated)
        {
            memcpy(text + used, lines[i], line);
            text[used + line] = '\n';
            used += line + 1;
        }
    }
    if (capacity != 0)
    {
        text[valid ? used : 0] = 0;
    }
    free(lines);
    free(bytes);
    return valid;
}

// #3576: -fvisibility= and -fvisibility-ms-compat give the unit's definitions of external linkage without a visibility of
// their own, and only them, theirs, as clang's do: functions, initialized, constant, tentative (-fcommon's commons too),
// thread-local and weak objects, a use before the definition, aliases, ifuncs, target_clones' and cpu_dispatch's
// symbols, C99's extern inline definitions and `#pragma weak`'s aliases, but not an attribute's or a pragma's (default
// or protected), nor declarations, weak references or the inline-only function the unit doesn't emit. Each option's
// object at both tiers, alone, with -fcommon and with -fPIC, whose relocations show how the unit reaches each symbol
// (directly or through its GOT, a thread-local by general- or local-dynamic access), must list clang's symbols and
// accesses; so must a plain unit's, which the fused direct path takes only at the default visibility. Then a library
// of hidden symbols but its export, built by clang, by buster at both tiers and by cc from buster's -c object, which a
// program can't preempt and whose hidden symbols dlsym doesn't find.
static void check_test_driver_default_visibility(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "extern int ext;\nextern int ext_after;\nint defined_var = 2;\nint tentative;\nconst int ro = 3;\nstatic int local_s = 4;\n"
        "_Thread_local int tls_def = 1;\nextern _Thread_local int tls_ext;\n"
        "int fn_def(void) { return ext + local_s + tls_def + tls_ext; }\nint fn_decl(void);\n"
        "int use(void) { return fn_decl() + defined_var + tentative + ext_after + fn_def(); }\nint ext_after = 5;\n"
        "__attribute__((visibility(\"default\"))) int exp_def = 6;\n__attribute__((visibility(\"default\"))) int exp_decl(void);\n"
        "__attribute__((weak)) int weak_def = 7;\nextern int weak_ref __attribute__((weak));\n"
        "int alias_target = 8;\nextern int alias_object __attribute__((alias(\"alias_target\")));\n"
        "#pragma GCC visibility push(default)\nint pushed_def = 9;\n#pragma GCC visibility pop\n"
        "#pragma GCC visibility push(protected)\nint pushed_protected = 10;\n#pragma GCC visibility pop\n"
        "inline int inline_only(void) { return 11; }\nextern inline int extern_inline(void) { return 12; }\n"
        "int pragma_target(void) { return 13; }\n#pragma weak pragma_alias = pragma_target\n"
        "static int impl(void) { return 14; }\nstatic int (*resolve(void))(void) { return impl; }\nint indirect(void) __attribute__((ifunc(\"resolve\")));\n"
        "__attribute__((target_clones(\"default\", \"avx2\"))) int clones(int x) { return x + 1; }\n"
        "int (*pointers[])(void) = { fn_def, fn_decl, exp_decl, inline_only };\nint (*function_address(void))(void) { return fn_def; }\n"
        "int* addresses(void) { return &weak_ref != 0 ? &weak_ref : &defined_var; }\n"
        "int reads(void) { return exp_def + weak_def + alias_object + pushed_def + pushed_protected; }\n"
        "int calls(void) { return inline_only() + extern_inline() + exp_decl() + indirect() + clones(1) + pointers[0](); }\n"
        "__attribute__((cpu_specific(generic))) int dispatched(void) { return 15; }\n"
        "__attribute__((cpu_specific(haswell))) int dispatched(void) { return 16; }\n"
        "__attribute__((cpu_dispatch(generic, haswell))) int dispatched(void);\nint dispatch(void) { return dispatched(); }\n";
    // A plain unit, which the fused direct path (#2850, c_direct.c) compiles where the default visibility is the default.
    static const char plain[] = "int counter = 1;\nint tentative_plain;\nint bump(int x)\n{\n    counter += x;\n    return counter + tentative_plain;\n}\n";
    static const char* const options[] = {
        "-fvisibility=hidden",
        "-fvisibility=protected",
        "-fvisibility=internal",
        "-fvisibility-ms-compat",
        "-fvisibility=default",
    };
    static const char* const modes[] = {
        NULL,
        "-fcommon",
        "-fPIC",
    };
    char paths[2][512];
    char reference[512];
    char object[512];
    char label[256];
    snprintf(paths[0], sizeof(paths[0]), "%s/default-visibility.c", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/default-visibility-plain.c", directory);
    snprintf(reference, sizeof(reference), "%s/default-visibility-clang.o", directory);
    snprintf(object, sizeof(object), "%s/default-visibility.o", directory);
    const bool written = parse_test_write(paths[0], source, sizeof(source) - 1) && parse_test_write(paths[1], plain, sizeof(plain) - 1);
    check_test_expect(test, written, "default visibility: the source fixtures are written");
    char* expected = malloc(BUSTER_CHECK_VISIBILITY_LISTING);
    char* actual = malloc(BUSTER_CHECK_VISIBILITY_LISTING);
    // The harness's SOURCE_DATE_EPOCH keeps the fused direct path out of every compile it runs; the plain unit's run
    // without it, so that the direct path's own admission is what keeps it from the visibility options.
    const char* epoch = getenv("SOURCE_DATE_EPOCH");
    char saved_epoch[64] = { 0 };
    const bool had_epoch = epoch != NULL && snprintf(saved_epoch, sizeof(saved_epoch), "%s", epoch) < (int)sizeof(saved_epoch);
    for (uint32_t c = 0; written && expected != NULL && actual != NULL && c < 2 * BUSTER_ARRAY_LENGTH(options); c += 1)
    {
        const uint32_t o = c >> 1;
        char* path = paths[c & 1];
        for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
        {
            const bool relocations = m == 2;
            char* diagnostics = NULL;
            char* oracle[] = { (char*)clang, "-std=gnu17", "-O0", "-c", (char*)options[o], path, "-o", reference, (char*)modes[m], NULL };
            unlink(reference);
            const bool compiled = check_test_driver_spawn(oracle, &diagnostics) == 0 &&
                                  check_test_visibility_listing(reference, relocations, expected, BUSTER_CHECK_VISIBILITY_LISTING);
            free(diagnostics);
            snprintf(label, sizeof(label), "default visibility: clang's object of %s %s %s is read", c & 1 ? "the plain unit" : "the fixture", options[o],
                     modes[m] != NULL ? modes[m] : "alone");
            check_test_expect(test, compiled, label);
            for (uint32_t tier = 0; compiled && tier < 2; tier += 1)
            {
                char* arguments[] = { (char*)executable, "-std=gnu17", "-c", (char*)options[o], path, "-o", object, tier != 0 ? "--tier1" : (char*)modes[m],
                                      tier != 0 ? (char*)modes[m] : NULL, NULL };
                diagnostics = NULL;
                unlink(object);
                if ((c & 1) != 0)
                {
                    unsetenv("SOURCE_DATE_EPOCH");
                }
                const bool ran = check_test_driver_spawn(arguments, &diagnostics) == 0;
                if ((c & 1) != 0 && had_epoch)
                {
                    setenv("SOURCE_DATE_EPOCH", saved_epoch, 1);
                }
                const bool built = ran && check_test_visibility_listing(object, relocations, actual, BUSTER_CHECK_VISIBILITY_LISTING);
                const bool same = built && strcmp(expected, actual) == 0;
                snprintf(label, sizeof(label), "default visibility: %s %s %s at Tier %u lists clang's symbols%s", c & 1 ? "the plain unit" : "the fixture", options[o],
                         modes[m] != NULL ? modes[m] : "alone", tier, relocations ? " and accesses" : "");
                check_test_expect(test, same, label);
                if (!same)
                {
                    printf("%s\nclang's:\n%s\nbuster's:\n%s\n%s", label, expected, built ? actual : "(none)", diagnostics != NULL ? diagnostics : "");
                }
                free(diagnostics);
            }
        }
    }
    free(expected);
    free(actual);
    unlink(paths[0]);
    unlink(paths[1]);
    unlink(reference);
    unlink(object);
    // The library: hidden but its export, which reaches its own hidden function, object and thread-local.
    static const char library_text[] =
        "int counter = 5;\nint helper(int x) { return x * 2; }\n_Thread_local int tls_counter = 3;\n"
        "__attribute__((visibility(\"default\"))) int api(int x) { return helper(x) + counter + tls_counter; }\n";
    static const char preempt_text[] =
        "#include <stdio.h>\nint counter = 100;\n_Thread_local int tls_counter = 100;\nint helper(int x) { return -x; }\nint api(int);\n"
        "int main(void)\n{\n    printf(\"%d\\n\", api(1));\n    return 0;\n}\n";
    static const char dl_text[] =
        "#include <dlfcn.h>\n#include <stdio.h>\n"
        "int main(int argc, char** argv)\n{\n    void* library = argc > 1 ? dlopen(argv[1], RTLD_NOW | RTLD_LOCAL) : 0;\n    if (library == 0) return 2;\n"
        "    int (*api)(int) = (int (*)(int))dlsym(library, \"api\");\n"
        "    printf(\"%d %d %d %d\\n\", api != 0 ? api(1) : -1, dlsym(library, \"helper\") == 0, dlsym(library, \"counter\") == 0,\n"
        "           dlsym(library, \"tls_counter\") == 0);\n    return 0;\n}\n";
    char library_source[256];
    char preempt_source[256];
    char dl_source[256];
    char library[256];
    char library_object[256];
    char preempt[256];
    char dl[256];
    char rpath[300];
    snprintf(library_source, sizeof(library_source), "%s/visibility-library.c", directory);
    snprintf(preempt_source, sizeof(preempt_source), "%s/visibility-preempt.c", directory);
    snprintf(dl_source, sizeof(dl_source), "%s/visibility-dl.c", directory);
    snprintf(library, sizeof(library), "%s/libvisibility.so", directory);
    snprintf(library_object, sizeof(library_object), "%s/visibility-library.o", directory);
    snprintf(preempt, sizeof(preempt), "%s/visibility-preempt", directory);
    snprintf(dl, sizeof(dl), "%s/visibility-dl", directory);
    snprintf(rpath, sizeof(rpath), "-Wl,-rpath,%s", directory);
    const bool sources = parse_test_write(library_source, library_text, sizeof(library_text) - 1) &&
                         parse_test_write(preempt_source, preempt_text, sizeof(preempt_text) - 1) && parse_test_write(dl_source, dl_text, sizeof(dl_text) - 1);
    char* clang_library[] = { (char*)clang, "-shared", "-fPIC", "-fvisibility=hidden", "-Wl,-soname,libvisibility.so", "-o", library, library_source, NULL };
    char* tier0[] = { (char*)executable, "-shared", "-fPIC", "-fvisibility=hidden", "-Wl,-soname,libvisibility.so", "-o", library, library_source, NULL };
    char* tier1[] = { (char*)executable, "-shared", "-fpic", "--tier1", "-fvisibility=hidden", "-Wl,-soname,libvisibility.so", "-o", library, library_source, NULL };
    char* compiled[] = { (char*)executable, "-c", "-fPIC", "-fvisibility=hidden", "-o", library_object, library_source, NULL };
    char* system_link[] = { "cc", "-shared", "-Wl,-soname,libvisibility.so", "-o", library, library_object, NULL };
    char* link_preempt[] = { (char*)clang, "-o", preempt, preempt_source, library, rpath, NULL };
    char* link_dl[] = { (char*)clang, "-o", dl, dl_source, NULL };
    char* run_preempt[] = { preempt, NULL };
    char* run_dl[] = { dl, library, NULL };
    static const char* const variants[] = {
        "clang's -shared",
        "-shared -fPIC",
        "-shared -fpic --tier1",
        "-c -fPIC and cc -shared",
    };
    for (uint32_t v = 0; sources && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        char* text = NULL;
        unlink(library);
        bool built = check_test_driver_spawn(v == 0 ? clang_library : v == 1 ? tier0 : v == 2 ? tier1 : compiled, &text) == 0;
        free(text);
        text = NULL;
        built = built && (v != 3 || check_test_driver_spawn(system_link, &text) == 0);
        free(text);
        text = NULL;
        built = built && check_test_driver_spawn(link_preempt, &text) == 0;
        free(text);
        text = NULL;
        built = built && check_test_driver_spawn(link_dl, &text) == 0;
        free(text);
        char* preempted = NULL;
        char* loaded = NULL;
        const bool ran = built && check_test_driver_pic_output(run_preempt, &preempted) == 0 && check_test_driver_pic_output(run_dl, &loaded) == 0;
        snprintf(label, sizeof(label), "default visibility: a hidden library by %s keeps its own symbols from a program and dlsym", variants[v]);
        check_test_expect(test, ran && preempted != NULL && strcmp(preempted, "10\n") == 0 && loaded != NULL && strcmp(loaded, "10 1 1 1\n") == 0, label);
        free(preempted);
        free(loaded);
    }
    const char* const files[] = { library_source, preempt_source, dl_source, library, library_object, preempt, dl };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// #3578: compare every global/weak symbol, including the synthetic reference, resolver and clone bodies, with clang.
// The fixture nests all three pragma visibilities and leaves ordinary declarations at each pop, so -fvisibility='s
// default must apply only outside them. Its CPU versions return the same answers on every host.
static void check_test_driver_multiversion_visibility(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const options[] =
    {
        "-fvisibility=default",
        "-fvisibility=hidden",
        "-fvisibility=protected",
    };
    static const char* const modes[] =
    {
        "-fno-pic",
        "-fPIC",
    };
    char source[PATH_MAX];
    const bool located = check_test_fixture_path(source, sizeof(source), "driver/visibility_multiversion.c");
    check_test_expect(test, located, "visibility multiversion: fixture path fits");
    char object[512];
    char program[512];
    char description[192];
    char* expected = malloc(BUSTER_CHECK_VISIBILITY_LISTING);
    char* actual = malloc(BUSTER_CHECK_VISIBILITY_LISTING);
    const bool room = expected != NULL && actual != NULL;
    check_test_expect(test, room, "multiversion visibility: the symbol listings have room");
    snprintf(object, sizeof(object), "%s/multiversion-visibility.o", directory);
    snprintf(program, sizeof(program), "%s/multiversion-visibility", directory);
    for (uint32_t o = 0; located && room && o < BUSTER_ARRAY_LENGTH(options); o += 1)
    {
        for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
        {
            bool reference = false;
            for (uint32_t p = 0; p < 3; p += 1)
            {
                char* diagnostics = NULL;
                char* arguments[] =
                {
                    (char*)(p == 0 ? clang : executable),
                    "-std=gnu17",
                    "-O0",
                    "-c",
                    (char*)source,
                    "-o",
                    object,
                    (char*)options[o],
                    (char*)modes[m],
                    p == 2 ? "--tier1" : NULL,
                    NULL,
                };
                unlink(object);
                const bool compiled = check_test_driver_spawn(arguments, &diagnostics) == 0 && diagnostics != NULL && diagnostics[0] == 0;
                free(diagnostics);
                const bool listed = compiled && check_test_visibility_listing(object, false, p == 0 ? expected : actual, BUSTER_CHECK_VISIBILITY_LISTING);
                reference = p == 0 ? listed : reference;
                snprintf(description, sizeof(description), "multiversion visibility: %s %s by %s has clang's symbols", options[o], modes[m],
                         p == 0 ? "clang" : p == 1 ? "Tier 0" : "Tier 1");
                check_test_expect(test, listed && reference && (p == 0 || strcmp(actual, expected) == 0), description);
                if (listed && p != 0 && reference && strcmp(actual, expected) != 0)
                {
                    fprintf(stderr, "multiversion visibility expected:\n%sactual:\n%s", expected, actual);
                }
                char* link_arguments[] =
                {
                    (char*)clang,
                    "-no-pie",
                    object,
                    "-o",
                    program,
                    NULL,
                };
                diagnostics = NULL;
                unlink(program);
                const bool linked = compiled && check_test_driver_spawn(link_arguments, &diagnostics) == 0;
                free(diagnostics);
                char captured[64] = { 0 };
                snprintf(description, sizeof(description), "multiversion visibility: %s %s by %s executes every group", options[o], modes[m],
                         p == 0 ? "clang" : p == 1 ? "Tier 0" : "Tier 1");
                check_test_expect(test, linked && check_test_driver_capture(program, captured, sizeof(captured)) && captured[0] == 0, description);
            }
        }
    }
    unlink(object);
    unlink(program);
    free(expected);
    free(actual);
}

static void check_test_driver_visibility(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] = "#pragma clang diagnostic ignored \"-Wimplicit-function-declaration\"\n"
        "int outside=1;\n#pragma GCC visibility push(hidden)\n"
        "int hidden=2;extern int external_hidden;_Thread_local int tls_hidden;extern _Thread_local int external_tls;static int local=3;"
        "int prior(void);int explicit_default __attribute__((visibility(\"default\")))=4;\n"
        "int implicit_caller(void){return missing_function(1)+external_tls;}\n"
        "#pragma GCC visibility push(protected)\nint protected_value=5;\n#pragma GCC visibility pop\n"
        "int hidden_again=6;\n#pragma GCC visibility push(bad)\nint ignored_bad=9;\n#pragma GCC visibility pop\nint restored=7;"
        "int prior(void){return outside+hidden+external_hidden+local+protected_value+hidden_again+restored+tls_hidden;}\n"
        "#pragma GCC visibility push(internal)\nint internal_value=8;\n#pragma GCC visibility pop\n"
        "int crossing(void){\n#pragma GCC visibility push(protected)\nreturn 0;}\n"
        "int after_body=10;\n#pragma GCC visibility pop\n"
        "__private_extern__ int private_definition=11;\n"
        "__private_extern__ int private_redeclared;int private_redeclared=12;\n"
        "__private_extern__ int external_private;\n"
        "__private_extern__ int private_function(void){return private_definition+private_redeclared+external_private;}\n";
    static const struct
    {
        const char* name;
        uint8_t visibility;
        uint8_t binding;
    } symbols[] = {
        { "outside", STV_DEFAULT, STB_GLOBAL },
        { "hidden", STV_HIDDEN, STB_GLOBAL },
        { "external_hidden", STV_HIDDEN, STB_GLOBAL },
        { "tls_hidden", STV_HIDDEN, STB_GLOBAL },
        { "external_tls", STV_HIDDEN, STB_GLOBAL },
        { "implicit_caller", STV_HIDDEN, STB_GLOBAL },
        { "missing_function", STV_HIDDEN, STB_GLOBAL },
        { "local", STV_DEFAULT, STB_LOCAL },
        { "prior", STV_HIDDEN, STB_GLOBAL },
        { "explicit_default", STV_DEFAULT, STB_GLOBAL },
        { "protected_value", STV_PROTECTED, STB_GLOBAL },
        { "hidden_again", STV_HIDDEN, STB_GLOBAL },
        { "ignored_bad", STV_HIDDEN, STB_GLOBAL },
        { "restored", STV_DEFAULT, STB_GLOBAL },
        { "internal_value", STV_HIDDEN, STB_GLOBAL },
        { "crossing", STV_DEFAULT, STB_GLOBAL },
        { "after_body", STV_PROTECTED, STB_GLOBAL },
        { "private_definition", STV_HIDDEN, STB_GLOBAL },
        { "private_redeclared", STV_HIDDEN, STB_GLOBAL },
        { "external_private", STV_HIDDEN, STB_GLOBAL },
        { "private_function", STV_HIDDEN, STB_GLOBAL },
    };
    char path[512];
    char objects[3][512] = { { 0 } };
    snprintf(path, sizeof(path), "%s/visibility.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "visibility source fixture is written");
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/visibility-%u.o", directory, p);
        char* diagnostics = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &diagnostics) == 0;
        check_test_expect(test, compiled, "visibility pragmas compile through Clang and both checker paths");
        free(diagnostics);
        for (uint32_t s = 0; compiled && s < BUSTER_ARRAY_LENGTH(symbols); s += 1)
        {
            uint16_t actual = UINT16_MAX;
            const uint16_t expected = (uint16_t)((uint16_t)symbols[s].binding << 8 | symbols[s].visibility);
            char description[160];
            snprintf(description, sizeof(description), "%s visibility/binding independently matches ELF and Clang", symbols[s].name);
            check_test_expect(test, check_test_visibility_symbol(objects[p], symbols[s].name, &actual) && actual == expected, description);
        }
    }
    if (written)
    {
        check_test_expect(test, check_test_driver_same_files(objects[1], objects[2]), "visibility scalar/vector objects match byte for byte");
    }
    for (uint32_t p = 0; p < 3; p += 1)
    {
        unlink(objects[p]);
    }
    static const char* const malformed[] = {
        "#pragma GCC visibility pop\nint x;\n",
        "#pragma GCC visibility push(bad)\nint x;\n",
        "#pragma GCC visibility push\nint x;\n",
        "#pragma GCC visibility push()\nint x;\n",
        "#pragma GCC visibility push(hidden) extra\nint x;\n",
        "#pragma GCC visibility what\nint x;\n",
        "#pragma GCC visibility push(hidden\nint x;\n",
        "#pragma GCC visibility pop()\nint x;\n",
        "#pragma GCC visibility push(bad)\n#pragma clang diagnostic ignored \"-Wignored-attributes\"\nint x;\n",
        "#pragma clang diagnostic ignored \"-Wignored-attributes\"\n#pragma GCC visibility push(bad)\nint x;\n",
        "#pragma GCC visibility push\n#pragma clang diagnostic ignored \"-Wignored-pragmas\"\nint x;\n",
        "#pragma clang diagnostic error \"-Wignored-pragmas\"\n#pragma GCC visibility push\nint x;\n",
        "#pragma GCC visibility push(hidden)\n#pragma GCC visibility pop\n",
        "int\n#pragma GCC visibility push(hidden)\nx;\n",
        "int f(void){return\n#pragma GCC visibility push(hidden)\n0;}\n",
        "struct S {\n#pragma GCC visibility push(hidden)\nint x;};\n",
        "int x=\n#pragma GCC visibility push(hidden)\n1;\n",
        "int x=\n#pragma GCC visibility push(bad)\n1;\n",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(malformed); c += 1)
    {
        const bool ready = parse_test_write(path, malformed[c], strlen(malformed[c]));
        for (uint32_t p = 0; ready && p < 2; p += 1)
        {
            char* expected = NULL;
            char* arguments[] = { (char*)clang, "-fsyntax-only", "-fno-caret-diagnostics", path, p != 0 ? "-w" : NULL, NULL, NULL };
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[p != 0 ? 5 : 4] = scalar != 0 ? "--scalar" : NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                char description[160];
                snprintf(description, sizeof(description), "malformed visibility case %u %s %s has Clang's exact diagnostic", c,
                         scalar != 0 ? "scalar" : "vector", p != 0 ? "quiet" : "ordinary");
                check_test_expect(test, oracle >= 0 && status == oracle && actual != NULL && expected != NULL && strcmp(actual, expected) == 0, description);
                free(actual);
            }
            free(expected);
        }
    }
    unlink(path);
    check_test_driver_default_visibility(test, clang, executable, directory);
    check_test_driver_multiversion_visibility(test, clang, executable, directory);
}
