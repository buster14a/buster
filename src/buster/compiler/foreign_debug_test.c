// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Foreign DWARF through link-foreign (#395, foreign_debug.c), in test-foreign. Original C and C++ fixtures are
// compiled with -g by gcc, whose GNU as compresses the debug sections with zlib by default, and by clang, which
// doesn't, at -O0 and -O2, in DWARF 5 and 4, and with -gz=zlib and -gz=none, and each is linked as a PIE by
// link-foreign on 1 lane, on 8 and by the exact path (--scalar): the same bytes, which run and print what the program
// computes. The C++ fixture's inline functions are COMDAT groups in both units, whose losing copies' DWARF stays with
// tombstones; a link with --gc-sections keeps the DWARF of what it collects the same way; -S leaves the debug sections
// out. A section of another compression (objcopy's zstd, where binutils has it) and one whose zlib stream is damaged
// are refused, naming their object and section. Excluded debug sections (SHF_EXCLUDE) are left out, as GNU ld and lld
// leave them out, and empty ones and empty relocation sections link, also where no other section of their kind has bytes
// and the link has no debug relocation (foreign_debug_test_sections). A thread-local offset (R_X86_64_DTPOFF32) is a
// signed 32-bit field and an R_X86_64_32 an unsigned one, by the writer's and the immediate paths, each refused past its
// range (foreign_debug_test_offsets).
// The relocation kernels' vector path is checked against its exact twin on each debug relocation section, ending at a
// PROT_NONE page, with each rare relocation at each of a batch's lanes (foreign_debug_test_kernels).
// With --cross-check, ld.lld links the same objects: llvm-dwarfdump --verify finds no error in either output, their
// --statistics agree but for .debug_str's and .debug_line_str's sizes (lld merges their strings), and gdb, stopped in a
// function of each, gives the same backtrace, arguments and locals but for addresses.

#define BUSTER_FOREIGN_DEBUG_TEST_STATISTICS 192
// The most relocations a section of the kernels' fixture has (foreign_debug_test_kernels).
#define BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS 16384

typedef struct ForeignDebugTestSource ForeignDebugTestSource;
struct ForeignDebugTestSource
{
    const char* name;
    const char* text;
};

static const ForeignDebugTestSource foreign_debug_test_sources[] = {
    {
        .name = "dbg_shape.h",
        .text = "struct dbg_shape { int width; int height; };\nstatic inline int dbg_square(int x) { return x * x; }\n",
    },
    {
        .name = "dbg_main.c",
        .text = "#include <stdio.h>\n#include \"dbg_shape.h\"\n"
                "int dbg_sum(const struct dbg_shape* shapes, int count);\n"
                "__thread int dbg_tls = 7;\n"
                "static int dbg_twice(int x) { return 2 * x; }\n"
                "int main(int argc, char** argv)\n{\n"
                "    struct dbg_shape shapes[3] = { { 1, 2 }, { 3, 4 }, { 5, argc } };\n"
                "    int total = dbg_sum(shapes, 3) + dbg_twice(argc) + dbg_tls + dbg_square(argc);\n"
                "    printf(\"total %d %s\\n\", total, argc > 1 ? argv[1] : \"-\");\n"
                "    return 0;\n}\n",
    },
    {
        .name = "dbg_sum.c",
        .text = "#include \"dbg_shape.h\"\n"
                "int dbg_sum(const struct dbg_shape* shapes, int count)\n{\n"
                "    int total = 0;\n"
                "    for (int i = 0; i < count; i += 1)\n    {\n"
                "        int area = shapes[i].width * shapes[i].height;\n"
                "        total += area + dbg_square(i);\n"
                "    }\n"
                "    return total;\n}\n"
                "int dbg_unused(int x) { return dbg_square(x) + 1; }\n",
    },
    {
        .name = "dbg_a.cpp",
        .text = "#include <cstdio>\n"
                "template <typename T> T dbg_max(T a, T b) { return a > b ? a : b; }\n"
                "inline int dbg_clamp(int x) { return dbg_max(x, 0); }\n"
                "int dbg_other(int x);\n"
                "int main(int argc, char**) { std::printf(\"max %d %d\\n\", dbg_clamp(argc - 3), dbg_other(argc)); return 0; }\n",
    },
    {
        .name = "dbg_b.cpp",
        .text = "template <typename T> T dbg_max(T a, T b) { return a > b ? a : b; }\n"
                "inline int dbg_clamp(int x) { return dbg_max(x, 0); }\n"
                "int dbg_other(int x) { return dbg_clamp(x + 4) + dbg_max(x, 2); }\n",
    },
};

typedef struct ForeignDebugTestVariant ForeignDebugTestVariant;
struct ForeignDebugTestVariant
{
    const char* name;
    const char* compiler;
    const char* flags[4];
    bool cxx;
    // Linked with --gc-sections, which collects dbg_unused.
    bool gc;
};

static const ForeignDebugTestVariant foreign_debug_test_variants[] = {
    { .name = "gcc-O0", .compiler = "gcc", .flags = { "-g", "-O0" } },
    { .name = "gcc-O2", .compiler = "gcc", .flags = { "-g", "-O2" } },
    { .name = "gcc-dwarf4", .compiler = "gcc", .flags = { "-g", "-O2", "-gdwarf-4" } },
    { .name = "gcc-uncompressed", .compiler = "gcc", .flags = { "-g", "-O2", "-gz=none" } },
    { .name = "gcc-gc", .compiler = "gcc", .flags = { "-g", "-O2", "-ffunction-sections" }, .gc = true },
    { .name = "clang-O2", .compiler = "clang", .flags = { "-g", "-O2" } },
    { .name = "clang-zlib", .compiler = "clang", .flags = { "-g", "-O2", "-gz=zlib" } },
    { .name = "clang-dwarf4", .compiler = "clang", .flags = { "-g", "-O0", "-gdwarf-4" } },
    { .name = "gxx-O0", .compiler = "g++", .flags = { "-g", "-O0" }, .cxx = true },
    { .name = "clangxx-O0", .compiler = "clang++", .flags = { "-g", "-O0" }, .cxx = true },
};

// A program's output.
static const char* const foreign_debug_test_expected[2] = { "total 34 -\n", "max 0 7\n" };

// Where gdb stops in a variant's program, so that every local it prints holds what the program put there: at
// dbg_other in the C++ program, which has none; at dbg_sum in an optimized C one, whose locations give the values; and
// in an -O0 one at dbg_sum's first add to its total (at the function's first line, its `total` was the stack's leftover
// bytes, which differ between two executables whose startups leave different ones). An optimized build's line 8 shares
// its address with line 7, and gdb moves a breakpoint there to a line the run doesn't reach.
static const char* foreign_debug_test_stop(const ForeignDebugTestVariant* variant)
{
    bool unoptimized = false;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(variant->flags) && variant->flags[f] != 0; f += 1)
    {
        unoptimized = unoptimized || strcmp(variant->flags[f], "-O0") == 0;
    }
    return variant->cxx ? "dbg_other" : unoptimized ? "dbg_sum.c:8" : "dbg_sum";
}

// The pairs "key": value of llvm-dwarfdump --statistics' JSON, as "key=value" rows, sorted, but the file's name and the
// sizes of the sections whose strings lld merges.
typedef struct ForeignDebugTestStatistics ForeignDebugTestStatistics;
struct ForeignDebugTestStatistics
{
    char rows[BUSTER_FOREIGN_DEBUG_TEST_STATISTICS][192];
    uint32_t count;
};

static int foreign_debug_test_compare_rows(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

static void foreign_debug_test_statistics(String8 json, ForeignDebugTestStatistics* statistics)
{
    statistics->count = 0;
    const char* at = json.pointer;
    const char* end = json.pointer != 0 ? json.pointer + json.length : 0;
    while (at != 0 && at < end && statistics->count < BUSTER_FOREIGN_DEBUG_TEST_STATISTICS)
    {
        const char* key = memchr(at, '"', (uint64_t)(end - at));
        const char* key_end = key != 0 ? memchr(key + 1, '"', (uint64_t)(end - key - 1)) : 0;
        const char* value = key_end != 0 ? key_end + 1 : 0;
        while (value != 0 && value < end && (*value == ':' || *value == ' '))
        {
            value += 1;
        }
        const char* value_end = value;
        while (value_end != 0 && value_end < end && *value_end != ',' && *value_end != '}' && *value_end != '\n')
        {
            value_end += 1;
        }
        if (key_end != 0)
        {
            const int key_length = (int)(key_end - key - 1);
            const bool skipped = (key_length == 4 && memcmp(key + 1, "file", 4) == 0) || (key_length >= 9 && memcmp(key + 1, "#bytes in", 9) == 0 && memmem(key + 1, (uint64_t)key_length, "_str", 4) != 0);
            if (!skipped)
            {
                snprintf(statistics->rows[statistics->count], sizeof(statistics->rows[0]), "%.*s=%.*s", key_length, key + 1, (int)(value_end - value), value);
                statistics->count += 1;
            }
        }
        at = value_end != 0 && value_end < end ? value_end + 1 : end;
    }
    qsort(statistics->rows, statistics->count, sizeof(statistics->rows[0]), foreign_debug_test_compare_rows);
}

// gdb's transcript without its addresses (each 0x and the hex digits after it) and process numbers.
static void foreign_debug_test_normalize(String8 text, char* normalized, uint64_t capacity)
{
    uint64_t used = 0;
    for (uint64_t i = 0; text.pointer != 0 && i < text.length && used + 1 < capacity; i += 1)
    {
        const bool hex = text.pointer[i] == '0' && i + 1 < text.length && text.pointer[i + 1] == 'x';
        const bool process = i + 8 <= text.length && memcmp(text.pointer + i, "process ", 8) == 0;
        if (hex || process)
        {
            i += hex ? 2 : 8;
            while (i < text.length && ((text.pointer[i] >= '0' && text.pointer[i] <= '9') || (text.pointer[i] >= 'a' && text.pointer[i] <= 'f')))
            {
                i += 1;
            }
            i -= 1;
            normalized[used] = '#';
            used += 1;
        }
        else
        {
            normalized[used] = text.pointer[i];
            used += 1;
        }
    }
    normalized[used] = 0;
}

// The sizes of an output's debug sections, as "name size" lines in the names' order (linkers order them their own
// ways), but .debug_str's and .debug_line_str's.
static void foreign_debug_test_sizes(String8 file, char* sizes, uint64_t capacity, bool* any)
{
    const DynamicTestImage image = dynamic_test_image(file);
    char lines[32][96];
    uint32_t count = 0;
    *any = false;
    for (uint32_t i = 1; image.valid && i < image.header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image.section_headers[i];
        const char* name = image.names + section->sh_name;
        const bool debug = strncmp(name, ".debug_", 7) == 0;
        *any = *any || debug;
        if (debug && strcmp(name, ".debug_str") != 0 && strcmp(name, ".debug_line_str") != 0 && count < BUSTER_ARRAY_LENGTH(lines))
        {
            snprintf(lines[count], sizeof(lines[0]), "%s %llu\n", name, (unsigned long long)section->sh_size);
            count += 1;
        }
    }
    qsort(lines, count, sizeof(lines[0]), foreign_debug_test_compare_rows);
    uint64_t used = 0;
    sizes[0] = 0;
    for (uint32_t i = 0; i < count && used < capacity; i += 1)
    {
        used += (uint64_t)snprintf(sizes + used, capacity - used, "%s", lines[i]);
    }
}

// The lines of llvm-dwarfdump --verify's report that count its errors, or "none" when it found none; 0 for a report that
// isn't one.
static const char* foreign_debug_test_verdict(String8 report, char* verdict, uint64_t capacity)
{
    const char* result = 0;
    if (report.pointer != 0 && strstr(report.pointer, "No errors.") != 0)
    {
        result = "none";
    }
    else if (report.pointer != 0 && strstr(report.pointer, "Errors detected.") != 0)
    {
        uint64_t used = 0;
        verdict[0] = 0;
        for (const char* line = strstr(report.pointer, "error: "); line != 0 && used < capacity; line = strstr(line + 1, "error: "))
        {
            const char* end = strchr(line, '\n');
            const uint64_t length = end != 0 ? (uint64_t)(end - line) : strlen(line);
            const bool counted = memmem(line, length, "occurred", 8) != 0;
            used += counted ? (uint64_t)snprintf(verdict + used, capacity - used, "%.*s\n", (int)length, line) : 0;
        }
        result = verdict;
    }
    return result;
}

static void foreign_debug_test_variant(ForeignTestTally* tally, const char* directory, const ForeignDebugTestVariant* variant, ForeignTestSystem* system, bool cross_check,
                                       Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    bool ready = line != 0;
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        foreign_test_pie_begin(line, system);
    }
    // The C program's units, or the C++ one's.
    const uint32_t first = variant->cxx ? 3 : 1;
    for (uint32_t s = first; ready && s < first + 2; s += 1)
    {
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/%s", directory, foreign_debug_test_sources[s].name);
        snprintf(object, sizeof(object), "%s/%s-%s.o", directory, variant->name, foreign_debug_test_sources[s].name);
        snprintf(errors, sizeof(errors), "%s.err", object);
        char include[BUSTER_FOREIGN_TEST_PATH_BYTES + 2];
        snprintf(include, sizeof(include), "-I%s", directory);
        char* arguments[16] = { (char*)variant->compiler, (char*)"-c" };
        uint32_t count = 2;
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(variant->flags) && variant->flags[f] != 0; f += 1)
        {
            arguments[count] = (char*)variant->flags[f];
            count += 1;
        }
        arguments[count] = include;
        arguments[count + 1] = source;
        arguments[count + 2] = (char*)"-o";
        arguments[count + 3] = object;
        ready = object_test_run(arguments, "/dev/null", errors) == 0;
        foreign_test_expect(tally, ready, "compile a -g fixture", object);
        char name[256];
        snprintf(name, sizeof(name), "%s-%s.o", variant->name, foreign_debug_test_sources[s].name);
        foreign_test_add(line, foreign_test_path(directory, name, arena), true, true);
    }
    if (ready && variant->cxx)
    {
        foreign_test_add(line, (char*)"-lstdc++", true, true);
        foreign_test_add(line, (char*)"-lm", true, true);
    }
    if (ready)
    {
        foreign_test_pie_end(line, system);
    }
    // link-foreign on 1 lane, on 8, by the exact path, and with -S.
    static const char* const ways[4] = { "--lanes=1", "--lanes=8", "--scalar", "-S" };
    String8 files[4] = { 0 };
    for (uint32_t way = 0; ready && way < 4; way += 1)
    {
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(output, sizeof(output), "%s/%s.out%u", directory, variant->name, way);
        snprintf(errors, sizeof(errors), "%s.err", output);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 16);
        uint32_t count = 0;
        static const char* const head[] = { "/proc/self/exe", "link-foreign", "-pie", "--dynamic-linker", "/lib64/ld-linux-x86-64.so.2" };
        for (uint32_t i = 0; arguments != 0 && i < BUSTER_ARRAY_LENGTH(head); i += 1)
        {
            arguments[count] = (char*)head[i];
            count += 1;
        }
        if (arguments != 0)
        {
            arguments[count] = (char*)ways[way];
            arguments[count + 1] = variant->gc ? (char*)"--gc-sections" : (char*)"--no-gc-sections";
            arguments[count + 2] = (char*)"-o";
            arguments[count + 3] = output;
            count += 4;
            for (uint32_t i = 0; i < line->our_count; i += 1)
            {
                arguments[count] = line->ours[i];
                count += 1;
            }
            arguments[count] = 0;
        }
        const bool linked = arguments != 0 && object_test_run(arguments, "/dev/null", errors) == 0;
        foreign_test_expect(tally, linked, "link-foreign links the -g objects", errors);
        files[way] = linked ? foreign_test_read(arena, output) : (String8){ 0 };
        if (linked)
        {
            ForeignTestPath run_output;
            snprintf(run_output, sizeof(run_output), "%s.run", output);
            char* run[] = { output, 0 };
            const bool ran = object_test_run(run, run_output, "/dev/null") == 0;
            const String8 printed = foreign_test_read(arena, run_output);
            const char* expected = foreign_debug_test_expected[variant->cxx ? 1 : 0];
            foreign_test_expect(tally, ran && printed.pointer != 0 && strcmp(printed.pointer, expected) == 0, "the program runs and prints what it computes", output);
        }
        bool any = false;
        char sizes[4096];
        foreign_debug_test_sizes(files[way], sizes, sizeof(sizes), &any);
        foreign_test_expect(tally, files[way].pointer == 0 || any == (way != 3), "the debug sections are written, and left out with -S", output);
    }
    for (uint32_t way = 1; ready && way < 3; way += 1)
    {
        foreign_test_expect(tally, files[0].pointer != 0 && files[way].pointer != 0 && files[0].length == files[way].length && memcmp(files[0].pointer, files[way].pointer, files[0].length) == 0,
                            "the same bytes on 8 lanes and by the exact path as on 1", variant->name);
    }
    if (ready && cross_check && files[0].pointer != 0)
    {
        ForeignTestPath ours;
        ForeignTestPath theirs;
        ForeignTestPath errors;
        snprintf(ours, sizeof(ours), "%s/%s.out0", directory, variant->name);
        snprintf(theirs, sizeof(theirs), "%s/%s.lld", directory, variant->name);
        snprintf(errors, sizeof(errors), "%s.err", theirs);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 16);
        uint32_t count = 0;
        static const char* const head[] = { "ld.lld", "--hash-style=gnu", "--build-id", "--eh-frame-hdr", "-z", "now", "-z", "relro" };
        for (uint32_t i = 0; arguments != 0 && i < BUSTER_ARRAY_LENGTH(head); i += 1)
        {
            arguments[count] = (char*)head[i];
            count += 1;
        }
        if (arguments != 0)
        {
            arguments[count] = variant->gc ? (char*)"--gc-sections" : (char*)"--no-gc-sections";
            arguments[count + 1] = (char*)"-o";
            arguments[count + 2] = theirs;
            count += 3;
            for (uint32_t i = 0; i < line->their_count; i += 1)
            {
                arguments[count] = line->theirs[i];
                count += 1;
            }
            arguments[count] = 0;
        }
        const bool linked = arguments != 0 && object_test_run(arguments, "/dev/null", errors) == 0;
        foreign_test_expect(tally, linked, "ld.lld links the same objects", errors);
        const char* outputs[2] = { ours, theirs };
        ForeignDebugTestStatistics* statistics = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignDebugTestStatistics, 2);
        char* transcripts[2] = { arena_push(arena, 1u << 16, 1), arena_push(arena, 1u << 16, 1) };
        char* sizes[2] = { arena_push(arena, 4096, 1), arena_push(arena, 4096, 1) };
        // llvm-dwarfdump --verify's errors, which must be lld's output's: none, or (clang++'s tombstoned COMDAT copies'
        // ranges at address 0) the same.
        const char* verdicts[2] = { 0 };
        char verdict_lines[2][1024];
        for (uint32_t side = 0; linked && statistics != 0 && transcripts[1] != 0 && sizes[1] != 0 && side < 2; side += 1)
        {
            ForeignTestPath report;
            snprintf(report, sizeof(report), "%s.verify", outputs[side]);
            char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", (char*)outputs[side], 0 };
            object_test_run(verify, report, "/dev/null");
            verdicts[side] = foreign_debug_test_verdict(foreign_test_read(arena, report), verdict_lines[side], sizeof(verdict_lines[side]));
            snprintf(report, sizeof(report), "%s.statistics", outputs[side]);
            char* summarize[] = { (char*)"llvm-dwarfdump", (char*)"--statistics", (char*)outputs[side], 0 };
            const bool summarized = object_test_run(summarize, report, "/dev/null") == 0;
            foreign_test_expect(tally, summarized, "llvm-dwarfdump --statistics", report);
            foreign_debug_test_statistics(foreign_test_read(arena, report), &statistics[side]);
            snprintf(report, sizeof(report), "%s.gdb", outputs[side]);
            char stop[64];
            snprintf(stop, sizeof(stop), "break %s", foreign_debug_test_stop(variant));
            char* gdb[] = { (char*)"gdb", (char*)"-nx", (char*)"-batch", (char*)"-ex", (char*)"set debuginfod enabled off", (char*)"-ex", stop, (char*)"-ex", (char*)"run",
                            (char*)"-ex", (char*)"bt", (char*)"-ex", (char*)"info args", (char*)"-ex", (char*)"info locals", (char*)outputs[side], 0 };
            const bool debugged = object_test_run(gdb, report, "/dev/null") == 0;
            foreign_test_expect(tally, debugged, "gdb runs the program to a breakpoint", report);
            foreign_debug_test_normalize(foreign_test_read(arena, report), transcripts[side], 1u << 16);
            bool any = false;
            foreign_debug_test_sizes(foreign_test_read(arena, outputs[side]), sizes[side], 4096, &any);
        }
        if (linked && statistics != 0 && transcripts[1] != 0 && sizes[1] != 0)
        {
            bool same = statistics[0].count == statistics[1].count && statistics[0].count != 0;
            for (uint32_t i = 0; same && i < statistics[0].count; i += 1)
            {
                same = strcmp(statistics[0].rows[i], statistics[1].rows[i]) == 0;
            }
            foreign_test_expect(tally, same, "llvm-dwarfdump --statistics agrees with lld's output's", variant->name);
            foreign_test_expect(tally, verdicts[0] != 0 && verdicts[1] != 0 && strcmp(verdicts[0], verdicts[1]) == 0, "llvm-dwarfdump --verify finds lld's output's errors, or none", variant->name);
            // Everything gdb says but the executable's own name.
            const char* ours_name = strrchr(ours, '/');
            const char* theirs_name = strrchr(theirs, '/');
            char* breakpoint = strstr(transcripts[0], "Breakpoint 1, ");
            char* lld_breakpoint = strstr(transcripts[1], "Breakpoint 1, ");
            foreign_test_expect(tally, breakpoint != 0 && lld_breakpoint != 0 && strcmp(breakpoint, lld_breakpoint) == 0 && ours_name != 0 && theirs_name != 0,
                                "gdb's backtrace, arguments and locals as in lld's output", variant->name);
            foreign_test_expect(tally, strcmp(sizes[0], sizes[1]) == 0, "the debug sections of lld's output's sizes, but its merged strings'", variant->name);
        }
    }
    arena_pop_to(arena, mark);
}

// A compressed debug section that a link refuses: objcopy's zstd, where binutils has it, and a zlib stream with a byte
// changed in its middle.
static void foreign_debug_test_refusals(ForeignTestTally* tally, const char* directory, ForeignTestSystem* system, Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignTestPath object;
    ForeignTestPath zstd;
    ForeignTestPath damaged;
    snprintf(object, sizeof(object), "%s/gcc-O2-dbg_sum.c.o", directory);
    snprintf(zstd, sizeof(zstd), "%s/dbg_sum-zstd.o", directory);
    snprintf(damaged, sizeof(damaged), "%s/dbg_sum-damaged.o", directory);
    char* compress[] = { (char*)"objcopy", (char*)"--compress-debug-sections=zstd", object, zstd, 0 };
    const bool zstd_made = object_test_run(compress, "/dev/null", "/dev/null") == 0;
    // The damaged copy: .debug_info's stream with its middle byte's bits flipped.
    const String8 bytes = foreign_test_read(arena, object);
    ImportElf elf;
    bool made = bytes.pointer != 0 && import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE;
    bool damaged_made = false;
    for (uint32_t section = 1; made && section < elf.section_count && !damaged_made; section += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, section);
        const char* name = header.sh_name < elf.section_names.length ? elf.section_names.pointer + header.sh_name : "";
        if (strcmp(name, ".debug_info") == 0 && (header.sh_flags & SHF_COMPRESSED) != 0 && header.sh_size > sizeof(Elf64_Chdr) + 16)
        {
            char* copy = arena_push(arena, bytes.length, 1);
            if (copy != 0)
            {
                memcpy(copy, bytes.pointer, bytes.length);
                copy[header.sh_offset + sizeof(Elf64_Chdr) + (header.sh_size - sizeof(Elf64_Chdr)) / 2] ^= (char)0xff;
                damaged_made = foreign_test_write(damaged, copy, bytes.length);
            }
        }
    }
    foreign_test_expect(tally, damaged_made, "damage a zlib-compressed .debug_info", object);
    const char* inputs[2] = { zstd, damaged };
    const char* messages[2] = { "compressed otherwise than with zlib", "doesn't decompress to its size" };
    const bool present[2] = { zstd_made, damaged_made };
    for (uint32_t c = 0; c < 2; c += 1)
    {
        if (present[c])
        {
            ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
            ForeignTestPath output;
            ForeignTestPath errors;
            snprintf(output, sizeof(output), "%s/refused%u", directory, c);
            snprintf(errors, sizeof(errors), "%s.err", output);
            char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 16);
            uint32_t count = 0;
            if (line != 0 && arguments != 0)
            {
                *line = (ForeignTestLine){ 0 };
                foreign_test_pie_begin(line, system);
                foreign_test_add(line, foreign_test_path(directory, "gcc-O2-dbg_main.c.o", arena), true, true);
                foreign_test_add(line, (char*)inputs[c], true, true);
                foreign_test_pie_end(line, system);
                static const char* const head[] = { "/proc/self/exe", "link-foreign", "-pie", "--dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "-o" };
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
                {
                    arguments[count] = (char*)head[i];
                    count += 1;
                }
                arguments[count] = output;
                count += 1;
                for (uint32_t i = 0; i < line->our_count; i += 1)
                {
                    arguments[count] = line->ours[i];
                    count += 1;
                }
                arguments[count] = 0;
            }
            const int status = arguments != 0 ? object_test_run(arguments, "/dev/null", errors) : 0;
            const String8 diagnostic = foreign_test_read(arena, errors);
            const char* file = strrchr(inputs[c], '/');
            const bool named = diagnostic.pointer != 0 && strstr(diagnostic.pointer, messages[c]) != 0 && file != 0 && strstr(diagnostic.pointer, file + 1) != 0 &&
                               strstr(diagnostic.pointer, "(section ") != 0;
            foreign_test_expect(tally, status > 0 && named, "a compressed debug section that doesn't decompress refused, naming its object and section", errors);
        }
    }
    if (!zstd_made)
    {
        printf("  (objcopy makes no zstd-compressed section here: that refusal isn't checked)\n");
    }
    arena_pop_to(arena, mark);
}

// An assembly fixture of debug sections a link leaves out or that are empty, beside the C program: an excluded (SHF_EXCLUDE,
// "e") .debug_str and .debug_ranges that a kept .debug_info and .debug_loc refer to, which lld gives its tombstones (0,
// and 1 in .debug_loc); two .debug_macro sections (GNU as's `unique`), the second empty, whose place .debug_info refers
// to (8, after the first); and a .debug_aranges that holds a function's address. Each contribution starts with a marker.
static const char foreign_debug_test_sections_source[] =
    "    .text\n"
    "    .globl dbg_sections_marker\n"
    "    .type dbg_sections_marker, @function\n"
    "dbg_sections_marker:\n"
    "    ret\n"
    "    .size dbg_sections_marker, .-dbg_sections_marker\n"
    "    .section .debug_str,\"MSe\",@progbits,1\n"
    ".Lexcluded_string:\n"
    "    .asciz \"EXCLUDED_DEBUG_STRING\"\n"
    "    .section .debug_ranges,\"e\",@progbits\n"
    ".Lexcluded_ranges:\n"
    "    .quad 0x7777777777777777\n"
    "    .section .debug_macro,\"\",@progbits,unique,1\n"
    "    .quad 0x5555555555555555\n"
    "    .section .debug_macro,\"\",@progbits,unique,2\n"
    ".Lempty:\n"
    "    .section .debug_info,\"\",@progbits\n"
    "    .long 0x11111111\n"
    "    .long .Lexcluded_string\n"
    "    .quad .Lexcluded_ranges\n"
    "    .long .Lempty\n"
    "    .long 0x22222222\n"
    "    .section .debug_loc,\"\",@progbits\n"
    "    .quad 0x3333333333333333\n"
    "    .quad .Lexcluded_ranges\n"
    "    .section .debug_aranges,\"\",@progbits\n"
    "    .quad 0x4444444444444444\n"
    "    .quad dbg_sections_marker\n";

// The empty sections' fixture for a link with no other debug section of their kinds and no debug relocation (with a
// main compiled without -g): an empty .debug_names, the only contribution of its kind, and a .debug_pubtypes whose
// relocation section the test empties.
static const char foreign_debug_test_empty_source[] =
    "    .text\n"
    "    .globl dbg_empty_marker\n"
    "    .type dbg_empty_marker, @function\n"
    "dbg_empty_marker:\n"
    "    ret\n"
    "    .size dbg_empty_marker, .-dbg_empty_marker\n"
    "    .section .debug_names,\"\",@progbits\n"
    "    .section .debug_pubtypes,\"\",@progbits\n"
    "    .quad 0x6666666666666666\n"
    "    .quad dbg_empty_marker\n";

// An output's bytes of section `name` from its marker on (the marker's `marker_length` bytes, little-endian), `length`
// of them, or none.
static const uint8_t* foreign_debug_test_marked(String8 file, const char* name, uint64_t marker, uint32_t marker_length, uint64_t length)
{
    const DynamicTestImage image = dynamic_test_image(file);
    const Elf64_Shdr* section = dynamic_test_section(&image, name);
    const uint8_t* result = 0;
    for (uint64_t at = 0; section != 0 && section->sh_type == SHT_PROGBITS && result == 0 && at + length <= section->sh_size; at += 1)
    {
        const uint8_t* place = (const uint8_t*)file.pointer + section->sh_offset + at;
        result = memcmp(place, &marker, marker_length) == 0 ? place : 0;
    }
    return result;
}

// The value of an output's regular symbol `name`, or UINT64_MAX.
static uint64_t foreign_debug_test_symbol(String8 file, const char* name)
{
    const DynamicTestImage image = dynamic_test_image(file);
    const Elf64_Shdr* table = dynamic_test_section(&image, ".symtab");
    const bool linked = table != 0 && table->sh_link < image.header->e_shnum && image.section_headers[table->sh_link].sh_offset + image.section_headers[table->sh_link].sh_size <= file.length;
    uint64_t result = UINT64_MAX;
    for (uint64_t i = 1; linked && result == UINT64_MAX && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, file.pointer + table->sh_offset + i * sizeof(Elf64_Sym), sizeof(symbol));
        const Elf64_Shdr* strings = &image.section_headers[table->sh_link];
        const bool named = symbol.st_name < strings->sh_size && strcmp(file.pointer + strings->sh_offset + symbol.st_name, name) == 0;
        result = named ? symbol.st_value : UINT64_MAX;
    }
    return result;
}

// What an output must hold, by fixture (foreign_debug_test_sections): the sections fixture's, no excluded byte,
// .debug_info's references into the excluded sections tombstones and the one into the empty .debug_macro its place, 8,
// .debug_loc's tombstone 1 and .debug_aranges' address dbg_sections_marker's (0 in the copy whose relocation section was
// emptied); the empty fixture's, no .debug_names and .debug_pubtypes' field 0 (its relocation section emptied).
static bool foreign_debug_test_sections_hold(String8 file, uint32_t fixture)
{
    static const char excluded[] = "EXCLUDED_DEBUG_STRING";
    const uint64_t ranges = UINT64_C(0x7777777777777777);
    bool result = file.pointer != 0;
    if (result && fixture < 2)
    {
        result = memmem(file.pointer, file.length, excluded, sizeof(excluded) - 1) == 0 && memmem(file.pointer, file.length, &ranges, 8) == 0;
        const uint8_t* info = foreign_debug_test_marked(file, ".debug_info", 0x11111111, 4, 24);
        const uint8_t* loc = foreign_debug_test_marked(file, ".debug_loc", UINT64_C(0x3333333333333333), 8, 16);
        const uint8_t* aranges = foreign_debug_test_marked(file, ".debug_aranges", UINT64_C(0x4444444444444444), 8, 16);
        uint32_t string = 1;
        uint64_t range = 1;
        uint32_t empty = 0;
        uint32_t end = 0;
        uint64_t tombstone = 0;
        uint64_t address = 1;
        if (info != 0 && loc != 0 && aranges != 0)
        {
            memcpy(&string, info + 4, 4);
            memcpy(&range, info + 8, 8);
            memcpy(&empty, info + 16, 4);
            memcpy(&end, info + 20, 4);
            memcpy(&tombstone, loc + 8, 8);
            memcpy(&address, aranges + 8, 8);
        }
        const uint64_t function = fixture == 1 ? 0 : foreign_debug_test_symbol(file, "dbg_sections_marker");
        result = result && info != 0 && loc != 0 && aranges != 0 && string == 0 && range == 0 && empty == 8 && end == 0x22222222 && tombstone == 1 && address == function;
    }
    else if (result)
    {
        const DynamicTestImage image = dynamic_test_image(file);
        const Elf64_Shdr* names = dynamic_test_section(&image, ".debug_names");
        const uint8_t* pubtypes = foreign_debug_test_marked(file, ".debug_pubtypes", UINT64_C(0x6666666666666666), 8, 16);
        uint64_t field = 1;
        if (pubtypes != 0)
        {
            memcpy(&field, pubtypes + 8, 8);
        }
        result = image.valid && (names == 0 || names->sh_size == 0) && pubtypes != 0 && field == 0;
    }
    return result;
}

// An object's copy whose relocation section `relocation_name` has size 0: an empty relocation section, whose fields
// keep their bytes (a RELA field's are 0).
static bool foreign_debug_test_empty_relocations(const char* object, const char* copy_path, const char* relocation_name, Arena* arena)
{
    const String8 bytes = foreign_test_read(arena, object);
    ImportElf elf = { 0 };
    bool result = bytes.pointer != 0 && import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE;
    uint32_t relocation = 0;
    for (uint32_t section = 1; result && section < elf.section_count; section += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, section);
        const char* name = header.sh_name < elf.section_names.length ? elf.section_names.pointer + header.sh_name : "";
        relocation = strcmp(name, relocation_name) == 0 ? section : relocation;
    }
    char* copy = result && relocation != 0 ? arena_push(arena, bytes.length, 8) : 0;
    result = copy != 0;
    if (result)
    {
        const uint64_t size = 0;
        memcpy(copy, bytes.pointer, bytes.length);
        memcpy(copy + elf.section_offset + (uint64_t)relocation * sizeof(Elf64_Shdr) + offsetof(Elf64_Shdr, sh_size), &size, sizeof(size));
        result = foreign_test_write(copy_path, copy, bytes.length);
    }
    return result;
}

// Debug sections a link leaves out or that are empty: the sections fixture (foreign_debug_test_sections_source) with the
// C program's -O2 objects, its copy whose .rela.debug_aranges is empty, and the empty fixture's copy whose
// .rela.debug_pubtypes is empty with a main compiled without -g (so that no section of .debug_names' kind has bytes and
// the link has no debug relocation). Each is linked on 1 lane, on 8 and by the exact path: the same bytes, which run
// (and print what the C program computes), holding what foreign_debug_test_sections_hold checks. With --cross-check, so
// does lld's output of the same objects.
static void foreign_debug_test_sections(ForeignTestTally* tally, const char* directory, ForeignTestSystem* system, bool cross_check, Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignTestPath sources[2];
    ForeignTestPath assembled[2];
    ForeignTestPath objects[3];
    ForeignTestPath plain_source;
    ForeignTestPath plain;
    ForeignTestPath errors;
    snprintf(sources[0], sizeof(sources[0]), "%s/dbg_sections.s", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/dbg_empty.s", directory);
    snprintf(assembled[0], sizeof(assembled[0]), "%s/dbg_sections.o", directory);
    snprintf(assembled[1], sizeof(assembled[1]), "%s/dbg_empty-full.o", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s", assembled[0]);
    snprintf(objects[1], sizeof(objects[1]), "%s/dbg_sections-empty.o", directory);
    snprintf(objects[2], sizeof(objects[2]), "%s/dbg_empty.o", directory);
    snprintf(plain_source, sizeof(plain_source), "%s/dbg_plain.c", directory);
    snprintf(plain, sizeof(plain), "%s/dbg_plain.o", directory);
    snprintf(errors, sizeof(errors), "%s.err", assembled[0]);
    static const char plain_text[] = "int main(void)\n{\n    return 0;\n}\n";
    const char* texts[2] = { foreign_debug_test_sections_source, foreign_debug_test_empty_source };
    const uint64_t lengths[2] = { sizeof(foreign_debug_test_sections_source) - 1, sizeof(foreign_debug_test_empty_source) - 1 };
    bool ready = foreign_test_write(plain_source, plain_text, sizeof(plain_text) - 1);
    for (uint32_t s = 0; ready && s < 2; s += 1)
    {
        char* assemble[] = { (char*)"as", sources[s], (char*)"-o", assembled[s], 0 };
        ready = foreign_test_write(sources[s], texts[s], lengths[s]) && object_test_run(assemble, "/dev/null", errors) == 0;
    }
    char* compile[] = { (char*)"cc", (char*)"-c", (char*)"-O2", plain_source, (char*)"-o", plain, 0 };
    ready = ready && object_test_run(compile, "/dev/null", errors) == 0;
    foreign_test_expect(tally, ready, "assemble the excluded and empty debug sections' fixtures", errors);
    const bool emptied = ready && foreign_debug_test_empty_relocations(assembled[0], objects[1], ".rela.debug_aranges", arena) &&
                         foreign_debug_test_empty_relocations(assembled[1], objects[2], ".rela.debug_pubtypes", arena);
    foreign_test_expect(tally, emptied, "empty the fixtures' relocation sections", objects[1]);
    for (uint32_t fixture = 0; emptied && fixture < 3; fixture += 1)
    {
        ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 16);
        const bool lined = line != 0 && arguments != 0;
        if (lined)
        {
            *line = (ForeignTestLine){ 0 };
            foreign_test_pie_begin(line, system);
            if (fixture < 2)
            {
                foreign_test_add(line, foreign_test_path(directory, "gcc-O2-dbg_main.c.o", arena), true, true);
                foreign_test_add(line, foreign_test_path(directory, "gcc-O2-dbg_sum.c.o", arena), true, true);
            }
            else
            {
                foreign_test_add(line, plain, true, true);
            }
            foreign_test_add(line, objects[fixture], true, true);
            foreign_test_pie_end(line, system);
        }
        static const char* const ways[3] = { "--lanes=1", "--lanes=8", "--scalar" };
        String8 files[3] = { 0 };
        for (uint32_t way = 0; lined && way < 3; way += 1)
        {
            ForeignTestPath output;
            ForeignTestPath link_errors;
            snprintf(output, sizeof(output), "%s/sections%u.out%u", directory, fixture, way);
            snprintf(link_errors, sizeof(link_errors), "%s.err", output);
            static const char* const head[] = { "/proc/self/exe", "link-foreign", "-pie", "--dynamic-linker", "/lib64/ld-linux-x86-64.so.2" };
            uint32_t count = 0;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
            {
                arguments[count] = (char*)head[i];
                count += 1;
            }
            arguments[count] = (char*)ways[way];
            arguments[count + 1] = (char*)"-o";
            arguments[count + 2] = output;
            count += 3;
            for (uint32_t i = 0; i < line->our_count; i += 1)
            {
                arguments[count] = line->ours[i];
                count += 1;
            }
            arguments[count] = 0;
            const bool linked = object_test_run(arguments, "/dev/null", link_errors) == 0;
            foreign_test_expect(tally, linked, "link-foreign links excluded and empty debug sections", link_errors);
            files[way] = linked ? foreign_test_read(arena, output) : (String8){ 0 };
            if (linked)
            {
                ForeignTestPath run_output;
                snprintf(run_output, sizeof(run_output), "%s.run", output);
                char* run[] = { output, 0 };
                const bool ran = object_test_run(run, run_output, "/dev/null") == 0;
                const String8 printed = foreign_test_read(arena, run_output);
                const char* expected = fixture < 2 ? foreign_debug_test_expected[0] : "";
                foreign_test_expect(tally, ran && printed.pointer != 0 && strcmp(printed.pointer, expected) == 0, "the program runs and prints what it computes", output);
                foreign_test_expect(tally, foreign_debug_test_sections_hold(files[way], fixture),
                                    "no excluded debug byte, lld's tombstones into them, an empty section's place, and an empty relocation section's field", output);
            }
        }
        for (uint32_t way = 1; lined && way < 3; way += 1)
        {
            foreign_test_expect(tally, files[0].pointer != 0 && files[way].pointer != 0 && files[0].length == files[way].length &&
                                       memcmp(files[0].pointer, files[way].pointer, files[0].length) == 0,
                                "the same bytes on 8 lanes and by the exact path as on 1", objects[fixture]);
        }
        if (lined && cross_check)
        {
            ForeignTestPath theirs;
            ForeignTestPath lld_errors;
            snprintf(theirs, sizeof(theirs), "%s/sections%u.lld", directory, fixture);
            snprintf(lld_errors, sizeof(lld_errors), "%s.err", theirs);
            static const char* const head[] = { "ld.lld", "--hash-style=gnu", "--build-id", "--eh-frame-hdr", "-z", "now", "-z", "relro", "-o" };
            uint32_t count = 0;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
            {
                arguments[count] = (char*)head[i];
                count += 1;
            }
            arguments[count] = theirs;
            count += 1;
            for (uint32_t i = 0; i < line->their_count; i += 1)
            {
                arguments[count] = line->theirs[i];
                count += 1;
            }
            arguments[count] = 0;
            const bool linked = object_test_run(arguments, "/dev/null", lld_errors) == 0;
            foreign_test_expect(tally, linked, "ld.lld links excluded and empty debug sections", lld_errors);
            foreign_test_expect(tally, linked && foreign_debug_test_sections_hold(foreign_test_read(arena, theirs), fixture),
                                "lld's output holds what ours does: no excluded byte, its tombstones, the empty section's place and field", theirs);
        }
    }
    arena_pop_to(arena, mark);
}

// The thread-local offsets' fixtures (foreign_debug_test_offsets): a thread-local at module offset 0 (the link's only
// one, with a main that has none), and a .debug_info whose fields, after a marker, are R_X86_64_DTPOFF32's against it
// with addend -1 (0xffffffff, as GNU ld and lld write it) and -2^31 (the least a signed 32-bit field holds), then two
// without a symbol, whose value is their addend: a DTPOFF32 of -1 and an R_X86_64_32 of 2^32 - 1 (the most an unsigned
// one holds). Then four that a link refuses: a DTPOFF32 past the signed range either way (2^31 and -2^31 - 1, which GNU
// ld and lld refuse too), and without a symbol a DTPOFF32 of 2^31 and an R_X86_64_32 of -1. (lld gives every symbol-less
// relocation in a debug section its tombstone, and GNU ld a DTPOFF32 without a symbol a value from the TLS segment's
// address, so those aren't compared with theirs.)
static const char* const foreign_debug_test_offset_sources[5] = {
    "    .text\n    .globl dbg_tls_marker\n    .type dbg_tls_marker, @function\ndbg_tls_marker:\n    ret\n    .size dbg_tls_marker, .-dbg_tls_marker\n"
    "    .section .tdata,\"awT\",@progbits\n    .globl dbg_tls_value\n    .type dbg_tls_value, @object\n    .size dbg_tls_value, 4\ndbg_tls_value:\n    .long 5\n"
    "    .section .debug_info,\"\",@progbits\n    .long 0x99999999\n"
    "    .reloc ., R_X86_64_DTPOFF32, dbg_tls_value-1\n    .long 0\n"
    "    .reloc ., R_X86_64_DTPOFF32, dbg_tls_value-0x80000000\n    .long 0\n"
    "    .reloc ., R_X86_64_DTPOFF32, -1\n    .long 0\n"
    "    .reloc ., R_X86_64_32, 0xffffffff\n    .long 0\n",
    "    .section .tdata,\"awT\",@progbits\n    .globl dbg_tls_value\n    .type dbg_tls_value, @object\n    .size dbg_tls_value, 4\ndbg_tls_value:\n    .long 5\n"
    "    .section .debug_info,\"\",@progbits\n    .reloc ., R_X86_64_DTPOFF32, dbg_tls_value+0x80000000\n    .long 0\n",
    "    .section .tdata,\"awT\",@progbits\n    .globl dbg_tls_value\n    .type dbg_tls_value, @object\n    .size dbg_tls_value, 4\ndbg_tls_value:\n    .long 5\n"
    "    .section .debug_info,\"\",@progbits\n    .reloc ., R_X86_64_DTPOFF32, dbg_tls_value-0x80000001\n    .long 0\n",
    "    .section .debug_info,\"\",@progbits\n    .reloc ., R_X86_64_DTPOFF32, 0x80000000\n    .long 0\n",
    "    .section .debug_info,\"\",@progbits\n    .reloc ., R_X86_64_32, -1\n    .long 0\n",
};

// Thread-local offsets in debug sections (foreign_debug_test_offset_sources), each fixture linked with a main compiled
// without -g on 1 lane, on 8 (the writer's lane paths) and by the exact path: the first into the same bytes, which run,
// with each field as its comment says, and every other refused; with --cross-check, lld writes the first's fields with a
// symbol alike and refuses the two with a symbol past the range.
static void foreign_debug_test_offsets(ForeignTestTally* tally, const char* directory, ForeignTestSystem* system, bool cross_check, Arena* arena)
{
    const uint64_t mark = arena->length;
    ForeignTestPath plain;
    snprintf(plain, sizeof(plain), "%s/dbg_plain.o", directory);
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(foreign_debug_test_offset_sources); fixture += 1)
    {
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/dbg_offsets%u.s", directory, fixture);
        snprintf(object, sizeof(object), "%s/dbg_offsets%u.o", directory, fixture);
        snprintf(errors, sizeof(errors), "%s.err", object);
        char* assemble[] = { (char*)"as", source, (char*)"-o", object, 0 };
        const bool assembled = foreign_test_write(source, foreign_debug_test_offset_sources[fixture], strlen(foreign_debug_test_offset_sources[fixture])) &&
                               object_test_run(assemble, "/dev/null", errors) == 0;
        foreign_test_expect(tally, assembled, "assemble a thread-local offsets' fixture", errors);
        ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
        char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, BUSTER_FOREIGN_TEST_MAX_ARGUMENTS + 16);
        const bool lined = assembled && line != 0 && arguments != 0;
        if (lined)
        {
            *line = (ForeignTestLine){ 0 };
            foreign_test_pie_begin(line, system);
            foreign_test_add(line, plain, true, true);
            foreign_test_add(line, object, true, true);
            foreign_test_pie_end(line, system);
        }
        static const char* const ways[3] = { "--lanes=1", "--lanes=8", "--scalar" };
        String8 files[3] = { 0 };
        for (uint32_t way = 0; lined && way < 3; way += 1)
        {
            ForeignTestPath output;
            ForeignTestPath link_errors;
            snprintf(output, sizeof(output), "%s/offsets%u.out%u", directory, fixture, way);
            snprintf(link_errors, sizeof(link_errors), "%s.err", output);
            static const char* const head[] = { "/proc/self/exe", "link-foreign", "-pie", "--dynamic-linker", "/lib64/ld-linux-x86-64.so.2" };
            uint32_t count = 0;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
            {
                arguments[count] = (char*)head[i];
                count += 1;
            }
            arguments[count] = (char*)ways[way];
            arguments[count + 1] = (char*)"-o";
            arguments[count + 2] = output;
            count += 3;
            for (uint32_t i = 0; i < line->our_count; i += 1)
            {
                arguments[count] = line->ours[i];
                count += 1;
            }
            arguments[count] = 0;
            const int status = object_test_run(arguments, "/dev/null", link_errors);
            if (fixture == 0)
            {
                foreign_test_expect(tally, status == 0, "link-foreign links thread-local offsets in range", link_errors);
                files[way] = status == 0 ? foreign_test_read(arena, output) : (String8){ 0 };
                const uint8_t* fields = files[way].pointer != 0 ? foreign_debug_test_marked(files[way], ".debug_info", 0x99999999, 4, 20) : 0;
                uint32_t values[4] = { 0 };
                if (fields != 0)
                {
                    memcpy(values, fields + 4, sizeof(values));
                }
                char* run[] = { output, 0 };
                foreign_test_expect(tally, status == 0 && object_test_run(run, "/dev/null", "/dev/null") == 0, "the program runs", output);
                foreign_test_expect(tally, fields != 0 && values[0] == 0xffffffffu && values[1] == 0x80000000u && values[2] == 0xffffffffu && values[3] == 0xffffffffu,
                                    "each thread-local offset and value in range as a signed or unsigned 32-bit field holds it", output);
            }
            else
            {
                foreign_test_expect(tally, status > 0, "link-foreign refuses a 32-bit field's value past its range", output);
            }
        }
        for (uint32_t way = 1; lined && fixture == 0 && way < 3; way += 1)
        {
            foreign_test_expect(tally, files[0].pointer != 0 && files[way].pointer != 0 && files[0].length == files[way].length &&
                                       memcmp(files[0].pointer, files[way].pointer, files[0].length) == 0,
                                "the same bytes on 8 lanes and by the exact path as on 1", object);
        }
        // lld's, where it agrees: the fields with a symbol, and the two with a symbol past the range refused.
        if (lined && cross_check && fixture <= 2)
        {
            ForeignTestPath theirs;
            ForeignTestPath lld_errors;
            snprintf(theirs, sizeof(theirs), "%s/offsets%u.lld", directory, fixture);
            snprintf(lld_errors, sizeof(lld_errors), "%s.err", theirs);
            static const char* const head[] = { "ld.lld", "--hash-style=gnu", "--build-id", "--eh-frame-hdr", "-z", "now", "-z", "relro", "-o" };
            uint32_t count = 0;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(head); i += 1)
            {
                arguments[count] = (char*)head[i];
                count += 1;
            }
            arguments[count] = theirs;
            count += 1;
            for (uint32_t i = 0; i < line->their_count; i += 1)
            {
                arguments[count] = line->theirs[i];
                count += 1;
            }
            arguments[count] = 0;
            const int status = object_test_run(arguments, "/dev/null", lld_errors);
            if (fixture == 0)
            {
                const String8 file = status == 0 ? foreign_test_read(arena, theirs) : (String8){ 0 };
                const uint8_t* fields = file.pointer != 0 ? foreign_debug_test_marked(file, ".debug_info", 0x99999999, 4, 20) : 0;
                uint32_t values[2] = { 0 };
                if (fields != 0)
                {
                    memcpy(values, fields + 4, sizeof(values));
                }
                foreign_test_expect(tally, fields != 0 && values[0] == 0xffffffffu && values[1] == 0x80000000u, "lld writes the thread-local offsets with a symbol as ours", theirs);
            }
            else
            {
                foreign_test_expect(tally, status > 0, "lld refuses the thread-local offset past the signed range too", lld_errors);
            }
        }
    }
    arena_pop_to(arena, mark);
}

// One run of a relocation section's kernel for foreign_debug_test_kernels: its bytes, records, count and result.
typedef struct ForeignDebugTestRun ForeignDebugTestRun;
struct ForeignDebugTestRun
{
    uint8_t* bytes;
    uint32_t offsets[BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS];
    int64_t addends[BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS];
    SymbolId targets[BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS];
    uint8_t section_kinds[BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS];
    uint8_t kinds[BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS];
    uint64_t count;
    ForeignLinkResult result;
};

// The kernels' twins on one copy of the object: `file` (`length` bytes, its relocation section `relocation` moved to its
// end) ending at a PROT_NONE page, its target's bytes copied twice to buffers ending at one, the vector path's run
// against the exact path's: the same result, bytes and records.
static bool foreign_debug_test_twins(const uint8_t* file, uint64_t length, uint32_t relocation, const uint8_t* contents, uint64_t size)
{
    ImportElf elf = { 0 };
    ImportSymbols symbols = { 0 };
    bool same = import_elf(&elf, (String8){ .pointer = (char*)file, .length = length }, ET_REL).error == IMPORT_ERROR_NONE &&
                import_symbols(&elf, &symbols).error == IMPORT_ERROR_NONE;
    const Elf64_Shdr header = same ? import_section_header(&elf, relocation) : (Elf64_Shdr){ 0 };
    ForeignObject input = { .elf = elf, .symbols = symbols };
    uint32_t kind = 0;
    same = same && foreign_debug_section(&input, header.sh_info, &kind) && header.sh_size / sizeof(Elf64_Rela) <= BUSTER_FOREIGN_DEBUG_TEST_RELOCATIONS;
    // Each kept debug section at a place of its own, and a third of the symbols without one in the link.
    uint64_t bases[256];
    SymbolId map[4096];
    same = same && elf.section_count <= BUSTER_ARRAY_LENGTH(bases) && symbols.count <= BUSTER_ARRAY_LENGTH(map);
    for (uint32_t section = 0; same && section < elf.section_count; section += 1)
    {
        uint32_t other = 0;
        bases[section] = foreign_debug_section(&input, section, &other) ? 0x1000ull * section + 0x30 : UINT64_MAX;
    }
    for (uint32_t index = 0; same && index < symbols.count; index += 1)
    {
        map[index] = index % 3 == 0 ? 0 : index + 100;
    }
    ImportObject object = { .symbols = map, .symbol_count = symbols.count };
    ForeignLink output = { .objects = &object };
    static uint64_t resolved[4096];
    if (same && symbols.count != 0)
    {
        foreign_debug_resolve(&input, &object, bases, resolved);
    }
    static ForeignDebugTestRun runs[2];
    ForeignTestEdge edges[2] = { 0 };
    for (uint32_t path = 0; same && path < 2; path += 1)
    {
        same = foreign_test_edge(&edges[path], size + 1);
        ForeignDebugTestRun* run = &runs[path];
        run->bytes = same ? edges[path].end - size : 0;
        if (same)
        {
            memcpy(run->bytes, contents, size);
            run->count = 0;
            const ForeignDebugSection section = {
                .output = &output,
                .input = &input,
                .relocation_section = relocation,
                .kind = kind,
                .base = 0x48,
                .bytes = run->bytes,
                .size = size,
                .bases = bases,
                .resolved = resolved,
                .records = {
                    .offsets = run->offsets,
                    .addends = run->addends,
                    .targets = run->targets,
                    .section_kinds = run->section_kinds,
                    .kinds = run->kinds,
                },
            };
            run->result = path == 0 ? foreign_debug_relocate_vector(&section, &run->count) : foreign_debug_relocate(&section, 0, header.sh_size / sizeof(Elf64_Rela), &run->count);
        }
    }
    const ForeignLinkResult* a = &runs[0].result;
    const ForeignLinkResult* b = &runs[1].result;
    same = same && a->error == b->error && a->object == b->object && a->import.error == b->import.error && a->import.offset == b->import.offset &&
           a->import.section == b->import.section && a->import.relocation == b->import.relocation && a->import.type == b->import.type && runs[0].count == runs[1].count &&
           memcmp(runs[0].bytes, runs[1].bytes, size) == 0;
    for (uint64_t r = 0; same && r < runs[0].count; r += 1)
    {
        same = runs[0].offsets[r] == runs[1].offsets[r] && runs[0].addends[r] == runs[1].addends[r] && runs[0].targets[r] == runs[1].targets[r] &&
               runs[0].section_kinds[r] == runs[1].section_kinds[r] && runs[0].kinds[r] == runs[1].kinds[r];
    }
    foreign_test_edge_release(&edges[0]);
    foreign_test_edge_release(&edges[1]);
    return same;
}

// The relocation kernels' vector path against its exact twin (foreign_debug_relocate_vector, foreign_debug_relocate),
// on each debug relocation section of an object gcc compiles with -g (a thread-local among its variables), moved to
// the end of a copy of the object that ends at a PROT_NONE page, where a load past its entries faults: as compiled, cut
// to each count short of a batch, and with relocation j of the first 24 changed each way a batch leaves to the exact
// path or that changes what a batch writes: R_X86_64_NONE, a type debug sections don't take (R_X86_64_PC32), a
// thread-local offset (where its symbol is a debug section's, refused), a field past the section, a symbol past the
// table, its symbol's section through SHN_XINDEX without the extended table, no symbol, an ABS32 whose value doesn't
// fit, a field that overlaps the one before or comes before it, a negated addend, its symbol at a place in its
// section (a section's symbol, which compilers use, is at 0), and without a symbol (its value its addend) a thread-local
// offset of -1, of 2^31 (past a signed 32-bit field's reach), of -2^31 (its least), and an ABS32 of -1 (past an
// unsigned one's) and of 2^32 - 1 (its most). Each copy's runs must agree: the same failure, or the same bytes and
// records.
static void foreign_debug_test_kernels(ForeignTestTally* tally, const char* directory, Arena* arena)
{
    ForeignTestPath source;
    ForeignTestPath object;
    snprintf(source, sizeof(source), "%s/%s", directory, foreign_debug_test_sources[1].name);
    snprintf(object, sizeof(object), "%s/kernels.o", directory);
    char include[BUSTER_FOREIGN_TEST_PATH_BYTES + 2];
    snprintf(include, sizeof(include), "-I%s", directory);
    char* arguments[] = { (char*)"cc", (char*)"-c", (char*)"-g", (char*)"-O2", (char*)"-gz=none", include, source, (char*)"-o", object, 0 };
    const bool compiled = object_test_run(arguments, "/dev/null", "/dev/null") == 0;
    foreign_test_expect(tally, compiled, "compile the kernels' fixture", object);
    const String8 bytes = compiled ? foreign_test_read(arena, object) : (String8){ 0 };
    ImportElf elf = { 0 };
    ImportSymbols symbols = { 0 };
    const bool read = bytes.pointer != 0 && import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE && import_symbols(&elf, &symbols).error == IMPORT_ERROR_NONE;
    foreign_test_expect(tally, read, "read the kernels' fixture", object);
    ForeignObject input = { .elf = elf, .symbols = symbols };
    uint32_t sections = 0;
    uint64_t runs = 0;
    for (uint32_t relocation = 1; read && relocation < elf.section_count; relocation += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, relocation);
        uint32_t kind = 0;
        const bool debug = header.sh_type == SHT_RELA && header.sh_info < elf.section_count && foreign_debug_section(&input, header.sh_info, &kind);
        const Elf64_Shdr target = debug ? import_section_header(&elf, header.sh_info) : (Elf64_Shdr){ 0 };
        const String8 contents = debug ? import_section_bytes(&elf, &target) : (String8){ 0 };
        const uint64_t total = debug ? header.sh_size / sizeof(Elf64_Rela) : 0;
        // The copy: the object, then the relocations at a multiple of 8, the section's header pointing at them.
        const uint64_t place = (bytes.length + 7) & ~7ull;
        Elf64_Rela* entries = debug ? BUSTER_ARENA_PUSH_ARRAY(arena, Elf64_Rela, total + 1) : 0;
        uint8_t* copy = debug ? arena_push(arena, place + (total + 1) * sizeof(Elf64_Rela), 8) : 0;
        foreign_test_expect(tally, !debug || (entries != 0 && copy != 0), "room for the kernels' copies", object);
        sections += entries != 0 && copy != 0 ? 1 : 0;
        for (uint32_t change = 0; entries != 0 && copy != 0 && change < 18; change += 1)
        {
            for (uint64_t j = 0; j < (change == 0 ? 9 : 24) && j < total; j += 1)
            {
                memcpy(entries, bytes.pointer + header.sh_offset, total * sizeof(Elf64_Rela));
                uint64_t count = change == 0 ? total - j : total;
                Elf64_Rela* changed = &entries[j];
                const uint32_t index = (uint32_t)ELF64_R_SYM(changed->r_info);
                const uint32_t type = ELF64_R_TYPE(changed->r_info);
                uint64_t symbol_entry = 0;
                uint64_t symbol_value = 0;
                switch (change)
                {
                    case 1: changed->r_info = ELF64_R_INFO(index, R_X86_64_NONE); break;
                    case 2: changed->r_info = ELF64_R_INFO(index, R_X86_64_PC32); break;
                    case 3: changed->r_info = ELF64_R_INFO(index, R_X86_64_DTPOFF32); break;
                    case 4: changed->r_offset = target.sh_size; break;
                    case 5: changed->r_info = ELF64_R_INFO(symbols.count, type); break;
                    case 6: symbol_entry = index; break;
                    case 7: changed->r_info = ELF64_R_INFO(0, type); break;
                    case 8:
                        changed->r_info = ELF64_R_INFO(index, R_X86_64_32);
                        changed->r_addend += 1ll << 32;
                        break;
                    case 9: changed->r_offset = j != 0 ? entries[j - 1].r_offset + 2 : changed->r_offset; break;
                    case 10: changed->r_offset = j != 0 ? entries[j - 1].r_offset - 4 : changed->r_offset; break;
                    case 11: changed->r_addend = -changed->r_addend - 3; break;
                    case 12: symbol_value = index; break;
                    // No symbol: R_X86_64_DTPOFF32's value is signed, R_X86_64_32's unsigned.
                    case 13: changed->r_info = ELF64_R_INFO(0, R_X86_64_DTPOFF32); changed->r_addend = -1; break;
                    case 14: changed->r_info = ELF64_R_INFO(0, R_X86_64_DTPOFF32); changed->r_addend = INT64_C(0x80000000); break;
                    case 15: changed->r_info = ELF64_R_INFO(0, R_X86_64_DTPOFF32); changed->r_addend = INT32_MIN; break;
                    case 16: changed->r_info = ELF64_R_INFO(0, R_X86_64_32); changed->r_addend = -1; break;
                    case 17: changed->r_info = ELF64_R_INFO(0, R_X86_64_32); changed->r_addend = INT64_C(0xffffffff); break;
                    default: break;
                }
                memcpy(copy, bytes.pointer, bytes.length);
                memset(copy + bytes.length, 0, place - bytes.length);
                memcpy(copy + place, entries, count * sizeof(Elf64_Rela));
                Elf64_Shdr moved = header;
                moved.sh_offset = place;
                moved.sh_size = count * sizeof(Elf64_Rela);
                memcpy(copy + elf.section_offset + (uint64_t)relocation * sizeof(Elf64_Shdr), &moved, sizeof(moved));
                if (symbol_entry != 0)
                {
                    const uint16_t extended = SHN_XINDEX;
                    memcpy(copy + import_section_header(&elf, symbols.section).sh_offset + symbol_entry * sizeof(Elf64_Sym) + 6, &extended, sizeof(extended));
                }
                if (symbol_value != 0)
                {
                    // A symbol at a place in its section, as a section's symbols aren't.
                    const uint64_t value = 8 + 4 * j;
                    memcpy(copy + import_section_header(&elf, symbols.section).sh_offset + symbol_value * sizeof(Elf64_Sym) + 8, &value, sizeof(value));
                }
                const uint64_t length = place + count * sizeof(Elf64_Rela);
                ForeignTestEdge edge;
                bool same = foreign_test_edge(&edge, length);
                if (same)
                {
                    memcpy(edge.end - length, copy, length);
                    same = foreign_debug_test_twins(edge.end - length, length, relocation, (const uint8_t*)contents.pointer, target.sh_size);
                }
                foreign_test_edge_release(&edge);
                char what[128];
                snprintf(what, sizeof(what), "the relocation kernels agree: section %u, change %u, relocation %llu", relocation, change, (unsigned long long)j);
                foreign_test_expect(tally, same, what, object);
                runs += 1;
            }
        }
    }
    foreign_test_expect(tally, sections != 0, "the kernels' fixture has debug relocation sections", object);
    printf("  relocation kernels: %u sections, %llu copies, each the vector path's run against the exact path's\n", sections, (unsigned long long)runs);
}

static bool foreign_debug_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    ForeignTestTally tally = {
        .check = "foreign DWARF",
    };
    bool written = system->found;
    foreign_test_expect(&tally, written, "find the system's C runtime", 0);
    for (uint32_t s = 0; written && s < BUSTER_ARRAY_LENGTH(foreign_debug_test_sources); s += 1)
    {
        ForeignTestPath path;
        snprintf(path, sizeof(path), "%s/%s", directory, foreign_debug_test_sources[s].name);
        written = foreign_test_write(path, foreign_debug_test_sources[s].text, strlen(foreign_debug_test_sources[s].text));
        foreign_test_expect(&tally, written, "write the fixtures", path);
    }
    for (uint32_t v = 0; written && v < BUSTER_ARRAY_LENGTH(foreign_debug_test_variants); v += 1)
    {
        foreign_debug_test_variant(&tally, directory, &foreign_debug_test_variants[v], system, cross_check, arena);
    }
    if (written)
    {
        foreign_debug_test_refusals(&tally, directory, system, arena);
        foreign_debug_test_sections(&tally, directory, system, cross_check, arena);
        foreign_debug_test_offsets(&tally, directory, system, cross_check, arena);
        foreign_debug_test_kernels(&tally, directory, arena);
    }
    return foreign_test_report(&tally);
}
