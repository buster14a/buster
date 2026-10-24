// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-streaming [--seed N] [--units N] [--cross-check] [--keep]: streaming.c's checks. Programs of generated units,
// in both orders, with the default chunks and with 4 KB ones (many chunks, and forward fields across their boundaries),
// written by every schedule: the files must be byte for byte the same, with the build ID the headers' note holds, and
// each must run and print the fold of every unit's entry as tier0_test.c's interpreter computes it; so must the direct
// path's executable of the same code (executable_write on 1 and 3 lanes, in lld's order). The test reads each file back:
// the segments in the data-first order (R, RW, RX, R), the entry at _start's symbol, .eh_frame_hdr's table sorted and
// as long as the functions, each FDE's range a function's. With --cross-check, which needs readelf, gdb and perf:
// readelf reads every file without a warning; gdb stops in the program's functions, unwinds through them to _start
// with its .eh_frame and gives each frame its function's line in streamed.c from the DWARF; and perf's samples, with
// DWARF call graphs, name the program's functions, reach _start and give the functions' lines.
// main.c includes this file after tier0_test.c (its interpreter), unwind_native_test.c and streaming.c.

typedef struct StreamTestTally StreamTestTally;
struct StreamTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool stream_test_expect(StreamTestTally* tally, bool condition, const char* what, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 20)
        {
            printf("streaming: %s%s%s\n", what, detail != 0 ? ": " : "", detail != 0 ? detail : "");
        }
    }
    return condition;
}

// The output every unit's entry folds into, as _start folds it: r = rol(r, 5) ^ result, each unit's globals fresh.
static bool stream_test_expected(const StreamProgram* program, uint64_t* expected)
{
    static uint8_t globals[BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64];
    uint64_t fold = 0;
    bool fault = false;
    for (uint32_t u = 0; !fault && u < program->unit_count; u += 1)
    {
        const StreamUnit* unit = &program->units[u];
        const Tier0TestData data = tier0_test_layout(&unit->program);
        tier0_test_initial(&unit->program, &data, globals);
        const uint64_t result = tier0_test_interpret(&unit->program, &unit->functions, &data, globals, unit->inputs, &fault);
        fold = (fold << 5 | fold >> 59) ^ result;
    }
    *expected = fold;
    return !fault;
}

// Runs `path`, which must exit with 0 having printed exactly `expected`'s 8 bytes.
static bool stream_test_run(const char* directory, const char* path, uint64_t expected)
{
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(output, sizeof(output), "%s/run.out", directory);
    snprintf(errors, sizeof(errors), "%s/run.err", directory);
    char* arguments[] = { (char*)path, 0 };
    const int status = object_test_run(arguments, output, errors);
    FILE* file = status == 0 ? fopen(output, "rb") : 0;
    uint64_t printed = 0;
    uint8_t extra = 0;
    const bool read = file != 0 && fread(&printed, 1, 8, file) == 8 && fread(&extra, 1, 1, file) == 0;
    if (file != 0)
    {
        fclose(file);
    }
    return read && printed == expected;
}

static bool stream_test_read(const char* path, Arena* arena, String8* bytes)
{
    *bytes = object_test_read_file(arena, path);
    return bytes->pointer != 0;
}

// The file's structure: the ELF header's entry and counts, the segments in the data-first order, the build ID note's
// hash, and .eh_frame_hdr's table: as many entries as described functions, sorted, each FDE at its address with its
// function's start.
static bool stream_test_structure(const StreamProgram* program, const StreamResult* result, String8 file)
{
    const uint8_t* bytes = (const uint8_t*)file.pointer;
    Elf64_Ehdr header;
    bool valid = file.length >= BUSTER_STREAM_PAGE_BYTES && file.length == result->file_size;
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = header.e_type == ET_EXEC && header.e_machine == EM_X86_64 && header.e_phnum == BUSTER_STREAM_HEADERS && header.e_shnum == BUSTER_STREAM_SECTIONS &&
                header.e_shoff + BUSTER_STREAM_SECTIONS * sizeof(Elf64_Shdr) == file.length;
    }
    Elf64_Phdr segments[BUSTER_STREAM_HEADERS];
    if (valid)
    {
        memcpy(segments, bytes + header.e_phoff, sizeof(segments));
        valid = segments[0].p_type == PT_LOAD && segments[0].p_flags == PF_R && segments[1].p_type == PT_LOAD && segments[1].p_flags == (PF_R | PF_W) &&
                segments[2].p_type == PT_LOAD && segments[2].p_flags == (PF_R | PF_X) && segments[3].p_type == PT_LOAD && segments[3].p_flags == PF_R &&
                segments[0].p_vaddr < segments[1].p_vaddr && segments[1].p_vaddr + segments[1].p_memsz <= segments[2].p_vaddr &&
                segments[2].p_vaddr + segments[2].p_memsz <= segments[3].p_vaddr && segments[1].p_filesz == program->data_size &&
                segments[2].p_filesz == result->text_size && header.e_entry >= segments[2].p_vaddr && header.e_entry < segments[2].p_vaddr + segments[2].p_memsz &&
                segments[4].p_type == PT_NOTE && segments[5].p_type == PT_GNU_EH_FRAME && segments[6].p_type == PT_GNU_STACK;
    }
    if (valid)
    {
        valid = memcmp(bytes + segments[4].p_offset + sizeof(Elf64_Nhdr) + 4, result->build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0;
        valid = valid && memcmp(bytes + segments[1].p_offset, program->data, program->data_size) == 0;
    }
    if (valid)
    {
        const uint8_t* table = bytes + segments[5].p_offset;
        uint32_t count = 0;
        memcpy(&count, table + 8, 4);
        valid = table[0] == 1 && table[1] == 0x1b && table[2] == 0x03 && table[3] == 0x3b && count == program->function_count - 1 &&
                segments[5].p_filesz == BUSTER_STREAM_EH_FRAME_HDR_BYTES(count);
        int32_t previous = INT32_MIN;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            int32_t entry[2];
            memcpy(entry, table + 12 + 8 * (uint64_t)i, sizeof(entry));
            const uint64_t function = segments[5].p_vaddr + (uint64_t)(int64_t)entry[0];
            const uint64_t fde = segments[5].p_vaddr + (uint64_t)(int64_t)entry[1];
            int32_t initial = 0;
            const uint64_t fde_offset = segments[3].p_offset + (fde - segments[3].p_vaddr);
            valid = entry[0] > previous && fde >= segments[3].p_vaddr && fde + 16 <= segments[3].p_vaddr + segments[3].p_memsz && function >= segments[2].p_vaddr &&
                    function < segments[2].p_vaddr + segments[2].p_memsz;
            if (valid)
            {
                memcpy(&initial, bytes + fde_offset + 8, 4);
                valid = fde + 8 + (uint64_t)(int64_t)initial == function;
            }
            previous = entry[0];
        }
    }
    return valid;
}

// Whether `name` (up to a space, a newline or the end) is one of the program's functions' names, u<unit>_f<index>.
static bool stream_test_function_name(const char* name)
{
    uint32_t at = name[0] == 'u' ? 1 : 0;
    const uint32_t unit_digits = at;
    while (at != 0 && name[at] >= '0' && name[at] <= '9')
    {
        at += 1;
    }
    bool valid = at > unit_digits && name[at] == '_' && name[at + 1] == 'f';
    at += valid ? 2 : 0;
    const uint32_t index_digits = at;
    while (valid && name[at] >= '0' && name[at] <= '9')
    {
        at += 1;
    }
    return valid && at > index_digits && (name[at] == ' ' || name[at] == '\n' || name[at] == 0);
}

// The function a gdb frame line names: after " in " when gdb gives the address, else after the frame's number.
static const char* stream_test_frame_name(const char* line)
{
    const char* end = strchr(line, '\n');
    const char* in = strstr(line, " in ");
    const char* name = in != 0 && (end == 0 || in < end) ? in + 4 : line + 2;
    while (*name == ' ')
    {
        name += 1;
    }
    return name;
}

// Whether a gdb frame line's " at ./streamed.c:LINE" (the unit's directory, then its file), if it has one, is the line
// of the function it names: function LINE - 1's, in the DWARF's order. `found` says whether it had one.
static bool stream_test_frame_line(const StreamProgram* program, const char* line, const char* name, bool* found)
{
    const char* end = strchr(line, '\n');
    const char* at = strstr(line, " at ./streamed.c:");
    *found = at != 0 && (end == 0 || at < end);
    const uint64_t number = *found ? strtoull(at + 17, 0, 10) : 0;
    bool same = !*found;
    if (*found && number >= 1 && number <= program->function_count)
    {
        const char* expected = program->strtab + program->name_offsets[number - 1];
        const uint64_t length = strlen(expected);
        same = strncmp(name, expected, length) == 0 && name[length] == ' ';
    }
    return same;
}

// The consumers on a streamed file: readelf without warnings, gdb's backtraces through the program's functions to
// _start with their lines, and perf's samples with DWARF call graphs that name its functions, reach _start and give
// their lines.
static void stream_test_consumers(StreamTestTally* tally, const StreamProgram* program, const char* directory, const char* path, Arena* arena)
{
    char output[PATH_MAX];
    char errors[PATH_MAX];
    char commands[PATH_MAX];
    char data[PATH_MAX];
    snprintf(output, sizeof(output), "%s/consumer.out", directory);
    snprintf(errors, sizeof(errors), "%s/consumer.err", directory);
    snprintf(commands, sizeof(commands), "%s/check.gdb", directory);
    snprintf(data, sizeof(data), "%s/perf.data", directory);
    char* readelf[] = { "readelf", "-W", "-a", (char*)path, 0 };
    const bool read = object_test_run(readelf, output, errors) == 0;
    const String8 warnings = read ? object_test_read_file(arena, errors) : (String8){ 0 };
    stream_test_expect(tally, read && warnings.pointer != 0 && warnings.length == 0, "readelf reads the streamed file without a warning", path);
    // gdb: a breakpoint in every unit's functions, and three stops' backtraces.
    FILE* script = fopen(commands, "w");
    bool written = script != 0;
    if (script != 0)
    {
        fputs("set pagination off\nset confirm off\nset debuginfod enabled off\nrbreak ^u[0-9]*_f[0-9]*$\nrun\n", script);
        for (uint32_t stop = 0; stop < 24; stop += 1)
        {
            fputs("printf \"STREAM_STOP\\n\"\nbt\ncontinue\n", script);
        }
        fputs("quit\n", script);
        written = fclose(script) == 0;
    }
    char* gdb[] = { "gdb", "-nx", "-batch", "-x", commands, (char*)path, 0 };
    const bool ran = written && object_test_run(gdb, output, errors) == 0;
    const String8 text = ran ? object_test_read_file(arena, output) : (String8){ 0 };
    // Every stop's backtrace is program functions down to _start, and some stop's has two of them above _start: the
    // unwinder went through a Tier 0 frame by its FDE.
    bool deep = false;
    bool consistent = true;
    bool lines_right = true;
    uint32_t lines = 0;
    uint32_t stops = 0;
    const char* cursor = text.pointer;
    while (cursor != 0 && (cursor = strstr(cursor, "STREAM_STOP\n")) != 0)
    {
        const char* next = strstr(cursor + 1, "STREAM_STOP\n");
        uint32_t frames = 0;
        bool program_frames = true;
        bool ends_at_start = false;
        const char* line = strchr(cursor, '\n');
        while (line != 0 && (next == 0 || line < next))
        {
            line += 1;
            if (line[0] == '#' && line[1] >= '0' && line[1] <= '9')
            {
                const char* name = stream_test_frame_name(line);
                ends_at_start = strncmp(name, "_start ", 7) == 0;
                program_frames = program_frames && (ends_at_start || stream_test_function_name(name));
                bool found = false;
                lines_right = lines_right && stream_test_frame_line(program, line, name, &found);
                lines += found ? 1 : 0;
                frames += 1;
            }
            line = strchr(line, '\n');
        }
        consistent = consistent && frames >= 2 && program_frames && ends_at_start;
        deep = deep || frames >= 3;
        stops += 1;
        cursor += 1;
    }
    stream_test_expect(tally, ran && stops >= 2 && consistent && deep, "gdb stops in the program's functions and unwinds through them to _start", path);
    // Every frame's line is its function's, and the frames have lines: gdb read the DWARF at the data-first addresses.
    stream_test_expect(tally, ran && lines_right && lines >= stops, "gdb gives each frame its function's line in streamed.c", path);
    if (!(ran && stops >= 2 && consistent && deep && lines_right && lines >= stops) && text.pointer != 0)
    {
        printf("%.*s\n", (int)(text.length < 4000 ? text.length : 4000), text.pointer);
    }
    char* record[] = { "perf", "record", "-q", "-m", "4M", "-e", "cycles:u", "-c", "20000", "--call-graph", "dwarf", "-o", data, "--", (char*)path, 0 };
    char* report[] = { "perf", "script", "-i", data, "-F", "ip,sym,srcline", 0 };
    const bool recorded = object_test_run(record, "/dev/null", errors) == 0;
    const bool scripted = recorded && object_test_run(report, output, errors) == 0;
    const String8 samples = scripted ? object_test_read_file(arena, output) : (String8){ 0 };
    // perf script's lines are an address and a symbol, each with its source line on the line after it: some name a
    // program function, some _start, a caller, and some give a line of streamed.c.
    bool named = false;
    bool reached = false;
    bool sourced = false;
    const char* sample = samples.pointer;
    while (sample != 0 && *sample != 0)
    {
        const char* end = strchr(sample, '\n');
        const char* source = strstr(sample, "streamed.c:");
        sourced = sourced || (source != 0 && (end == 0 || source < end));
        const char* symbol = sample;
        while (*symbol == ' ' || *symbol == '\t')
        {
            symbol += 1;
        }
        while (*symbol != 0 && *symbol != ' ' && *symbol != '\n')
        {
            symbol += 1;
        }
        while (*symbol == ' ')
        {
            symbol += 1;
        }
        named = named || stream_test_function_name(symbol);
        reached = reached || strncmp(symbol, "_start", 6) == 0;
        sample = strchr(sample, '\n');
        sample = sample != 0 ? sample + 1 : 0;
    }
    stream_test_expect(tally, scripted && named && reached, "perf's samples name the program's functions and reach _start", path);
    stream_test_expect(tally, scripted && sourced, "perf's samples give the functions' lines in streamed.c", path);
}

static bool stream_test_same(String8 left, String8 right)
{
    return left.pointer != 0 && right.pointer != 0 && left.length == right.length && memcmp(left.pointer, right.pointer, left.length) == 0;
}

// One program, in both orders, with `chunk_bytes`: every schedule's file, the direct path's, and their runs.
static void stream_test_program(StreamTestTally* tally, const char* directory, const StreamProgram* program, uint64_t chunk_bytes, bool cross_check, Arena* arena)
{
    uint64_t expected = 0;
    const bool predicted = stream_test_expected(program, &expected);
    stream_test_expect(tally, predicted, "the interpreter runs every unit's entry", 0);
    static const char* const schedules[STREAM_SCHEDULES] = { "end", "stream", "writer" };
    static const char* const orders[2] = { "callees", "callers" };
    for (uint32_t order = 0; predicted && order < 2; order += 1)
    {
        String8 reference = { 0 };
        StreamResult first;
        memset(&first, 0, sizeof(first));
        for (uint32_t schedule = 0; schedule < STREAM_SCHEDULES; schedule += 1)
        {
            char path[PATH_MAX];
            char detail[PATH_MAX + 64];
            snprintf(path, sizeof(path), "%s/stream-%s-%s-%" PRIu64, directory, orders[order], schedules[schedule], chunk_bytes);
            const StreamOptions options = {
                .path = { .pointer = path, .length = strlen(path) },
                .schedule = (StreamSchedule)schedule,
                .order = (StreamOrder)order,
                .chunk_bytes = chunk_bytes,
            };
            StreamResult result;
            const bool wrote = stream_write(program, &options, &result);
            snprintf(detail, sizeof(detail), "%s (error %d, function %u, errno %d)", path, (int)result.error, result.function, result.system_error);
            if (stream_test_expect(tally, wrote, "the schedule writes the file", detail))
            {
                String8 bytes;
                const bool read = stream_test_read(path, arena, &bytes);
                stream_test_expect(tally, read && stream_test_structure(program, &result, bytes), "the file's structure", path);
                stream_test_expect(tally, stream_test_run(directory, path, expected), "the file runs and prints the interpreter's fold", path);
                if (schedule == STREAM_SCHEDULE_END)
                {
                    reference = bytes;
                    first = result;
                }
                else
                {
                    stream_test_expect(tally, stream_test_same(bytes, reference) && memcmp(result.build_id, first.build_id, sizeof(first.build_id)) == 0,
                                       "the streamed file is the file written at the end, byte for byte", path);
                    // Streaming happened: a chunk was written before the last function compiled, when there are several.
                    stream_test_expect(tally, result.chunks < 3 || result.chunks_streamed != 0, "chunks were written while the program compiled", path);
                }
                stream_test_expect(tally, order == STREAM_ORDER_CALLEES ? result.forward_fixups == 0 || schedule == STREAM_SCHEDULE_END :
                                                                         result.forward_fixups != 0 || schedule == STREAM_SCHEDULE_END,
                                   "forward fields only in the callers' order", path);
                if (cross_check && schedule == STREAM_SCHEDULE_STREAM && order == STREAM_ORDER_CALLEES && chunk_bytes == BUSTER_STREAM_CHUNK_BYTES)
                {
                    stream_test_consumers(tally, program, directory, path, arena);
                }
            }
        }
        for (uint32_t lanes = 1; lanes <= 3; lanes += 2)
        {
            char path[PATH_MAX];
            char detail[PATH_MAX + 64];
            snprintf(path, sizeof(path), "%s/link-%s-%u", directory, orders[order], lanes);
            StreamLinkResult result;
            const bool wrote = stream_write_link(program, (String8){ .pointer = path, .length = strlen(path) }, (StreamOrder)order, lanes, &result);
            snprintf(detail, sizeof(detail), "%s (error %d, link %d, executable %d)", path, (int)result.error, (int)result.link_error, (int)result.written.error);
            stream_test_expect(tally, wrote, "the direct path writes the same code", detail);
            stream_test_expect(tally, wrote && stream_test_run(directory, path, expected), "the direct path's file runs and prints the interpreter's fold", path);
        }
    }
}

static int run_streaming_tests(int argc, char** argv)
{
    uint64_t seed = 88;
    uint32_t units = 40;
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            seed = strtoull(argv[i], 0, 10);
        }
        else if (strcmp(argv[i], "--units") == 0 && i + 1 < argc)
        {
            i += 1;
            units = (uint32_t)strtoul(argv[i], 0, 10);
            valid = units != 0 && units <= 4096;
        }
        else
        {
            cross_check = cross_check || strcmp(argv[i], "--cross-check") == 0;
            keep = keep || strcmp(argv[i], "--keep") == 0;
            valid = strcmp(argv[i], "--cross-check") == 0 || strcmp(argv[i], "--keep") == 0;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: test-streaming [--seed N] [--units N] [--cross-check] [--keep]\n");
    }
    StreamTestTally tally = { 0 };
    char directory[PATH_MAX];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-streaming-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    valid = valid && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize() && mkdtemp(directory) != 0;
    Tier0Tables tables;
    memset(&tables, 0, sizeof(tables));
    valid = valid && tier0_tables_open(&tables);
    Arena arena = arena_reserve(1ull << 36);
    Arena scratch = arena_reserve(1ull << 32);
    valid = valid && arena.base != 0 && scratch.base != 0;
    // A small program and a larger one, each in a shape with calls, loops, floating values, pointers, objects and
    // calls through pointers.
    const Tier0ProgramOptions options = {
        .functions = 8,
        .statements = 16,
        .expression_nodes = 10,
        .calls = true,
        .loops = true,
        .floating = true,
        .pointers = true,
        .defined = true,
        .objects = true,
        .indirect = true,
    };
    const uint32_t sizes[2] = { 3, units };
    for (uint32_t s = 0; valid && s < 2; s += 1)
    {
        StreamProgram program;
        const uint64_t mark = arena.length;
        const bool generated = stream_program_generate(&program, &arena, &scratch, &tables, seed + 1000ull * s, sizes[s], options);
        if (stream_test_expect(&tally, generated, "the program generates", 0))
        {
            stream_test_program(&tally, directory, &program, BUSTER_STREAM_CHUNK_BYTES, cross_check && s == 1, &arena);
            stream_test_program(&tally, directory, &program, 4096, false, &arena);
        }
        arena_pop_to(&arena, mark);
    }
    if (valid && !keep && tally.failures == 0)
    {
        char* remove[] = { "rm", "-r", directory, 0 };
        object_test_run(remove, "/dev/null", "/dev/null");
    }
    else if (valid)
    {
        printf("streaming files: %s\n", directory);
    }
    printf("streaming: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
    tier0_tables_close(&tables);
    arena_release(&scratch);
    arena_release(&arena);
    return valid && tally.failures == 0 ? 0 : 1;
}
