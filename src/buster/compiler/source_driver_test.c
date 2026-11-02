// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real default CLI, not the explicit check/assemble/link-foreign subcommands. All fixtures live in a private
// working directory; child processes isolate terminal commands' parked workers. Clang supplies independent objects
// and a shared library, and ar supplies an archive with an unneeded member whose reference must remain unlinked.

static bool source_driver_test_command(CheckTest* test, const char* label, char** arguments, int expected, const char* diagnostic, bool execute)
{
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    bool valid = status == expected && (diagnostic == NULL || (text != NULL && strstr(text, diagnostic) != NULL));
    if (!valid)
    {
        fprintf(stderr, "source driver: %s: exit %d, expected %d\n%s", label, status, expected, text != NULL ? text : "no diagnostics\n");
    }
    free(text);
    if (valid && execute)
    {
        char* run[] = { "./program", NULL };
        const int ran = check_test_driver_spawn(run, &text);
        valid = ran == 0;
        if (!valid)
        {
            fprintf(stderr, "source driver: %s: executable exited %d\n%s", label, ran, text != NULL ? text : "");
        }
        free(text);
    }
    check_test_expect(test, valid, label);
    return valid;
}

static int run_source_driver_tests(void)
{
    CheckTest test = { 0 };
    char executable[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    const int saved = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char directory[] = "/tmp/buster-source-driver-XXXXXX";
    const bool made = mkdtemp(directory) != NULL;
    bool ready = length > 0 && saved >= 0 && made && chdir(directory) == 0;
    if (length > 0)
    {
        executable[length] = 0;
    }
    static const struct
    {
        const char* name;
        const char* text;
    } fixtures[] = {
        { "main.c", "int answer(void); int main(void) { return answer() != 42; }\n" },
        { "answer.c", "int answer(void) { return 42; }\n" },
        { "answer.s", ".text\n.globl answer\nanswer:\n movl $42, %eax\n ret\n.section .note.GNU-stack,\"\",@progbits\n" },
        { "macro.S", "#include \"value.h\"\n.text\n.globl answer\nanswer:\n movl $VALUE, %eax\n ret\n.section .note.GNU-stack,\"\",@progbits\n" },
        { "value.h", "#define VALUE (BASE + 2)\n" },
        { "code.s", "int answer(void) { return 42; }\n" },
        { "extensionless", ".text\n.globl answer\nanswer:\n movl $42, %eax\n ret\n.section .note.GNU-stack,\"\",@progbits\n" },
        { "plain.i", "# 1 \"original.c\"\nint answer(void) { return 42; }\n" },
        { "bad.c", "int main(void) { return nonexistent; }\n" },
        { "bad.s", ".not_an_assembly_directive\n" },
        { "broken.o", "not an ELF object\n" },
        { "unneeded.c", "int never_defined(void); int unused_member(void) { return never_defined(); }\n" },
        { "other.c", "int answer(void) { return 7; }\n" },
        { "extra.c", "int extra(void) { return 0; }\n" },
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        ready = parse_test_write(fixtures[i].name, fixtures[i].text, strlen(fixtures[i].text));
    }
    check_test_expect(&test, ready, "mixed-input driver fixtures are available");
    if (ready)
    {
        char* own[] = { executable, "-c", "answer.s", "-o", "own.o", NULL };
        char* c_object[] = { executable, "--lanes", "1", "-c", "answer.c", "-o", "own-c.o", NULL };
        char* foreign[] = { "clang", "-c", "answer.c", "-o", "clang.o", NULL };
        char* raw[] = { "clang", "-c", "answer.c", "-o", "answer.raw", NULL };
        char* unused[] = { "clang", "-c", "unneeded.c", "-o", "unneeded.o", NULL };
        char* other[] = { "clang", "-c", "other.c", "-o", "other.o", NULL };
        char* archive[] = { "ar", "rcs", "libanswer.a", "clang.o", "unneeded.o", NULL };
        char* archive_other[] = { "ar", "rcs", "libother.a", "other.o", NULL };
        source_driver_test_command(&test, "default -c assembles plain .s", own, 0, NULL, false);
        source_driver_test_command(&test, "produce a Buster C object", c_object, 0, NULL, false);
        source_driver_test_command(&test, "independent Clang object", foreign, 0, NULL, false);
        source_driver_test_command(&test, "independent object with an unknown suffix", raw, 0, NULL, false);
        source_driver_test_command(&test, "archive's unused member has an unresolved reference", unused, 0, NULL, false);
        source_driver_test_command(&test, "second archive's alternate definition", other, 0, NULL, false);
        source_driver_test_command(&test, "independent archive", archive, 0, NULL, false);
        source_driver_test_command(&test, "independent second archive", archive_other, 0, NULL, false);
        const char* inputs[] = { "own.o", "own-c.o", "clang.o", "libanswer.a", "answer.s", "answer.raw" };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
            {
                char label[128];
                snprintf(label, sizeof(label), "C plus %s links and runs (%s)", inputs[i], scalar ? "scalar" : "default");
                char* mixed[] = { executable, scalar ? "--scalar" : "--verify", "--lanes", "1", "-o", "program", "main.c", (char*)inputs[i], NULL };
                source_driver_test_command(&test, label, mixed, 0, NULL, true);
            }
        }
        char* cpp[] = { executable, "--lanes", "1", "-DBASE=40", "-I", ".", "main.c", "macro.S", "-o", "program", NULL };
        source_driver_test_command(&test, ".S uses command-line macros and includes", cpp, 0, NULL, true);
        char* positional[] = { executable, "--lanes", "1", "main.c", "-x", "assembler", "extensionless", "-x", "none", "extra.c", "-o", "program", NULL };
        source_driver_test_command(&test, "-x assembler is positional and -x none restores C suffixes", positional, 0, NULL, true);
        char* force_c[] = { executable, "--lanes", "1", "main.c", "-xc", "code.s", "-xnone", "-o", "program", NULL };
        source_driver_test_command(&test, "joined -xc overrides .s without changing an earlier input", force_c, 0, NULL, true);
        char* force_cpp[] = { executable, "--lanes", "1", "main.c", "-DBASE=40", "-x", "assembler-with-cpp", "macro.S", "-o", "program", NULL };
        source_driver_test_command(&test, "-x assembler-with-cpp uses the preprocessor", force_cpp, 0, NULL, true);
        char* force_plain[] = { executable, "--lanes", "1", "main.c", "-x", "assembler", "macro.S", "-o", "rejected", NULL };
        source_driver_test_command(&test, "-x assembler overrides .S preprocessing", force_plain, 1, NULL, false);
        // Compile the preprocessed input independently: the deliberately destructive -D must not rename its symbol.
        char* plain[] = { executable, "--lanes", "1", "-Danswer=wrong", "-include", "nonexistent.h", "-x", "cpp-output", "-c", "plain.i", "-o", "plain.o", NULL };
        source_driver_test_command(&test, "cpp-output ignores command-line macros and forced includes", plain, 0, NULL, false);
        char* link_plain[] = { executable, "--lanes", "1", "main.c", "plain.o", "-o", "program", NULL };
        source_driver_test_command(&test, "preprocessed C object retains its identifier", link_plain, 0, NULL, true);
        // Compile .i separately so -D does not change main.c's expected external name.
        char* suffix_object[] = { executable, "--lanes", "1", "-Danswer=wrong", "-c", "plain.i", "-o", "plain.o", NULL };
        source_driver_test_command(&test, ".i suffix ignores command-line macros", suffix_object, 0, NULL, false);
        source_driver_test_command(&test, ".i suffix retains its identifier", link_plain, 0, NULL, true);
        char* parallel[] = { executable, "--lanes", "2", "--poison-arenas", "main.c", "extra.c", "own.o", "-o", "program", NULL };
        source_driver_test_command(&test, "parallel captured C objects survive worker arena release", parallel, 0, NULL, true);
        check_test_expect(&test, access("main.o", F_OK) != 0 && access("extra.o", F_OK) != 0, "mixed linking writes no temporary object files");
        char* first_archive[] = { executable, "--lanes", "1", "main.c", "libanswer.a", "libother.a", "-o", "program", NULL };
        source_driver_test_command(&test, "first archive definition wins and unused members stay out", first_archive, 0, NULL, true);
        char* object_only[] = { executable, "--lanes", "1", "-c", "main.c", "-o", "main.o", NULL };
        source_driver_test_command(&test, "compile main for object-only link", object_only, 0, NULL, false);
        char* only[] = { executable, "--lanes", "1", "main.o", "own.o", "-o", "program", NULL };
        source_driver_test_command(&test, "object-only invocation links without a C frontend", only, 0, NULL, true);
        char* via_library[] = { executable, "--lanes", "1", "main.o", "-L", ".", "-l", "answer", "-o", "program", NULL };
        source_driver_test_command(&test, "object and -l archive use the default link driver", via_library, 0, NULL, true);
        char* mixed_compile[] = { executable, "--lanes", "1", "-c", "main.c", "answer.s", NULL };
        source_driver_test_command(&test, "mixed -c emits each source's object", mixed_compile, 0, NULL, false);
        check_test_expect(&test, access("main.o", F_OK) == 0 && access("answer.o", F_OK) == 0, "mixed -c wrote both frontend outputs");
        char* unknown_option[] = { executable, "--unknown-driver-option", "main.o", "own.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "object-only link rejects unknown options", unknown_option, 1, "unknown argument", false);
        char* raw_linker[] = { executable, "-Wl,--no-such-linker-option", "main.o", "own.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "raw linker options are not silently discarded", raw_linker, 1, "unsupported linker option", false);
        char* compile_asm[] = { executable, "-x", "assembler", "-c", "extensionless", "-o", "forced.o", NULL };
        source_driver_test_command(&test, "explicit -x assembler -c is accepted", compile_asm, 0, NULL, false);
        char* unused_link[] = { executable, "-c", "own.o", "-o", "unused-output", NULL };
        source_driver_test_command(&test, "-c does not parse or overwrite a linker input", unused_link, 0, "input unused", false);
        check_test_expect(&test, access("unused-output", F_OK) != 0, "unused linker input does not create output");
        char* multi[] = { executable, "-c", "main.c", "answer.s", "-o", "rejected", NULL };
        source_driver_test_command(&test, "-o with multiple emitting inputs is rejected", multi, 1, "multiple output", false);
        char* missing[] = { executable, "--lanes", "1", "main.c", "missing.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "missing object gets a linker diagnostic", missing, 1, "link-foreign:", false);
        char* malformed[] = { executable, "--lanes", "1", "main.c", "broken.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "malformed object is never treated as C", malformed, 1, "link-foreign:", false);
        char* duplicate[] = { executable, "--lanes", "1", "main.c", "own.o", "clang.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "duplicate definitions fail before writing output", duplicate, 1, NULL, false);
        char* bad_c[] = { executable, "--lanes", "1", "bad.c", "own.o", "-o", "rejected", NULL };
        source_driver_test_command(&test, "C diagnostics stop a mixed link", bad_c, 1, "error:", false);
        char* bad_asm[] = { executable, "--lanes", "1", "main.c", "bad.s", "-o", "rejected", NULL };
        source_driver_test_command(&test, "assembly diagnostics stop a mixed link", bad_asm, 1, "error:", false);
        char* bad_language[] = { executable, "-x", "c++", "main.c", "-o", "rejected", NULL };
        source_driver_test_command(&test, "unsupported -x language is diagnosed", bad_language, 1, "unsupported language", false);
        char* missing_language[] = { executable, "main.c", "-x", NULL };
        source_driver_test_command(&test, "missing -x operand is diagnosed", missing_language, 1, "missing argument", false);
        char* option_operand[] = { executable, "--lanes", "1", "-c", "main.c", "-o", "-not-input.s", NULL };
        source_driver_test_command(&test, "an option operand ending in .s is not an assembly input", option_operand, 0, NULL, false);
        check_test_expect(&test, access("-not-input.s", F_OK) == 0, "dash-prefixed output operand is preserved");
        check_test_expect(&test, access("rejected", F_OK) != 0, "all rejected links leave no executable");
        char shared[PATH_MAX];
        snprintf(shared, sizeof(shared), "%s/libanswer.so.7", directory);
        char* shared_build[] = { "clang", "-shared", "-fPIC", "answer.c", "-o", shared, NULL };
        source_driver_test_command(&test, "independent versioned shared library", shared_build, 0, NULL, false);
        char* shared_link[] = { executable, "--lanes", "1", "main.c", shared, "-o", "program", NULL };
        source_driver_test_command(&test, "versioned shared-library path is a linker input", shared_link, 0, NULL, true);
    }
    if (ready)
    {
        DIR* files = opendir(".");
        struct dirent* file;
        while (files != NULL && (file = readdir(files)) != NULL)
        {
            if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0)
            {
                unlink(file->d_name);
            }
        }
        if (files != NULL)
        {
            closedir(files);
        }
    }
    if (saved >= 0)
    {
        check_test_expect(&test, fchdir(saved) == 0, "restore the original working directory");
        close(saved);
    }
    if (made)
    {
        rmdir(directory);
    }
    printf("source driver: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0 ? 1 : 0;
}
