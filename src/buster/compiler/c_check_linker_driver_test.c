// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1123: forwarding is an ordered linker stream, never warning flags or source filenames. Inspect the actual ELF
// tags and run programs, including a DSO outside the loader's default search. Refusals preserve existing outputs.

static int check_test_driver_linker_command(const char* executable, const char* source, const char* output, bool scalar,
                                          const char* const* options, uint32_t option_count, char** text)
{
    char* arguments[64] = { 0 };
    assert(option_count + 7 < BUSTER_ARRAY_LENGTH(arguments));
    uint32_t count = 0;
    arguments[count++] = (char*)executable;
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    for (uint32_t i = 0; i < option_count; i += 1)
    {
        arguments[count++] = (char*)options[i];
    }
    return check_test_driver_spawn(arguments, text);
}

static bool check_test_driver_linker_path(const char* path, const char* expected, bool old_dtags)
{
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    const DynamicTestImage image = dynamic_test_image(object_test_read_file(&arena, path));
    uint64_t offset = 0;
    uint64_t unused = 0;
    const Elf64_Shdr* strings = dynamic_test_section(&image, ".dynstr");
    const bool tagged = dynamic_test_tag(&image, old_dtags ? DT_RPATH : DT_RUNPATH, &offset);
    const bool other = dynamic_test_tag(&image, old_dtags ? DT_RUNPATH : DT_RPATH, &unused);
    const uint64_t length = strlen(expected);
    const bool valid = image.valid && tagged && !other && strings != NULL && offset < strings->sh_size &&
                       length < strings->sh_size - offset && memcmp(image.file.pointer + strings->sh_offset + offset, expected, length + 1) == 0;
    arena_release(&arena);
    return valid;
}

static void check_test_driver_linker(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char plain[] = "int main(void) { return 0; }\n";
    static const char alternate[] = "__attribute__((naked)) void alternate(void) { __asm__(\"mov $37,%edi; mov $60,%eax; syscall\"); }\n"
                                    "int main(void) { return 0; }\n";
    static const char sentinel[] = "unchanged linker output\n";
    char source[256];
    char output[256];
    char saved[256];
    char vector[256];
    char label[256];
    snprintf(source, sizeof(source), "%s/linker-options.c", directory);
    snprintf(output, sizeof(output), "%s/linker-options.out", directory);
    snprintf(saved, sizeof(saved), "%s/linker-options.saved", directory);
    snprintf(vector, sizeof(vector), "%s/linker-options.vector", directory);
    const bool written = parse_test_write(source, plain, sizeof(plain) - 1) && parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, written, "linker option fixtures are written");
    static const struct
    {
        const char* options[8];
        const char* path;
        bool old_dtags;
    } paths[] = {
        { { "-Wl,-rpath,/first,-rpath,/second" }, "/first:/second", false },
        { { "-Xlinker", "-rpath", "-Xlinker", "/a,b" }, "/a,b", false },
        { { "-Wl,-rpath", "-Xlinker", "$ORIGIN/../lib", "-Wl,-R/last" }, "$ORIGIN/../lib:/last", false },
        { { "-Wl,-R,/short" }, "/short", false },
        { { "-Wl,--rpath=/equals=here" }, "/equals=here", false },
        { { "-Wl,-rpath=/one", "-Wl,--rpath,/two" }, "/one:/two", false },
        { { "-Wl,--disable-new-dtags,-rpath,legacy" }, "legacy", true },
        { { "-Wl,--disable-new-dtags,--enable-new-dtags,-rpath,modern" }, "modern", false },
        { { "-Wl,-rpath," }, "", false },
        { { "-Wl,-rpath,,--rpath,/after,-R" , "-Xlinker", "" }, ":/after:", false },
    };
    for (uint32_t p = 0; written && p < BUSTER_ARRAY_LENGTH(paths); p += 1)
    {
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(paths[p].options) && paths[p].options[count] != NULL)
        {
            count += 1;
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* text = NULL;
            const char* destination = scalar == 0 ? vector : output;
            unlink(destination);
            const int status = check_test_driver_linker_command(executable, source, destination, scalar != 0, paths[p].options, count, &text);
            snprintf(label, sizeof(label), "linker path spelling %u scalar=%u", p, scalar);
            check_test_expect(test, status == 0 && check_test_driver_linker_path(destination, paths[p].path, paths[p].old_dtags), label);
            if (status != 0)
            {
                printf("  %s: %s\n", label, text != NULL ? text : "no diagnostic");
            }
            free(text);
        }
        check_test_expect(test, check_test_driver_same_files(vector, output), "linker paths produce identical scalar/vector ELF bytes");
    }
    static const struct
    {
        const char* options[4];
        const char* diagnostic;
    } refused[] = {
        { { "-Wl,--bogus-option-xyz" }, "unsupported linker option '--bogus-option-xyz'" },
        { { "-Xlinker", "--bogus-option-xyz" }, "unsupported linker option '--bogus-option-xyz'" },
        { { "-Wl,--defsym,foo=0x1234" }, "unsupported linker option '--defsym'" },
        { { "-Wl,-Map,out.map" }, "unsupported linker option '-Map'" },
        { { "-Wl,--version-script=script" }, "unsupported linker option '--version-script=script'" },
        { { "-Wl,--whole-archive" }, "unsupported linker option '--whole-archive'" },
        { { "-Wl,-soname,libignored.so" }, "unsupported linker option '-soname'" },
        { { "-Wl,--gc-sections" }, "unsupported linker option '--gc-sections'" },
        { { "-Wl,--build-id=none" }, "unsupported linker option '--build-id=none'" },
        { { "-Wl,-z,lazy" }, "unsupported linker option '-z lazy'" },
        { { "-Wl,-z,execstack" }, "unsupported linker option '-z execstack'" },
        { { "-Wl,-z,norelro" }, "unsupported linker option '-z norelro'" },
        { { "-Xlinker", "-E" }, "unsupported linker option '-E'" },
        { { "-Xlinker", "-c" }, "unsupported linker option '-c'" },
        { { "-Xlinker", "--version" }, "unsupported linker option '--version'" },
        { { "-Wl," }, "unsupported linker option ''" },
        { { "-Wl,--build-id," }, "unsupported linker option ''" },
        { { "-Xlinker" }, "missing argument to linker option '-Xlinker'" },
        { { "-Wl,-rpath" }, "missing argument to linker option '-rpath'" },
        { { "-Wl,-e" }, "missing argument to linker option '-e'" },
        { { "-Wl,-z" }, "missing argument to linker option '-z'" },
        { { "-Wl,-e,nosuch_entry" }, "linker entry 'nosuch_entry' is not a defined code symbol" },
    };
    for (uint32_t r = 0; written && r < BUSTER_ARRAY_LENGTH(refused); r += 1)
    {
        uint32_t count = 0;
        while (count < BUSTER_ARRAY_LENGTH(refused[r].options) && refused[r].options[count] != NULL)
        {
            count += 1;
        }
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t existing = 0; existing < 2; existing += 1)
            {
                unlink(output);
                const bool prepared = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                char* text = NULL;
                const int status = check_test_driver_linker_command(executable, source, output, scalar != 0, refused[r].options, count, &text);
                snprintf(label, sizeof(label), "linker refusal %u scalar=%u existing=%u", r, scalar, existing);
                check_test_expect(test, prepared && status != 0 && text != NULL && strstr(text, refused[r].diagnostic) != NULL &&
                                       (existing == 0 ? access(output, F_OK) != 0 : check_test_driver_same_files(saved, output)), label);
                free(text);
            }
        }
    }
    // Every accepted fixed-output flag is exercised individually, rather than hidden behind another parse failure.
    static const char* const defaults[] = { "-Wl,--build-id", "-Wl,--eh-frame-hdr", "-Wl,--hash-style=gnu", "-Wl,-z,now",
                                           "-Wl,-z,relro", "-Wl,-z,noexecstack", "-Wl,-z,text", "-Wl,-znow" };
    for (uint32_t d = 0; written && d < BUSTER_ARRAY_LENGTH(defaults); d += 1)
    {
        char* text = NULL;
        const int status = check_test_driver_linker_command(executable, source, output, false, defaults + d, 1, &text);
        check_test_expect(test, status == 0, defaults[d]);
        free(text);
    }
    const bool entry_written = parse_test_write(source, alternate, sizeof(alternate) - 1);
    const char* entries[][4] = {
        { "-Wl,-e,alternate" },
        { "-Xlinker", "--entry=alternate" },
        { "-Wl,--entry,missing,-e,alternate" },
    };
    for (uint32_t e = 0; entry_written && e < BUSTER_ARRAY_LENGTH(entries); e += 1)
    {
        uint32_t count = 0;
        while (entries[e][count] != NULL)
        {
            count += 1;
        }
        char* text = NULL;
        const int status = check_test_driver_linker_command(executable, source, output, false, entries[e], count, &text);
        free(text);
        text = NULL;
        char* run[] = { output, NULL };
        const int ran = status == 0 ? check_test_driver_spawn(run, &text) : -1;
        check_test_expect(test, status == 0 && ran == 37, "forwarded entry changes the executed entry point; last option wins");
        free(text);
    }
    char library_source[256];
    char library[256];
    snprintf(library_source, sizeof(library_source), "%s/linker-dso.c", directory);
    snprintf(library, sizeof(library), "%s/libbuster1123.so", directory);
    static const char dso[] = "int linker_value(void) { return 37; }\n";
    static const char consumer[] = "int linker_value(void); int main(void) { return linker_value() != 37; }\n";
    const bool dso_written = parse_test_write(library_source, dso, sizeof(dso) - 1) && parse_test_write(source, consumer, sizeof(consumer) - 1);
    char* text = NULL;
    char* build[] = { (char*)clang, "-shared", "-fPIC", "-Wl,-soname,libbuster1123.so", "-o", library, library_source, NULL };
    const int built = dso_written ? check_test_driver_spawn(build, &text) : -1;
    check_test_expect(test, built == 0, "build nonstandard runtime-path DSO with independent compiler");
    free(text);
    for (uint32_t scalar = 0; built == 0 && scalar < 2; scalar += 1)
    {
        const char* options[] = { "-L", directory, "-lbuster1123", "-Wl,-rpath,$ORIGIN" };
        text = NULL;
        const int linked = check_test_driver_linker_command(executable, source, output, scalar != 0, options, BUSTER_ARRAY_LENGTH(options), &text);
        free(text);
        text = NULL;
        char* run[] = { output, NULL };
        const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
        check_test_expect(test, linked == 0 && ran == 0 && check_test_driver_linker_path(output, "$ORIGIN", false),
                          "RUNPATH loads a DSO outside the default search without LD_LIBRARY_PATH");
        free(text);
    }
    const bool plain_written = parse_test_write(source, plain, sizeof(plain) - 1);
    for (uint32_t mode = 0; plain_written && built == 0 && mode < 3; mode += 1)
    {
        const char* options[] = { "-L", directory, mode == 0 ? "-Wl,--as-needed" : "-Wl,--no-as-needed", "-lbuster1123",
                                  "-Wl,--as-needed", "-lbuster1123", "-Wl,-rpath,$ORIGIN" };
        // A later no-as-needed occurrence must retain a previously seen, unreferenced DSO as well.
        options[4] = mode == 2 ? "-Wl,--no-as-needed" : "-Wl,--as-needed";
        options[2] = mode == 2 ? "-Wl,--as-needed" : options[2];
        text = NULL;
        const int linked = check_test_driver_linker_command(executable, source, output, false, options, BUSTER_ARRAY_LENGTH(options), &text);
        char needed[512];
        const bool read = linked == 0 && check_test_driver_needed(output, needed, sizeof(needed));
        check_test_expect(test, read && (strstr(needed, "libbuster1123.so") != NULL) == (mode != 0),
                          "as-needed state follows input order and repeated DSOs retain explicit no-as-needed");
        free(text);
    }
    // Forwarded operands that spell driver actions cannot select those actions. Genuine non-linking actions consume
    // the transport without interpreting its payload; -Xlinker itself still requires an operand.
    const char* actions[] = { "-c", "-fsyntax-only", "-E" };
    for (uint32_t a = 0; plain_written && a < BUSTER_ARRAY_LENGTH(actions); a += 1)
    {
        const char* options[] = { actions[a], "-Xlinker", "not-a-source.c", "-Wl,--bogus-option-xyz" };
        text = NULL;
        const int status = check_test_driver_linker_command(executable, source, output, false, options, BUSTER_ARRAY_LENGTH(options), &text);
        check_test_expect(test, status == 0, "non-linking action consumes linker-only operands");
        free(text);
    }
    unlink(library_source);
    unlink(library);
    unlink(vector);
    unlink(saved);
    unlink(output);
    unlink(source);
}
