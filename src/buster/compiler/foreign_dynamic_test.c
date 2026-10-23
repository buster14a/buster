// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Dynamic foreign-input handoff tests. Include from foreign_test.c after foreign_link_test.c and its helpers.
// Original C/assembly fixtures exercise arbitrary versioned/unversioned DSOs, imports through GOT and function
// pointers, as-needed selection, executable exports reached by a DSO and dlsym, and libc startup plus an archive.
// Every fresh exact/vector resolution writes a fresh PIE on 1/2/3/8 lanes. Complete bytes must agree; execution and
// ELF metadata independently check the result. --cross-check links and runs the same inputs with GNU ld and lld.

typedef struct ForeignDynamicTestSource ForeignDynamicTestSource;
struct ForeignDynamicTestSource
{
    const char* file;
    const char* text;
    const char* library;
    const char* version;
};

static const ForeignDynamicTestSource foreign_dynamic_test_sources[] = {
    {
        .file = "fd_alpha.c",
        .text = "int alpha_data = 15; int alpha(void) { return 27; }\n",
        .library = "libfd-alpha.so",
        .version = "FD_ALPHA_1 { global: alpha; alpha_data; local: *; };\n",
    },
    {
        .file = "fd_beta.c",
        .text = "int beta(void) { return 9; }\n",
        .library = "libfd-beta.so",
    },
    {
        .file = "fd_gamma.c",
        .text = "int foreign_gamma(void) { return 6; }\n",
        .library = "libfd-gamma.so",
        .version = "FD_GAMMA_1 { global: foreign_gamma; local: *; };\n",
    },
    {
        .file = "fd_unused.c",
        .text = "int never_needed(void) { return 101; } int discarded_only_missing(void) { return 99; }\n",
        .library = "libfd-unused.so",
    },
    {
        .file = "fd_back.c",
        .text = "extern int host_value(void); extern char host_blob[]; int from_host(void) { return host_value() + host_blob[0] - 26; }\n",
        .library = "libfd-back.so",
        .version = "FD_BACK_1 { global: from_host; local: *; };\n",
    },
    {
        .file = "fd_version.s",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\n"
            "call alpha\ncmp $27,%eax\njne .Lfail\n"
            "mov alpha_data@GOTPCREL(%rip),%rdx\ncmpl $15,(%rdx)\njne .Lfail\n"
            "call *alpha_pointer(%rip)\ncmp $27,%eax\njne .Lfail\n"
            "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".data\n.balign 8\nalpha_pointer:\n.quad alpha\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .file = "fd_multi.s",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\n"
            "call alpha\nmov %eax,%ebx\ncall beta\nadd %eax,%ebx\ncall foreign_gamma\nadd %eax,%ebx\n"
            "mov %ebx,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .file = "fd_back_main.c",
        .text =
            "extern void* dlsym(void*, const char*); extern int from_host(void);\n"
            "char host_blob[13] = { 34 }; int host_value(void) { return host_blob[0]; }\n"
            "int main(void) { if (dlsym((void*)0, \"host_blob\") != (void*)host_blob || dlsym((void*)0, \"host_value\") != (void*)host_value) return 101; return from_host(); }\n",
    },
    {
        .file = "fd_archive_main.c",
        .text = "extern int puts(const char*); extern int archive_value(void); int main(void) { return puts(\"foreign dynamic\") < 0 ? 101 : archive_value(); }\n",
    },
    {
        .file = "fd_archive.c",
        .text = "int archive_value(void) { return 42; }\n",
    },
    {
        .file = "fd_comdat.s",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\ncall chosen\ncmp $23,%eax\njne .Lfail\n"
            "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n"
            ".size _start,.-_start\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .file = "fd_weak.s",
        .text =
            ".text\n.globl _start\n.type _start,@function\n.weak alpha\n.weak _init\n.weak _fini\n_start:\n"
            "mov alpha@GOTPCREL(%rip),%rax\ntest %rax,%rax\njnz .Lfail\n"
            "mov _init@GOTPCREL(%rip),%rax\ntest %rax,%rax\njnz .Lfail\n"
            "mov _fini@GOTPCREL(%rip),%rax\ntest %rax,%rax\njnz .Lfail\n"
            "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n"
            ".size _start,.-_start\n.section .note.GNU-stack,\"\",@progbits\n",
    },
};

static bool foreign_dynamic_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    ForeignTestPath source;
    ForeignTestPath object;
    ForeignTestPath library;
    ForeignTestPath script;
    ForeignTestPath output;
    ForeignTestPath errors;
    char version_option[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char soname[128];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_dynamic_test_sources); i += 1)
    {
        const ForeignDynamicTestSource* fixture = &foreign_dynamic_test_sources[i];
        const int stem = (int)(strrchr(fixture->file, '.') - fixture->file);
        snprintf(source, sizeof(source), "%s/%s", directory, fixture->file);
        snprintf(object, sizeof(object), "%s/%.*s.o", directory, stem, fixture->file);
        snprintf(output, sizeof(output), "%s/%.*s_compile.out", directory, stem, fixture->file);
        snprintf(errors, sizeof(errors), "%s/%.*s_compile.err", directory, stem, fixture->file);
        char* compile[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-O1",
            (char*)"-fPIC",
            (char*)"-fno-asynchronous-unwind-tables",
            (char*)"-fno-unwind-tables",
            source,
            (char*)"-o",
            object,
            0,
        };
        bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(compile, output, errors) == 0;
        if (made && fixture->library != 0)
        {
            snprintf(library, sizeof(library), "%s/%s", directory, fixture->library);
            snprintf(soname, sizeof(soname), "-Wl,-soname,%s", fixture->library);
            snprintf(script, sizeof(script), "%s/%.*s.map", directory, stem, fixture->file);
            snprintf(version_option, sizeof(version_option), "-Wl,--version-script=%s", script);
            snprintf(output, sizeof(output), "%s/%.*s_link.out", directory, stem, fixture->file);
            snprintf(errors, sizeof(errors), "%s/%.*s_link.err", directory, stem, fixture->file);
            made = fixture->version == 0 || foreign_test_write(script, fixture->version, strlen(fixture->version));
            char* link[] = {
                (char*)"cc",
                (char*)"-shared",
                (char*)"-nostdlib",
                soname,
                object,
                (char*)"-o",
                library,
                fixture->version != 0 ? version_option : 0,
                0,
            };
            made = made && object_test_run(link, output, errors) == 0;
        }
        foreign_test_expect(tally, made, "compile original dynamic fixture", fixture->file);
        result = result && made;
    }
    return result;
}

static uint32_t foreign_dynamic_test_symbol(const DynamicTestImage* image, const char* wanted, Elf64_Sym* value)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    const char* names = (const char*)dynamic_test_bytes(image, strings);
    uint32_t result = 0;
    for (uint64_t i = 1; result == 0 && symbols != 0 && names != 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        if (symbols[i].st_name < strings->sh_size && strcmp(names + symbols[i].st_name, wanted) == 0)
        {
            result = (uint32_t)i;
            *value = symbols[i];
        }
    }
    return result;
}

static bool foreign_dynamic_test_relocation(const DynamicTestImage* image, const char* name, uint32_t type)
{
    Elf64_Sym symbol = { 0 };
    const uint32_t wanted = foreign_dynamic_test_symbol(image, name, &symbol);
    const Elf64_Shdr* section = dynamic_test_section(image, ".rela.dyn");
    const Elf64_Rela* relocations = (const Elf64_Rela*)dynamic_test_bytes(image, section);
    bool result = false;
    for (uint64_t i = 0; wanted != 0 && relocations != 0 && i < section->sh_size / sizeof(Elf64_Rela); i += 1)
    {
        result = result || (ELF64_R_SYM(relocations[i].r_info) == wanted && ELF64_R_TYPE(relocations[i].r_info) == type);
    }
    return result;
}

static void foreign_dynamic_test_version(ForeignTestTally* tally, const DynamicTestImage* image, const char* name, bool versioned)
{
    Elf64_Sym symbol = { 0 };
    const uint32_t index = foreign_dynamic_test_symbol(image, name, &symbol);
    const Elf64_Shdr* section = dynamic_test_section(image, ".gnu.version");
    const Elf64_Half* versions = (const Elf64_Half*)dynamic_test_bytes(image, section);
    const bool present = versions != 0 && index < section->sh_size / sizeof(Elf64_Half);
    const bool correct = versioned ? present && versions[index] > VER_NDX_GLOBAL : !present || versions[index] == VER_NDX_GLOBAL;
    foreign_test_expect(tally, index != 0 && correct, "import has its required symbol version", name);
}

static void foreign_dynamic_test_structure(ForeignTestTally* tally, const ForeignLink* converted, String8 file, uint32_t fixture)
{
    const DynamicTestImage image = dynamic_test_image(file);
    foreign_test_expect(tally, image.valid && dynamic_test_segment(&image, PT_INTERP) != 0, "foreign output is an interpreted PIE", 0);
    const char* needed[16] = { 0 };
    const uint32_t count = dynamic_test_needed(&image, needed, BUSTER_ARRAY_LENGTH(needed));
    bool alpha = false;
    bool beta = false;
    bool gamma = false;
    bool unused = false;
    bool back = false;
    bool libc = false;
    for (uint32_t i = 0; i < count && i < BUSTER_ARRAY_LENGTH(needed); i += 1)
    {
        alpha = alpha || strcmp(needed[i], "libfd-alpha.so") == 0;
        beta = beta || strcmp(needed[i], "libfd-beta.so") == 0;
        gamma = gamma || strcmp(needed[i], "libfd-gamma.so") == 0;
        unused = unused || strcmp(needed[i], "libfd-unused.so") == 0;
        back = back || strcmp(needed[i], "libfd-back.so") == 0;
        libc = libc || strcmp(needed[i], "libc.so.6") == 0;
    }
    char versions[8192];
    const bool described = dynamic_test_version_needs(&image, versions, sizeof(versions));
    foreign_test_expect(tally, described && !unused, "needed libraries and version chain exclude unused as-needed DSO", 0);
    if (fixture <= 1)
    {
        foreign_test_expect(tally, alpha && count == (fixture == 0 ? 1u : 3u), "all arbitrary needed DSOs are recorded", 0);
        foreign_test_expect(tally, described && strstr(versions, "libfd-alpha.so:FD_ALPHA_1;") != 0, "versioned DSO has VERNEED entry", 0);
        foreign_dynamic_test_version(tally, &image, "alpha", true);
        foreign_test_expect(tally, foreign_dynamic_test_relocation(&image, "alpha", R_X86_64_GLOB_DAT), "function import has a GOT relocation", 0);
    }
    if (fixture == 0)
    {
        foreign_dynamic_test_version(tally, &image, "alpha_data", true);
        foreign_test_expect(tally, foreign_dynamic_test_relocation(&image, "alpha_data", R_X86_64_GLOB_DAT), "data import uses GOT without a copy relocation", 0);
        foreign_test_expect(tally, foreign_dynamic_test_relocation(&image, "alpha", R_X86_64_64), "function pointer has a symbolic absolute relocation", 0);
        foreign_test_expect(tally, dynamic_test_count_relocations(&image, R_X86_64_COPY) == 0, "foreign PIE has no COPY relocation", 0);
    }
    else if (fixture == 1)
    {
        foreign_test_expect(tally, beta && gamma, "third and unversioned DSOs remain needed", 0);
        foreign_dynamic_test_version(tally, &image, "beta", false);
        foreign_dynamic_test_version(tally, &image, "foreign_gamma", true);
        foreign_test_expect(tally, described && strstr(versions, "libfd-gamma.so:FD_GAMMA_1;") != 0, "third DSO version is retained", 0);
    }
    else if (fixture == 2)
    {
        Elf64_Sym blob = { 0 };
        Elf64_Sym function = { 0 };
        const uint32_t blob_id = foreign_dynamic_test_symbol(&image, "host_blob", &blob);
        const uint32_t function_id = foreign_dynamic_test_symbol(&image, "host_value", &function);
        foreign_test_expect(tally, back && libc, "DSO and libc both remain needed", 0);
        foreign_test_expect(tally, blob_id != 0 && blob.st_shndx != SHN_UNDEF && blob.st_size == 13 && ELF64_ST_TYPE(blob.st_info) == STT_OBJECT,
                            "executable data export retains its size and type", 0);
        foreign_test_expect(tally, function_id != 0 && function.st_shndx != SHN_UNDEF && function.st_size != 0 && ELF64_ST_TYPE(function.st_info) == STT_FUNC,
                            "DSO reference exports executable function", 0);
        foreign_dynamic_test_version(tally, &image, "from_host", true);
    }
    else if (fixture == 3)
    {
        foreign_test_expect(tally, libc, "archive program needs libc", 0);
    }
    else if (fixture == 4)
    {
        Elf64_Sym symbol = { 0 };
        const uint32_t index = foreign_dynamic_test_symbol(&image, "alpha", &symbol);
        uint64_t callback = 0;
        foreign_test_expect(tally, count == 0 && described && versions[0] == 0 && converted->dynamic.version_count == 0,
                            "weak-only as-needed DSO publishes neither dependency nor version", 0);
        foreign_test_expect(tally, index != 0 && symbol.st_shndx == SHN_UNDEF && ELF64_ST_BIND(symbol.st_info) == STB_WEAK && ELF64_ST_TYPE(symbol.st_info) == STT_FUNC,
                            "weak DSO function stays undefined with its type", 0);
        foreign_dynamic_test_version(tally, &image, "alpha", false);
        foreign_test_expect(tally, foreign_dynamic_test_relocation(&image, "alpha", R_X86_64_GLOB_DAT), "weak DSO function has a symbolic GOT relocation", 0);
        foreign_test_expect(tally, !dynamic_test_tag(&image, DT_INIT, &callback) && !dynamic_test_tag(&image, DT_FINI, &callback) &&
                            converted->dynamic.init == 0 && converted->dynamic.fini == 0, "undefined weak init and fini do not become loader callbacks", 0);
    }
    else
    {
        Elf64_Sym symbol = { 0 };
        foreign_test_expect(tally, count == 0 && described && versions[0] == 0 && converted->dynamic.version_count == 0 &&
                            foreign_dynamic_test_symbol(&image, "discarded_only_missing", &symbol) == 0,
                            "discarded COMDAT reference imports neither symbol nor earlier as-needed DSO", 0);
    }
    foreign_test_expect(tally, converted->dynamic.interpreter.length != 0, "dynamic handoff preserves interpreter", 0);
}

static bool foreign_dynamic_test_run(const char* directory, char* executable, uint32_t fixture, Arena* arena)
{
    char environment[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    char errors[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
    snprintf(environment, sizeof(environment), "LD_LIBRARY_PATH=%s", directory);
    snprintf(output, sizeof(output), "%s_run.out", executable);
    snprintf(errors, sizeof(errors), "%s_run.err", executable);
    char* arguments[] = {
        (char*)"env",
        environment,
        executable,
        0,
    };
    const bool ran = object_test_run(arguments, output, errors) == 42;
    const String8 printed = foreign_test_read(arena, output);
    const char* expected = fixture == 3 ? "foreign dynamic\n" : "";
    return ran && printed.pointer != 0 && printed.length == strlen(expected) && memcmp(printed.pointer, expected, printed.length) == 0;
}

static void foreign_dynamic_test_oracles(ForeignTestTally* tally, const char* directory, const ForeignTestLine* line, uint32_t fixture, Arena* arena)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t linker = 0; linker < BUSTER_ARRAY_LENGTH(linkers); linker += 1)
    {
        const uint64_t saved = arena->length;
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fd_reference_%u_%u", directory, fixture, linker);
        snprintf(output, sizeof(output), "%s/fd_reference_%u_%u_link.out", directory, fixture, linker);
        snprintf(errors, sizeof(errors), "%s/fd_reference_%u_%u_link.err", directory, fixture, linker);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)line->their_count + 4);
        bool ready = arguments != 0;
        if (ready)
        {
            arguments[0] = (char*)linkers[linker];
            arguments[1] = (char*)"-o";
            arguments[2] = executable;
            memcpy(arguments + 3, line->theirs, (uint64_t)line->their_count * sizeof(char*));
            arguments[line->their_count + 3] = 0;
            ready = object_test_run(arguments, output, errors) == 0;
        }
        foreign_test_expect(tally, ready, "reference links dynamic foreign fixture", linkers[linker]);
        foreign_test_expect(tally, ready && foreign_dynamic_test_run(directory, executable, fixture, arena), "reference executes dynamic foreign fixture", linkers[linker]);
        arena_pop_to(arena, saved);
    }
}

static void foreign_dynamic_test_refusals(ForeignTestTally* tally, const Foreign* foreign, Arena* arena, Arena* names, String8 interpreter)
{
    const uint64_t saved = arena->length;
    const uint64_t saved_names = names->length;
    const String8 invalid[] = {
        { 0 },
        {
            .pointer = interpreter.pointer,
            .length = 0,
        },
        {
            .pointer = 0,
            .length = interpreter.length,
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        ForeignLink failed = { 0 };
        const ForeignLinkResult refused = foreign_link_build_dynamic(&failed, foreign, arena, names, invalid[i]);
        foreign_test_expect(tally, refused.error == FOREIGN_LINK_ERROR_INPUT && failed.names == 0 && failed.objects == 0 && failed.dynamic.interpreter.length == 0 &&
                            arena->length == saved && names->length == saved_names, "missing interpreter refuses before publishing conversion", 0);
    }
    Arena small = arena_reserve(4096);
    const bool filled = small.base != 0 && arena_push(&small, small.capacity, 1) != 0;
    foreign_test_expect(tally, filled, "reserve exhausted dynamic conversion arena", 0);
    if (filled)
    {
        ForeignLink failed = { 0 };
        const ForeignLinkResult refused = foreign_link_build_dynamic(&failed, foreign, &small, names, interpreter);
        foreign_test_expect(tally, refused.error == FOREIGN_LINK_ERROR_MEMORY && failed.names == 0 && failed.objects == 0 && failed.dynamic.library_count == 0 &&
                            names->length == saved_names, "exhausted arena refuses before publishing dynamic conversion", 0);
    }
    arena_release(&small);
}

static void foreign_dynamic_test_case(ForeignTestTally* tally, const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system,
                                      String8 interpreter, uint32_t fixture)
{
    const uint64_t saved = arena->length;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    const bool c_runtime = fixture == 2 || fixture == 3;
    bool ready = line != 0 && (!c_runtime || system->found);
    foreign_test_expect(tally, ready, "dynamic fixture has command room and required C runtime", 0);
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        if (c_runtime)
        {
            foreign_test_pie_begin(line, system);
        }
        else
        {
            foreign_test_add(line, (char*)"-pie", false, true);
            foreign_test_add(line, (char*)"-e", false, true);
            foreign_test_add(line, (char*)"_start", false, true);
            foreign_test_add(line, (char*)"-dynamic-linker", false, true);
            foreign_test_add(line, interpreter.pointer, false, true);
        }
        static const char* const objects[] = {
            "fd_version.o",
            "fd_multi.o",
            "fd_back_main.o",
            "fd_archive_main.o",
            "fd_weak.o",
            "fd_comdat.o",
        };
        if (fixture == 5)
        {
            foreign_test_add(line, (char*)"--as-needed", true, true);
            foreign_test_add(line, foreign_test_path(directory, "libfd-unused.so", arena), true, true);
        }
        foreign_test_add(line, foreign_test_path(directory, objects[fixture], arena), true, true);
        if (fixture <= 1 || fixture == 4)
        {
            foreign_test_add(line, (char*)"--as-needed", true, true);
            foreign_test_add(line, foreign_test_path(directory, "libfd-alpha.so", arena), true, true);
            if (fixture == 1)
            {
                foreign_test_add(line, foreign_test_path(directory, "libfd-beta.so", arena), true, true);
                foreign_test_add(line, foreign_test_path(directory, "libfd-gamma.so", arena), true, true);
            }
            foreign_test_add(line, foreign_test_path(directory, "libfd-unused.so", arena), true, true);
        }
        else if (fixture == 2)
        {
            foreign_test_add(line, foreign_test_path(directory, "libfd-back.so", arena), true, true);
        }
        else if (fixture == 3)
        {
            const char* members[] = {
                "fd_archive.o",
            };
            foreign_test_add(line, foreign_test_archive(directory, "fd_members.a", members, BUSTER_ARRAY_LENGTH(members), false, arena), true, true);
        }
        else
        {
            foreign_test_add(line, foreign_test_path(directory, "fl_first.o", arena), true, true);
            foreign_test_add(line, foreign_test_path(directory, "fl_loser.o", arena), true, true);
        }
        if (c_runtime)
        {
            foreign_test_pie_end(line, system);
        }
        for (uint32_t i = 0; i < line->our_count; i += 1)
        {
            ready = ready && line->ours[i] != 0;
        }
        ForeignCommand command = { 0 };
        ready = ready && foreign_test_command(&command, line->ours, line->our_count, arena);
        foreign_test_expect(tally, ready, "prepare dynamic foreign command", 0);
        String8 reference = { 0 };
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
            {
                const uint64_t iteration = arena->length;
                Foreign foreign;
                foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
                Arena names = arena_reserve(1ull << 28);
                if (fixture == 0 && l == 0 && foreign.failure.error == FOREIGN_ERROR_NONE)
                {
                    foreign_dynamic_test_refusals(tally, &foreign, arena, &names, interpreter);
                }
                ForeignLink converted = { 0 };
                const ForeignLinkResult built = foreign_link_build_dynamic(&converted, &foreign, arena, &names, interpreter);
                char detail[192];
                snprintf(detail, sizeof(detail), "fixture %u %s %u lanes: resolve %u convert %u object %u name %u import %u", fixture,
                         path == 0 ? "exact" : "vector", foreign_test_lanes[l], (unsigned)foreign.failure.error, (unsigned)built.error,
                         built.object, built.name, (unsigned)built.import.error);
                const bool linked = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
                foreign_test_expect(tally, linked, "convert dynamic foreign inputs", detail);
                ForeignTestPath executable;
                snprintf(executable, sizeof(executable), "%s/fd_output_%u_%u_%u", directory, fixture, path, foreign_test_lanes[l]);
                bool published = false;
                if (linked)
                {
                    if (fixture == 4)
                    {
                        const uint32_t name = foreign_test_name(&foreign, "alpha");
                        const SymbolId symbol = name != 0 ? converted.names[name - 1] : 0;
                        const bool typed = symbol != 0 && converted.link.symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE &&
                                           converted.link.symbols.version[symbol] == 0 &&
                                           (converted.link.symbols.flags[symbol] & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING)) == (BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_WEAK);
                        foreign_test_expect(tally, name != 0 && foreign.names.resolution[name - 1] == FOREIGN_RESOLUTION_SHARED && typed,
                                            "unneeded weak DSO import keeps its function type without a version", detail);
                    }
                    else if (fixture == 5)
                    {
                        const uint32_t name = foreign_test_name(&foreign, "discarded_only_missing");
                        foreign_test_expect(tally, name != 0 && foreign.names.resolution[name - 1] == FOREIGN_RESOLUTION_SHARED &&
                                            (foreign.names.flags[name - 1] & BUSTER_FOREIGN_NAME_REFERENCED) != 0 && converted.names[name - 1] == 0,
                                            "discarded reference does not import an unneeded DSO name", detail);
                    }
                    ExecutableTestWrite write = {
                        .link = &converted.link,
                        .options = {
                            .path = {
                                .pointer = executable,
                                .length = strlen(executable),
                            },
                            .entry = foreign_link_test_symbol(&foreign, &converted, "_start"),
                            .dynamic = &converted.dynamic,
                            .exact = path == 0,
                        },
                    };
                    const bool ran = lane_run(foreign_test_lanes[l], executable_test_lane_main, &write, 0);
                    published = ran && write.succeeded;
                    foreign_test_expect(tally, published, "write foreign dynamic executable", detail);
                    if (!published)
                    {
                        executable_test_print_failure(detail, &write.result);
                    }
                    if (published)
                    {
                        foreign_test_expect(tally, foreign_dynamic_test_run(directory, executable, fixture, arena), "loader executes foreign dynamic program", detail);
                        const String8 bytes = foreign_test_read(arena, executable);
                        foreign_dynamic_test_structure(tally, &converted, bytes, fixture);
                        if (path != 0 || l != 0)
                        {
                            const bool same = reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0;
                            foreign_test_expect(tally, same, "dynamic foreign output is identical across paths and lanes", detail);
                        }
                    }
                }
                arena_release(&names);
                foreign_release(&foreign);
                arena_pop_to(arena, iteration);
                if (path == 0 && l == 0)
                {
                    reference = published ? foreign_test_read(arena, executable) : (String8){ 0 };
                    foreign_test_expect(tally, reference.pointer != 0, "retain exact dynamic foreign output", 0);
                }
            }
        }
        if (ready && cross_check)
        {
            foreign_dynamic_test_oracles(tally, directory, line, fixture, arena);
        }
    }
    arena_pop_to(arena, saved);
}

static bool foreign_dynamic_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    ForeignTestTally tally = {
        .check = "foreign dynamic output",
    };
    Runtime runtime;
    const bool found = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
    foreign_test_expect(&tally, found, "find system dynamic loader", 0);
    if (found && foreign_dynamic_test_compile(directory, &tally))
    {
        for (uint32_t fixture = 0; fixture < 6; fixture += 1)
        {
            foreign_dynamic_test_case(&tally, directory, arena, cross_check, system, runtime.loader_path, fixture);
        }
    }
    runtime_release(&runtime);
    return foreign_test_report(&tally);
}
