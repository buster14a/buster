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
            // Without an object, -fsyntax-only.
            if (object != NULL)
            {
                arguments[count++] = "-c";
                arguments[count++] = "-o";
                arguments[count++] = (char*)object;
            }
            else
            {
                arguments[count++] = "-fsyntax-only";
            }
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
                snprintf(outcome->message, sizeof(outcome->message), "%s", strstr(at, "error: ") + 7);
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

// The direct path's -l libraries, through the production command (#145): a program that calls libm's functions, takes
// one's address and reads libm's signgam (data, through its GOT slot), linked with -lm by both paths into the same bytes,
// that runs and prints what this executable's own libm computes, needing libm.so.6 before libc.so.6; one that calls
// none, with -lm, needing libc.so.6 alone; and the refusals, each named: no -lm, a library no directory has, and an
// archive with members.
static void check_test_driver_libraries(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <math.h>\n#include <stdio.h>\ndouble (*pick)(double) = cos;\n"
                                  "int main(void) { double logarithm = lgamma(-0.5); printf(\"%f %f %f %f %f %d\\n\", sin(2), sqrt(2.0), pow(2.0, 10.0), "
                                  "pick(0.5), logarithm, signgam); return 0; }\n";
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
    const double logarithm = lgamma(-0.5);
    snprintf(expected, sizeof(expected), "%f %f %f %f %f %d\n", sin(2), sqrt(2.0), pow(2.0, 10.0), cos(0.5), logarithm, signgam);
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

// GNU C's inline assembly by the driver (#147): an executable, linked by the direct path from each path into the same
// bytes, whose naked function has a label another template takes the address of, and whose template calls libc's puts
// by name, an undefined global that binding takes from libc.so.6; and a label named as one of the unit's functions,
// which the emission refuses by name, as GNU as and clang refuse a symbol defined twice.
static void check_test_driver_asm_symbols(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] =
        "static __attribute__((used)) const char message[] = \"naked\";\n"
        "static __attribute__((naked, noinline)) long twice(long x)\n"
        "{\n"
        "    __asm__(\"leaq (%rdi,%rdi), %rax\\n\\tret\\nthrice:\\n\\tleaq (%rdi,%rdi,2), %rax\\n\\tret\\n\");\n"
        "}\n"
        "static long thrice_address(void) { long a; __asm__(\"leaq thrice(%%rip), %0\" : \"=r\"(a)); return a; }\n"
        "int main(void)\n"
        "{\n"
        "    long (*f)(long) = (long (*)(long))thrice_address();\n"
        "    __asm__ volatile(\"leaq message(%%rip), %%rdi\\n\\tcall puts\" ::: \"rax\", \"rcx\", \"rdx\", \"rsi\", \"rdi\", \"r8\", \"r9\", \"r10\", \"r11\", \"memory\");\n"
        "    return (int)(twice(5) + f(7)) - 31;\n"
        "}\n";
    static const char refused[] = "int tested(int n) { __asm__(\"tested: nop\"); return n; }\n";
    char source[256];
    char refused_source[256];
    char outputs[2][256];
    char object[256];
    snprintf(source, sizeof(source), "%s/asm_symbols.c", directory);
    snprintf(refused_source, sizeof(refused_source), "%s/asm_label_symbol.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/asm_symbols", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/asm_symbols_scalar", directory);
    snprintf(object, sizeof(object), "%s/asm_label_symbol.o", directory);
    const bool written = object_write_file(source, (const uint8_t*)program, sizeof(program) - 1) == OBJECT_ERROR_NONE &&
                         object_write_file(refused_source, (const uint8_t*)refused, sizeof(refused) - 1) == OBJECT_ERROR_NONE;
    char* text = NULL;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "a naked function's label and a template's call of puts link" : "the same, scalar");
        free(text);
    }
    check_test_expect(test, written && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of templates' symbols");
    char captured[64] = { 0 };
    FILE* output = written ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == 6 && memcmp(captured, "naked\n", 6) == 0, "the program's templates run: puts prints, and the label is a second entry");
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", object, refused_source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", object, refused_source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: inline assembly labels named as the unit's symbols") != NULL,
                          path == 0 ? "a label named as the unit's function is refused" : "the same, scalar");
        free(text);
    }
    unlink(source);
    unlink(refused_source);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(object);
}

// A program whose 128-bit integers' division, remainder and conversions to and from double and long double call
// libgcc.a's functions (#133's lowering), __fixdfti among them, whose member calls __fixunsdfti, which the program calls
// too.
static const char check_test_libgcc_program[] = "#include <stdio.h>\n"
                                                "static volatile unsigned __int128 dividend = ((unsigned __int128)1 << 100) + 12345;\n"
                                                "static volatile unsigned __int128 divisor = 1000003;\n"
                                                "static volatile __int128 negative = -((__int128)1 << 90) - 7;\n"
                                                "static volatile double value = 1e30;\n"
                                                "int main(void)\n"
                                                "{\n"
                                                "    unsigned __int128 q = dividend / divisor, r = dividend % divisor;\n"
                                                "    __int128 s = negative / 77, t = negative % 77;\n"
                                                "    unsigned __int128 back = (unsigned __int128)value;\n"
                                                "    __int128 signed_back = (__int128)-value;\n"
                                                "    printf(\"%llx %llx %llx %lld %.17g %.17g %.21Lg\\n\", (unsigned long long)(q >> 64), (unsigned long long)q,\n"
                                                "           (unsigned long long)r, (long long)t, (double)back, (double)signed_back, (long double)s);\n"
                                                "    return 0;\n"
                                                "}\n";

// libgcc.a in the direct path's executables (#136): the libgcc program (check_test_libgcc_program), linked by both
// paths into the same bytes, needing libc.so.6 alone (the members join the executable), must print what this
// executable computes of the same expressions.
static void check_test_driver_libgcc(CheckTest* test, const char* executable, const char* directory)
{
    char source[256];
    char outputs[2][256];
    snprintf(source, sizeof(source), "%s/libgcc.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/libgcc", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/libgcc-scalar", directory);
    const bool written = parse_test_write(source, check_test_libgcc_program, sizeof(check_test_libgcc_program) - 1);
    check_test_expect(test, written, "libgcc program written");
    char* text = NULL;
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "128-bit division and conversions link libgcc.a's members" : "libgcc.a's members, scalar");
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable with libgcc.a's members");
    char needed[256] = { 0 };
    check_test_expect(test, linked && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "libgcc.a's members in the executable, libc.so.6 needed alone");
    volatile unsigned __int128 dividend = ((unsigned __int128)1 << 100) + 12345;
    volatile unsigned __int128 divisor = 1000003;
    volatile __int128 negative = -((__int128)1 << 90) - 7;
    volatile double value = 1e30;
    const unsigned __int128 q = dividend / divisor;
    const unsigned __int128 r = dividend % divisor;
    const __int128 s = negative / 77;
    const __int128 t = negative % 77;
    const unsigned __int128 back = (unsigned __int128)value;
    const __int128 signed_back = (__int128)-value;
    char expected[256];
    snprintf(expected, sizeof(expected), "%llx %llx %llx %lld %.17g %.17g %.21Lg\n", (unsigned long long)(q >> 64), (unsigned long long)q, (unsigned long long)r,
             (long long)t, (double)back, (double)signed_back, (long double)s);
    char captured[256] = { 0 };
    FILE* output = linked ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == strlen(expected) && memcmp(captured, expected, got) == 0, "the program prints the 128-bit results");
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
}

// A function's target attribute in the bodies -c and the direct path select (#180, check_program_select): each selected
// body keeps its own instruction set though bodies before it aren't selected. A BMI2 function after an unselected one, in
// a baseline unit, must get BMI2 (an executable that runs where the host has it, by both paths into the same bytes); and
// a function with vectors after an unselected one whose attribute turns AVX-512VL off, in a znver5 unit, the unit's.
static void check_test_driver_target_selection(CheckTest* test, const char* executable, const char* directory)
{
    static const char bmi2[] =
        "static int unused(int x){return x*3;}\n"
        "__attribute__((target(\"bmi2\"))) static unsigned deposit(unsigned x,unsigned m){return __builtin_ia32_pdep_si(x,m);}\n"
        "static int also_unused(int x){return x+1;}\n"
        "int main(void){volatile unsigned x=0x5u,m=0xf0f0u;return deposit(x,m)==0x50u?0:1;}\n";
    static const char vectors[] =
        "typedef int v4si __attribute__((vector_size(16)));\n"
        "__attribute__((target(\"no-avx512vl\"))) static int scalar_only(int x){return x*5;}\n"
        "static int sum(int n){v4si a={n,1,2,3},b={4,5,6,7};a=a+b;return a[0]+a[3];}\n"
        "int main(void){volatile int n=1;return sum(n)==15?0:1;}\n";
    char sources[2][256];
    char outputs[3][256];
    snprintf(sources[0], sizeof(sources[0]), "%s/target-selection-bmi2.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/target-selection-vectors.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/target-selection", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/target-selection-scalar", directory);
    snprintf(outputs[2], sizeof(outputs[2]), "%s/target-selection-vectors.o", directory);
    const bool written = parse_test_write(sources[0], bmi2, sizeof(bmi2) - 1) && parse_test_write(sources[1], vectors, sizeof(vectors) - 1);
    check_test_expect(test, written, "the target attributes' selection programs written");
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=x86-64", "-o", outputs[0], sources[0], NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=x86-64", "-o", outputs[1], sources[0], NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "a selected BMI2 function after an unselected one links with BMI2" :
                                                         "a selected BMI2 function after an unselected one, scalar");
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        linked = linked && status == 0;
        free(text);
        char* const vector_object[] = { (char*)executable, "-march=znver5", "-c", "-o", outputs[2], sources[1], NULL };
        char* const scalar_object[] = { (char*)executable, "--scalar", "-march=znver5", "-c", "-o", outputs[2], sources[1], NULL };
        text = NULL;
        const int compiled = check_test_driver_spawn(path == 0 ? vector_object : scalar_object, &text);
        check_test_expect(test, compiled == 0, path == 0 ? "a selected function's vectors after an unselected no-avx512vl one compile" :
                                                           "a selected function's vectors after an unselected no-avx512vl one, scalar");
        if (compiled != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same BMI2 executable");
    if (linked && (x86_target_native().features & BUSTER_X86_FEATURE(BMI2)) != 0)
    {
        char* text = NULL;
        char* const run[] = { outputs[0], NULL };
        check_test_expect(test, check_test_driver_spawn(run, &text) == 0, "the selected BMI2 function deposits as PDEP does");
        free(text);
    }
    unlink(sources[0]);
    unlink(sources[1]);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(outputs[2]);
}

static bool check_test_driver_capture(const char* path, char* captured, size_t capacity);
static uint8_t* check_test_driver_read_aligned(const char* path, uint64_t* length);

// The unwinder in the direct path (#489): a program whose own code walks its stack with _Unwind_Backtrace, which GCC's
// driver takes from libgcc_s.so.1, as needed, and so must the direct path: by both paths, it must link without a
// diagnostic into an executable that names libgcc_s.so.1 (its DT_NEEDED entry and version need), the same bytes by
// both, and run, printing what it should. And one that calls __splitstack_getcontext, whose member of libgcc.a makes
// morestack.o join, whose _Unwind_Resume binds to libgcc_s.so.1 (#489), where #483 refused it, naming morestack.o:
// binding joins generic-morestack.o too, whose thread-locals it reaches by general- and local-dynamic accesses, which
// the writer relaxes in a dynamic PIE (#515), so it must link alike, into the same bytes by both paths, which name
// libgcc_s.so.1 and run, printing nothing. On a system without libgcc_s.so.1 (check_test_driver_has_gcc_s), binding
// refuses both instead: the
// backtrace's name as undefined, and morestack.o's _Unwind_Resume naming the member and saying the system has no
// libgcc_s.so.1.
static bool check_test_driver_has_gcc_s(void);

static void check_test_driver_split_stack(CheckTest* test, const char* executable, const char* directory)
{
    static const char split[] = "extern void __splitstack_getcontext(void* context[10]);\n"
                                "int main(void)\n"
                                "{\n"
                                "    void* context[10];\n"
                                "    __splitstack_getcontext(context);\n"
                                "    return 0;\n"
                                "}\n";
    static const char backtrace[] = "#include <stdio.h>\n"
                                    "struct _Unwind_Context;\n"
                                    "typedef int (*Trace)(struct _Unwind_Context* context, void* argument);\n"
                                    "extern int _Unwind_Backtrace(Trace trace, void* argument);\n"
                                    "static int count(struct _Unwind_Context* context, void* argument)\n"
                                    "{\n"
                                    "    (void)context;\n"
                                    "    *(int*)argument += 1;\n"
                                    "    return 0;\n"
                                    "}\n"
                                    "int main(void)\n"
                                    "{\n"
                                    "    int frames = 0;\n"
                                    "    _Unwind_Backtrace(count, &frames);\n"
                                    "    printf(\"%d\\n\", frames > 0);\n"
                                    "    return 0;\n"
                                    "}\n";
    static const struct
    {
        const char* name;
        const char* source;
        const char* output;
        // A part of the refusal without libgcc_s.so.1.
        const char* unwinder_refusal;
    } programs[] = {
        { "backtrace", backtrace, "1\n", "error: undefined reference to '_Unwind_Backtrace'\n" },
        { "split-stack", split, "",
          "(morestack.o): undefined reference to '_Unwind_Resume'; GCC's driver takes it from libgcc_s.so.1, which this system doesn't have\n" },
    };
    const bool unwinder = check_test_driver_has_gcc_s();
    if (!unwinder)
    {
        printf("  the unwinder: this system has no libgcc_s.so.1, so both programs must be refused\n");
    }
    for (uint32_t p = 0; p < sizeof(programs) / sizeof(programs[0]); p += 1)
    {
        char source[256];
        char outputs[2][256];
        snprintf(source, sizeof(source), "%s/%s.c", directory, programs[p].name);
        snprintf(outputs[0], sizeof(outputs[0]), "%s/%s", directory, programs[p].name);
        snprintf(outputs[1], sizeof(outputs[1]), "%s/%s-scalar", directory, programs[p].name);
        const bool written = parse_test_write(source, programs[p].source, strlen(programs[p].source));
        check_test_expect(test, written, "unwinder program written");
        uint8_t* bytes[2] = { NULL, NULL };
        uint64_t lengths[2] = { 0, 0 };
        for (uint32_t path = 0; written && path < 2; path += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
            const bool quiet = text == NULL || text[0] == 0;
            const bool refused = !unwinder;
            if (!unwinder)
            {
                const size_t length = text != NULL ? strlen(text) : 0;
                const bool worded = length != 0 && strstr(text, programs[p].unwinder_refusal) != NULL && strchr(text, '\n') == text + length - 1;
                check_test_expect(test, status == 1 && worded && access(outputs[path], F_OK) != 0,
                                  path == 0 ? "the unwinder's program refused without libgcc_s.so.1" : "the unwinder's program refused, scalar");
            }
            else if (status != 0 || !quiet)
            {
                printf("  %s%s: status %d: %s", programs[p].name, path == 0 ? "" : ", scalar", status, text != NULL ? text : "\n");
            }
            bytes[path] = status == 0 ? check_test_driver_read_aligned(outputs[path], &lengths[path]) : NULL;
            const uint8_t needed[] = "libgcc_s.so.1";
            const bool named = bytes[path] != NULL && memmem(bytes[path], lengths[path], needed, sizeof(needed)) != NULL;
            char captured[64];
            const bool ran = status == 0 && check_test_driver_capture(outputs[path], captured, sizeof(captured)) && strcmp(captured, programs[p].output) == 0;
            check_test_expect(test, refused || (status == 0 && quiet && named && ran),
                              path == 0 ? "the unwinder's program links, names libgcc_s.so.1 and runs" : "the unwinder's program, scalar");
            free(text);
        }
        check_test_expect(test,
                          !unwinder ||
                              (bytes[0] != NULL && bytes[1] != NULL && lengths[0] == lengths[1] && memcmp(bytes[0], bytes[1], lengths[0]) == 0),
                          "the unwinder's program the same by both paths");
        free(bytes[0]);
        free(bytes[1]);
        unlink(source);
        unlink(outputs[0]);
        unlink(outputs[1]);
    }
}

// Whether this system has libgcc_s.so.1 where GCC's driver finds it: cc -print-file-name names its path, or the name
// alone when it finds none.
static bool check_test_driver_has_gcc_s(void)
{
    FILE* output = popen("cc -print-file-name=libgcc_s.so.1", "r");
    char path[4096] = { 0 };
    const bool read = output != NULL && fgets(path, sizeof(path), output) != NULL;
    if (output != NULL)
    {
        pclose(output);
    }
    const size_t length = read ? strcspn(path, "\n") : 0;
    path[length] = 0;
    return length != 0 && path[0] == '/' && access(path, F_OK) == 0;
}

// A file's bytes, read whole into memory aligned to 64 bytes, as a snapshot's view takes them (snapshot_view), which the
// caller frees, and their count in *length; NULL if the file can't be read.
static uint8_t* check_test_driver_read_aligned(const char* path, uint64_t* length)
{
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool known = file >= 0 && fstat(file, &status) == 0 && status.st_size > 0;
    const uint64_t bytes = known ? (uint64_t)status.st_size : 0;
    uint8_t* result = known ? aligned_alloc(64, (bytes + 63) / 64 * 64) : NULL;
    uint64_t done = 0;
    bool reading = result != NULL;
    while (reading && done < bytes)
    {
        const ssize_t got = read(file, result + done, bytes - done);
        reading = got > 0;
        done += reading ? (uint64_t)got : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    if (result != NULL && done != bytes)
    {
        free(result);
        result = NULL;
    }
    *length = result != NULL ? bytes : 0;
    return result;
}

// Runs the executable at `path` and captures up to `capacity` - 1 bytes of its output into `captured`, NUL-terminated:
// whether it ran and exited with status 0.
static bool check_test_driver_capture(const char* path, char* captured, size_t capacity)
{
    FILE* output = popen(path, "r");
    const size_t got = output != NULL ? fread(captured, 1, capacity - 1, output) : 0;
    captured[got] = 0;
    return output != NULL && pclose(output) == 0;
}

// A damaged snapshot in the direct path (#483's review): the C runtime's snapshot in a cache directory of the test's own
// (XDG_CACHE_HOME), written by the driver's first link of the libgcc program, then the first atom's alignment and size
// of the member that defines __udivti3 changed in the file, to 2^255 and 2^64 - 1: records that only that member's piece
// hash covers, which the room the driver gives the link (bind_capacity_needed) mustn't read. Binding finds the piece
// damaged as it joins it (BIND_ERROR_SNAPSHOT), and the driver discards the snapshot and imports the runtime again: the
// second link must succeed without a word on its errors, its executable print what the first one's does, and the
// snapshot be the first link's again.
static void check_test_driver_damaged_snapshot(CheckTest* test, const char* executable, const char* directory)
{
    static const char name[] = "__udivti3";
    static const uint8_t ones[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    char cache[256];
    char place[320];
    char snapshot[512];
    char source[256];
    char output[256];
    snprintf(cache, sizeof(cache), "%s/cache", directory);
    snprintf(place, sizeof(place), "%s/buster", cache);
    snprintf(snapshot, sizeof(snapshot), "%s/%s", place, snapshot_name(RUNTIME_KIND_DYNAMIC));
    snprintf(source, sizeof(source), "%s/damaged.c", directory);
    snprintf(output, sizeof(output), "%s/damaged", directory);
    const char* previous = getenv("XDG_CACHE_HOME");
    char* saved = previous != NULL ? strdup(previous) : NULL;
    const bool written = mkdir(cache, 0700) == 0 && setenv("XDG_CACHE_HOME", cache, 1) == 0 &&
                         parse_test_write(source, check_test_libgcc_program, sizeof(check_test_libgcc_program) - 1);
    check_test_expect(test, written, "a damaged snapshot's cache and program written");
    char* const arguments[] = { (char*)executable, "-o", output, source, NULL };
    char* text = NULL;
    const int first = written ? check_test_driver_spawn(arguments, &text) : -1;
    free(text);
    char before[256];
    const bool ran = first == 0 && check_test_driver_capture(output, before, sizeof(before));
    check_test_expect(test, ran, "the libgcc program linked, writing the snapshot");
    uint64_t length = 0;
    uint8_t* good = ran ? check_test_driver_read_aligned(snapshot, &length) : NULL;
    // The places of the member's first atom's alignment and size in the file, by the layout of a view of a copy, which the
    // test's own reading of it (snapshot_test_piece_at) must find the piece's records.
    uint8_t* copy = good != NULL ? aligned_alloc(64, (length + 63) / 64 * 64) : NULL;
    Runtime view = { 0 };
    if (copy != NULL)
    {
        memcpy(copy, good, length);
        snapshot_view(&view, copy, length);
    }
    const SnapshotHeader* header = (const SnapshotHeader*)copy;
    const uint32_t entry = copy != NULL ? runtime_find_global_name(&view.global_names, (const uint8_t*)name, sizeof(name) - 1, link_hash((const uint8_t*)name, sizeof(name) - 1)) : 0;
    const uint32_t member = entry != 0 ? view.global_names.member[entry - 1] : 0;
    const uint32_t piece = member != 0 ? RUNTIME_OBJECTS + member - 1 : UINT32_MAX;
    const AtomId atom = piece != UINT32_MAX ? view.pieces[piece].begin.atoms : 0;
    const uint64_t alignment = piece != UINT32_MAX ? snapshot_record_offset(header, SNAPSHOT_REGION_ATOM_ALIGNMENT, atom) : 0;
    const uint64_t size = piece != UINT32_MAX ? snapshot_record_offset(header, SNAPSHOT_REGION_ATOM_SIZE, atom) : 0;
    const bool placed = piece != UINT32_MAX && view.pieces[piece].end.atoms > atom && snapshot_test_piece_at(header, &view, alignment) == piece &&
                        snapshot_test_piece_at(header, &view, size) == piece;
    check_test_expect(test, placed, "the __udivti3 member's first atom's alignment and size in the snapshot");
    const int file = placed ? open(snapshot, O_WRONLY | O_CLOEXEC) : -1;
    const bool damaged = file >= 0 && snapshot_test_pwrite(file, ones, 1, alignment) && snapshot_test_pwrite(file, ones, sizeof(ones), size);
    if (file >= 0)
    {
        close(file);
    }
    check_test_expect(test, damaged, "the member's records damaged in the snapshot");
    const int second = damaged ? check_test_driver_spawn(arguments, &text) : -1;
    check_test_expect(test, second == 0 && text != NULL && text[0] == 0, "a damaged piece: the link imports the runtime again, without a word");
    free(text);
    char after[256];
    check_test_expect(test, second == 0 && check_test_driver_capture(output, after, sizeof(after)) && strcmp(before, after) == 0, "the program runs as before");
    uint64_t again_length = 0;
    uint8_t* again = second == 0 ? check_test_driver_read_aligned(snapshot, &again_length) : NULL;
    check_test_expect(test, again != NULL && again_length == length && memcmp(again, good, length) == 0, "the snapshot written again as the first link wrote it");
    free(again);
    free(copy);
    free(good);
    if (saved != NULL)
    {
        setenv("XDG_CACHE_HOME", saved, 1);
    }
    else
    {
        unsetenv("XDG_CACHE_HOME");
    }
    free(saved);
    unlink(snapshot);
    rmdir(place);
    rmdir(cache);
    unlink(source);
    unlink(output);
}

// Complex values' products and quotients in the direct path (#493's review): a program that multiplies and divides
// _Complex float, double and long double values, a real dividend over a complex divisor among them, which call
// libgcc.a's __mulsc3, __muldc3, __mulxc3, __divsc3, __divdc3 and __divxc3 (#133's lowering). Linked by both paths into
// the same bytes, needing libc.so.6 alone (the members join the executable), it must print every part's bits (%a) as
// this executable, built by clang, computes them.
static void check_test_driver_complex(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "static volatile double ar = 1.5, ai = -2.25, br = 0.5, bi = 3.0;\n"
                                  "static volatile float fr = 2.0f, fi = 0.5f;\n"
                                  "static volatile long double lr = 1.25L, li = -0.75L;\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    _Complex double a = ar + ai * 1.0i, b = br + bi * 1.0i;\n"
                                  "    _Complex float f = fr + fi * 1.0fi, g = fi - fr * 1.0fi;\n"
                                  "    _Complex long double l = lr + li * 1.0Li, m = li + lr * 1.0Li;\n"
                                  "    _Complex double p = a * b, q = a / b, r = ar / b;\n"
                                  "    _Complex float s = f * g, t = f / g;\n"
                                  "    _Complex long double u = l * m, v = l / m;\n"
                                  "    printf(\"%a %a %a %a %a %a %a %a %a %a %La %La %La %La\\n\", __real__ p, __imag__ p, __real__ q, __imag__ q, __real__ r,\n"
                                  "           __imag__ r, (double)__real__ s, (double)__imag__ s, (double)__real__ t, (double)__imag__ t, __real__ u, __imag__ u,\n"
                                  "           __real__ v, __imag__ v);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[2][256];
    snprintf(source, sizeof(source), "%s/complex.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/complex", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/complex-scalar", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "complex program written");
    char* text = NULL;
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "complex products and quotients link libgcc.a's members" : "complex products and quotients, scalar");
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of complex products and quotients");
    char needed[256] = { 0 };
    check_test_expect(test, linked && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "the complex helpers in the executable, libc.so.6 needed alone");
    volatile double ar = 1.5, ai = -2.25, br = 0.5, bi = 3.0;
    volatile float fr = 2.0f, fi = 0.5f;
    volatile long double lr = 1.25L, li = -0.75L;
    const _Complex double a = ar + ai * 1.0i;
    const _Complex double b = br + bi * 1.0i;
    const _Complex float f = fr + fi * 1.0fi;
    const _Complex float g = fi - fr * 1.0fi;
    const _Complex long double l = lr + li * 1.0Li;
    const _Complex long double m = li + lr * 1.0Li;
    const _Complex double p = a * b;
    const _Complex double q = a / b;
    const _Complex double r = ar / b;
    const _Complex float s = f * g;
    const _Complex float t = f / g;
    const _Complex long double u = l * m;
    const _Complex long double v = l / m;
    char expected[512];
    snprintf(expected, sizeof(expected), "%a %a %a %a %a %a %a %a %a %a %La %La %La %La\n", __real__ p, __imag__ p, __real__ q, __imag__ q, __real__ r, __imag__ r,
             (double)__real__ s, (double)__imag__ s, (double)__real__ t, (double)__imag__ t, __real__ u, __imag__ u, __real__ v, __imag__ v);
    char captured[512];
    check_test_expect(test, linked && check_test_driver_capture(outputs[0], captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the program prints the complex results' bits");
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
}

// The jump tables' switches of check_test_driver_switch_tables, as the test's own clang build computes them.
static int check_test_switch_dense(int x)
{
    int result = -1;
    switch (x)
    {
        case 0: result = 3; break;
        case 1: result = 5; break;
        case 2: result = 7; break;
        case 3: result = 11; break;
        case 5: result = 13; break;
        case 7: result = 17; break;
        default: break;
    }
    return result;
}

static int check_test_switch_wide(long x)
{
    int result = 0;
    switch (x)
    {
        case -3: result += 1;
        case -2: result += 2; break;
        case 0 ... 2: result += 4; break;
        case 4: result += 8;
        case 5: result += 16; break;
        default: result += 100; break;
    }
    return result;
}

// Whether `bytes` (`length` of them) are an ELF object without the lowering's switch symbols (.Lbuster_switch_N and
// .Lbuster_case_N) and whose .rodata's relocations are `entries` PC32 ones, each against .text's section symbol: the
// tables and their labels the lowering's temporaries (check_driver_object), as an assembler leaves clang's .LJTI and
// .LBB. String literals' .Lbuster_string_N stay, as clang's .L.str do.
static bool check_test_driver_temporaries(const uint8_t* bytes, uint64_t length, uint32_t entries)
{
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shstrndx < header.e_shnum &&
                header.e_shoff + (uint64_t)header.e_shnum * sizeof(Elf64_Shdr) <= length;
    }
    Elf64_Shdr names = { 0 };
    if (valid)
    {
        memcpy(&names, bytes + header.e_shoff + header.e_shstrndx * sizeof(Elf64_Shdr), sizeof(names));
        valid = names.sh_offset + names.sh_size <= length;
    }
    uint32_t symtab = 0;
    uint32_t text = 0;
    uint32_t rodata = 0;
    for (uint32_t s = 1; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        const char* name = section.sh_name < names.sh_size ? (const char*)bytes + names.sh_offset + section.sh_name : "";
        symtab = section.sh_type == SHT_SYMTAB ? s : symtab;
        text = strcmp(name, ".text") == 0 ? s : text;
        rodata = strcmp(name, ".rodata") == 0 ? s : rodata;
    }
    Elf64_Shdr symbols = { 0 };
    Elf64_Shdr strings = { 0 };
    valid = valid && symtab != 0 && text != 0 && rodata != 0;
    if (valid)
    {
        memcpy(&symbols, bytes + header.e_shoff + symtab * sizeof(Elf64_Shdr), sizeof(symbols));
        valid = symbols.sh_link < header.e_shnum && symbols.sh_entsize == sizeof(Elf64_Sym) && symbols.sh_offset + symbols.sh_size <= length;
    }
    if (valid)
    {
        memcpy(&strings, bytes + header.e_shoff + symbols.sh_link * sizeof(Elf64_Shdr), sizeof(strings));
        valid = strings.sh_offset + strings.sh_size <= length;
    }
    const uint64_t symbol_count = valid ? symbols.sh_size / sizeof(Elf64_Sym) : 0;
    for (uint64_t i = 0; valid && i < symbol_count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, bytes + symbols.sh_offset + i * sizeof(Elf64_Sym), sizeof(symbol));
        const char* name = symbol.st_name < strings.sh_size ? (const char*)bytes + strings.sh_offset + symbol.st_name : NULL;
        valid = name != NULL && strncmp(name, ".Lbuster_switch_", 16) != 0 && strncmp(name, ".Lbuster_case_", 14) != 0;
    }
    uint32_t relocations = 0;
    for (uint32_t s = 1; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        const bool applies = section.sh_type == SHT_RELA && section.sh_info == rodata;
        valid = !applies || (section.sh_entsize == sizeof(Elf64_Rela) && section.sh_offset + section.sh_size <= length);
        for (uint64_t r = 0; valid && applies && r < section.sh_size / sizeof(Elf64_Rela); r += 1)
        {
            Elf64_Rela relocation;
            memcpy(&relocation, bytes + section.sh_offset + r * sizeof(Elf64_Rela), sizeof(relocation));
            Elf64_Sym symbol = { 0 };
            valid = ELF64_R_SYM(relocation.r_info) < symbol_count && ELF64_R_TYPE(relocation.r_info) == R_X86_64_PC32;
            if (valid)
            {
                memcpy(&symbol, bytes + symbols.sh_offset + ELF64_R_SYM(relocation.r_info) * sizeof(Elf64_Sym), sizeof(symbol));
                valid = ELF64_ST_TYPE(symbol.st_info) == STT_SECTION && symbol.st_shndx == text;
            }
            relocations += 1;
        }
    }
    return valid && relocations == entries;
}

// Jump tables in the C driver's outputs (#136's item 7): a program of two switches clang -O0 would make tables of, an
// int's of 8 entries and a long's of 9, and a 128-bit division, which calls libgcc's __udivti3 from the same part of
// the bodies (one lane's), where the tables' symbols' numbers fall among the runtime functions' provisional ones, each
// reference rewritten once. Compiled with -c by both paths into the same bytes. The object holds no symbol of the
// lowering's (.Lbuster_switch_N, .Lbuster_case_N), its tables' 17 entries are relocations against .text's section
// symbol, and linked by clang it prints what the test's clang build computes, as does the direct path's executable.
static void check_test_driver_switch_tables(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "static int dense(int x)\n"
                                  "{\n"
                                  "    int result = -1;\n"
                                  "    switch (x)\n"
                                  "    {\n"
                                  "        case 0: result = 3; break;\n"
                                  "        case 1: result = 5; break;\n"
                                  "        case 2: result = 7; break;\n"
                                  "        case 3: result = 11; break;\n"
                                  "        case 5: result = 13; break;\n"
                                  "        case 7: result = 17; break;\n"
                                  "        default: break;\n"
                                  "    }\n"
                                  "    return result;\n"
                                  "}\n"
                                  "static int wide(long x)\n"
                                  "{\n"
                                  "    int result = 0;\n"
                                  "    switch (x)\n"
                                  "    {\n"
                                  "        case -3: result += 1;\n"
                                  "        case -2: result += 2; break;\n"
                                  "        case 0 ... 2: result += 4; break;\n"
                                  "        case 4: result += 8;\n"
                                  "        case 5: result += 16; break;\n"
                                  "        default: result += 100; break;\n"
                                  "    }\n"
                                  "    return result;\n"
                                  "}\n"
                                  "static unsigned long long quotient(unsigned __int128 n, unsigned long long d)\n"
                                  "{\n"
                                  "    return (unsigned long long)(n / d);\n"
                                  "}\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    unsigned sum = 0;\n"
                                  "    for (int i = -4; i < 10; i++)\n"
                                  "    {\n"
                                  "        sum = sum * 31 + (unsigned)dense(i);\n"
                                  "        sum = sum * 7 + (unsigned)wide(i - 2);\n"
                                  "        sum = sum * 3 + (unsigned)quotient(((unsigned __int128)(unsigned)(i + 5) << 70) + 12345, 1000003);\n"
                                  "    }\n"
                                  "    printf(\"%u\\n\", sum);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char objects[2][256];
    char linked_path[256];
    char direct_path[256];
    snprintf(source, sizeof(source), "%s/switch-tables.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/switch-tables.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/switch-tables-scalar.o", directory);
    snprintf(linked_path, sizeof(linked_path), "%s/switch-tables-linked", directory);
    snprintf(direct_path, sizeof(direct_path), "%s/switch-tables-direct", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "jump tables' program written");
    char* text = NULL;
    bool compiled = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "the jump tables' program compiles with -c" : "the jump tables' program compiles with -c, scalar");
        compiled = compiled && status == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), "both paths write the same object of jump tables");
    uint64_t length = 0;
    uint8_t* bytes = compiled ? check_test_driver_read_aligned(objects[0], &length) : NULL;
    check_test_expect(test, check_test_driver_temporaries(bytes, length, 17),
                      "the object holds none of the lowering's symbols, and its tables' 17 entries name .text's section symbol");
    free(bytes);
    unsigned sum = 0;
    for (int i = -4; i < 10; i += 1)
    {
        sum = sum * 31 + (unsigned)check_test_switch_dense(i);
        sum = sum * 7 + (unsigned)check_test_switch_wide(i - 2);
        sum = sum * 3 + (unsigned)(unsigned long long)((((unsigned __int128)(unsigned)(i + 5) << 70) + 12345) / 1000003u);
    }
    char expected[32];
    snprintf(expected, sizeof(expected), "%u\n", sum);
    char* const link[] = { (char*)clang, "-o", linked_path, objects[0], NULL };
    const int linked = compiled ? check_test_driver_spawn(link, &text) : -1;
    free(text);
    text = NULL;
    char captured[64];
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "clang's link of the object dispatches through the tables as clang's own code does");
    char* const direct[] = { (char*)executable, "-o", direct_path, source, NULL };
    const int built = written ? check_test_driver_spawn(direct, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, built == 0 && check_test_driver_capture(direct_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the direct path's executable dispatches through the tables as clang's own code does");
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(linked_path);
    unlink(direct_path);
}

// #591: member accesses whose lookups take every kind of record's: a struct of 2,000 members, with an anonymous union
// every 40th and an anonymous struct of two bit-fields every 60th, written, read and their offsets taken; and 300 small
// structs each read by a function of its own. Both paths must write the same object, which clang links into a program
// that prints what clang's own build prints, and so must the direct path's executable: the lowering's offsets, which it
// takes from the checker's stream, are clang's. And a vector's element through a pointer, `(*p).y`, a MEMBER node that
// is no record's member, which the lowering must still refuse rather than read at the stream's offset 0.
#define BUSTER_CHECK_TEST_DRIVER_MEMBERS 2000
#define BUSTER_CHECK_TEST_DRIVER_RECORDS 300

static void check_test_driver_members(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const uint64_t capacity = UINT64_C(1) << 20;
    char* program = malloc(capacity);
    uint64_t length = 0;
    if (program != NULL)
    {
        length += (uint64_t)snprintf(program + length, capacity - length, "#include <stdio.h>\nstruct big {\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    union { int u%u; short v%u; };\n", i, i) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    struct { unsigned b%u : 5; unsigned c%u : 7; };\n", i, i) :
                                                snprintf(program + length, capacity - length, "    int m%u;\n", i));
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "};\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_RECORDS; i += 1)
        {
            length += (uint64_t)snprintf(program + length, capacity - length,
                                         "struct s%u { char a; int b; short c; };\n"
                                         "static int f%u(struct s%u* p) { return p->a * 3 + p->b * 5 + p->c * 7 + p->b; }\n",
                                         i, i, i);
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "static struct big g;\nint main(void)\n{\n    struct big* p = &g;\n    unsigned long sum = 0;\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    p->u%u = %u;\n", i, i * 7919u) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    p->b%u = %u;\n    g.c%u = %u;\n", i, i % 32, i, i % 128) :
                                                snprintf(program + length, capacity - length, "    p->m%u = %u;\n", i, i * 7 + 1));
        }
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)p->v%u + (unsigned long)((char*)&g.u%u - (char*)p);\n", i, i) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + p->b%u + g.c%u;\n", i, i) :
                                 i % 37 == 0 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)g.m%u + (unsigned long)((char*)&p->m%u - (char*)&g);\n", i, i) :
                                               snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)p->m%u;\n", i));
        }
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_RECORDS; i += 1)
        {
            length += (uint64_t)snprintf(program + length, capacity - length, "    { struct s%u v; v.a = %u; v.b = %u; v.c = %u; sum = sum * 7 + (unsigned)f%u(&v); }\n",
                                         i, i % 100, i * 3, i * 5, i);
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "    printf(\"%%lu\\n\", sum);\n    return 0;\n}\n");
    }
    char source[256];
    char objects[2][256];
    char paths[3][256];
    snprintf(source, sizeof(source), "%s/members.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/members.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/members-scalar.o", directory);
    snprintf(paths[0], sizeof(paths[0]), "%s/members-clang", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/members-linked", directory);
    snprintf(paths[2], sizeof(paths[2]), "%s/members-direct", directory);
    const bool written = program != NULL && length < capacity && parse_test_write(source, program, length);
    check_test_expect(test, written, "the members' program is written");
    char* text = NULL;
    bool compiled = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "the members' program compiles with -c" : "the members' program compiles with -c, scalar");
        compiled = compiled && status == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), "both paths write the same object of the members' program");
    char* const reference[] = { (char*)clang, "-std=gnu17", "-w", "-o", paths[0], source, NULL };
    char* const link[] = { (char*)clang, "-o", paths[1], objects[0], NULL };
    char* const direct[] = { (char*)executable, "-o", paths[2], source, NULL };
    char* const* commands[3] = {
        reference,
        link,
        direct,
    };
    char captured[3][64] = { { 0 } };
    bool ran[3] = { false };
    for (uint32_t command = 0; written && command < 3; command += 1)
    {
        const int status = command != 1 || compiled ? check_test_driver_spawn(commands[command], &text) : -1;
        free(text);
        text = NULL;
        ran[command] = status == 0 && check_test_driver_capture(paths[command], captured[command], sizeof(captured[command])) && captured[command][0] != 0;
    }
    check_test_expect(test, ran[0], "clang's build of the members' program runs");
    check_test_expect(test, ran[0] && ran[1] && strcmp(captured[1], captured[0]) == 0, "clang's link of the members' object prints what clang's build prints");
    check_test_expect(test, ran[0] && ran[2] && strcmp(captured[2], captured[0]) == 0, "the direct path's members' program prints what clang's build prints");
    static const char swizzle[] = "typedef float v4 __attribute__((ext_vector_type(4)));\nfloat f(v4* p) { return (*p).y; }\n";
    const bool swizzle_written = parse_test_write(source, swizzle, sizeof(swizzle) - 1);
    for (uint32_t path = 0; swizzle_written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: this member access") != NULL,
                          path == 0 ? "a vector's element through a pointer is refused as a member access" :
                                      "a vector's element through a pointer is refused as a member access, scalar");
        free(text);
        text = NULL;
    }
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        unlink(paths[i]);
    }
    free(program);
}

// A library that a needed library refers to (#398's review): main calls a_func, which libA defines and which calls
// b_func, which libB defines. Where libA's DT_NEEDED doesn't name libB, the executable needs both, in the line's order,
// either way round (GNU ld refuses -lB -lA); where it does (libAB), the executable needs libAB alone, as GNU ld links
// it, and the loader loads libB for it; and where main defines b_func itself, which the executable exports and the
// loader binds libA's reference to, the executable needs libA alone, as GNU ld and lld link it, and a_func calls main's
// b_func, with -lB on the line or without it. clang builds the libraries, and each program runs, finding them through
// LD_LIBRARY_PATH. Then -lA alone for main calling a_func, where nothing defines libA's b_func: refused, naming libA and
// b_func, as GNU ld and lld refuse it (--no-allow-shlib-undefined), where the program would fail to load.
static void check_test_driver_dependencies(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char a_source[] = "int b_func(void);\nint a_func(void) { return b_func() + 1; }\n";
    static const char b_source[] = "int b_func(void) { return 41; }\n";
    static const char program[] = "int a_func(void);\nint main(void) { return a_func() == 42 ? 0 : 1; }\n";
    static const char defines_b[] = "int a_func(void);\nint b_func(void) { return 9; }\nint main(void) { return a_func() == 10 ? 0 : 1; }\n";
    char a_path[256];
    char b_path[256];
    char source[256];
    char defines_source[256];
    char library_a[256];
    char library_b[256];
    char library_ab[256];
    char output[256];
    char search[300];
    snprintf(a_path, sizeof(a_path), "%s/a.c", directory);
    snprintf(b_path, sizeof(b_path), "%s/b.c", directory);
    snprintf(source, sizeof(source), "%s/uses_a.c", directory);
    snprintf(defines_source, sizeof(defines_source), "%s/defines_b.c", directory);
    snprintf(library_a, sizeof(library_a), "%s/libA.so", directory);
    snprintf(library_b, sizeof(library_b), "%s/libB.so", directory);
    snprintf(library_ab, sizeof(library_ab), "%s/libAB.so", directory);
    snprintf(output, sizeof(output), "%s/uses_a", directory);
    snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s", directory);
    const bool written = parse_test_write(a_path, a_source, sizeof(a_source) - 1) && parse_test_write(b_path, b_source, sizeof(b_source) - 1) &&
                         parse_test_write(source, program, sizeof(program) - 1) && parse_test_write(defines_source, defines_b, sizeof(defines_b) - 1);
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
        const char* source;
        const char* first;
        const char* second;
        const char* needed;
    } const cases[] = {
        { "-lA -lB: libB for libA's reference", source, "-lA", "-lB", "libA.so libB.so libc.so.6" },
        { "-lB -lA: libB for libA's reference", source, "-lB", "-lA", "libB.so libA.so libc.so.6" },
        { "-lAB -lB: libB left to libAB's DT_NEEDED", source, "-lAB", "-lB", "libAB.so libc.so.6" },
        { "-lA -lB, main defining b_func: libA alone, calling main's", defines_source, "-lA", "-lB", "libA.so libc.so.6" },
        { "-lA, main defining b_func: libA alone, calling main's", defines_source, "-lA", NULL, "libA.so libc.so.6" },
    };
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* const link[] = { (char*)executable, "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].first, (char*)cases[c].second, NULL };
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
    // -lA alone, nothing defining libA's b_func: refused, as GNU ld words it.
    char* const alone[] = { (char*)executable, "-o", output, source, "-L", (char*)directory, "-lA", NULL };
    const int alone_status = built ? check_test_driver_spawn(alone, &text) : -1;
    char message[320];
    snprintf(message, sizeof(message), "%s: undefined reference to 'b_func'", library_a);
    const bool refused = alone_status == 1 && text != NULL && strstr(text, message) != NULL;
    if (!refused)
    {
        printf("  -lA alone: status %d: %s\n", alone_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    check_test_expect(test, refused, "-lA alone: libA's b_func undefined, refused");
    const char* const files[] = { a_path, b_path, source, defines_source, library_a, library_b, library_ab, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// A library whose strong reference to libgcc_s.so.1's _Unwind_Backtrace its own DT_NEEDED doesn't cover (#511's
// review): clang compiles the helper, which GNU ld links with -shared alone (libhelper.so), and again with
// libgcc_s.so.1 (libhelper_needs.so). main calls helper, -lhelper: GNU ld's --as-needed keeps libgcc_s.so.1 for the
// library's reference, and so must the direct path, by both paths, its DT_NEEDED entries GNU ld's (cc -fuse-ld=bfd's)
// and the program running, where lld drops the library and the program fails to load. With -lhelper_needs, the
// library's DT_NEEDED loads libgcc_s.so.1 for it, and the executable needs only it; with main defining
// _Unwind_Backtrace itself, which the executable exports, the reference is main's, and libgcc_s.so.1 isn't needed; and
// with -lplain_needs beside -lhelper, in either order, a library main calls too whose DT_NEEDED names libgcc_s.so.1, the
// loader loads it for that one, and GNU ld writes no entry of the executable's own for it. On a system without
// libgcc_s.so.1 (check_test_driver_has_gcc_s), -lhelper is refused, naming the library and the name.
static void check_test_driver_unwinder_dependency(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char helper_source[] = "struct _Unwind_Context;\n"
                                        "typedef int (*Trace)(struct _Unwind_Context* context, void* argument);\n"
                                        "extern int _Unwind_Backtrace(Trace trace, void* argument);\n"
                                        "static int frame(struct _Unwind_Context* context, void* argument)\n"
                                        "{\n"
                                        "    (void)context;\n"
                                        "    *(int*)argument += 1;\n"
                                        "    return 0;\n"
                                        "}\n"
                                        "int helper(void)\n"
                                        "{\n"
                                        "    int frames = 0;\n"
                                        "    _Unwind_Backtrace(frame, &frames);\n"
                                        "    return frames >= 2 ? 0 : 1;\n"
                                        "}\n";
    static const char program[] = "int helper(void);\nint main(void) { return helper(); }\n";
    static const char plain_source[] = "int plain(void) { return 0; }\n";
    static const char both[] = "int helper(void);\nint plain(void);\nint main(void) { return helper() + plain(); }\n";
    static const char defines[] = "int helper(void);\n"
                                  "int _Unwind_Backtrace(void* trace, void* argument)\n"
                                  "{\n"
                                  "    (void)trace;\n"
                                  "    *(int*)argument = 7;\n"
                                  "    return 0;\n"
                                  "}\n"
                                  "int main(void) { return helper(); }\n";
    const bool unwinder = check_test_driver_has_gcc_s();
    char helper_path[256];
    char helper_object[256];
    char source[256];
    char defines_source[256];
    char library[256];
    char library_needs[256];
    char plain_path[256];
    char plain_object[256];
    char plain_library[256];
    char both_source[256];
    char output[256];
    char reference[256];
    char search[300];
    char gcc_s[4096] = { 0 };
    snprintf(helper_path, sizeof(helper_path), "%s/helper.c", directory);
    snprintf(helper_object, sizeof(helper_object), "%s/helper.o", directory);
    snprintf(source, sizeof(source), "%s/calls_helper.c", directory);
    snprintf(defines_source, sizeof(defines_source), "%s/defines_unwinder.c", directory);
    snprintf(library, sizeof(library), "%s/libhelper.so", directory);
    snprintf(library_needs, sizeof(library_needs), "%s/libhelper_needs.so", directory);
    snprintf(plain_path, sizeof(plain_path), "%s/plain.c", directory);
    snprintf(plain_object, sizeof(plain_object), "%s/plain.o", directory);
    snprintf(plain_library, sizeof(plain_library), "%s/libplain_needs.so", directory);
    snprintf(both_source, sizeof(both_source), "%s/calls_both.c", directory);
    snprintf(output, sizeof(output), "%s/calls_helper", directory);
    snprintf(reference, sizeof(reference), "%s/calls_helper_bfd", directory);
    snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s", directory);
    FILE* found = unwinder ? popen("cc -print-file-name=libgcc_s.so.1", "r") : NULL;
    const bool located = found != NULL && fgets(gcc_s, sizeof(gcc_s), found) != NULL;
    if (found != NULL)
    {
        pclose(found);
    }
    gcc_s[located ? strcspn(gcc_s, "\n") : 0] = 0;
    const bool written = parse_test_write(helper_path, helper_source, sizeof(helper_source) - 1) && parse_test_write(source, program, sizeof(program) - 1) &&
                         parse_test_write(defines_source, defines, sizeof(defines) - 1) && parse_test_write(plain_path, plain_source, sizeof(plain_source) - 1) &&
                         parse_test_write(both_source, both, sizeof(both) - 1);
    char* const compile[] = { (char*)clang, "-O2", "-fPIC", "-c", "-o", helper_object, helper_path, NULL };
    char* const link_alone[] = { "ld", "-shared", "--eh-frame-hdr", "-soname", "libhelper.so", "-o", library, helper_object, NULL };
    char* const link_needs[] = { "ld", "-shared", "--eh-frame-hdr", "-soname", "libhelper_needs.so", "-o", library_needs, helper_object, gcc_s, NULL };
    char* const compile_plain[] = { (char*)clang, "-O2", "-fPIC", "-c", "-o", plain_object, plain_path, NULL };
    char* const link_plain[] = { "ld", "-shared", "-soname", "libplain_needs.so", "-o", plain_library, plain_object, gcc_s, NULL };
    char* text = NULL;
    bool built = written;
    char* const* const builds[] = { compile, link_alone, link_needs, compile_plain, link_plain };
    for (uint32_t b = 0; built && b < (unwinder ? 5u : 2u); b += 1)
    {
        built = check_test_driver_spawn(builds[b], &text) == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, built, "a library that refers to the unwinder: clang and GNU ld build it");
    struct
    {
        const char* name;
        const char* source;
        const char* library;
        const char* second;
        const char* needed;
    } const cases[] = {
        { "-lhelper: libgcc_s.so.1 for libhelper's _Unwind_Backtrace", source, "-lhelper", NULL, "libhelper.so libgcc_s.so.1 libc.so.6" },
        { "-lhelper_needs: libgcc_s.so.1 left to its DT_NEEDED", source, "-lhelper_needs", NULL, "libhelper_needs.so libc.so.6" },
        { "-lhelper, main defining _Unwind_Backtrace: libhelper alone", defines_source, "-lhelper", NULL, "libhelper.so libc.so.6" },
        { "-lhelper -lplain_needs: libgcc_s.so.1 left to libplain_needs' DT_NEEDED", both_source, "-lhelper", "-lplain_needs",
          "libhelper.so libplain_needs.so libc.so.6" },
        { "-lplain_needs -lhelper: libgcc_s.so.1 left to libplain_needs' DT_NEEDED", both_source, "-lplain_needs", "-lhelper",
          "libplain_needs.so libhelper.so libc.so.6" },
    };
    for (uint32_t c = 0; built && unwinder && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char needed[256] = { 0 };
        char reference_needed[256] = { 0 };
        bool listed = true;
        int ran = 0;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char* const vector[] = { (char*)executable, "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL };
            char* const scalar[] = {
                (char*)executable, "--scalar", "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL,
            };
            char* const run[] = { "env", search, output, NULL };
            const int linked = check_test_driver_spawn(path == 0 ? vector : scalar, &text);
            free(text);
            text = NULL;
            listed = listed && linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, cases[c].needed) == 0;
            const int status = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
            free(text);
            text = NULL;
            ran = ran != 0 ? ran : status;
        }
        // GNU ld's link of the same program, through GCC's driver.
        char* const bfd[] = { "cc", "-fuse-ld=bfd", "-o", reference, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL };
        const bool reference_linked = check_test_driver_spawn(bfd, &text) == 0;
        free(text);
        text = NULL;
        const bool agrees = reference_linked && check_test_driver_needed(reference, reference_needed, sizeof(reference_needed)) && strcmp(reference_needed, needed) == 0;
        if (!listed || ran != 0 || !agrees)
        {
            printf("  %s: DT_NEEDED \"%s\", GNU ld's \"%s\", run status %d\n", cases[c].name, needed, reference_needed, ran);
        }
        check_test_expect(test, listed && ran == 0 && agrees, cases[c].name);
    }
    if (built && !unwinder)
    {
        char* const link[] = { (char*)executable, "-o", output, source, "-L", (char*)directory, "-lhelper", NULL };
        const int status = check_test_driver_spawn(link, &text);
        char message[320];
        snprintf(message, sizeof(message), "%s: undefined reference to '_Unwind_Backtrace'", library);
        const bool refused = status == 1 && text != NULL && strstr(text, message) != NULL;
        if (!refused)
        {
            printf("  -lhelper without libgcc_s.so.1: status %d: %s\n", status, text != NULL ? text : "");
        }
        free(text);
        text = NULL;
        check_test_expect(test, refused, "-lhelper without libgcc_s.so.1: libhelper's _Unwind_Backtrace undefined, refused");
    }
    const char* const files[] = { helper_path, helper_object, source, defines_source, library, library_needs, plain_path, plain_object, plain_library, both_source, output, reference };
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

// The ways check_test_driver_versions changes libA's version need of V1 (#398's review).
typedef enum CheckTestNeed
{
    // vna_other's top bit set (VER_NEED_HIDDEN), which glibc's loader and lld mask from its index.
    CHECK_TEST_NEED_HIDDEN,
    // The entry's vn_aux past its section.
    CHECK_TEST_NEED_AUXILIARY,
    // The auxiliary entry's vna_name past the string table.
    CHECK_TEST_NEED_NAME,
    // The auxiliary entry's vna_other an index that no .gnu.version entry has, so that foo's names nothing.
    CHECK_TEST_NEED_INDEX,
    // The entry's vn_next, 0 as the last entry's, to the section's end, where the loader would read one more.
    CHECK_TEST_NEED_NEXT,
    // VER_FLG_WEAK in the auxiliary entry's vna_flags, a need the loader doesn't refuse a program for.
    CHECK_TEST_NEED_WEAK,
    // Another vna_hash, which the loader compares with the version definition's vd_hash before the names.
    CHECK_TEST_NEED_HASH,
    // The version renamed in place, each digit a 9 (GLIBC_2.2.5 to GLIBC_9.9.9), with its hash, and not weak: a version
    // that the entry's file doesn't define.
    CHECK_TEST_NEED_UNDEFINED,
} CheckTestNeed;

// The ELF hash of `name`, as vna_hash and vd_hash hold it.
static uint32_t check_test_driver_elf_hash(const char* name)
{
    uint32_t hash = 0;
    for (const unsigned char* c = (const unsigned char*)name; *c != 0; c += 1)
    {
        hash = (hash << 4) + *c;
        const uint32_t high = hash & 0xf0000000u;
        hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

// Copies the library at `from` to `to` with the .gnu.version_r auxiliary entry that requires `version` changed as
// `change` says: false if the file doesn't read or has no such entry.
static bool check_test_driver_change_need(const char* from, const char* to, const char* version, CheckTestNeed change)
{
    uint64_t length = 0;
    uint8_t* bytes = NULL;
    FILE* file = fopen(from, "rb");
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
    Elf64_Ehdr header = { 0 };
    bool valid = length != 0;
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) &&
                header.e_shoff + (uint64_t)header.e_shnum * sizeof(Elf64_Shdr) <= length;
    }
    Elf64_Shdr needs = { 0 };
    for (uint32_t s = 0; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        needs = section.sh_type == SHT_GNU_verneed ? section : needs;
    }
    Elf64_Shdr strings = { 0 };
    valid = valid && needs.sh_type == SHT_GNU_verneed && needs.sh_link < header.e_shnum && needs.sh_offset + needs.sh_size <= length;
    if (valid)
    {
        memcpy(&strings, bytes + header.e_shoff + needs.sh_link * sizeof(Elf64_Shdr), sizeof(strings));
        valid = strings.sh_offset + strings.sh_size <= length;
    }
    bool changed = false;
    uint64_t offset = 0;
    bool more = valid;
    while (more)
    {
        Elf64_Verneed need = { 0 };
        more = offset + sizeof(need) <= needs.sh_size;
        if (more)
        {
            memcpy(&need, bytes + needs.sh_offset + offset, sizeof(need));
        }
        uint64_t at = offset + need.vn_aux;
        for (uint32_t a = 0; more && !changed && a < need.vn_cnt && at + sizeof(Elf64_Vernaux) <= needs.sh_size; a += 1)
        {
            Elf64_Vernaux auxiliary;
            memcpy(&auxiliary, bytes + needs.sh_offset + at, sizeof(auxiliary));
            char* name = (char*)bytes + strings.sh_offset + auxiliary.vna_name;
            const bool named = auxiliary.vna_name < strings.sh_size && memchr(name, 0, strings.sh_size - auxiliary.vna_name) != NULL && strcmp(name, version) == 0;
            if (named)
            {
                need.vn_aux = change == CHECK_TEST_NEED_AUXILIARY ? (uint32_t)needs.sh_size : need.vn_aux;
                need.vn_next = change == CHECK_TEST_NEED_NEXT ? (uint32_t)(needs.sh_size - offset) : need.vn_next;
                auxiliary.vna_name = change == CHECK_TEST_NEED_NAME ? (uint32_t)strings.sh_size + 64 : auxiliary.vna_name;
                auxiliary.vna_other = change == CHECK_TEST_NEED_HIDDEN ? (uint16_t)(auxiliary.vna_other | 0x8000)
                                      : change == CHECK_TEST_NEED_INDEX ? 0x7ff0
                                                                        : auxiliary.vna_other;
                for (char* c = name; change == CHECK_TEST_NEED_UNDEFINED && *c != 0; c += 1)
                {
                    *c = *c >= '0' && *c <= '9' ? '9' : *c;
                }
                auxiliary.vna_hash = change == CHECK_TEST_NEED_UNDEFINED ? check_test_driver_elf_hash(name)
                                     : change == CHECK_TEST_NEED_HASH    ? auxiliary.vna_hash ^ 1
                                                                         : auxiliary.vna_hash;
                auxiliary.vna_flags = change == CHECK_TEST_NEED_WEAK        ? (uint16_t)(auxiliary.vna_flags | VER_FLG_WEAK)
                                      : change == CHECK_TEST_NEED_UNDEFINED ? 0
                                                                            : auxiliary.vna_flags;
                memcpy(bytes + needs.sh_offset + offset, &need, sizeof(need));
                memcpy(bytes + needs.sh_offset + at, &auxiliary, sizeof(auxiliary));
                changed = true;
            }
            at += auxiliary.vna_next;
        }
        more = more && !changed && need.vn_next != 0;
        offset += more ? need.vn_next : 0;
    }
    FILE* output = changed ? fopen(to, "wb") : NULL;
    bool written = output != NULL && fwrite(bytes, 1, length, output) == length;
    if (output != NULL)
    {
        written = fclose(output) == 0 && written;
    }
    free(bytes);
    return written;
}

// A needed library's reference that requires a symbol version (#398's reviews): libA, linked against a libB whose foo
// is foo@@V1, requires foo@V1 of libB.so. It links (by both paths into the same bytes) and runs, a_func calling foo@V1,
// against a libB that exports foo@V1 only as a version that isn't its name's default, and one that exports it beside a
// default foo@@V2, as GNU ld and lld link it and the loader binds it. It is refused, naming foo@V1, as both linkers
// refuse it: against a libB whose foo is foo@@V2 alone, also where main defines foo (the loader would then find libB
// without V1); and against one whose foo has no version (which has .gnu.version, for libc's names), which glibc's
// loader would bind it to. Where main defines foo, against the libB of foo@V1, it links and runs, as both linkers link
// it. Then copies of libA with its need changed (check_test_driver_change_need): hidden (vna_other's top bit), which
// links and runs against the libB of foo@V1, as lld links it (GNU ld refuses the bit) and the loader binds it, and is
// refused against foo@@V2 alone and against foo without a version, as lld refuses it; and needs that don't read (an
// entry or a name outside its section) or that foo's index doesn't name, refused as GNU ld and lld refuse them, where
// the loader would take foo as unversioned, and a last entry's vn_next to the section's end, past which the loader would
// read another, which GNU ld refuses too (lld counts the entries by sh_info). Then each version need checked against the
// library its file names, as the loader checks it at startup (#398's review): with a libC on the line before libB, of
// foo@@V1 or of foo@V1 beside foo@@V3, which libA's foo@V1 binds to, it is refused against a libB of foo@@V2 alone,
// which doesn't define the V1 libA requires of libB.so ("libB.so: version 'V1' not found (required by libA.so)", the
// loader's words: GNU ld and lld link it and the program fails to load), and links and runs against a libB that defines
// V1, a libB without versions (the loader only warns), and with the need weak (likewise); a copy of libA whose need of
// V1 has another hash (vna_hash, which the loader compares with vd_hash) is refused against the libB of foo@V1; a copy
// of libA whose need of libc.so.6's GLIBC_2.2.5 is renamed GLIBC_9.9.9 is refused naming libc.so.6, and so is a copy
// of that libB with its own need renamed so, which the loader loads for libA's DT_NEEDED though the link doesn't need
// it. Each case links by both paths, which must write the same bytes or refuse it alike. clang builds the libraries
// with version scripts.
static void check_test_driver_versions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char a_source[] = "int foo(void);\nint a_func(void) { return foo(); }\n";
    static const char program[] = "int a_func(void);\nint main(void) { return a_func() == 11 ? 0 : 1; }\n";
    static const char defines_foo[] = "int a_func(void);\nint foo(void) { return 11; }\nint main(void) { return a_func() == 11 ? 0 : 1; }\n";
    // The libB the libA is built against, four libBs to link with, and two libCs: a name, source, version script and
    // library each (DIRECTORY/NAME/lib<LIBRARY>.so, whose soname is its file's name).
    static const char* const variants[][4] = {
        { "old", "int foo(void) { return 1; }\n", "V1 { global: foo; local: *; };\n", "B" },
        { "v1only", "int foo_v1(void) { return 11; }\n__asm__(\".symver foo_v1,foo@V1\");\n", "V1 { global: foo; local: *; };\n", "B" },
        { "v1v2", "int foo_v1(void) { return 11; }\nint foo_v2(void) { return 22; }\n__asm__(\".symver foo_v1,foo@V1\");\n__asm__(\".symver foo_v2,foo@@V2\");\n",
          "V1 { global: foo; local: *; };\nV2 { global: foo; } V1;\n", "B" },
        { "v2only", "int foo(void) { return 2; }\n", "V2 { global: foo; local: *; };\n", "B" },
        { "plain", "int foo(void) { return 11; }\n", "", "B" },
        { "c", "int foo(void) { return 11; }\n", "V1 { global: foo; local: *; };\n", "C" },
        { "cnd", "int foo_v1(void) { return 11; }\nint foo_v3(void) { return 33; }\n__asm__(\".symver foo_v1,foo@V1\");\n__asm__(\".symver foo_v3,foo@@V3\");\n",
          "V1 { global: foo; local: *; };\nV3 { global: foo; } V1;\n", "C" },
    };
    // The changed copies, each in a directory of its own: of libA, or of a libB variant's library (`of`), the library's
    // letter, and the version whose need each changes.
    static const struct
    {
        const char* name;
        const char* of;
        const char* library;
        CheckTestNeed change;
        const char* version;
    } changes[] = {
        { "hidden", 0, "A", CHECK_TEST_NEED_HIDDEN, "V1" },
        { "auxiliary", 0, "A", CHECK_TEST_NEED_AUXILIARY, "V1" },
        { "name", 0, "A", CHECK_TEST_NEED_NAME, "V1" },
        { "index", 0, "A", CHECK_TEST_NEED_INDEX, "V1" },
        { "next", 0, "A", CHECK_TEST_NEED_NEXT, "V1" },
        { "weak", 0, "A", CHECK_TEST_NEED_WEAK, "V1" },
        { "hash", 0, "A", CHECK_TEST_NEED_HASH, "V1" },
        { "glibc", 0, "A", CHECK_TEST_NEED_UNDEFINED, "GLIBC_2.2.5" },
        { "bglibc", "v1only", "B", CHECK_TEST_NEED_UNDEFINED, "GLIBC_2.2.5" },
    };
    enum
    {
        CHECK_TEST_VERSIONS_LINKED,
        CHECK_TEST_VERSIONS_REFUSED,
        CHECK_TEST_VERSIONS_UNREAD,
        CHECK_TEST_VERSIONS_VERSION,
    };
    // libA's directory under the test's ("" for libA as built), libC's ("" for none, else on the line between them),
    // libB's, whether main defines foo, and what must come of it; for a version refused, the library that doesn't define
    // it (a path under the test's directory, or a soname), the version and the library that requires it (0 for libA).
    static const struct
    {
        const char* name;
        const char* a;
        const char* c;
        const char* b;
        bool defines;
        int outcome;
        const char* lacking;
        const char* missing;
        const char* by;
    } cases[] = {
        { "libA's foo@V1 against a libB of foo@V1 alone: linked and run", "", "", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo@V1 and foo@@V2: linked and run", "", "", "v1v2", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo@@V2 alone: refused", "", "", "v2only", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo without a version: refused", "", "", "plain", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's foo@V1, main defining foo, against a libB of foo@V1: linked and run", "", "", "v1only", true, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1, main defining foo, against a libB of foo@@V2 alone: refused", "", "", "v2only", true, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo@V1 alone: linked and run", "hidden", "", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo@@V2 alone: refused", "hidden", "", "v2only", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo without a version: refused", "hidden", "", "plain", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's need of V1 with vn_aux past its section: refused", "auxiliary", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's need of V1 with vna_name past the strings: refused", "name", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's foo at an index that names no need: refused", "index", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's last need's vn_next to its section's end: refused", "next", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@@V2 alone, which lacks V1: refused", "", "c", "v2only", false, CHECK_TEST_VERSIONS_VERSION,
          "v2only/libB.so", "V1", 0 },
        { "libA's foo@V1, libC's foo@V1 beside foo@@V3, a libB of foo@@V2 alone: refused", "", "cnd", "v2only", false, CHECK_TEST_VERSIONS_VERSION,
          "v2only/libB.so", "V1", 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@V1 alone: linked and run", "", "c", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1, libC's foo@V1 beside foo@@V3, a libB of foo@V1 and foo@@V2: linked and run", "", "cnd", "v1v2", false, CHECK_TEST_VERSIONS_LINKED, 0,
          0, 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB without versions: linked and run", "", "c", "plain", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "a weak need of V1, libC's foo@@V1, a libB of foo@@V2 alone: linked and run", "weak", "c", "v2only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's need of GLIBC_2.2.5 renamed GLIBC_9.9.9: refused naming libc.so.6", "glibc", "", "v1only", false, CHECK_TEST_VERSIONS_VERSION, "libc.so.6",
          "GLIBC_9.9.9", 0 },
        { "libA's need of V1 with another hash: refused, as the loader compares it", "hash", "", "v1only", false, CHECK_TEST_VERSIONS_VERSION, "v1only/libB.so", "V1",
          0 },
        { "libB's need of GLIBC_9.9.9, libB loaded for libA's DT_NEEDED: refused naming libc.so.6", "", "", "bglibc", false, CHECK_TEST_VERSIONS_VERSION,
          "libc.so.6", "GLIBC_9.9.9", "bglibc/libB.so" },
    };
    char path[3][320];
    char a_path[256];
    char library_a[256];
    char sources[2][256];
    char outputs[2][256];
    snprintf(a_path, sizeof(a_path), "%s/versioned_a.c", directory);
    snprintf(library_a, sizeof(library_a), "%s/libA.so", directory);
    snprintf(sources[0], sizeof(sources[0]), "%s/versioned.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/versioned_defines.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/versioned", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/versioned-scalar", directory);
    bool built = parse_test_write(a_path, a_source, sizeof(a_source) - 1) && parse_test_write(sources[0], program, sizeof(program) - 1) &&
                 parse_test_write(sources[1], defines_foo, sizeof(defines_foo) - 1);
    char* text = NULL;
    for (uint32_t v = 0; built && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        // DIRECTORY/NAME/libB.so, from NAME.c and NAME.map beside it.
        snprintf(path[0], sizeof(path[0]), "%s/%s", directory, variants[v][0]);
        snprintf(path[1], sizeof(path[1]), "%s/%s.c", directory, variants[v][0]);
        snprintf(path[2], sizeof(path[2]), "%s/%s.map", directory, variants[v][0]);
        char library[352];
        char soname[64];
        char script[352];
        snprintf(library, sizeof(library), "%s/lib%s.so", path[0], variants[v][3]);
        snprintf(soname, sizeof(soname), "-Wl,-soname,lib%s.so", variants[v][3]);
        snprintf(script, sizeof(script), "-Wl,--version-script=%s", path[2]);
        const bool scripted = variants[v][2][0] != 0;
        built = mkdir(path[0], 0755) == 0 && parse_test_write(path[1], variants[v][1], strlen(variants[v][1])) &&
                (!scripted || parse_test_write(path[2], variants[v][2], strlen(variants[v][2])));
        char* const build[] = { (char*)clang, "-shared", "-fPIC", "-o", library, path[1], soname, scripted ? script : NULL, NULL };
        built = built && check_test_driver_spawn(build, &text) == 0;
        free(text);
        text = NULL;
        // libA against the first: its foo is then foo@V1 of libB.so.
        char old[352];
        snprintf(old, sizeof(old), "%s/old", directory);
        char* const build_a[] = { (char*)clang, "-shared", "-fPIC", "-o", library_a, a_path, "-L", old, "-lB", "-Wl,-soname,libA.so", NULL };
        built = built && (v != 0 || check_test_driver_spawn(build_a, &text) == 0);
        free(text);
        text = NULL;
    }
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(changes); c += 1)
    {
        char copy[352];
        char original[352];
        snprintf(path[0], sizeof(path[0]), "%s/%s", directory, changes[c].name);
        snprintf(copy, sizeof(copy), "%s/lib%s.so", path[0], changes[c].library);
        snprintf(original, sizeof(original), "%s/%s/lib%s.so", directory, changes[c].of != 0 ? changes[c].of : "", changes[c].library);
        built = mkdir(path[0], 0755) == 0 && check_test_driver_change_need(changes[c].of != 0 ? original : library_a, copy, changes[c].version, changes[c].change);
    }
    check_test_expect(test, built, "versioned libraries: clang builds them, and the copies of libA");
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char a_directory[320];
        char b_directory[320];
        char c_directory[320];
        char a_library[352];
        char search[1000];
        snprintf(a_directory, sizeof(a_directory), "%s%s%s", directory, cases[c].a[0] != 0 ? "/" : "", cases[c].a);
        snprintf(b_directory, sizeof(b_directory), "%s/%s", directory, cases[c].b);
        snprintf(c_directory, sizeof(c_directory), "%s/%s", directory, cases[c].c);
        snprintf(a_library, sizeof(a_library), "%s/libA.so", a_directory);
        const bool third = cases[c].c[0] != 0;
        snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s:%s%s%s", a_directory, b_directory, third ? ":" : "", third ? c_directory : "");
        const bool linking = cases[c].outcome == CHECK_TEST_VERSIONS_LINKED;
        int linked[2] = { -1, -1 };
        char messages[2][700] = { { 0 } };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            // [--scalar] -o OUTPUT SOURCE -L A [-L C] -L B -lA [-lC] -lB.
            char* link[16];
            uint32_t count = 0;
            link[count++] = (char*)executable;
            if (scalar != 0)
            {
                link[count++] = "--scalar";
            }
            link[count++] = "-o";
            link[count++] = outputs[scalar];
            link[count++] = sources[cases[c].defines ? 1 : 0];
            link[count++] = "-L";
            link[count++] = a_directory;
            if (third)
            {
                link[count++] = "-L";
                link[count++] = c_directory;
            }
            link[count++] = "-L";
            link[count++] = b_directory;
            link[count++] = "-lA";
            if (third)
            {
                link[count++] = "-lC";
            }
            link[count++] = "-lB";
            link[count] = NULL;
            linked[scalar] = check_test_driver_spawn(link, &text);
            snprintf(messages[scalar], sizeof(messages[scalar]), "%s", text != NULL ? text : "");
            free(text);
            text = NULL;
        }
        char* const run[] = { "env", search, outputs[0], NULL };
        const int ran = linking && linked[0] == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        text = NULL;
        char expected[1000];
        if (cases[c].outcome == CHECK_TEST_VERSIONS_VERSION)
        {
            const bool under = strchr(cases[c].lacking, '/') != NULL;
            char requiring[352];
            snprintf(requiring, sizeof(requiring), "%s%s%s", cases[c].by != 0 ? directory : a_library, cases[c].by != 0 ? "/" : "", cases[c].by != 0 ? cases[c].by : "");
            snprintf(expected, sizeof(expected), "%s%s%s: version '%s' not found (required by %s)", under ? directory : "", under ? "/" : "", cases[c].lacking,
                     cases[c].missing, requiring);
        }
        else
        {
            snprintf(expected, sizeof(expected), cases[c].outcome == CHECK_TEST_VERSIONS_REFUSED ? "%s: undefined reference to 'foo@V1'" : "%s isn't a shared library",
                     a_library);
        }
        const bool right = linking ? linked[0] == 0 && linked[1] == 0 && ran == 0 && check_test_driver_same_files(outputs[0], outputs[1])
                                   : linked[0] == 1 && linked[1] == 1 && strstr(messages[0], expected) != NULL && strstr(messages[1], expected) != NULL;
        if (!right)
        {
            printf("  %s: link status %d and %d, run status %d: %s%s\n", cases[c].name, linked[0], linked[1], ran, messages[0], messages[1]);
        }
        check_test_expect(test, right, cases[c].name);
    }
    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        char file[352];
        snprintf(file, sizeof(file), "%s/%s/lib%s.so", directory, variants[v][0], variants[v][3]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s.c", directory, variants[v][0]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s.map", directory, variants[v][0]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s", directory, variants[v][0]);
        rmdir(file);
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(changes); c += 1)
    {
        char file[352];
        snprintf(file, sizeof(file), "%s/%s/lib%s.so", directory, changes[c].name, changes[c].library);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s", directory, changes[c].name);
        rmdir(file);
    }
    const char* const files[] = { a_path, library_a, sources[0], sources[1], outputs[0], outputs[1] };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
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

// Reads --time-stages' report in `text` (check_clock_read): whether it has every row once, the stages adding up to the
// total and the preprocessor's rows to its stage (each line is rounded to a microsecond), and the process's lines.
// Each stage's milliseconds go to `stages` (by CheckStage).
static bool check_test_driver_time_report(const char* text, double* stages)
{
    static const char* const process[] = { "\ntime-process\ttsc-mhz\t", "\ntime-process\tuser-ms\t", "\ntime-process\tsystem-ms\t",
                                           "\ntime-process\tpeak-rss-kb\t", "\ntime-process\tminor-faults\t", "\ntime-process\tmajor-faults\t" };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double total = -1;
    bool valid = check_clock_read(text, stages, phases, &total, NULL);
    double sum = 0;
    double phase_sum = 0;
    for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
    {
        sum += stages[s];
    }
    for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
    {
        phase_sum += phases[p];
    }
    for (uint32_t p = 0; valid && p < BUSTER_ARRAY_LENGTH(process); p += 1)
    {
        valid = strstr(text, process[p]) != NULL;
    }
    const double stage_error = sum > total ? sum - total : total - sum;
    const double phase_error = phase_sum > stages[CHECK_STAGE_PREPROCESS] ? phase_sum - stages[CHECK_STAGE_PREPROCESS] : stages[CHECK_STAGE_PREPROCESS] - phase_sum;
    return valid && total > 0 && stage_error <= 0.001 * (CHECK_STAGE_COUNT + 1) && phase_error <= 0.001 * (BUSTER_CHECK_CLOCK_PHASES + 1);
}

// --time-stages (#518): an object compiled with it must be the bytes compiled without it, which prints no report, and
// its report must account for every stage once, adding up to its total; so must the direct path's, whose executable
// runs, with -g too, whose DWARF and static data are stages of their own, and -fsyntax-only's, which emits nothing.
static void check_test_driver_time_stages(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "static int table[4] = { 1, 2, 3, 4 };\n"
                                  "static int sum(int n) { int s = 0; for (int i = 0; i < n; i++) s += table[i & 3]; return s; }\n"
                                  "int main(void) { printf(\"%d\\n\", sum(10)); return sum(4) == 10 ? 0 : 1; }\n";
    char compiler[PATH_MAX];
    char source[256];
    char plain[256];
    char timed[256];
    char linked[256];
    snprintf(source, sizeof(source), "%s/time_stages.c", directory);
    snprintf(plain, sizeof(plain), "%s/time_stages_plain.o", directory);
    snprintf(timed, sizeof(timed), "%s/time_stages_timed.o", directory);
    snprintf(linked, sizeof(linked), "%s/time_stages", directory);
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, program, sizeof(program) - 1);
    char* const compile_plain[] = { compiler, "-c", source, "-o", plain, NULL };
    char* const compile_timed[] = { compiler, "-c", "--time-stages", source, "-o", timed, NULL };
    char* const link[] = { compiler, "--time-stages", source, "-o", linked, NULL };
    char* const syntax[] = { compiler, "-fsyntax-only", "--time-stages", source, NULL };
    char* const run[] = { linked, NULL };
    char* text = NULL;
    double stages[CHECK_STAGE_COUNT] = { 0 };
    const int plain_status = made ? check_test_driver_spawn(compile_plain, &text) : -1;
    check_test_expect(test, plain_status == 0 && text != NULL && strstr(text, "time-stage") == NULL, "-c without --time-stages prints no report");
    free(text);
    text = NULL;
    const int timed_status = made ? check_test_driver_spawn(compile_timed, &text) : -1;
    const bool object_report = timed_status == 0 && check_test_driver_time_report(text, stages);
    if (!object_report)
    {
        printf("  --time-stages -c: status %d: %s\n", timed_status, text != NULL ? text : "");
    }
    check_test_expect(test, object_report && stages[CHECK_STAGE_ENCODE] > 0 && stages[CHECK_STAGE_OBJECT_WRITE] > 0 && stages[CHECK_STAGE_EXECUTABLE] == 0 &&
                                stages[CHECK_STAGE_LANES] == 0,
                      "--time-stages -c reports every stage once, adding up to its total, its functions on one lane");
    check_test_expect(test, timed_status == 0 && check_test_driver_same_files(plain, timed), "--time-stages -c writes the object -c writes");
    uint64_t expression_nodes = 0;
    uint64_t batch_nodes = 0;
    const char* coverage = text != NULL ? strstr(text, "lowering-nodes\tevaluated=") : NULL;
    const bool counted = coverage != NULL && sscanf(coverage, "lowering-nodes\tevaluated=%" SCNu64 "\tbatched=%" SCNu64, &expression_nodes, &batch_nodes) == 2;
    check_test_expect(test, counted && expression_nodes != 0 && expression_nodes >= batch_nodes && (BUSTER_HOST_VECTOR ? batch_nodes != 0 : batch_nodes == 0),
                      "--time-stages reports evaluated expressions and the host's template share");
    free(text);
    text = NULL;
    memset(stages, 0, sizeof(stages));
    const int link_status = made ? check_test_driver_spawn(link, &text) : -1;
    const bool link_report = link_status == 0 && check_test_driver_time_report(text, stages);
    if (!link_report)
    {
        printf("  --time-stages: status %d: %s\n", link_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    const int ran = link_status == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, link_report && ran == 0 && stages[CHECK_STAGE_EXECUTABLE] > 0 && stages[CHECK_STAGE_OBJECT_WRITE] == 0,
                      "--time-stages on the direct path reports every stage once, and its executable runs");
    // The direct path's -g on a program of many functions and tables, enough that each stage takes microseconds: the
    // DWARF and the static data are their own stages, not the link's.
    memset(stages, 0, sizeof(stages));
    static char many[1u << 17];
    uint64_t length = 0;
    for (uint32_t f = 0; f < 512; f += 1)
    {
        length += (uint64_t)snprintf(many + length, sizeof(many) - length, "static int t%u[16] = { %u };\nstatic int f%u(void) { return t%u[0]; }\n", f, f, f, f);
    }
    length += (uint64_t)snprintf(many + length, sizeof(many) - length, "int main(void)\n{\n    int s = 0;\n");
    for (uint32_t f = 0; f < 512; f += 1)
    {
        length += (uint64_t)snprintf(many + length, sizeof(many) - length, "    s += f%u();\n", f);
    }
    length += (uint64_t)snprintf(many + length, sizeof(many) - length, "    return s == 512 * 511 / 2 ? 0 : 1;\n}\n");
    char debug_source[256];
    snprintf(debug_source, sizeof(debug_source), "%s/time_stages_debug.c", directory);
    const bool debug_made = made && length < sizeof(many) && parse_test_write(debug_source, many, length);
    char* const debug_link[] = { compiler, "--time-stages", "-g", debug_source, "-o", linked, NULL };
    const int debug_status = debug_made ? check_test_driver_spawn(debug_link, &text) : -1;
    const bool debug_report = debug_status == 0 && check_test_driver_time_report(text, stages);
    if (!debug_report)
    {
        printf("  --time-stages -g: status %d: %s\n", debug_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    const int debug_ran = debug_status == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, debug_report && debug_ran == 0 && stages[CHECK_STAGE_DEBUG] > 0 && stages[CHECK_STAGE_PUBLISH_DATA] > 0 &&
                                stages[CHECK_STAGE_EXECUTABLE] > 0,
                      "--time-stages -g on the direct path charges the DWARF and the static data to their own stages");
    // The same program's functions on two lanes (#151): their run is the lanes' stage, not the one lane's stages from
    // lowering to unwind, and the object is one lane's.
    char one_lane[256];
    char two_lanes[256];
    snprintf(one_lane, sizeof(one_lane), "%s/time_stages_one_lane.o", directory);
    snprintf(two_lanes, sizeof(two_lanes), "%s/time_stages_two_lanes.o", directory);
    char* const compile_one[] = { compiler, "-c", "--lanes", "1", debug_source, "-o", one_lane, NULL };
    char* const compile_two[] = { compiler, "-c", "--time-stages", "--lanes", "2", debug_source, "-o", two_lanes, NULL };
    memset(stages, 0, sizeof(stages));
    const int one_status = debug_made ? check_test_driver_spawn(compile_one, &text) : -1;
    free(text);
    text = NULL;
    const int two_status = debug_made ? check_test_driver_spawn(compile_two, &text) : -1;
    const bool two_report = two_status == 0 && check_test_driver_time_report(text, stages);
    if (!two_report)
    {
        printf("  --time-stages --lanes 2: status %d: %s\n", two_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    check_test_expect(test, one_status == 0 && two_report && stages[CHECK_STAGE_LANES] > 0 && stages[CHECK_STAGE_ENCODE] == 0 &&
                                check_test_driver_same_files(one_lane, two_lanes),
                      "--time-stages with the functions on two lanes charges their run to the lanes' stage, and writes one lane's object");
    memset(stages, 0, sizeof(stages));
    const int syntax_status = made ? check_test_driver_spawn(syntax, &text) : -1;
    check_test_expect(test, syntax_status == 0 && check_test_driver_time_report(text, stages) && stages[CHECK_STAGE_PARSE] > 0 && stages[CHECK_STAGE_LOWER] == 0,
                      "--time-stages -fsyntax-only reports the stages it ran");
    free(text);
    text = NULL;
    // measure-compile on the same source, with this compiler as its reference, and with a reference that fails.
    char* const measured[] = { compiler, "measure-compile", "--runs", "2", "--directory", (char*)directory, "--reference", compiler, "--", "-c", source, NULL };
    char* const failed[] = { compiler, "measure-compile", "--runs", "1", "--directory", (char*)directory, "--reference", "/bin/false", "--", "-c", source, NULL };
    const int measured_status = made ? check_test_driver_spawn(measured, &text) : -1;
    free(text);
    text = NULL;
    const int failed_status = made ? check_test_driver_spawn(failed, &text) : -1;
    free(text);
    check_test_expect(test, measured_status == 0 && failed_status == 1, "measure-compile times the compiles, and fails when one fails");
    const char* const files[] = { source, plain, timed, linked, debug_source, one_lane, two_lanes };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// Exact-capacity warning sinks end at an unreadable page. Scalar scans also visit the non-warning tokens after
// their last output, which must never write a speculative token beyond the sink's logical extent.
static void check_test_driver_warning_order(CheckTest* test)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "warning-order exact-capacity guard pages are available");
    if (ready)
    {
        uint8_t seen[200] = { 0 };
        uint32_t* tokens = (uint32_t*)(memory + page) - 2;
        bool same = true;
        for (uint32_t count = 2; count <= BUSTER_ARRAY_LENGTH(seen); count += 1)
        {
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                memset(seen, 0, sizeof(seen));
                seen[0] = 2;
                seen[1] = 1;
                tokens[0] = 0;
                tokens[1] = UINT32_MAX;
                CheckInitWarningSink sink = { .tokens = tokens, .capacity = 2, .count = 2 };
                CheckInitWarningCollector collector = { .sink = &sink, .seen = seen, .token_count = count };
                check_init_warning_order(&collector, 1, scalar != 0);
                same &= sink.count == 2 && tokens[0] == 0 && tokens[1] == 1;
            }
        }
        check_test_expect(test, same, "warning ordering preserves prior entries and never writes beyond an exactly full sink on either path");
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 2);
    }
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
            const bool ready = check_driver_reports(&parser, &parser, &pp, pp.output.count, &input, NULL, NULL, NULL, NULL, 0, NULL, 0, false, &scratch);
            check_test_expect(test, !ready && scratch.length == mark && *sentinel == UINT64_C(0x73516A93) && pp.errors == 0,
                              "diagnostic allocation failure restores scratch and publishes no partial diagnostics");
        }
    }
    scratch.capacity = capacity;
    arena_release(&scratch);
}

// The reports' walk before #592, every token in turn with a dense column of each token's warnings and of its first report:
// check_test_driver_reports' oracle for check_driver_reports, which visits only the tokens that have any.
typedef struct CheckTestDriverWarning CheckTestDriverWarning;
struct CheckTestDriverWarning
{
    uint16_t flags;
    uint16_t enabled;
};

static bool check_test_driver_reports_dense(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                             const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                             const uint32_t* excess_tokens, uint32_t excess_count, bool quiet, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = parser->tokens.count;
    const bool reports = input->report_count != 0 || (stream != 0 && stream->report_count != 0) || extra_count != 0;
    uint64_t* first = reports ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : NULL;
    const bool show_warnings = !quiet;
    CheckTestDriverWarning* warnings = show_warnings ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTestDriverWarning, (uint64_t)count + 1) : NULL;
    const bool ready = (!reports || first != NULL) && (!show_warnings || warnings != NULL);
    if (ready)
    {
        for (uint32_t token = 0; reports && token <= count; token += 1)
        {
            first[token] = UINT64_MAX;
        }
        if (show_warnings)
        {
            memset(warnings, 0, ((uint64_t)count + 1) * sizeof(*warnings));
            for (uint32_t token = 0; token < count && token < input->token_count; token += 1)
            {
                const uint32_t flags = check_diagnostic_attribute_warnings(input, token);
                warnings[token].flags = flags;
                warnings[token].enabled = flags;
            }
            for (uint32_t token = 0; input->constant_warnings != NULL && token < count && token < input->token_count; token += 1)
            {
                const uint8_t flags = input->constant_warnings[token];
                const uint8_t ignored = input->constant_warning_ignored != NULL ? input->constant_warning_ignored[token] : 0;
                warnings[token].flags |= flags;
                warnings[token].enabled |= flags & ~ignored;
            }
            for (uint32_t node = 0; stream != NULL && node < stream->count; node += 1)
            {
                const uint32_t token = input->graph.tokens[node];
                if (token < count)
                {
                    const uint32_t flags = stream->warnings[node];
                    const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
                    warnings[token].flags |= flags;
                    warnings[token].enabled |= flags & ~ignored;
                }
            }
            for (uint32_t i = 0; i < excess_count; i += 1)
            {
                if (excess_tokens[i] < count)
                {
                    warnings[excess_tokens[i]].flags |= CHECK_WARNING_EXCESS_INITIALIZERS;
                    warnings[excess_tokens[i]].enabled |= CHECK_WARNING_EXCESS_INITIALIZERS;
                }
            }
        }
        for (uint32_t i = 0; i < input->report_count; i += 1)
        {
            const uint32_t token = input->reports[i].token < count ? input->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_input_report(repair, input->reports + i) || check_repair_loop_report(repair, input, input->reports + i)))
            {
                first[token] = i;
            }
        }
        for (uint32_t i = 0; stream != 0 && i < stream->report_count; i += 1)
        {
            const uint32_t token = stream->reports[i].token < count ? stream->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_graph_report(repair, &input->graph, stream->reports + i)))
            {
                first[token] = (UINT64_C(1) << 32) | i;
            }
        }
        for (uint32_t i = 0; i < extra_count; i += 1)
        {
            const uint32_t token = extra[i].position < count ? extra[i].position : count;
            if (first[token] == UINT64_MAX)
            {
                first[token] = (UINT64_C(2) << 32) | i;
            }
        }
        const CheckInput* pragma_input = original_pragmas != NULL ? original_pragmas : input;
        uint32_t pragma = 0;
        uint32_t packed = 0;
        for (uint32_t token = 0; !pp->fatal && token <= count; token += 1)
        {
            while (!pp->fatal && pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token == token)
            {
                const CheckInputFpPragma* record = pragma_input->fp_pragmas + pragma;
                const bool retained = original_pragmas != NULL || repair == NULL || token < check_repair_pragma_end(repair);
                if (retained)
                {
                    check_driver_pragma_report(pp, pragma_input, record, quiet);
                }
                pragma += 1;
            }
            uint32_t flags = show_warnings && (repair == NULL || check_repair_warning_token(repair, token)) ?
                             warnings[token].flags : 0;
            while (!pp->fatal && flags != 0)
            {
                const uint32_t warning = flags & (0u - flags);
                const bool ignored = (warnings[token].enabled & warning) == 0;
                const CheckWarningSeverity severity = check_diagnostic_severity_default(input, token, warning, ignored);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
                    pp_diagnose_output(pp, pp_severity, parser->tokens.sources[token], "%s", check_diagnostic_message(warning));
                }
                flags &= flags - 1;
            }
            // Addresses are sparse and ordered by the '&' event. A generic selection can put the diagnosed
            // member after another address in its control expression, so its location is not the iteration key.
            // Keep these records out of the dense 16-bit warning columns.
            while (!pp->fatal && packed < input->packed_member_count && input->packed_members[packed].address <= token)
            {
                const CheckInputPackedMember* member = input->packed_members + packed;
                if (show_warnings && !member->suppressed && member->address == token &&
                    (repair == NULL || check_repair_warning_token(repair, member->token)))
                {
                    check_driver_packed_report(parser, pp, input, member);
                }
                packed += 1;
            }
            if (!pp->fatal && reports && first[token] != UINT64_MAX)
            {
                const uint32_t index = (uint32_t)first[token];
                const uint32_t phase = (uint32_t)(first[token] >> 32);
                const char* message = phase == 2 ? extra[index].message : phase == 1 ? check_driver_report_message(stream->reports + index) :
                                                                                     check_driver_input_report_message(input, input->reports + index);
                const uint32_t source = phase == 0 ? check_diagnostic_input_report_source(input, parser, input->reports + index) : BUSTER_CHECK_NONE;
                if (phase == 0 && input->reports[index].message == CHECK_INPUT_UNDECLARED && token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
                {
                    const uint32_t name = parser->tokens.names[token];
                    pp_diagnose_output(pp, PP_SEVERITY_ERROR, source, "use of undeclared identifier '%.*s'",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                }
                else if (source != BUSTER_CHECK_NONE)
                {
                    pp_diagnose_output(pp, PP_SEVERITY_ERROR, source, "%s", message);
                }
                else
                {
                    check_driver_diagnose(parser, pp, phase == 2 ? extra[index].token : token, message);
                }
            }
        }
        // A directive in an unfinished expression can lie beyond every safely checked prefix. Its original
        // source spelling and warning policy are independent of that expression and precede the parser failure.
        for (; !pp->fatal && original_pragmas != NULL && pragma < pragma_input->fp_pragma_count; pragma += 1)
        {
            check_driver_pragma_report(pp, pragma_input, pragma_input->fp_pragmas + pragma, quiet);
        }
    }
    arena_pop_to(scratch, mark);
    return ready;
}


static uint64_t check_test_driver_random(uint64_t* state)
{
    *state = *state * 6364136223846793005u + 1442695040888963407u;
    return *state >> 33;
}

// A token of `count` at random, or with `past` sometimes one past the last or further.
static uint32_t check_test_driver_random_token(uint64_t* state, uint32_t count, bool past)
{
    const uint32_t random = (uint32_t)check_test_driver_random(state);
    return past && random % 16 == 0 ? count + random % 3 : random % (count != 0 ? count : 1);
}

// check_driver_reports against the dense walk above, on both paths, into memory: units with warnings of each kind (an
// integer constant's overflow, a pointer's sign, excess elements, an unknown floating-point pragma state, a packed
// member's address, and promoted to an error and to a fatal error), each reported as checked and then with its sparse
// records rewritten at random: the constants' and the nodes' warnings at random tokens and nodes, the floating-point
// pragmas and the packed members' addresses in token order, out of it and past the last token, with the original
// pragmas apart, the reader's and the expressions' reports and the driver's own diagnostics, and the excess elements,
// quiet and not. The texts, the counts of warnings and errors, the fatal flag and the outcome must be the oracle's.
static void check_test_driver_reports(CheckTest* test)
{
    char wide_source[8192];
    uint32_t wide_length = 0;
    for (uint32_t token = 0; token < 256; token += 1)
    {
        wide_length += (uint32_t)snprintf(wide_source + wide_length, sizeof(wide_source) - wide_length, "int padding%u;\n", token);
    }
    snprintf(wide_source + wide_length, sizeof(wide_source) - wide_length,
             "enum { W = 2147483647 + 1 }; int wide[1] = { 1, 2 }; int f(int x) { return x << 40; }\n");
    const char* const sources[] = {
        "#pragma STDC FENV_ACCESS MAYBE\n"
        "enum { A = 2147483647 + 1 };\n"
        "struct __attribute__((packed)) P { char c; int i; };\n"
        "struct P s;\n"
        "int a[1] = { 1, 2 };\n"
        "int* f(void) { return &s.i; }\n"
        "void g(char* c) { unsigned char* u = c; (void)u; }\n"
        "int h(int x) { return x << 40; }\n",
        "#pragma clang diagnostic error \"-Winteger-overflow\"\n"
        "enum { B = 2147483647 + 1 };\n"
        "#pragma clang diagnostic fatal \"-Wpointer-sign\"\n"
        "void g(char* c) { unsigned char* u = c; (void)u; }\n"
        "int b[2] = { 1, 2, 3 };\n",
        wide_source,
        "int f(void) { return 1; }\n",
        "int f(void) { return 1; } int f(void) __asm__(\"late_name\");\n",
    };
    uint64_t state = 0x592;
    uint32_t compared = 0;
    bool all = true;
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sources); s += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parsing = { .extension_metadata = true };
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(UINT64_C(1) << 28);
        Arena scratch = arena_reserve(UINT64_C(1) << 28);
        Arena records = arena_reserve(UINT64_C(1) << 24);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        const CheckOptions checking = { 0 };
        const bool ready = retained.base != 0 && scratch.base != 0 && records.base != 0 && check_test_types_open(&types) &&
                           intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                           parse_initialize(&parser, &parsing, preprocessing.language) &&
                           pp_run_source(&pp, "reports.c", (const uint8_t*)sources[s], strlen(sources[s])) && parse_translation_unit(&parser, &pp) &&
                           check_input(&input, &parser, &pp, &types.table, &retained, &scratch) &&
                           check_graph_run(&types.table, &input.graph, &retained, &scratch, &checking, &stream);
        check_test_expect(test, ready && parser.tokens.count != 0 && stream.count != 0, "a unit with warnings checks, for the reports' walk");
        const uint32_t count = ready ? parser.tokens.count : 0;
        if (s == 2)
        {
            check_test_expect(test, count > 256, "the reports' ordering oracle exercises multi-byte token keys");
        }
        const CheckInput checked = input;
        const CheckStream streamed = stream;
        uint32_t warning_rows = 0;
        uint32_t attribute_rows = 0;
        uint32_t constant_rows = 0;
        for (uint32_t node = 0; ready && node < stream.count; node += 1)
        {
            warning_rows += stream.warnings[node] != 0;
        }
        for (uint32_t token = 0; ready && token < count; token += 1)
        {
            attribute_rows += check_diagnostic_attribute_warnings(&input, token) != 0;
            constant_rows += input.constant_warnings[token] != 0;
        }
        check_test_expect(test, ready && stream.warnings_prepared && stream.warning_count == warning_rows && input.asm_warnings_prepared &&
                               (input.ignored_asm_count != 0) == (attribute_rows != 0) && input.constant_warnings_prepared &&
                               input.constant_warning_count == constant_rows,
                          "warning producers publish exact node/constant counts and ignored-asm presence for report collection");
        const uint32_t errors = pp.errors;
        const uint32_t warnings = pp.warnings;
        for (uint32_t round = 0; ready && round < 160; round += 1)
        {
            arena_pop_to(&records, 0);
            input = checked;
            stream = streamed;
            CheckInput pragmas = checked;
            CheckDriverDiagnostic extra[3] = { 0 };
            uint32_t extra_count = 0;
            uint32_t* excess = NULL;
            uint32_t excess_count = 0;
            const bool quiet = round % 5 == 4;
            const bool apart = round % 3 == 2;
            // Round 0 reports each unit as checked; the others rewrite its sparse records.
            if (round != 0)
            {
                const uint32_t density = (uint32_t)check_test_driver_random(&state) % 4;
                uint8_t* constants = BUSTER_ARENA_PUSH_ARRAY(&records, uint8_t, count);
                uint8_t* constants_ignored = BUSTER_ARENA_PUSH_ARRAY(&records, uint8_t, count);
                uint32_t* node_warnings = BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, stream.count);
                uint32_t* node_ignored = BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, stream.count);
                for (uint32_t t = 0; constants != NULL && constants_ignored != NULL && t < count; t += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const bool set = density != 0 && random % (density == 1 ? 64 : density == 2 ? 8 : 2) == 0;
                    constants[t] = set ? (uint8_t)(random >> 8) : 0;
                    constants_ignored[t] = set ? (uint8_t)(random >> 16) : 0;
                }
                for (uint32_t n = 0; node_warnings != NULL && node_ignored != NULL && n < stream.count; n += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const bool set = density != 0 && random % (density == 1 ? 64 : density == 2 ? 8 : 2) == 0;
                    // Bits from 16 up, which the per-token warnings don't keep, as well as the kept ones.
                    node_warnings[n] = set ? (random >> 4) & 0x3ffff : 0;
                    node_ignored[n] = set ? (random >> 10) & 0x3ffff : 0;
                }
                input.constant_warnings = constants;
                input.constant_warning_ignored = check_test_driver_random(&state) % 2 == 0 ? constants_ignored : NULL;
                input.constant_warnings_prepared = round % 2 == 0;
                input.constant_warning_count = 0;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    input.constant_warning_count += constants[token] != 0;
                }
                stream.warnings = node_warnings;
                stream.warning_ignored = check_test_driver_random(&state) % 2 == 0 ? node_ignored : NULL;
                // Alternate completed producer metadata with hand-built streams, whose collector retains the dense fallback.
                stream.warnings_prepared = round % 2 == 0;
                stream.warning_count = 0;
                for (uint32_t node = 0; node < stream.count; node += 1)
                {
                    stream.warning_count += node_warnings[node] != 0;
                }
                const uint32_t pragma_count = (uint32_t)check_test_driver_random(&state) % 6;
                CheckInputFpPragma* fp = pragma_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputFpPragma, pragma_count) : NULL;
                for (uint32_t p = 0; fp != NULL && p < pragma_count; p += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const CheckInputFpPragma* model = checked.fp_pragma_count != 0 ? checked.fp_pragmas + random % checked.fp_pragma_count : NULL;
                    fp[p] = model != NULL ? *model : (CheckInputFpPragma){ .source_begin = 0, .source_end = 6 };
                    fp[p].token = check_test_driver_random_token(&state, count, true);
                    fp[p].warnings = (uint8_t)((random >> 4) & (CHECK_FP_PRAGMA_INVALID_PLACEMENT | CHECK_FP_PRAGMA_UNKNOWN_STATE));
                }
                // In token order, but now and then out of it.
                for (uint32_t p = 1; fp != NULL && p < pragma_count; p += 1)
                {
                    for (uint32_t q = p; q > 0 && fp[q - 1].token > fp[q].token && check_test_driver_random(&state) % 8 != 0; q -= 1)
                    {
                        const CheckInputFpPragma swap = fp[q - 1];
                        fp[q - 1] = fp[q];
                        fp[q] = swap;
                    }
                }
                (apart ? &pragmas : &input)->fp_pragmas = fp;
                (apart ? &pragmas : &input)->fp_pragma_count = pragma_count;
                const uint32_t packed_count = (uint32_t)check_test_driver_random(&state) % 6;
                CheckInputPackedMember* packed = packed_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputPackedMember, packed_count) : NULL;
                for (uint32_t p = 0; packed != NULL && p < packed_count; p += 1)
                {
                    packed[p] = (CheckInputPackedMember){
                        .address = check_test_driver_random_token(&state, count, true),
                        .token = check_test_driver_random_token(&state, count, false),
                        .member_name = BUSTER_S8("i"),
                        .record_name = BUSTER_S8("P"),
                        .suppressed = check_test_driver_random(&state) % 4 == 0,
                    };
                }
                for (uint32_t p = 1; packed != NULL && p < packed_count; p += 1)
                {
                    for (uint32_t q = p; q > 0 && packed[q - 1].address > packed[q].address && check_test_driver_random(&state) % 8 != 0; q -= 1)
                    {
                        const CheckInputPackedMember swap = packed[q - 1];
                        packed[q - 1] = packed[q];
                        packed[q] = swap;
                    }
                }
                input.packed_members = packed;
                input.packed_member_count = packed_count;
                const uint32_t input_reports = (uint32_t)check_test_driver_random(&state) % 4;
                CheckInputReport* reports = input_reports != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputReport, input_reports) : NULL;
                for (uint32_t r = 0; reports != NULL && r < input_reports; r += 1)
                {
                    reports[r] = (CheckInputReport){
                        .token = check_test_driver_random_token(&state, count, true),
                        .message = (CheckInputMessage)(check_test_driver_random(&state) % 10),
                    };
                }
                input.reports = reports;
                input.report_count = input_reports;
                const uint32_t stream_reports = (uint32_t)check_test_driver_random(&state) % 4;
                CheckReport* graph_reports = stream_reports != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckReport, stream_reports) : NULL;
                for (uint32_t r = 0; graph_reports != NULL && r < stream_reports; r += 1)
                {
                    graph_reports[r] = (CheckReport){
                        .token = check_test_driver_random_token(&state, count, true),
                        .message = (CheckMessage)(CHECK_MESSAGE_ICE + check_test_driver_random(&state) % 3),
                    };
                }
                stream.reports = graph_reports;
                stream.report_count = stream_reports;
                extra_count = (uint32_t)check_test_driver_random(&state) % 4;
                for (uint32_t e = 0; e < extra_count; e += 1)
                {
                    extra[e] = (CheckDriverDiagnostic){ check_test_driver_random_token(&state, count, false), "driver's own diagnostic",
                                                        check_test_driver_random_token(&state, count, true) };
                }
                excess_count = (uint32_t)check_test_driver_random(&state) % 5;
                excess = excess_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, excess_count) : NULL;
                for (uint32_t e = 0; excess != NULL && e < excess_count; e += 1)
                {
                    excess[e] = check_test_driver_random_token(&state, count, true);
                }
            }
            char* texts[3] = { 0 };
            size_t sizes[3] = { 0 };
            bool outcomes[3] = { 0 };
            uint32_t counted[3][2] = { { 0 } };
            bool fatal[3] = { 0 };
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                pp.errors = errors;
                pp.warnings = warnings;
                pp.fatal = false;
                parser.options.scalar = variant == 2;
                FILE* memory = open_memstream(&texts[variant], &sizes[variant]);
                pp.options.diagnostics = memory;
                const CheckInput* original = apart ? &pragmas : NULL;
                outcomes[variant] = memory != NULL &&
                                    (variant == 0 ? check_test_driver_reports_dense(&parser, &pp, &input, &stream, NULL, original, extra, extra_count, excess, excess_count, quiet, &scratch) :
                                                    check_driver_reports(&parser, &parser, &pp, pp.output.count, &input, &stream, NULL, original, extra, extra_count, excess, excess_count, quiet, &scratch));
                pp.options.diagnostics = NULL;
                if (memory != NULL)
                {
                    fclose(memory);
                }
                counted[variant][0] = pp.errors;
                counted[variant][1] = pp.warnings;
                fatal[variant] = pp.fatal;
            }
            for (uint32_t variant = 1; variant < 3; variant += 1)
            {
                const bool same = texts[0] != NULL && texts[variant] != NULL && outcomes[variant] == outcomes[0] && sizes[variant] == sizes[0] &&
                                  memcmp(texts[variant], texts[0], sizes[0]) == 0 && counted[variant][0] == counted[0][0] &&
                                  counted[variant][1] == counted[0][1] && fatal[variant] == fatal[0];
                all &= same;
                compared += 1;
                if (!same)
                {
                    printf("  reports' walk: source %u round %u %s path differs from the dense walk (%zu bytes against %zu)\n", s, round,
                           variant == 2 ? "scalar" : "vector", sizes[variant], sizes[0]);
                }
            }
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                free(texts[variant]);
            }
        }
        parser.options.scalar = false;
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&records);
        arena_release(&scratch);
        arena_release(&retained);
    }
    printf("  reports' walk: %u comparisons with the dense walk\n", compared);
    check_test_expect(test, all && compared != 0, "the reports' walk gives the dense walk's diagnostics, in its order, by both paths");
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

// Named universal character names (\N{...}), which clang reads as its extension in C and buster refuses alike in an
// identifier, in #if's character constants and in the checker's literals (#452): each source clang accepts must be
// rejected, first at the named UCN's place (its backslash, or its literal's start in #if), by both paths and by the
// default C route when `executable` is given, without writing an object. The checker places a literal's error by its
// own rules (an initializer's at its '='), so for a literal only the line is fixed (column 0). Each names a character
// outside the basic set, which C17 doesn't let a UCN name even in a literal.
static void check_test_driver_named_ucns(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t line;
        uint32_t column;
    } fixtures[] = {
        { "a named UCN in an identifier", "int \\N{LATIN SMALL LETTER E WITH ACUTE} = 1;\nint f(void) { return \xC3\xA9 + \\u00e9; }\n", 1, 5 },
        { "a named UCN in a later identifier", "int \xC3\xA9 = 1;\nint f(void)\n{\n    return \\N{LATIN SMALL LETTER E WITH ACUTE};\n}\n", 4, 12 },
        { "a named UCN in #if's character constant", "#if L'\\N{LATIN SMALL LETTER E WITH ACUTE}' == 0xE9\nint e;\n#endif\n", 1, 5 },
        { "a named UCN in a string literal", "const char *s = \"\\N{LATIN SMALL LETTER E WITH ACUTE}\";\n", 1, 0 },
        { "a named UCN in a character constant", "int c = L'\\N{LATIN SMALL LETTER E WITH ACUTE}';\n", 1, 0 },
    };
    const char* clang_arguments[] = { "-std=gnu17", "-w" };
    const uint32_t routes = executable != NULL ? 4 : 2;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/named-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/named-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                  check_corpus_clang_file(clang, clang_arguments, 2, source, &oracle);
        snprintf(description, sizeof(description), "%s: clang accepts it", fixtures[i].name);
        check_test_expect(test, oracle_ready && !oracle.rejected, description);
        for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
        {
            unlink(object);
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ready = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, false, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, %s %s: error at %u:%u, wanted at %u:%u", fixtures[i].name, route >= 2 ? "default CLI" : "direct",
                     (route & 1) != 0 ? "scalar" : "vector", checked.line, checked.column, fixtures[i].line, fixtures[i].column);
            check_test_expect(test, ready && checked.rejected && checked.line == fixtures[i].line &&
                                    (fixtures[i].column == 0 || checked.column == fixtures[i].column), description);
            check_test_expect(test, access(object, F_OK) != 0, "a refused named UCN writes no object");
        }
        unlink(object);
        unlink(source);
    }
}

#include "c_check_packed_test.c"
#include "c_check_x86_diagnostic_test.c"
#include "c_check_capacity_test.c"
#include "c_check_visibility_driver_test.c"
#include "c_check_implicit_driver_test.c"
#include "c_check_kr_driver_test.c"
#include "c_check_return_driver_test.c"

// #503: compare the production renderer, not just source locations sorted after the fact. Type errors are discovered
// after adaptation; placement/conflicts must retain a complete diagnostic graph through the annotated statement.
static void check_test_driver_loop_hints(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
        bool exact_message;
        // Existing argument/control columns stay as they were; these cases check first-error line precedence.
        bool line_only;
    } fixtures[] =
    {
        { "conflict after a body name error", "void f(int n) {\n#pragma clang loop unroll_count(4)\n#pragma clang loop unroll(full)\n    while (n--) { undeclared_x = 1; }\n}\n" },
        { "duplicate after a body type error", "void f(int n) {\n#pragma clang loop unroll(disable)\n#pragma clang loop unroll(enable)\n    while (n--) { *n = 1; }\n}\n" },
        { "count duplicate after a condition name error", "void f(int n) {\n#pragma clang loop unroll_count(4) unroll_count(8)\n    while (missing) {}\n}\n" },
        { "conflict after a for update type error", "void f(int n) {\n#pragma clang loop vectorize(disable) vectorize_width(4)\n    for (; n; *n) {}\n}\n" },
        { "do conflict after its trailing condition", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    do {} while (*n);\n}\n" },
        { "conflict after an initializer's type error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { int x = *n; }\n}\n" },
        { "placement after an expression name error", "void f(int n) {\n#pragma clang loop unroll(full)\n    missing = 1;\n}\n" },
        { "placement after a block type error", "void f(int n) {\n#pragma nounroll\n    { *n = 1; }\n}\n" },
        { "placement after an if's else type error", "void f(int n) {\n#pragma unroll\n    if (n) {} else { *n = 1; }\n}\n" },
        { "placement after a labeled loop name error", "void f(int n) {\n#pragma clang loop unroll(full)\n    label: while (n--) { missing = 1; }\n}\n" },
        { "conflict before a later statement error", "void f(int n) {\n#pragma clang loop unroll_count(4)\n#pragma clang loop unroll(full)\n    while (n--) {}\n    missing = 1;\n}\n", true },
        { "placement before a later statement error", "void f(int n) {\n#pragma nounroll\n    n = 1;\n    missing = 1;\n}\n", true },
        { "duplicate's message and column", "void f(int n) {\n#pragma clang loop unroll(disable) unroll(enable)\n    while (n--) {}\n}\n", true },
        { "duplicate counts' message and column", "void f(int n) {\n#pragma unroll 4\n#pragma GCC unroll 8\n    while (n--) {}\n}\n", true },
        { "unroll zero belongs to the state category", "void f(int n) {\n#pragma unroll 0\n#pragma nounroll\n    while (n--) {}\n}\n", true },
        { "scalable width's diagnostic spelling", "void f(int n) {\n#pragma clang loop vectorize_width(4, scalable) vectorize_width(fixed)\n    while (n--) {}\n}\n", true },
        { "ordinary unroll conflict spelling", "void f(int n) {\n#pragma unroll 4\n#pragma nounroll\n    while (n--) {}\n}\n", true },
        { "placement overrides conflicts", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    n = 1;\n}\n", true },
        { "an inner pragma argument precedes an outer conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n#pragma unroll ()\n        while (n--) {}\n    }\n}\n", true, true },
        { "later argument before a same pragma conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4) interleave_count()\n    while (n--) { missing = 1; }\n}\n", true, true },
        { "later argument before an earlier pragma conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n#pragma unroll ()\n    while (n--) { missing = 1; }\n}\n", true, true },
        { "inner and outer loops complete together", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--)\n#pragma clang loop distribute(enable) distribute(disable)\n        while (n--) {}\n}\n", true },
        { "nested placement completes before outer conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n#pragma nounroll\n        n = 1;\n    }\n}\n", true },
        { "dropped hints do not hide a body error", "void f(int n) {\n#pragma clang loop unroll_count(4))\n    while (n--) { *n = 1; }\n}\n" },
        { "an annotated statement's syntax error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { n = ; }\n}\n" },
        { "conflict before a later statement's syntax error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n    int x = ;\n}\n", true },
        { "placement before a later statement's syntax error", "void f(int n) {\n#pragma nounroll\n    n = 1;\n    int x = ;\n}\n", true },
        { "synthetic closure does not complete the annotated loop", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { n = 1;\n" },
        { "a following pragma argument comes after completed-loop Sema", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll ()\n    while (n--) {}\n}\n", true },
        { "a following pragma handler runs in the loop's lookahead", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "a following unclosed pragma runs in the loop's lookahead", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll (4\n    while (n--) {}\n}\n", true, true },
        { "a trailing empty annotation is not a nested statement", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll\n}\n", true },
        { "duplicate labels precede a loop conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n        same:;\n        same:;\n    }\n}\n", false, true },
        { "duplicate case values precede a loop conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { switch (n) {\n        case 0: break;\n        case 0: break;\n    } }\n}\n", false, true },
        { "duplicate defaults precede loop placement", "void f(int n) {\n#pragma unroll\n    switch (n) {\n        default: break;\n        default: break;\n    }\n}\n", false, true },
        { "a later duplicate label follows the loop conflict", "void f(int n) {\n    same:;\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n    same:;\n}\n", true },
        { "an undefined goto is an end-of-function obligation", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { goto missing; }\n}\n", true },
        { "a forward label address stays typed before loop placement", "void f(int n) {\n#pragma unroll\n    { void *p = &&later; }\n    later:;\n}\n", true },
        { "an undefined label address waits for function completion", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { void *p = &&missing; }\n}\n", true },
        { "a body type error after a pending label address precedes the hint", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { void *p = &&missing;\n        *n = 1;\n    }\n}\n" },
        { "nested switch errors precede a later pragma handler", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { switch (n) {\n        case 0: break;\n        case 0: break;\n    } }\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "lookahead handler precedes completion of its direct switch", "void f(int n) {\n#pragma unroll\n    switch (n) {\n        case 0: break;\n        case 0: break;\n    }\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "pragma expansion retains the conflict's invocation", "void f(int n) {\n_Pragma(\"clang loop unroll_count(4)\")\n_Pragma(\"clang loop unroll(full)\")\n    while (n--) {}\n}\n", true },
    };
    const char* arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/loop-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/loop-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected, fixtures[i].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u, clang %u:%u", fixtures[i].name, route,
                     checked.line, checked.column, oracle.line, oracle.column);
            check_test_expect(test, ran && checked.rejected && checked.line == oracle.line && (fixtures[i].line_only || checked.column == oracle.column), description);
            snprintf(description, sizeof(description), "%s: first diagnostic text matches clang", fixtures[i].name);
            check_test_expect(test, !fixtures[i].exact_message || strcmp(checked.message, oracle.message) == 0, description);
            check_test_expect(test, access(object, F_OK) != 0, "a rejected annotated statement writes no object");
        }
        unlink(object);
        unlink(source);
    }
}

#include "c_checker_driver_limit_test.c"
#include "c_checker_driver_recovery_test.c"
#include "c_checker_driver_const_test.c"

// An ELF relocatable object's sections and symbols, as the section fixtures compare clang's and -c's (#619): the parts a
// reader of either needs, every offset checked against the file, nothing else (object_view_open's checks are the
// writer's own objects' rules, which clang's needn't keep).
typedef struct CheckTestElf CheckTestElf;
struct CheckTestElf
{
    String8 file;
    const Elf64_Shdr* sections;
    uint32_t section_count;
    const char* section_names;
    uint64_t section_names_size;
    const Elf64_Sym* symbols;
    uint64_t symbol_count;
    const char* symbol_names;
    uint64_t symbol_names_size;
};

static bool check_test_elf_range(String8 file, uint64_t offset, uint64_t size)
{
    return offset <= file.length && size <= file.length - offset;
}

static bool check_test_elf_open(CheckTestElf* elf, String8 file)
{
    *elf = (CheckTestElf){ .file = file };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    bool valid = file.pointer != 0 && file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                 header->e_shentsize == sizeof(Elf64_Shdr) && header->e_shnum != 0 && header->e_shstrndx < header->e_shnum &&
                 check_test_elf_range(file, header->e_shoff, (uint64_t)header->e_shnum * sizeof(Elf64_Shdr));
    if (valid)
    {
        elf->sections = (const Elf64_Shdr*)(file.pointer + header->e_shoff);
        elf->section_count = header->e_shnum;
        const Elf64_Shdr* names = &elf->sections[header->e_shstrndx];
        valid = check_test_elf_range(file, names->sh_offset, names->sh_size);
        elf->section_names = valid ? file.pointer + names->sh_offset : 0;
        elf->section_names_size = valid ? names->sh_size : 0;
    }
    for (uint32_t s = 0; valid && s < elf->section_count; s += 1)
    {
        const Elf64_Shdr* table = &elf->sections[s];
        if (table->sh_type == SHT_SYMTAB)
        {
            const Elf64_Shdr* strings = table->sh_link < elf->section_count ? &elf->sections[table->sh_link] : 0;
            valid = strings != 0 && table->sh_entsize == sizeof(Elf64_Sym) && check_test_elf_range(file, table->sh_offset, table->sh_size) &&
                    check_test_elf_range(file, strings->sh_offset, strings->sh_size);
            elf->symbols = valid ? (const Elf64_Sym*)(file.pointer + table->sh_offset) : 0;
            elf->symbol_count = valid ? table->sh_size / sizeof(Elf64_Sym) : 0;
            elf->symbol_names = valid ? file.pointer + strings->sh_offset : 0;
            elf->symbol_names_size = valid ? strings->sh_size : 0;
        }
    }
    return valid;
}

static const char* check_test_elf_string(const char* table, uint64_t size, uint64_t offset)
{
    return offset < size && memchr(table + offset, 0, size - offset) != 0 ? table + offset : 0;
}

static const char* check_test_elf_section_name(const CheckTestElf* elf, uint32_t index)
{
    return index < elf->section_count ? check_test_elf_string(elf->section_names, elf->section_names_size, elf->sections[index].sh_name) : 0;
}

// The section of the function, object or thread-local named `name` that `elf` defines, or 0.
static uint32_t check_test_elf_defined(const CheckTestElf* elf, const char* name)
{
    uint32_t result = 0;
    for (uint64_t i = 1; result == 0 && i < elf->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf->symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* spelling = check_test_elf_string(elf->symbol_names, elf->symbol_names_size, symbol->st_name);
        const bool typed = type == STT_FUNC || type == STT_OBJECT || type == STT_TLS;
        result = typed && spelling != 0 && strcmp(spelling, name) == 0 && symbol->st_shndx != SHN_UNDEF && symbol->st_shndx < SHN_LORESERVE &&
                 symbol->st_shndx < elf->section_count ? symbol->st_shndx : 0;
    }
    return result;
}

// The section named `name` of `elf`, or 0.
static uint32_t check_test_elf_named(const CheckTestElf* elf, const char* name)
{
    uint32_t result = 0;
    for (uint32_t s = 1; result == 0 && s < elf->section_count; s += 1)
    {
        const char* spelling = check_test_elf_section_name(elf, s);
        result = spelling != 0 && strcmp(spelling, name) == 0 ? s : 0;
    }
    return result;
}

// Whether -c's object `ours` puts each function, object and thread-local it defines (but the compiler's own .L names)
// in a section of the name, type, flags and alignment that clang's object `theirs` of the same source puts it in; and,
// with `named`, has that section as clang's has it, with its size and, with `bytes`, its contents: a file-scope
// assembly's section, which no symbol shows. A description of the first difference goes in `error`.
static bool check_test_driver_same_sections(const char* ours_path, const char* theirs_path, const char* named, bool bytes, char* error, uint64_t error_size)
{
    Arena arena = arena_reserve(UINT64_C(1) << 30);
    CheckTestElf ours = { 0 };
    CheckTestElf theirs = { 0 };
    bool same = arena.base != 0 && check_test_elf_open(&ours, object_test_read_file(&arena, ours_path)) &&
                check_test_elf_open(&theirs, object_test_read_file(&arena, theirs_path));
    snprintf(error, error_size, "%s", same ? "" : "an object that doesn't read");
    for (uint64_t i = 1; same && i < ours.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &ours.symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* name = check_test_elf_string(ours.symbol_names, ours.symbol_names_size, symbol->st_name);
        if ((type == STT_FUNC || type == STT_OBJECT || type == STT_TLS) && name != 0 && strncmp(name, ".L", 2) != 0 && symbol->st_shndx != SHN_UNDEF &&
            symbol->st_shndx < ours.section_count)
        {
            const uint32_t theirs_index = check_test_elf_defined(&theirs, name);
            const Elf64_Shdr* a = &ours.sections[symbol->st_shndx];
            const Elf64_Shdr* b = theirs_index != 0 ? &theirs.sections[theirs_index] : 0;
            const char* a_name = check_test_elf_section_name(&ours, symbol->st_shndx);
            const char* b_name = theirs_index != 0 ? check_test_elf_section_name(&theirs, theirs_index) : 0;
            same = b != 0 && a_name != 0 && b_name != 0 && strcmp(a_name, b_name) == 0 && a->sh_type == b->sh_type && a->sh_flags == b->sh_flags &&
                   a->sh_addralign == b->sh_addralign;
            if (!same)
            {
                snprintf(error, error_size, "%s in %s (type %u, flags %#llx, aligned to %llu), clang's in %s (type %u, flags %#llx, aligned to %llu)", name,
                         a_name != 0 ? a_name : "?", a->sh_type, (unsigned long long)a->sh_flags, (unsigned long long)a->sh_addralign, b_name != 0 ? b_name : "none",
                         b != 0 ? b->sh_type : 0, b != 0 ? (unsigned long long)b->sh_flags : 0, b != 0 ? (unsigned long long)b->sh_addralign : 0);
            }
        }
    }
    if (same && named != 0)
    {
        const uint32_t a_index = check_test_elf_named(&ours, named);
        const uint32_t b_index = check_test_elf_named(&theirs, named);
        const Elf64_Shdr* a = a_index != 0 ? &ours.sections[a_index] : 0;
        const Elf64_Shdr* b = b_index != 0 ? &theirs.sections[b_index] : 0;
        same = a != 0 && b != 0 && a->sh_type == b->sh_type && a->sh_flags == b->sh_flags && a->sh_addralign == b->sh_addralign &&
               (!bytes || (a->sh_size == b->sh_size && (a->sh_type == SHT_NOBITS ||
                                                         (check_test_elf_range(ours.file, a->sh_offset, a->sh_size) &&
                                                          check_test_elf_range(theirs.file, b->sh_offset, b->sh_size) &&
                                                          memcmp(ours.file.pointer + a->sh_offset, theirs.file.pointer + b->sh_offset, a->sh_size) == 0))));
        if (!same)
        {
            snprintf(error, error_size, "%s: ours %s (type %u, flags %#llx, aligned to %llu, %llu bytes), clang's %s (type %u, flags %#llx, aligned to %llu, %llu bytes)",
                     named, a != 0 ? "present" : "missing", a != 0 ? a->sh_type : 0, a != 0 ? (unsigned long long)a->sh_flags : 0,
                     a != 0 ? (unsigned long long)a->sh_addralign : 0, a != 0 ? (unsigned long long)a->sh_size : 0, b != 0 ? "present" : "missing",
                     b != 0 ? b->sh_type : 0, b != 0 ? (unsigned long long)b->sh_flags : 0, b != 0 ? (unsigned long long)b->sh_addralign : 0,
                     b != 0 ? (unsigned long long)b->sh_size : 0);
        }
    }
    arena_release(&arena);
    return same;
}

// #619's #pragma clang section against clang: each line's settings and errors, at clang's places with its messages, and
// the section type conflicts between settings and section attributes in clang's order, by -fsyntax-only. What the checker
// refuses by name instead: a name in a place clang reads expanded that was a macro's in the unit, and an error at the
// line's end. Then -c of the units clang takes: each function and variable in the section its attribute or a function's
// text setting gives it, and the file-scope assembly's sections (check_program_sections, check_program_emit_file_asm),
// which must be clang's -O0 -c's of the same source (check_test_driver_same_sections); and the refusals by name: a
// static-storage definition under an active object setting (conservatively, until the emission selects its section
// kind), a section whose name gives it another kind or type, and file-scope assembly with a symbol or of another kind
// of section.
static void check_test_driver_section_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const char refused[] = "construct is not yet supported by the type checker";
    static const char placed[] = "unsupported: a definition under active '#pragma clang section' settings";
    static const char kind[] = "unsupported: a definition in a section whose name gives it another kind";
    static const char typed[] = "unsupported: a section name with an unsupported ELF type or reserved family";
    static const char asm_symbol[] = "unsupported: file-scope assembly that defines or refers to a symbol";
    static const char asm_kind[] = "unsupported: file-scope assembly's section of this kind";
    static const struct
    {
        const char* name;
        const char* source;
        // Where -fsyntax-only refuses the line instead of clang's outcome (`refused`), or 0.
        uint32_t refused_line;
        // -c of a unit clang takes: the line of its refusal and its message, or 0 for an object whose sections are clang's.
        uint32_t c_line;
        const char* c_message;
        // Compare the first error's place alone: the checker's message for another error is its own.
        bool line_only;
        // A section the object must have as clang's has it, and whether with its size and bytes too.
        const char* section;
        bool bytes;
    } fixtures[] =
    {
        { "text around a definition", "#pragma clang section text = \".text.x\"\nint f(void) { return 1; }\n#pragma clang section text = \"\"\nint main(void) { return f() - 1; }\n" },
        { "every kind in one line", "#pragma clang section text = \".t\" data = \".d\" bss = \".b\" rodata = \".r\" relro = \".rr\"\nint f(void) { return 1; }\nint g = 1;\n", 0, 3, placed },
        { "a line of no settings", "#pragma clang section\nint f(void) { return 1; }\n" },
        { "adjacent strings join", "#pragma clang section text = \".a\" \".b\"\nint f(void) { return 1; }\n" },
        { "a name read as written after the first kind", "#define data 1\n#pragma clang section text = \".a\" data = \".b\"\nint f(void) { return data; }\n" },
        { "a cleared kind places nothing", "#pragma clang section text = \".x\"\n#pragma clang section text = \"\"\nint f(void) { return 1; }\n" },
        { "data places a block's static", "#pragma clang section data = \".d\"\nint f(void) { static int s = 1; return s; }\n", 0, 2, placed },
        { "bss places a tentative definition", "#pragma clang section bss = \".x\"\nint z;\nint f(void) { return z; }\n", 0, 2, placed },
        // Clang puts these initialized writable objects in ordinary .data: the bounded refusal deliberately includes
        // every static-storage definition under any active object setting until the emission selects its final section
        // kind.
        { "bss conservatively refuses an ordinary data definition", "#pragma clang section bss = \".only_bss\"\nint x = 1;\nint f(void) { return x; }\n", 0, 2,
          placed },
        { "rodata conservatively refuses an ordinary data definition", "#pragma clang section rodata = \".only_rodata\"\nint x = 1;\nint f(void) { return x; }\n", 0, 2,
          placed },
        { "a text setting leaves static objects alone", "#pragma clang section text = \".only_text\"\nint x = 1;\n" },
        { "cleared object settings leave static objects alone", "#pragma clang section bss = \".b\" rodata = \".r\"\n#pragma clang section bss = \"\" rodata = \"\"\nint x = 1;\n" },
        { "rodata and relro share their flags", "#pragma clang section rodata = \".x\" relro = \".x\"\nconst int c __attribute__((section(\".x\"))) = 1;\n" },
        { "an extern declaration unifies nothing", "extern int e __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\nint f(void) { return e; }\n" },
        { "a function's own section", "__attribute__((section(\".text.mine\"))) int f(void) { return 1; }\nint main(void) { return f() - 1; }\n" },
        { "a declaration's section passes to the definition", "int f(void) __attribute__((section(\".text.f\")));\nint f(void) { return 1; }\nint main(void) { return f() - 1; }\n" },
        { "an attribute over a setting", "#pragma clang section text = \".t\"\n__attribute__((section(\".own\"))) int f(void) { return 1; }\nint g(void) { return f() + 1; }\n" },
        { "a function in another kind's section", "int f(void) __attribute__((section(\".data\")));\nint f(void) { return 1; }\nint g = 1;\nint main(void) { return f() + g - 2; }\n" },
        { "a variable's own section", "int g __attribute__((section(\".data.mine\"))) = 1;\nint main(void) { return g - 1; }\n" },
        { "an empty section leaves tentative zero fill in bss", "int z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section leaves initialized zero fill in bss", "int z __attribute__((section(\"\"))) = 0;\nint main(void) { return z; }\n" },
        { "an empty section leaves tls zero fill in tbss", "_Thread_local int z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section clears an inherited name", "extern int z __attribute__((section(\".data.x\")));\nint z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section overrides a text setting", "#pragma clang section text = \".text.x\"\nint f(void) __attribute__((section(\"\")));\nint f(void) { return 17; }\nint main(void) { return f() - 17; }\n" },
        { "an empty section overrides an object setting", "#pragma clang section bss = \".bss.x\"\nint z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "the first section in one attribute list", "int x __attribute__((section(\".a\"), section(\".b\"))) = 17;\nint main(void) { return x - 17; }\n" },
        { "the first section across a declaration's groups", "__attribute__((section(\".a\"))) int x __attribute__((section(\".b\"))) = 17;\n"
                                                                   "int main(void) { return x - 17; }\n" },
        { "a later declaration replaces its section", "extern int x __attribute__((section(\".a\")));\nextern int x __attribute__((section(\".b\")));\n"
                                                        "int x = 17;\nint main(void) { return x - 17; }\n" },
        { "a function's first section in one list", "int f(void) __attribute__((section(\".a\"), section(\".b\")));\n"
                                                    "int f(void) { return 17; }\nint main(void) { return f() - 17; }\n" },
        { "a zero-filled variable's section is data", "int z __attribute__((section(\".mine\"))) = 0;\nint main(void) { return z; }\n" },
        { "a zero-filled variable in a .bss", "int z __attribute__((section(\".bss.mine\")));\nint main(void) { return z; }\n" },
        { "a constant's section", "const int c __attribute__((section(\".rodata.mine\"))) = 1;\nint main(void) { return c - 1; }\n" },
        { "a pointer constant's section", "static int x;\nint* const p __attribute__((section(\".relro.mine\"))) = &x;\nint main(void) { return *p; }\n" },
        { "thread-locals' sections", "_Thread_local int t __attribute__((section(\".tdata.mine\"))) = 1;\n_Thread_local int u __attribute__((section(\".tbss.mine\")));\n"
                                     "_Thread_local int v __attribute__((section(\".mine.tls\")));\nint main(void) { return t + u + v - 1; }\n" },
        { "a block's static's section", "int f(void) { static int s __attribute__((section(\".data.s\"))) = 1; return s; }\n" },
        { "main.c's file-scope assembly", "__asm__(\".section .text.buster_checker,\\\"ax\\\",@progbits\\n\\t.p2align 12\\n\\t.text\");\n"
                                          "#pragma clang section text = \".text.buster_checker\"\nint f(void) { return 1; }\n#pragma clang section text = \"\"\n"
                                          "int main(void) { return f() - 1; }\n", 0, 0, NULL, false, ".text.buster_checker" },
        { "file-scope assembly's bytes", "__asm__(\".section .rodata.mine,\\\"a\\\",@progbits\\n.byte 1, 2, 3\\n.text\");\nint main(void) { return 0; }\n",
          0, 0, NULL, false, ".rodata.mine", true },
        { "two file-scope statements in order", "__asm__(\".section .data.asm,\\\"aw\\\",@progbits\\n.byte 1\");\n__asm__(\".section .data.asm,\\\"aw\\\",@progbits\\n.byte 2\");\n"
                                                "int main(void) { return 0; }\n", 0, 0, NULL, false, ".data.asm", true },
        { "file-scope assembly's label", "__asm__(\".globl answer\\nanswer: .byte 42\");\nint main(void) { return 0; }\n", 0, 1, asm_symbol },
        { "file-scope assembly's init array", "__asm__(\".section .init_array,\\\"aw\\\"\\n.quad 0\");\nint main(void) { return 0; }\n", 0, 1, asm_kind },
        { "an initialized variable in a .bss", "int g __attribute__((section(\".bss.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, kind },
        { "a variable in a .tdata", "int g __attribute__((section(\".tdata.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, kind },
        { "a note's section", "int g __attribute__((section(\".note.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, typed },
        { "the reserved llvm dot family includes ordinary PROGBITS names", "int g __attribute__((section(\".llvm.custom\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, typed },
        { "llvm underscore is outside the reserved dot family", "int g __attribute__((section(\".llvm_custom\"))) = 1;\nint main(void) { return g - 1; }\n" },
        { "an unknown kind", "#pragma clang section foo = \".x\"\nint f(void) { return 1; }\n" },
        { "a missing '='", "#pragma clang section text \".x\"\nint f(void) { return 1; }\n" },
        { "a name that isn't a string", "#pragma clang section text = x\nint f(void) { return 1; }\n" },
        { "a wide string", "#pragma clang section text = L\".x\"\nint f(void) { return 1; }\n" },
        { "a token after a setting", "#pragma clang section text = \".x\" extra\nint f(void) { return 1; }\n" },
        { "a comma after a setting", "#pragma clang section text = \".x\",\nint f(void) { return 1; }\n" },
        { "a keyword for a kind", "#pragma clang section int = \".x\"\nint f(void) { return 1; }\n" },
        { "a macro for the name", "#define S \".x\"\n#pragma clang section text = S\nint f(void) { return 1; }\n" },
        { "two settings' flags", "#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\nint f(void) { return 1; }\n" },
        { "a cleared setting keeps its name's flags", "#pragma clang section text = \".x\"\n#pragma clang section text = \"\"\n#pragma clang section data = \".x\"\n" },
        { "a _Pragma's setting", "_Pragma(\"clang section text = \\\".x\\\"\")\nint f(void) { return 1; }\n#pragma clang section data = \".x\"\n" },
        { "two attributes", "__attribute__((section(\".x\"))) int g = 1;\n__attribute__((section(\".x\"))) int f(void) { return 1; }\n" },
        { "empty section attributes retain mandatory conflicts", "int f(void) __attribute__((section(\"\")));\nint x __attribute__((section(\"\"))) = 1;\n" },
        { "the first section in one declaration determines conflicts", "int f(void) __attribute__((section(\".a\"), section(\".b\")));\n"
                                                                      "int x __attribute__((section(\".a\"))) = 1;\n" },
        { "an attribute after a setting", "#pragma clang section data = \".x\"\n__attribute__((section(\".x\"))) int f(void) { return 1; }\n" },
        { "a variable's attribute after text", "#pragma clang section text = \".x\"\nint g __attribute__((section(\".x\"))) = 1;\n" },
        { "a setting after an attribute", "__attribute__((section(\".x\"))) int g = 1;\n#pragma clang section text = \".x\"\n" },
        { "rodata after text", "#pragma clang section text = \".x\"\nconst int c = 1; int f(void) { return c; }\n#pragma clang section rodata = \".x\"\nconst int d = 2;\n" },
        { "a line's error before the next line's conflict", "#pragma clang section text = \".x\" foo = \".y\"\n#pragma clang section data = \".x\"\n" },
        { "bss's zero fill", "#pragma clang section bss = \".x\"\nint z __attribute__((section(\".x\"))) = 0;\n" },
        { "a tentative definition at the unit's end", "int e __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\n" },
        { "a function before a tentative definition", "int f(void) __attribute__((section(\".x\")));\nint e __attribute__((section(\".x\")));\n" },
        { "a definition inherits its tentative one's", "int g __attribute__((section(\".x\")));\nint g = 1;\nint f(void) __attribute__((section(\".x\")));\n" },
        { "the last tentative definition", "int e __attribute__((section(\".x\")));\nint e;\nint f(void) __attribute__((section(\".x\")));\n" },
        { "a definition inherits an extern's", "extern int g __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\nint g = 1;\n" },
        { "a constant variable is read-only", "int f(void) __attribute__((section(\".x\")));\nconst int c __attribute__((section(\".x\"))) = 1;\n" },
        { "a block's static", "int f(void) { static int s __attribute__((section(\".x\"))) = 1; return s; }\nint h(void) __attribute__((section(\".x\")));\n" },
        { "a static tentative definition after a setting", "#pragma clang section text = \".x\"\nint f(void);\nint f(void) __attribute__((section(\".y\")));\nint f(void) { return 1; }\nstatic int v __attribute__((section(\".x\")));\n" },
        { "a conflict before a body's error", "#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\nint f(void) { return missing; }\n" },
        { "a body's error before a conflict", "int f(void) { return missing; }\n#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\n", 0, 0, NULL, true },
        { "an initializer's error before a conflict", "#pragma clang section text = \".x\"\nint g = missing;\n#pragma clang section data = \".x\"\n", 0, 0, NULL, true },
        { "a macro for the first kind", "#define T text\n#pragma clang section T = \".x\"\nint f(void) { return 1; }\n", 2 },
        { "a name that was a macro's", "#define text 1\n#undef text\n#pragma clang section text = \".x\"\nint f(void) { return 1; }\n", 3 },
        { "a macro for an '='", "#define EQ =\n#pragma clang section text EQ \".x\"\nint f(void) { return 1; }\n", 2 },
        { "a missing '=' at the line's end", "#pragma clang section text\nint f(void) { return 1; }\n", 1 },
        { "a missing name at the line's end", "#pragma clang section text =\nint f(void) { return 1; }\n", 1 },
    };
    const char* arguments[] = { "-std=gnu17" };
    const CheckCorpusSymbols oracle_object = { .clang = clang, .arguments = arguments, .argument_count = BUSTER_ARRAY_LENGTH(arguments), .directory = directory };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char theirs[192];
        char description[1024];
        snprintf(source, sizeof(source), "%s/section-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/section-%u.o", directory, i);
        snprintf(theirs, sizeof(theirs), "%s/section-%u-clang.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        snprintf(description, sizeof(description), "%s: clang's outcome", fixtures[i].name);
        check_test_expect(test, ready, description);
        // clang's object, for the units -c writes one of.
        const bool objects = ready && !oracle.rejected && fixtures[i].refused_line == 0 && fixtures[i].c_line == 0;
        const bool compiled_theirs = objects && check_corpus_clang_object(&oracle_object, source, theirs);
        snprintf(description, sizeof(description), "%s: clang's object", fixtures[i].name);
        check_test_expect(test, !objects || compiled_theirs, description);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, NULL, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u '%s', clang %u:%u '%s'", fixtures[i].name, route, checked.line,
                     checked.column, checked.message, oracle.rejected ? oracle.line : 0, oracle.rejected ? oracle.column : 0, oracle.message);
            if (fixtures[i].refused_line != 0)
            {
                check_test_expect(test, ran && checked.rejected && checked.line == fixtures[i].refused_line && strcmp(checked.message, refused) == 0, description);
            }
            else
            {
                check_test_expect(test, ran && checked.rejected == oracle.rejected &&
                                  (!oracle.rejected || (checked.line == oracle.line && checked.column == oracle.column &&
                                                        (fixtures[i].line_only || strcmp(checked.message, oracle.message) == 0))), description);
            }
            if (ran && !oracle.rejected && fixtures[i].refused_line == 0)
            {
                unlink(object);
                const bool compiled = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
                snprintf(description, sizeof(description), "%s, route %u: -c's first error %u '%s', expected %u", fixtures[i].name, route, checked.line,
                         checked.message, fixtures[i].c_line);
                check_test_expect(test, compiled && (fixtures[i].c_line != 0 ? checked.rejected && checked.line == fixtures[i].c_line &&
                                                                               strcmp(checked.message, fixtures[i].c_message) == 0 && access(object, F_OK) != 0 :
                                                                               !checked.rejected && access(object, F_OK) == 0), description);
                if (compiled_theirs && fixtures[i].c_line == 0 && !checked.rejected)
                {
                    char difference[512];
                    const bool same = check_test_driver_same_sections(object, theirs, fixtures[i].section, fixtures[i].bytes, difference, sizeof(difference));
                    snprintf(description, sizeof(description), "%s, route %u: the object's sections against clang's: %s", fixtures[i].name, route, difference);
                    check_test_expect(test, same, description);
                }
                // The direct path's executable of a unit with a main, which exits with 0: its named sections are in their
                // classes' (check_program_sections).
                if (route >= 2 && fixtures[i].c_line == 0 && strstr(fixtures[i].source, "int main(") != NULL)
                {
                    char program[192];
                    snprintf(program, sizeof(program), "%s/section-%u", directory, i);
                    char* const vector_arguments[] = { (char*)executable, "-std=gnu17", "-o", program, source, NULL };
                    char* const scalar_arguments[] = { (char*)executable, "--scalar", "-std=gnu17", "-o", program, source, NULL };
                    char* output = NULL;
                    const int linked = check_test_driver_spawn(route == 2 ? vector_arguments : scalar_arguments, &output);
                    char* const run_arguments[] = { program, NULL };
                    char* run_output = NULL;
                    const int status = linked == 0 ? check_test_driver_spawn(run_arguments, &run_output) : -1;
                    snprintf(description, sizeof(description), "%s, route %u: the direct path's executable links (%d: %s) and exits with 0 (%d)", fixtures[i].name,
                             route, linked, output != NULL ? output : "", status);
                    check_test_expect(test, linked == 0 && status == 0, description);
                    free(output);
                    free(run_output);
                    unlink(program);
                }
            }
        }
        unlink(object);
        unlink(theirs);
        unlink(source);
    }
}

// #619's #pragma clang optimize (main.c's checker tests compile between `optimize off` and `on`, #681) against clang: the
// lines clang takes, anywhere in a unit, and its two errors at clang's places with its messages, by -fsyntax-only. What
// the checker refuses by name instead: an argument or an extra token that was a macro's name in the unit (clang reads
// both expanded), and a missing argument, an error at the line's end. Then -c of the units clang takes writes the object:
// the pragma's optnone changes nothing a Tier 0 compile emits.
static void check_test_driver_optimize_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const char refused[] = "construct is not yet supported by the type checker";
    static const struct
    {
        const char* name;
        const char* source;
        // Where -fsyntax-only refuses the line instead of clang's outcome (`refused`), or 0.
        uint32_t refused_line;
        // Compare the first error's place alone: the checker's message for another error is its own.
        bool line_only;
    } fixtures[] =
    {
        { "off and on around a definition", "#pragma clang optimize off\nint f(void) { return 1; }\n#pragma clang optimize on\nint main(void) { return f() - 1; }\n" },
        { "off to the unit's end", "int g(void) { return 2; }\n#pragma clang optimize off\nstatic int f(void) { return 1; }\nint main(void) { return f() + g() - 3; }\n" },
        { "in a body", "int f(void)\n{\n#pragma clang optimize off\n    return 1;\n}\n" },
        { "in a declaration", "int f(void)\n#pragma clang optimize off\n{ return 1; }\n" },
        { "a _Pragma", "_Pragma(\"clang optimize off\")\nint f(void) { return 1; }\n" },
        { "an always_inline function", "#pragma clang optimize off\nstatic inline __attribute__((always_inline)) int f(void) { return 1; }\nint g(void) { return f(); }\n" },
        { "an unknown argument", "#pragma clang optimize maybe\nint f(void) { return 1; }\n" },
        { "an argument in capitals", "#pragma clang optimize OFF\nint f(void) { return 1; }\n" },
        { "a number", "#pragma clang optimize 1\nint f(void) { return 1; }\n" },
        { "a string", "#pragma clang optimize \"off\"\nint f(void) { return 1; }\n" },
        { "a keyword", "#pragma clang optimize int\nint f(void) { return 1; }\n" },
        { "an extra name", "#pragma clang optimize off extra\nint f(void) { return 1; }\n" },
        { "an extra punctuator", "#pragma clang optimize off ;\nint f(void) { return 1; }\n" },
        { "on then off", "#pragma clang optimize on off\nint f(void) { return 1; }\n" },
        { "a _Pragma's error", "_Pragma(\"clang optimize maybe\")\nint f(void) { return 1; }\n" },
        { "an error after a body's", "int f(void) { return missing; }\n#pragma clang optimize maybe\n", 0, true },
        { "a body's error after an off", "#pragma clang optimize off\nint f(void) { return missing; }\n", 0, true },
        { "the second line's error", "#pragma clang optimize off\nint f(void) { return 1; }\n#pragma clang optimize of\n" },
        { "a macro for the argument", "#define OFF off\n#pragma clang optimize OFF\nint f(void) { return 1; }\n", 2 },
        { "an argument that was a macro's", "#define off 1\n#undef off\n#pragma clang optimize off\nint f(void) { return 1; }\n", 3 },
        { "an extra token of a macro", "#define NOTHING\n#pragma clang optimize off NOTHING\nint f(void) { return 1; }\n", 2 },
        { "a missing argument", "#pragma clang optimize\nint f(void) { return 1; }\n", 1 },
    };
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[512];
        snprintf(source, sizeof(source), "%s/optimize-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/optimize-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        snprintf(description, sizeof(description), "%s: clang's outcome", fixtures[i].name);
        check_test_expect(test, ready, description);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, NULL, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u '%s', clang %u:%u '%s'", fixtures[i].name, route, checked.line,
                     checked.column, checked.message, oracle.rejected ? oracle.line : 0, oracle.rejected ? oracle.column : 0, oracle.message);
            if (fixtures[i].refused_line != 0)
            {
                check_test_expect(test, ran && checked.rejected && checked.line == fixtures[i].refused_line && strcmp(checked.message, refused) == 0, description);
            }
            else
            {
                check_test_expect(test, ran && checked.rejected == oracle.rejected &&
                                  (!oracle.rejected || (checked.line == oracle.line && checked.column == oracle.column &&
                                                        (fixtures[i].line_only || strcmp(checked.message, oracle.message) == 0))), description);
            }
            if (ran && !oracle.rejected && fixtures[i].refused_line == 0)
            {
                unlink(object);
                const bool compiled = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
                snprintf(description, sizeof(description), "%s, route %u: -c's first error %u '%s'", fixtures[i].name, route, checked.line, checked.message);
                check_test_expect(test, compiled && !checked.rejected && access(object, F_OK) == 0, description);
            }
        }
        unlink(object);
        unlink(source);
    }
}

// The terminal CLI may leave mappings for exit, but callers that go on compiling must retain the reusable command's
// cleanup contract. Warm each route once so lazy thread scratch and libc state are outside the comparison.
static void check_test_driver_reuse(CheckTest* test, const char* directory)
{
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/reused-driver.c", directory);
    const char text[] = "int reused(int value) { return value + 1; }\n";
    const bool written = parse_test_write(source, text, sizeof(text) - 1);
    check_test_expect(test, written, "reusable driver fixture is available");
    char* check_arguments[] = { "-fsyntax-only", source };
    char* pp_arguments[] = { "-E", "-P", "-o", "/dev/null", source };
    for (uint32_t route = 0; written && route < 2; route += 1)
    {
        const int warm = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(check_arguments), check_arguments, true) :
                                     run_preprocessor_command(BUSTER_ARRAY_LENGTH(pp_arguments), pp_arguments);
        const uint64_t before = parse_test_status_bytes("VmSize:");
        const int again = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(check_arguments), check_arguments, true) :
                                      run_preprocessor_command(BUSTER_ARRAY_LENGTH(pp_arguments), pp_arguments);
        const uint64_t after = parse_test_status_bytes("VmSize:");
        check_test_expect(test, warm == 0 && again == 0 && before != 0 && after == before,
                          "reusable source and preprocessor commands release their mappings before returning");
        char* invalid_arguments[] = { "--buster-invalid-option" };
        const int saved = dup(STDERR_FILENO);
        const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
        int rejected = -1;
        if (saved >= 0 && quiet >= 0 && dup2(quiet, STDERR_FILENO) == STDERR_FILENO)
        {
            rejected = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(invalid_arguments), invalid_arguments, true) :
                                    run_preprocessor_command(BUSTER_ARRAY_LENGTH(invalid_arguments), invalid_arguments);
        }
        if (saved >= 0)
        {
            dup2(saved, STDERR_FILENO);
            close(saved);
        }
        if (quiet >= 0)
        {
            close(quiet);
        }
        check_test_expect(test, rejected == 1 && parse_test_status_bytes("VmSize:") == before,
                          "a reusable command's rejected options release mappings after an earlier successful compile");
    }
    unlink(source);
}

static int run_checker_driver_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
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
    // The process's oracle cache, set up before any check runs clang through it (oracle_cache.c, #542).
    OracleCache* oracle = oracle_cache();
    check_test_driver_warning_order(&test);
    check_test_driver_diagnostic_capacity(&test);
    check_test_driver_reports(&test);
    check_test_lower_capacity(&test);
    const CheckDriverFixture fixtures[] =
    {
        {
            .name = "K&R definition emits its deprecated-non-prototype warning",
            .source = "int f(a)float a;{return (int)a;}\n",
            .warnings = 1,
        },
        {
            .name = "K&R arity warnings do not block object emission",
            .source = "int f(a)int a;{return a;}int g(void){return f()+f(1,2); }\n",
            .warnings = 3,
        },
        {
            .name = "K&R deprecation suppression leaves the independent arity warning",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\n"
                      "int f(a)int a;{return a;}int g(void){return (&f)(1,2); }\n",
            .warnings = 1,
        },
        {
            .name = "promoted K&R definition warning blocks object emission",
            .source = "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n",
            .rejected = true,
        },
        {
            .name = "K&R indirect call warning follows source-ordered diagnostic policy",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n"
                      "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint g(void){return (*f)(1,2);}\n",
            .rejected = true,
        },
        {
            .name = "quiet K&R warnings still emit an object",
            .source = "int f(a)int a;{return a;}int g(void){return f(1,2);}\n",
            .quiet = true,
        },
        {
            .name = "K&R source-only sizeof call retains its arity warning",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof(f(1,2));}\n",
            .warnings = 2,
        },
        {
            .name = "K&R source-only grouped and addressed calls retain arity warnings",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof((f)(1,2))+sizeof((&f)(1,2));}\n",
            .warnings = 3,
        },
        {
            .name = "K&R source-only addressed call retains known argument constraints",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof((&f)((int*)0));}\n",
            .rejected = true,
            .warnings = 1,
        },
        {
            .name = "source-only implicit calls do not add non-prototype warnings",
            .source = "#pragma clang diagnostic ignored \"-Wimplicit-function-declaration\"\n"
                      "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\n"
                      "int g(void){int i=missing(1);return i+sizeof((&missing)(2))+sizeof((missing)(3));}\n",
        },
        {
            .name = "K&R source-only call obeys warning promotion",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\n"
                      "int f(a)int a;{return a;}\n#pragma clang diagnostic error \"-Weverything\"\n"
                      "int g(void){return sizeof(f(1,2));}\n",
            .rejected = true,
        },
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
        // clang's default-error -Wint-conversion, suppressed in a system header: a line marker's system region returns an
        // integer as a pointer (suppressed), and so does its macro, invoked in the user's file (reported there, by its
        // expansion location, which the preprocessor's runs give: pp_system_levels).
        {
            .name = "a system header's macro returns an integer as a pointer in the user's file",
            .source = "# 1 \"sys.h\" 3\nstatic int *in_system(void) { return 1; }\n#define FROM_SYSTEM(x) return x;\n# 10 \"user.c\"\n"
                      "int *in_user(void) { FROM_SYSTEM(2) }\n",
            .rejected = true,
        },
        // A system header's macro invoked in the user's file, with a line marker among its arguments: the body's tokens
        // are where the invocation starts, before the marker, in the user's file, which only the history says (the runs are
        // inexact). The runs alone, or the tokens' own places, would suppress the error.
        {
            .name = "a line marker among a macro's arguments leaves its body in the user's file",
            .source = "# 1 \"sys.h\" 3\n#define F(x) int *f(void) { return 1; } int x;\n# 3 \"user.c\"\nF(\n# 5 \"sys2.h\" 3\ny)\n",
            .rejected = true,
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
        {
            .name = "visibility warning follows earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n#pragma GCC visibility push(bad)\n",
            .rejected = true,
            .warnings = 1,
        },
        {
            .name = "visibility error cannot hide earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n#pragma GCC visibility pop\n",
            .rejected = true,
        },
        {
            .name = "promoted visibility warning cannot hide earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n"
                      "#pragma clang diagnostic error \"-Wignored-attributes\"\n#pragma GCC visibility push(bad)\n",
            .rejected = true,
        },
        {
            .name = "visibility warning inside unfinished initializer retains parser placement",
            .source = "int value=\n#pragma GCC visibility push(bad)\n;\n",
            .rejected = true,
        },
        {
            .name = "legal visibility directive survives an erased malformed body",
            .source = "int f(void){\n#pragma GCC visibility push(hidden)\nreturn 1+;\n}\n",
            .rejected = true,
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
            snprintf(description, sizeof(description), "%s, %s %s%s: warnings %u, expected %u", fixtures[i].name,
                     external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "", warnings, fixtures[i].warnings);
            check_test_expect(&test, warnings == fixtures[i].warnings, description);
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
        check_test_driver_error_limits(&test, clang, executable, directory);
        check_test_driver_recovery(&test, clang, executable, directory);
        check_test_driver_const(&test, clang, executable, directory);
        check_test_driver_reuse(&test, directory);
        check_test_driver_large_function(&test, clang, executable, directory);
        check_test_packed_members(&test, clang, executable, directory);
        check_test_driver_lazy(&test, clang, executable, directory);
        check_test_driver_named_ucns(&test, clang, executable, directory, lazy);
        check_test_driver_loop_hints(&test, clang, executable, directory, lazy);
        check_test_driver_section_pragmas(&test, clang, executable, directory, lazy);
        check_test_driver_optimize_pragmas(&test, clang, executable, directory, lazy);
        if (executable != NULL)
        {
            check_test_driver_libraries(&test, executable, directory);
            check_test_driver_visibility(&test, clang, executable, directory);
            check_test_driver_implicit(&test, clang, executable, directory);
            check_test_driver_kr_policy(&test, clang, executable, directory, lazy);
            check_test_driver_returns(&test, clang, executable, directory);
            check_test_driver_libgcc(&test, executable, directory);
            check_test_driver_complex(&test, executable, directory);
            check_test_driver_target_selection(&test, executable, directory);
            check_test_driver_switch_tables(&test, clang, executable, directory);
            check_test_driver_members(&test, clang, executable, directory);
            check_test_driver_asm_symbols(&test, executable, directory);
            check_test_driver_split_stack(&test, executable, directory);
            check_test_driver_damaged_snapshot(&test, executable, directory);
            check_test_driver_dependencies(&test, clang, executable, directory);
            check_test_driver_unwinder_dependency(&test, clang, executable, directory);
            check_test_driver_script_path(&test, clang, executable, directory);
            check_test_driver_versions(&test, clang, executable, directory);
            check_test_driver_option_values(&test, executable, directory);
            check_test_driver_time_stages(&test, executable, directory);
        }
        rmdir(directory);
    }
    oracle_cache_report(oracle, "test-checker-driver");
    printf("checker-driver: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int returned = test.failures != 0;
    oracle_epoch_end(epoch);
    return returned;
}
