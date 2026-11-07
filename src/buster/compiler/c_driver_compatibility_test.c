// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real CC invocations for #1113, shared by test-checker-driver and a standalone developer harness. No production
// helpers are reused as the oracle: Clang supplies dependency rules, GNU make consumes escaping, and ELF headers
// plus execution check the requested link modes. Each process has a private fixture directory and bounded captures.
#ifdef BUSTER_DRIVER_COMPAT_STANDALONE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <elf.h>

typedef struct CCCompatibilityTest CCCompatibilityTest;
struct CCCompatibilityTest
{
    char directory[PATH_MAX];
    char compiler[PATH_MAX];
    const char* clang;
    uint32_t checks;
    uint32_t failures;
};

typedef struct CCCompatibilityRun CCCompatibilityRun;
struct CCCompatibilityRun
{
    int status;
    char* output;
    char* errors;
};

static void cc_compat_expect(CCCompatibilityTest* test, bool good, const char* name)
{
    test->checks += 1;
    test->failures += !good;
    if (!good)
    {
        fprintf(stderr, "CC compatibility: FAIL: %s\n", name);
    }
}

static bool cc_compat_write(CCCompatibilityTest* test, const char* name, const char* text)
{
    char path[PATH_MAX];
    const int named = snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    FILE* stream = named > 0 && (size_t)named < sizeof(path) ? fopen(path, "wb") : NULL;
    bool result = stream != NULL;
    if (stream != NULL)
    {
        const size_t length = strlen(text);
        const bool written = fwrite(text, 1, length, stream) == length;
        result = fclose(stream) == 0 && written;
    }
    return result;
}

static char* cc_compat_read(CCCompatibilityTest* test, const char* name)
{
    char path[PATH_MAX];
    const int named = snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    FILE* stream = named > 0 && (size_t)named < sizeof(path) ? fopen(path, "rb") : NULL;
    struct stat status;
    char* result = NULL;
    if (stream != NULL && fstat(fileno(stream), &status) == 0 && status.st_size >= 0 && status.st_size < (16 << 20))
    {
        result = calloc((size_t)status.st_size + 1, 1);
        if (result != NULL && fread(result, 1, (size_t)status.st_size, stream) != (size_t)status.st_size)
        {
            free(result);
            result = NULL;
        }
    }
    if (stream != NULL)
    {
        fclose(stream);
    }
    return result;
}

static CCCompatibilityRun cc_compat_run(CCCompatibilityTest* test, const char* program, const char* input, const char* const* arguments)
{
    CCCompatibilityRun result = { .status = -1 };
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        bool ready = chdir(test->directory) == 0;
        const int in = ready ? open(input != NULL ? input : "/dev/null", O_RDONLY) : -1;
        const int out = ready ? open("capture.out", O_WRONLY | O_CREAT | O_TRUNC, 0600) : -1;
        const int err = ready ? open("capture.err", O_WRONLY | O_CREAT | O_TRUNC, 0600) : -1;
        ready = ready && in >= 0 && out >= 0 && err >= 0 && dup2(in, 0) >= 0 && dup2(out, 1) >= 0 && dup2(err, 2) >= 0;
        if (ready)
        {
            close(in);
            close(out);
            close(err);
            char* argv[64] = { (char*)program };
            uint32_t count = 1;
            for (uint32_t i = 0; arguments[i] != NULL && count < 63; i += 1)
            {
                argv[count++] = (char*)arguments[i];
            }
            execvp(program, argv);
        }
        _exit(126);
    }
    int status = 0;
    pid_t waited;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    if (waited == child && child > 0 && WIFEXITED(status))
    {
        result.status = WEXITSTATUS(status);
        result.output = cc_compat_read(test, "capture.out");
        result.errors = cc_compat_read(test, "capture.err");
    }
    return result;
}

static void cc_compat_release(CCCompatibilityRun* run)
{
    free(run->output);
    free(run->errors);
    *run = (CCCompatibilityRun){ .status = -1 };
}

// GCC inserts empty lines before -MP rules while Clang does not. Ignore only empty physical lines at line starts;
// preserve every other byte, notably escaped filename spaces/tabs and raw -MT expressions. These bounded oracle
// cases fit a single Clang rule line, so line wrapping needs no normalization.
static bool cc_compat_rule_equal(const char* a, const char* b)
{
    bool result = a != NULL && b != NULL;
    bool line_start = true;
    while (result)
    {
        if (line_start)
        {
            while (*a == '\n')
            {
                a += 1;
            }
            while (*b == '\n')
            {
                b += 1;
            }
        }
        result = *a == *b;
        if (!result || *a == 0)
        {
            break;
        }
        line_start = *a == '\n';
        a += 1;
        b += 1;
    }
    return result;
}

static void cc_compat_oracle(CCCompatibilityTest* test, const char* name, const char* input, const char* const* arguments, const char* depfile)
{
    char destination[PATH_MAX];
    if (depfile != NULL)
    {
        snprintf(destination, sizeof(destination), "%s/%s", test->directory, depfile);
        unlink(destination);
    }
    CCCompatibilityRun ours = cc_compat_run(test, test->compiler, input, arguments);
    char* our_rule = depfile != NULL ? cc_compat_read(test, depfile) : NULL;
    if (depfile != NULL)
    {
        unlink(destination);
    }
    CCCompatibilityRun oracle = cc_compat_run(test, test->clang, input, arguments);
    char* their_rule = depfile != NULL ? cc_compat_read(test, depfile) : NULL;
    const char* a = depfile != NULL ? our_rule : ours.output;
    const char* b = depfile != NULL ? their_rule : oracle.output;
    const bool equal = ours.status == 0 && oracle.status == 0 && cc_compat_rule_equal(a, b);
    cc_compat_expect(test, equal, name);
    if (!equal)
    {
        fprintf(stderr, "  buster status %d, clang status %d\n  buster: %s\n  clang: %s\n  diagnostics: %s\n", ours.status, oracle.status,
                a != NULL ? a : "(missing)", b != NULL ? b : "(missing)", ours.errors != NULL ? ours.errors : "(missing)");
    }
    free(our_rule);
    free(their_rule);
    cc_compat_release(&ours);
    cc_compat_release(&oracle);
}

static void cc_compat_failure(CCCompatibilityTest* test, const char* name, const char* input, const char* const* arguments)
{
    const bool prepared = cc_compat_write(test, "keep.d", "previous dependency output\n");
    CCCompatibilityRun run = cc_compat_run(test, test->compiler, input, arguments);
    char* kept = cc_compat_read(test, "keep.d");
    cc_compat_expect(test, prepared && run.status != 0 && kept != NULL && strcmp(kept, "previous dependency output\n") == 0, name);
    free(kept);
    cc_compat_release(&run);
}

// A temporary sibling must not add characters to a destination basename. Exercise both sides of the old 11-byte
// suffix boundary and the directory's actual limit, then a default .d derived from a maximum-length object name.
static void cc_compat_long_depfiles(CCCompatibilityTest* test)
{
    const long limit = pathconf(test->directory, _PC_NAME_MAX);
    const bool supported = limit >= 12 && limit < PATH_MAX - 16;
    cc_compat_expect(test, supported, "query a usable directory component limit");
    if (supported)
    {
        const size_t lengths[] = { (size_t)limit - 11, (size_t)limit - 10, (size_t)limit };
        char name[PATH_MAX];
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const size_t prefix = i == 0 ? 0 : 4;
            memcpy(name, "out/", prefix);
            memset(name + prefix, 'a' + (int)i, lengths[i]);
            name[prefix + lengths[i]] = 0;
            const char* const args[] = { "-M", "-MF", name, "a.c", NULL };
            cc_compat_oracle(test, "long explicit -MF matches Clang", NULL, args, name);
            const bool prepared = cc_compat_write(test, name, "old dependency\n");
            const char* const failure[] = { "-M", "-MF", name, "missing.c", NULL };
            CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, failure);
            char* old = cc_compat_read(test, name);
            cc_compat_expect(test, prepared && run.status != 0 && old != NULL && strcmp(old, "old dependency\n") == 0,
                             "failure preserves a long existing depfile");
            free(old);
            cc_compat_release(&run);
        }
        char object[PATH_MAX];
        char depfile[PATH_MAX];
        memcpy(object, "out/", 4);
        memset(object + 4, 'z', (size_t)limit - 2);
        memcpy(object + 4 + (size_t)limit - 2, ".o", 3);
        memcpy(depfile, object, 4 + (size_t)limit + 1);
        depfile[4 + (size_t)limit - 1] = 'd';
        // Keep the rule target short so Clang's long-line wrapping is not part of this filename test.
        const char* const args[] = { "-c", "-MMD", "-MT", "unit.o", "a.c", "-o", object, NULL };
        cc_compat_oracle(test, "maximum-length default .d matches Clang", NULL, args, depfile);
    }
}

#if !defined(BUSTER_DRIVER_COMPAT_STANDALONE) && !defined(BUSTER_DRIVER_MIXED_STANDALONE)
// Force the exact random suffix ABCDEF in the name comparison: neither destination may equal its chosen temporary,
// regardless of what mkstemp draws. Then force a real close/write failure after creation with RLIMIT_FSIZE.
static void cc_compat_publication_failure(CCCompatibilityTest* test)
{
    const char* const names[] = { ".tmp.ABCDEF", ".tmq.ABCDEF" };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        char candidate[12];
        memcpy(candidate, dependency_temporary_template(names[i]), sizeof(candidate));
        memcpy(candidate + 5, "ABCDEF", 6);
        cc_compat_expect(test, strcmp(candidate, names[i]) != 0, "temporary and destination cannot collide for a chosen suffix");
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/out/rollback.d", test->directory);
    const bool prepared = cc_compat_write(test, "out/rollback.d", "previous dependency output\n");
    const pid_t child = fork();
    if (child == 0)
    {
        struct rlimit limit = { .rlim_cur = 0, .rlim_max = 0 };
        const bool ready = signal(SIGXFSZ, SIG_IGN) != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0;
        char data[8192] = { 0 };
        const DependencyOutput output = { .path = path, .bytes = { .pointer = data, .length = sizeof(data) } };
        FILE* errors = fopen("/dev/null", "w");
        const bool failed = ready && errors != NULL && !dependency_publish(&output, errors);
        if (errors != NULL)
        {
            fclose(errors);
        }
        _exit(failed ? 0 : 1);
    }
    int status = 0;
    pid_t waited;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    char* old = cc_compat_read(test, "out/rollback.d");
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/out", test->directory);
    DIR* dir = opendir(directory);
    bool clean = dir != NULL;
    struct dirent* entry;
    while (dir != NULL && (entry = readdir(dir)) != NULL)
    {
        clean &= strncmp(entry->d_name, ".tmp.", 5) != 0 && strncmp(entry->d_name, ".tmq.", 5) != 0;
    }
    if (dir != NULL)
    {
        closedir(dir);
    }
    cc_compat_expect(test, prepared && waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                           old != NULL && strcmp(old, "previous dependency output\n") == 0 && clean,
                     "write or close failure preserves the old depfile and removes its temporary");
    free(old);
}
#endif

static bool cc_compat_elf(CCCompatibilityTest* test, const char* name, uint16_t type, bool interpreter)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    FILE* file = fopen(path, "rb");
    Elf64_Ehdr header = { 0 };
    bool result = file != NULL && fread(&header, 1, sizeof(header), file) == sizeof(header) && memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 &&
                  header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_type == type && header.e_phentsize == sizeof(Elf64_Phdr) && header.e_phoff < LONG_MAX;
    bool found = false;
    if (result)
    {
        result = fseek(file, (long)header.e_phoff, SEEK_SET) == 0;
        for (uint16_t i = 0; result && i < header.e_phnum; i += 1)
        {
            Elf64_Phdr program;
            result = fread(&program, 1, sizeof(program), file) == sizeof(program);
            found |= result && program.p_type == PT_INTERP;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return result && found == interpreter;
}

#include "c_driver_response_test.c"

static void cc_compat_cleanup(CCCompatibilityTest* test)
{
    // The fixture owns only these flat subdirectories. No shell, recursive traversal or caller-provided path.
    const char* names[] = { "sys", "inc", "out", "" };
    for (uint32_t d = 0; d < sizeof(names) / sizeof(names[0]); d += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", test->directory, names[d]);
        DIR* directory = opendir(path);
        struct dirent* entry;
        while (directory != NULL && (entry = readdir(directory)) != NULL)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                char file[PATH_MAX];
                const int length = snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
                if (length > 0 && (size_t)length < sizeof(file))
                {
                    unlink(file);
                }
            }
        }
        if (directory != NULL)
        {
            closedir(directory);
        }
        rmdir(path);
    }
}

// These preprocessing-only fixtures have no string literals or whitespace-sensitive token boundaries. Compare their
// independent expected spellings with Clang and both Buster kernels, ignoring only printer whitespace.
static bool cc_compat_language_output(const char* text, const char* expected)
{
    bool result = text != NULL;
    for (size_t i = 0; result && text[i] != 0; i += 1)
    {
        const char byte = text[i];
        if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f' && byte != '\v')
        {
            result = *expected != 0 && byte == *expected;
            expected += result;
        }
    }
    return result && *expected == 0;
}

static void cc_compat_language_expect(CCCompatibilityTest* test, const char* name, const char* variant, const CCCompatibilityRun* run, const char* expected)
{
    char label[256];
    snprintf(label, sizeof(label), "%s (%s)", name, variant);
    const bool good = run->status == 0 && cc_compat_language_output(run->output, expected);
    cc_compat_expect(test, good, label);
    if (!good)
    {
        fprintf(stderr, "  %s status %d\n  stdout: %s\n  stderr: %s\n", variant, run->status,
                run->output != NULL ? run->output : "(missing)", run->errors != NULL ? run->errors : "(missing)");
    }
}

static void cc_compat_preprocessor_languages(CCCompatibilityTest* test)
{
    const char* source = "#define N 3\n#ifdef __ASSEMBLER__\nasm_mode\n#else\nc_mode\n#endif\n$N\n";
    bool files = cc_compat_write(test, "language.c", source) && cc_compat_write(test, "language.S", source) &&
                 cc_compat_write(test, "language.s", "#error raw assembly must not be preprocessed\n") &&
                 cc_compat_write(test, "-xassembler-with-cpp.S", source) &&
                 cc_compat_write(test, "-xassembler-with-cpp", "#define FORCED_LANGUAGE 1\n");
    cc_compat_expect(test, files, "write positional preprocessing language fixtures");
    static const struct
    {
        const char* name;
        const char* arguments[16];
        const char* expected;
        const char* oracle_path;
    } cases[] =
    {
        { "suffixes select C and preprocessed assembly independently", { "-E", "-P", "language.c", "language.S", NULL }, "c_mode$Nasm_mode$3", NULL },
        { "suffixes select assembly then C independently", { "-E", "-P", "language.S", "language.c", NULL }, "asm_mode$3c_mode$N", NULL },
        { "separate -x is positional", { "-E", "-P", "-x", "c", "language.S", "-x", "assembler-with-cpp", "language.c", NULL }, "c_mode$Nasm_mode$3", NULL },
        { "joined -x and separate none restore suffixes", { "-E", "-P", "-xc", "language.S", "-x", "none", "language.S", "language.c", NULL }, "c_mode$Nasm_mode$3c_mode$N", NULL },
        { "joined none restores raw and cpp suffixes", { "-E", "-P", "-xassembler-with-cpp", "language.c", "-xnone", "language.s", "language.S", NULL }, "asm_mode$3asm_mode$3", NULL },
        { "a later -x does not change an earlier input", { "-E", "-P", "language.c", "-xassembler-with-cpp", NULL }, "c_mode$N", NULL },
        { "raw assembly suffix has no preprocessing action", { "-E", "-P", "language.s", NULL }, "", NULL },
        { "separate assembler language has no preprocessing action", { "-E", "-P", "-x", "assembler", "language.c", NULL }, "", NULL },
        { "joined assembler language has no preprocessing action", { "-E", "-P", "-xassembler", "language.S", NULL }, "", NULL },
        { "an include operand is not a language option", { "-E", "-P", "-include", "-xassembler-with-cpp", "language.c", NULL }, "c_mode$N", NULL },
        { "a dependency target is not a language option", { "-E", "-P", "-MT", "-xassembler-with-cpp", "language.c", NULL }, "c_mode$N", NULL },
        { "a filename after -- is not a language option", { "-E", "-P", "--", "-xassembler-with-cpp.S", NULL }, "asm_mode$3", "./-xassembler-with-cpp.S" },
    };
    for (uint32_t i = 0; files && i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        const char* oracle_arguments[16] = { 0 };
        uint32_t oracle_count = 0;
        for (uint32_t a = 0; cases[i].arguments[a] != NULL; a += 1)
        {
            oracle_arguments[oracle_count++] = cases[i].arguments[a];
        }
        if (cases[i].oracle_path != NULL)
        {
            // The oracle reads the same file through ./; Buster still receives the bare option-looking name after --.
            // This keeps Clang's driver-to-cc1 argument rendering out of Buster's -- boundary regression.
            oracle_arguments[oracle_count - 1] = cases[i].oracle_path;
        }
        CCCompatibilityRun oracle = cc_compat_run(test, test->clang, NULL, oracle_arguments);
        cc_compat_language_expect(test, cases[i].name, "Clang oracle", &oracle, cases[i].expected);
        cc_compat_release(&oracle);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const char* arguments[18] = { 0 };
            uint32_t count = 0;
            if (scalar != 0)
            {
                arguments[count++] = "--scalar";
            }
            for (uint32_t a = 0; cases[i].arguments[a] != NULL; a += 1)
            {
                arguments[count++] = cases[i].arguments[a];
            }
            CCCompatibilityRun run = cc_compat_run(test, test->compiler, NULL, arguments);
            cc_compat_language_expect(test, cases[i].name, scalar != 0 ? "Buster scalar kernel" : "Buster default kernel", &run, cases[i].expected);
            cc_compat_release(&run);
        }
    }
    const bool kept = cc_compat_write(test, "raw-output.s", "previous output\n");
    const char* const raw_output[] = { "-E", "language.s", "-o", "raw-output.s", NULL };
    CCCompatibilityRun raw = cc_compat_run(test, test->compiler, NULL, raw_output);
    char* previous = cc_compat_read(test, "raw-output.s");
    cc_compat_expect(test, kept && raw.status == 0 && previous != NULL && strcmp(previous, "previous output\n") == 0,
                     "raw assembly leaves -o untouched");
    free(previous);
    cc_compat_release(&raw);
    const char* const raw_stdin[] = { "-E", "-xassembler", "-", NULL };
    raw = cc_compat_run(test, test->compiler, "language.s", raw_stdin);
    cc_compat_expect(test, raw.status == 0 && raw.output != NULL && raw.output[0] == 0, "raw assembly stdin is not preprocessed");
    cc_compat_release(&raw);
    const char* const invalid_language[] = { "-E", "language.c", "-x", "unknown-language", NULL };
    raw = cc_compat_run(test, test->compiler, NULL, invalid_language);
    cc_compat_expect(test, raw.status != 0 && raw.output != NULL && raw.output[0] == 0, "invalid later language fails before output");
    cc_compat_release(&raw);
    const char* const mixed_zvector[] = { "-E", "-fzvector", "language.S", "language.c", NULL };
    raw = cc_compat_run(test, test->compiler, NULL, mixed_zvector);
    cc_compat_expect(test, raw.status != 0 && raw.output != NULL && raw.output[0] == 0,
                     "assembly-only option exceptions do not bypass a C input's refusal");
    cc_compat_release(&raw);

    // Reassemble the actual -E text as raw .s, then execute a C consumer. An unexpanded $VALUE becomes a symbol
    // relocation rather than the intended immediate, so linking and execution catch the original silent change.
    files = cc_compat_write(test, "roundtrip.S", "#define VALUE 42\n.text\n.globl language_value\n.type language_value,@function\nlanguage_value:\n mov $VALUE, %eax\n ret\n.section .note.GNU-stack,\"\",@progbits\n") &&
            cc_compat_write(test, "roundtrip.c", "int language_value(void); int main(void) { return language_value() != 42; }\n");
    const char* const preprocess[] = { "-E", "roundtrip.S", "-o", "roundtrip.s", NULL };
    CCCompatibilityRun expanded = cc_compat_run(test, test->compiler, NULL, preprocess);
    cc_compat_expect(test, files && expanded.status == 0, "preprocess .S round-trip source");
    cc_compat_release(&expanded);
    const char* sources[] = { "roundtrip.S", "roundtrip.s" };
    for (uint32_t i = 0; files && i < 2; i += 1)
    {
        const char* const assemble[] = { "-c", sources[i], "-o", "roundtrip.o", NULL };
        CCCompatibilityRun object = cc_compat_run(test, test->compiler, NULL, assemble);
        cc_compat_expect(test, object.status == 0, "assemble original or preprocessed assembly");
        if (object.status == 0)
        {
            const char* const link[] = { "roundtrip.c", "roundtrip.o", "-o", "roundtrip", NULL };
            CCCompatibilityRun program = cc_compat_run(test, test->compiler, NULL, link);
            cc_compat_expect(test, program.status == 0, "link original or preprocessed assembly");
            if (program.status == 0)
            {
                const char* const arguments[] = { NULL };
                CCCompatibilityRun executed = cc_compat_run(test, "./roundtrip", NULL, arguments);
                cc_compat_expect(test, executed.status == 0, "original and -E assembly return the same immediate");
                cc_compat_release(&executed);
            }
            cc_compat_release(&program);
        }
        cc_compat_release(&object);
    }
}

// A FIFO has no size. The writer blocks in open until the compiler opens it, then the compiler reads until the
// writer closes. The alarm keeps a compiler that never opens the FIFO from leaving the writer waiting.
static void cc_compat_fifo_source(CCCompatibilityTest* test)
{
    char fifo[PATH_MAX];
    snprintf(fifo, sizeof(fifo), "%s/stream.c", test->directory);
    const bool made = mkfifo(fifo, 0600) == 0;
    fflush(NULL);
    const pid_t writer = made ? fork() : -1;
    if (writer == 0)
    {
        alarm(15);
        const int fd = open(fifo, O_WRONLY);
        const char text[] = "int stream_value;\n";
        const bool wrote = fd >= 0 && write(fd, text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1);
        if (fd >= 0)
        {
            close(fd);
        }
        _exit(wrote ? 0 : 1);
    }
    const char* const args[] = { "-fsyntax-only", "stream.c", NULL };
    CCCompatibilityRun run = writer > 0 ? cc_compat_run(test, test->compiler, NULL, args) : (CCCompatibilityRun){ .status = -1 };
    int waited_status = 0;
    pid_t waited = -1;
    do
    {
        waited = writer > 0 ? waitpid(writer, &waited_status, 0) : -1;
    }
    while (waited < 0 && writer > 0 && errno == EINTR);
    const bool writer_ok = waited == writer && WIFEXITED(waited_status) && WEXITSTATUS(waited_status) == 0;
    cc_compat_expect(test, made && writer > 0 && writer_ok && run.status == 0, "FIFO source is read until EOF");
    if (!(made && writer_ok && run.status == 0))
    {
        fprintf(stderr, "  fifo diagnostics: %s\n", run.errors != NULL ? run.errors : "(missing)");
    }
    cc_compat_release(&run);
    unlink(fifo);
}

// /dev/fd/N is the process's own pipe. The write end is closed before the compiler runs, so read() reaches EOF.
static void cc_compat_pipe_source(CCCompatibilityTest* test)
{
    int ends[2] = { -1, -1 };
    const bool opened = pipe(ends) == 0;
    fflush(NULL);
    const pid_t writer = opened ? fork() : -1;
    if (writer == 0)
    {
        if (ends[0] >= 0)
        {
            close(ends[0]);
        }
        const char text[] = "int pipe_value;\n";
        const bool wrote = ends[1] >= 0 && write(ends[1], text, sizeof(text) - 1) == (ssize_t)(sizeof(text) - 1);
        if (ends[1] >= 0)
        {
            close(ends[1]);
        }
        _exit(wrote ? 0 : 1);
    }
    if (ends[1] >= 0)
    {
        close(ends[1]);
        ends[1] = -1;
    }
    char spec[64];
    snprintf(spec, sizeof(spec), "/dev/fd/%d", ends[0]);
    const char* const args[] = { "-fsyntax-only", "-x", "c", spec, NULL };
    CCCompatibilityRun run = opened && writer > 0 ? cc_compat_run(test, test->compiler, NULL, args) : (CCCompatibilityRun){ .status = -1 };
    int waited_status = 0;
    pid_t waited = -1;
    do
    {
        waited = writer > 0 ? waitpid(writer, &waited_status, 0) : -1;
    }
    while (waited < 0 && writer > 0 && errno == EINTR);
    const bool writer_ok = waited == writer && WIFEXITED(waited_status) && WEXITSTATUS(waited_status) == 0;
    cc_compat_expect(test, opened && writer_ok && run.status == 0, "/dev/fd pipe source is read until EOF");
    if (!(opened && writer_ok && run.status == 0))
    {
        fprintf(stderr, "  pipe diagnostics: %s\n", run.errors != NULL ? run.errors : "(missing)");
    }
    cc_compat_release(&run);
    if (ends[0] >= 0)
    {
        close(ends[0]);
    }
}

static int driver_compatibility_tests(const char* compiler, const char* clang)
{
    CCCompatibilityTest test = { .clang = clang != NULL ? clang : "clang" };
    snprintf(test.directory, sizeof(test.directory), "/tmp/buster-cc-compat-XXXXXX");
    const bool ready = realpath(compiler != NULL ? compiler : "/proc/self/exe", test.compiler) != NULL && mkdtemp(test.directory) != NULL;
    cc_compat_expect(&test, ready, "prepare private CC fixtures");
    if (ready)
    {
        const char* directories[] = { "sys", "inc", "out" };
        bool files = true;
        for (uint32_t d = 0; d < 3; d += 1)
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", test.directory, directories[d]);
            files &= mkdir(path, 0700) == 0;
        }
        files &= cc_compat_write(&test, "a.c", "#include \"h.h\"\nint a(void) {return VALUE;}\n");
        files &= cc_compat_write(&test, "b.c", "#include \"h.h\"\nint b(void) {return VALUE+1;}\n");
        files &= cc_compat_write(&test, "h.h", "#ifndef HEADER_H\n#define HEADER_H\n#define VALUE 7\n#endif\n");
        files &= cc_compat_write(&test, "forced.h", "#define FORCED 1\n");
        files &= cc_compat_write(&test, "sys/system.h", "#include \"../inc/nested.h\"\n");
        files &= cc_compat_write(&test, "inc/nested.h", "#pragma once\n#define NESTED 1\n");
        files &= cc_compat_write(&test, "system.c", "#include <system.h>\nint system_value;\n");
        files &= cc_compat_write(&test, "probe.c", "#if __has_include(\"h.h\")\nint probe;\n#endif\n#if 0\n#include \"unused.h\"\n#endif\n");
        files &= cc_compat_write(&test, "missing.c", "#include \"absent.h\"\n");
        files &= cc_compat_write(&test, "bad.c", "int broken = ;\n");
        files &= cc_compat_write(&test, "stdin.txt", "#include \"h.h\"\nint stdin_value = VALUE;\n");
        files &= cc_compat_write(&test, "space #$.h", "#define SPACED 1\n");
        files &= cc_compat_write(&test, "space.c", "#include \"space #$.h\"\nint space;\n");
        files &= cc_compat_write(&test, "main.c", "extern int puts(const char*);\n_Thread_local int local = 7;\nint main(void) { puts(\"CC smoke\"); return local != 7; }\n");
        cc_compat_expect(&test, files, "write CC fixtures");
        cc_compat_preprocessor_languages(&test);
        const char* const basic[] = { "-M", "./a.c", NULL };
        cc_compat_oracle(&test, "-M defaults and leading ./ normalization", NULL, basic, NULL);
        const char* const system_all[] = { "-M", "-isystem", "sys", "system.c", NULL };
        cc_compat_oracle(&test, "-M includes system descendants", NULL, system_all, NULL);
        const char* const system_user[] = { "-MM", "-isystem", "sys", "system.c", NULL };
        cc_compat_oracle(&test, "-MM excludes system descendants", NULL, system_user, NULL);
        const char* const probe[] = { "-M", "probe.c", NULL };
        cc_compat_oracle(&test, "positive __has_include is observed, inactive include is not", NULL, probe, NULL);
        const char* const forced[] = { "-MM", "-include", "forced.h", "a.c", NULL };
        cc_compat_oracle(&test, "forced header and root ordering", NULL, forced, NULL);
        const char* const guards[] = { "-M", "-DHEADER_H", "a.c", NULL };
        cc_compat_oracle(&test, "guard-skipped header remains a prerequisite", NULL, guards, NULL);
        const char* const targets[] = { "-MM", "-MT", "raw $(V)", "-MQ", "out $x", "-MTlast", "-MP", "a.c", NULL };
        cc_compat_oracle(&test, "ordered raw/quoted targets, joined argument, phony rules", NULL, targets, NULL);
        const char* const spaces[] = { "-M", "-MQ", "out #$.o", "-MP", "space.c", NULL };
        cc_compat_oracle(&test, "Make space/hash/dollar escaping", NULL, spaces, NULL);
        const char* const md[] = { "-MD", "-c", "a.c", "-o", "out/a.o", NULL };
        cc_compat_oracle(&test, "-MD with explicit object directory", NULL, md, "out/a.d");
        const char* const static_pp[] = { "-E", "-P", "-static", "a.c", NULL };
        cc_compat_oracle(&test, "-E and -M ignore the static link mode", NULL, static_pp, NULL);
        // Clang's depfile is fopen's: a new file has 0666 less the umask, and replacing one keeps its mode. mkstemp's
        // private 0600 must not leak out of the atomic temporary.
        char mode_depfile[PATH_MAX];
        snprintf(mode_depfile, sizeof(mode_depfile), "%s/out/mode.d", test.directory);
        const mode_t previous_umask = umask(022);
        const char* const mode_arguments[] = { "-MD", "-c", "a.c", "-o", "out/mode.o", NULL };
        CCCompatibilityRun mode_run = cc_compat_run(&test, test.compiler, NULL, mode_arguments);
        struct stat mode_status;
        const bool mode_new = mode_run.status == 0 && stat(mode_depfile, &mode_status) == 0 && (mode_status.st_mode & 0777) == 0644;
        cc_compat_expect(&test, mode_new, "a new depfile has fopen's permissions");
        cc_compat_release(&mode_run);
        unlink(mode_depfile);
        const int mode_file = open(mode_depfile, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        const bool mode_written = mode_file >= 0 && fchmod(mode_file, 0640) == 0;
        if (mode_file >= 0)
        {
            close(mode_file);
        }
        mode_run = cc_compat_run(&test, test.compiler, NULL, mode_arguments);
        const bool mode_kept = mode_written && mode_run.status == 0 && stat(mode_depfile, &mode_status) == 0 && (mode_status.st_mode & 0777) == 0640;
        cc_compat_expect(&test, mode_kept, "a replaced depfile keeps its permissions");
        cc_compat_release(&mode_run);
        umask(previous_umask);
        unlink(mode_depfile);
        const char* const mmd[] = { "-MMD", "-MFjoined.d", "-c", "a.c", "-o", "out/a.o", NULL };
        cc_compat_oracle(&test, "-MMD with joined -MF", NULL, mmd, "joined.d");
        const char* const phony_arguments[] = { "-MMD", "-MP", "-MF", "out/phony.d", "-c", "a.c", "-o", "out/phony.o", NULL };
        CCCompatibilityRun phony_depfile = cc_compat_run(&test, test.compiler, NULL, phony_arguments);
        char* phony_rule = cc_compat_read(&test, "out/phony.d");
        cc_compat_expect(&test, phony_depfile.status == 0 && phony_rule != NULL && strcmp(phony_rule, "out/phony.o: a.c h.h\nh.h:\n") == 0,
                         "-MP phony rules have Clang's exact spacing");
        free(phony_rule);
        cc_compat_release(&phony_depfile);
        cc_compat_long_depfiles(&test);
#if !defined(BUSTER_DRIVER_COMPAT_STANDALONE) && !defined(BUSTER_DRIVER_MIXED_STANDALONE)
        cc_compat_publication_failure(&test);
#endif
        const char* const ep[] = { "-E", "-MD", "-P", "a.c", "-o", "out/a.i", NULL };
        cc_compat_oracle(&test, "-E -MD -o produces primary and side dependency", NULL, ep, "out/a.d");
        const char* const mixed[] = { "-MD", "-M", "a.c", NULL };
        cc_compat_oracle(&test, "dependency side-effect precedence is independent of order", NULL, mixed, "a.d");
        const char* const mixed_system[] = { "-MM", "-M", "-MD", "-isystem", "sys", "system.c", NULL };
        cc_compat_oracle(&test, "-MM takes precedence for system filtering", NULL, mixed_system, "system.d");
        const char* const mo[] = { "-M", "-o", "only.d", "a.c", NULL };
        cc_compat_oracle(&test, "-M -o is a depfile, not an object target", NULL, mo, "only.d");
        const char* const syntax[] = { "-fsyntax-only", "-MMD", "a.c", NULL };
        cc_compat_oracle(&test, "syntax-only side dependency", NULL, syntax, "a.d");
        const char* const standard_input[] = { "-x", "c", "-M", "-", NULL };
        cc_compat_oracle(&test, "stdin has no filesystem prerequisite", "stdin.txt", standard_input, NULL);
        const char* const stdin_object[] = { "-x", "c", "-MMD", "-c", "-", "-o", "out/stdin.o", NULL };
        cc_compat_oracle(&test, "stdin object and side dependency", "stdin.txt", stdin_object, "out/stdin.d");
        const char* const stdin_pp[] = { "-E", "-P", "-", NULL };
        cc_compat_oracle(&test, "stdin preprocessing", "stdin.txt", stdin_pp, NULL);
        const char* const multi[] = { "-M", "a.c", "b.c", NULL };
        cc_compat_oracle(&test, "multiple dependency-only units in argv order", NULL, multi, NULL);
        const char* const shared[] = { "-c", "-MD", "-MF", "shared.d", "a.c", "b.c", NULL };
        cc_compat_oracle(&test, "parallel units, shared -MF deterministic last rule", NULL, shared, "shared.d");
        const char* const action_value[] = { "-c", "-MD", "-MF", "-E", "-MT", "--version", "a.c", NULL };
        cc_compat_oracle(&test, "option values cannot become driver actions", NULL, action_value, "-E");
        const char* const absent[] = { "-M", "-MF", "keep.d", "missing.c", NULL };
        cc_compat_failure(&test, "preprocessing failure preserves old depfile", NULL, absent);
        const char* const malformed[] = { "-c", "-MD", "-MF", "keep.d", "bad.c", NULL };
        cc_compat_failure(&test, "semantic failure preserves old depfile", NULL, malformed);
        const char* const bad_target[] = { "-M", "-MF", "keep.d", "-MT", "bad\ntarget", "a.c", NULL };
        cc_compat_failure(&test, "malformed target fails before publication", NULL, bad_target);
        const char* const missing_value[] = { "-M", "a.c", "-MF", NULL };
        cc_compat_failure(&test, "missing option operand rejected", NULL, missing_value);
        const char* const bad_stdin[] = { "-x", "c", "-MD", "-MF", "keep.d", "-fsyntax-only", "-", NULL };
        cc_compat_failure(&test, "stdin diagnostic replay preserves failure", "bad.c", bad_stdin);
        const char* const alias[] = { "-M", "-MF", "a.c", "a.c", NULL };
        CCCompatibilityRun collision = cc_compat_run(&test, test.compiler, NULL, alias);
        char* source = cc_compat_read(&test, "a.c");
        cc_compat_expect(&test, collision.status != 0 && source != NULL && strncmp(source, "#include", 8) == 0, "depfile cannot overwrite its source");
        free(source);
        cc_compat_release(&collision);
        const bool protected_object = cc_compat_write(&test, "a.o", "old object\n");
        const char* const object_alias[] = { "-c", "-MD", "-MF", "a.o", "a.c", NULL };
        CCCompatibilityRun object_collision = cc_compat_run(&test, test.compiler, NULL, object_alias);
        char* old_object = cc_compat_read(&test, "a.o");
        cc_compat_expect(&test, protected_object && object_collision.status != 0 && old_object != NULL && strcmp(old_object, "old object\n") == 0,
                         "depfile cannot overwrite default object; rejection preserves existing object");
        free(old_object);
        cc_compat_release(&object_collision);
        const bool protected_program = cc_compat_write(&test, "a.out", "old program\n");
        const char* const executable_alias[] = { "-MD", "-MF", "a.out", "main.c", NULL };
        CCCompatibilityRun program_collision = cc_compat_run(&test, test.compiler, NULL, executable_alias);
        char* old_program = cc_compat_read(&test, "a.out");
        cc_compat_expect(&test, protected_program && program_collision.status != 0 && old_program != NULL && strcmp(old_program, "old program\n") == 0,
                         "depfile cannot overwrite default executable; rejection preserves existing executable");
        free(old_program);
        cc_compat_release(&program_collision);
        char new_object[PATH_MAX];
        snprintf(new_object, sizeof(new_object), "%s/new.o", test.directory);
        unlink(new_object);
        const char* const lexical_alias[] = { "-c", "-MD", "-MF", "./new.o", "a.c", "-o", "new.o", NULL };
        CCCompatibilityRun lexical_collision = cc_compat_run(&test, test.compiler, NULL, lexical_alias);
        cc_compat_expect(&test, lexical_collision.status != 0 && access(new_object, F_OK) != 0, "nonexistent aliases rejected before creating primary output");
        cc_compat_release(&lexical_collision);
        struct stat before_null;
        struct stat after_null;
        const bool null_before = stat("/dev/null", &before_null) == 0;
        const char* const null_destination[] = { "-M", "-MF", "/dev/null", "a.c", NULL };
        CCCompatibilityRun null_output = cc_compat_run(&test, test.compiler, NULL, null_destination);
        cc_compat_expect(&test, null_before && null_output.status == 0 && stat("/dev/null", &after_null) == 0 && S_ISCHR(after_null.st_mode) &&
                               before_null.st_ino == after_null.st_ino && before_null.st_dev == after_null.st_dev, "/dev/null remains a character device");
        cc_compat_release(&null_output);
        const char* const directory_destination[] = { "-M", "-MF", "out", "a.c", NULL };
        CCCompatibilityRun directory_output = cc_compat_run(&test, test.compiler, NULL, directory_destination);
        char directory_path[PATH_MAX];
        struct stat directory_status;
        snprintf(directory_path, sizeof(directory_path), "%s/out", test.directory);
        cc_compat_expect(&test, directory_output.status != 0 && stat(directory_path, &directory_status) == 0 && S_ISDIR(directory_status.st_mode), "directory cannot be replaced by dependency output");
        cc_compat_release(&directory_output);
        const char* const macros[] = { "-E", "-dM", "-", NULL };
        CCCompatibilityRun macro = cc_compat_run(&test, test.compiler, "stdin.txt", macros);
        cc_compat_expect(&test, macro.status == 0 && macro.output != NULL && strstr(macro.output, "#define VALUE 7") != NULL, "echo-style macro query from stdin");
        cc_compat_release(&macro);
        const char* const verbose[] = { "-v", "-E", "-P", "a.c", NULL };
        CCCompatibilityRun verbose_run = cc_compat_run(&test, test.compiler, NULL, verbose);
        cc_compat_expect(&test, verbose_run.status == 0 && verbose_run.errors != NULL && strstr(verbose_run.errors, "buster") != NULL &&
                               verbose_run.output != NULL && strstr(verbose_run.output, "buster") == NULL, "-v stays off machine-readable stdout");
        cc_compat_release(&verbose_run);
        const char* const bare_verbose[] = { "-v", NULL };
        CCCompatibilityRun bare = cc_compat_run(&test, test.compiler, NULL, bare_verbose);
        cc_compat_expect(&test, bare.status == 0 && bare.output != NULL && bare.output[0] == 0 && bare.errors != NULL && strstr(bare.errors, "Target:") != NULL, "bare -v");
        cc_compat_release(&bare);
        const char* const version[] = { "-dumpversion", NULL };
        CCCompatibilityRun number = cc_compat_run(&test, test.compiler, NULL, version);
        cc_compat_expect(&test, number.status == 0 && number.output != NULL && strcmp(number.output, "0.0.0\n") == 0, "numeric development version, not a spoofed Clang version");
        cc_compat_release(&number);
        const char* const search[] = { "-print-search-dirs", NULL };
        CCCompatibilityRun paths = cc_compat_run(&test, test.compiler, NULL, search);
        cc_compat_expect(&test, paths.status == 0 && paths.output != NULL && strstr(paths.output, "libraries: =") != NULL && strstr(paths.output, "programs: =") != NULL, "search-directory probe");
        cc_compat_release(&paths);
        const char* const unknown[] = { "-print-file-name=does-not-exist-1113.a", NULL };
        CCCompatibilityRun absent_file = cc_compat_run(&test, test.compiler, NULL, unknown);
        cc_compat_expect(&test, absent_file.status == 0 && absent_file.output != NULL && strcmp(absent_file.output, "does-not-exist-1113.a\n") == 0, "unknown library probe returns its spelling");
        cc_compat_release(&absent_file);
        const char* const archive[] = { "-print-file-name=libgcc.a", NULL };
        CCCompatibilityRun gcc = cc_compat_run(&test, test.compiler, NULL, archive);
        bool archive_found = gcc.status == 0 && gcc.output != NULL && gcc.output[0] == '/';
        if (archive_found)
        {
            gcc.output[strcspn(gcc.output, "\n")] = 0;
            archive_found = access(gcc.output, R_OK) == 0;
        }
        cc_compat_expect(&test, archive_found, "runtime library probe resolves an existing archive");
        cc_compat_release(&gcc);
        const char* const combined[] = { "-print-search-dirs", "-print-file-name=libgcc.a", NULL };
        CCCompatibilityRun both = cc_compat_run(&test, test.compiler, NULL, combined);
        CCCompatibilityRun file_only = cc_compat_run(&test, test.compiler, NULL, archive);
        const bool file_line = file_only.status == 0 && file_only.output != NULL && file_only.output[0] == '/';
        cc_compat_expect(&test, both.status == 0 && both.output != NULL && strstr(both.output, "libraries: =") != NULL && file_line &&
                               strstr(both.output, file_only.output) != NULL, "search dirs keep an accompanying file lookup");
        cc_compat_release(&both);
        cc_compat_release(&file_only);
        const char* const file_first[] = { "-print-file-name=libgcc.a", "-print-search-dirs", NULL };
        CCCompatibilityRun first = cc_compat_run(&test, test.compiler, NULL, file_first);
        CCCompatibilityRun file_again = cc_compat_run(&test, test.compiler, NULL, archive);
        cc_compat_expect(&test, first.status == 0 && first.output != NULL && file_again.output != NULL && strstr(first.output, "programs: =") != NULL &&
                               strstr(first.output, file_again.output) != NULL, "file lookup before search dirs is still printed");
        cc_compat_release(&first);
        cc_compat_release(&file_again);
        const char* const empty_name[] = { "-print-file-name=", NULL };
        CCCompatibilityRun empty_file = cc_compat_run(&test, test.compiler, NULL, empty_name);
        cc_compat_expect(&test, empty_file.status == 0 && empty_file.output != NULL && strcmp(empty_file.output, "\n") == 0, "empty file lookup is not a directory");
        cc_compat_release(&empty_file);
        const bool stale = cc_compat_write(&test, "real.d", "stale\n");
        char link_path[PATH_MAX];
        snprintf(link_path, sizeof(link_path), "%s/dep.d", test.directory);
        unlink(link_path);
        const bool linked = symlink("real.d", link_path) == 0;
        const char* const through[] = { "-MD", "-c", "a.c", "-MF", "dep.d", "-o", "out/same.o", NULL };
        CCCompatibilityRun through_run = cc_compat_run(&test, test.compiler, NULL, through);
        struct stat link_status;
        char target[PATH_MAX];
        const ssize_t target_length = readlink(link_path, target, sizeof(target) - 1);
        if (target_length >= 0)
        {
            target[target_length] = 0;
        }
        char* updated = cc_compat_read(&test, "real.d");
        const char* const plain[] = { "-MD", "-c", "a.c", "-MF", "plain.d", "-o", "out/same.o", NULL };
        CCCompatibilityRun plain_run = cc_compat_run(&test, test.compiler, NULL, plain);
        char* plain_rule = cc_compat_read(&test, "plain.d");
        cc_compat_expect(&test, stale && linked && through_run.status == 0 && plain_run.status == 0 && lstat(link_path, &link_status) == 0 &&
                               S_ISLNK(link_status.st_mode) && target_length > 0 && strcmp(target, "real.d") == 0 && updated != NULL &&
                               plain_rule != NULL && strcmp(updated, "stale\n") != 0 && strcmp(updated, plain_rule) == 0,
                         "a symlinked depfile is written through and left a symlink");
        if (!(through_run.status == 0 && updated != NULL && plain_rule != NULL && strcmp(updated, plain_rule) == 0))
        {
            fprintf(stderr, "  symlink depfile status %d, target: %s\n  updated: %s\n  plain: %s\n  diagnostics: %s\n", through_run.status,
                    target_length > 0 ? target : "(none)", updated != NULL ? updated : "(missing)", plain_rule != NULL ? plain_rule : "(missing)",
                    through_run.errors != NULL ? through_run.errors : "(missing)");
        }
        free(updated);
        free(plain_rule);
        cc_compat_release(&through_run);
        cc_compat_release(&plain_run);
        const char* const null_source[] = { "-dM", "-E", "-x", "c", "/dev/null", NULL };
        CCCompatibilityRun null_predefines = cc_compat_run(&test, test.compiler, NULL, null_source);
        cc_compat_expect(&test, null_predefines.status == 0 && null_predefines.output != NULL && strstr(null_predefines.output, "#define") != NULL,
                         "/dev/null is an empty translation unit");
        if (null_predefines.status != 0)
        {
            fprintf(stderr, "  /dev/null diagnostics: %s\n", null_predefines.errors != NULL ? null_predefines.errors : "(missing)");
        }
        cc_compat_release(&null_predefines);
        const char* const null_object[] = { "-c", "-x", "c", "/dev/null", "-o", "null.o", NULL };
        CCCompatibilityRun null_compile = cc_compat_run(&test, test.compiler, NULL, null_object);
        char null_object_path[PATH_MAX];
        struct stat null_object_status;
        snprintf(null_object_path, sizeof(null_object_path), "%s/null.o", test.directory);
        cc_compat_expect(&test, null_compile.status == 0 && stat(null_object_path, &null_object_status) == 0 && S_ISREG(null_object_status.st_mode) &&
                               null_object_status.st_size > 0, "/dev/null compiles to an object");
        if (null_compile.status != 0)
        {
            fprintf(stderr, "  /dev/null -c diagnostics: %s\n", null_compile.errors != NULL ? null_compile.errors : "(missing)");
        }
        cc_compat_release(&null_compile);
        const char* const dev_stdin[] = { "-dM", "-E", "-x", "c", "/dev/stdin", NULL };
        CCCompatibilityRun stdin_device = cc_compat_run(&test, test.compiler, NULL, dev_stdin);
        cc_compat_expect(&test, stdin_device.status == 0 && stdin_device.output != NULL && strstr(stdin_device.output, "#define") != NULL,
                         "/dev/stdin is read to EOF");
        if (stdin_device.status != 0)
        {
            fprintf(stderr, "  /dev/stdin diagnostics: %s\n", stdin_device.errors != NULL ? stdin_device.errors : "(missing)");
        }
        cc_compat_release(&stdin_device);
        cc_compat_fifo_source(&test);
        cc_compat_pipe_source(&test);
        const char* modes[] = { "-pie", "-static", "-static-pie" };
        for (uint32_t m = 0; m < 3; m += 1)
        {
            const char* args[] = { modes[m], "main.c", "-o", "program", NULL };
            CCCompatibilityRun linked = cc_compat_run(&test, test.compiler, NULL, args);
            const bool elf = linked.status == 0 && cc_compat_elf(&test, "program", m == 1 ? ET_EXEC : ET_DYN, m == 0);
            cc_compat_expect(&test, elf, modes[m]);
            if (!elf)
            {
                fprintf(stderr, "  link diagnostics: %s\n", linked.errors != NULL ? linked.errors : "(missing)");
            }
            if (elf)
            {
                const char* no_arguments[] = { NULL };
                CCCompatibilityRun executed = cc_compat_run(&test, "./program", NULL, no_arguments);
                cc_compat_expect(&test, executed.status == 0 && executed.output != NULL && strcmp(executed.output, "CC smoke\n") == 0, "requested runtime executes with TLS and libc");
                cc_compat_release(&executed);
            }
            cc_compat_release(&linked);
        }
        const char* const static_order[] = { "-static", "-pie", "main.c", "-o", "program", NULL };
        CCCompatibilityRun priority = cc_compat_run(&test, test.compiler, NULL, static_order);
        cc_compat_expect(&test, priority.status == 0 && cc_compat_elf(&test, "program", ET_EXEC, false), "-pie cannot downgrade a static request");
        cc_compat_release(&priority);
        const char* const static_pie_order[] = { "-static-pie", "-static", "main.c", "-o", "program", NULL };
        priority = cc_compat_run(&test, test.compiler, NULL, static_pie_order);
        cc_compat_expect(&test, priority.status == 0 && cc_compat_elf(&test, "program", ET_DYN, false), "-static cannot downgrade a static PIE request");
        cc_compat_release(&priority);
        // Feed an actually escaped target/prerequisite rule to GNU make, not just a string oracle.
        files = cc_compat_write(&test, "Makefile", "include space.d\nall: out\\ \\#$$.o\n\t@echo dependency-ready\n");
        const char* const make_rule[] = { "-M", "-MF", "space.d", "-MQ", "out #$.o", "-MP", "space.c", NULL };
        CCCompatibilityRun emitted = cc_compat_run(&test, test.compiler, NULL, make_rule);
        const char* const make_args[] = { "-f", "Makefile", "all", NULL };
        CCCompatibilityRun make = cc_compat_run(&test, "make", NULL, make_args);
        cc_compat_expect(&test, files && emitted.status == 0 && make.status == 0 && make.output != NULL && strstr(make.output, "dependency-ready") != NULL, "GNU make consumes escaped rule");
        cc_compat_release(&emitted);
        cc_compat_release(&make);
        const char* const phony[] = { "-M", "-MP", "a.c", NULL };
        CCCompatibilityRun phony_run = cc_compat_run(&test, test.compiler, NULL, phony);
        const bool phony_text = phony_run.status == 0 && phony_run.output != NULL && strstr(phony_run.output, "\n\n") == NULL && strstr(phony_run.output, "h.h:") != NULL;
        cc_compat_expect(&test, phony_text, "-MP phony rules have no blank line");
        if (!phony_text)
        {
            fprintf(stderr, "  phony output: %s\n", phony_run.output != NULL ? phony_run.output : "(missing)");
            fprintf(stderr, "  phony diagnostics: %s\n", phony_run.errors != NULL ? phony_run.errors : "(missing)");
        }
        cc_compat_release(&phony_run);
        const char* const split_joined[] = { "-I-", "-fsyntax-only", "a.c", NULL };
        CCCompatibilityRun split_joined_run = cc_compat_run(&test, test.compiler, NULL, split_joined);
        cc_compat_expect(&test, split_joined_run.status != 0 && split_joined_run.errors != NULL &&
                               strstr(split_joined_run.errors, "'-I-' not supported, please use -iquote instead") != NULL, "joined -I- is rejected");
        if (split_joined_run.status == 0 || split_joined_run.errors == NULL || strstr(split_joined_run.errors, "'-I-' not supported") == NULL)
        {
            fprintf(stderr, "  -I- diagnostics: %s\n", split_joined_run.errors != NULL ? split_joined_run.errors : "(missing)");
        }
        cc_compat_release(&split_joined_run);
        const char* const split_separate[] = { "-I", "-", "-fsyntax-only", "a.c", NULL };
        CCCompatibilityRun split_separate_run = cc_compat_run(&test, test.compiler, NULL, split_separate);
        cc_compat_expect(&test, split_separate_run.status != 0 && split_separate_run.errors != NULL &&
                               strstr(split_separate_run.errors, "'-I-' not supported, please use -iquote instead") != NULL, "separate -I - is rejected");
        cc_compat_release(&split_separate_run);
        const char* const dash_directory[] = { "-I./-", "-fsyntax-only", "a.c", NULL };
        CCCompatibilityRun dash_directory_run = cc_compat_run(&test, test.compiler, NULL, dash_directory);
        cc_compat_expect(&test, dash_directory_run.status == 0, "a directory spelled ./- is still an include path");
        if (dash_directory_run.status != 0)
        {
            fprintf(stderr, "  ./- diagnostics: %s\n", dash_directory_run.errors != NULL ? dash_directory_run.errors : "(missing)");
        }
        cc_compat_release(&dash_directory_run);
        cc_compat_response_files(&test);
        cc_compat_cleanup(&test);
    }
    printf("CC compatibility: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures != 0;
}

#ifdef BUSTER_DRIVER_COMPAT_STANDALONE
int main(int argc, char** argv)
{
    return argc >= 2 && argc <= 3 ? driver_compatibility_tests(argv[1], argc == 3 ? argv[2] : "clang") : 2;
}
#endif
