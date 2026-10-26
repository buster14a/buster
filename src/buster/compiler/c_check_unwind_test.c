// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's unwind tables (#395, check_program_emit's), which test-check-debug --cross-check runs. A fixture's
// `depth` counts the frames above it with glibc's backtrace() (libgcc's unwinder, which finds each function's FDE through
// .eh_frame_hdr), called from main, from a function with a variable-length array (a frame pointer, rsp moved in its
// body) that a computed goto's label calls, and from one with values live across the call:
//
// - The driver's executable by each path, the same bytes: by default, and with -funwind-tables or -g after
//   -fno-asynchronous-unwind-tables, the unwinder walks through every function to main and libc's start, which the
//   program checks (it prints the three depths and returns 0); with -fno-asynchronous-unwind-tables alone, it stops at
//   the first of them, and the program prints 1 1 1.
// - Its object (-c) by each path, the same bytes: an .eh_frame of one CIE and an FDE per function the object defines,
//   each a relocation; linked by cc through GNU ld and through lld, and by link-foreign from GCC's driver's line, into
//   programs that walk alike. With -fno-asynchronous-unwind-tables, no .eh_frame.
// - gdb at every instruction of each function of the -g executable, from its first to its ret, stepping over calls:
//   each backtrace's frames are the function and its callers.

static const char check_unwind_test_text[] =
    "#include <execinfo.h>\n"
    "#include <stdio.h>\n"
    "\n"
    "int depth(void)\n"
    "{\n"
    "    void* frames[64];\n"
    "    return backtrace(frames, 64);\n"
    "}\n"
    "\n"
    "long pressure(long x)\n"
    "{\n"
    "    long a = x + 1;\n"
    "    long b = x + 2;\n"
    "    long c = x + 3;\n"
    "    long d = x + 4;\n"
    "    long e = x + 5;\n"
    "    long f = x + 6;\n"
    "    long g = x + 7;\n"
    "    long h = x + 8;\n"
    "    long i = x + 9;\n"
    "    long j = x + 10;\n"
    "    long k = x + 11;\n"
    "    long l = x + 12;\n"
    "    long m = x + 13;\n"
    "    long n = x + 14;\n"
    "    long frames = depth();\n"
    "    long sum = a + b + c + d + e + f + g + h + i + j + k + l + m + n;\n"
    "    return sum == 14 * x + 105 ? frames : -1;\n"
    "}\n"
    "\n"
    "int vla(int n)\n"
    "{\n"
    "    int values[n];\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        values[i] = i;\n"
    "    }\n"
    "    return depth() + values[n - 1] - (n - 1);\n"
    "}\n"
    "\n"
    "int jump(int which)\n"
    "{\n"
    "    static void* const labels[] = { &&first, &&second };\n"
    "    goto *labels[which];\n"
    "first:\n"
    "    return vla(3);\n"
    "second:\n"
    "    return vla(5);\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int baseline = depth();\n"
    "    int deep = jump(1);\n"
    "    long pressed = pressure(5);\n"
    "    printf(\"%d %d %ld\\n\", baseline, deep, pressed);\n"
    "    return baseline >= 3 && deep == baseline + 2 && pressed == baseline + 1 ? 0 : 1;\n"
    "}\n";

// An option set of the driver's, and whether its program has unwind tables.
typedef struct CheckUnwindTestSet CheckUnwindTestSet;
struct CheckUnwindTestSet
{
    const char* name;
    const char* options[2];
    uint32_t count;
    bool tables;
};

static const CheckUnwindTestSet check_unwind_test_sets[] = {
    {
        .name = "default",
        .tables = true,
    },
    {
        .name = "no-tables",
        .options = { "-fno-asynchronous-unwind-tables" },
        .count = 1,
    },
    {
        .name = "unwind-tables",
        .options = { "-fno-asynchronous-unwind-tables", "-funwind-tables" },
        .count = 2,
        .tables = true,
    },
    {
        .name = "debug",
        .options = { "-fno-asynchronous-unwind-tables", "-g" },
        .count = 2,
        .tables = true,
    },
};

// The driver (/proc/self/exe) on `source` with the set's options (and -c, or --scalar), into `output`: whether it
// succeeded, its errors in `output`.err.
static bool check_unwind_test_compile(const char* source, const char* output, const CheckUnwindTestSet* set, bool object, bool scalar)
{
    char errors[PATH_MAX + 8];
    snprintf(errors, sizeof(errors), "%s.err", output);
    char* arguments[12] = { (char*)"/proc/self/exe" };
    uint32_t count = 1;
    for (uint32_t i = 0; i < set->count; i += 1)
    {
        arguments[count] = (char*)set->options[i];
        count += 1;
    }
    if (object)
    {
        arguments[count] = (char*)"-c";
        count += 1;
    }
    if (scalar)
    {
        arguments[count] = (char*)"--scalar";
        count += 1;
    }
    arguments[count] = (char*)"-o";
    arguments[count + 1] = (char*)output;
    arguments[count + 2] = (char*)source;
    arguments[count + 3] = 0;
    return object_test_run(arguments, "/dev/null", errors) == 0;
}

// Whether the program runs as its unwind tables (or their absence) make it: with them, it returns 0 having printed
// depths that walk through main to libc's start; without them, it returns 1 having printed 1 1 1.
static bool check_unwind_test_run(const char* program, bool tables, Arena* arena)
{
    char report[PATH_MAX + 8];
    snprintf(report, sizeof(report), "%s.out", program);
    char* run[] = { (char*)program, 0 };
    const int status = object_test_run(run, report, "/dev/null");
    const String8 printed = foreign_test_read(arena, report);
    int baseline = 0;
    int deep = 0;
    long pressed = 0;
    const bool read = printed.pointer != 0 && sscanf(printed.pointer, "%d %d %ld", &baseline, &deep, &pressed) == 3;
    return read && (tables ? status == 0 && baseline >= 3 && deep == baseline + 2 && pressed == baseline + 1 : status == 1 && baseline == 1 && deep == 1 && pressed == 1);
}

// The object's .eh_frame records (CIEs and FDEs) and its function definitions, from its sections; false if they can't be
// read.
static bool check_unwind_test_census(String8 object, uint32_t* cies, uint32_t* fdes, uint32_t* functions, uint32_t* relocations)
{
    const Elf64_Shdr frames = check_debug_test_section(object, ".eh_frame", false);
    const Elf64_Shdr relocated = check_debug_test_section(object, ".eh_frame", true);
    const Elf64_Shdr symbols = check_debug_test_section(object, ".symtab", false);
    *cies = 0;
    *fdes = 0;
    *functions = 0;
    *relocations = (uint32_t)(relocated.sh_size / sizeof(Elf64_Rela));
    bool valid = symbols.sh_size != 0 && symbols.sh_entsize == sizeof(Elf64_Sym);
    for (uint64_t at = 0; valid && at < frames.sh_size;)
    {
        uint32_t length = 0;
        uint32_t id = 0;
        valid = frames.sh_size - at >= 8;
        if (valid)
        {
            memcpy(&length, object.pointer + frames.sh_offset + at, 4);
            memcpy(&id, object.pointer + frames.sh_offset + at + 4, 4);
            valid = length != 0 && length != UINT32_MAX && length <= frames.sh_size - at - 4;
            *cies += id == 0 ? 1 : 0;
            *fdes += id != 0 ? 1 : 0;
            at += 4 + (uint64_t)length;
        }
    }
    for (uint64_t s = 1; valid && s < symbols.sh_size / sizeof(Elf64_Sym); s += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, object.pointer + symbols.sh_offset + s * sizeof(symbol), sizeof(symbol));
        *functions += ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && symbol.st_shndx != SHN_UNDEF ? 1 : 0;
    }
    return valid;
}

// The size of the function `name` in an executable's .symtab, or 0.
static uint64_t check_unwind_test_size(String8 file, const char* name)
{
    const ExecutableTestImage image = executable_test_image_of(file, ET_DYN);
    const Elf64_Shdr* symbols = image.valid ? executable_test_section(&image, ".symtab") : 0;
    const bool valid = symbols != 0 && symbols->sh_entsize == sizeof(Elf64_Sym) && symbols->sh_link < image.header->e_shnum &&
                       symbols->sh_offset <= file.length && symbols->sh_size <= file.length - symbols->sh_offset;
    const Elf64_Shdr* strings = valid ? &image.section_headers[symbols->sh_link] : 0;
    uint64_t size = 0;
    for (uint64_t s = 1; valid && strings->sh_offset + strings->sh_size <= file.length && s < symbols->sh_size / sizeof(Elf64_Sym); s += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, file.pointer + symbols->sh_offset + s * sizeof(symbol), sizeof(symbol));
        const char* text = symbol.st_name < strings->sh_size ? file.pointer + strings->sh_offset + symbol.st_name : 0;
        const bool named = text != 0 && memchr(text, 0, strings->sh_size - symbol.st_name) != 0 && strcmp(text, name) == 0;
        size = named && ELF64_ST_TYPE(symbol.st_info) == STT_FUNC ? symbol.st_size : size;
    }
    return size;
}

// Whether every step of gdb's transcript (UNWIND_STEP, then `bt`) has the frames `names` give, the stepped function's
// first, and the transcript ends with UNWIND_DONE; with the count of steps.
static bool check_unwind_test_steps(const char* text, const char* const* names, uint32_t count, uint32_t* steps)
{
    bool valid = text != 0 && strstr(text, "UNWIND_DONE") != 0;
    const char* block = valid ? strstr(text, "UNWIND_STEP\n") : 0;
    *steps = 0;
    while (valid && block != 0)
    {
        const char* next = strstr(block + 1, "UNWIND_STEP\n");
        const char* end = next != 0 ? next : strstr(block, "UNWIND_DONE");
        for (uint32_t frame = 0; valid && frame < count; frame += 1)
        {
            char label[16];
            char name[80];
            snprintf(label, sizeof(label), "\n#%u ", frame);
            snprintf(name, sizeof(name), " %s (", names[frame]);
            const char* line = strstr(block, label);
            const char* line_end = line != 0 ? strchr(line + 1, '\n') : 0;
            const char* found = line != 0 ? strstr(line, name) : 0;
            valid = line != 0 && line < end && line_end != 0 && found != 0 && found < line_end;
        }
        *steps += 1;
        block = next;
    }
    return valid && *steps != 0;
}

// gdb at every instruction of `function` of the -g executable `program` (its first call), from its first to its ret,
// stepping over calls (see the file's comment).
static void check_unwind_test_gdb(CheckDebugTestTally* tally, const char* program, String8 file, const char* const* names, uint32_t count, Arena* arena)
{
    const uint64_t size = check_unwind_test_size(file, names[0]);
    char script[PATH_MAX + 32];
    char report[PATH_MAX + 32];
    snprintf(script, sizeof(script), "%s.%s.gdb", program, names[0]);
    snprintf(report, sizeof(report), "%s.%s.steps", program, names[0]);
    char text[1024];
    const int length = snprintf(text, sizeof(text),
                                "set pagination off\nset confirm off\nset width 0\nset debuginfod enabled off\nbreak *%s\nrun\n"
                                "while $pc >= (char*)%s && $pc < (char*)%s + %" PRIu64 "\nprintf \"UNWIND_STEP\\n\"\nbt %u\nnexti\nend\n"
                                "printf \"UNWIND_DONE\\n\"\nkill\n",
                                names[0], names[0], names[0], size, count);
    const bool written = size != 0 && length > 0 && (uint32_t)length < sizeof(text) && foreign_test_write(script, text, (uint64_t)length);
    check_debug_test_expect(tally, written, "write gdb's script for a function's every instruction (its size from .symtab)", script);
    char* arguments[] = { (char*)"gdb", (char*)"-nx", (char*)"-batch", (char*)"-x", script, (char*)program, 0 };
    const bool ran = written && object_test_run(arguments, report, "/dev/null") == 0;
    const String8 transcript = ran ? foreign_test_read(arena, report) : (String8){ 0 };
    uint32_t steps = 0;
    const bool walked = ran && check_unwind_test_steps(transcript.pointer, names, count, &steps);
    char detail[PATH_MAX + 64];
    snprintf(detail, sizeof(detail), "%s, %u steps", report, steps);
    check_debug_test_expect(tally, walked && steps > 4, "gdb's backtrace at every instruction of the function gives it and its callers", detail);
}

// The fixture's executables and objects, and gdb on its -g executable (see the file's comment).
static void check_unwind_test_driver(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/dbg_unwind.c", directory);
    const bool written = foreign_test_write(source, check_unwind_test_text, sizeof(check_unwind_test_text) - 1);
    check_debug_test_expect(tally, written, "write the unwind tables' fixture", source);
    char debug_program[PATH_MAX + 32] = { 0 };
    for (uint32_t s = 0; written && s < BUSTER_ARRAY_LENGTH(check_unwind_test_sets); s += 1)
    {
        const CheckUnwindTestSet* set = &check_unwind_test_sets[s];
        // The executable, then the object, each by both paths.
        for (uint32_t object = 0; object < 2; object += 1)
        {
            char outputs[2][PATH_MAX + 32];
            String8 files[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                snprintf(outputs[path], sizeof(outputs[path]), "%s/dbg_unwind.%s%s%s", directory, set->name, path == 0 ? "" : ".scalar", object != 0 ? ".o" : "");
                const bool made = check_unwind_test_compile(source, outputs[path], set, object != 0, path != 0);
                check_debug_test_expect(tally, made, object != 0 ? "the driver writes the unwind fixture's object" : "the driver links the unwind fixture", outputs[path]);
                files[path] = made ? foreign_test_read(arena, outputs[path]) : (String8){ 0 };
            }
            check_debug_test_expect(tally, files[0].pointer != 0 && files[1].pointer != 0 && files[0].length == files[1].length &&
                                               memcmp(files[0].pointer, files[1].pointer, files[0].length) == 0,
                                    "the vector path's output is --scalar's, byte for byte", outputs[0]);
            if (object == 0 && files[0].pointer != 0)
            {
                check_debug_test_expect(tally, check_unwind_test_run(outputs[0], set->tables, arena),
                                        set->tables ? "the unwinder walks through every function of the program" : "without unwind tables, the unwinder stops at the program's first function",
                                        outputs[0]);
                if (strcmp(set->name, "debug") == 0)
                {
                    snprintf(debug_program, sizeof(debug_program), "%s", outputs[0]);
                }
            }
            uint32_t cies = 0;
            uint32_t fdes = 0;
            uint32_t functions = 0;
            uint32_t relocations = 0;
            const bool census = object != 0 && files[0].pointer != 0 && check_unwind_test_census(files[0], &cies, &fdes, &functions, &relocations);
            if (object != 0 && set->tables)
            {
                check_debug_test_expect(tally, census && functions >= 5 && cies == 1 && fdes == functions && relocations == fdes,
                                        "the object's .eh_frame is one CIE and an FDE per function, each relocated", outputs[0]);
            }
            else if (object != 0)
            {
                check_debug_test_expect(tally, census && functions >= 5 && cies == 0 && fdes == 0 && relocations == 0, "without unwind tables, the object has no .eh_frame",
                                        outputs[0]);
            }
            // The object linked by cc through GNU ld and lld, and by link-foreign.
            for (uint32_t linker = 0; object != 0 && set->tables && census && linker < 3; linker += 1)
            {
                static const char* const names[] = { "bfd", "lld", "foreign" };
                char program[PATH_MAX + 48];
                char errors[PATH_MAX + 56];
                snprintf(program, sizeof(program), "%s.%s", outputs[0], names[linker]);
                snprintf(errors, sizeof(errors), "%s.err", program);
                bool linked = false;
                if (linker < 2)
                {
                    char* arguments[] = { (char*)"cc", linker == 0 ? (char*)"-fuse-ld=bfd" : (char*)"-fuse-ld=lld", (char*)"-o", program, outputs[0], 0 };
                    linked = object_test_run(arguments, "/dev/null", errors) == 0;
                }
                else
                {
                    linked = check_debug_test_link_foreign(outputs[0], program, errors, arena);
                }
                check_debug_test_expect(tally, linked, linker == 0 ? "cc links the unwind fixture's object through GNU ld" :
                                                   linker == 1 ? "cc links the unwind fixture's object through lld" : "link-foreign links the unwind fixture's object",
                                        errors);
                check_debug_test_expect(tally, linked && check_unwind_test_run(program, true, arena), "the linked program's unwinder walks through every function", program);
            }
        }
    }
    // gdb at every instruction of each function but main, of the -g executable.
    const String8 file = debug_program[0] != 0 ? foreign_test_read(arena, debug_program) : (String8){ 0 };
    check_debug_test_expect(tally, file.pointer != 0, "the -g executable for gdb's steps", debug_program);
    static const char* const depth[] = { "depth", "main" };
    static const char* const pressure[] = { "pressure", "main" };
    static const char* const vla[] = { "vla", "jump", "main" };
    static const char* const jump[] = { "jump", "main" };
    if (file.pointer != 0)
    {
        check_unwind_test_gdb(tally, debug_program, file, depth, BUSTER_ARRAY_LENGTH(depth), arena);
        check_unwind_test_gdb(tally, debug_program, file, pressure, BUSTER_ARRAY_LENGTH(pressure), arena);
        check_unwind_test_gdb(tally, debug_program, file, vla, BUSTER_ARRAY_LENGTH(vla), arena);
        check_unwind_test_gdb(tally, debug_program, file, jump, BUSTER_ARRAY_LENGTH(jump), arena);
    }
}
