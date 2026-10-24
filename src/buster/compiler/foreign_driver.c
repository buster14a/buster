// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Raw built-in linker command. Driver options are removed without reordering foreign_arguments' inputs, archive
// controls or library-search controls. The caller supplies startup objects and libraries in their desired order;
// runtime discovery supplies only the default interpreter for a PIE. The default is a static executable at _start.
//
// Conversion precedes optional section GC and literal merging, then the existing lane-parallel writer. The original
// Foreign and all arenas remain live through writing. Entry lookup uses Foreign's canonical names again after GC's
// remapping. No source/unwind offsets are consumed after literal merging; its offset map remains live until writing.
// Unsupported literal expressions retain the unchanged input. Other failures stop before the writer is invoked.
// Named semantic symbols are selected from the final Link, after all maps. Each lane counts and then compacts its
// ID range into one checked arena allocation; the ordered prefixes preserve IDs across lanes. The exact twin runs
// on lane zero. All name loads are logically bounded, and the descriptor lives through writing. -s skips selection.
// --time reports each stage's wall time on standard error after the link, as wild's --time and mold's --perf do: the
// resolution's phases (the slowest lane's), the conversion, the optional passes and the writer's phases (lane 0's).

typedef struct ForeignDriver ForeignDriver;
struct ForeignDriver
{
    ForeignCommand command;
    String8 output;
    String8 entry;
    String8 interpreter;
    RuntimeKind kind;
    ForeignPath path;
    uint32_t lanes;
    bool gc;
    bool merge_strings;
    bool dedup_unwind;
    bool strip_all;
    bool time;
    bool help;
};

typedef struct ForeignDriverWrite ForeignDriverWrite;
struct ForeignDriverWrite
{
    Link* link;
    Arena* arena;
    ExecutableOptions options;
    ExecutableSymbols symbols;
    SymbolId* selected;
    uint32_t counts[BUSTER_LANE_MAX_COUNT];
    uint32_t offsets[BUSTER_LANE_MAX_COUNT];
    ExecutableResult result;
    bool strip_all;
    bool symbols_ready;
    bool succeeded;
};

static void foreign_driver_usage(void)
{
    fprintf(stderr, "usage: link-foreign [-o OUTPUT] [-e ENTRY] [-static|-pie] [--dynamic-linker PATH]\n"
                    "                    [--gc-sections|--no-gc-sections] [--merge-strings|--no-merge-strings]\n"
                    "                    [--dedup-unwind|--no-dedup-unwind]\n"
                    "                    [-s|--strip-all] [--scalar] [--lanes N] [--time] INPUT... [-L DIR] [-l LIB] [-u NAME]\n"
                    "Defaults: static executable, a.out, entry _start, one lane; GC, string merging and unwind deduplication off.\n"
                    "Supply startup objects and runtime libraries explicitly, in link order.\n"
                    "Use -pie for C runtime startup; static output currently supports no-libc inputs.\n"
                    "Shared-library and static-PIE outputs are not supported by this command yet.\n"
                    "Static output searches archives by default; PIE output prefers shared libraries.\n"
                    "Named regular symbols are emitted by default; -s/--strip-all omits them, preserving dynamic symbols.\n"
                    "Archive groups, --whole-archive, --as-needed, -Bstatic and -Bdynamic retain input order.\n"
                    "--time reports each stage's time on standard error.\n");
}

static bool foreign_driver_count(const char* text, uint32_t* count)
{
    uint32_t value = 0;
    bool valid = text != 0 && text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        valid = text[i] >= '0' && text[i] <= '9' && value <= BUSTER_LANE_MAX_COUNT / 10;
        if (valid)
        {
            value = value * 10 + (uint32_t)(text[i] - '0');
            valid = value <= BUSTER_LANE_MAX_COUNT;
        }
    }
    valid = valid && value != 0;
    *count = valid ? value : 0;
    return valid;
}

static bool foreign_driver_arguments(ForeignDriver* driver, int argc, char** argv, Arena* arena)
{
    *driver = (ForeignDriver){
        .output = foreign_string("a.out"),
        .entry = foreign_string("_start"),
        .kind = RUNTIME_KIND_STATIC,
        .path = FOREIGN_PATH_VECTOR,
        .lanes = 1,
    };
    char** inputs = argc >= 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)argc + 2) : 0;
    bool valid = inputs != 0;
    uint32_t count = 1;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool foreign_value = strcmp(argument, "-L") == 0 || strcmp(argument, "-l") == 0 || strcmp(argument, "-u") == 0 || strcmp(argument, "--undefined") == 0;
        const bool output = strcmp(argument, "-o") == 0 || strcmp(argument, "--output") == 0 || foreign_starts_with(argument, "--output=") ||
                            (foreign_starts_with(argument, "-o") && argument[2] != 0);
        const bool entry = strcmp(argument, "-e") == 0 || strcmp(argument, "--entry") == 0 || foreign_starts_with(argument, "--entry=");
        const bool interpreter = strcmp(argument, "-dynamic-linker") == 0 || strcmp(argument, "--dynamic-linker") == 0 || foreign_starts_with(argument, "--dynamic-linker=");
        const bool lanes = strcmp(argument, "--lanes") == 0 || foreign_starts_with(argument, "--lanes=");
        if (foreign_value)
        {
            inputs[count] = argv[i];
            count += 1;
            if (i + 1 < argc)
            {
                i += 1;
                inputs[count] = argv[i];
                count += 1;
            }
        }
        else if (output || entry || interpreter || lanes)
        {
            const char* equals = strchr(argument, '=');
            const char* value = output && argument[0] == '-' && argument[1] == 'o' && argument[2] != 0 ? argument + 2 : equals != 0 ? equals + 1 : 0;
            if (value == 0 && i + 1 < argc)
            {
                i += 1;
                value = argv[i];
            }
            valid = value != 0 && value[0] != 0;
            if (valid && output)
            {
                driver->output = foreign_string(value);
            }
            else if (valid && entry)
            {
                driver->entry = foreign_string(value);
            }
            else if (valid && interpreter)
            {
                driver->interpreter = foreign_string(value);
            }
            else if (valid)
            {
                valid = foreign_driver_count(value, &driver->lanes);
            }
            if (!valid)
            {
                fprintf(stderr, "link-foreign: invalid or missing value for %s\n", argument);
            }
        }
        else if (strcmp(argument, "-pie") == 0)
        {
            driver->kind = RUNTIME_KIND_DYNAMIC;
        }
        else if (strcmp(argument, "-static") == 0)
        {
            driver->kind = RUNTIME_KIND_STATIC;
            inputs[count] = argv[i];
            count += 1;
        }
        else if (strcmp(argument, "--gc-sections") == 0 || strcmp(argument, "--no-gc-sections") == 0)
        {
            driver->gc = strcmp(argument, "--gc-sections") == 0;
        }
        else if (strcmp(argument, "--merge-strings") == 0 || strcmp(argument, "--no-merge-strings") == 0)
        {
            driver->merge_strings = strcmp(argument, "--merge-strings") == 0;
        }
        else if (strcmp(argument, "--dedup-unwind") == 0 || strcmp(argument, "--no-dedup-unwind") == 0)
        {
            driver->dedup_unwind = strcmp(argument, "--dedup-unwind") == 0;
        }
        else if (strcmp(argument, "--scalar") == 0)
        {
            driver->path = FOREIGN_PATH_EXACT;
        }
        else if (strcmp(argument, "--build-id") == 0 || strcmp(argument, "--eh-frame-hdr") == 0 || strcmp(argument, "--hash-style=gnu") == 0 ||
                 strcmp(argument, "-melf_x86_64") == 0)
        {
            // What every output has already, as GCC's and clang's drivers ask GNU ld for it: a build ID, .eh_frame_hdr,
            // .gnu.hash, and x86-64.
        }
        else if ((strcmp(argument, "-m") == 0 || strcmp(argument, "-z") == 0) && i + 1 < argc)
        {
            // The emulation, x86-64's alone, and the -z keywords that describe what the output does already: its
            // imports bound at load (now), RELRO over the GOT (relro) and a stack that doesn't execute (noexecstack).
            i += 1;
            const char* keyword = argv[i];
            valid = strcmp(argument, "-m") == 0 ? strcmp(keyword, "elf_x86_64") == 0 :
                                                  strcmp(keyword, "now") == 0 || strcmp(keyword, "relro") == 0 || strcmp(keyword, "noexecstack") == 0;
            if (!valid)
            {
                fprintf(stderr, "link-foreign: unsupported option: %s %s\n", argument, keyword);
            }
        }
        else if (strcmp(argument, "-s") == 0 || strcmp(argument, "--strip-all") == 0)
        {
            driver->strip_all = true;
        }
        else if (strcmp(argument, "--time") == 0)
        {
            driver->time = true;
        }
        else if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)
        {
            driver->help = true;
        }
        else
        {
            inputs[count] = argv[i];
            count += 1;
        }
    }
    if (valid && !driver->help)
    {
        inputs[0] = driver->kind == RUNTIME_KIND_STATIC ? (char*)"-Bstatic" : (char*)"-Bdynamic";
        const ForeignFailure failure = foreign_arguments(&driver->command, (int)count, inputs, arena);
        valid = failure.error == FOREIGN_ERROR_NONE && driver->command.argument_count != 0 && driver->entry.length <= UINT32_MAX;
        if (failure.error == FOREIGN_ERROR_ARGUMENTS)
        {
            fprintf(stderr, "link-foreign: invalid input option: %s\n", inputs[failure.argument]);
        }
        if (valid)
        {
            // foreign_arguments reserves count + 1 entries, leaving room for the entry's forced reference.
            driver->command.undefined[driver->command.undefined_count] = driver->entry;
            driver->command.undefined_count += 1;
        }
    }
    return valid;
}

static SymbolId foreign_driver_entry(const ForeignDriver* driver, const Foreign* foreign, const ForeignLink* converted)
{
    const uint64_t hash = link_hash((const uint8_t*)driver->entry.pointer, driver->entry.length);
    const uint32_t name = foreign_find_name(foreign, (const uint8_t*)driver->entry.pointer, (uint32_t)driver->entry.length, hash);
    return name != 0 ? converted->names[name - 1] : 0;
}

static void foreign_driver_conversion_error(const Foreign* foreign, ForeignLinkResult result, RuntimeKind kind)
{
    String8 shared_library = { 0 };
    if (kind == RUNTIME_KIND_STATIC && result.error == FOREIGN_LINK_ERROR_UNSUPPORTED)
    {
        for (uint32_t library = 0; library < foreign->library_count && shared_library.pointer == 0; library += 1)
        {
            if (foreign->libraries[library].needed)
            {
                const uint32_t input = foreign->libraries[library].input;
                shared_library = foreign->inputs[input].path;
            }
        }
    }
    if (shared_library.pointer != 0)
    {
        fprintf(stderr, "link-foreign: static output cannot use shared library %.*s; use -pie or select an archive with -Bstatic\n",
                (int)shared_library.length, shared_library.pointer);
    }
    else
    {
        static const char* const messages[] = {
            "none",
            "invalid input",
            "out of memory",
            "input exceeds linker capacity",
            "multiple definitions",
            "undefined symbol",
            "unsupported input",
            "unsupported or malformed relocation",
            "invalid link representation",
            "an IFUNC definition, which a foreign executable doesn't take yet",
        };
        const char* message = (uint32_t)result.error < BUSTER_ARRAY_LENGTH(messages) ? messages[result.error] : "conversion failed";
        fprintf(stderr, "link-foreign: %s", message);
        if (result.name != 0 && result.name <= foreign->names.count)
        {
            const uint32_t name = result.name - 1;
            fprintf(stderr, ": %.*s", (int)foreign->names.length[name], foreign->names.bytes[name]);
        }
        if (result.object != 0 && result.object < foreign->object_count)
        {
            const uint32_t input = foreign->objects[result.object].input;
            if (input < foreign->input_count)
            {
                fprintf(stderr, " in %.*s", (int)foreign->inputs[input].path.length, foreign->inputs[input].path.pointer);
            }
        }
        fprintf(stderr, " (section %u, relocation %u, offset %llu)\n", result.import.section, result.import.relocation, (unsigned long long)result.import.offset);
    }
}

// Conversion has already checked the Link. Nonzero NameIds denote nonempty names; original STT_FILE rows and
// unnamed STT_SECTION bookkeeping are not present in this selection. A local named .L... is a temporary label, which
// the assembler keeps only for relocations into SHF_MERGE sections (GCC's .LC0 strings): GNU ld and lld leave those
// out of .symtab, and so does the selection. Count/fill read the final name and flags columns, and a local's name.
static bool foreign_driver_temporary(const Link* link, SymbolId symbol)
{
    const NameId name = link->symbols.name[symbol];
    const bool local = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
    return local && link->names.length[name] >= 2 && link->names.bytes[name][0] == '.' && link->names.bytes[name][1] == 'L';
}

static uint32_t foreign_driver_symbols_exact(const Link* link, Range64 range, SymbolId* ids)
{
    const NameId* names = link->symbols.name;
    uint32_t count = 0;
    #pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t i = range.begin; i < range.end; i += 1)
    {
        if (names[i] != 0 && !foreign_driver_temporary(link, (SymbolId)i))
        {
            if (ids != 0)
            {
                ids[count] = (SymbolId)i;
            }
            count += 1;
        }
    }
    return count;
}

// Of eight named locals (their name ids in `named`, the lanes in `lanes`, each name two bytes or more), those named
// .L...: each name's first two bytes from the aligned eight bytes that hold its first, and for a name that starts in
// a block's last byte, the next block's, which its second byte is in: a gather never reads outside a block the name
// has a byte in.
static __mmask8 foreign_driver_temporaries(const Link* link, __m256i named, __mmask8 lanes)
{
    const __m512i pointers = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), lanes, named, (const void*)link->names.bytes, 8);
    const __m512i shift = _mm512_slli_epi64(_mm512_and_si512(pointers, _mm512_set1_epi64(7)), 3);
    const __m512i aligned = _mm512_andnot_si512(_mm512_set1_epi64(7), pointers);
    const __m512i low = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), lanes, aligned, (const void*)0, 1);
    const __mmask8 crossing = lanes & _mm512_cmpeq_epi64_mask(shift, _mm512_set1_epi64(56));
    const __m512i high = _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), crossing, _mm512_add_epi64(aligned, _mm512_set1_epi64(8)), (const void*)0, 1);
    const __m512i bytes = _mm512_or_si512(_mm512_srlv_epi64(low, shift), _mm512_sllv_epi64(high, _mm512_sub_epi64(_mm512_set1_epi64(64), shift)));
    return lanes & _mm512_cmpeq_epi64_mask(_mm512_and_si512(bytes, _mm512_set1_epi64(0xffff)), _mm512_set1_epi64('.' | 'L' << 8));
}

static uint32_t foreign_driver_symbols_vector(const Link* link, Range64 range, SymbolId* ids)
{
    const NameId* names = link->symbols.name;
    uint32_t count = 0;
    const __m512i sequence = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = range.begin; first < range.end; first += 16)
    {
        const uint32_t length = range.end - first < 16 ? (uint32_t)(range.end - first) : 16;
        const __mmask16 active = (__mmask16)((1u << length) - 1);
        const __m512i named = _mm512_maskz_loadu_epi32(active, names + first);
        __mmask16 selected = _mm512_cmpneq_epi32_mask(named, _mm512_setzero_si512());
        // The named locals of two bytes or more, whose names the temporaries' test reads.
        const __m512i binding = _mm512_and_si512(_mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, link->symbols.flags + first)), _mm512_set1_epi32(BUSTER_LINK_SYMBOL_BINDING));
        const __mmask16 locals = selected & _mm512_cmpeq_epi32_mask(binding, _mm512_set1_epi32(BUSTER_LINK_SYMBOL_LOCAL));
        if (locals != 0)
        {
            const __m512i lengths = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), locals, named, (const void*)link->names.length, 4);
            const __mmask16 candidates = _mm512_mask_cmpge_epu32_mask(locals, lengths, _mm512_set1_epi32(2));
            const __mmask8 low = candidates != 0 ? foreign_driver_temporaries(link, _mm512_castsi512_si256(named), (__mmask8)candidates) : 0;
            const __mmask8 high = (candidates >> 8) != 0 ? foreign_driver_temporaries(link, _mm512_extracti64x4_epi64(named, 1), (__mmask8)(candidates >> 8)) : 0;
            selected &= (__mmask16) ~((uint32_t)low | (uint32_t)high << 8);
        }
        const uint32_t added = (uint32_t)__builtin_popcount((uint32_t)selected);
        if (ids != 0)
        {
            const __m512i values = _mm512_add_epi32(sequence, _mm512_set1_epi32((int)first));
            const __m512i packed = _mm512_maskz_compress_epi32(selected, values);
            _mm512_mask_storeu_epi32(ids + count, (__mmask16)((1u << added) - 1), packed);
        }
        count += added;
    }
    return count;
}

static void foreign_driver_symbols(ForeignDriverWrite* write)
{
    const uint32_t lane = lane_index();
    // A converted, checked Link always includes symbol zero, even when no named symbol survives.
    const uint32_t count = write->link->symbols.count - 1;
    const Range64 part = lane_range(count);
    const Range64 range = write->options.exact ? (Range64){ .begin = lane == 0 ? 1 : (uint64_t)count + 1, .end = (uint64_t)count + 1 } :
                                               (Range64){ .begin = part.begin + 1, .end = part.end + 1 };
    const Link* link = write->link;
    write->counts[lane] = write->options.exact ? foreign_driver_symbols_exact(link, range, 0) : foreign_driver_symbols_vector(link, range, 0);
    lane_sync();
    if (lane == 0)
    {
        uint64_t total = 0;
        for (uint32_t i = 0; i < lane_count(); i += 1)
        {
            write->offsets[i] = (uint32_t)total;
            total += write->counts[i];
        }
        // Every selected ID belongs to one disjoint subrange of the checked 32-bit symbol column.
        const bool bounded = total <= count;
        write->selected = bounded && total != 0 ? BUSTER_ARENA_PUSH_ARRAY(write->arena, SymbolId, total) : 0;
        write->symbols_ready = bounded && (total == 0 || write->selected != 0);
        write->symbols = (ExecutableSymbols){ .ids = write->selected, .count = (uint32_t)total };
        write->options.symbols = &write->symbols;
        write->result.error = write->symbols_ready ? EXECUTABLE_ERROR_NONE : EXECUTABLE_ERROR_MEMORY;
    }
    lane_sync();
    if (write->symbols_ready && write->counts[lane] != 0)
    {
        SymbolId* ids = write->selected + write->offsets[lane];
        if (write->options.exact)
        {
            foreign_driver_symbols_exact(link, range, ids);
        }
        else
        {
            foreign_driver_symbols_vector(link, range, ids);
        }
    }
    lane_sync();
}

static void foreign_driver_write_lane(void* argument)
{
    ForeignDriverWrite* write = argument;
    if (!write->strip_all)
    {
        foreign_driver_symbols(write);
    }
    const bool succeeded = write->symbols_ready && executable_write(write->link, &write->options, &write->result);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

// The stages --time reports, each the time from the stage before's end to its own.
typedef enum ForeignDriverStage
{
    FOREIGN_DRIVER_STAGE_ARGUMENTS,
    FOREIGN_DRIVER_STAGE_INTERPRETER,
    FOREIGN_DRIVER_STAGE_RESOLVE,
    FOREIGN_DRIVER_STAGE_CONVERT,
    FOREIGN_DRIVER_STAGE_DEDUP_UNWIND,
    FOREIGN_DRIVER_STAGE_MERGE_STRINGS,
    FOREIGN_DRIVER_STAGE_WRITE,
    FOREIGN_DRIVER_STAGE_RELEASE,
    FOREIGN_DRIVER_STAGES,
} ForeignDriverStage;

static uint64_t foreign_driver_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Each resolution phase's time on the slowest lane, into `slowest`.
static void foreign_driver_resolve_phases(const Foreign* foreign, uint64_t* slowest)
{
    for (uint32_t phase = 0; phase < FOREIGN_PHASES; phase += 1)
    {
        slowest[phase] = 0;
        for (uint32_t lane = 0; foreign->lane_states != 0 && lane < foreign->lanes; lane += 1)
        {
            const uint64_t taken = foreign->lane_states[lane].nanoseconds[phase];
            slowest[phase] = taken > slowest[phase] ? taken : slowest[phase];
        }
    }
}

// The report: the whole, each stage, the resolution's phases (`resolve`, the slowest lane's time in each) and the
// writer's, from lane 0's timestamps; the writer's first part is the lanes' start and the symbol selection, up to
// executable_write's own start. `ends` holds each stage's end, `begin` the start; a stage that didn't run ends where the
// one before did.
static void foreign_driver_report(const uint64_t* resolve, uint64_t begin, const uint64_t* ends, uint64_t write_begin, const ExecutableResult* written)
{
    static const char* const stages[FOREIGN_DRIVER_STAGES] = {
        "arguments",
        "interpreter",
        "resolve",
        "convert",
        "dedup-unwind",
        "merge-strings",
        "write",
        "release",
    };
    static const char* const resolve_phases[FOREIGN_PHASES] = {
        "open",
        "indices",
        "headers",
        "symbols",
        "exchange",
        "resolve",
        "load",
        "groups",
        "winners",
    };
    static const char* const write_phases[EXECUTABLE_PHASES] = {
        "count",
        "layout",
        "symbols",
        "dynamic",
        "open",
        "chunks",
        "finish",
    };
    fprintf(stderr, "link-foreign: %.3f ms\n", (double)(ends[FOREIGN_DRIVER_STAGES - 1] - begin) / 1e6);
    uint64_t previous = begin;
    for (uint32_t stage = 0; stage < FOREIGN_DRIVER_STAGES; stage += 1)
    {
        fprintf(stderr, "  %-13s %9.3f ms", stages[stage], (double)(ends[stage] - previous) / 1e6);
        for (uint32_t phase = 0; stage == FOREIGN_DRIVER_STAGE_RESOLVE && phase < FOREIGN_PHASES; phase += 1)
        {
            fprintf(stderr, " %s %.3f", resolve_phases[phase], (double)resolve[phase] / 1e6);
        }
        if (stage == FOREIGN_DRIVER_STAGE_WRITE && written->start_time >= write_begin)
        {
            fprintf(stderr, " lanes+symbols %.3f", (double)(written->start_time - write_begin) / 1e6);
            uint64_t phase_begin = written->start_time;
            for (uint32_t phase = 0; phase < EXECUTABLE_PHASES; phase += 1)
            {
                const uint64_t end = written->phase_times[phase] >= phase_begin ? written->phase_times[phase] : phase_begin;
                fprintf(stderr, " %s %.3f", write_phases[phase], (double)(end - phase_begin) / 1e6);
                phase_begin = end;
            }
        }
        fprintf(stderr, "\n");
        previous = ends[stage];
    }
}

static int run_foreign_link(int argc, char** argv)
{
    const uint64_t begin = foreign_driver_now();
    uint64_t ends[FOREIGN_DRIVER_STAGES] = { 0 };
    uint64_t write_begin = 0;
    ExecutableResult written = { 0 };
    Arena arena = arena_reserve(1ull << 36);
    Arena names = arena_reserve(1ull << 34);
    ForeignDriver driver = { 0 };
    bool succeeded = arena.base != 0 && names.base != 0 && foreign_driver_arguments(&driver, argc, argv, &arena);
    Foreign foreign = { 0 };
    Runtime runtime = { 0 };
    ForeignLink converted = { 0 };
    ForeignUnwindDedupLink deduplicated = { 0 };
    if (!succeeded || driver.help)
    {
        foreign_driver_usage();
    }
    ends[FOREIGN_DRIVER_STAGE_ARGUMENTS] = foreign_driver_now();
    if (succeeded && !driver.help && driver.kind == RUNTIME_KIND_DYNAMIC && driver.interpreter.length == 0)
    {
        succeeded = runtime_discover_interpreter(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
        driver.interpreter = runtime.loader_path;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: no system interpreter found; pass --dynamic-linker PATH\n");
        }
    }
    ends[FOREIGN_DRIVER_STAGE_INTERPRETER] = foreign_driver_now();
    if (succeeded && !driver.help)
    {
        foreign_run(&foreign, &driver.command, driver.path, driver.lanes, 0);
        succeeded = foreign.failure.error == FOREIGN_ERROR_NONE;
        if (!succeeded)
        {
            static const char* const messages[] = {
                "none",
                "invalid input argument",
                "cannot read input",
                "library or script input not found",
                "unrecognized input format",
                "malformed input",
                "unsupported input",
                "out of memory",
                "cannot start linker workers",
            };
            const uint32_t error = (uint32_t)foreign.failure.error;
            fprintf(stderr, "link-foreign: %s", error < BUSTER_ARRAY_LENGTH(messages) ? messages[error] : "input resolution failed");
            if (foreign.failure.input < foreign.input_count)
            {
                const String8 path = foreign.inputs[foreign.failure.input].path;
                fprintf(stderr, " in %.*s", (int)path.length, path.pointer);
            }
            if (foreign.failure.system_error != 0)
            {
                fprintf(stderr, ": %s", strerror(foreign.failure.system_error));
            }
            fprintf(stderr, "\n");
        }
    }
    ends[FOREIGN_DRIVER_STAGE_RESOLVE] = foreign_driver_now();
    if (succeeded && !driver.help)
    {
        ForeignLinkResult result = driver.gc ? foreign_link_build_for_gc(&converted, &foreign, &arena, &names, driver.kind, driver.interpreter) :
                                   driver.kind == RUNTIME_KIND_DYNAMIC ? foreign_link_build_dynamic(&converted, &foreign, &arena, &names, driver.interpreter) :
                                                                        foreign_link_build(&converted, &foreign, &arena, &names);
        if (result.error == FOREIGN_LINK_ERROR_NONE && driver.gc)
        {
            const ForeignGcRoots roots = { .entry = foreign_driver_entry(&driver, &foreign, &converted) };
            ForeignLink compacted = { 0 };
            result = foreign_gc_link(&compacted, &foreign, &converted, &roots, driver.path, &arena, &names);
            if (result.error == FOREIGN_LINK_ERROR_NONE)
            {
                converted = compacted;
            }
        }
        succeeded = result.error == FOREIGN_LINK_ERROR_NONE;
        if (!succeeded)
        {
            foreign_driver_conversion_error(&foreign, result, driver.kind);
        }
    }
    ends[FOREIGN_DRIVER_STAGE_CONVERT] = foreign_driver_now();
    if (succeeded && !driver.help && driver.dedup_unwind)
    {
        const ForeignGcRoots roots = { .entry = foreign_driver_entry(&driver, &foreign, &converted) };
        const ForeignLinkResult result = foreign_unwind_dedup_link(&deduplicated, &foreign, &converted, &roots, driver.path, &arena, &names);
        succeeded = result.error == FOREIGN_LINK_ERROR_NONE || result.error == FOREIGN_LINK_ERROR_UNSUPPORTED;
        if (result.error == FOREIGN_LINK_ERROR_NONE)
        {
            converted = deduplicated.converted;
        }
        else if (!succeeded)
        {
            fprintf(stderr, "link-foreign: unwind deduplication failed (%u, object %u, section %u, offset %llu)\n",
                    (unsigned)result.error, result.object, result.import.section, (unsigned long long)result.import.offset);
        }
    }
    ends[FOREIGN_DRIVER_STAGE_DEDUP_UNWIND] = foreign_driver_now();
    if (succeeded && !driver.help && driver.merge_strings)
    {
        ForeignStringsMerge merged = { 0 };
        const ForeignStringsError error = foreign_strings_merge(&converted, &foreign, &arena, &merged);
        succeeded = error == FOREIGN_STRINGS_ERROR_NONE || error == FOREIGN_STRINGS_ERROR_UNSUPPORTED;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: string merging failed (%u)\n", (unsigned)error);
        }
    }
    ends[FOREIGN_DRIVER_STAGE_MERGE_STRINGS] = foreign_driver_now();
    write_begin = ends[FOREIGN_DRIVER_STAGE_MERGE_STRINGS];
    if (succeeded && !driver.help)
    {
        ForeignDriverWrite write = {
            .link = &converted.link,
            .arena = &arena,
            .options = {
                .path = driver.output,
                .entry = foreign_driver_entry(&driver, &foreign, &converted),
                .dynamic = driver.kind == RUNTIME_KIND_DYNAMIC ? &converted.dynamic : 0,
                .exact = driver.path == FOREIGN_PATH_EXACT,
                // The atoms' bytes are the inputs' private mappings: staged, a chunk is patched in a lane's buffer
                // rather than in pages that each write would copy.
                .stage = true,
            },
            .strip_all = driver.strip_all,
            .symbols_ready = true,
        };
        succeeded = lane_run(driver.lanes, foreign_driver_write_lane, &write, 0) && write.succeeded;
        written = write.result;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: executable writing failed (%u, atom %u, symbol %u, fixup %u)",
                    (unsigned)write.result.error, write.result.atom, write.result.symbol, write.result.fixup);
            if (write.result.system_error != 0)
            {
                fprintf(stderr, ": %s", strerror(write.result.system_error));
            }
            fprintf(stderr, "\n");
        }
    }
    ends[FOREIGN_DRIVER_STAGE_WRITE] = foreign_driver_now();
    const bool time = driver.time && succeeded;
    // The resolution's lane states, which foreign_release frees, give their phases' times first.
    uint64_t resolve[FOREIGN_PHASES];
    foreign_driver_resolve_phases(&foreign, resolve);
    foreign_release(&foreign);
    runtime_release(&runtime);
    arena_release(&names);
    arena_release(&arena);
    ends[FOREIGN_DRIVER_STAGE_RELEASE] = foreign_driver_now();
    if (time)
    {
        foreign_driver_report(resolve, begin, ends, write_begin, &written);
    }
    return succeeded ? 0 : 1;
}
