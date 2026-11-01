// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source fixtures for #1077: an undeclared identifier suppresses the diagnostics of the operators, calls and
// statements that contain it, as clang's RecoveryExpr does, and of nothing else: the operands beside it, before it or
// after it, and the statements after it, keep theirs. Each fixture is a translation unit of its own (the driver gives
// one control-flow diagnostic a unit), which clang -fsyntax-only and the driver's two paths, with -fsyntax-only and with
// -c, compile. Their diagnostics are compared line for line: each error's and warning's line, column and severity, and
// an undeclared identifier's whole message. Other messages are this checker's own (`integer operands required` for
// clang's `invalid operands to binary expression`), which #1077 leaves. Clang's lines must also be the fixture's, so
// that a fixture keeps testing what it says. Include after c_check_x86_diagnostic_test.c, whose helpers it uses.

#define BUSTER_CHECK_UNDECLARED_TEXT 4096u

// The diagnostics `text` gives for the file at `source`: each error and warning as `line:column: severity: message`,
// the message an undeclared identifier's or "-", into `normal` (capacity bytes); other lines (notes, the summary) are
// left out. Without `columns`, a line whose message is "-" leaves out its column too. The number of lines, or
// UINT32_MAX where they don't fit.
static uint32_t check_test_undeclared_lines(const char* text, const char* source, bool columns, char* normal, uint32_t capacity)
{
    uint32_t result = 0;
    uint32_t used = 0;
    const size_t name = strlen(source);
    const char* at = text != NULL ? text : "";
    normal[0] = 0;
    while (*at != 0 && result != UINT32_MAX)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        char line[1024];
        snprintf(line, sizeof(line), "%.*s", (int)(length < sizeof(line) ? length : sizeof(line) - 1), at);
        uint32_t row = 0;
        uint32_t column = 0;
        char severity[16] = { 0 };
        int message = 0;
        const bool located = strncmp(line, source, name) == 0 && line[name] == ':' &&
                             sscanf(line + name + 1, "%u:%u: %15[a-z]: %n", &row, &column, severity, &message) == 3 && message != 0;
        if (located && (strcmp(severity, "error") == 0 || strcmp(severity, "warning") == 0))
        {
            const char* body = line + name + 1 + message;
            const bool kept = strncmp(body, "use of undeclared identifier", 28) == 0;
            const int written = kept || columns ? snprintf(normal + used, capacity - used, "%u:%u: %s: %s\n", row, column, severity, kept ? body : "-") :
                                                  snprintf(normal + used, capacity - used, "%u: %s: -\n", row, severity);
            if (written > 0 && (uint32_t)written < capacity - used)
            {
                used += (uint32_t)written;
                result += 1;
            }
            else
            {
                normal[used] = 0;
                result = UINT32_MAX;
            }
        }
        at = end != NULL ? end + 1 : at + length;
    }
    return result;
}

// The driver on `source`, by the scalar or the vector path, with -fsyntax-only or with -c into `object`, in a child
// process whose standard errors go to the file at `errors`: its exit status, or -1 if it didn't exit.
static int check_test_undeclared_run(const char* source, const char* object, const char* errors, bool scalar, bool compile)
{
    int result = -1;
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        const int out = open("/dev/null", O_WRONLY);
        const int err = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0 || err < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0)
        {
            _exit(126);
        }
        close(out);
        close(err);
        char* arguments[10];
        int count = 0;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (compile)
        {
            arguments[count++] = "-c";
            arguments[count++] = "-o";
            arguments[count++] = (char*)object;
        }
        else
        {
            arguments[count++] = "-fsyntax-only";
        }
        arguments[count++] = "-ferror-limit=0";
        arguments[count++] = "-march=x86-64-v4";
        arguments[count++] = "-std=gnu17";
        arguments[count++] = (char*)source;
        const int code = run_check_command(count, arguments, true);
        fflush(NULL);
        _exit(code);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && errno == EINTR);
    if (waited == child && child > 0 && WIFEXITED(status))
    {
        result = WEXITSTATUS(status);
    }
    return result;
}

// The file at `path`, NUL-terminated, in malloc's memory (the caller frees it), or null.
static char* check_test_undeclared_read(const char* path)
{
    FILE* file = fopen(path, "r");
    char* text = NULL;
    size_t length = 0;
    if (file != NULL)
    {
        if (!pp_test_read_output(file, &text, &length))
        {
            free(text);
            text = NULL;
        }
        fclose(file);
    }
    return text;
}

static __attribute__((noinline)) void check_test_undeclared(CheckTest* test, const char* clang)
{
    static const struct
    {
        const char* name;
        const char* source;
        // Clang's lines, as check_test_undeclared_lines writes them.
        const char* expected;
        // Whether the lines that aren't undeclared identifiers' keep their columns. #1066 (pull request #1068) places the
        // x86 call's error at the builtin's name, as clang does: until it lands, the driver's is at the call's `)`.
        bool columns;
    } fixtures[] =
    {
        { "the issue's operand after the name", "int f(int *p) { return undeclared3 + (p + 1.5); }\n",
          "1:24: error: use of undeclared identifier 'undeclared3'\n1:41: error: -\n", true },
        { "the issue's operand before the name", "int f(int *p) { return (p + 1.5) + undeclared5; }\n",
          "1:27: error: -\n1:36: error: use of undeclared identifier 'undeclared5'\n", true },
        { "the issue's x86 call after the name", "unsigned f(unsigned b, unsigned i) { return undeclared4 + __builtin_ia32_kshiftlisi(b, i); }\n",
          "1:45: error: use of undeclared identifier 'undeclared4'\n1: error: -\n", false },
        { "an x86 call before the name", "unsigned f(unsigned b, unsigned i) { return __builtin_ia32_kshiftlisi(b, i) + undeclared4; }\n",
          "1: error: -\n1:79: error: use of undeclared identifier 'undeclared4'\n", false },
        { "the operators and calls that hold the name stay silent",
          "int take(int);\n"
          "int f1(void) { return -undeclared + *1; }\n"
          "int f2(void) { return take(undeclared) + *1; }\n"
          "int f3(void) { return take(undeclared, *1); }\n"
          "int f4(void) { return take(undeclared, 1, 2); }\n"
          "int f5(void) { return (int)undeclared + *1; }\n"
          "int f6(void) { return undeclared[*1]; }\n"
          "int f7(void) { return (undeclared = 3) + *1; }\n"
          "int f8(void) { return sizeof(undeclared) + *1; }\n"
          "int f9(void) { return ++undeclared + *1; }\n"
          "int f10(void) { return !undeclared && *1; }\n"
          "int f11(void) { return *(undeclared + 1) + *1; }\n"
          "int f12(void) { return 1 << 40 + undeclared; }\n"
          "int f13(void) { return undeclared == 1; }\n",
          "2:24: error: use of undeclared identifier 'undeclared'\n2:37: error: -\n"
          "3:28: error: use of undeclared identifier 'undeclared'\n3:42: error: -\n"
          "4:28: error: use of undeclared identifier 'undeclared'\n4:40: error: -\n"
          "5:28: error: use of undeclared identifier 'undeclared'\n"
          "6:28: error: use of undeclared identifier 'undeclared'\n6:41: error: -\n"
          "7:23: error: use of undeclared identifier 'undeclared'\n7:34: error: -\n"
          "8:24: error: use of undeclared identifier 'undeclared'\n8:42: error: -\n"
          "9:30: error: use of undeclared identifier 'undeclared'\n9:44: error: -\n"
          "10:25: error: use of undeclared identifier 'undeclared'\n10:38: error: -\n"
          "11:25: error: use of undeclared identifier 'undeclared'\n11:39: error: -\n"
          "12:26: error: use of undeclared identifier 'undeclared'\n12:44: error: -\n"
          "13:34: error: use of undeclared identifier 'undeclared'\n"
          "14:24: error: use of undeclared identifier 'undeclared'\n", true },
        { "the operands beside the name keep their diagnostics",
          "int f1(int *p) { return undeclared ? *1 : p + 1.5; }\n"
          "int f2(void) { return (undeclared, *1); }\n"
          "int f3(void) { return undeclared1 + undeclared2 + *1; }\n"
          "int f4(void) { return *1 + undeclared + *2; }\n"
          "int f5(void) { return undeclared + (1 << 40); }\n"
          "int f6(void) { return (1 << 40) + undeclared; }\n",
          "1:25: error: use of undeclared identifier 'undeclared'\n1:38: error: -\n1:45: error: -\n"
          "2:24: error: use of undeclared identifier 'undeclared'\n2:36: error: -\n"
          "3:23: error: use of undeclared identifier 'undeclared1'\n3:37: error: use of undeclared identifier 'undeclared2'\n3:51: error: -\n"
          "4:23: error: -\n4:28: error: use of undeclared identifier 'undeclared'\n4:41: error: -\n"
          "5:23: error: use of undeclared identifier 'undeclared'\n5:39: warning: -\n"
          "6:35: error: use of undeclared identifier 'undeclared'\n6:26: warning: -\n", true },
        { "the statements after the name keep their diagnostics",
          "int f1(void) { int x = undeclared; return x + *1; }\n"
          "int f2(void) { undeclared; return *1; }\n"
          "int f3(void) { if (undeclared) { return *1; } return 0; }\n"
          "int f4(void) { if (*1) { return undeclared; } return *2; }\n"
          "int f5(void) { while (undeclared) { *1; } for (undeclared;;) { *2; } do { *3; } while (undeclared); return 0; }\n"
          "int f6(void) { return ({ undeclared; *1; }); }\n"
          "int f7(void) { label: undeclared; goto label; return *1; }\n",
          "1:24: error: use of undeclared identifier 'undeclared'\n1:47: error: -\n"
          "2:16: error: use of undeclared identifier 'undeclared'\n2:35: error: -\n"
          "3:20: error: use of undeclared identifier 'undeclared'\n3:41: error: -\n"
          "4:20: error: -\n4:33: error: use of undeclared identifier 'undeclared'\n4:54: error: -\n"
          "5:23: error: use of undeclared identifier 'undeclared'\n5:37: error: -\n5:48: error: use of undeclared identifier 'undeclared'\n"
          "5:64: error: -\n5:75: error: -\n5:88: error: use of undeclared identifier 'undeclared'\n"
          "6:26: error: use of undeclared identifier 'undeclared'\n6:38: error: -\n"
          "7:23: error: use of undeclared identifier 'undeclared'\n7:54: error: -\n", true },
        { "a duplicate case after a case whose statement has the name",
          "int f(int x) { switch (x) { case 1: undeclared; case 1: return 0; } return 1; }\n",
          "1:37: error: use of undeclared identifier 'undeclared'\n1:54: error: -\n", true },
        { "a duplicate case whose statement has the name, published at the switch's end",
          "int f(int x) { switch (x) { case 1: return 0; case 1: undeclared; } return 1; }\n",
          "1:55: error: use of undeclared identifier 'undeclared'\n1:52: error: -\n", true },
        { "a duplicate case after a case of the name",
          "int f(int x) { switch (x) { case undeclared: return 0; case 2: case 2: return 1; } return 1; }\n",
          "1:34: error: use of undeclared identifier 'undeclared'\n1:69: error: -\n", true },
        { "no case is checked in a switch on the name",
          "int f(int x) { switch (undeclared) { case 1: case 1: return *1; } return 0; }\n",
          "1:24: error: use of undeclared identifier 'undeclared'\n1:61: error: -\n", true },
        { "a case of the name doesn't duplicate another",
          "int f(int x) { switch (x) { case 1 + undeclared: case 1: return 0; } return 1; }\n",
          "1:38: error: use of undeclared identifier 'undeclared'\n", true },
        { "initializers beside the name",
          "struct S { int a; };\n"
          "int f1(void) { struct S s = { undeclared, *1 }; return 0; }\n"
          "int f2(void) { struct S s = { undeclared }; struct S t = { *2 }; return 0; }\n"
          "void f3(int *p) { static int *a[2] = { undeclared, p }; }\n",
          "2:31: error: use of undeclared identifier 'undeclared'\n2:43: error: -\n"
          "3:31: error: use of undeclared identifier 'undeclared'\n3:60: error: -\n"
          "4:40: error: use of undeclared identifier 'undeclared'\n", true },
        { "file-scope initializers beside the name",
          "int y;\n"
          "int *g1[2] = { undeclared, (int *)(long)&y + y };\n"
          "int g2 = undeclared + (1 << 40);\n"
          "struct T { int a; int b; } g3 = { undeclared, 1.5 + &y };\n"
          "int g4[2] = { undeclared, y };\n",
          "2:16: error: use of undeclared identifier 'undeclared'\n"
          "3:10: error: use of undeclared identifier 'undeclared'\n3:26: warning: -\n"
          "4:35: error: use of undeclared identifier 'undeclared'\n4:51: error: -\n"
          "5:15: error: use of undeclared identifier 'undeclared'\n", true },
        { "several functions",
          "int f(void) { int x = undeclared; return x + *1; }\n"
          "int g(void) { return *2 + undeclared; }\n"
          "int h(void) { return *3; }\n",
          "1:23: error: use of undeclared identifier 'undeclared'\n1:46: error: -\n"
          "2:22: error: -\n2:27: error: use of undeclared identifier 'undeclared'\n"
          "3:22: error: -\n", true },
    };
    const char* temporary = getenv("TMPDIR");
    char directory[256];
    snprintf(directory, sizeof(directory), "%s/buster-undeclared-XXXXXX", temporary != NULL && temporary[0] != 0 ? temporary : "/tmp");
    const bool ready = mkdtemp(directory) != NULL;
    check_test_expect(test, ready, "undeclared-identifier recovery: the fixtures' directory");
    char source[320];
    char object[320];
    char errors[320];
    snprintf(source, sizeof(source), "%s/source.c", directory);
    snprintf(object, sizeof(object), "%s/source.o", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    static char theirs[BUSTER_CHECK_UNDECLARED_TEXT];
    static char ours[BUSTER_CHECK_UNDECLARED_TEXT];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool written = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-ferror-limit=0",
                              "-march=x86-64-v4", source, NULL };
        OracleRun run = {
            .purpose = "checker: undeclared-identifier recovery",
            .arguments = arguments,
            .dependencies = true,
        };
        const bool ran = written && oracle_run(&run);
        const int oracle_status = ran && WIFEXITED(run.status) ? WEXITSTATUS(run.status) : -1;
        const uint32_t lines = check_test_undeclared_lines(ran ? run.errors : NULL, source, fixtures[i].columns, theirs, sizeof(theirs));
        oracle_run_release(&run);
        char description[2 * BUSTER_CHECK_UNDECLARED_TEXT + 512];
        snprintf(description, sizeof(description), "undeclared-identifier recovery, %s: clang's status %d and diagnostics\n%s\nexpected 1 and\n%s",
                 fixtures[i].name, oracle_status, theirs, fixtures[i].expected);
        check_test_expect(test, written && oracle_status == 1 && lines != UINT32_MAX && strcmp(theirs, fixtures[i].expected) == 0, description);
        for (uint32_t mode = 0; written && mode < 4; mode += 1)
        {
            const bool scalar = (mode & 1) != 0;
            const bool compile = (mode & 2) != 0;
            unlink(object);
            const int status = check_test_undeclared_run(source, object, errors, scalar, compile);
            char* text = check_test_undeclared_read(errors);
            const uint32_t count = check_test_undeclared_lines(text, source, fixtures[i].columns, ours, sizeof(ours));
            free(text);
            snprintf(description, sizeof(description), "undeclared-identifier recovery, %s, %s %s: status %d and diagnostics\n%s\nclang's %d and\n%s",
                     fixtures[i].name, scalar ? "scalar" : "vector", compile ? "-c" : "-fsyntax-only", status, ours, oracle_status, theirs);
            check_test_expect(test, status == 1 && count == lines && strcmp(ours, theirs) == 0 && access(object, F_OK) != 0, description);
        }
    }
    if (ready)
    {
        unlink(source);
        unlink(object);
        unlink(errors);
        rmdir(directory);
    }
}
