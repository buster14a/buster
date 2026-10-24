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
// are refused, naming their object and section.
// With --cross-check, ld.lld links the same objects: llvm-dwarfdump --verify finds no error in either output, their
// --statistics agree but for .debug_str's and .debug_line_str's sizes (lld merges their strings), and gdb, stopped in a
// function of each, gives the same backtrace, arguments and locals but for addresses.

#define BUSTER_FOREIGN_DEBUG_TEST_STATISTICS 192

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

// A program's output, and where gdb stops in it.
static const char* const foreign_debug_test_expected[2] = { "total 34 -\n", "max 0 7\n" };
static const char* const foreign_debug_test_stops[2] = { "dbg_sum", "dbg_other" };

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
            snprintf(stop, sizeof(stop), "break %s", foreign_debug_test_stops[variant->cxx ? 1 : 0]);
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
    }
    return foreign_test_report(&tally);
}
