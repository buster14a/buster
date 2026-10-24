// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-foreign-corpus: the foreign linker's differential corpus test (#86's "done when", and #394's output kinds). Each
// program of the corpus is compiled by each C compiler given (gcc and clang by default) at -O2, as their drivers compile
// a PIE by default, then linked from one command line by link-foreign (this executable, run as a process of its own), by
// lld and by mold: the GCC driver's crt objects and libraries around the program's objects and archives, as each kind of
// output (ForeignCorpusKind) has them: a dynamic PIE, a static executable (-static, with libc.a), a static PIE (GCC's
// -static-pie line) and a shared library of the program's library part (-shared, its sources compiled with -fPIC), which
// a PIE of the rest that lld links loads, by each linker's library in turn. The three outputs must:
//   - run alike: each run's exit status, standard output and standard errors, run as ./program from directories of the
//     same name, with the same arguments, input and environment;
//   - have the same symbols with the same sizes: .symtab's named entries, each one's name, binding, type, visibility,
//     size and whether it is defined, as a sorted multiset, but for bookkeeping (STT_FILE and STT_SECTION entries),
//     the names a linker defines for itself (foreign_corpus_synthesized), mold's labels with a '$' in them
//     (printf$plt), and __abi_tag, a crt object's symbol in the ABI note that link-foreign writes as a note of its own;
//   - agree on what readelf shows of the dynamic section: DT_NEEDED's libraries in order, DT_SONAME, DT_FLAGS_1's PIE,
//     whether DT_INIT, DT_FINI, DT_INIT_ARRAY and DT_FINI_ARRAY are there and the arrays' sizes;
//   - and of the versions: .dynsym's names, each with its version's name and whether it is defined, as a multiset;
//   - and of the relocations: each dynamic relocation that names a symbol, by the symbol's name and its class (a GOT
//     slot: GLOB_DAT, or the JUMP_SLOT that lld and mold give a call's slot, where link-foreign binds every import at
//     load with GLOB_DAT; a COPY; an R_X86_64_64; a TPOFF64; a DTPMOD64; a DTPOFF64), as a multiset, and the count of
//     relative relocations (RELATIVE ones and RELR's) against lld's.
// And link-foreign's output must come out byte for byte the same when it links on eight lanes, through the resolution's,
// the conversion's and the writer's lane paths, as on one. A static executable's IRELATIVE relocations are in
// .rela.iplt (GNU ld's place), lld's and mold's in .rela.dyn: the comparison reads both.
// What a reference linker writes as its own choice, where link-foreign's choice is lld's or GNU ld's and as valid, is
// counted and printed as documented rather than as a failure (foreign_corpus_documented): mold leaves a weak undefined
// name that nothing defines and that has no version out of .dynsym, binds the aliases it copies weakly, writes its own
// symbols local, and keeps a few GOT slots that lld and link-foreign relax, which changes its relative relocations.
//
// The corpus: two original programs always (C, of three translation units, an archive of two members it needs and one
// it doesn't, threads, thread-locals, libc's and libm's data and functions; and C++, of two translation units, with
// iostream, exceptions across them, RTTI, templates in COMDAT groups, inline variables and threads), and with
// --sqlite DIR the SQLite amalgamation's shell (sqlite3.c and shell.c), with --lua DIR Lua's interpreter (its library
// as an archive), and with --self DIR this repository's compiler at DIR, compiled by clang alone, each with runs of its
// own. --compilers and --linkers choose others; --keep keeps the directory under --directory (TMPDIR or /tmp).

#define BUSTER_FOREIGN_CORPUS_MAX_SOURCES 64
#define BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS 160
#define BUSTER_FOREIGN_CORPUS_MAX_ROWS 65536
#define BUSTER_FOREIGN_CORPUS_ROW_BYTES 192
#define BUSTER_FOREIGN_CORPUS_RUN_SECONDS 120

typedef struct ForeignCorpusRun ForeignCorpusRun;
struct ForeignCorpusRun
{
    const char* arguments[6];
    const char* input;
};

typedef struct ForeignCorpusProgram ForeignCorpusProgram;
struct ForeignCorpusProgram
{
    const char* name;
    bool cxx;
    // The sources' directory, or 0 for the original fixtures (foreign_corpus_fixtures), which the test writes.
    const char* directory;
    // Linked directly, then in one archive after them, then the libraries.
    const char* sources[BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    const char* archived[BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    const char* flags[8];
    const char* libraries[8];
    // Compiled by these compilers alone (0 for every one given).
    const char* only_compiler;
    ForeignCorpusRun runs[8];
    // Its library part, for the shared kind: these sources (of `sources` and `archived`), compiled with -fPIC, make
    // libcorpus.so, which each linker links, and the others a PIE that lld links against it. None leaves the kind out.
    const char* library[BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    // The kinds left out, a bit per ForeignCorpusKind.
    uint32_t skipped;
};

// The kinds of output the corpus links (link-foreign's -pie, -static, -static -pie and -shared).
typedef enum ForeignCorpusKind
{
    FOREIGN_CORPUS_PIE,
    FOREIGN_CORPUS_STATIC,
    FOREIGN_CORPUS_STATIC_PIE,
    FOREIGN_CORPUS_SHARED,
    FOREIGN_CORPUS_KINDS,
} ForeignCorpusKind;

static const char* const foreign_corpus_kind_names[FOREIGN_CORPUS_KINDS] = {
    "pie",
    "static",
    "static-pie",
    "shared",
};

typedef struct ForeignCorpusFixture ForeignCorpusFixture;
struct ForeignCorpusFixture
{
    const char* file;
    const char* text;
};

static const ForeignCorpusFixture foreign_corpus_fixtures[] = {
    {
        .file = "corpus_main.c",
        .text = "#include <errno.h>\n#include <math.h>\n#include <pthread.h>\n#include <setjmp.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
                "#include <string.h>\n#include <unistd.h>\n"
                "extern char** environ;\n"
                "extern int corpus_counter;\n"
                "extern const char* corpus_name(int);\n"
                "extern double corpus_scale(double);\n"
                "extern int corpus_shared_work(int);\n"
                "extern void corpus_weak_hook(void) __attribute__((weak));\n"
                "struct corpus_entry { const char* name; long value; };\n"
                "static const struct corpus_entry corpus_entry_main __attribute__((used, section(\"corpus_registry\"))) = { \"main\", 3 };\n"
                "extern const struct corpus_entry __start_corpus_registry[], __stop_corpus_registry[];\n"
                "__thread int corpus_tls = 5;\n"
                "static __thread char corpus_tls_buffer[64];\n"
                "static jmp_buf corpus_jump;\n"
                "static int constructed;\n"
                "static int compare(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }\n"
                "static void* worker(void* argument) {\n"
                "    corpus_tls += (int)(long)argument;\n"
                "    snprintf(corpus_tls_buffer, sizeof(corpus_tls_buffer), \"t%d\", corpus_tls * 7);\n"
                "    return (void*)(long)strlen(corpus_tls_buffer);\n"
                "}\n"
                "__attribute__((constructor)) static void construct(void) { constructed = 17; }\n"
                "__attribute__((destructor)) static void destruct(void) { fputs(\"destructor\\n\", stdout); }\n"
                "int main(int argc, char** argv) {\n"
                "    const char* words[] = { \"pear\", \"apple\", \"fig\", \"kiwi\" };\n"
                "    qsort(words, 4, sizeof(words[0]), compare);\n"
                "    printf(\"sorted %s %s %s %s\\n\", words[0], words[1], words[2], words[3]);\n"
                "    pthread_t threads[3];\n"
                "    long total = 0;\n"
                "    for (long i = 0; i < 3; i += 1) pthread_create(&threads[i], 0, worker, (void*)(i + 1));\n"
                "    for (int i = 0; i < 3; i += 1) { void* r; pthread_join(threads[i], &r); total += (long)r; }\n"
                "    printf(\"threads %ld tls %d constructed %d\\n\", total, corpus_tls, constructed);\n"
                "    long table = 0;\n"
                "    for (const struct corpus_entry* e = __start_corpus_registry; e < __stop_corpus_registry; e += 1) table = table * 10 + e->value;\n"
                "    printf(\"table %ld %s\\n\", table, __start_corpus_registry[0].name);\n"
                "    errno = 0;\n"
                "    strtol(\"99999999999999999999999\", 0, 10);\n"
                "    printf(\"errno %d\\n\", errno == ERANGE);\n"
                "    volatile int jumped = 0;\n"
                "    if (setjmp(corpus_jump) == 0) { jumped = 1; longjmp(corpus_jump, 3); }\n"
                "    printf(\"name %s scale %.3f counter %d shared %d jumped %d\\n\", corpus_name(argc), corpus_scale(2.0), corpus_counter, corpus_shared_work(argc), jumped);\n"
                "    printf(\"weak %d env %d args %d %s\\n\", corpus_weak_hook == 0, environ != 0 && getenv(\"CORPUS_ENV\") != 0, argc, argc > 1 ? argv[1] : \"-\");\n"
                "    char line[128];\n"
                "    if (fgets(line, sizeof(line), stdin) != 0) printf(\"input %zu\\n\", strlen(line));\n"
                "    fflush(stdout);\n"
                "    fprintf(stderr, \"stderr %d\\n\", (int)sqrt(49.0 + argc));\n"
                "    return 3;\n"
                "}\n",
    },
    {
        .file = "corpus_counter.c",
        .text = "#include <string.h>\n"
                "struct corpus_entry { const char* name; long value; };\n"
                "static const struct corpus_entry corpus_entry_counter __attribute__((used, section(\"corpus_registry\"))) = { \"counter\", 5 };\n"
                "int corpus_counter = 41;\n"
                "static char corpus_table[4096];\n"
                "int corpus_shared_work(int n) { memset(corpus_table, n, sizeof(corpus_table)); int total = 0; for (unsigned i = 0; i < sizeof(corpus_table); i += 512) total += corpus_table[i]; return total; }\n",
    },
    {
        .file = "corpus_names.c",
        .text = "struct corpus_entry { const char* name; long value; };\n"
                "static const struct corpus_entry corpus_entry_names __attribute__((used, section(\"corpus_registry\"))) = { \"names\", 7 };\n"
                "const char* corpus_name(int n) { static const char* const names[] = { \"zero\", \"one\", \"two\", \"three\" }; return names[n & 3]; }\n",
    },
    {
        .file = "corpus_scale.c",
        .text = "#include <math.h>\ndouble corpus_scale(double x) { return pow(x, 1.5) + sin(x) + cbrt(x * 4.0); }\n",
    },
    {
        .file = "corpus_unused.c",
        .text = "int corpus_unused(void) { return 5; }\n",
    },
    {
        .file = "corpus_shapes.cpp",
        .text = "#include <iostream>\n#include <map>\n#include <memory>\n#include <stdexcept>\n#include <string>\n#include <thread>\n#include <typeinfo>\n#include <vector>\n"
                "struct Shape { virtual ~Shape() = default; virtual double area() const = 0; virtual std::string name() const { return \"shape\"; } };\n"
                "struct Square : Shape { double s; explicit Square(double s) : s(s) {} double area() const override { return s * s; } std::string name() const override { return \"square\"; } };\n"
                "struct Circle : Shape { double r; explicit Circle(double r) : r(r) {} double area() const override { return 3.25 * r * r; } };\n"
                "template <typename T> struct Registry { static inline std::map<std::string, T> entries; };\n"
                "inline int corpus_inline_counter = 3;\n"
                "template <typename T> T total(const std::vector<std::unique_ptr<Shape>>& shapes) { T t = 0; for (auto& s : shapes) t += static_cast<T>(s->area()); return t; }\n"
                "int corpus_throw(int x);\n"
                "int corpus_registered();\n"
                "int main(int argc, char** argv) {\n"
                "    std::vector<std::unique_ptr<Shape>> shapes;\n"
                "    shapes.push_back(std::make_unique<Square>(2.0));\n"
                "    shapes.push_back(std::make_unique<Circle>(1.0));\n"
                "    std::cout << \"total \" << total<double>(shapes) << ' ' << total<int>(shapes) << ' ' << shapes[0]->name() << ' ' << shapes[1]->name() << '\\n';\n"
                "    std::cout << \"rtti \" << (dynamic_cast<Square*>(shapes[0].get()) != nullptr) << (dynamic_cast<Square*>(shapes[1].get()) != nullptr) << ' ' << (typeid(*shapes[1]) == typeid(Circle)) << '\\n';\n"
                "    try { corpus_throw(argc); } catch (const std::runtime_error& e) { std::cout << \"caught \" << e.what() << '\\n'; }\n"
                "    Registry<int>::entries[\"main\"] = argc;\n"
                "    int threaded = 0;\n"
                "    std::thread worker([&threaded] { threaded = corpus_inline_counter * 14; });\n"
                "    worker.join();\n"
                "    std::cout << \"registered \" << corpus_registered() << \" threaded \" << threaded << '\\n';\n"
                "    std::cerr << \"done \" << argv[0][0] << '\\n';\n"
                "    return 0;\n"
                "}\n",
    },
    {
        .file = "corpus_throw.cpp",
        .text = "#include <map>\n#include <stdexcept>\n#include <string>\n"
                "template <typename T> struct Registry { static inline std::map<std::string, T> entries; };\n"
                "inline int corpus_inline_counter = 3;\n"
                "struct Guard { std::string s; ~Guard() { s.clear(); } };\n"
                "int corpus_throw(int x) { Guard g{ \"guard\" }; if (x > 0) throw std::runtime_error(\"boom \" + std::to_string(x)); return x; }\n"
                "int corpus_registered() { Registry<int>::entries[\"throw\"] = corpus_inline_counter; return (int)Registry<int>::entries.size(); }\n",
    },
};

// The names a linker defines for itself, which each defines its own way: each in the regular symbol table, or not, of
// its own visibility (GNU ld's linker script, lld's reserved symbols, mold's synthetic ones).
static bool foreign_corpus_synthesized(const char* name)
{
    static const char* const names[] = {
        "_DYNAMIC", "_GLOBAL_OFFSET_TABLE_", "__GNU_EH_FRAME_HDR", "__ehdr_start", "__executable_start", "_end", "end", "_edata", "edata", "_etext",
        "etext", "__etext", "__bss_start", "__init_array_start", "__init_array_end", "__fini_array_start", "__fini_array_end", "__preinit_array_start",
        "__preinit_array_end", "_PROCEDURE_LINKAGE_TABLE_", "_TLS_MODULE_BASE_", "__rela_iplt_start", "__rela_iplt_end", "__abi_tag",
    };
    bool result = strchr(name, '$') != 0;
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        result = strcmp(names[i], name) == 0;
    }
    return result;
}

typedef struct ForeignCorpus ForeignCorpus;
struct ForeignCorpus
{
    char directory[BUSTER_FOREIGN_TEST_PATH_BYTES];
    const char* self;
    const char* compilers[4];
    uint32_t compiler_count;
    const char* linkers[4];
    uint32_t linker_count;
    ForeignTestSystem system;
    // Each kind's crt objects, by RuntimeObject, as GCC's driver gives them (empty for none): the PIE's are
    // runtime.c's, and the others beside them, as GCC installs them (crt1.o and rcrt1.o beside Scrt1.o, crtbeginT.o and
    // crtend.o beside crtbeginS.o).
    char objects[FOREIGN_CORPUS_KINDS][RUNTIME_OBJECTS][BUSTER_FOREIGN_TEST_PATH_BYTES];
    ForeignTestTally tally;
    uint32_t jobs;
    Arena* arena;
};

// `path`'s directory, with `name` in it, into `result`.
static void foreign_corpus_beside(char* result, const char* path, const char* name)
{
    const char* slash = strrchr(path, '/');
    const int length = slash != 0 ? (int)(slash - path) : 0;
    snprintf(result, BUSTER_FOREIGN_TEST_PATH_BYTES, "%.*s%s%s", length, path, slash != 0 ? "/" : "", name);
}

static void foreign_corpus_objects(ForeignCorpus* corpus)
{
    for (uint32_t kind = 0; kind < FOREIGN_CORPUS_KINDS; kind += 1)
    {
        memcpy(corpus->objects[kind], corpus->system.objects, sizeof(corpus->system.objects));
    }
    const char* start = corpus->system.objects[RUNTIME_OBJECT_START];
    foreign_corpus_beside(corpus->objects[FOREIGN_CORPUS_STATIC][RUNTIME_OBJECT_START], start, "crt1.o");
    foreign_corpus_beside(corpus->objects[FOREIGN_CORPUS_STATIC][RUNTIME_OBJECT_BEGIN], corpus->system.objects[RUNTIME_OBJECT_BEGIN], "crtbeginT.o");
    foreign_corpus_beside(corpus->objects[FOREIGN_CORPUS_STATIC][RUNTIME_OBJECT_END], corpus->system.objects[RUNTIME_OBJECT_END], "crtend.o");
    foreign_corpus_beside(corpus->objects[FOREIGN_CORPUS_STATIC_PIE][RUNTIME_OBJECT_START], start, "rcrt1.o");
    corpus->objects[FOREIGN_CORPUS_SHARED][RUNTIME_OBJECT_START][0] = 0;
}

static void foreign_corpus_append(char** arguments, uint32_t* count, const char* argument)
{
    if (*count + 1 < BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS)
    {
        arguments[*count] = (char*)argument;
        *count += 1;
        arguments[*count] = 0;
    }
}

// Runs each command line of `lines` (each null-terminated), at most `jobs` at once, each one's output and errors into
// its own files named after `logs[i]`: whether every one exited with status 0.
static bool foreign_corpus_spawn_all(char*** lines, char** logs, uint32_t count, uint32_t jobs)
{
    bool result = true;
    pid_t running[64] = { 0 };
    uint32_t started = 0;
    uint32_t active = 0;
    jobs = jobs == 0 ? 1 : jobs > BUSTER_ARRAY_LENGTH(running) ? BUSTER_ARRAY_LENGTH(running) : jobs;
    while (started < count || active != 0)
    {
        while (started < count && active < jobs)
        {
            char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
            char errors[BUSTER_FOREIGN_TEST_PATH_BYTES + 16];
            snprintf(output, sizeof(output), "%s.out", logs[started]);
            snprintf(errors, sizeof(errors), "%s.err", logs[started]);
            posix_spawn_file_actions_t actions;
            pid_t child = 0;
            const bool spawned = posix_spawn_file_actions_init(&actions) == 0 &&
                                 posix_spawn_file_actions_addopen(&actions, 1, output, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                                 posix_spawn_file_actions_addopen(&actions, 2, errors, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                                 posix_spawnp(&child, lines[started][0], &actions, 0, lines[started], environ) == 0;
            posix_spawn_file_actions_destroy(&actions);
            result = result && spawned;
            for (uint32_t slot = 0; spawned && slot < BUSTER_ARRAY_LENGTH(running); slot += 1)
            {
                if (running[slot] == 0)
                {
                    running[slot] = child;
                    active += 1;
                    slot = BUSTER_ARRAY_LENGTH(running);
                }
            }
            started += 1;
        }
        if (active != 0)
        {
            int status = 0;
            const pid_t done = waitpid(-1, &status, 0);
            for (uint32_t slot = 0; done > 0 && slot < BUSTER_ARRAY_LENGTH(running); slot += 1)
            {
                if (running[slot] == done)
                {
                    running[slot] = 0;
                    active -= 1;
                    result = result && WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
            }
            active = done < 0 ? 0 : active;
        }
    }
    return result;
}

// A program's run: ./program with `run`'s arguments and input from `directory`, its output and errors into files beside
// it named after `stem`, and with `library`, its shared library found in `directory` too; its exit status, or -1.
static int foreign_corpus_execute(const char* directory, const ForeignCorpusRun* run, const char* stem, bool library)
{
    char input[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char errors[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    snprintf(input, sizeof(input), "%s/%s.in", directory, stem);
    snprintf(output, sizeof(output), "%s/%s.out", directory, stem);
    snprintf(errors, sizeof(errors), "%s/%s.err", directory, stem);
    const char* text = run->input != 0 ? run->input : "";
    char* arguments[8] = { (char*)"./program" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(run->arguments) && run->arguments[i] != 0; i += 1)
    {
        arguments[i + 1] = (char*)run->arguments[i];
    }
    char* environment[] = { (char*)"CORPUS_ENV=1", (char*)"LANG=C", (char*)"LC_ALL=C", (char*)"PATH=/usr/bin:/bin", (char*)"HOME=/nonexistent",
                            library ? (char*)"LD_LIBRARY_PATH=." : 0, 0 };
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (foreign_test_write(input, text, strlen(text)) && posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 0, input, O_RDONLY, 0) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 1, output, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 2, errors, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addchdir_np(&actions, directory) == 0;
        if (ready && posix_spawn(&child, "./program", &actions, 0, arguments, environment) == 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = { .tv_sec = 0, .tv_nsec = 2000000 };
            for (uint64_t ticks = 0; waited == 0 && ticks < 500ull * BUSTER_FOREIGN_CORPUS_RUN_SECONDS; ticks += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            else if (waited == child)
            {
                result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

// Rows of text, compared as sorted multisets.
typedef struct ForeignCorpusRows ForeignCorpusRows;
struct ForeignCorpusRows
{
    char (*rows)[BUSTER_FOREIGN_CORPUS_ROW_BYTES];
    uint32_t count;
    bool overflow;
};

static void foreign_corpus_row(ForeignCorpusRows* rows, const char* format, ...)
{
    if (rows->count < BUSTER_FOREIGN_CORPUS_MAX_ROWS)
    {
        va_list list;
        va_start(list, format);
        vsnprintf(rows->rows[rows->count], BUSTER_FOREIGN_CORPUS_ROW_BYTES, format, list);
        va_end(list);
        rows->count += 1;
    }
    else
    {
        rows->overflow = true;
    }
}

static int foreign_corpus_compare_rows(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

static ForeignCorpusRows foreign_corpus_rows(Arena* arena)
{
    ForeignCorpusRows result = { 0 };
    result.rows = arena_push(arena, (uint64_t)BUSTER_FOREIGN_CORPUS_MAX_ROWS * BUSTER_FOREIGN_CORPUS_ROW_BYTES, 64);
    result.overflow = result.rows == 0;
    return result;
}

static const char* foreign_corpus_string(const DynamicTestImage* image, const Elf64_Shdr* strings, uint64_t offset)
{
    const char* bytes = (const char*)dynamic_test_bytes(image, strings);
    return bytes != 0 && offset < strings->sh_size ? bytes + offset : "";
}

// .symtab's rows: "name binding type visibility size defined", but bookkeeping and the linkers' own names.
static void foreign_corpus_symbols(const DynamicTestImage* image, ForeignCorpusRows* rows)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".symtab");
    const Elf64_Shdr* strings = table != 0 && table->sh_link < image->header->e_shnum ? &image->section_headers[table->sh_link] : 0;
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    for (uint64_t i = 1; symbols != 0 && strings != 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        const Elf64_Sym* symbol = &symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* name = foreign_corpus_string(image, strings, symbol->st_name);
        if (type != STT_FILE && type != STT_SECTION && name[0] != 0 && !foreign_corpus_synthesized(name))
        {
            const bool defined = symbol->st_shndx != SHN_UNDEF;
            const char* place = !defined ? "undefined" : symbol->st_shndx == SHN_ABS ? "absolute" : "defined";
            foreign_corpus_row(rows, "%s %u %u %u %" PRIu64 " %s", name, ELF64_ST_BIND(symbol->st_info), type, ELF64_ST_VISIBILITY(symbol->st_other),
                               defined ? (uint64_t)symbol->st_size : 0, place);
        }
    }
}

// The version name of .dynsym's entry `index`, through .gnu.version and .gnu.version_r (and .gnu.version_d for an
// executable's own, which link-foreign doesn't write), or "".
static const char* foreign_corpus_version(const DynamicTestImage* image, uint64_t index)
{
    const Elf64_Shdr* versions = dynamic_test_section(image, ".gnu.version");
    const Elf64_Shdr* needs = dynamic_test_section(image, ".gnu.version_r");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Half* indices = (const Elf64_Half*)dynamic_test_bytes(image, versions);
    const uint8_t* need = dynamic_test_bytes(image, needs);
    const Elf64_Half wanted = indices != 0 && index < versions->sh_size / sizeof(Elf64_Half) ? indices[index] & 0x7fff : 0;
    const char* result = "";
    uint64_t offset = 0;
    for (uint32_t entry = 0; wanted > VER_NDX_GLOBAL && need != 0 && strings != 0 && entry < needs->sh_info && offset + sizeof(Elf64_Verneed) <= needs->sh_size; entry += 1)
    {
        Elf64_Verneed file;
        memcpy(&file, need + offset, sizeof(file));
        uint64_t aux = offset + file.vn_aux;
        for (uint32_t a = 0; a < file.vn_cnt && aux + sizeof(Elf64_Vernaux) <= needs->sh_size; a += 1)
        {
            Elf64_Vernaux version;
            memcpy(&version, need + aux, sizeof(version));
            result = version.vna_other == wanted ? foreign_corpus_string(image, strings, version.vna_name) : result;
            aux += version.vna_next;
        }
        offset += file.vn_next;
    }
    return result;
}

// .dynsym's rows: "name@version defined"; mold leaves out a weak undefined name that nothing defines and that has no
// version, which `weak_undefined` counts apart.
static void foreign_corpus_dynamic_symbols(const DynamicTestImage* image, ForeignCorpusRows* rows, uint32_t* weak_undefined)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    for (uint64_t i = 1; symbols != 0 && strings != 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        const Elf64_Sym* symbol = &symbols[i];
        const char* name = foreign_corpus_string(image, strings, symbol->st_name);
        const char* version = foreign_corpus_version(image, i);
        const bool defined = symbol->st_shndx != SHN_UNDEF;
        const bool weak = ELF64_ST_BIND(symbol->st_info) == STB_WEAK;
        if (!defined && weak && version[0] == 0)
        {
            *weak_undefined += 1;
        }
        else
        {
            foreign_corpus_row(rows, "%s@%s %s", name, version, defined ? "defined" : "undefined");
        }
    }
}

// The dynamic relocations' rows, "class name" for each that names a symbol; and the relative ones' count, RELATIVE
// entries and those RELR's words encode.
static uint64_t foreign_corpus_relocations(const DynamicTestImage* image, ForeignCorpusRows* rows)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    const char* sections[] = { ".rela.dyn", ".rela.plt", ".rela.iplt" };
    uint64_t relative = 0;
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sections); s += 1)
    {
        const Elf64_Shdr* section = dynamic_test_section(image, sections[s]);
        const Elf64_Rela* entries = (const Elf64_Rela*)dynamic_test_bytes(image, section);
        for (uint64_t i = 0; entries != 0 && i < section->sh_size / sizeof(Elf64_Rela); i += 1)
        {
            const uint32_t type = ELF64_R_TYPE(entries[i].r_info);
            const uint64_t symbol = ELF64_R_SYM(entries[i].r_info);
            const char* class = type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT ? "slot" : type == R_X86_64_COPY ? "copy" : type == R_X86_64_64 ? "absolute" :
                                type == R_X86_64_TPOFF64 ? "tpoff" : type == R_X86_64_DTPMOD64 ? "dtpmod" : type == R_X86_64_DTPOFF64 ? "dtpoff" :
                                type == R_X86_64_IRELATIVE ? "irelative" : type == R_X86_64_RELATIVE ? 0 : "other";
            relative += type == R_X86_64_RELATIVE ? 1 : 0;
            if (class != 0)
            {
                const bool named = symbols != 0 && symbol != 0 && symbol < table->sh_size / sizeof(Elf64_Sym);
                foreign_corpus_row(rows, "%s %s", class, named ? foreign_corpus_string(image, strings, symbols[symbol].st_name) : "");
            }
        }
    }
    const Elf64_Shdr* relr = dynamic_test_section(image, ".relr.dyn");
    const uint64_t* words = (const uint64_t*)dynamic_test_bytes(image, relr);
    for (uint64_t i = 0; words != 0 && i < relr->sh_size / sizeof(uint64_t); i += 1)
    {
        relative += (words[i] & 1) == 0 ? 1 : (uint64_t)__builtin_popcountll(words[i] >> 1);
    }
    return relative;
}

// The dynamic section's rows that the comparison reads.
static void foreign_corpus_dynamic(const DynamicTestImage* image, ForeignCorpusRows* rows)
{
    const char* needed[64] = { 0 };
    const uint32_t count = dynamic_test_needed(image, needed, BUSTER_ARRAY_LENGTH(needed));
    char line[BUSTER_FOREIGN_CORPUS_ROW_BYTES] = "needed";
    for (uint32_t i = 0; i < count && i < BUSTER_ARRAY_LENGTH(needed); i += 1)
    {
        const uint64_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " %s", needed[i]);
    }
    foreign_corpus_row(rows, "%s", line);
    uint64_t value = 0;
    const bool flags = dynamic_test_tag(image, DT_FLAGS_1, &value);
    foreign_corpus_row(rows, "pie %d", flags && (value & DF_1_PIE) != 0);
    foreign_corpus_row(rows, "soname %d", dynamic_test_tag(image, DT_SONAME, &value));
    foreign_corpus_row(rows, "init %d", dynamic_test_tag(image, DT_INIT, &value));
    foreign_corpus_row(rows, "fini %d", dynamic_test_tag(image, DT_FINI, &value));
    value = 0;
    foreign_corpus_row(rows, "init_array %d %" PRIu64, dynamic_test_tag(image, DT_INIT_ARRAY, &value), dynamic_test_tag(image, DT_INIT_ARRAYSZ, &value) ? value : 0);
    value = 0;
    foreign_corpus_row(rows, "fini_array %d %" PRIu64, dynamic_test_tag(image, DT_FINI_ARRAY, &value), dynamic_test_tag(image, DT_FINI_ARRAYSZ, &value) ? value : 0);
}

// The link of one program by one linker into `output`, as `kind`: the GCC driver's line for it around the objects and
// archives (a shared library's are the program's library part); `lanes`, link-foreign's lanes option. lld and mold get
// the options that describe what link-foreign writes anyway: a build ID, .eh_frame_hdr, .gnu.hash, and imports bound
// at load under RELRO.
static bool foreign_corpus_link(ForeignCorpus* corpus, const ForeignCorpusProgram* program, const char* linker, const char* lanes, ForeignCorpusKind kind,
                                char* const* inputs, uint32_t input_count, const char* output, const char* log)
{
    char* arguments[BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS] = { 0 };
    uint32_t count = 0;
    const bool ours = strcmp(linker, "buster") == 0;
    const bool static_kind = kind == FOREIGN_CORPUS_STATIC || kind == FOREIGN_CORPUS_STATIC_PIE;
    foreign_corpus_append(arguments, &count, ours ? "/proc/self/exe" : linker);
    if (ours)
    {
        foreign_corpus_append(arguments, &count, "link-foreign");
        foreign_corpus_append(arguments, &count, lanes);
    }
    static const char* const kind_options[FOREIGN_CORPUS_KINDS][6] = {
        [FOREIGN_CORPUS_PIE] = { "-pie" },
        [FOREIGN_CORPUS_STATIC] = { "-static" },
        [FOREIGN_CORPUS_STATIC_PIE] = { "-static", "-pie", "--no-dynamic-linker", "-z", "text" },
        [FOREIGN_CORPUS_SHARED] = { "-shared", "-soname", "libcorpus.so" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(kind_options[kind]) && kind_options[kind][i] != 0; i += 1)
    {
        foreign_corpus_append(arguments, &count, kind_options[kind][i]);
    }
    foreign_corpus_append(arguments, &count, "-o");
    foreign_corpus_append(arguments, &count, output);
    if (kind == FOREIGN_CORPUS_PIE)
    {
        foreign_corpus_append(arguments, &count, ours ? "--dynamic-linker" : "-dynamic-linker");
        foreign_corpus_append(arguments, &count, "/lib64/ld-linux-x86-64.so.2");
    }
    if (!ours)
    {
        static const char* const options[] = { "--hash-style=gnu", "--build-id", "--eh-frame-hdr", "-z", "now", "-z", "relro" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options); i += 1)
        {
            foreign_corpus_append(arguments, &count, options[i]);
        }
    }
    for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
    {
        if (corpus->objects[kind][i][0] != 0)
        {
            foreign_corpus_append(arguments, &count, corpus->objects[kind][i]);
        }
    }
    for (uint32_t i = 0; i < input_count; i += 1)
    {
        foreign_corpus_append(arguments, &count, inputs[i]);
    }
    for (uint32_t d = 0; d < 3; d += 1)
    {
        foreign_corpus_append(arguments, &count, corpus->system.directories[d]);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(program->libraries) && program->libraries[i] != 0; i += 1)
    {
        foreign_corpus_append(arguments, &count, program->libraries[i]);
    }
    // GCC's driver's libraries: g++'s -lstdc++ -lm before them, and libgcc and libc around them, as an archive group in a
    // static link.
    static const char* const c_libraries[] = { "-lgcc", "--as-needed", "-lgcc_s", "--no-as-needed", "-lc", "-lgcc", "--as-needed", "-lgcc_s", "--no-as-needed" };
    static const char* const cxx_libraries[] = { "-lstdc++", "-lm", "-lgcc_s", "-lgcc", "-lc", "-lgcc_s", "-lgcc" };
    static const char* const static_libraries[] = { "--start-group", "-lgcc", "-lgcc_eh", "-lc", "--end-group" };
    if (static_kind && program->cxx)
    {
        foreign_corpus_append(arguments, &count, "-lstdc++");
        foreign_corpus_append(arguments, &count, "-lm");
    }
    const char* const* libraries = static_kind ? static_libraries : program->cxx ? cxx_libraries : c_libraries;
    const uint32_t library_count = static_kind ? BUSTER_ARRAY_LENGTH(static_libraries) : program->cxx ? BUSTER_ARRAY_LENGTH(cxx_libraries) : BUSTER_ARRAY_LENGTH(c_libraries);
    for (uint32_t i = 0; i < library_count; i += 1)
    {
        foreign_corpus_append(arguments, &count, libraries[i]);
    }
    for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
    {
        foreign_corpus_append(arguments, &count, corpus->objects[kind][i]);
    }
    char* logs[] = { (char*)log };
    char** lines[] = { arguments };
    return foreign_corpus_spawn_all(lines, logs, 1, 1);
}

// Builds the program's objects with `compiler` (and its archive), in `directory`: their paths in `inputs`, how many.
static uint32_t foreign_corpus_build(ForeignCorpus* corpus, const ForeignCorpusProgram* program, const char* compiler, const char* directory, char** inputs)
{
    Arena* arena = corpus->arena;
    char** lines[2 * BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    char* logs[2 * BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    uint32_t count = 0;
    uint32_t input_count = 0;
    char* members[BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    uint32_t member_count = 0;
    bool built = true;
    for (uint32_t part = 0; built && part < 2; part += 1)
    {
        const char* const* sources = part == 0 ? program->sources : program->archived;
        for (uint32_t i = 0; built && i < BUSTER_FOREIGN_CORPUS_MAX_SOURCES && sources[i] != 0; i += 1)
        {
            char** line = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS);
            char* source = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
            char* object = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
            char* log = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
            built = line != 0 && source != 0 && object != 0 && log != 0;
            if (built)
            {
                const char* base = strrchr(sources[i], '/') != 0 ? strrchr(sources[i], '/') + 1 : sources[i];
                snprintf(source, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s", program->directory != 0 ? program->directory : corpus->directory, sources[i]);
                snprintf(object, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s.o", directory, base);
                snprintf(log, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s.compile", directory, base);
                uint32_t n = 0;
                const uint64_t length = strlen(base);
                const bool cxx = length > 4 && strcmp(base + length - 4, ".cpp") == 0;
                const char* driver = cxx ? (strcmp(compiler, "gcc") == 0 ? "g++" : strcmp(compiler, "clang") == 0 ? "clang++" : compiler) : compiler;
                foreign_corpus_append(line, &n, driver);
                foreign_corpus_append(line, &n, "-c");
                foreign_corpus_append(line, &n, "-O2");
                for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(program->flags) && program->flags[f] != 0; f += 1)
                {
                    foreign_corpus_append(line, &n, program->flags[f]);
                }
                foreign_corpus_append(line, &n, source);
                foreign_corpus_append(line, &n, "-o");
                foreign_corpus_append(line, &n, object);
                lines[count] = line;
                logs[count] = log;
                count += 1;
                if (part == 0)
                {
                    inputs[input_count] = object;
                    input_count += 1;
                }
                else
                {
                    members[member_count] = object;
                    member_count += 1;
                }
            }
        }
    }
    built = built && foreign_corpus_spawn_all(lines, logs, count, corpus->jobs);
    if (built && member_count != 0)
    {
        char** line = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS);
        char* archive = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
        char* log = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
        built = line != 0 && archive != 0 && log != 0;
        if (built)
        {
            snprintf(archive, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/libcorpus.a", directory);
            snprintf(log, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/archive", directory);
            uint32_t n = 0;
            foreign_corpus_append(line, &n, "ar");
            foreign_corpus_append(line, &n, "rcs");
            foreign_corpus_append(line, &n, archive);
            for (uint32_t m = 0; m < member_count; m += 1)
            {
                foreign_corpus_append(line, &n, members[m]);
            }
            char** lines_archive[] = { line };
            char* logs_archive[] = { log };
            built = foreign_corpus_spawn_all(lines_archive, logs_archive, 1, 1);
            inputs[input_count] = archive;
            input_count += 1;
        }
    }
    return built ? input_count : 0;
}

// Whether source `source` is one of the program's library part.
static bool foreign_corpus_in_library(const ForeignCorpusProgram* program, const char* source)
{
    bool result = false;
    for (uint32_t i = 0; !result && i < BUSTER_FOREIGN_CORPUS_MAX_SOURCES && program->library[i] != 0; i += 1)
    {
        result = strcmp(program->library[i], source) == 0;
    }
    return result;
}

// The shared kind's inputs, after foreign_corpus_build: the library part compiled again with -fPIC, as
// <directory>/pic-<source>.o, into `library` (their count into *library_count), and the objects of the other sources,
// which foreign_corpus_build made, into `executable` (their count into *executable_count). Whether it compiled.
static bool foreign_corpus_build_library(ForeignCorpus* corpus, const ForeignCorpusProgram* program, const char* compiler, const char* directory, char** library,
                                         uint32_t* library_count, char** executable, uint32_t* executable_count)
{
    Arena* arena = corpus->arena;
    char** lines[2 * BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    char* logs[2 * BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    uint32_t count = 0;
    bool built = true;
    *library_count = 0;
    *executable_count = 0;
    for (uint32_t part = 0; built && part < 2; part += 1)
    {
        const char* const* sources = part == 0 ? program->sources : program->archived;
        for (uint32_t i = 0; built && i < BUSTER_FOREIGN_CORPUS_MAX_SOURCES && sources[i] != 0; i += 1)
        {
            const char* base = strrchr(sources[i], '/') != 0 ? strrchr(sources[i], '/') + 1 : sources[i];
            char* object = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
            built = object != 0;
            const bool pic = built && foreign_corpus_in_library(program, sources[i]);
            if (built && !pic)
            {
                snprintf(object, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s.o", directory, base);
                executable[*executable_count] = object;
                *executable_count += 1;
            }
            else if (built)
            {
                char** line = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_CORPUS_MAX_ARGUMENTS);
                char* source = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
                char* log = arena_push(arena, BUSTER_FOREIGN_TEST_PATH_BYTES, 1);
                built = line != 0 && source != 0 && log != 0;
                if (built)
                {
                    snprintf(source, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/%s", program->directory != 0 ? program->directory : corpus->directory, sources[i]);
                    snprintf(object, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/pic-%s.o", directory, base);
                    snprintf(log, BUSTER_FOREIGN_TEST_PATH_BYTES, "%s/pic-%s.compile", directory, base);
                    uint32_t n = 0;
                    const uint64_t length = strlen(base);
                    const bool cxx = length > 4 && strcmp(base + length - 4, ".cpp") == 0;
                    const char* driver = cxx ? (strcmp(compiler, "gcc") == 0 ? "g++" : strcmp(compiler, "clang") == 0 ? "clang++" : compiler) : compiler;
                    foreign_corpus_append(line, &n, driver);
                    foreign_corpus_append(line, &n, "-c");
                    foreign_corpus_append(line, &n, "-O2");
                    foreign_corpus_append(line, &n, "-fPIC");
                    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(program->flags) && program->flags[f] != 0; f += 1)
                    {
                        foreign_corpus_append(line, &n, program->flags[f]);
                    }
                    foreign_corpus_append(line, &n, source);
                    foreign_corpus_append(line, &n, "-o");
                    foreign_corpus_append(line, &n, object);
                    lines[count] = line;
                    logs[count] = log;
                    count += 1;
                    library[*library_count] = object;
                    *library_count += 1;
                }
            }
        }
    }
    return built && *library_count != 0 && foreign_corpus_spawn_all(lines, logs, count, corpus->jobs);
}

// What a reference linker writes otherwise than link-foreign, where both choices are valid: the comparison counts each
// row it explains as documented, prints the counts, and fails on any other row one side has alone.
typedef enum ForeignCorpusReason
{
    FOREIGN_CORPUS_REASON_NONE,
    FOREIGN_CORPUS_REASON_WEAK_UNDEFINED,
    FOREIGN_CORPUS_REASON_GROUP_SIGNATURE,
    FOREIGN_CORPUS_REASON_FRAME_END,
    FOREIGN_CORPUS_REASON_WEAK_ALIAS,
    FOREIGN_CORPUS_REASON_INTERNAL,
    FOREIGN_CORPUS_REASON_BOUNDS,
    FOREIGN_CORPUS_REASON_SYMBOLIC_COPY,
    FOREIGN_CORPUS_REASON_SECOND_SLOT,
    FOREIGN_CORPUS_REASON_RELATIVE,
    FOREIGN_CORPUS_REASON_CANONICAL_IFUNC,
    FOREIGN_CORPUS_REASON_LINK_WARNING,
    FOREIGN_CORPUS_REASON_LOCAL_VISIBILITY,
    FOREIGN_CORPUS_REASON_IFUNC_FUNCTION,
    FOREIGN_CORPUS_REASON_UNREFERENCED_BOUNDS,
    FOREIGN_CORPUS_REASON_IFUNC_SECOND_SLOT,
    FOREIGN_CORPUS_REASONS,
} ForeignCorpusReason;

static const char* const foreign_corpus_reasons[FOREIGN_CORPUS_REASONS] = {
    [FOREIGN_CORPUS_REASON_WEAK_UNDEFINED] = "a weak undefined name that nothing defines and that has no version is left out of .dynsym, with its GOT slot's relocation",
    [FOREIGN_CORPUS_REASON_GROUP_SIGNATURE] = "a COMDAT group's signature, a local of its group section, is kept as an absolute local",
    [FOREIGN_CORPUS_REASON_FRAME_END] = "crtend's __FRAME_END__ and crtbeginT.o's __EH_FRAME_BEGIN__, locals of .eh_frame, are left out, with the .eh_frame it rebuilds",
    [FOREIGN_CORPUS_REASON_WEAK_ALIAS] = "the names a copy relocation defines are bound weakly",
    [FOREIGN_CORPUS_REASON_INTERNAL] = "a symbol of internal visibility is written as hidden",
    [FOREIGN_CORPUS_REASON_BOUNDS] = "the __start_ and __stop_ names are written local, where GNU ld and lld write them global and protected",
    [FOREIGN_CORPUS_REASON_SYMBOLIC_COPY] = "an R_X86_64_64 to a library's data, scanned before the reference that copies it, stays symbolic",
    [FOREIGN_CORPUS_REASON_SECOND_SLOT] = "a function that code both calls and loads from the GOT has two slots, .got.plt's and .got's, where link-foreign's stub reads the one",
    [FOREIGN_CORPUS_REASON_RELATIVE] = "a few GOT slots that lld and link-foreign relax keep relative relocations",
    [FOREIGN_CORPUS_REASON_CANONICAL_IFUNC] = "an IFUNC whose address is taken is written as a function of no size at its stub, where link-foreign keeps the IFUNC at its resolver",
    [FOREIGN_CORPUS_REASON_LINK_WARNING] = "the symbols of libc.a's .gnu.warning sections, which GNU ld reads for link-time warnings and link-foreign leaves out with them, are kept",
    [FOREIGN_CORPUS_REASON_LOCAL_VISIBILITY] = "in a static executable, a symbol of hidden visibility, written local, is written with default visibility",
    [FOREIGN_CORPUS_REASON_IFUNC_FUNCTION] = "in a static executable, an IFUNC is written as a function at its resolver",
    [FOREIGN_CORPUS_REASON_UNREFERENCED_BOUNDS] = "the __start_ and __stop_ names of every section named as a C identifier are defined, local, though nothing refers to them",
    [FOREIGN_CORPUS_REASON_IFUNC_SECOND_SLOT] = "an IFUNC that code both calls and loads from the GOT has two IRELATIVE slots, where link-foreign's loads read its stub's, as lld's do",
};

typedef enum ForeignCorpusCategory
{
    FOREIGN_CORPUS_SYMBOLS,
    FOREIGN_CORPUS_DYNAMIC,
    FOREIGN_CORPUS_VERSIONS,
    FOREIGN_CORPUS_RELOCATIONS,
} ForeignCorpusCategory;

typedef struct ForeignCorpusContext ForeignCorpusContext;
struct ForeignCorpusContext
{
    const char* linker;
    bool mold;
    bool lld;
    // The names our image defines at a COPY relocation's address, a copy and its aliases.
    ForeignCorpusRows copied;
    uint32_t documented[FOREIGN_CORPUS_REASONS];
};

static bool foreign_corpus_listed(const ForeignCorpusRows* rows, const char* text)
{
    bool result = false;
    for (uint32_t i = 0; !result && i < rows->count; i += 1)
    {
        result = strcmp(rows->rows[i], text) == 0;
    }
    return result;
}

// Whether two symbol rows ("name binding type visibility size place") differ in field `field` alone, `a`'s being
// `a_value` and `b`'s `b_value` (or any two values for 0).
static bool foreign_corpus_differs_in(const char* a, const char* b, uint32_t field, const char* a_value, const char* b_value)
{
    char a_fields[6][BUSTER_FOREIGN_CORPUS_ROW_BYTES] = { 0 };
    char b_fields[6][BUSTER_FOREIGN_CORPUS_ROW_BYTES] = { 0 };
    const bool parsed = sscanf(a, "%191s %191s %191s %191s %191s %191s", a_fields[0], a_fields[1], a_fields[2], a_fields[3], a_fields[4], a_fields[5]) == 6 &&
                        sscanf(b, "%191s %191s %191s %191s %191s %191s", b_fields[0], b_fields[1], b_fields[2], b_fields[3], b_fields[4], b_fields[5]) == 6;
    bool result = parsed && strcmp(a_fields[field], b_fields[field]) != 0 && (a_value == 0 || strcmp(a_fields[field], a_value) == 0) &&
                  (b_value == 0 || strcmp(b_fields[field], b_value) == 0);
    for (uint32_t f = 0; result && f < 6; f += 1)
    {
        result = f == field || strcmp(a_fields[f], b_fields[f]) == 0;
    }
    return result;
}

// A symbol row's six fields ("name binding type visibility size place"): whether it has them.
static bool foreign_corpus_fields(const char* row, char (*fields)[BUSTER_FOREIGN_CORPUS_ROW_BYTES])
{
    return sscanf(row, "%191s %191s %191s %191s %191s %191s", fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]) == 6;
}

// Whether a symbol row of link-foreign's and one of lld's are an IFUNC whose address is taken, which link-foreign keeps
// as an IFUNC (type 10) and lld writes as a function (type 2) of no size at its PLT entry: the same name, binding,
// visibility and place.
static bool foreign_corpus_canonical_ifunc(const char* ours, const char* theirs)
{
    char a[6][BUSTER_FOREIGN_CORPUS_ROW_BYTES] = { 0 };
    char b[6][BUSTER_FOREIGN_CORPUS_ROW_BYTES] = { 0 };
    return foreign_corpus_fields(ours, a) && foreign_corpus_fields(theirs, b) && strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 && strcmp(a[3], b[3]) == 0 &&
           strcmp(a[5], b[5]) == 0 && strcmp(a[2], "10") == 0 && strcmp(b[2], "2") == 0 && strcmp(b[4], "0") == 0;
}

// Whether one of a row set's unmatched rows differs from `row` in field `field` alone, `row`'s being `value` and the
// other's `other_value`.
static bool foreign_corpus_paired(const char* row, const ForeignCorpusRows* rows, const uint32_t* unmatched, uint32_t count, uint32_t field, const char* value,
                                  const char* other_value)
{
    bool result = false;
    for (uint32_t i = 0; !result && i < count; i += 1)
    {
        result = foreign_corpus_differs_in(row, rows->rows[unmatched[i]], field, value, other_value);
    }
    return result;
}

// Whether a row set has a row of symbol `name` among the unmatched ones.
static bool foreign_corpus_has_name(const ForeignCorpusRows* rows, const uint32_t* unmatched, uint32_t count, const char* name)
{
    bool result = false;
    const uint64_t length = strlen(name);
    for (uint32_t i = 0; !result && i < count; i += 1)
    {
        const char* row = rows->rows[unmatched[i]];
        result = strncmp(row, name, length) == 0 && row[length] == ' ';
    }
    return result;
}

// The reason a row that one side has alone (ours, or the reference's) is the reference linker's documented choice.
static ForeignCorpusReason foreign_corpus_reason(const ForeignCorpusContext* context, ForeignCorpusCategory category, bool ours, const char* row,
                                                 const ForeignCorpusRows* other, const uint32_t* other_unmatched, uint32_t other_count)
{
    ForeignCorpusReason result = FOREIGN_CORPUS_REASON_NONE;
    char name[BUSTER_FOREIGN_CORPUS_ROW_BYTES];
    const char* space = strchr(row, ' ');
    const uint64_t length = space != 0 ? (uint64_t)(space - row) : strlen(row);
    snprintf(name, sizeof(name), "%.*s", (int)length, row);
    char fields[6][BUSTER_FOREIGN_CORPUS_ROW_BYTES] = { 0 };
    const bool parsed = category == FOREIGN_CORPUS_SYMBOLS && foreign_corpus_fields(row, fields);
    if (category == FOREIGN_CORPUS_SYMBOLS && context->lld)
    {
        bool canonical = false;
        for (uint32_t i = 0; i < other_count; i += 1)
        {
            const char* paired = other->rows[other_unmatched[i]];
            canonical = canonical || (ours ? foreign_corpus_canonical_ifunc(row, paired) : foreign_corpus_canonical_ifunc(paired, row));
        }
        result = canonical                                               ? FOREIGN_CORPUS_REASON_CANONICAL_IFUNC :
                 !ours && strncmp(name, "__evoke_link_warning_", 21) == 0 ? FOREIGN_CORPUS_REASON_LINK_WARNING :
                                                                         FOREIGN_CORPUS_REASON_NONE;
    }
    else if (category == FOREIGN_CORPUS_SYMBOLS && context->mold && !ours && strncmp(name, "__evoke_link_warning_", 21) == 0)
    {
        result = FOREIGN_CORPUS_REASON_LINK_WARNING;
    }
    else if (category == FOREIGN_CORPUS_SYMBOLS && context->mold && parsed && strcmp(fields[1], "0") == 0 &&
             foreign_corpus_paired(row, other, other_unmatched, other_count, 3, ours ? "2" : "0", ours ? "0" : "2"))
    {
        result = FOREIGN_CORPUS_REASON_LOCAL_VISIBILITY;
    }
    else if (category == FOREIGN_CORPUS_SYMBOLS && context->mold && parsed &&
             foreign_corpus_paired(row, other, other_unmatched, other_count, 2, ours ? "10" : "2", ours ? "2" : "10"))
    {
        result = FOREIGN_CORPUS_REASON_IFUNC_FUNCTION;
    }
    else if (category == FOREIGN_CORPUS_SYMBOLS && context->mold && parsed && !ours && strcmp(fields[1], "0") == 0 &&
             (strncmp(name, "__start_", 8) == 0 || strncmp(name, "__stop_", 7) == 0) && !foreign_corpus_has_name(other, other_unmatched, other_count, name))
    {
        result = FOREIGN_CORPUS_REASON_UNREFERENCED_BOUNDS;
    }
    else if (category == FOREIGN_CORPUS_SYMBOLS && context->mold)
    {
        const uint64_t row_length = strlen(row);
        const char* signature = " 0 0 0 0 absolute";
        const bool absolute_local = !ours && row_length > strlen(signature) && strcmp(row + row_length - strlen(signature), signature) == 0;
        bool alias = false;
        bool internal = false;
        bool bounds = false;
        const bool bound_name = strncmp(name, "__start_", 8) == 0 || strncmp(name, "__stop_", 7) == 0;
        for (uint32_t i = 0; i < other_count; i += 1)
        {
            const char* paired = other->rows[other_unmatched[i]];
            alias = alias || foreign_corpus_differs_in(row, paired, 1, 0, 0);
            internal = internal || foreign_corpus_differs_in(row, paired, 3, ours ? "1" : "2", ours ? "2" : "1");
            bounds = bounds || (bound_name && strncmp(paired, name, strlen(name)) == 0 && paired[strlen(name)] == ' ');
        }
        alias = alias && foreign_corpus_listed(&context->copied, name);
        const bool frame_label = strcmp(name, "__FRAME_END__") == 0 || strcmp(name, "__EH_FRAME_BEGIN__") == 0;
        result = absolute_local ? FOREIGN_CORPUS_REASON_GROUP_SIGNATURE : ours && frame_label ? FOREIGN_CORPUS_REASON_FRAME_END :
                 alias ? FOREIGN_CORPUS_REASON_WEAK_ALIAS : internal ? FOREIGN_CORPUS_REASON_INTERNAL : bounds ? FOREIGN_CORPUS_REASON_BOUNDS : FOREIGN_CORPUS_REASON_NONE;
    }
    else if (category == FOREIGN_CORPUS_RELOCATIONS && !ours && strncmp(row, "absolute ", 9) == 0)
    {
        result = foreign_corpus_listed(&context->copied, row + 9) ? FOREIGN_CORPUS_REASON_SYMBOLIC_COPY : FOREIGN_CORPUS_REASON_NONE;
    }
    else if (category == FOREIGN_CORPUS_RELOCATIONS && context->mold && !ours && strcmp(row, "irelative ") == 0)
    {
        result = FOREIGN_CORPUS_REASON_IFUNC_SECOND_SLOT;
    }
    else if (category == FOREIGN_CORPUS_RELOCATIONS && !ours && strncmp(row, "slot ", 5) == 0)
    {
        // A second slot of a name link-foreign has one slot of (matched).
        result = foreign_corpus_listed(other, row) ? FOREIGN_CORPUS_REASON_SECOND_SLOT : FOREIGN_CORPUS_REASON_NONE;
    }
    return result;
}

// Whether two row sets are equal as multisets, but for the rows foreign_corpus_reason explains, which it counts; up to 12
// of the others are printed.
static bool foreign_corpus_same_rows(ForeignCorpusRows* ours, ForeignCorpusRows* theirs, ForeignCorpusCategory category, ForeignCorpusContext* context, Arena* arena)
{
    static const char* const what[] = { "symbol", "dynamic", "version", "relocation" };
    qsort(ours->rows, ours->count, BUSTER_FOREIGN_CORPUS_ROW_BYTES, foreign_corpus_compare_rows);
    qsort(theirs->rows, theirs->count, BUSTER_FOREIGN_CORPUS_ROW_BYTES, foreign_corpus_compare_rows);
    const uint64_t mark = arena->length;
    uint32_t* ours_unmatched = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)ours->count + 1);
    uint32_t* theirs_unmatched = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)theirs->count + 1);
    bool same = !ours->overflow && !theirs->overflow && ours_unmatched != 0 && theirs_unmatched != 0;
    uint32_t ours_count = 0;
    uint32_t theirs_count = 0;
    uint32_t i = 0;
    uint32_t j = 0;
    while (same && (i < ours->count || j < theirs->count))
    {
        const int order = i == ours->count ? 1 : j == theirs->count ? -1 : strcmp(ours->rows[i], theirs->rows[j]);
        if (order < 0)
        {
            ours_unmatched[ours_count++] = i;
        }
        else if (order > 0)
        {
            theirs_unmatched[theirs_count++] = j;
        }
        i += order <= 0 ? 1 : 0;
        j += order >= 0 ? 1 : 0;
    }
    uint32_t printed = 0;
    for (uint32_t side = 0; side < 2; side += 1)
    {
        const bool our_side = side == 0;
        const ForeignCorpusRows* rows = our_side ? ours : theirs;
        const uint32_t* unmatched = our_side ? ours_unmatched : theirs_unmatched;
        const uint32_t count = our_side ? ours_count : theirs_count;
        for (uint32_t k = 0; unmatched != 0 && k < count; k += 1)
        {
            const char* row = rows->rows[unmatched[k]];
            const ForeignCorpusReason reason = foreign_corpus_reason(context, category, our_side, row, our_side ? theirs : ours, our_side ? theirs_unmatched : ours_unmatched,
                                                                     our_side ? theirs_count : ours_count);
            context->documented[reason] += reason != FOREIGN_CORPUS_REASON_NONE ? 1 : 0;
            same = same && reason != FOREIGN_CORPUS_REASON_NONE;
            if (reason == FOREIGN_CORPUS_REASON_NONE && printed < 12)
            {
                printf("    %s only in %s's: %s\n", what[category], our_side ? "link-foreign" : context->linker, row);
                printed += 1;
            }
        }
    }
    arena_pop_to(arena, mark);
    return same;
}

// The names an image defines at a COPY relocation's address, into `copied`.
static void foreign_corpus_copied(const DynamicTestImage* image, ForeignCorpusRows* copied)
{
    const Elf64_Shdr* table = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* strings = dynamic_test_section(image, ".dynstr");
    const Elf64_Shdr* section = dynamic_test_section(image, ".rela.dyn");
    const Elf64_Sym* symbols = (const Elf64_Sym*)dynamic_test_bytes(image, table);
    const Elf64_Rela* entries = (const Elf64_Rela*)dynamic_test_bytes(image, section);
    for (uint64_t r = 0; entries != 0 && symbols != 0 && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
    {
        for (uint64_t i = 1; ELF64_R_TYPE(entries[r].r_info) == R_X86_64_COPY && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
        {
            if (symbols[i].st_shndx != SHN_UNDEF && symbols[i].st_value == entries[r].r_offset)
            {
                foreign_corpus_row(copied, "%s", foreign_corpus_string(image, strings, symbols[i].st_name));
            }
        }
    }
}

static void foreign_corpus_compare(ForeignCorpus* corpus, const DynamicTestImage* ours_image, String8 file, bool linked, const char* linker, const char* detail);

// An output as dynamic_test_image reads one, but a static executable (ET_EXEC) too, which it doesn't take.
static DynamicTestImage foreign_corpus_image(String8 file)
{
    DynamicTestImage result = { .file = file };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    result.valid = file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                   (header->e_type == ET_DYN || header->e_type == ET_EXEC) && header->e_machine == EM_X86_64 &&
                   header->e_phoff + header->e_phnum * sizeof(Elf64_Phdr) <= file.length && header->e_shoff + header->e_shnum * sizeof(Elf64_Shdr) <= file.length &&
                   header->e_shstrndx < header->e_shnum;
    if (result.valid)
    {
        result.header = header;
        result.program_headers = (const Elf64_Phdr*)(file.pointer + header->e_phoff);
        result.section_headers = (const Elf64_Shdr*)(file.pointer + header->e_shoff);
        const Elf64_Shdr* names = &result.section_headers[header->e_shstrndx];
        result.valid = names->sh_offset + names->sh_size <= file.length;
        result.names = file.pointer + names->sh_offset;
    }
    return result;
}

// One kind of one program's output, built by one compiler: linked by every linker in <directory>/<kind>/<linker>, run
// and compared. `inputs` are the program's objects and archive; for the shared kind, `library` is its library part
// compiled with -fPIC, which each linker links into libcorpus.so, and `executable` the rest, which the first reference
// linker links into a PIE against its own library, run from each linker's directory with that linker's.
static void foreign_corpus_kind(ForeignCorpus* corpus, const ForeignCorpusProgram* program, const char* directory, const char* detail, ForeignCorpusKind kind,
                                char** inputs, uint32_t input_count, char** library, uint32_t library_count, char** executable, uint32_t executable_count)
{
    Arena* arena = corpus->arena;
    const bool shared = kind == FOREIGN_CORPUS_SHARED;
    char base[BUSTER_FOREIGN_TEST_PATH_BYTES + 32];
    char what[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
    snprintf(base, sizeof(base), "%s/%s", directory, foreign_corpus_kind_names[kind]);
    snprintf(what, sizeof(what), "%s, %s", detail, foreign_corpus_kind_names[kind]);
    const bool made = mkdir(base, 0755) == 0;
    foreign_test_expect(&corpus->tally, made, "make the kind's directory", what);
    const char* linkers[5] = { "buster" };
    for (uint32_t l = 0; l < corpus->linker_count; l += 1)
    {
        linkers[l + 1] = corpus->linkers[l];
    }
    const char* output_name = shared ? "libcorpus.so" : "program";
    char* const* link_inputs = shared ? library : inputs;
    const uint32_t link_count = shared ? library_count : input_count;
    String8 files[5] = { 0 };
    bool linked[5] = { false };
    for (uint32_t l = 0; made && l <= corpus->linker_count; l += 1)
    {
        char place[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        char log[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        snprintf(place, sizeof(place), "%s/%s", base, linkers[l]);
        snprintf(output, sizeof(output), "%s/%s", place, output_name);
        snprintf(log, sizeof(log), "%s/link", place);
        linked[l] = mkdir(place, 0755) == 0 && foreign_corpus_link(corpus, program, linkers[l], "--lanes=1", kind, link_inputs, link_count, output, log);
        char linked_what[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        snprintf(linked_what, sizeof(linked_what), "%s, linked by %s", what, linkers[l]);
        foreign_test_expect(&corpus->tally, linked[l], "link the corpus program", linked_what);
        files[l] = linked[l] ? foreign_test_read(arena, output) : (String8){ 0 };
    }
    // link-foreign on eight lanes, the resolution's, the conversion's and the writer's lane paths: the same bytes.
    if (linked[0])
    {
        char place[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        char output[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        char log[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        snprintf(place, sizeof(place), "%s/buster-lanes", base);
        snprintf(output, sizeof(output), "%s/%s", place, output_name);
        snprintf(log, sizeof(log), "%s/link", place);
        const bool laned = mkdir(place, 0755) == 0 && foreign_corpus_link(corpus, program, "buster", "--lanes=8", kind, link_inputs, link_count, output, log);
        const String8 file = laned ? foreign_test_read(arena, output) : (String8){ 0 };
        const bool same = file.pointer != 0 && file.length == files[0].length && memcmp(file.pointer, files[0].pointer, file.length) == 0;
        foreign_test_expect(&corpus->tally, same, "link-foreign writes the same bytes on eight lanes as on one", what);
    }
    // The shared kind's PIE, against the first reference linker's library, in every linker's directory.
    bool runnable = linked[0];
    if (shared && linked[0] && corpus->linker_count != 0 && linked[1])
    {
        char reference[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        char search[BUSTER_FOREIGN_TEST_PATH_BYTES + 72];
        char program_path[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        char log[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        snprintf(reference, sizeof(reference), "%s/%s", base, linkers[1]);
        snprintf(search, sizeof(search), "-L%s", reference);
        snprintf(program_path, sizeof(program_path), "%s/program", base);
        snprintf(log, sizeof(log), "%s/program-link", base);
        char* executable_inputs[BUSTER_FOREIGN_CORPUS_MAX_SOURCES + 3];
        memcpy(executable_inputs, executable, (uint64_t)executable_count * sizeof(char*));
        executable_inputs[executable_count] = search;
        executable_inputs[executable_count + 1] = (char*)"-lcorpus";
        runnable = foreign_corpus_link(corpus, program, linkers[1], "--lanes=1", FOREIGN_CORPUS_PIE, executable_inputs, executable_count + 2, program_path, log);
        for (uint32_t l = 0; runnable && l <= corpus->linker_count; l += 1)
        {
            char copy[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
            snprintf(copy, sizeof(copy), "%s/%s/program", base, linkers[l]);
            runnable = !linked[l] || link(program_path, copy) == 0;
        }
        foreign_test_expect(&corpus->tally, runnable, "link the shared kind's program against the reference library", what);
    }
    else if (shared)
    {
        runnable = false;
    }
    // The runs, each by every linker's output.
    for (uint32_t r = 0; runnable && r < BUSTER_ARRAY_LENGTH(program->runs) && (r == 0 || program->runs[r].arguments[0] != 0 || program->runs[r].input != 0); r += 1)
    {
        char stem[32];
        snprintf(stem, sizeof(stem), "run%u", r);
        char place[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
        snprintf(place, sizeof(place), "%s/buster", base);
        const int status = foreign_corpus_execute(place, &program->runs[r], stem, shared);
        char path[BUSTER_FOREIGN_TEST_PATH_BYTES + 96];
        snprintf(path, sizeof(path), "%s/%s.out", place, stem);
        const String8 output = foreign_test_read(arena, path);
        snprintf(path, sizeof(path), "%s/%s.err", place, stem);
        const String8 errors = foreign_test_read(arena, path);
        for (uint32_t l = 1; l <= corpus->linker_count; l += 1)
        {
            if (linked[l])
            {
                snprintf(place, sizeof(place), "%s/%s", base, linkers[l]);
                const int their_status = foreign_corpus_execute(place, &program->runs[r], stem, shared);
                snprintf(path, sizeof(path), "%s/%s.out", place, stem);
                const String8 their_output = foreign_test_read(arena, path);
                snprintf(path, sizeof(path), "%s/%s.err", place, stem);
                const String8 their_errors = foreign_test_read(arena, path);
                const bool same = status >= 0 && status == their_status && output.length == their_output.length && errors.length == their_errors.length &&
                                  memcmp(output.pointer, their_output.pointer, output.length) == 0 && memcmp(errors.pointer, their_errors.pointer, errors.length) == 0;
                char run_what[512];
                snprintf(run_what, sizeof(run_what), "%s, run %u: status %d against %s's %d", what, r, status, linkers[l], their_status);
                foreign_test_expect(&corpus->tally, same, "the program runs as the reference linker's does", run_what);
            }
        }
    }
    // The files.
    const DynamicTestImage ours = foreign_corpus_image(files[0]);
    for (uint32_t l = 1; linked[0] && l <= corpus->linker_count; l += 1)
    {
        foreign_corpus_compare(corpus, &ours, files[l], linked[l], linkers[l], what);
    }
}

// One program, built by one compiler and linked as each kind of output by every linker, compared.
static void foreign_corpus_program(ForeignCorpus* corpus, const ForeignCorpusProgram* program, const char* compiler)
{
    Arena* arena = corpus->arena;
    const uint64_t saved = arena->length;
    char directory[BUSTER_FOREIGN_TEST_PATH_BYTES];
    char detail[256];
    snprintf(directory, sizeof(directory), "%s/%s-%s", corpus->directory, program->name, compiler);
    snprintf(detail, sizeof(detail), "%s by %s", program->name, compiler);
    char* inputs[BUSTER_FOREIGN_CORPUS_MAX_SOURCES + 1];
    const bool made = mkdir(directory, 0755) == 0;
    const uint32_t input_count = made ? foreign_corpus_build(corpus, program, compiler, directory, inputs) : 0;
    foreign_test_expect(&corpus->tally, input_count != 0, "compile the corpus program", detail);
    char* library[BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    char* executable[2 * BUSTER_FOREIGN_CORPUS_MAX_SOURCES];
    uint32_t library_count = 0;
    uint32_t executable_count = 0;
    const bool library_part = program->library[0] != 0 && (program->skipped & (1u << FOREIGN_CORPUS_SHARED)) == 0;
    const bool library_built = input_count != 0 && library_part &&
                               foreign_corpus_build_library(corpus, program, compiler, directory, library, &library_count, executable, &executable_count);
    if (library_part)
    {
        foreign_test_expect(&corpus->tally, library_built, "compile the corpus program's library part with -fPIC", detail);
    }
    for (uint32_t kind = 0; input_count != 0 && kind < FOREIGN_CORPUS_KINDS; kind += 1)
    {
        const bool wanted = (program->skipped & (1u << kind)) == 0 && (kind != FOREIGN_CORPUS_SHARED || library_built);
        if (wanted)
        {
            const uint64_t mark = arena->length;
            foreign_corpus_kind(corpus, program, directory, detail, (ForeignCorpusKind)kind, inputs, input_count, library, library_count, executable, executable_count);
            arena_pop_to(arena, mark);
        }
    }
    arena_pop_to(arena, saved);
}

// The comparison of link-foreign's file with one reference linker's (see the top of the file).
static void foreign_corpus_compare(ForeignCorpus* corpus, const DynamicTestImage* ours_image, String8 file, bool linked, const char* linker, const char* detail)
{
    Arena* arena = corpus->arena;
    const uint64_t mark = arena->length;
    const DynamicTestImage theirs = foreign_corpus_image(file);
    ForeignCorpusContext context = {
        .linker = linker,
        .mold = strstr(linker, "mold") != 0,
        .lld = strstr(linker, "lld") != 0,
        .copied = foreign_corpus_rows(arena),
    };
    char what[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
    snprintf(what, sizeof(what), "%s, against %s", detail, linker);
    // An output the comparison can't read would compare as nothing against nothing.
    foreign_test_expect(&corpus->tally, !linked || (ours_image->valid && theirs.valid), "read both outputs' headers", what);
    if (linked && ours_image->valid && theirs.valid)
    {
        foreign_corpus_copied(ours_image, &context.copied);
        ForeignCorpusRows our_rows = foreign_corpus_rows(arena);
        ForeignCorpusRows their_rows = foreign_corpus_rows(arena);
        printf("  %s:\n", what);
        foreign_corpus_symbols(ours_image, &our_rows);
        foreign_corpus_symbols(&theirs, &their_rows);
        foreign_test_expect(&corpus->tally, foreign_corpus_same_rows(&our_rows, &their_rows, FOREIGN_CORPUS_SYMBOLS, &context, arena), "the same symbols with the same sizes", what);
        our_rows.count = 0;
        their_rows.count = 0;
        foreign_corpus_dynamic(ours_image, &our_rows);
        foreign_corpus_dynamic(&theirs, &their_rows);
        foreign_test_expect(&corpus->tally, foreign_corpus_same_rows(&our_rows, &their_rows, FOREIGN_CORPUS_DYNAMIC, &context, arena), "the same dynamic section", what);
        our_rows.count = 0;
        their_rows.count = 0;
        uint32_t our_weak = 0;
        uint32_t their_weak = 0;
        foreign_corpus_dynamic_symbols(ours_image, &our_rows, &our_weak);
        foreign_corpus_dynamic_symbols(&theirs, &their_rows, &their_weak);
        context.documented[FOREIGN_CORPUS_REASON_WEAK_UNDEFINED] += our_weak > their_weak ? our_weak - their_weak : their_weak - our_weak;
        foreign_test_expect(&corpus->tally, foreign_corpus_same_rows(&our_rows, &their_rows, FOREIGN_CORPUS_VERSIONS, &context, arena),
                            "the same dynamic symbols at the same versions", what);
        our_rows.count = 0;
        their_rows.count = 0;
        const uint64_t our_relative = foreign_corpus_relocations(ours_image, &our_rows);
        const uint64_t their_relative = foreign_corpus_relocations(&theirs, &their_rows);
        // Each side's GOT slots of weak undefined names without a version: mold has neither the names nor the slots'
        // relocations.
        const DynamicTestImage* images[2] = { ours_image, &theirs };
        ForeignCorpusRows* sides[2] = { &our_rows, &their_rows };
        for (uint32_t side = 0; side < 2; side += 1)
        {
            uint32_t kept = 0;
            for (uint32_t i = 0; i < sides[side]->count; i += 1)
            {
                Elf64_Sym symbol = { 0 };
                const bool slot = strncmp(sides[side]->rows[i], "slot ", 5) == 0;
                const uint32_t index = slot ? foreign_dynamic_test_symbol(images[side], sides[side]->rows[i] + 5, &symbol) : 0;
                const bool weak = index != 0 && symbol.st_shndx == SHN_UNDEF && ELF64_ST_BIND(symbol.st_info) == STB_WEAK && foreign_corpus_version(images[side], index)[0] == 0;
                if (!weak)
                {
                    memcpy(sides[side]->rows[kept], sides[side]->rows[i], BUSTER_FOREIGN_CORPUS_ROW_BYTES);
                    kept += 1;
                }
            }
            sides[side]->count = kept;
        }
        const uint32_t symbolic_copies = context.documented[FOREIGN_CORPUS_REASON_SYMBOLIC_COPY];
        foreign_test_expect(&corpus->tally, foreign_corpus_same_rows(&our_rows, &their_rows, FOREIGN_CORPUS_RELOCATIONS, &context, arena),
                            "the same dynamic relocations by symbol and class", what);
        // A symbolic R_X86_64_64 to a copy is a relative one in link-foreign's file.
        const uint64_t expected = their_relative + (context.documented[FOREIGN_CORPUS_REASON_SYMBOLIC_COPY] - symbolic_copies);
        if (strstr(linker, "lld") != 0)
        {
            char counts[512];
            snprintf(counts, sizeof(counts), "%s: %" PRIu64 " relative relocations against %" PRIu64, what, our_relative, expected);
            foreign_test_expect(&corpus->tally, our_relative == expected, "as many relative relocations as lld", counts);
        }
        else
        {
            context.documented[FOREIGN_CORPUS_REASON_RELATIVE] += our_relative != expected ? 1 : 0;
        }
        for (uint32_t reason = 1; reason < FOREIGN_CORPUS_REASONS; reason += 1)
        {
            if (context.documented[reason] != 0)
            {
                printf("    %s differs, as documented: %u: %s\n", linker, context.documented[reason], foreign_corpus_reasons[reason]);
            }
        }
    }
    arena_pop_to(arena, mark);
}

static int run_foreign_corpus_tests(int argc, char** argv)
{
    ForeignCorpus* corpus = calloc(1, sizeof(ForeignCorpus));
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    const char* sqlite = 0;
    const char* lua = 0;
    bool keep = false;
    bool valid = corpus != 0;
    const char* compilers = "gcc,clang";
    const char* linkers = "ld.lld,mold";
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            base = argv[++i];
        }
        else if (strcmp(argv[i], "--sqlite") == 0 && has_value)
        {
            sqlite = argv[++i];
        }
        else if (strcmp(argv[i], "--lua") == 0 && has_value)
        {
            lua = argv[++i];
        }
        else if (strcmp(argv[i], "--self") == 0 && has_value)
        {
            corpus->self = argv[++i];
        }
        else if (strcmp(argv[i], "--compilers") == 0 && has_value)
        {
            compilers = argv[++i];
        }
        else if (strcmp(argv[i], "--linkers") == 0 && has_value)
        {
            linkers = argv[++i];
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    // The lists, split at commas in place of copies of them.
    char compiler_list[256];
    char linker_list[256];
    if (valid)
    {
        snprintf(compiler_list, sizeof(compiler_list), "%s", compilers);
        snprintf(linker_list, sizeof(linker_list), "%s", linkers);
        for (char* item = strtok(compiler_list, ","); item != 0 && corpus->compiler_count < BUSTER_ARRAY_LENGTH(corpus->compilers); item = strtok(0, ","))
        {
            corpus->compilers[corpus->compiler_count++] = item;
        }
        for (char* item = strtok(linker_list, ","); item != 0 && corpus->linker_count < BUSTER_ARRAY_LENGTH(corpus->linkers); item = strtok(0, ","))
        {
            corpus->linkers[corpus->linker_count++] = item;
        }
        valid = corpus->compiler_count != 0 && corpus->linker_count != 0;
    }
    Arena arena = arena_reserve(1ull << 34);
    bool passed = valid;
    if (!valid)
    {
        printf("usage: test-foreign-corpus [--sqlite DIR] [--lua DIR] [--self DIR] [--compilers LIST] [--linkers LIST] [--directory PATH] [--keep]\n");
    }
    else
    {
        snprintf(corpus->directory, sizeof(corpus->directory), "%s/buster-corpus-XXXXXX", base);
        passed = arena.base != 0 && mkdtemp(corpus->directory) != 0;
        corpus->arena = &arena;
        corpus->tally = (ForeignTestTally){ .check = "foreign corpus" };
        const long online = sysconf(_SC_NPROCESSORS_ONLN);
        corpus->jobs = online > 0 ? (uint32_t)online : 1;
        foreign_test_system(&corpus->system);
        passed = passed && corpus->system.found;
        foreign_corpus_objects(corpus);
        for (uint32_t f = 0; passed && f < BUSTER_ARRAY_LENGTH(foreign_corpus_fixtures); f += 1)
        {
            char path[BUSTER_FOREIGN_TEST_PATH_BYTES + 64];
            snprintf(path, sizeof(path), "%s/%s", corpus->directory, foreign_corpus_fixtures[f].file);
            passed = foreign_test_write(path, foreign_corpus_fixtures[f].text, strlen(foreign_corpus_fixtures[f].text));
        }
    }
    if (passed)
    {
        ForeignCorpusProgram* programs = calloc(8, sizeof(ForeignCorpusProgram));
        uint32_t count = 0;
        programs[count++] = (ForeignCorpusProgram){
            .name = "c-fixture",
            .sources = { "corpus_main.c", "corpus_counter.c" },
            .archived = { "corpus_names.c", "corpus_scale.c", "corpus_unused.c" },
            .libraries = { "-lm" },
            .runs = {
                { .input = "a line of input\n" },
                { .arguments = { "first", "second" } },
            },
            .library = { "corpus_counter.c", "corpus_names.c", "corpus_scale.c", "corpus_unused.c" },
        };
        programs[count++] = (ForeignCorpusProgram){
            .name = "cxx-fixture",
            .cxx = true,
            .sources = { "corpus_shapes.cpp", "corpus_throw.cpp" },
            .runs = {
                { .arguments = { 0 } },
                { .arguments = { "x", "y" } },
            },
            .library = { "corpus_throw.cpp" },
        };
        if (sqlite != 0)
        {
            programs[count++] = (ForeignCorpusProgram){
                .name = "sqlite",
                .directory = sqlite,
                .sources = { "shell.c", "sqlite3.c" },
                .libraries = { "-lm" },
                .runs = {
                    { .input = "create table t(a integer primary key, b text, c real);\n"
                               "insert into t(b, c) values ('x', 1.5), ('y', 2.25), ('z', 3.125);\n"
                               "select count(*), sum(c), group_concat(b, '-') from t;\n"
                               "select printf('%08.3f', avg(c)), upper(b) from t where c > 2 order by c desc;\n" },
                    { .input = "with recursive n(x) as (select 1 union all select x + 1 from n where x < 1000)\n"
                               "select sum(x), max(x), json_object('a', 1, 'b', json_array(1, 2.5, 'three')) from n;\n"
                               "select json_extract('{\"k\":[1,{\"v\":42}]}', '$.k[1].v'), hex(zeroblob(3)), length(zeroblob(17));\n" },
                    { .input = ".mode csv\nselect 1, 'two', 3.0;\n.mode box\nselect 'boxed' as column_name;\n" },
                    { .input = "select * from missing_table;\n" },
                    { .arguments = { "-version" } },
                },
                .library = { "sqlite3.c" },
            };
        }
        if (lua != 0)
        {
            ForeignCorpusProgram* program = &programs[count++];
            *program = (ForeignCorpusProgram){
                .name = "lua",
                .directory = lua,
                .sources = { "lua.c" },
                .archived = { "lapi.c", "lcode.c", "lctype.c", "ldebug.c", "ldo.c", "ldump.c", "lfunc.c", "lgc.c", "llex.c", "lmem.c", "lobject.c",
                              "lopcodes.c", "lparser.c", "lstate.c", "lstring.c", "ltable.c", "ltm.c", "lundump.c", "lvm.c", "lzio.c", "lauxlib.c",
                              "lbaselib.c", "lcorolib.c", "ldblib.c", "liolib.c", "lmathlib.c", "loadlib.c", "loslib.c", "lstrlib.c", "ltablib.c",
                              "lutf8lib.c", "linit.c" },
                .flags = { "-DLUA_USE_LINUX" },
                .libraries = { "-lm", "-ldl" },
                .runs = {
                    { .arguments = { "-" }, .input = "local t = {} for i = 1, 100 do t[i] = i * i end\n"
                                                     "print(#t, t[100], string.format('%5.2f %x %s', math.pi, 255, tostring(nil)))\n"
                                                     "local co = coroutine.wrap(function(a) local b = coroutine.yield(a + 1) return b * 2 end)\n"
                                                     "print(co(1), co(21), select('#', 1, 2, 3))\n"
                                                     "print(pcall(error, {code = 7}), string.rep('ab', 3, ','), utf8.char(72, 228))\n"
                                                     "io.stderr:write('stderr line\\n') os.exit(5)\n" },
                    { .arguments = { "-e", "print(string.gsub('hello world', 'o', '0'), math.type(1), math.type(1.0), 7 // 2, 7 % -3)" } },
                },
            };
            memcpy(program->library, program->archived, sizeof(program->library));
        }
        if (corpus->self != 0)
        {
            ForeignCorpusProgram* program = &programs[count++];
            *program = (ForeignCorpusProgram){
                .name = "self",
                .directory = corpus->self,
                .only_compiler = "clang",
                .sources = { "src/buster/compiler/main.c", "src/buster/compiler/main_unit_x86.c", "src/buster/compiler/main_unit_frontend.c",
                             "src/buster/compiler/main_unit_assembler.c", "src/buster/compiler/main_unit_link.c", "src/buster/compiler/main_unit_executable.c",
                             "src/buster/compiler/main_unit_dynamic.c", "src/buster/compiler/main_unit_types.c", "src/buster/compiler/main_unit_checker.c",
                             "src/buster/compiler/main_unit_check_builtins.c", "src/buster/compiler/main_unit_backend.c", "src/buster/compiler/main_unit_fragments.c",
                             "src/buster/compiler/c_lexer_benchmark.c", "src/buster/compiler/x86_64_encoder_benchmark.c", "src/buster/compiler/lane_benchmark.c",
                             "src/buster/compiler/dataflow_benchmark.c", "src/buster/compiler/dominators_benchmark.c", "src/buster/compiler/tier1_allocate_benchmark.c",
                             "src/buster/compiler/object_writer_benchmark.c", "src/buster/compiler/executable_benchmark.c", "src/buster/compiler/import_benchmark.c",
                             "src/buster/compiler/branch_sizing_benchmark.c", "src/buster/compiler/bind_benchmark.c", "src/buster/compiler/c_refine_benchmark.c",
                             "src/buster/compiler/c_preprocessor_benchmark.c", "src/buster/compiler/c_files_benchmark.c", "src/buster/compiler/c_parser_benchmark.c",
                             "src/buster/compiler/c_types_benchmark.c", "src/buster/compiler/c_builtin_benchmark.c", "src/buster/compiler/c_checker_benchmark.c",
                             "src/buster/compiler/calling_convention_benchmark.c", "src/buster/compiler/tier1_frame_benchmark.c", "src/buster/compiler/snapshot_benchmark.c",
                             "src/buster/compiler/link_benchmark.c", "src/buster/compiler/foreign_benchmark.c", "src/buster/compiler/floors_benchmark.c",
                             "src/buster/compiler/x86_64_disassembler_benchmark.c", "src/buster/compiler/x86_semantics_benchmark.c", "src/buster/compiler/tier0_benchmark.c",
                             "src/buster/compiler/tier1_schedule_benchmark.c", "src/buster/compiler/tier1_select_benchmark.c", "src/buster/compiler/tier1_model_measure.c" },
                .libraries = { "-lm", "-ldl" },
                .runs = {
                    { .arguments = { "test-lexer", "--fuzz", "2000" } },
                    { .arguments = { "test-x86-encoder", "--count", "3000" } },
                },
            };
            char* include = arena_push(&arena, BUSTER_FOREIGN_TEST_PATH_BYTES + 8, 1);
            snprintf(include, BUSTER_FOREIGN_TEST_PATH_BYTES + 8, "-I%s/src", corpus->self);
            const char* flags[] = { "-march=native", include, "-fwrapv", "-funsigned-char", "-fno-strict-aliasing" };
            memcpy(program->flags, flags, sizeof(flags));
        }
        for (uint32_t p = 0; p < count; p += 1)
        {
            for (uint32_t c = 0; c < corpus->compiler_count; c += 1)
            {
                if (programs[p].only_compiler == 0 || strcmp(programs[p].only_compiler, corpus->compilers[c]) == 0)
                {
                    foreign_corpus_program(corpus, &programs[p], corpus->compilers[c]);
                }
            }
        }
        passed = foreign_test_report(&corpus->tally);
        free(programs);
    }
    if (corpus != 0 && corpus->directory[0] != 0 && !keep && passed)
    {
        char* remove[] = { (char*)"rm", (char*)"-rf", corpus->directory, 0 };
        object_test_run(remove, "/dev/null", "/dev/null");
    }
    else if (corpus != 0 && corpus->directory[0] != 0)
    {
        printf("test-foreign-corpus: the files are in %s\n", corpus->directory);
    }
    arena_release(&arena);
    free(corpus);
    return passed ? 0 : 1;
}
