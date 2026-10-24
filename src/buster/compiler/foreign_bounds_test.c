// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __start_ and __stop_ in foreign executables (foreign_link_bounds_plan and foreign_link_bounds_merge), and the refusal
// of an IFUNC that a foreign executable defines and refers to (foreign_link_refuse_ifuncs). Include from foreign_test.c
// after foreign_copy_test.c, whose run helper it takes. The fixtures are original: a table of 16-byte entries in a
// section named fb_table from two objects and an archive member that loads (and one that doesn't, whose entry mustn't
// count), at alignments 8 and 16; a section named fb_bytes, writable in one object and read-only in another, which
// merges into one writable section of their bytes; a weak reference to the bounds of a name no section has, which stays
// zero; then an IFUNC called (refused, naming it) and one that nothing refers to (linked). Each resolution, on both paths
// and 1/2/3/8 lanes, writes a PIE the loader runs, byte for byte alike, whose symbols the test reads; with --cross-check,
// lld links the same inputs, which must run alike and define the bounds alike.

static const ForeignCopyTestSource foreign_bounds_test_sources[] = {
    {
        .file = "fb_main.c",
        .text = "struct fb_entry { const char* name; long value; };\n"
                "extern const struct fb_entry __start_fb_table[], __stop_fb_table[];\n"
                "extern char __start_fb_bytes[], __stop_fb_bytes[];\n"
                "extern char __start_fb_missing[] __attribute__((weak));\n"
                "static const struct fb_entry fb_main_entry __attribute__((used, section(\"fb_table\"), aligned(16))) = { \"main\", 1 };\n"
                "extern int fb_other(void);\n"
                "__attribute__((noreturn)) void _start(void) {\n"
                "    long total = 0;\n"
                "    long count = 0;\n"
                "    for (const struct fb_entry* e = __start_fb_table; e < __stop_fb_table; e += 1) { total += e->value; count += 1; }\n"
                "    const long bytes = __stop_fb_bytes - __start_fb_bytes;\n"
                "    const int status = count == 4 && total == 15 && __start_fb_missing == 0 && bytes == 12 && __start_fb_bytes[0] == 'w' && fb_other() == 3 ? 42 : 101;\n"
                "    __asm__ volatile(\"syscall\" : : \"a\"(60), \"D\"(status));\n"
                "    __builtin_unreachable();\n"
                "}\n",
    },
    {
        .file = "fb_other.c",
        .text = "struct fb_entry { const char* name; long value; };\n"
                "static const struct fb_entry fb_two __attribute__((used, section(\"fb_table\"))) = { \"two\", 2 };\n"
                "static const struct fb_entry fb_four __attribute__((used, section(\"fb_table\"))) = { \"four\", 4 };\n"
                "char fb_written[5] __attribute__((section(\"fb_bytes\"))) = \"wxyz\";\n"
                "extern int fb_member(void);\n"
                "int fb_other(void) { return fb_member(); }\n",
    },
    {
        .file = "fb_member.c",
        .text = "struct fb_entry { const char* name; long value; };\n"
                "static const struct fb_entry fb_eight __attribute__((used, section(\"fb_table\"), aligned(16))) = { \"eight\", 8 };\n"
                "const char fb_constant[7] __attribute__((section(\"fb_bytes\"))) = \"const!\";\n"
                "int fb_member(void) { return 3; }\n",
    },
    {
        .file = "fb_unloaded.c",
        .text = "struct fb_entry { const char* name; long value; };\n"
                "static const struct fb_entry fb_sixteen __attribute__((used, section(\"fb_table\"))) = { \"sixteen\", 16 };\n"
                "int fb_unloaded(void) { return 16; }\n",
    },
    {
        .file = "fb_ifunc.c",
        .text = "static int fb_one(void) { return 42; }\n"
                "static int (*fb_resolve(void))(void) { return fb_one; }\n"
                "int fb_add(void) __attribute__((ifunc(\"fb_resolve\")));\n"
                "__attribute__((noreturn)) void _start(void) { __asm__ volatile(\"syscall\" : : \"a\"(60), \"D\"(fb_add())); __builtin_unreachable(); }\n",
    },
    {
        .file = "fb_ifunc_unused.c",
        .text = "static int fb_one(void) { return 7; }\n"
                "static int (*fb_resolve(void))(void) { return fb_one; }\n"
                "int fb_unused(void) __attribute__((ifunc(\"fb_resolve\")));\n"
                "__attribute__((noreturn)) void _start(void) { __asm__ volatile(\"syscall\" : : \"a\"(60), \"D\"(42)); __builtin_unreachable(); }\n",
    },
};

typedef enum ForeignBoundsTestCase
{
    FOREIGN_BOUNDS_TEST_TABLE,
    FOREIGN_BOUNDS_TEST_IFUNC,
    FOREIGN_BOUNDS_TEST_IFUNC_UNUSED,
    FOREIGN_BOUNDS_TEST_CASES,
} ForeignBoundsTestCase;

static bool foreign_bounds_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_bounds_test_sources); i += 1)
    {
        const ForeignCopyTestSource* fixture = &foreign_bounds_test_sources[i];
        const int stem = (int)(strrchr(fixture->file, '.') - fixture->file);
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s", directory, fixture->file);
        snprintf(object, sizeof(object), "%s/%.*s.o", directory, stem, fixture->file);
        snprintf(output, sizeof(output), "%s/%.*s_compile.out", directory, stem, fixture->file);
        snprintf(errors, sizeof(errors), "%s/%.*s_compile.err", directory, stem, fixture->file);
        char* compile[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-O2",
            (char*)"-fPIE",
            (char*)"-fno-asynchronous-unwind-tables",
            (char*)"-fno-stack-protector",
            source,
            (char*)"-o",
            object,
            0,
        };
        const bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(compile, output, errors) == 0;
        foreign_test_expect(tally, made, "compile original __start_ and __stop_ fixture", fixture->file);
        result = result && made;
    }
    return result;
}

// What the table case's PIE must hold: the bounds defined global and protected, of no size, 64 bytes apart, and the
// missing name's bound left undefined.
static void foreign_bounds_test_structure(ForeignTestTally* tally, String8 file, const char* detail)
{
    const DynamicTestImage image = dynamic_test_image(file);
    const Elf64_Shdr* table = dynamic_test_section(&image, ".symtab");
    const Elf64_Shdr* strings = table != 0 && table->sh_link < image.header->e_shnum ? &image.section_headers[table->sh_link] : 0;
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(&image, table);
    const char* names = (const char*)dynamic_test_bytes(&image, strings);
    Elf64_Sym start = { 0 };
    Elf64_Sym stop = { 0 };
    Elf64_Sym missing = { 0 };
    uint32_t found = 0;
    for (uint64_t i = 1; symbols != 0 && names != 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        const char* name = symbols[i].st_name < strings->sh_size ? names + symbols[i].st_name : "";
        const bool is_start = strcmp(name, "__start_fb_table") == 0;
        const bool is_stop = strcmp(name, "__stop_fb_table") == 0;
        const bool is_missing = strcmp(name, "__start_fb_missing") == 0;
        start = is_start ? symbols[i] : start;
        stop = is_stop ? symbols[i] : stop;
        missing = is_missing ? symbols[i] : missing;
        found += is_start || is_stop || is_missing ? 1 : 0;
    }
    const bool bounds = found == 3 && start.st_shndx != SHN_UNDEF && stop.st_shndx == start.st_shndx && stop.st_value - start.st_value == 64 && start.st_size == 0 &&
                        ELF64_ST_BIND(start.st_info) == STB_GLOBAL && ELF64_ST_VISIBILITY(start.st_other) == STV_PROTECTED &&
                        ELF64_ST_BIND(stop.st_info) == STB_GLOBAL && ELF64_ST_VISIBILITY(stop.st_other) == STV_PROTECTED;
    foreign_test_expect(tally, bounds, "the bounds are global and protected, 64 bytes apart", detail);
    foreign_test_expect(tally, found == 3 && missing.st_shndx == SHN_UNDEF && ELF64_ST_BIND(missing.st_info) == STB_WEAK, "a bound of a name no section has stays undefined", detail);
}

static void foreign_bounds_test_case(ForeignTestTally* tally, const char* directory, Arena* arena, bool cross_check, String8 interpreter, ForeignBoundsTestCase test)
{
    const uint64_t saved = arena->length;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    bool ready = line != 0;
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        foreign_test_add(line, (char*)"-pie", false, true);
        foreign_test_add(line, (char*)"-e", false, true);
        foreign_test_add(line, (char*)"_start", false, true);
        foreign_test_add(line, (char*)"-dynamic-linker", false, true);
        foreign_test_add(line, interpreter.pointer, false, true);
        if (test == FOREIGN_BOUNDS_TEST_TABLE)
        {
            const char* members[] = { "fb_member.o", "fb_unloaded.o" };
            foreign_test_add(line, foreign_test_path(directory, "fb_main.o", arena), true, true);
            foreign_test_add(line, foreign_test_path(directory, "fb_other.o", arena), true, true);
            foreign_test_add(line, foreign_test_archive(directory, "fb_members.a", members, BUSTER_ARRAY_LENGTH(members), false, arena), true, true);
        }
        else
        {
            foreign_test_add(line, foreign_test_path(directory, test == FOREIGN_BOUNDS_TEST_IFUNC ? "fb_ifunc.o" : "fb_ifunc_unused.o", arena), true, true);
        }
        for (uint32_t i = 0; i < line->our_count; i += 1)
        {
            ready = ready && line->ours[i] != 0;
        }
    }
    ForeignCommand command = { 0 };
    ready = ready && foreign_test_command(&command, line->ours, line->our_count, arena);
    foreign_test_expect(tally, ready, "prepare __start_ and __stop_ command", 0);
    String8 reference = { 0 };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            const uint64_t iteration = arena->length;
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            Arena names = arena_reserve(1ull << 28);
            ForeignLink converted = { 0 };
            const ForeignLinkResult built = foreign_link_build_dynamic(&converted, &foreign, arena, &names, interpreter);
            char detail[192];
            snprintf(detail, sizeof(detail), "case %u %s %u lanes: resolve %u convert %u name %u", (unsigned)test, path == 0 ? "exact" : "vector",
                     foreign_test_lanes[l], (unsigned)foreign.failure.error, (unsigned)built.error, built.name);
            if (test == FOREIGN_BOUNDS_TEST_IFUNC)
            {
                const uint32_t wanted = foreign.failure.error == FOREIGN_ERROR_NONE ? foreign_test_name(&foreign, "fb_add") : 0;
                foreign_test_expect(tally, wanted != 0 && built.error == FOREIGN_LINK_ERROR_IFUNC && built.name == wanted && built.object != 0,
                                    "an IFUNC a fixup reaches is refused, naming it", detail);
            }
            else
            {
                const bool linked = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
                foreign_test_expect(tally, linked, "convert __start_ and __stop_ fixture", detail);
                ForeignTestPath executable;
                snprintf(executable, sizeof(executable), "%s/fb_output_%u_%u_%u", directory, (unsigned)test, path, foreign_test_lanes[l]);
                const ExecutableSymbols* selected = 0;
                ExecutableTestWrite write = {
                    .link = &converted.link,
                    .options = {
                        .path = { .pointer = executable, .length = strlen(executable) },
                        .entry = linked ? foreign_link_test_symbol(&foreign, &converted, "_start") : 0,
                        .dynamic = &converted.dynamic,
                        .exact = path == 0,
                    },
                };
                // Every named symbol, for the bounds' checks.
                SymbolId* ids = linked ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, converted.link.symbols.count) : 0;
                uint32_t id_count = 0;
                for (SymbolId symbol = 1; ids != 0 && symbol < converted.link.symbols.count; symbol += 1)
                {
                    if (converted.link.symbols.name[symbol] != 0)
                    {
                        ids[id_count] = symbol;
                        id_count += 1;
                    }
                }
                ExecutableSymbols symbols = { .ids = ids, .count = id_count };
                selected = ids != 0 ? &symbols : 0;
                write.options.symbols = selected;
                const bool published = linked && lane_run(foreign_test_lanes[l], executable_test_lane_main, &write, 0) && write.succeeded;
                foreign_test_expect(tally, published, "write __start_ and __stop_ PIE", detail);
                if (linked && !published)
                {
                    executable_test_print_failure(detail, &write.result);
                }
                const String8 bytes = published ? foreign_test_read(arena, executable) : (String8){ 0 };
                foreign_test_expect(tally, published && foreign_copy_test_run(directory, executable, FOREIGN_COPY_TEST_DATA, arena), "loader runs __start_ and __stop_ fixture",
                                    detail);
                if (published && test == FOREIGN_BOUNDS_TEST_TABLE)
                {
                    foreign_bounds_test_structure(tally, bytes, detail);
                }
                if (path == 0 && l == 0)
                {
                    reference = bytes;
                }
                else
                {
                    const bool same = reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0;
                    foreign_test_expect(tally, same, "__start_ and __stop_ output is identical across paths and lanes", detail);
                }
            }
            arena_release(&names);
            foreign_release(&foreign);
            if (path != 0 || l != 0)
            {
                arena_pop_to(arena, iteration);
            }
        }
    }
    if (ready && cross_check && test == FOREIGN_BOUNDS_TEST_TABLE)
    {
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fb_reference", directory);
        snprintf(output, sizeof(output), "%s/fb_reference_link.out", directory);
        snprintf(errors, sizeof(errors), "%s/fb_reference_link.err", directory);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)line->their_count + 4);
        bool linked = arguments != 0;
        if (linked)
        {
            arguments[0] = (char*)"ld.lld";
            arguments[1] = (char*)"-o";
            arguments[2] = executable;
            memcpy(arguments + 3, line->theirs, (uint64_t)line->their_count * sizeof(char*));
            arguments[line->their_count + 3] = 0;
            linked = object_test_run(arguments, output, errors) == 0;
        }
        foreign_test_expect(tally, linked && foreign_copy_test_run(directory, executable, FOREIGN_COPY_TEST_DATA, arena), "lld links and runs the __start_ and __stop_ fixture", 0);
        if (linked)
        {
            foreign_bounds_test_structure(tally, foreign_test_read(arena, executable), "lld");
        }
    }
    arena_pop_to(arena, saved);
}

static bool foreign_bounds_test(const char* directory, Arena* arena, bool cross_check)
{
    ForeignTestTally tally = {
        .check = "foreign __start_ and __stop_",
    };
    Runtime runtime;
    const bool found = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, found, "find system dynamic loader", 0);
    if (found && foreign_bounds_test_compile(directory, &tally))
    {
        for (uint32_t test = 0; test < FOREIGN_BOUNDS_TEST_CASES; test += 1)
        {
            foreign_bounds_test_case(&tally, directory, arena, cross_check, runtime.loader_path, (ForeignBoundsTestCase)test);
        }
    }
    runtime_release(&runtime);
    return foreign_test_report(&tally);
}
