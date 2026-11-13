// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source links whose selected named archives use the same foreign resolver as positional archives (#1470). Included
// after the mixed-driver helpers. Clang/ar make independent archive and DSO inputs; Buster links and runs them.
static void cc_mixed_library_needed(CCCompatibilityTest* test, const char* expected, const char* description)
{
    const char* const dynamic[] = { "-d", "program", NULL };
    CCCompatibilityRun needed = cc_compat_run(test, "readelf", NULL, dynamic);
    const bool correct = needed.status == 0 && needed.output != NULL && strstr(needed.output, expected) != NULL;
    cc_compat_expect(test, correct, description);
    if (!correct)
    {
        fprintf(stderr, "  readelf status %d, expected %s:\n%s\n%s\n", needed.status, expected,
                needed.output != NULL ? needed.output : "(missing output)", needed.errors != NULL ? needed.errors : "(missing errors)");
    }
    cc_compat_release(&needed);
}

static void cc_mixed_named_libraries(CCCompatibilityTest* test)
{
    bool files = cc_compat_write(test, "named-main.c", "#ifndef EXPECTED\n#define EXPECTED 42\n#endif\nextern int answer(void); int main(void) { return answer() != EXPECTED; }\n") &&
                 cc_compat_write(test, "named-static.c", "int answer(void) { return 17; }\n") &&
                 cc_compat_write(test, "named-head.c", "extern int tail(void); int answer(void) { return tail() + 2; }\n") &&
                 cc_compat_write(test, "named-tail.c", "int tail(void) { return 40; }\n") &&
                 cc_compat_write(test, "named-extra.c", "int extra(void) { return 3; }\n") &&
                 cc_compat_write(test, "libscript2548.a", "/* A static library can be a linker script, as glibc's libm.a is. */\nGROUP ( libchoice1470.a )\n") &&
                 cc_compat_write(test, "libshort2548.a", "INPUT(a)") &&
                 cc_compat_write(test, "named-optional.c", "extern int puts(const char *); __attribute__((constructor)) static void loaded(void) { puts(\"optional loaded\"); } int optional(void) { return 0; }\n");
    cc_compat_expect(test, files, "write named-library source fixtures");
    const char* const objects[][12] = {
        { "-c", "named-static.c", "-o", "named-static.o", NULL },
        { "-c", "named-static.c", "-o", "a", NULL },
        { "-c", "named-head.c", "-o", "named-head.o", NULL },
        { "-c", "named-tail.c", "-o", "named-tail.o", NULL },
        { "-shared", "-fPIC", "helper.c", "-Wl,-soname,libchoice1470.so", "-o", "libchoice1470.so", NULL },
        { "-shared", "-fPIC", "helper.c", "-Wl,-soname,libscript2548.so", "-o", "libscript2548.so", NULL },
        { "-shared", "-fPIC", "helper.c", "-o", "libnoname1470.so", NULL },
        { "-shared", "-fPIC", "helper.c", "-Wl,-soname,libcontract1470.so", "-o", "libsoname1470.so", NULL },
        { "-shared", "-fPIC", "named-optional.c", "-Wl,-soname,liboptional1470.so", "-o", "liboptional1470.so", NULL },
    };
    for (uint32_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i += 1)
    {
        CCCompatibilityRun oracle = cc_compat_run(test, test->clang, NULL, objects[i]);
        cc_compat_expect(test, oracle.status == 0, "Clang produces named-library object or DSO");
        cc_compat_release(&oracle);
    }
    const char* const archives[][8] = {
        // The unused member's unresolved reference must not be pulled into any of these links.
        { "rcs", "libarchiveonly1470.a", "clang.o", "unused.o", NULL },
        { "rcs", "libchoice1470.a", "named-static.o", "unused.o", NULL },
        { "rcs", "libhead1470.a", "named-head.o", "unused.o", NULL },
        { "rcs", "libtail1470.a", "named-tail.o", NULL },
        { "rcs", "libfirst1470.a", "clang.o", NULL },
        { "rcs", "libsecond1470.a", "named-static.o", NULL },
        { "rcs", "named-renamed.data", "clang.o", "unused.o", NULL },
        { "rcs", "libempty1470.a", NULL },
    };
    for (uint32_t i = 0; i < sizeof(archives) / sizeof(archives[0]); i += 1)
    {
        CCCompatibilityRun archive = cc_compat_run(test, "ar", NULL, archives[i]);
        cc_compat_expect(test, archive.status == 0, "create independent named-library archive");
        cc_compat_release(&archive);
    }
    const char* const cases[][24] = {
        { "main.c", "-L.", "-larchiveonly1470", "-o", "program", NULL },
        { "main.c", "-L", ".", "-l", "archiveonly1470", "-o", "program", NULL },
        { "main.c", "-L.", "-l:libanswer.a", "-o", "program", NULL },
        { "main.c", "-L.", "-larchiveonly1470", "--scalar", "--lanes", "1", "-o", "program", NULL },
        { "main.c", "-lhead1470", "named-extra.c", "-ltail1470", "-L.", "--lanes", "4", "--worker-budget", "2", "-o", "program", NULL },
        { "main.c", "-L.", "-lhead1470", "-ltail1470", "-g", "-o", "program", NULL },
        { "named-main.c", "-L.", "-lfirst1470", "-lsecond1470", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-L.", "-lsecond1470", "-lfirst1470", "-o", "program", NULL },
        // One name has a DSO returning 42 and an archive returning 17: runtime mode must decide which is selected.
        { "named-main.c", "-L.", "-lchoice1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-static", "-L.", "-lchoice1470", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-static-pie", "-L.", "-lchoice1470", "-o", "program", NULL },
        // The static spelling selects a script even with a DSO of the same name, and wherever -static appears.
        { "named-main.c", "-L.", "-lscript2548", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-static", "-L.", "-lscript2548", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-L.", "-lscript2548", "-static", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-static-pie", "-L.", "-lscript2548", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-L.", "-l:libscript2548.a", "--scalar", "--lanes", "1", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-static", "-L.", "-lshort2548", "-o", "program", NULL },
        { "named-main.c", "-DEXPECTED=17", "-L.", "-l:libchoice1470.a", "-o", "program", NULL },
        { "main.c", "-L.", "-l:named-renamed.data", "-o", "program", NULL },
        { "named-main.c", "-L.", "-lchoice1470", "-lempty1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
        { "named-main.c", "-L.", "-l:libnoname1470.so", "-larchiveonly1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
        { "named-main.c", "-L.", "-lnoname1470", "-larchiveonly1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        char name[96];
        snprintf(name, sizeof(name), "source named-library link %u (%s)", i + 1, cases[i][0]);
        cc_mixed_command(test, name, cases[i], NULL, true);
    }
    cc_mixed_library_needed(test, "[libnoname1470.so]", "a named DSO beside an archive retains basename DT_NEEDED without a SONAME");
    const char* const positional[] = { "named-main.c", "./libnoname1470.so", "-L.", "-larchiveonly1470", "-o", "program", NULL };
    cc_mixed_command(test, "a positional DSO beside an archive retains its input path", positional, NULL, true);
    cc_mixed_library_needed(test, "[./libnoname1470.so]", "a positional DSO without a SONAME retains path DT_NEEDED");
    const char* const soname[] = { "named-main.c", "-L.", "-lsoname1470", "-larchiveonly1470", "-o", "program", NULL };
    cc_mixed_command(test, "a named DSO with a distinct SONAME links beside an archive", soname, NULL, false);
    cc_mixed_library_needed(test, "[libcontract1470.so]", "SONAME overrides a named DSO's search filename");
    const char* const standard_input[] = { "-x", "c", "-", "-x", "none", "-DEXPECTED=17", "-L.", "-l:libchoice1470.a", "--id-order", "7", "-o", "program", NULL };
    cc_mixed_command(test, "named archive with buffered C stdin and changed identifier order", standard_input, "named-main.c", true);
    const char* const state[][16] = {
        { "named-main.c", "-L.", "-Wl,--as-needed", "-loptional1470", "-lchoice1470", "-larchiveonly1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
        { "named-main.c", "-L.", "-Wl,--no-as-needed", "-loptional1470", "-Wl,--as-needed", "-lchoice1470", "-larchiveonly1470", "-Wl,-rpath,$ORIGIN", "-o", "program", NULL },
    };
    for (uint32_t i = 0; i < sizeof(state) / sizeof(state[0]); i += 1)
    {
        CCCompatibilityRun linked = cc_compat_run(test, test->compiler, NULL, state[i]);
        cc_compat_expect(test, linked.status == 0, "named libraries retain argv-local as-needed state and runpath");
        if (linked.status == 0)
        {
            const char* const none[] = { NULL };
            CCCompatibilityRun executed = cc_compat_run(test, "./program", NULL, none);
            const char* expected = i == 0 ? "" : "optional loaded\n";
            cc_compat_expect(test, executed.status == 0 && executed.output != NULL && strcmp(executed.output, expected) == 0,
                             "only the retained optional DSO runs its constructor");
            cc_compat_release(&executed);
        }
        cc_compat_release(&linked);
    }
    const char* const missing[] = { "named-main.c", "-L.", "-lmissing1470", "-MMD", "-MF", "keep.d", "-o", "program", NULL };
    cc_mixed_refusal(test, "missing named library preserves executable and dependency output", missing, "missing1470");
    const char* const missing_archive[] = { "named-main.c", "-L.", "-larchiveonly1470", "-lmissing1470", "-MMD", "-MF", "keep.d", "-o", "program", NULL };
    cc_mixed_refusal(test, "missing named library beside an archive preserves outputs", missing_archive, "missing1470");
    const char* const object[] = { "-c", "named-main.c", "-L.", "-lmissing1470", "-o", "named-only.o", NULL };
    const char* const syntax[] = { "-fsyntax-only", "named-main.c", "-L.", "-lmissing1470", "-o", "program", NULL };
    cc_mixed_command(test, "compile-only consumes named libraries without resolving them", object, NULL, false);
    cc_mixed_command(test, "syntax-only consumes named libraries without resolving them", syntax, NULL, false);
    char* kept = cc_compat_read(test, "program");
    cc_compat_expect(test, kept != NULL && strcmp(kept, "previous executable\n") == 0, "non-linking named-library commands preserve the existing executable");
    free(kept);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/named-only.o", test->directory);
    FILE* file = fopen(path, "rb");
    Elf64_Ehdr header = { 0 };
    const bool emitted = file != NULL && fread(&header, 1, sizeof(header), file) == sizeof(header) && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_type == ET_REL;
    if (file != NULL)
    {
        fclose(file);
    }
    cc_compat_expect(test, emitted, "compile-only with an unresolved library still emits an ELF object");
}
