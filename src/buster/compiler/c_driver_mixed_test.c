// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original argv-level regressions for #1120. Clang and ar only produce independent foreign inputs; all driver
// invocations and executions use a private directory. Included after c_driver_compatibility_test.c's process helpers.
#ifdef BUSTER_DRIVER_MIXED_STANDALONE
#define _GNU_SOURCE
#include "c_driver_compatibility_test.c"
#endif

static void cc_mixed_command(CCCompatibilityTest* test, const char* name, const char* const* arguments, const char* input, bool execute)
{
    CCCompatibilityRun run = cc_compat_run(test, test->compiler, input, arguments);
    cc_compat_expect(test, run.status == 0, name);
    if (run.status != 0)
    {
        fprintf(stderr, "  diagnostics: %s\n", run.errors != NULL ? run.errors : "(missing)");
    }
    if (run.status == 0 && execute)
    {
        const char* const none[] = { NULL };
        CCCompatibilityRun program = cc_compat_run(test, "./program", NULL, none);
        cc_compat_expect(test, program.status == 0, "mixed executable returns the independently expected value");
        if (program.status != 0)
        {
            fprintf(stderr, "  executable status %d: %s\n", program.status, program.errors != NULL ? program.errors : "(missing)");
        }
        cc_compat_release(&program);
    }
    cc_compat_release(&run);
}

static void cc_mixed_refusal(CCCompatibilityTest* test, const char* name, const char* const* arguments, const char* diagnostic)
{
    const bool prepared = cc_compat_write(test, "program", "previous executable\n") && cc_compat_write(test, "keep.d", "previous dependencies\n");
    CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, arguments);
    char* executable = cc_compat_read(test, "program");
    char* dependency = cc_compat_read(test, "keep.d");
    cc_compat_expect(test, prepared && run.status != 0 && run.errors != NULL && strstr(run.errors, diagnostic) != NULL &&
                           executable != NULL && strcmp(executable, "previous executable\n") == 0 &&
                           dependency != NULL && strcmp(dependency, "previous dependencies\n") == 0, name);
    if (run.status == 0 || run.errors == NULL || strstr(run.errors, diagnostic) == NULL)
    {
        fprintf(stderr, "  status %d: %s\n", run.status, run.errors != NULL ? run.errors : "(missing)");
    }
    free(executable);
    free(dependency);
    cc_compat_release(&run);
}

static void cc_mixed_unsupported_sources(CCCompatibilityTest* test)
{
    static const char* const suffixes[] = {
        ".cpp",
        ".cc",
        ".cxx",
        ".C",
        ".m",
        ".mm",
        ".cu",
        ".hip",
        ".sycl",
        ".ii",
        ".iih",
        ".bc",
        ".ll",
        ".ads",
        ".asm",
        ".hpp",
        ".so.cpp",
        ".so.ll",
    };
    for (uint32_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i += 1)
    {
        char path[64];
        char diagnostic[128];
        snprintf(path, sizeof(path), "unsupported%s", suffixes[i]);
        snprintf(diagnostic, sizeof(diagnostic), "unsupported input language for '%s'", path);
        cc_compat_expect(test, cc_compat_write(test, path, "int value;\n"), "write unsupported-language source");
        const char* const syntax[] = { "-fsyntax-only", path, "-o", "program", NULL };
        const char* const object[] = { "-c", path, "-o", "program", NULL };
        const char* const link[] = { path, "-o", "program", NULL };
        cc_mixed_refusal(test, "syntax checking refuses unsupported source suffixes", syntax, diagnostic);
        cc_mixed_refusal(test, "object emission refuses unsupported source suffixes", object, diagnostic);
        cc_mixed_refusal(test, "linking refuses unsupported source suffixes before opening outputs", link, diagnostic);
        const char* const explicit_c[] = { "-x", "c", "-fsyntax-only", path, NULL };
        cc_mixed_command(test, "explicit C language overrides an unsupported source suffix", explicit_c, NULL, false);
    }
    const char* const reset[] = { "-xc", "helper.c", "-xnone", "unsupported.cpp", "-fsyntax-only", "-o", "program", NULL };
    const char* const after[] = { "unsupported.cpp", "-xc", "helper.c", "-fsyntax-only", "-o", "program", NULL };
    const char* const end_options[] = { "-fsyntax-only", "-o", "program", "--", "unsupported.cpp", NULL };
    cc_mixed_refusal(test, "-x none restores unsupported-source classification", reset, "unsupported input language for 'unsupported.cpp'");
    cc_mixed_refusal(test, "a later -x cannot change an earlier unsupported source", after, "unsupported input language for 'unsupported.cpp'");
    cc_mixed_refusal(test, "unsupported suffixes are refused after --", end_options, "unsupported input language for 'unsupported.cpp'");
}

static int driver_mixed_tests(const char* compiler, const char* clang)
{
    CCCompatibilityTest test = { .clang = clang != NULL ? clang : "clang" };
    snprintf(test.directory, sizeof(test.directory), "/tmp/buster-mixed-XXXXXX");
    const bool ready = realpath(compiler != NULL ? compiler : "/proc/self/exe", test.compiler) != NULL && mkdtemp(test.directory) != NULL;
    cc_compat_expect(&test, ready, "prepare mixed-input fixtures");
    if (ready)
    {
        const char* main_source = "extern int answer(void); int from_c(void) { return 2; } int main(void) { return answer() != 42; }\n";
        const char* helper_source = "int answer(void) { return 42; }\n";
        const char* assembly = ".text\n.globl answer\n.type answer,@function\nanswer:\nsub $8,%rsp\ncall from_c\nadd $8,%rsp\nadd $40,%eax\nret\n.size answer,.-answer\n.section .note.GNU-stack,\"\",@progbits\n";
        const char* preprocessing = "#ifndef __ASSEMBLER__\n#error assembly mode required\n#endif\n#include \"number.h\"\n.text\n.globl answer\n.type answer,@function\nanswer:\nmov $VALUE,%eax\nret\n.size answer,.-answer\n.section .note.GNU-stack,\"\",@progbits\n";
        bool files = cc_compat_write(&test, "main.c", main_source) && cc_compat_write(&test, "helper.c", helper_source) &&
                     cc_compat_write(&test, "answer.s", assembly) && cc_compat_write(&test, "answer.S", preprocessing) &&
                     cc_compat_write(&test, "number.h", "#define VALUE (BASE + 2)\n") &&
                     cc_compat_write(&test, "source.s", main_source) && cc_compat_write(&test, "raw.txt", assembly) &&
                     cc_compat_write(&test, "preprocessed.txt", preprocessing) &&
                     cc_compat_write(&test, "unused.c", "extern int unavailable(void); int unused(void) { return unavailable(); }\n") &&
                     cc_compat_write(&test, "bad.s", ".text\nthis_is_not_an_instruction\n") &&
                     cc_compat_write(&test, "fatal.S", "#error FIRST_ASSEMBLY_ERROR\n") &&
                     cc_compat_write(&test, "later.c", "#error LATER_C_ERROR\n") &&
                     cc_compat_write(&test, "broken.o", "not an ELF object or valid linker script\n") &&
                     cc_compat_write(&test, "extra.S", "#ifndef __ASSEMBLER__\n#error wrong mode\n#endif\n.data\n.globl extra\nextra: .long BASE\n");
        cc_compat_expect(&test, files, "write mixed-input fixtures");
        const char* const clang_object[] = { "-c", "helper.c", "-o", "clang.o", NULL };
        const char* const clang_unused[] = { "-c", "unused.c", "-o", "unused.o", NULL };
        const char* const clang_shared[] = { "-shared", "-fPIC", "helper.c", "-Wl,-soname,libanswer.so", "-o", "libanswer.so", NULL };
        const char* const clang_versioned[] = { "-shared", "-fPIC", "helper.c", "-Wl,-soname,libanswer.so", "-o", "libanswer.so.1", NULL };
        const char* const* oracle_commands[] = { clang_object, clang_unused, clang_shared, clang_versioned };
        for (uint32_t i = 0; i < sizeof(oracle_commands) / sizeof(oracle_commands[0]); i += 1)
        {
            CCCompatibilityRun oracle = cc_compat_run(&test, test.clang, NULL, oracle_commands[i]);
            cc_compat_expect(&test, oracle.status == 0, "Clang produces an independent foreign fixture");
            cc_compat_release(&oracle);
        }
        const char* const archive_arguments[] = { "rcs", "libanswer.a", "clang.o", "unused.o", NULL };
        CCCompatibilityRun archive = cc_compat_run(&test, "ar", NULL, archive_arguments);
        cc_compat_expect(&test, archive.status == 0, "archive includes an unneeded member with an unresolved reference");
        cc_compat_release(&archive);
        const char* const compile_main[] = { "-c", "main.c", "-o", "main.o", NULL };
        const char* const compile_helper[] = { "-c", "helper.c", "-o", "helper.o", NULL };
        const char* const compile_unknown[] = { "-c", "helper.c", "-o", "helper.blob", NULL };
        const char* const compile_dash_name[] = { "-c", "helper.c", "-o", "-E", NULL };
        cc_mixed_command(&test, "Buster main object", compile_main, NULL, false);
        cc_mixed_command(&test, "Buster helper object", compile_helper, NULL, false);
        cc_mixed_command(&test, "Buster object with an unknown suffix", compile_unknown, NULL, false);
        cc_mixed_command(&test, "Buster object named like an option", compile_dash_name, NULL, false);
        const char* const cases[][20] = {
            { "main.c", "helper.o", "-o", "program" },
            { "main.o", "helper.o", "-o", "program" },
            { "main.c", "clang.o", "-o", "program" },
            { "main.c", "libanswer.a", "-o", "program" },
            { "libanswer.a", "main.c", "-o", "program" },
            { "main.c", "answer.s", "-o", "program" },
            { "main.c", "answer.S", "-DBASE=40", "-o", "program" },
            { "main.c", "answer.S", "extra.S", "-DBASE=40", "--lanes", "4", "-o", "program" },
            { "main.c", "answer.s", "--scalar", "--lanes", "1", "-o", "program" },
            { "main.c", "answer.s", "--lanes", "4", "--worker-budget", "1", "-o", "program" },
            { "main.c", "answer.s", "--lanes", "4", "--worker-budget", "2", "-o", "program" },
            { "main.c", "answer.s", "-g", "-o", "program" },
            { "-x", "c", "source.s", "-x", "none", "helper.o", "-o", "program" },
            { "main.c", "-x", "assembler", "raw.txt", "-o", "program" },
            { "main.c", "-xassembler-with-cpp", "preprocessed.txt", "-DBASE=40", "-o", "program" },
            { "-x", "assembler", "raw.txt", "-xnone", "main.c", "-o", "program" },
            { "main.c", "helper.blob", "-o", "program" },
            { "-o", "program", "--", "main.o", "-E" },
            { "main.c", "./libanswer.so", "-Wl,-rpath,$ORIGIN", "-o", "program" },
            { "main.c", "./libanswer.so.1", "-Wl,-rpath,$ORIGIN", "-o", "program" },
            { "main.o", "-L.", "-l:libanswer.a", "-o", "program" },
            { "-static", "main.c", "helper.o", "-o", "program" },
            { "-static-pie", "main.c", "helper.o", "-o", "program" },
            { "main.c", "helper.c", "-o", "program" },
        };
        for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
        {
            char name[80];
            snprintf(name, sizeof(name), "mixed-input case %u (%s)", i + 1, cases[i][0]);
            cc_mixed_command(&test, name, cases[i], NULL, true);
        }
        const char* const stdin_c[] = { "-x", "c", "-", "-x", "none", "helper.o", "-o", "program", NULL };
        const char* const stdin_asm[] = { "main.c", "-x", "assembler", "-", "-o", "program", NULL };
        cc_mixed_command(&test, "positional C stdin and object", stdin_c, "main.c", true);
        cc_mixed_command(&test, "assembly stdin", stdin_asm, "answer.s", true);
        const char* const assembly_object[] = { "-x", "assembler", "-c", "raw.txt", "-o", "answer.o", NULL };
        const char* const mixed_objects[] = { "-c", "main.c", "answer.s", "--lanes", "4", NULL };
        const char* const link_objects[] = { "main.o", "answer.o", "-o", "program", NULL };
        cc_mixed_command(&test, "explicit assembler compile-only", assembly_object, NULL, false);
        cc_mixed_command(&test, "compile C and assembly separately in one command", mixed_objects, NULL, false);
        cc_mixed_command(&test, "link mixed -c output", link_objects, NULL, true);
        const char* const unused_input[] = { "-c", "main.c", "helper.o", "-o", "main.o", NULL };
        cc_mixed_command(&test, "one source and unused linker input permit -c -o", unused_input, NULL, false);
        const char* const dependency_arguments[] = { "main.c", "answer.S", "-DBASE=40", "-MMD", "-MF", "mixed.d", "-o", "program", NULL };
        cc_mixed_command(&test, "preprocessed assembly dependency publication follows argv order", dependency_arguments, NULL, true);
        char* dependencies = cc_compat_read(&test, "mixed.d");
        cc_compat_expect(&test, dependencies != NULL && strstr(dependencies, "answer.S") != NULL && strstr(dependencies, "number.h") != NULL,
                         "assembly header appears in dependency output");
        free(dependencies);
        const char* const duplicate[] = { "main.c", "helper.o", "clang.o", "-MMD", "-MF", "keep.d", "-o", "program", NULL };
        const char* const missing[] = { "main.o", "-o", "program", NULL };
        const char* const malformed[] = { "main.c", "broken.o", "-o", "program", NULL };
        const char* const bad_assembly[] = { "main.c", "bad.s", "-MMD", "-MF", "keep.d", "-o", "program", NULL };
        const char* const bad_language[] = { "-x", "c++", "main.c", "-o", "program", NULL };
        const char* const language_reset[] = { "-x", "c", "-x", "none", "-", "-o", "program", NULL };
        const char* const missing_link_value[] = { "-Wl,-e", "main.c", "helper.o", "-o", "program", NULL };
        const char* const multiple_output[] = { "-c", "main.c", "answer.s", "-o", "program", NULL };
        cc_mixed_refusal(&test, "duplicate definition preserves outputs", duplicate, "answer");
        cc_mixed_refusal(&test, "object-only unresolved reference preserves output", missing, "answer");
        cc_mixed_refusal(&test, "malformed object uses linker diagnostic", malformed, "link-foreign:");
        cc_mixed_refusal(&test, "assembly error preserves outputs", bad_assembly, "bad.s:");
        cc_mixed_refusal(&test, "unsupported language is explicit", bad_language, "input language");
        cc_mixed_refusal(&test, "-x none cancels stdin language", language_reset, "requires -x");
        cc_mixed_refusal(&test, "linker option cannot consume a source operand", missing_link_value, "missing");
        cc_mixed_refusal(&test, "mixed multiple -c -o rejected", multiple_output, "multiple output");
        cc_mixed_unsupported_sources(&test);
        const char* const fatal_order[] = { "-c", "-Wfatal-errors", "fatal.S", "later.c", NULL };
        CCCompatibilityRun fatal = cc_compat_run(&test, test.compiler, NULL, fatal_order);
        cc_compat_expect(&test, fatal.status != 0 && fatal.errors != NULL && strstr(fatal.errors, "FIRST_ASSEMBLY_ERROR") != NULL &&
                                strstr(fatal.errors, "LATER_C_ERROR") == NULL,
                         "fatal assembly preprocessing stops later units' diagnostics");
        cc_compat_release(&fatal);
        cc_compat_cleanup(&test);
    }
    printf("mixed driver: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures != 0;
}

#ifdef BUSTER_DRIVER_MIXED_STANDALONE
int main(int argc, char** argv)
{
    return argc >= 2 && argc <= 3 ? driver_mixed_tests(argv[1], argc == 3 ? argv[2] : "clang") : 2;
}
#endif
