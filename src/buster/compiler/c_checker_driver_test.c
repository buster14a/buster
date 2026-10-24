// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// End-to-end source diagnostics and emission barriers. Every original file is independently checked by Clang;
// the driver runs in a child so its real diagnostic renderer and return status are observed together. Supplying
// --executable also tests the production command's default C route, independently of its explicit check command.

typedef struct CheckDriverFixture CheckDriverFixture;
struct CheckDriverFixture
{
    const char* name;
    const char* source;
    bool rejected;
    uint32_t warnings;
    bool quiet;
};

static bool check_test_driver_run(const char* executable, const char* source, const char* object, bool scalar, bool dump_static, bool quiet, bool lazy,
                                  CheckCorpusOracle* outcome, uint32_t* warnings)
{
    *outcome = (CheckCorpusOracle){ 0 };
    *warnings = 0;
    FILE* diagnostics = tmpfile();
    bool ready = diagnostics != NULL;
    int status = 0;
    pid_t child = -1;
    char* text = NULL;
    size_t length = 0;
    if (ready)
    {
        fflush(NULL);
        child = fork();
        if (child == 0)
        {
            const int output = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(fileno(diagnostics), STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            close(output);
            char* arguments[16] = { 0 };
            uint32_t count = 0;
            arguments[count++] = (char*)(executable != NULL ? executable : "checker");
            if (scalar)
            {
                arguments[count++] = "--scalar";
            }
            if (dump_static)
            {
                arguments[count++] = "--dump-static";
            }
            if (quiet)
            {
                arguments[count++] = "-w";
            }
            if (lazy)
            {
                arguments[count++] = "-flazy-system-headers";
            }
            arguments[count++] = "-std=gnu17";
            arguments[count++] = "-c";
            arguments[count++] = "-o";
            arguments[count++] = (char*)object;
            arguments[count++] = (char*)source;
            if (executable != NULL)
            {
                execvp(executable, arguments);
                _exit(127);
            }
            const int code = run_check_command((int)count - 1, arguments + 1, true);
            fflush(NULL);
            _exit(code);
        }
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        ready = child > 0 && waited == child && WIFEXITED(status) && pp_test_read_output(diagnostics, &text, &length);
    }
    bool unlocated = false;
    for (char* at = text; ready && at != NULL && *at != 0;)
    {
        char* end = strchr(at, '\n');
        if (end != NULL)
        {
            *end = 0;
        }
        if (strstr(at, "error: ") != NULL)
        {
            uint32_t line = 0;
            uint32_t column = 0;
            const bool located = sscanf(at, "%*[^:]:%u:%u:", &line, &column) == 2 && line != 0 && column != 0;
            unlocated |= !located;
            if (located && !outcome->rejected)
            {
                outcome->rejected = true;
                outcome->line = line;
                outcome->column = column;
            }
        }
        *warnings += strstr(at, "warning: ") != NULL;
        at = end != NULL ? end + 1 : NULL;
    }
    ready = ready && !unlocated && ((WEXITSTATUS(status) == 0 && !outcome->rejected) || (WEXITSTATUS(status) == 1 && outcome->rejected));
    if (diagnostics != NULL)
    {
        ready = fclose(diagnostics) == 0 && ready;
    }
    free(text);
    return ready;
}

// Runs `arguments` (arguments[0] the executable) with standard output to /dev/null and its errors into *text, which
// the caller frees: its exit status, or -1 if it didn't exit.
static int check_test_driver_spawn(char* const* arguments, char** text)
{
    FILE* errors = tmpfile();
    int status = 0;
    pid_t child = -1;
    size_t length = 0;
    *text = NULL;
    if (errors != NULL)
    {
        fflush(NULL);
        child = fork();
        if (child == 0)
        {
            const int output = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (output >= 0 && dup2(output, STDOUT_FILENO) >= 0 && dup2(fileno(errors), STDERR_FILENO) >= 0)
            {
                execvp(arguments[0], arguments);
            }
            _exit(127);
        }
    }
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    const bool read = errors != NULL && waited == child && pp_test_read_output(errors, text, &length);
    if (errors != NULL)
    {
        fclose(errors);
    }
    return read && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Whether the files at `a` and `b` can be read and have the same bytes.
static bool check_test_driver_same_files(const char* a, const char* b)
{
    FILE* files[2] = { fopen(a, "rb"), fopen(b, "rb") };
    bool same = files[0] != NULL && files[1] != NULL;
    bool reading = same;
    while (reading)
    {
        char first[4096];
        char second[4096];
        const size_t got = fread(first, 1, sizeof(first), files[0]);
        same = fread(second, 1, sizeof(second), files[1]) == got && memcmp(first, second, got) == 0;
        reading = same && got != 0;
    }
    for (uint32_t f = 0; f < 2; f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    return same;
}

// The DT_NEEDED names of the executable at `path`, in order, joined by spaces into `names`: whether its dynamic section
// could be read (a PIE's file offsets are its addresses' in its first PT_LOAD, as the direct path lays one out).
static bool check_test_driver_needed(const char* path, char* names, uint64_t capacity)
{
    uint64_t length = 0;
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        const long size = ftell(file);
        bytes = size > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0 ? malloc((uint64_t)size) : NULL;
        length = bytes != NULL && fread(bytes, 1, (uint64_t)size, file) == (uint64_t)size ? (uint64_t)size : 0;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    names[0] = 0;
    Elf64_Ehdr header = { 0 };
    bool valid = length != 0;
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_phoff + (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= length;
    }
    uint64_t dynamic_offset = 0;
    uint64_t dynamic_size = 0;
    uint64_t load_offset = 0;
    uint64_t load_address = 0;
    bool load = false;
    for (uint32_t p = 0; valid && p < header.e_phnum; p += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes + header.e_phoff + p * sizeof(Elf64_Phdr), sizeof(segment));
        dynamic_offset = segment.p_type == PT_DYNAMIC ? segment.p_offset : dynamic_offset;
        dynamic_size = segment.p_type == PT_DYNAMIC ? segment.p_filesz : dynamic_size;
        load_offset = segment.p_type == PT_LOAD && !load ? segment.p_offset : load_offset;
        load_address = segment.p_type == PT_LOAD && !load ? segment.p_vaddr : load_address;
        load = load || segment.p_type == PT_LOAD;
    }
    valid = valid && dynamic_size != 0 && dynamic_offset + dynamic_size <= length;
    uint64_t strings = 0;
    for (uint64_t at = 0; valid && at + sizeof(Elf64_Dyn) <= dynamic_size; at += sizeof(Elf64_Dyn))
    {
        Elf64_Dyn entry;
        memcpy(&entry, bytes + dynamic_offset + at, sizeof(entry));
        strings = entry.d_tag == DT_STRTAB ? entry.d_un.d_ptr - load_address + load_offset : strings;
    }
    valid = valid && strings != 0 && strings < length;
    uint64_t used = 0;
    for (uint64_t at = 0; valid && at + sizeof(Elf64_Dyn) <= dynamic_size; at += sizeof(Elf64_Dyn))
    {
        Elf64_Dyn entry;
        memcpy(&entry, bytes + dynamic_offset + at, sizeof(entry));
        if (entry.d_tag == DT_NEEDED)
        {
            const uint64_t name = strings + entry.d_un.d_val;
            valid = name < length && memchr(bytes + name, 0, length - name) != NULL;
            const int written = valid ? snprintf(names + used, capacity - used, "%s%s", used != 0 ? " " : "", (const char*)bytes + name) : 0;
            valid = valid && written > 0 && used + (uint64_t)written < capacity;
            used += valid ? (uint64_t)written : 0;
        }
    }
    free(bytes);
    return valid;
}

// The direct path's -l libraries, through the production command (#145): a program that calls libm's functions, and
// takes one's address, linked with -lm by both paths into the same bytes, that runs and prints what this executable's
// own libm computes, needing libm.so.6 before libc.so.6; one that calls none, with -lm, needing libc.so.6 alone; and the
// refusals, each named: no -lm, a library no directory has, and an archive with members.
static void check_test_driver_libraries(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <math.h>\n#include <stdio.h>\ndouble (*pick)(double) = cos;\n"
                                  "int main(void) { printf(\"%f %f %f %f\\n\", sin(2), sqrt(2.0), pow(2.0, 10.0), pick(0.5)); return 0; }\n";
    static const char plain[] = "int puts(const char*);\nint main(void) { puts(\"plain\"); return 0; }\n";
    static const char archive[] = "!<arch>\n/               0           0     0     644     4         `\n\0\0\0\0";
    char source[256];
    char plain_source[256];
    char archive_path[256];
    char outputs[2][256];
    char plain_output[256];
    snprintf(source, sizeof(source), "%s/math.c", directory);
    snprintf(plain_source, sizeof(plain_source), "%s/plain.c", directory);
    snprintf(archive_path, sizeof(archive_path), "%s/libbuster_members.a", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/math", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/math-scalar", directory);
    snprintf(plain_output, sizeof(plain_output), "%s/plain", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1) && parse_test_write(plain_source, plain, sizeof(plain) - 1) &&
                         parse_test_write(archive_path, archive, sizeof(archive) - 1);
    check_test_expect(test, written, "library programs written");
    char* text = NULL;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, "-lm", NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, "-lm", NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "-lm links libm's functions" : "-lm links libm's functions, scalar");
        free(text);
    }
    check_test_expect(test, written && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable with -lm");
    char needed[256] = { 0 };
    check_test_expect(test, written && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libm.so.6 libc.so.6") == 0,
                      "libm.so.6 needed before libc.so.6");
    char expected[256];
    snprintf(expected, sizeof(expected), "%f %f %f %f\n", sin(2), sqrt(2.0), pow(2.0, 10.0), cos(0.5));
    char captured[256] = { 0 };
    FILE* output = written ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == strlen(expected) && memcmp(captured, expected, got) == 0, "the program prints libm's results");
    char* const plain_arguments[] = { (char*)executable, "-o", plain_output, plain_source, "-lm", NULL };
    const int plain_status = written ? check_test_driver_spawn(plain_arguments, &text) : -1;
    free(text);
    check_test_expect(test, plain_status == 0 && check_test_driver_needed(plain_output, needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "-lm unused needs libc.so.6 alone (as needed)");
    struct
    {
        const char* name;
        const char* message;
        char* arguments[8];
    } const refusals[] = {
        { "no -lm", "undefined reference to 'cos'", { (char*)executable, "-o", outputs[0], source, NULL } },
        { "a library nowhere", "cannot find -lbuster_nowhere", { (char*)executable, "-o", outputs[0], source, "-lm", "-lbuster_nowhere", NULL } },
        { "an archive with members", "unsupported: static libraries", { (char*)executable, "-o", outputs[0], source, "-L", (char*)directory, "-lbuster_members", NULL } },
    };
    for (uint32_t r = 0; written && r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        const int status = check_test_driver_spawn(refusals[r].arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, refusals[r].message) != NULL, refusals[r].name);
        free(text);
    }
    unlink(source);
    unlink(plain_source);
    unlink(archive_path);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(plain_output);
}

// A library that a needed library refers to (#398's review): main calls a_func, which libA defines and which calls
// b_func, which libB defines. Where libA's DT_NEEDED doesn't name libB, the executable needs both, in the line's order,
// either way round (GNU ld refuses -lB -lA); where it does (libAB), the executable needs libAB alone, as GNU ld links
// it, and the loader loads libB for it. clang builds the libraries, and each program runs, finding them through
// LD_LIBRARY_PATH.
static void check_test_driver_dependencies(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char a_source[] = "int b_func(void);\nint a_func(void) { return b_func() + 1; }\n";
    static const char b_source[] = "int b_func(void) { return 41; }\n";
    static const char program[] = "int a_func(void);\nint main(void) { return a_func() == 42 ? 0 : 1; }\n";
    char a_path[256];
    char b_path[256];
    char source[256];
    char library_a[256];
    char library_b[256];
    char library_ab[256];
    char output[256];
    char search[300];
    snprintf(a_path, sizeof(a_path), "%s/a.c", directory);
    snprintf(b_path, sizeof(b_path), "%s/b.c", directory);
    snprintf(source, sizeof(source), "%s/uses_a.c", directory);
    snprintf(library_a, sizeof(library_a), "%s/libA.so", directory);
    snprintf(library_b, sizeof(library_b), "%s/libB.so", directory);
    snprintf(library_ab, sizeof(library_ab), "%s/libAB.so", directory);
    snprintf(output, sizeof(output), "%s/uses_a", directory);
    snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s", directory);
    const bool written = parse_test_write(a_path, a_source, sizeof(a_source) - 1) && parse_test_write(b_path, b_source, sizeof(b_source) - 1) &&
                         parse_test_write(source, program, sizeof(program) - 1);
    // libB.so, libA.so without a DT_NEEDED for libB, and libAB.so with one (libB.so, which has no soname).
    char* const build_b[] = { (char*)clang, "-shared", "-fPIC", "-o", library_b, b_path, NULL };
    char* const build_a[] = { (char*)clang, "-shared", "-fPIC", "-o", library_a, a_path, NULL };
    char* const build_ab[] = { (char*)clang, "-shared", "-fPIC", "-o", library_ab, a_path, "-L", (char*)directory, "-lB", NULL };
    char* text = NULL;
    bool built = written;
    char* const* const builds[] = { build_b, build_a, build_ab };
    for (uint32_t b = 0; built && b < BUSTER_ARRAY_LENGTH(builds); b += 1)
    {
        built = check_test_driver_spawn(builds[b], &text) == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, built, "libraries a needed library refers to: clang builds them");
    struct
    {
        const char* name;
        const char* first;
        const char* second;
        const char* needed;
    } const cases[] = {
        { "-lA -lB: libB for libA's reference", "-lA", "-lB", "libA.so libB.so libc.so.6" },
        { "-lB -lA: libB for libA's reference", "-lB", "-lA", "libB.so libA.so libc.so.6" },
        { "-lAB -lB: libB left to libAB's DT_NEEDED", "-lAB", "-lB", "libAB.so libc.so.6" },
    };
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* const link[] = { (char*)executable, "-o", output, source, "-L", (char*)directory, (char*)cases[c].first, (char*)cases[c].second, NULL };
        char* const run[] = { "env", search, output, NULL };
        const int linked = check_test_driver_spawn(link, &text);
        free(text);
        text = NULL;
        char needed[256] = { 0 };
        const bool listed = linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, cases[c].needed) == 0;
        const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        text = NULL;
        if (!listed || ran != 0)
        {
            printf("  %s: link status %d, DT_NEEDED \"%s\", run status %d\n", cases[c].name, linked, needed, ran);
        }
        check_test_expect(test, listed && ran == 0, cases[c].name);
    }
    const char* const files[] = { a_path, b_path, source, library_a, library_b, library_ab, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// A library that a linker script names by its path, without a soname (#398's review): its DT_NEEDED entry is that
// path, as GNU ld and lld write it, so that the program loads it with no search path.
static void check_test_driver_script_path(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char c_source[] = "int c_func(void) { return 7; }\n";
    static const char program[] = "int c_func(void);\nint main(void) { return c_func() == 7 ? 0 : 1; }\n";
    char private_directory[256];
    char scripts[256];
    char c_path[256];
    char library_c[256];
    char script[256];
    char source[256];
    char output[256];
    char script_text[320];
    char expected[320];
    snprintf(private_directory, sizeof(private_directory), "%s/private", directory);
    snprintf(scripts, sizeof(scripts), "%s/scripts", directory);
    snprintf(c_path, sizeof(c_path), "%s/c.c", directory);
    snprintf(library_c, sizeof(library_c), "%s/libC.so", private_directory);
    snprintf(script, sizeof(script), "%s/libwrap.so", scripts);
    snprintf(source, sizeof(source), "%s/uses_c.c", directory);
    snprintf(output, sizeof(output), "%s/uses_c", directory);
    snprintf(script_text, sizeof(script_text), "INPUT(%s)\n", library_c);
    snprintf(expected, sizeof(expected), "%s libc.so.6", library_c);
    const bool made = mkdir(private_directory, 0755) == 0 && mkdir(scripts, 0755) == 0 && parse_test_write(c_path, c_source, sizeof(c_source) - 1) &&
                      parse_test_write(script, script_text, strlen(script_text)) && parse_test_write(source, program, sizeof(program) - 1);
    char* const build[] = { (char*)clang, "-shared", "-fPIC", "-o", library_c, c_path, NULL };
    char* const link[] = { (char*)executable, "-o", output, source, "-L", scripts, "-lwrap", NULL };
    char* const run[] = { output, NULL };
    char* text = NULL;
    const int built = made ? check_test_driver_spawn(build, &text) : -1;
    free(text);
    text = NULL;
    const int linked = built == 0 ? check_test_driver_spawn(link, &text) : -1;
    free(text);
    text = NULL;
    char needed[512] = { 0 };
    const bool listed = linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, expected) == 0;
    const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    if (!listed || ran != 0)
    {
        printf("  a script's library by its path: build status %d, link status %d, DT_NEEDED \"%s\", run status %d\n", built, linked, needed, ran);
    }
    check_test_expect(test, listed && ran == 0, "a script's library without a soname needed by its path");
    const char* const files[] = { c_path, library_c, script, source, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
    rmdir(private_directory);
    rmdir(scripts);
}

// Options that take the next argument as their value keep it, however it is spelled (#398's review): run in the test's
// directory (env -C), `-o -lresult -include -local.h -I -Lheaders` writes -lresult from a program that needs the forced
// include's macro and a header that only -Lheaders has, and the program runs.
static void check_test_driver_option_values(CheckTest* test, const char* executable, const char* directory)
{
    static const char header_text[] = "#define VALUE 3\n";
    static const char forced_text[] = "#define LOCAL 4\n";
    static const char program[] = "#include \"h.h\"\nint main(void) { return VALUE + LOCAL == 7 ? 0 : 1; }\n";
    char compiler[PATH_MAX];
    char headers[256];
    char header[256];
    char forced[256];
    char source[256];
    char output[256];
    snprintf(headers, sizeof(headers), "%s/-Lheaders", directory);
    snprintf(header, sizeof(header), "%s/-Lheaders/h.h", directory);
    snprintf(forced, sizeof(forced), "%s/-local.h", directory);
    snprintf(source, sizeof(source), "%s/values.c", directory);
    snprintf(output, sizeof(output), "%s/-lresult", directory);
    // The compiler runs in the test's directory, so its path must not be relative to this one.
    const bool made = realpath(executable, compiler) != NULL && mkdir(headers, 0755) == 0 && parse_test_write(header, header_text, sizeof(header_text) - 1) &&
                      parse_test_write(forced, forced_text, sizeof(forced_text) - 1) && parse_test_write(source, program, sizeof(program) - 1);
    char* const link[] = { "env", "-C", (char*)directory, compiler, "values.c", "-o", "-lresult", "-include", "-local.h", "-I", "-Lheaders", NULL };
    char* const run[] = { output, NULL };
    char* text = NULL;
    const int linked = made ? check_test_driver_spawn(link, &text) : -1;
    if (linked != 0)
    {
        printf("  option values: link status %d: %s\n", linked, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, linked == 0 && ran == 0, "-o -lresult, -include -local.h and -I -Lheaders keep their values");
    const char* const files[] = { header, forced, source, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
    rmdir(headers);
}

static void check_test_driver_diagnostic_capacity(CheckTest* test)
{
    Arena scratch = arena_reserve(64);
    const uint64_t capacity = scratch.capacity;
    uint64_t* sentinel = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, 1);
    check_test_expect(test, sentinel != NULL, "diagnostic rollback fixture storage is available");
    if (sentinel != NULL)
    {
        *sentinel = UINT64_C(0x73516A93);
        const uint64_t mark = scratch.length;
        Parser parser = { 0 };
        parser.tokens.count = 1;
        CheckInput input = { .report_count = 1 };
        Preprocessor pp = { 0 };
        for (uint32_t phase = 0; phase < 2; phase += 1)
        {
            scratch.capacity = mark + phase * 2 * sizeof(uint64_t);
            const bool ready = check_driver_reports(&parser, &pp, &input, NULL, NULL, NULL, NULL, 0, NULL, 0, false, &scratch);
            check_test_expect(test, !ready && scratch.length == mark && *sentinel == UINT64_C(0x73516A93) && pp.errors == 0,
                              "diagnostic allocation failure restores scratch and publishes no partial diagnostics");
        }
    }
    scratch.capacity = capacity;
    arena_release(&scratch);
}

static bool check_test_driver_files_equal(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    bool equal = a != NULL && b != NULL;
    bool complete = false;
    while (equal && !complete)
    {
        unsigned char left[4096];
        unsigned char right[4096];
        const size_t count_a = fread(left, 1, sizeof(left), a);
        const size_t count_b = fread(right, 1, sizeof(right), b);
        equal = count_a == count_b && memcmp(left, right, count_a) == 0 && !ferror(a) && !ferror(b);
        complete = count_a < sizeof(left);
    }
    if (a != NULL)
    {
        equal = fclose(a) == 0 && equal;
    }
    if (b != NULL)
    {
        equal = fclose(b) == 0 && equal;
    }
    return equal;
}

static void check_test_driver_lazy(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const struct
    {
        const char* name;
        const char* header;
        const char* source;
        bool rejected;
        bool lazy_rejected;
    } fixtures[] =
    {
        {
            "unused system static semantic error is deferred only when requested",
            "#pragma GCC system_header\nstatic int unused(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            false,
        },
        {
            "referenced system static semantic error is always checked",
            "#pragma GCC system_header\nstatic int used(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return used();}\n",
            true,
            true,
        },
        {
            "ordinary unused static semantic error is always checked",
            "static int unused(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            true,
        },
        {
            "unused system static syntax error is always parsed",
            "#pragma GCC system_header\nstatic int unused(void){int x=; return 1;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            true,
        },
        {
            "valid referenced and unreferenced header functions have equal objects",
            "#pragma GCC system_header\nstatic int unused(void){return 99;}\nstatic int used(void){return 7;}\n",
            "#include \"lazy.h\"\nint f(void){return used();}\n",
            false,
            false,
        },
        {
            "a file-scope address retains a system body in both modes",
            "#pragma GCC system_header\nstatic int used(void){return 7;}\nstatic int unused(void){return 99;}\n",
            "#include \"lazy.h\"\nint (*pointer)(void)=used; int f(void){return 3;}\n",
            false,
            false,
        },
    };
    char header[192];
    char source[192];
    char objects[2][192];
    snprintf(header, sizeof(header), "%s/lazy.h", directory);
    snprintf(source, sizeof(source), "%s/lazy.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/eager.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/lazy.o", directory);
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(header, fixtures[fixture].header, strlen(fixtures[fixture].header)) &&
                           parse_test_write(source, fixtures[fixture].source, strlen(fixtures[fixture].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected == fixtures[fixture].rejected, fixtures[fixture].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            bool accepted[2] = { false, false };
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                unlink(objects[mode]);
                CheckCorpusOracle checked = { 0 };
                uint32_t warnings = 0;
                const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, objects[mode], (route & 1) != 0,
                                                       false, false, mode != 0, &checked, &warnings);
                const bool rejected = mode == 0 ? fixtures[fixture].rejected : fixtures[fixture].lazy_rejected;
                check_test_expect(test, ran && checked.rejected == rejected, fixtures[fixture].name);
                check_test_expect(test, rejected || access(objects[mode], R_OK) == 0, "accepted lazy-policy source publishes its object");
                check_test_expect(test, !rejected || access(objects[mode], F_OK) != 0, "rejected lazy-policy source publishes no object");
                accepted[mode] = ran && !checked.rejected;
            }
            if (!fixtures[fixture].rejected)
            {
                check_test_expect(test, accepted[0] && accepted[1] && check_test_driver_files_equal(objects[0], objects[1]),
                                  "eager and lazy modes produce byte-identical valid objects");
            }
        }
    }
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(source);
    unlink(header);
}

static int run_checker_driver_tests(int argc, char** argv)
{
    CheckTest test = { 0 };
    const char* clang = "clang";
    const char* executable = NULL;
    bool lazy = false;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--lazy-system-headers") == 0)
        {
            lazy = true;
        }
        else
        {
            options = false;
        }
    }
    check_test_expect(&test, options, "driver test options are recognized");
    check_test_driver_diagnostic_capacity(&test);
    const CheckDriverFixture fixtures[] =
    {
        {
            "valid functions and relocatable static storage",
            "static int value=7; int *pointer=&value; int f(int x){return x+value;}\n",
            false,
        },
        {
            "static function relocation shares the emitted object symbols",
            "static int helper(void){return 7;} int (*pointer)(void)=helper;\n",
            false,
        },
        {
            "invalid call before later body syntax error",
            "int f(void){\n1();\nint broken=;\n}\n",
            true,
        },
        {
            "invalid original DO body before missing condition",
            "int f(void){do{\n1();\n}\n}\n",
            true,
        },
        {
            "macro error before later body syntax error",
            "#define CALL(x) x()\nint f(void){CALL(1);\nint broken=;\n}\n",
            true,
        },
        {
            "static initializer before later graph error",
            "int *p[1]={\n1\n};\nint f(void){return 1();}\n",
            true,
        },
        {
            "automatic initializer before later missing name",
            "int f(void){\nint *p[1]={1};\nreturn missing;\n}\n",
            true,
        },
        {
            "assembly destination before later missing name",
            "int f(void){\n__asm__(\"\" : \"=r\"(1));\nreturn missing;\n}\n",
            true,
        },
        {
            "duplicate switch case before later missing name",
            "int f(int n){\nswitch(n){case 0:break;\ncase 0:break;}\nreturn missing;\n}\n",
            true,
        },
        {
            "GNU enum folding retains its declaration warning",
            "enum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            1,
        },
        {
            "GNU assertion folding retains its graph warning",
            "_Static_assert(2147483647+1<0,\"wrap\"); int f(void){return 0;}\n",
            false,
            1,
        },
        {
            "GNU bound warning appears once across reader and graph",
            "int a[(2147483647+1)<0?1:2]; int f(void){return 0;}\n",
            false,
            2,
        },
        {
            "diagnostic pragma suppresses integer overflow",
            "#pragma clang diagnostic ignored \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
        },
        {
            "diagnostic pragma promotes integer overflow",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            true,
        },
        {
            "global warning suppression also suppresses promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            0,
            true,
        },
        {
            "mixed dialect pop restores previous warning state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma GCC diagnostic push\n"
            "#pragma GCC diagnostic ignored \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "#pragma clang diagnostic pop\nenum { B=2147483647+1 };\n",
            true,
        },
        {
            "explicit warning overrides earlier error state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma clang diagnostic warning \"-Winteger-overflow\"\n"
            "enum { A=2147483647+1 };\n",
            false,
            1,
        },
        {
            "fatal warning promotion blocks emission",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n",
            true,
        },
        {
            "later pragma cannot suppress earlier overflow",
            "enum { A=2147483647+1 };\n#pragma clang diagnostic ignored \"-Winteger-overflow\"\n",
            false,
            1,
        },
        {
            "specific pragma overrides group suppression",
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            true,
        },
        {
            "group pragma overrides earlier specific promotion",
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            false,
        },
        {
            "initializer warning promotion blocks emission",
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            true,
        },
        {
            "initializer warning suppression preserves object",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            false,
        },
        {
            "ordinary constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(void){\n1();\nreturn 2147483647+1;\n}\n",
            true,
        },
        {
            "completed control constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(int n){\nswitch(n){\ncase 0:break;\n"
            "case 0:break;\n}\nenum { A=2147483647+1 }; return A;\n}\n",
            true,
        },
        {
            "promoted warning stays before later ordinary constraint",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { A=2147483647+1 };\nint f(void){return 1();}\n",
            true,
        },
        {
            "original syntax rejection cannot emit a repaired object",
            "int f(void){int broken=;}\n",
            true,
        },
        {
            "earlier original enum warning survives later syntax error",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n"
            "int f(void){enum { A=2147483647+1 };\nint broken=;\n}\n",
            true,
        },
        {
            "later promoted initializer warning survives earlier ignored warning and graph error",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int f(void){return missing;}\n",
            true,
        },
        {
            "later promoted initializer warning survives planner semantic failure",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int *p[1]={1};\n",
            true,
        },
        {
            "fatal warning retains precedence over later parser error",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "int f(void){int broken=;}\n",
            true,
        },
        {
            "unknown floating pragma state retains its warning",
            "#pragma STDC FENV_ACCESS MAYBE\nint value;\n",
            false,
            1,
        },
        {
            "unknown complex pragma state can be promoted at end of file",
            "int value;\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n#pragma STDC CX_LIMITED_RANGE MAYBE\n",
            true,
        },
        {
            "later colocated pragma cannot change an earlier pragma warning",
            "#pragma STDC FENV_ACCESS MAYBE\n#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\nint value;\n",
            false,
            1,
        },
        {
            "earlier policy suppresses unknown pragma warnings",
            "#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\n#pragma STDC FENV_ACCESS MAYBE\nint value;\n",
            false,
        },
        {
            "original pragma in an unfinished expression precedes its syntax failure",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n}\n",
            true,
        },
        {
            "original pragma precedes a backwards-pointing missing semicolon diagnostic",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint f(void)\n{\nreturn 0\n#pragma STDC FENV_ACCESS MAYBE\n}\n",
            true,
        },
        {
            "original pragma is diagnosed without a repairable declaration",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n#pragma STDC CX_LIMITED_RANGE MAYBE\n;\n",
            true,
        },
        {
            "macro body pragma warning retains its invocation source",
            "#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n"
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\nCHECK_PRAGMA\n;\n",
            true,
        },
        {
            "direct pragma operator warning retains its invocation source",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n_Pragma(\"STDC CX_LIMITED_RANGE MAYBE\")\n;\n",
            true,
        },
        {
            "forwarded pragma operator warning retains its invocation source",
            "#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n#define FORWARD(x) x\n"
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\nFORWARD(CHECK_PRAGMA)\n;\n",
            true,
        },
        {
            "physical pragma warning retains its remapped source",
            "#line 71 \"original-pragma.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n"
            "int value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n",
            true,
        },
        {
            "original pragma after the syntax lookahead cannot precede it",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=;\n#pragma STDC FENV_ACCESS MAYBE\n",
            true,
        },
        {
            "ignored unfinished-expression pragma leaves the original syntax location",
            "#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\nint f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n}\n",
            true,
        },
        {
            .name = "recognized pragma cannot interrupt an initializer expression",
            .source = "int f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS ON\n;\n}\n",
            .rejected = true,
        },
        {
            .name = "quiet warnings retain recognized pragma syntax constraints",
            .source = "int value=\n#pragma STDC CX_LIMITED_RANGE DEFAULT\n;\n",
            .rejected = true,
            .quiet = true,
        },
        {
            .name = "misplaced floating pragma retains macro invocation source",
            .source = "#define ACCESS _Pragma(\"STDC FENV_ACCESS ON\")\nvoid f(void)\n{\nint value=0;\nACCESS\nvalue++;\n}\n",
            .rejected = true,
        },
        {
            .name = "misplaced floating pragma survives quiet mode at forwarded invocation",
            .source = "#define ACCESS _Pragma(\"STDC FENV_ACCESS ON\")\n#define FORWARD(x) x\n#line 70 \"placement.c\"\n"
                      "void f(void)\n{\nint value=0;\nFORWARD(ACCESS)\nvalue++;\n}\n",
            .rejected = true,
            .quiet = true,
        },
        {
            .name = "late assembler label retains its ignored attribute warning",
            .source = "int object=1;\nextern int object asm(\"renamed\");\n",
            .warnings = 1,
        },
        {
            .name = "late assembler label respects promoted attribute group",
            .source = "#pragma clang diagnostic error \"-Wattributes\"\nint object=1;\nextern int object asm(\"renamed\");\n",
            .rejected = true,
        },
        {
            .name = "late assembler label respects ignored attribute policy",
            .source = "#pragma clang diagnostic ignored \"-Wignored-attributes\"\nint object=1;\nextern int object asm(\"renamed\");\n",
        },
    };
    char directory[] = "/tmp/buster-checker-driver-XXXXXX";
    const bool temporary = options && mkdtemp(directory) != NULL;
    check_test_expect(&test, temporary, "driver test directory is available");
    const char* clang_arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; temporary && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                  check_corpus_clang_file(clang, clang_arguments, fixtures[i].quiet ? 2 : 1, source, &oracle);
        check_test_expect(&test, oracle_ready && oracle.rejected == fixtures[i].rejected, fixtures[i].name);
        const uint32_t schedules = executable != NULL ? 4 : 2;
        const uint32_t routes = schedules * 2;
        for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
        {
            unlink(object);
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool external = route % schedules >= 2;
            const bool dump_static = route >= schedules;
            const bool ready = check_test_driver_run(external ? executable : NULL, source, object, (route & 1) != 0, dump_static,
                                                     fixtures[i].quiet, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, %s %s%s: error line %u, Clang %u", fixtures[i].name,
                     external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "", checked.line, oracle.line);
            check_test_expect(&test, ready && checked.rejected == oracle.rejected && checked.line == oracle.line, description);
            check_test_expect(&test, warnings == fixtures[i].warnings, "driver renders each semantic warning once");
            FILE* emitted = fopen(object, "rb");
            unsigned char magic[4] = { 0 };
            const bool elf = emitted != NULL && fread(magic, 1, sizeof(magic), emitted) == sizeof(magic) &&
                             memcmp(magic, "\177ELF", sizeof(magic)) == 0;
            check_test_expect(&test, oracle.rejected ? emitted == NULL : elf, "driver emits an ELF object only for accepted original source");
            if (emitted != NULL)
            {
                fclose(emitted);
            }
            if (fixtures[i].rejected && route == 0)
            {
                static const char preserved[] = "existing output remains intact";
                const bool written = parse_test_write(object, preserved, sizeof(preserved));
                const bool rejected = written && check_test_driver_run(NULL, source, object, false, false, fixtures[i].quiet, lazy, &checked, &warnings) && checked.rejected;
                FILE* prior = fopen(object, "rb");
                char bytes[sizeof(preserved) + 1] = { 0 };
                const size_t length = prior != NULL ? fread(bytes, 1, sizeof(bytes), prior) : 0;
                check_test_expect(&test, rejected && length == sizeof(preserved) && memcmp(bytes, preserved, sizeof(preserved)) == 0,
                                  "rejected original source preserves an existing output file");
                if (prior != NULL)
                {
                    fclose(prior);
                }
            }
        }
        unlink(object);
        unlink(source);
    }
    if (temporary)
    {
        check_test_driver_lazy(&test, clang, executable, directory);
        if (executable != NULL)
        {
            check_test_driver_libraries(&test, executable, directory);
            check_test_driver_dependencies(&test, clang, executable, directory);
            check_test_driver_script_path(&test, clang, executable, directory);
            check_test_driver_option_values(&test, executable, directory);
        }
        rmdir(directory);
    }
    printf("checker-driver: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
