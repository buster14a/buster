// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's unwind tables (#395, check_program_emit's), which test-check-debug --cross-check runs. A fixture's
// `depth` counts the frames above it with glibc's backtrace() (libgcc's unwinder, which finds each function's FDE through
// .eh_frame_hdr), called from main, from a function with a variable-length array (a frame pointer, rsp moved in its
// body) that each of a computed goto's labels calls, from one with values live across the call, called with a
// positive and a negative argument so that both arms of its last ?: run, and from one of 128-bit integers (Tier 0's
// I128 and U128, in their frame slots), four of them live across the call, its division and remainder libgcc's calls,
// called with shifts of 3 and of 64 so that its shifts' cmovne takes both values:
//
// - The driver's executable by each path, the same bytes: by default, and with -funwind-tables or -g after
//   -fno-asynchronous-unwind-tables, the unwinder walks through every function to main and libc's start, which the
//   program checks (it prints the seven depths and returns 0); with -fno-asynchronous-unwind-tables alone, it stops at
//   the first of them, and the program prints 1 1 1 1 -1 1 1.
// - Its object (-c) by each path, the same bytes: an .eh_frame of one CIE and an FDE per function the object defines,
//   each a relocation; linked by cc through GNU ld and through lld, and by link-foreign from GCC's driver's line, into
//   programs that walk alike. With -fno-asynchronous-unwind-tables, no .eh_frame.
// - The options' rule (predefines_unwind_tables), on objects, against clang's objects of the same fixture with the same
//   options: those above, -ffreestanding (none) and -fasynchronous-unwind-tables after it, -fexceptions, each order of
//   -fasynchronous-unwind-tables and -fno-asynchronous-unwind-tables (the last one's), -fno-unwind-tables after
//   -funwind-tables (the asynchronous default's tables) and after it with the asynchronous ones off (none), and -g with
//   them off, whose call frame information clang writes to .debug_frame rather than .eh_frame. Clang's object must have
//   an FDE per function exactly where ours has: every function of the fixture calls, so -fexceptions's FDEs, which
//   clang gives only a function that may unwind, are every function's here too.
// - gdb on the -g executable, at every instruction it executes of each function from its first to its ret, stepping
//   over calls, through as many of the function's calls as the fixture's paths need: each backtrace's frames are the
//   function and its callers, and the steps reach every instruction of gdb's disassembly of the function.

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
    "    return sum - 14 * x - 105 + (x > 0 ? frames : -frames);\n"
    "}\n"
    "\n"
    "__int128 wide(__int128 x, int shift)\n"
    "{\n"
    "    __int128 high = x << 64;\n"
    "    __int128 product = high * 3;\n"
    "    __int128 quotient = product / high;\n"
    "    __int128 remainder = (product + 7) % high;\n"
    "    unsigned __int128 shifted = (unsigned __int128)product >> shift;\n"
    "    __int128 frames = depth();\n"
    "    return quotient + remainder - 10 + (__int128)(shifted << shift) - product + frames;\n"
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
    "    int shallow = jump(0);\n"
    "    long pressed = pressure(5);\n"
    "    long negative = pressure(-5);\n"
    "    __int128 short_shift = wide(5, 3);\n"
    "    __int128 long_shift = wide(7, 64);\n"
    "    printf(\"%d %d %d %ld %ld %ld %ld\\n\", baseline, deep, shallow, pressed, negative, (long)short_shift, (long)long_shift);\n"
    "    return baseline >= 3 && deep == baseline + 2 && shallow == baseline + 2 && pressed == baseline + 1 && negative == -(baseline + 1) &&\n"
    "           short_shift == baseline + 1 && long_shift == baseline + 1 ? 0 : 1;\n"
    "}\n";

// An option set of the driver's: whether its output has unwind tables; whether the fixture's executables and objects
// are made with it (`programs`), and whether its object is compared with clang's (`policy`), which writes the call
// frame information to .eh_frame, or with debug information alone to .debug_frame (`debug_frame`).
typedef struct CheckUnwindTestSet CheckUnwindTestSet;
struct CheckUnwindTestSet
{
    const char* name;
    const char* options[3];
    uint32_t count;
    bool tables;
    bool programs;
    bool policy;
    bool debug_frame;
};

static const CheckUnwindTestSet check_unwind_test_sets[] = {
    {
        .name = "default",
        .tables = true,
        .programs = true,
        .policy = true,
    },
    {
        .name = "no-tables",
        .options = { "-fno-asynchronous-unwind-tables" },
        .count = 1,
        .programs = true,
        .policy = true,
    },
    {
        .name = "unwind-tables",
        .options = { "-fno-asynchronous-unwind-tables", "-funwind-tables" },
        .count = 2,
        .tables = true,
        .programs = true,
        .policy = true,
    },
    {
        .name = "debug",
        .options = { "-fno-asynchronous-unwind-tables", "-g" },
        .count = 2,
        .tables = true,
        .programs = true,
        .policy = true,
        .debug_frame = true,
    },
    {
        .name = "freestanding",
        .options = { "-ffreestanding" },
        .count = 1,
        .policy = true,
    },
    {
        .name = "freestanding-asynchronous",
        .options = { "-ffreestanding", "-fasynchronous-unwind-tables" },
        .count = 2,
        .tables = true,
        .policy = true,
    },
    {
        .name = "exceptions",
        .options = { "-fno-asynchronous-unwind-tables", "-fexceptions" },
        .count = 2,
        .tables = true,
        .policy = true,
    },
    {
        .name = "asynchronous-last",
        .options = { "-fno-asynchronous-unwind-tables", "-fasynchronous-unwind-tables" },
        .count = 2,
        .tables = true,
        .policy = true,
    },
    {
        .name = "no-asynchronous-last",
        .options = { "-fasynchronous-unwind-tables", "-fno-asynchronous-unwind-tables" },
        .count = 2,
        .policy = true,
    },
    {
        .name = "no-unwind-tables-last",
        .options = { "-funwind-tables", "-fno-unwind-tables" },
        .count = 2,
        .tables = true,
        .policy = true,
    },
    {
        .name = "no-unwind-tables-only",
        .options = { "-fno-asynchronous-unwind-tables", "-funwind-tables", "-fno-unwind-tables" },
        .count = 3,
        .policy = true,
    },
};

// The driver (/proc/self/exe, or `compiler`) on `source` with the set's options (and -c, or --scalar), into `output`:
// whether it succeeded, its errors in `output`.err.
static bool check_unwind_test_compile(const char* compiler, const char* source, const char* output, const CheckUnwindTestSet* set, bool object, bool scalar)
{
    char errors[PATH_MAX + 8];
    snprintf(errors, sizeof(errors), "%s.err", output);
    char* arguments[12] = { (char*)compiler };
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
// depths that walk through main to libc's start; without them, it returns 1 having printed 1 1 1 1 -1 1 1.
static bool check_unwind_test_run(const char* program, bool tables, Arena* arena)
{
    char report[PATH_MAX + 8];
    snprintf(report, sizeof(report), "%s.out", program);
    char* run[] = { (char*)program, 0 };
    const int status = object_test_run(run, report, "/dev/null");
    const String8 printed = foreign_test_read(arena, report);
    int baseline = 0;
    int deep = 0;
    int shallow = 0;
    long pressed = 0;
    long negative = 0;
    long short_shift = 0;
    long long_shift = 0;
    const bool read = printed.pointer != 0 &&
                      sscanf(printed.pointer, "%d %d %d %ld %ld %ld %ld", &baseline, &deep, &shallow, &pressed, &negative, &short_shift, &long_shift) == 7;
    const bool walked = status == 0 && baseline >= 3 && deep == baseline + 2 && shallow == baseline + 2 && pressed == baseline + 1 && negative == -(baseline + 1) &&
                        short_shift == baseline + 1 && long_shift == baseline + 1;
    const bool stopped = status == 1 && baseline == 1 && deep == 1 && shallow == 1 && pressed == 1 && negative == -1 && short_shift == 1 && long_shift == 1;
    return read && (tables ? walked : stopped);
}

// The records of an object's call frame information section `name` (CIEs, whose id is `cie`, and FDEs), the relocations
// of .rela`name`, and the object's function definitions, from its sections; false if they can't be read.
static bool check_unwind_test_census(String8 object, const char* name, uint32_t cie, uint32_t* cies, uint32_t* fdes, uint32_t* functions, uint32_t* relocations)
{
    const Elf64_Shdr frames = check_debug_test_section(object, name, false);
    const Elf64_Shdr relocated = check_debug_test_section(object, name, true);
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
            *cies += id == cie ? 1 : 0;
            *fdes += id != cie ? 1 : 0;
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

// The most bytes of a function the steps' coverage takes: a bit per byte offset.
#define BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES 8192

// What gdb's transcript of a function's steps says (check_unwind_test_gdb's script): each step's frames, from
// UNWIND_STEP OFFSET to the next, its backtrace whose frames are `names`, the stepped function's first; the function's
// instructions, each "<+N>:" of the disassembly between UNWIND_CODE and UNWIND_CODE_END; and whether the steps reached
// every one of them. With the counts of steps, of instructions and of those the steps reached.
static bool check_unwind_test_steps(const char* text, const char* const* names, uint32_t count, uint32_t* steps, uint32_t* instructions, uint32_t* reached)
{
    uint64_t starts[BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES / 64] = { 0 };
    uint64_t visited[BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES / 64] = { 0 };
    const char* code = text != 0 ? strstr(text, "UNWIND_CODE\n") : 0;
    const char* code_end = code != 0 ? strstr(code, "UNWIND_CODE_END") : 0;
    bool valid = code_end != 0 && strstr(code_end, "UNWIND_DONE") != 0;
    for (const char* at = code; valid && (at = strstr(at, "<+")) != 0 && at < code_end; at += 2)
    {
        char* end = 0;
        const unsigned long offset = strtoul(at + 2, &end, 10);
        valid = end != at + 2 && end[0] == '>' && end[1] == ':' && offset < BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES;
        starts[valid ? offset / 64 : 0] |= valid ? 1ull << (offset % 64) : 0;
    }
    const char* block = valid ? strstr(code_end, "UNWIND_STEP ") : 0;
    *steps = 0;
    while (valid && block != 0)
    {
        const char* next = strstr(block + 1, "UNWIND_STEP ");
        const char* end = next != 0 ? next : strstr(block, "UNWIND_DONE");
        char* number_end = 0;
        const unsigned long offset = strtoul(block + sizeof("UNWIND_STEP ") - 1, &number_end, 10);
        valid = number_end != block + sizeof("UNWIND_STEP ") - 1 && *number_end == '\n' && offset < BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES;
        visited[valid ? offset / 64 : 0] |= valid ? 1ull << (offset % 64) : 0;
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
    *instructions = 0;
    *reached = 0;
    for (uint32_t word = 0; word < BUSTER_ARRAY_LENGTH(starts); word += 1)
    {
        *instructions += (uint32_t)__builtin_popcountll(starts[word]);
        *reached += (uint32_t)__builtin_popcountll(starts[word] & visited[word]);
        // A step at no instruction's start would be gdb's or the disassembly's mistake: refused too.
        valid = valid && (visited[word] & ~starts[word]) == 0;
    }
    return valid && *steps != 0 && *instructions != 0 && *reached == *instructions;
}

// gdb at every instruction of `names[0]`, the function, that the -g executable `program` runs in the function's first
// `calls` calls, from its first instruction to its ret, stepping over calls (see the file's comment).
static void check_unwind_test_gdb(CheckDebugTestTally* tally, const char* program, String8 file, const char* const* names, uint32_t count, uint32_t calls,
                                  Arena* arena)
{
    const char* function = names[0];
    const uint64_t size = check_unwind_test_size(file, function);
    char script[PATH_MAX + 32];
    char report[PATH_MAX + 32];
    snprintf(script, sizeof(script), "%s.%s.gdb", program, function);
    snprintf(report, sizeof(report), "%s.%s.steps", program, function);
    char text[1536];
    const int length = snprintf(text, sizeof(text),
                                "set pagination off\nset confirm off\nset width 0\nset debuginfod enabled off\nbreak *%s\nrun\n"
                                "printf \"UNWIND_CODE\\n\"\ndisassemble %s\nprintf \"UNWIND_CODE_END\\n\"\nset $call = 0\nwhile $call < %u\n"
                                "while $pc >= (char*)%s && $pc < (char*)%s + %" PRIu64 "\nprintf \"UNWIND_STEP %%ld\\n\", (long)$pc - (long)(char*)%s\n"
                                "bt %u\nnexti\nend\nset $call = $call + 1\nif $call < %u\ncontinue\nend\nend\nprintf \"UNWIND_DONE\\n\"\nkill\n",
                                function, function, calls, function, function, size, function, count, calls);
    const bool written = size != 0 && size <= BUSTER_CHECK_UNWIND_TEST_FUNCTION_BYTES && length > 0 && (uint32_t)length < sizeof(text) &&
                         foreign_test_write(script, text, (uint64_t)length);
    check_debug_test_expect(tally, written, "write gdb's script for a function's every instruction (its size from .symtab)", script);
    char* arguments[] = { (char*)"gdb", (char*)"-nx", (char*)"-batch", (char*)"-x", script, (char*)program, 0 };
    const bool ran = written && object_test_run(arguments, report, "/dev/null") == 0;
    const String8 transcript = ran ? foreign_test_read(arena, report) : (String8){ 0 };
    uint32_t steps = 0;
    uint32_t instructions = 0;
    uint32_t reached = 0;
    const bool walked = ran && check_unwind_test_steps(transcript.pointer, names, count, &steps, &instructions, &reached);
    char detail[PATH_MAX + 96];
    snprintf(detail, sizeof(detail), "%s, %u steps, %u of %u instructions", report, steps, reached, instructions);
    check_debug_test_expect(tally, walked, "gdb's backtrace at every instruction of the function gives it and its callers", detail);
}

// The options' rule (see the file's comment): the set's object by the driver, its .eh_frame one CIE and an FDE per
// function, each relocated, or none; and clang's object of the fixture with the same options, an FDE per function in
// its call frame information's section exactly where the driver's has them.
static void check_unwind_test_policy(CheckDebugTestTally* tally, const char* source, const char* directory, const CheckUnwindTestSet* set, Arena* arena)
{
    char ours[PATH_MAX + 48];
    char theirs[PATH_MAX + 48];
    snprintf(ours, sizeof(ours), "%s/dbg_unwind.%s.policy.o", directory, set->name);
    snprintf(theirs, sizeof(theirs), "%s/dbg_unwind.%s.clang.o", directory, set->name);
    const bool made = check_unwind_test_compile("/proc/self/exe", source, ours, set, true, false);
    const bool compiled = check_unwind_test_compile("clang", source, theirs, set, true, false);
    check_debug_test_expect(tally, made, "the driver writes the fixture's object with the set's options", ours);
    check_debug_test_expect(tally, compiled, "clang compiles the fixture with the set's options", theirs);
    const String8 object = made ? foreign_test_read(arena, ours) : (String8){ 0 };
    const String8 reference = compiled ? foreign_test_read(arena, theirs) : (String8){ 0 };
    uint32_t cies = 0;
    uint32_t fdes = 0;
    uint32_t functions = 0;
    uint32_t relocations = 0;
    uint32_t clang_cies = 0;
    uint32_t clang_fdes = 0;
    uint32_t clang_functions = 0;
    uint32_t clang_relocations = 0;
    const bool counted = object.pointer != 0 && check_unwind_test_census(object, ".eh_frame", 0, &cies, &fdes, &functions, &relocations);
    const bool clang_counted = reference.pointer != 0 &&
                               check_unwind_test_census(reference, set->debug_frame ? ".debug_frame" : ".eh_frame", set->debug_frame ? UINT32_MAX : 0, &clang_cies,
                                                        &clang_fdes, &clang_functions, &clang_relocations);
    const bool ours_right = counted && functions >= 6 &&
                            (set->tables ? cies == 1 && fdes == functions && relocations == fdes : cies == 0 && fdes == 0 && relocations == 0);
    check_debug_test_expect(tally, ours_right, set->tables ? "the options give the object an FDE per function" : "the options give the object no .eh_frame", ours);
    check_debug_test_expect(tally, ours_right && clang_counted && clang_functions == functions && clang_fdes == fdes,
                            set->debug_frame ? "clang's object has as many FDEs in .debug_frame as ours in .eh_frame" : "clang's object has as many FDEs as ours", theirs);
}

// The fixture's executables and objects, the options' rule, and gdb on its -g executable (see the file's comment).
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
        // The rule's sets, against clang's objects.
        if (set->policy)
        {
            check_unwind_test_policy(tally, source, directory, set, arena);
        }
        // The executable, then the object, each by both paths.
        for (uint32_t object = 0; set->programs && object < 2; object += 1)
        {
            char outputs[2][PATH_MAX + 32];
            String8 files[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                snprintf(outputs[path], sizeof(outputs[path]), "%s/dbg_unwind.%s%s%s", directory, set->name, path == 0 ? "" : ".scalar", object != 0 ? ".o" : "");
                const bool made = check_unwind_test_compile("/proc/self/exe", source, outputs[path], set, object != 0, path != 0);
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
            const bool census = object != 0 && files[0].pointer != 0 && check_unwind_test_census(files[0], ".eh_frame", 0, &cies, &fdes, &functions, &relocations);
            if (object != 0 && set->tables)
            {
                check_debug_test_expect(tally, census && functions >= 6 && cies == 1 && fdes == functions && relocations == fdes,
                                        "the object's .eh_frame is one CIE and an FDE per function, each relocated", outputs[0]);
            }
            else if (object != 0)
            {
                check_debug_test_expect(tally, census && functions >= 6 && cies == 0 && fdes == 0 && relocations == 0, "without unwind tables, the object has no .eh_frame",
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
    // gdb at every instruction of each function but main, of the -g executable: jump's two calls take both of its
    // labels and pressure's both arms of its last ?:; wide has no branch, and its first call runs all of it.
    const String8 file = debug_program[0] != 0 ? foreign_test_read(arena, debug_program) : (String8){ 0 };
    check_debug_test_expect(tally, file.pointer != 0, "the -g executable for gdb's steps", debug_program);
    static const char* const depth[] = { "depth", "main" };
    static const char* const pressure[] = { "pressure", "main" };
    static const char* const wide[] = { "wide", "main" };
    static const char* const vla[] = { "vla", "jump", "main" };
    static const char* const jump[] = { "jump", "main" };
    if (file.pointer != 0)
    {
        check_unwind_test_gdb(tally, debug_program, file, depth, BUSTER_ARRAY_LENGTH(depth), 1, arena);
        check_unwind_test_gdb(tally, debug_program, file, pressure, BUSTER_ARRAY_LENGTH(pressure), 2, arena);
        check_unwind_test_gdb(tally, debug_program, file, wide, BUSTER_ARRAY_LENGTH(wide), 1, arena);
        check_unwind_test_gdb(tally, debug_program, file, vla, BUSTER_ARRAY_LENGTH(vla), 1, arena);
        check_unwind_test_gdb(tally, debug_program, file, jump, BUSTER_ARRAY_LENGTH(jump), 2, arena);
    }
}
