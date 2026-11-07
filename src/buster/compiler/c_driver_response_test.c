// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real executable response-file controls for #1476, included by the existing CC compatibility harness. Valid
// argument decoding is compared with Clang; malformed/bounded-input refusals use explicit diagnostics and preserve
// an existing object, so a linker fallback or an unrelated compiler error cannot satisfy them.
static bool cc_compat_response_bytes(CCCompatibilityTest* test, const char* name, const void* bytes, size_t length)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    FILE* file = fopen(path, "wb");
    bool valid = file != NULL && fwrite(bytes, 1, length, file) == length;
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static void cc_compat_response_failure(CCCompatibilityTest* test, const char* name, const char* argument, const char* reason)
{
    const bool prepared = cc_compat_write(test, "rsp-kept.o", "previous object\n");
    const char* const arguments[] = { "-c", "rsp source.c", "-o", "rsp-kept.o", argument, NULL };
    CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, arguments);
    char* kept = cc_compat_read(test, "rsp-kept.o");
    cc_compat_expect(test, prepared && run.status != 0 && run.errors != NULL && strstr(run.errors, "response file") != NULL && strstr(run.errors, reason) != NULL &&
                           strstr(run.errors, "unrecognized suffix") == NULL && kept != NULL && strcmp(kept, "previous object\n") == 0, name);
    free(kept);
    cc_compat_release(&run);
}

static void cc_compat_response_files(CCCompatibilityTest* test)
{
    bool files = cc_compat_write(test, "rsp source.c", "#if VALUE != 29\n#error wrong response argument order\n#endif\nint response(void) { return VALUE; }\n") &&
                 cc_compat_write(test, "rsp-expand.c", "VALUE WORD TEXT\n") &&
                 cc_compat_write(test, "rsp-syntax.rsp", "-fsyntax-only -DVALUE=29 'rsp source.c'\n") &&
                 cc_compat_write(test, "rsp-quotes.rsp", "-E\t-P\r\n-DVALUE=11 '-DWORD=two words' \"-DTEXT=\\\"quoted\\\"\" rsp-expand.c\n") &&
                 cc_compat_write(test, "rsp-object.rsp", "-c -DVALUE=29 \"rsp source.c\" -o out/rsp\\ object.o\n") &&
                 cc_compat_write(test, "rsp-inner.rsp", "-UVALUE -DVALUE=29\n") &&
                 cc_compat_write(test, "out/rsp-inner.rsp", "-UVALUE -DVALUE=99\n") &&
                 cc_compat_write(test, "out/rsp-outer.rsp", "@rsp-inner.rsp\n") &&
                 cc_compat_write(test, "rsp-empty.rsp", "") &&
                 cc_compat_write(test, "rsp-empty-argument.rsp", "-I '' -E -P rsp-expand.c\n") &&
                 cc_compat_write(test, "rsp-after-dash.rsp", "'rsp source.c'\n") &&
                 cc_compat_write(test, "rsp-version.rsp", "-dumpversion\n");
    cc_compat_expect(test, files, "write response-file fixtures");
    const char* const syntax[] = { "@rsp-syntax.rsp", NULL };
    cc_compat_oracle(test, "response file selects syntax checking before action dispatch", NULL, syntax, NULL);
    const char* const quotes[] = { "@rsp-quotes.rsp", NULL };
    cc_compat_oracle(test, "response preprocessing whitespace quotes and escapes match Clang", NULL, quotes, NULL);
    const char* const nested[] = { "@out/rsp-outer.rsp", "-fsyntax-only", "rsp source.c", NULL };
    cc_compat_oracle(test, "nested response paths use cwd rather than the outer file directory", NULL, nested, NULL);
    const char* const siblings[] = { "@rsp-inner.rsp", "@rsp-inner.rsp", "-fsyntax-only", "rsp source.c", NULL };
    cc_compat_oracle(test, "repeated sibling response files are not cycles", NULL, siblings, NULL);
    const char* const order[] = { "-DVALUE=1", "@rsp-inner.rsp", "-UVALUE", "-DVALUE=31", "-E", "-P", "rsp-expand.c", NULL };
    cc_compat_oracle(test, "response arguments retain their position among command-line options", NULL, order, NULL);
    const char* const empty[] = { "-E", "-P", "@rsp-empty.rsp", "rsp-expand.c", NULL };
    cc_compat_oracle(test, "empty response files leave adjacent arguments intact", NULL, empty, NULL);
    const char* const empty_argument[] = { "@rsp-empty-argument.rsp", NULL };
    cc_compat_oracle(test, "empty quoted response operands do not consume following options", NULL, empty_argument, NULL);
    const char* const after_dash[] = { "-fsyntax-only", "-DVALUE=29", "--", "@rsp-after-dash.rsp", NULL };
    cc_compat_oracle(test, "response expansion precedes the -- option terminator", NULL, after_dash, NULL);
    const char* const version[] = { "@rsp-version.rsp", NULL };
    CCCompatibilityRun information = cc_compat_run(test, test->compiler, NULL, version);
    cc_compat_expect(test, information.status == 0 && information.output != NULL && strcmp(information.output, "0.0.0\n") == 0,
                     "response file retains driver information probes");
    cc_compat_release(&information);
    const char* const object[] = { "@rsp-object.rsp", NULL };
    CCCompatibilityRun compiled = cc_compat_run(test, test->compiler, NULL, object);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/out/rsp object.o", test->directory);
    FILE* stream = fopen(path, "rb");
    Elf64_Ehdr header = { 0 };
    const bool emitted = stream != NULL && fread(&header, 1, sizeof(header), stream) == sizeof(header) && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_type == ET_REL;
    if (stream != NULL)
    {
        fclose(stream);
    }
    cc_compat_expect(test, compiled.status == 0 && emitted, "response file compiles quoted source and escaped output paths");
    cc_compat_release(&compiled);

    const char embedded_nul[] = "-E\0-P rsp-expand.c\n";
    files = cc_compat_write(test, "rsp-quote.rsp", "'unterminated") && cc_compat_write(test, "rsp-escape.rsp", "unfinished\\") &&
            cc_compat_response_bytes(test, "rsp-nul.rsp", embedded_nul, sizeof(embedded_nul) - 1) &&
            cc_compat_write(test, "rsp-cycle.rsp", "@./rsp-cycle.rsp\n") && cc_compat_write(test, "rsp-hardlink.rsp", "@rsp-alias.rsp\n");
    char alias[PATH_MAX];
    snprintf(path, sizeof(path), "%s/rsp-hardlink.rsp", test->directory);
    snprintf(alias, sizeof(alias), "%s/rsp-alias.rsp", test->directory);
    files = link(path, alias) == 0 && files;
    snprintf(path, sizeof(path), "%s/rsp-pipe.rsp", test->directory);
    files = mkfifo(path, 0600) == 0 && files;
    cc_compat_expect(test, files, "write malformed cyclic and nonregular response files");
    const char* const invalid[] = { "@rsp-missing.rsp", "@rsp-quote.rsp", "@rsp-escape.rsp", "@rsp-nul.rsp", "@rsp-cycle.rsp", "@rsp-hardlink.rsp", "@rsp-pipe.rsp", "@inc" };
    const char* const reasons[] = { "regular file", "unterminated quote or escape", "unterminated quote or escape", "embedded NUL", "nested file cycle", "nested file cycle", "regular file", "regular file" };
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        cc_compat_response_failure(test, invalid[i], invalid[i], reasons[i]);
    }
    for (uint32_t i = 0; i < 33; i += 1)
    {
        char name[32];
        char contents[64];
        snprintf(name, sizeof(name), "rsp-depth-%u.rsp", i);
        if (i < 32)
        {
            snprintf(contents, sizeof(contents), "@rsp-depth-%u.rsp\n", i + 1);
        }
        else
        {
            memcpy(contents, "rsp-expand.c\n", sizeof("rsp-expand.c\n"));
        }
        files = cc_compat_write(test, name, contents) && files;
    }
    cc_compat_expect(test, files, "write overdeep response chain");
    cc_compat_response_failure(test, "response nesting is bounded", "@rsp-depth-0.rsp", "nesting limit");
    const size_t arguments = 65537;
    char* many = malloc(arguments * 2);
    if (many != NULL)
    {
        for (size_t i = 0; i < arguments; i += 1)
        {
            many[i * 2] = 'x';
            many[i * 2 + 1] = ' ';
        }
    }
    files = many != NULL && cc_compat_response_bytes(test, "rsp-many.rsp", many, arguments * 2);
    free(many);
    cc_compat_expect(test, files, "write response argument-count overflow");
    cc_compat_response_failure(test, "expanded response argument count is bounded", "@rsp-many.rsp", "argument limit");
    snprintf(path, sizeof(path), "%s/rsp-large.rsp", test->directory);
    const int large = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    files = large >= 0 && ftruncate(large, 16 * 1024 * 1024 + 1) == 0;
    if (large >= 0)
    {
        files = close(large) == 0 && files;
    }
    cc_compat_expect(test, files, "write response byte-budget overflow");
    cc_compat_response_failure(test, "response bytes are bounded before reading", "@rsp-large.rsp", "expansion limit");
    // Cumulative bytes count repeated occurrences, and file count includes empty leaves: neither budget can be
    // bypassed by a shallow expansion DAG. These files contain only spaces, not NULs or invalid compiler inputs.
    snprintf(path, sizeof(path), "%s/rsp-spaces.rsp", test->directory);
    stream = fopen(path, "wb");
    char spaces[4096];
    memset(spaces, ' ', sizeof(spaces));
    files = stream != NULL;
    for (uint32_t i = 0; files && i < 2048; i += 1)
    {
        files = fwrite(spaces, 1, sizeof(spaces), stream) == sizeof(spaces);
    }
    if (stream != NULL)
    {
        files = fclose(stream) == 0 && files;
    }
    files = cc_compat_write(test, "rsp-repeat-bytes.rsp", "@rsp-spaces.rsp @rsp-spaces.rsp\n") && files;
    snprintf(path, sizeof(path), "%s/rsp-repeat-files.rsp", test->directory);
    stream = fopen(path, "wb");
    files = stream != NULL && files;
    for (uint32_t i = 0; files && i < 4096; i += 1)
    {
        files = fputs("@rsp-empty.rsp ", stream) >= 0;
    }
    if (stream != NULL)
    {
        files = fclose(stream) == 0 && files;
    }
    cc_compat_expect(test, files, "write aggregate response expansion overflows");
    cc_compat_response_failure(test, "aggregate response bytes count repeated files", "@rsp-repeat-bytes.rsp", "expansion limit");
    cc_compat_response_failure(test, "response expansion count includes empty files", "@rsp-repeat-files.rsp", "expansion limit");
}
