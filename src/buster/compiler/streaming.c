// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Streaming the output (#88; docs/object-writer-and-linker.md, "Streaming the output"): an experiment that writes a
// program's executable while Tier 0 compiles it, in the data-first order, against writing the same file once
// compilation has ended. The program is units of tier0_generate.c's programs, each its globals and functions (the entry
// last, a callee always before its callers), and a _start that calls every unit's entry on fixed inputs, folds the
// results, writes them to standard output and exits.
//
// The file is a static executable without libc, laid out data first: the headers' page (the ELF header, the program
// headers and the build ID's note), .data at the next page, .text at the page after .data, then .eh_frame_hdr and
// .eh_frame in a read-only segment of their own, then the DWARF (debug.c's five sections), .symtab, .strtab, .shstrtab
// and the section headers. .data's size is known before the first function compiles, so .text's address is too, and
// with it every address the code refers to but a function's that isn't compiled yet. Functions start on 16 bytes and
// are padded with int3. The DWARF is one unit of one file, streamed.c, in which function f of the program's order (the
// units' functions, then _start) is line f + 1, with a row at its start; like .eh_frame's, its place in the file waits
// for .text's size, so it is built and written with the tail.
//
// The schedules write the same bytes, and the same build ID:
//   - at the end (STREAM_SCHEDULE_END): every function compiled (Tier 0, branch sizing, encoding, its unwind
//     descriptor) into .text in memory, then .data, every field patched, .text chunk by chunk, the tail and the headers
//     written;
//   - streamed (STREAM_SCHEDULE_STREAM): .data written first; each function's fields patched once compiled, those to a
//     function not yet compiled (only in the callers' order) when it is, and each chunk of .text hashed and written as
//     soon as it is whole and none of its fields waits; then the rest of .text, the tail and the headers;
//   - streamed by a writer lane (STREAM_SCHEDULE_WRITER): the same, with lane 1 hashing and writing the chunks that lane
//     0, which compiles, publishes as ready (lane_publish and lane_wait on a count of chunks), so that writing overlaps
//     compiling.
// A chunk is BUSTER_STREAM_CHUNK_BYTES of .text (the direct path's chunks' size, a quarter of Zen 5's L2) unless the
// options give another. The build ID is the hash (executable.c's chunk hash) of the pieces' hashes in the file's order:
// the headers with the note's hash zero, .data, each chunk and the tail.
//
// The order (StreamOrder) is the units' functions as generated (callees first: every call and every function's address
// backward) or reversed (callers first: every one forward), with _start last either way.
//
// main.c includes this file after tier0_generate.c, unwind_tier0.c and executable.c. Layout: the program
// (stream_program_generate), the code (stream_compile and stream_compile_start), the file (the layout, the tail and the
// headers), the schedules (stream_write), and the direct path's writer on the same code (stream_link and
// stream_write_link).

#define BUSTER_STREAM_PAGE_BYTES 4096ull
#define BUSTER_STREAM_CHUNK_BYTES (256ull << 10)
#define BUSTER_STREAM_FUNCTION_ALIGNMENT 16u
#define BUSTER_STREAM_HEADERS 7
#define BUSTER_STREAM_SECTIONS 14
#define BUSTER_STREAM_NOTE_OFFSET (sizeof(Elf64_Ehdr) + BUSTER_STREAM_HEADERS * sizeof(Elf64_Phdr))
#define BUSTER_STREAM_MAX_UNITS (1u << 20)
// A unit whose functions Tier 0 doesn't all compile is generated again from the next seed, at most this many times.
#define BUSTER_STREAM_ATTEMPTS 64
// _start's bytes: its prologue, each unit's call and fold, and its write and exit.
#define BUSTER_STREAM_START_PROLOGUE 3
#define BUSTER_STREAM_START_PER_UNIT 42
#define BUSTER_STREAM_START_EPILOGUE 40
#define BUSTER_STREAM_EH_FRAME_HDR_BYTES(fdes) (12ull + 8ull * (fdes))

typedef enum StreamOrder
{
    STREAM_ORDER_CALLEES,
    STREAM_ORDER_CALLERS,
} StreamOrder;

typedef enum StreamSchedule
{
    STREAM_SCHEDULE_END,
    STREAM_SCHEDULE_STREAM,
    STREAM_SCHEDULE_WRITER,
    STREAM_SCHEDULES,
} StreamSchedule;

typedef enum StreamError
{
    STREAM_OK,
    STREAM_ERROR_MEMORY,
    STREAM_ERROR_COMPILE,
    STREAM_ERROR_UNWIND,
    STREAM_ERROR_FILE,
    STREAM_ERROR_LIMIT,
    STREAM_ERROR_LINK,
    STREAM_ERROR_DEBUG,
} StreamError;

// A unit: a generated program, its functions as Tier 0 takes them, the inputs _start gives its entry, and where its
// globals are in .data (each 8-aligned, in order, as tier0_test.c's interpreter lays them out).
typedef struct StreamUnit StreamUnit;
struct StreamUnit
{
    Tier0Program program;
    Tier0Functions functions;
    uint64_t seed;
    uint64_t inputs[3];
    uint32_t first_function;
    uint32_t first_global;
    uint32_t data_offset;
    uint32_t global_offsets[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
};

// The program: its units, every unit's functions in turn and then _start (each function's unit, or unit_count for
// _start), its data symbols (the 8-byte slot _start writes its result to, then every unit's globals in turn, each one's
// offset in .data and unit), and .data's first bytes.
typedef struct StreamProgram StreamProgram;
struct StreamProgram
{
    StreamUnit* units;
    uint32_t unit_count;
    uint32_t function_count;
    uint32_t* function_units;
    uint32_t data_symbol_count;
    uint64_t* data_offsets;
    uint64_t* data_sizes;
    uint32_t* data_units;
    uint8_t* data;
    uint64_t data_size;
    uint64_t result_offset;
    uint64_t entries;
    // The symbols' names, as .strtab holds them (a NUL, then each name and its NUL in symbol order), and each one's
    // offset there: a compiler has them before it compiles anything.
    char* strtab;
    uint64_t strtab_size;
    uint32_t* name_offsets;
};

typedef struct StreamOptions StreamOptions;
struct StreamOptions
{
    String8 path;
    StreamSchedule schedule;
    StreamOrder order;
    uint64_t chunk_bytes;
    // The writer lane's CPUs (lane 0's, lane 1's), or 0 for the process's.
    const uint32_t* cpus;
};

// What a write did, and when (CLOCK_MONOTONIC nanoseconds): its start, the last function compiled, .text's last chunk
// and the last pairs written (`flushed`, the writer lane's chunks aside), the tail built and written, the headers
// written, and the file closed.
typedef struct StreamResult StreamResult;
struct StreamResult
{
    StreamError error;
    uint32_t function;
    int system_error;
    uint64_t start;
    uint64_t compiled;
    uint64_t flushed;
    uint64_t tail_built;
    uint64_t tail_written;
    uint64_t headers;
    uint64_t closed;
    uint64_t file_size;
    uint64_t text_size;
    uint64_t chunks;
    uint64_t chunks_streamed;
    uint64_t fixups;
    uint64_t forward_fixups;
    uint64_t records;
    uint8_t build_id[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
};

static uint64_t stream_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static uint64_t stream_align(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// The program

// The inputs of a unit's entry: small values, so that its loops stay short.
static void stream_inputs(uint64_t seed, uint64_t inputs[3])
{
    uint64_t state = seed ^ 0x5714ea3ull;
    for (uint32_t a = 0; a < 3; a += 1)
    {
        inputs[a] = tier0_generate_random(&state) % 7;
    }
}

// Whether Tier 0 compiles every function of `functions`, each into `scratch`, put back after it.
static bool stream_compiles(const Tier0Functions* functions, Arena* scratch)
{
    bool result = true;
    for (uint32_t f = 0; result && f < functions->count; f += 1)
    {
        const uint64_t mark = scratch->length;
        result = tier0_validate(&functions->functions[f]).error == TIER0_OK && tier0_compile(&functions->functions[f], scratch).error == TIER0_OK;
        arena_pop_to(scratch, mark);
    }
    return result;
}

// The name of a symbol of the program into `name` (room for 32 bytes): its functions (_start last), then its data
// symbols (the result slot, then the units' globals). Returns its length.
static uint32_t stream_symbol_name(const StreamProgram* program, uint32_t symbol, char* name)
{
    int length = 0;
    if (symbol < program->function_count - 1)
    {
        const uint32_t unit = program->function_units[symbol];
        length = snprintf(name, 32, "u%u_f%u", unit, symbol - program->units[unit].first_function);
    }
    else if (symbol == program->function_count - 1)
    {
        length = snprintf(name, 32, "_start");
    }
    else if (symbol == program->function_count)
    {
        length = snprintf(name, 32, "stream_result");
    }
    else
    {
        // A global: function_count plus its data symbol.
        const uint32_t data_symbol = symbol - program->function_count;
        const uint32_t unit = program->data_units[data_symbol];
        length = snprintf(name, 32, "u%u_g%u", unit, data_symbol - program->units[unit].first_global);
    }
    return (uint32_t)length;
}

// `unit_count` units from consecutive seeds after `seed` (a seed whose program Tier 0 doesn't compile whole is skipped),
// their columns in `arena`, with the options' shape; .data, and the symbols' names. Returns false if memory runs out or
// too many seeds in a row fail.
static bool stream_program_generate(StreamProgram* program, Arena* arena, Arena* scratch, const Tier0Tables* tables, uint64_t seed, uint32_t unit_count,
                                    Tier0ProgramOptions options)
{
    memset(program, 0, sizeof(*program));
    program->units = unit_count <= BUSTER_STREAM_MAX_UNITS ? BUSTER_ARENA_PUSH_ARRAY(arena, StreamUnit, unit_count) : 0;
    bool result = program->units != 0;
    uint64_t next = seed;
    uint64_t data_size = 0;
    uint32_t functions = 0;
    uint32_t globals = 0;
    for (uint32_t u = 0; result && u < unit_count; u += 1)
    {
        StreamUnit* unit = &program->units[u];
        bool generated = false;
        for (uint32_t attempt = 0; result && !generated && attempt < BUSTER_STREAM_ATTEMPTS; attempt += 1)
        {
            const uint64_t mark = arena->length;
            unit->seed = next;
            next += 1;
            result = tier0_generate_program(&unit->program, arena, unit->seed, options, "c_") && tier0_program_functions(&unit->program, tables, arena, &unit->functions);
            generated = result && stream_compiles(&unit->functions, scratch);
            if (!generated)
            {
                arena_pop_to(arena, mark);
            }
        }
        result = result && generated;
        if (result)
        {
            stream_inputs(unit->seed, unit->inputs);
            unit->first_function = functions;
            unit->first_global = 1 + globals;
            globals += unit->program.global_count;
            unit->data_offset = (uint32_t)data_size;
            uint64_t offset = 0;
            for (uint32_t g = 0; g < unit->program.global_count; g += 1)
            {
                unit->global_offsets[g] = (uint32_t)offset;
                offset += stream_align((uint64_t)tier0_class_bytes(unit->program.global_classes[g]) * unit->program.global_elements[g], 8);
            }
            data_size = stream_align(data_size + offset, 16);
            functions += unit->functions.count;
            for (uint32_t f = 0; f < unit->functions.count; f += 1)
            {
                program->entries += unit->functions.functions[f].count;
            }
            result = data_size < (1ull << 30) && functions < (1u << 28);
        }
    }
    if (result)
    {
        program->unit_count = unit_count;
        program->function_count = functions + 1;
        program->result_offset = data_size;
        program->data_size = data_size + 8;
        program->data_symbol_count = 1 + globals;
        program->function_units = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->function_count);
        program->data_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, program->data_symbol_count);
        program->data_sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, program->data_symbol_count);
        program->data_units = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, program->data_symbol_count);
        program->data = arena_push(arena, program->data_size, 64);
        result = program->function_units != 0 && program->data_offsets != 0 && program->data_sizes != 0 && program->data_units != 0 && program->data != 0;
    }
    if (result)
    {
        program->data_offsets[0] = program->result_offset;
        program->data_sizes[0] = 8;
        program->data_units[0] = unit_count;
    }
    for (uint32_t u = 0; result && u < unit_count; u += 1)
    {
        const StreamUnit* unit = &program->units[u];
        for (uint32_t f = 0; f < unit->functions.count; f += 1)
        {
            program->function_units[unit->first_function + f] = u;
        }
        for (uint32_t g = 0; g < unit->program.global_count; g += 1)
        {
            const uint32_t bytes = tier0_class_bytes(unit->program.global_classes[g]);
            program->data_offsets[unit->first_global + g] = unit->data_offset + unit->global_offsets[g];
            program->data_sizes[unit->first_global + g] = (uint64_t)bytes * unit->program.global_elements[g];
            program->data_units[unit->first_global + g] = u;
            uint8_t* place = program->data + unit->data_offset + unit->global_offsets[g];
            memset(place, 0, stream_align((uint64_t)bytes * unit->program.global_elements[g], 8));
            for (uint32_t e = 0; e < unit->program.global_elements[g]; e += 1)
            {
                memcpy(place + e * bytes, &unit->program.global_values[unit->program.global_first_value[g] + e], bytes);
            }
        }
    }
    if (result)
    {
        program->function_units[program->function_count - 1] = program->unit_count;
        // The gaps between units, and the result slot.
        for (uint32_t u = 0; u < unit_count; u += 1)
        {
            const StreamUnit* unit = &program->units[u];
            const uint64_t end = u + 1 < unit_count ? program->units[u + 1].data_offset : program->result_offset;
            uint64_t used = 0;
            for (uint32_t g = 0; g < unit->program.global_count; g += 1)
            {
                used = unit->global_offsets[g] + stream_align((uint64_t)tier0_class_bytes(unit->program.global_classes[g]) * unit->program.global_elements[g], 8);
            }
            memset(program->data + unit->data_offset + used, 0, end - unit->data_offset - used);
        }
        memset(program->data + program->result_offset, 0, 8);
        const uint32_t symbols = program->function_count + program->data_symbol_count;
        program->name_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols);
        program->strtab = arena_push(arena, 1 + (uint64_t)symbols * 32, 1);
        result = program->name_offsets != 0 && program->strtab != 0;
        uint64_t size = 1;
        if (result)
        {
            program->strtab[0] = 0;
        }
        for (uint32_t symbol = 0; result && symbol < symbols; symbol += 1)
        {
            program->name_offsets[symbol] = (uint32_t)size;
            size += stream_symbol_name(program, symbol, program->strtab + size) + 1;
        }
        program->strtab_size = size;
    }
    return result;
}

// The function at place `index` of the emission order.
static uint32_t stream_function_at(const StreamProgram* program, StreamOrder order, uint32_t index)
{
    const uint32_t last = program->function_count - 1;
    return index == last || order == STREAM_ORDER_CALLEES ? index : last - 1 - index;
}

// ---------------------------------------------------------------------------------------------------------------------
// The code

// The state a write builds: .text in memory and each function's place in it, the fields to patch (each one's offset in
// .text, its instruction's end and its target: a function's index, or function_count plus a data symbol's), each
// function's unwind descriptor, and for the streamed schedules the fields that wait for a function (a list per function,
// threaded through `waiting`) and the earliest field not yet patched.
typedef struct StreamCode StreamCode;
struct StreamCode
{
    const StreamProgram* program;
    Arena* scratch;
    uint8_t* text;
    uint64_t text_capacity;
    uint64_t text_size;
    uint64_t text_address;
    uint64_t data_address;
    uint32_t* offsets;
    uint32_t* sizes;
    bool* compiled;
    UnwindFunction* unwind;
    StreamOrder order;
    // The CIE and FDE pairs of the functions compiled so far, in the order (each pair's initial location's offset in
    // `eh`), `described` of them, and the count compiled (but _start, which has none).
    uint8_t* eh;
    uint64_t eh_capacity;
    uint64_t eh_size;
    uint32_t* eh_locations;
    uint32_t described;
    uint32_t emitted;
    uint32_t* fields;
    uint32_t* ends;
    uint32_t* targets;
    uint8_t* patched;
    uint32_t* waiting;
    uint32_t* waiting_heads;
    uint64_t fixup_count;
    uint64_t pending;
    uint64_t forward;
    uint64_t records;
};

// Room for `count` more fields, in the columns' arenas (each column its own, so that it stays one array).
static bool stream_fixup_room(StreamCode* code, Arena* columns, uint64_t count)
{
    const uint64_t total = code->fixup_count + count;
    const uint64_t have = columns[0].length / sizeof(uint32_t);
    bool result = true;
    if (total > have)
    {
        const uint64_t more = total - have + 4096;
        result = arena_push(&columns[0], more * sizeof(uint32_t), 4) != 0 && arena_push(&columns[1], more * sizeof(uint32_t), 4) != 0 &&
                 arena_push(&columns[2], more * sizeof(uint32_t), 4) != 0 && arena_push(&columns[3], more, 1) != 0 && arena_push(&columns[4], more * sizeof(uint32_t), 4) != 0;
    }
    return result;
}

// A field's value: its target's address less its instruction's end.
static int32_t stream_value(const StreamCode* code, uint64_t fixup)
{
    const uint32_t target = code->targets[fixup];
    const uint64_t address = target < code->program->function_count ? code->text_address + code->offsets[target] :
                                                                      code->data_address + code->program->data_offsets[target - code->program->function_count];
    return (int32_t)(address - (code->text_address + code->ends[fixup]));
}

static void stream_patch(StreamCode* code, uint64_t fixup)
{
    const int32_t value = stream_value(code, fixup);
    memcpy(code->text + code->fields[fixup], &value, sizeof(value));
    code->patched[fixup] = 1;
}

// Adds a field of the function being compiled; with `streamed`, patches it if its target is known, and otherwise makes
// it wait for its target.
static void stream_add_fixup(StreamCode* code, uint32_t field, uint32_t end, uint32_t target, bool streamed)
{
    const uint64_t fixup = code->fixup_count;
    code->fields[fixup] = field;
    code->ends[fixup] = end;
    code->targets[fixup] = target;
    code->patched[fixup] = 0;
    code->waiting[fixup] = 0;
    code->fixup_count += 1;
    if (streamed)
    {
        const bool known = target >= code->program->function_count || code->compiled[target];
        if (known)
        {
            stream_patch(code, fixup);
        }
        else
        {
            code->waiting[fixup] = code->waiting_heads[target];
            code->waiting_heads[target] = (uint32_t)(fixup + 1);
            code->forward += 1;
        }
    }
}

// The fields that waited for `function`, patched now that it has an address.
static void stream_resolve(StreamCode* code, uint32_t function)
{
    uint32_t next = code->waiting_heads[function];
    while (next != 0)
    {
        stream_patch(code, next - 1);
        next = code->waiting[next - 1];
    }
    code->waiting_heads[function] = 0;
}

// _start: each unit's entry called on the unit's inputs, the results folded (r = rol(r, 5) ^ result), stored in the
// result slot and written to standard output, then exit(0).
static uint32_t stream_compile_start(StreamCode* code, uint32_t start, bool streamed)
{
    const StreamProgram* program = code->program;
    uint8_t* bytes = code->text + start;
    uint32_t at = 0;
    static const uint8_t prologue[BUSTER_STREAM_START_PROLOGUE] = { 0x45, 0x31, 0xe4 };
    memcpy(bytes, prologue, sizeof(prologue));
    at += sizeof(prologue);
    for (uint32_t u = 0; u < program->unit_count; u += 1)
    {
        const StreamUnit* unit = &program->units[u];
        static const uint8_t moves[3][2] = { { 0x48, 0xbf }, { 0x48, 0xbe }, { 0x48, 0xba } };
        for (uint32_t a = 0; a < 3; a += 1)
        {
            memcpy(bytes + at, moves[a], 2);
            memcpy(bytes + at + 2, &unit->inputs[a], 8);
            at += 10;
        }
        bytes[at] = 0xe8;
        memset(bytes + at + 1, 0, 4);
        stream_add_fixup(code, start + at + 1, start + at + 5, unit->first_function + unit->functions.count - 1, streamed);
        at += 5;
        static const uint8_t fold[7] = { 0x49, 0xc1, 0xc4, 0x05, 0x49, 0x31, 0xc4 };
        memcpy(bytes + at, fold, sizeof(fold));
        at += sizeof(fold);
    }
    static const uint8_t store[3] = { 0x4c, 0x89, 0x25 };
    memcpy(bytes + at, store, sizeof(store));
    memset(bytes + at + 3, 0, 4);
    stream_add_fixup(code, start + at + 3, start + at + 7, program->function_count, streamed);
    at += 7;
    static const uint8_t write_call[10] = { 0xb8, 0x01, 0x00, 0x00, 0x00, 0xbf, 0x01, 0x00, 0x00, 0x00 };
    memcpy(bytes + at, write_call, sizeof(write_call));
    at += sizeof(write_call);
    static const uint8_t buffer[3] = { 0x48, 0x8d, 0x35 };
    memcpy(bytes + at, buffer, sizeof(buffer));
    memset(bytes + at + 3, 0, 4);
    stream_add_fixup(code, start + at + 3, start + at + 7, program->function_count, streamed);
    at += 7;
    static const uint8_t finish[16] = { 0xba, 0x08, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xb8, 0x3c, 0x00, 0x00, 0x00, 0x31, 0xff, 0x0f, 0x05 };
    memcpy(bytes + at, finish, sizeof(finish));
    at += sizeof(finish);
    return at;
}

// Compiles function `function` into .text after the code so far, with its fields and unwind descriptor; with
// `streamed`, patches the fields whose targets are known and the fields that waited for this function.
static StreamError stream_compile(StreamCode* code, Arena* columns, uint32_t function, bool streamed)
{
    const StreamProgram* program = code->program;
    const uint64_t start = stream_align(code->text_size, BUSTER_STREAM_FUNCTION_ALIGNMENT);
    const uint32_t unit_index = program->function_units[function];
    const bool is_start = unit_index == program->unit_count;
    const uint64_t start_bytes = BUSTER_STREAM_START_PROLOGUE + (uint64_t)program->unit_count * BUSTER_STREAM_START_PER_UNIT + BUSTER_STREAM_START_EPILOGUE;
    StreamError error = STREAM_OK;
    memset(code->text + code->text_size, BUSTER_EXECUTABLE_INT3, start - code->text_size);
    if (is_start)
    {
        error = start + start_bytes <= code->text_capacity && stream_fixup_room(code, columns, program->unit_count + 2) ? STREAM_OK : STREAM_ERROR_LIMIT;
        if (error == STREAM_OK)
        {
            const uint32_t length = stream_compile_start(code, (uint32_t)start, streamed);
            code->offsets[function] = (uint32_t)start;
            code->sizes[function] = length;
            code->text_size = start + length;
            memset(&code->unwind[function], 0, sizeof(UnwindFunction));
        }
    }
    else
    {
        const StreamUnit* unit = &program->units[unit_index];
        const uint32_t local = function - unit->first_function;
        const uint64_t mark = code->scratch->length;
        Tier0Code compiled = tier0_compile(&unit->functions.functions[local], code->scratch);
        uint32_t* offsets = compiled.error == TIER0_OK ? BUSTER_ARENA_PUSH_ARRAY(code->scratch, uint32_t, (uint64_t)compiled.count + 1) : 0;
        uint32_t* ends = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(code->scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(compiled.reference_count)) : 0;
        error = compiled.error != TIER0_OK ? STREAM_ERROR_COMPILE : ends == 0 ? STREAM_ERROR_MEMORY : STREAM_OK;
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(compiled.count);
        if (error == STREAM_OK)
        {
            error = start + capacity <= code->text_capacity && stream_fixup_room(code, columns, compiled.reference_count) ? STREAM_OK : STREAM_ERROR_LIMIT;
        }
        if (error == STREAM_OK)
        {
            const BranchSizing sizing = size_branches(compiled.records, compiled.count, compiled.branches, compiled.targets, compiled.branch_count, offsets);
            const int64_t length = sizing.error == BRANCH_SIZING_OK ? x86_encode(compiled.records, compiled.count, code->text + start, capacity, 0, ends) : -1;
            error = length >= 0 && (uint64_t)length == sizing.length ? STREAM_OK : STREAM_ERROR_COMPILE;
            if (error == STREAM_OK)
            {
                code->offsets[function] = (uint32_t)start;
                code->sizes[function] = (uint32_t)length;
                // The k-th reference is the k-th marked record's, one field each: a global's or a function's (the
                // unit's symbols: its globals from 1, then its functions).
                const uint32_t globals = unit->program.global_count;
                for (uint32_t r = 0; r < compiled.reference_count; r += 1)
                {
                    const LinkReference* reference = &compiled.references[r];
                    const uint32_t field = ends[r] - x86_fixup_distance(compiled.records[reference->instruction].form, reference->field);
                    const uint32_t target = reference->target <= globals ? program->function_count + unit->first_global + (reference->target - 1) :
                                                                          unit->first_function + (reference->target - globals - 1);
                    memset(code->text + start + field, 0, 4);
                    stream_add_fixup(code, (uint32_t)start + field, (uint32_t)start + ends[r], target, streamed);
                }
                code->text_size = start + (uint64_t)length;
                code->records += compiled.count;
                error = unwind_tier0(&compiled, offsets, (uint32_t)length, &code->unwind[function]) == UNWIND_OK ? STREAM_OK : STREAM_ERROR_UNWIND;
            }
        }
        arena_pop_to(code->scratch, mark);
    }
    if (error == STREAM_OK)
    {
        code->compiled[function] = true;
        if (streamed)
        {
            stream_resolve(code, function);
        }
    }
    return error;
}

// The pairs of the compiled functions from `code->described` up to `until` (in the order), eight at a time, after the
// pairs so far: their initial locations stay zero until the layout gives .eh_frame its address.
static StreamError stream_unwind(StreamCode* code, uint32_t until)
{
    StreamError error = STREAM_OK;
    while (error == STREAM_OK && code->described < until)
    {
        UnwindFunction batch[BUSTER_UNWIND_BATCH];
        const uint32_t count = until - code->described < BUSTER_UNWIND_BATCH ? until - code->described : BUSTER_UNWIND_BATCH;
        for (uint32_t i = 0; i < count; i += 1)
        {
            batch[i] = code->unwind[stream_function_at(code->program, code->order, code->described + i)];
        }
        UnwindOutput output = {
            .bytes = code->eh + code->eh_size,
            .capacity = code->eh_capacity - code->eh_size,
            .locations = code->eh_locations + code->described,
            .location_capacity = count,
        };
        error = unwind_write(batch, count, &output, false) == UNWIND_OK ? STREAM_OK : STREAM_ERROR_UNWIND;
        for (uint32_t i = 0; error == STREAM_OK && i < count; i += 1)
        {
            code->eh_locations[code->described + i] += (uint32_t)code->eh_size;
        }
        code->eh_size += error == STREAM_OK ? output.size : 0;
        code->described += error == STREAM_OK ? count : 0;
    }
    return error;
}

// ---------------------------------------------------------------------------------------------------------------------
// The file

// Where everything is: .data and .text from the start (.text's address is known once .data's size is), the rest once
// .text's size is.
typedef struct StreamLayout StreamLayout;
struct StreamLayout
{
    uint64_t data_offset;
    uint64_t text_offset;
    uint64_t text_size;
    uint64_t eh_offset;
    uint64_t eh_hdr_size;
    uint64_t eh_size;
    uint64_t debug_offsets[BUSTER_DEBUG_SECTIONS];
    uint64_t symtab_offset;
    uint64_t symtab_size;
    uint64_t strtab_offset;
    uint64_t strtab_size;
    uint64_t shstrtab_offset;
    uint64_t shstrtab_size;
    uint64_t section_offset;
    uint64_t file_size;
    uint64_t chunk_bytes;
    uint64_t chunk_count;
};

static const char stream_section_names[] =
    "\0.note.gnu.build-id\0.data\0.text\0.eh_frame_hdr\0.eh_frame\0.debug_info\0.debug_abbrev\0.debug_line\0.debug_addr\0.debug_rnglists\0.symtab\0.strtab\0.shstrtab";
static const uint32_t stream_section_name_offsets[BUSTER_STREAM_SECTIONS] = { 0, 1, 20, 26, 32, 46, 56, 68, 82, 94, 106, 122, 130, 138 };
static const String8 stream_debug_file = BUSTER_S8_INIT("streamed.c");

static StreamLayout stream_layout_begin(const StreamProgram* program, uint64_t chunk_bytes)
{
    StreamLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.data_offset = BUSTER_STREAM_PAGE_BYTES;
    layout.text_offset = stream_align(layout.data_offset + program->data_size, BUSTER_STREAM_PAGE_BYTES);
    layout.chunk_bytes = chunk_bytes;
    return layout;
}

// The program's DWARF (see the file's comment) in `output`, its bytes in `arena`: with `addresses` (each function's), its
// address slots finalized; without, zero, as executable_write takes them.
static StreamError stream_debug(const StreamProgram* program, const uint32_t* sizes, const uint64_t* addresses, Arena* arena, DebugUnit* unit, DebugOutput* output)
{
    const uint32_t count = program->function_count;
    DebugFunction* functions = BUSTER_ARENA_PUSH_ARRAY(arena, DebugFunction, count);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    uint32_t* lines = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    uint32_t* line_addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    StreamError error = functions != 0 && offsets != 0 && lines != 0 && line_addresses != 0 ? STREAM_OK : STREAM_ERROR_MEMORY;
    for (uint32_t f = 0; error == STREAM_OK && f < count; f += 1)
    {
        char* name = program->strtab + program->name_offsets[f];
        functions[f] = (DebugFunction){
            .name = { .pointer = name, .length = strlen(name) },
            .size = sizes[f],
            .first_row = f,
            .row_count = 1,
            .line = f + 1,
        };
        offsets[f] = 0;
        lines[f] = f + 1;
    }
    *unit = (DebugUnit){
        .directory = BUSTER_S8("."),
        .files = &stream_debug_file,
        .file_count = 1,
        .functions = functions,
        .function_count = count,
        .offsets = offsets,
        .lines = lines,
        .row_count = count,
    };
    memset(output, 0, sizeof(*output));
    output->line_addresses = line_addresses;
    output->address_capacity = count;
    // Measured first, which fails for capacity with the sizes set, then written into room of exactly those sizes.
    DebugError written = error == STREAM_OK ? debug_write(unit, output, false) : DEBUG_ERROR_INPUT;
    for (uint32_t section = 0; written == DEBUG_ERROR_CAPACITY && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        output->sections[section].bytes = arena_push(arena, output->sections[section].size, 8);
        output->sections[section].capacity = output->sections[section].bytes != 0 ? output->sections[section].size : 0;
    }
    written = written == DEBUG_ERROR_CAPACITY ? debug_write(unit, output, false) : written;
    written = written == DEBUG_OK && addresses != 0 ? debug_finalize(unit, output, addresses) : written;
    return error != STREAM_OK ? error : written == DEBUG_OK ? STREAM_OK : STREAM_ERROR_DEBUG;
}

// The tail: .eh_frame_hdr, .eh_frame (each compiled function's CIE and FDE, in .text's order, then a terminator), and
// the non-allocated sections, built in `tail` (the bytes from layout->eh_offset to the end of the file), with the layout
// completed. Returns STREAM_OK, or why not.
static StreamError stream_tail(const StreamCode* code, StreamLayout* layout, Arena* arena, uint8_t** tail_bytes, uint64_t* tail_size)
{
    const StreamProgram* program = code->program;
    const uint32_t functions = program->function_count - 1;
    StreamError error = code->described == functions ? STREAM_OK : STREAM_ERROR_UNWIND;
    layout->text_size = code->text_size;
    layout->chunk_count = (code->text_size + layout->chunk_bytes - 1) / layout->chunk_bytes;
    layout->eh_offset = stream_align(layout->text_offset + layout->text_size, BUSTER_STREAM_PAGE_BYTES);
    layout->eh_hdr_size = BUSTER_STREAM_EH_FRAME_HDR_BYTES(functions);
    layout->eh_size = code->eh_size + 4;
    const uint64_t eh_frame_offset = layout->eh_offset + stream_align(layout->eh_hdr_size, 8);
    const uint32_t symbols = program->function_count + program->data_symbol_count;
    // The DWARF at the functions' addresses, after .eh_frame.
    uint64_t* addresses = error == STREAM_OK ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, program->function_count) : 0;
    error = error == STREAM_OK && addresses == 0 ? STREAM_ERROR_MEMORY : error;
    for (uint32_t f = 0; error == STREAM_OK && f < program->function_count; f += 1)
    {
        addresses[f] = code->text_address + code->offsets[f];
    }
    DebugUnit unit;
    DebugOutput debug;
    memset(&debug, 0, sizeof(debug));
    error = error == STREAM_OK ? stream_debug(program, code->sizes, addresses, arena, &unit, &debug) : error;
    uint64_t debug_end = stream_align(eh_frame_offset + layout->eh_size, 8);
    for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        layout->debug_offsets[section] = debug_end;
        debug_end += debug.sections[section].size;
    }
    layout->symtab_offset = stream_align(debug_end, 8);
    layout->symtab_size = (uint64_t)(symbols + 1) * sizeof(Elf64_Sym);
    layout->strtab_offset = layout->symtab_offset + layout->symtab_size;
    layout->strtab_size = program->strtab_size;
    uint8_t* tail = 0;
    uint64_t tail_capacity = 0;
    if (error == STREAM_OK)
    {
        tail_capacity = layout->strtab_offset - layout->eh_offset + layout->strtab_size + sizeof(stream_section_names) + 8 + BUSTER_STREAM_SECTIONS * sizeof(Elf64_Shdr);
        tail = arena_push(arena, tail_capacity, 64);
        error = tail != 0 ? STREAM_OK : STREAM_ERROR_MEMORY;
    }
    if (error == STREAM_OK)
    {
        memset(tail, 0, tail_capacity);
        const uint64_t eh_hdr_address = BUSTER_EXECUTABLE_BASE + layout->eh_offset;
        const uint64_t eh_frame_address = BUSTER_EXECUTABLE_BASE + eh_frame_offset;
        // .eh_frame: the pairs with their initial locations, then the terminator.
        uint8_t* frames = tail + (eh_frame_offset - layout->eh_offset);
        memcpy(frames, code->eh, code->eh_size);
        // .eh_frame_hdr: version 1, .eh_frame's address (pcrel sdata4), the count (udata4) and the table (datarel sdata4).
        uint8_t* header = tail;
        header[0] = 1;
        header[1] = 0x1b;
        header[2] = 0x03;
        header[3] = 0x3b;
        const int32_t frame_pointer = (int32_t)(eh_frame_address - (eh_hdr_address + 4));
        memcpy(header + 4, &frame_pointer, 4);
        memcpy(header + 8, &functions, 4);
        for (uint32_t i = 0; i < functions; i += 1)
        {
            const uint64_t function_address = code->text_address + code->offsets[stream_function_at(program, code->order, i)];
            const uint64_t location_address = eh_frame_address + code->eh_locations[i];
            const int32_t initial = (int32_t)(function_address - location_address);
            memcpy(frames + code->eh_locations[i], &initial, 4);
            const int32_t table[2] = {
                (int32_t)(function_address - eh_hdr_address),
                (int32_t)(location_address - 8 - eh_hdr_address),
            };
            memcpy(header + 12 + 8 * (uint64_t)i, table, sizeof(table));
        }
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            memcpy(tail + (layout->debug_offsets[section] - layout->eh_offset), debug.sections[section].bytes, debug.sections[section].size);
        }
        // .symtab and .strtab: the null symbol, then every function, _start, the result slot and the globals, global.
        Elf64_Sym* table = (Elf64_Sym*)(void*)(tail + (layout->symtab_offset - layout->eh_offset));
        memcpy(tail + (layout->strtab_offset - layout->eh_offset), program->strtab, program->strtab_size);
        for (uint32_t s = 0; s < symbols; s += 1)
        {
            Elf64_Sym symbol;
            memset(&symbol, 0, sizeof(symbol));
            symbol.st_name = program->name_offsets[s];
            if (s < program->function_count)
            {
                symbol.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
                symbol.st_shndx = 3;
                symbol.st_value = code->text_address + code->offsets[s];
                symbol.st_size = code->sizes[s];
            }
            else
            {
                symbol.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
                symbol.st_shndx = 2;
                symbol.st_value = code->data_address + program->data_offsets[s - program->function_count];
                symbol.st_size = program->data_sizes[s - program->function_count];
            }
            memcpy(&table[s + 1], &symbol, sizeof(symbol));
        }
        layout->shstrtab_offset = layout->strtab_offset + layout->strtab_size;
        layout->shstrtab_size = sizeof(stream_section_names);
        memcpy(tail + (layout->shstrtab_offset - layout->eh_offset), stream_section_names, sizeof(stream_section_names));
        layout->section_offset = stream_align(layout->shstrtab_offset + layout->shstrtab_size, 8);
        Elf64_Shdr sections[BUSTER_STREAM_SECTIONS];
        memset(sections, 0, sizeof(sections));
        const uint64_t data_address = BUSTER_EXECUTABLE_BASE + layout->data_offset;
        sections[1] = (Elf64_Shdr){ .sh_type = SHT_NOTE, .sh_flags = SHF_ALLOC, .sh_addr = BUSTER_EXECUTABLE_BASE + BUSTER_STREAM_NOTE_OFFSET,
                                    .sh_offset = BUSTER_STREAM_NOTE_OFFSET, .sh_size = BUSTER_EXECUTABLE_NOTE_BYTES, .sh_addralign = 4 };
        sections[2] = (Elf64_Shdr){ .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_WRITE, .sh_addr = data_address, .sh_offset = layout->data_offset,
                                    .sh_size = program->data_size, .sh_addralign = 16 };
        sections[3] = (Elf64_Shdr){ .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_EXECINSTR, .sh_addr = code->text_address, .sh_offset = layout->text_offset,
                                    .sh_size = layout->text_size, .sh_addralign = BUSTER_STREAM_FUNCTION_ALIGNMENT };
        sections[4] = (Elf64_Shdr){ .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC, .sh_addr = eh_hdr_address, .sh_offset = layout->eh_offset,
                                    .sh_size = layout->eh_hdr_size, .sh_addralign = 4 };
        sections[5] = (Elf64_Shdr){ .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC, .sh_addr = eh_frame_address, .sh_offset = eh_frame_offset,
                                    .sh_size = layout->eh_size, .sh_addralign = 8 };
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            sections[6 + section] = (Elf64_Shdr){ .sh_type = SHT_PROGBITS, .sh_offset = layout->debug_offsets[section], .sh_size = debug.sections[section].size,
                                                  .sh_addralign = 1 };
        }
        sections[11] = (Elf64_Shdr){ .sh_type = SHT_SYMTAB, .sh_offset = layout->symtab_offset, .sh_size = layout->symtab_size, .sh_link = 12, .sh_info = 1,
                                     .sh_addralign = 8, .sh_entsize = sizeof(Elf64_Sym) };
        sections[12] = (Elf64_Shdr){ .sh_type = SHT_STRTAB, .sh_offset = layout->strtab_offset, .sh_size = layout->strtab_size, .sh_addralign = 1 };
        sections[13] = (Elf64_Shdr){ .sh_type = SHT_STRTAB, .sh_offset = layout->shstrtab_offset, .sh_size = layout->shstrtab_size, .sh_addralign = 1 };
        for (uint32_t s = 0; s < BUSTER_STREAM_SECTIONS; s += 1)
        {
            sections[s].sh_name = stream_section_name_offsets[s];
        }
        memcpy(tail + (layout->section_offset - layout->eh_offset), sections, sizeof(sections));
        layout->file_size = layout->section_offset + sizeof(sections);
        *tail_bytes = tail;
        *tail_size = layout->file_size - layout->eh_offset;
    }
    return error;
}

// The headers' page into `bytes` (BUSTER_STREAM_PAGE_BYTES): the ELF header, the program headers and the note, its
// hash zero.
static void stream_headers(const StreamProgram* program, const StreamCode* code, const StreamLayout* layout, uint8_t* bytes)
{
    memset(bytes, 0, BUSTER_STREAM_PAGE_BYTES);
    Elf64_Ehdr header;
    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_ident[EI_OSABI] = ELFOSABI_NONE;
    header.e_type = ET_EXEC;
    header.e_machine = EM_X86_64;
    header.e_version = EV_CURRENT;
    header.e_entry = code->text_address + code->offsets[program->function_count - 1];
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = layout->section_offset;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = BUSTER_STREAM_HEADERS;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = BUSTER_STREAM_SECTIONS;
    header.e_shstrndx = BUSTER_STREAM_SECTIONS - 1;
    memcpy(bytes, &header, sizeof(header));
    const uint64_t eh_total = stream_align(layout->eh_hdr_size, 8) + layout->eh_size;
    const Elf64_Phdr segments[BUSTER_STREAM_HEADERS] = {
        { .p_type = PT_LOAD, .p_flags = PF_R, .p_offset = 0, .p_vaddr = BUSTER_EXECUTABLE_BASE, .p_paddr = BUSTER_EXECUTABLE_BASE,
          .p_filesz = BUSTER_STREAM_NOTE_OFFSET + BUSTER_EXECUTABLE_NOTE_BYTES, .p_memsz = BUSTER_STREAM_NOTE_OFFSET + BUSTER_EXECUTABLE_NOTE_BYTES,
          .p_align = BUSTER_STREAM_PAGE_BYTES },
        { .p_type = PT_LOAD, .p_flags = PF_R | PF_W, .p_offset = layout->data_offset, .p_vaddr = code->data_address, .p_paddr = code->data_address,
          .p_filesz = program->data_size, .p_memsz = program->data_size, .p_align = BUSTER_STREAM_PAGE_BYTES },
        { .p_type = PT_LOAD, .p_flags = PF_R | PF_X, .p_offset = layout->text_offset, .p_vaddr = code->text_address, .p_paddr = code->text_address,
          .p_filesz = layout->text_size, .p_memsz = layout->text_size, .p_align = BUSTER_STREAM_PAGE_BYTES },
        { .p_type = PT_LOAD, .p_flags = PF_R, .p_offset = layout->eh_offset, .p_vaddr = BUSTER_EXECUTABLE_BASE + layout->eh_offset,
          .p_paddr = BUSTER_EXECUTABLE_BASE + layout->eh_offset, .p_filesz = eh_total, .p_memsz = eh_total, .p_align = BUSTER_STREAM_PAGE_BYTES },
        { .p_type = PT_NOTE, .p_flags = PF_R, .p_offset = BUSTER_STREAM_NOTE_OFFSET, .p_vaddr = BUSTER_EXECUTABLE_BASE + BUSTER_STREAM_NOTE_OFFSET,
          .p_paddr = BUSTER_EXECUTABLE_BASE + BUSTER_STREAM_NOTE_OFFSET, .p_filesz = BUSTER_EXECUTABLE_NOTE_BYTES, .p_memsz = BUSTER_EXECUTABLE_NOTE_BYTES,
          .p_align = 4 },
        { .p_type = PT_GNU_EH_FRAME, .p_flags = PF_R, .p_offset = layout->eh_offset, .p_vaddr = BUSTER_EXECUTABLE_BASE + layout->eh_offset,
          .p_paddr = BUSTER_EXECUTABLE_BASE + layout->eh_offset, .p_filesz = layout->eh_hdr_size, .p_memsz = layout->eh_hdr_size, .p_align = 4 },
        { .p_type = PT_GNU_STACK, .p_flags = PF_R | PF_W, .p_align = 16 },
    };
    memcpy(bytes + sizeof(Elf64_Ehdr), segments, sizeof(segments));
    const Elf64_Nhdr note = {
        .n_namesz = 4,
        .n_descsz = BUSTER_EXECUTABLE_BUILD_ID_BYTES,
        .n_type = NT_GNU_BUILD_ID,
    };
    memcpy(bytes + BUSTER_STREAM_NOTE_OFFSET, &note, sizeof(note));
    memcpy(bytes + BUSTER_STREAM_NOTE_OFFSET + sizeof(note), "GNU", 4);
}

static void stream_hash_piece(const uint8_t* bytes, uint64_t length, uint8_t* output)
{
    ExecutableHash hash;
    executable_hash_begin(&hash);
    executable_hash_bytes(&hash, bytes, length);
    executable_hash_end(&hash, output);
}

// Zeros for the gaps between the pieces, at most a page each.
static const uint8_t stream_zeros[BUSTER_STREAM_PAGE_BYTES];

static bool stream_pwrite(int descriptor, const uint8_t* bytes, uint64_t length, uint64_t offset)
{
    uint64_t written = 0;
    bool result = true;
    while (result && written < length)
    {
        const ssize_t count = pwrite(descriptor, bytes + written, length - written, (off_t)(offset + written));
        result = count > 0;
        written += result ? (uint64_t)count : 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The schedules

// The writer lane's word, besides the chunks made ready (times 2: lane_wait keeps bit 0): lane 0 has made ready every
// chunk there will be, or failed.
#define BUSTER_STREAM_READY_DONE 0x80000000u

// What a write's lanes share: the code, the layout, the file, the pieces' hashes (the headers', .data's, each chunk's
// and the tail's), and with a writer lane the word through which lane 0 makes chunks ready and .text's final size,
// which lane 1 reads once the word says done.
typedef struct StreamWrite StreamWrite;
struct StreamWrite
{
    const StreamOptions* options;
    StreamResult* result;
    Arena* arena;
    StreamCode code;
    StreamLayout layout;
    int descriptor;
    uint8_t* hashes;
    uint64_t flushed;
    uint64_t final_text_size;
    _Alignas(64) uint32_t ready;
    bool write_failed;
};

// Chunk `chunk` of .text, hashed and written: a whole chunk, or the last one up to `text_size`.
static bool stream_write_chunk(StreamWrite* write, uint64_t chunk, uint64_t text_size)
{
    const uint64_t begin = chunk * write->layout.chunk_bytes;
    const uint64_t end = begin + write->layout.chunk_bytes < text_size ? begin + write->layout.chunk_bytes : text_size;
    stream_hash_piece(write->code.text + begin, end - begin, write->hashes + (2 + chunk) * BUSTER_EXECUTABLE_BUILD_ID_BYTES);
    return stream_pwrite(write->descriptor, write->code.text + begin, end - begin, write->layout.text_offset + begin);
}

// The chunks that are whole and wait for no field, from `flushed` on, made ready: written now, or with a writer lane
// published for it. With `last`, every chunk, the last one partial.
static void stream_flush(StreamWrite* write, bool last)
{
    StreamCode* code = &write->code;
    while (code->pending < code->fixup_count && code->patched[code->pending] != 0)
    {
        code->pending += 1;
    }
    const uint64_t waits = code->pending < code->fixup_count ? code->fields[code->pending] : UINT64_MAX;
    const uint64_t chunk_bytes = write->layout.chunk_bytes;
    uint64_t ready = write->flushed;
    while ((ready + 1) * chunk_bytes <= code->text_size && (ready + 1) * chunk_bytes <= waits)
    {
        ready += 1;
    }
    if (last)
    {
        ready = (code->text_size + chunk_bytes - 1) / chunk_bytes;
    }
    if (write->options->schedule == STREAM_SCHEDULE_WRITER)
    {
        write->flushed = ready;
        write->final_text_size = code->text_size;
        lane_publish(&write->ready, (uint32_t)(2 * ready) | (last ? BUSTER_STREAM_READY_DONE : 0));
    }
    else
    {
        for (uint64_t chunk = write->flushed; chunk < ready; chunk += 1)
        {
            write->write_failed = !stream_write_chunk(write, chunk, code->text_size) || write->write_failed;
        }
        write->flushed = ready;
    }
}

// Lane 1 of a write by a writer lane: each chunk as lane 0 makes it ready, until the word says done. Only the last
// chunk, made ready with the done bit, may be partial, so .text's size is read only then.
static void stream_writer_lane(StreamWrite* write)
{
    uint64_t written = 0;
    uint32_t seen = 0;
    bool done = false;
    while (!done)
    {
        seen = lane_wait(&write->ready, seen);
        done = (seen & BUSTER_STREAM_READY_DONE) != 0;
        const uint64_t ready = (seen & ~BUSTER_STREAM_READY_DONE) / 2;
        const uint64_t text_size = done ? write->final_text_size : UINT64_MAX;
        for (uint64_t chunk = written; chunk < ready; chunk += 1)
        {
            write->write_failed = !stream_write_chunk(write, chunk, text_size) || write->write_failed;
        }
        written = ready;
    }
}

// Lane 0: compiles every function in the order, streaming its chunks or not, then the tail.
static void stream_compile_lane(StreamWrite* write)
{
    const StreamOptions* options = write->options;
    StreamResult* result = write->result;
    StreamCode* code = &write->code;
    const StreamProgram* program = code->program;
    const bool streamed = options->schedule != STREAM_SCHEDULE_END;
    Arena columns[5];
    for (uint32_t c = 0; c < 5; c += 1)
    {
        columns[c] = arena_reserve(1ull << 34);
    }
    code->fields = (uint32_t*)(void*)columns[0].base;
    code->ends = (uint32_t*)(void*)columns[1].base;
    code->targets = (uint32_t*)(void*)columns[2].base;
    code->patched = columns[3].base;
    code->waiting = (uint32_t*)(void*)columns[4].base;
    StreamError error = columns[0].base != 0 && columns[1].base != 0 && columns[2].base != 0 && columns[3].base != 0 && columns[4].base != 0 ? STREAM_OK : STREAM_ERROR_MEMORY;
    if (error == STREAM_OK && streamed)
    {
        // .data first: its bytes are final.
        stream_hash_piece(program->data, program->data_size, write->hashes + BUSTER_EXECUTABLE_BUILD_ID_BYTES);
        error = stream_pwrite(write->descriptor, program->data, program->data_size, write->layout.data_offset) &&
                stream_pwrite(write->descriptor, stream_zeros, write->layout.text_offset - write->layout.data_offset - program->data_size,
                              write->layout.data_offset + program->data_size) ? STREAM_OK : STREAM_ERROR_FILE;
    }
    for (uint32_t i = 0; error == STREAM_OK && i < program->function_count; i += 1)
    {
        const uint32_t function = stream_function_at(program, options->order, i);
        error = stream_compile(code, columns, function, streamed);
        result->function = function;
        code->emitted = i + 1 < program->function_count ? i + 1 : i;
        if (error == STREAM_OK && streamed)
        {
            // The pairs of each eight functions compiled, then the chunks ready.
            error = stream_unwind(code, code->emitted / BUSTER_UNWIND_BATCH * BUSTER_UNWIND_BATCH);
            stream_flush(write, false);
        }
    }
    result->compiled = stream_now();
    write->layout.text_size = code->text_size;
    write->layout.chunk_count = (code->text_size + write->layout.chunk_bytes - 1) / write->layout.chunk_bytes;
    result->chunks_streamed = write->flushed;
    if (error == STREAM_OK && !streamed)
    {
        stream_hash_piece(program->data, program->data_size, write->hashes + BUSTER_EXECUTABLE_BUILD_ID_BYTES);
        error = stream_pwrite(write->descriptor, program->data, program->data_size, write->layout.data_offset) &&
                stream_pwrite(write->descriptor, stream_zeros, write->layout.text_offset - write->layout.data_offset - program->data_size,
                              write->layout.data_offset + program->data_size) ? STREAM_OK : STREAM_ERROR_FILE;
        for (uint64_t fixup = 0; fixup < code->fixup_count; fixup += 1)
        {
            stream_patch(code, fixup);
        }
        code->pending = code->fixup_count;
    }
    if (error == STREAM_OK)
    {
        // The rest of .text: every chunk not written yet; and the pairs not written yet.
        stream_flush(write, true);
        error = stream_unwind(code, code->emitted);
    }
    result->flushed = stream_now();
    if (options->schedule == STREAM_SCHEDULE_WRITER && error != STREAM_OK)
    {
        // After a failure the writer lane writes what was ready and stops.
        lane_publish(&write->ready, (uint32_t)(2 * write->flushed) | BUSTER_STREAM_READY_DONE);
    }
    uint8_t* tail = 0;
    uint64_t tail_size = 0;
    if (error == STREAM_OK)
    {
        error = stream_tail(code, &write->layout, write->arena, &tail, &tail_size);
    }
    result->tail_built = stream_now();
    if (error == STREAM_OK)
    {
        stream_hash_piece(tail, tail_size, write->hashes + (2 + write->layout.chunk_count) * BUSTER_EXECUTABLE_BUILD_ID_BYTES);
        const uint64_t text_end = write->layout.text_offset + write->layout.text_size;
        error = stream_pwrite(write->descriptor, stream_zeros, write->layout.eh_offset - text_end, text_end) &&
                stream_pwrite(write->descriptor, tail, tail_size, write->layout.eh_offset) ? STREAM_OK : STREAM_ERROR_FILE;
    }
    result->tail_written = stream_now();
    result->fixups = code->fixup_count;
    result->forward_fixups = code->forward;
    result->records = code->records;
    for (uint32_t c = 0; c < 5; c += 1)
    {
        arena_release(&columns[c]);
    }
    result->error = error;
}

// A write's lanes: lane 0 compiles, and with a writer lane, lane 1 writes the chunks; then both sync, so that the
// headers, whose build ID needs every chunk's hash, come after every chunk.
static void stream_lane(void* argument)
{
    StreamWrite* write = argument;
    if (lane_index() == 0)
    {
        stream_compile_lane(write);
    }
    else
    {
        stream_writer_lane(write);
    }
    lane_sync();
}

// Writes the program's executable to options->path by options->schedule, in options->order. Returns whether it did;
// `result` says what it did, or why not, and when.
static bool stream_write(const StreamProgram* program, const StreamOptions* options, StreamResult* result)
{
    memset(result, 0, sizeof(*result));
    result->start = stream_now();
    Arena arena = arena_reserve(1ull << 36);
    Arena text = arena_reserve(1ull << 34);
    StreamWrite write;
    memset(&write, 0, sizeof(write));
    write.options = options;
    write.result = result;
    write.arena = &arena;
    write.descriptor = -1;
    StreamCode* code = &write.code;
    code->program = program;
    code->scratch = &arena;
    code->text = text.base;
    code->text_capacity = text.capacity;
    code->order = options->order;
    code->eh_capacity = (uint64_t)program->function_count * BUSTER_UNWIND_TEMPLATE_BYTES + 64;
    code->eh = arena_push(&arena, code->eh_capacity, 64);
    code->eh_locations = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, program->function_count);
    write.layout = stream_layout_begin(program, options->chunk_bytes != 0 ? options->chunk_bytes : BUSTER_STREAM_CHUNK_BYTES);
    code->data_address = BUSTER_EXECUTABLE_BASE + write.layout.data_offset;
    code->text_address = BUSTER_EXECUTABLE_BASE + write.layout.text_offset;
    const uint32_t functions = program->function_count;
    // The pieces' hashes: the headers, .data, as many chunks as .text's room holds, and the tail.
    const uint64_t pieces = text.capacity / write.layout.chunk_bytes + 4;
    code->offsets = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    code->sizes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    code->compiled = BUSTER_ARENA_PUSH_ARRAY(&arena, bool, functions);
    code->unwind = BUSTER_ARENA_PUSH_ARRAY(&arena, UnwindFunction, functions);
    code->waiting_heads = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    write.hashes = arena_push(&arena, pieces * BUSTER_EXECUTABLE_BUILD_ID_BYTES, 64);
    bool valid = arena.base != 0 && text.base != 0 && code->offsets != 0 && code->sizes != 0 && code->compiled != 0 && code->unwind != 0 &&
                 code->waiting_heads != 0 && write.hashes != 0 && code->eh != 0 && code->eh_locations != 0;
    if (valid)
    {
        memset(code->compiled, 0, functions * sizeof(bool));
        memset(code->waiting_heads, 0, functions * sizeof(uint32_t));
        char path[PATH_MAX];
        valid = options->path.length < sizeof(path);
        if (valid)
        {
            memcpy(path, options->path.pointer, options->path.length);
            path[options->path.length] = 0;
            // Overwritten in place, as executable.c overwrites a regular file, never truncated on open: on btrfs and ext4 a
            // file truncated and written again is flushed when it is closed (their replace-by-truncate heuristic).
            write.descriptor = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0755);
            valid = write.descriptor >= 0;
            result->system_error = valid ? 0 : errno;
        }
        result->error = valid ? STREAM_OK : STREAM_ERROR_FILE;
    }
    else
    {
        result->error = STREAM_ERROR_MEMORY;
    }
    if (valid)
    {
        const uint32_t lanes = options->schedule == STREAM_SCHEDULE_WRITER ? 2 : 1;
        valid = lane_run(lanes, stream_lane, &write, options->schedule == STREAM_SCHEDULE_WRITER ? options->cpus : 0);
        valid = valid && result->error == STREAM_OK && !write.write_failed;
        result->error = result->error == STREAM_OK && !valid ? STREAM_ERROR_FILE : result->error;
    }
    if (valid)
    {
        // The headers: hashed with the note's hash zero, then the build ID (the hash of the pieces' hashes) in place.
        uint8_t headers[BUSTER_STREAM_PAGE_BYTES];
        stream_headers(program, code, &write.layout, headers);
        const uint64_t header_bytes = BUSTER_STREAM_NOTE_OFFSET + BUSTER_EXECUTABLE_NOTE_BYTES;
        stream_hash_piece(headers, header_bytes, write.hashes);
        stream_hash_piece(write.hashes, (3 + write.layout.chunk_count) * BUSTER_EXECUTABLE_BUILD_ID_BYTES, result->build_id);
        memcpy(headers + BUSTER_STREAM_NOTE_OFFSET + sizeof(Elf64_Nhdr) + 4, result->build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES);
        valid = stream_pwrite(write.descriptor, headers, BUSTER_STREAM_PAGE_BYTES, 0) && ftruncate(write.descriptor, (off_t)write.layout.file_size) == 0;
        result->error = valid ? STREAM_OK : STREAM_ERROR_FILE;
        result->system_error = valid ? 0 : errno;
        result->file_size = write.layout.file_size;
        result->text_size = write.layout.text_size;
        result->chunks = write.layout.chunk_count;
    }
    result->headers = stream_now();
    if (write.descriptor >= 0)
    {
        valid = close(write.descriptor) == 0 && valid;
    }
    result->closed = stream_now();
    arena_release(&text);
    arena_release(&arena);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The direct path's writer on the same code

// What stream_write_link did, and when: its start, the last function compiled, the Link built and the file closed.
typedef struct StreamLinkResult StreamLinkResult;
struct StreamLinkResult
{
    StreamError error;
    LinkError link_error;
    uint64_t start;
    uint64_t compiled;
    uint64_t linked;
    uint64_t closed;
    ExecutableResult written;
};

typedef struct StreamLinkWrite StreamLinkWrite;
struct StreamLinkWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    bool succeeded;
};

static void stream_link_lane(void* argument)
{
    StreamLinkWrite* write = argument;
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

// The program compiled as stream_write compiles it at the end, then as a Link (a TEXT atom per function in the order,
// with its fields as CALL fixups to functions and PC32 ones to .data, a DATA atom per global and the result slot's, an
// EH_FRAME atom per function's CIE and FDE with a PC32 fixup at its initial location, and every symbol named), written
// with the same DWARF by executable_write on `lanes` lanes: the direct path's static executable, in lld's order.
static bool stream_write_link(const StreamProgram* program, String8 path, StreamOrder order, uint32_t lanes, StreamLinkResult* result)
{
    memset(result, 0, sizeof(*result));
    result->start = stream_now();
    Arena arena = arena_reserve(1ull << 36);
    Arena text = arena_reserve(1ull << 34);
    Arena names = arena_reserve(1ull << 34);
    Arena tables = arena_reserve(1ull << 36);
    StreamCode code;
    memset(&code, 0, sizeof(code));
    code.program = program;
    code.scratch = &arena;
    code.text = text.base;
    code.text_capacity = text.capacity;
    code.order = order;
    code.eh_capacity = (uint64_t)program->function_count * BUSTER_UNWIND_TEMPLATE_BYTES + 64;
    code.eh = arena_push(&arena, code.eh_capacity, 64);
    code.eh_locations = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, program->function_count);
    const uint32_t functions = program->function_count;
    code.offsets = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    code.sizes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    code.compiled = BUSTER_ARENA_PUSH_ARRAY(&arena, bool, functions);
    code.unwind = BUSTER_ARENA_PUSH_ARRAY(&arena, UnwindFunction, functions);
    code.waiting_heads = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, functions);
    Arena columns[5];
    for (uint32_t c = 0; c < 5; c += 1)
    {
        columns[c] = arena_reserve(1ull << 34);
    }
    code.fields = (uint32_t*)(void*)columns[0].base;
    code.ends = (uint32_t*)(void*)columns[1].base;
    code.targets = (uint32_t*)(void*)columns[2].base;
    code.patched = columns[3].base;
    code.waiting = (uint32_t*)(void*)columns[4].base;
    StreamError error = arena.base != 0 && text.base != 0 && names.base != 0 && tables.base != 0 && code.offsets != 0 && code.sizes != 0 && code.compiled != 0 &&
                        code.unwind != 0 && code.waiting_heads != 0 && code.eh != 0 && code.eh_locations != 0 && columns[0].base != 0 && columns[1].base != 0 &&
                        columns[2].base != 0 && columns[3].base != 0 && columns[4].base != 0 ? STREAM_OK : STREAM_ERROR_MEMORY;
    if (error == STREAM_OK)
    {
        memset(code.compiled, 0, functions * sizeof(bool));
        memset(code.waiting_heads, 0, functions * sizeof(uint32_t));
    }
    // Each function's first field: the fields come in the order's turn.
    uint64_t* first_fixups = error == STREAM_OK ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, (uint64_t)functions + 1) : 0;
    error = error == STREAM_OK && first_fixups == 0 ? STREAM_ERROR_MEMORY : error;
    for (uint32_t i = 0; error == STREAM_OK && i < functions; i += 1)
    {
        const uint32_t function = stream_function_at(program, order, i);
        first_fixups[function] = code.fixup_count;
        error = stream_compile(&code, columns, function, false);
    }
    result->compiled = stream_now();
    // The unwind pairs, in the order.
    const uint32_t described = functions - 1;
    if (error == STREAM_OK)
    {
        error = stream_unwind(&code, described);
    }
    const uint32_t symbol_count = functions + program->data_symbol_count;
    const LinkCapacity capacity = {
        .names = symbol_count + 2,
        .atoms = symbol_count + described + 2,
        .symbols = symbol_count + 2,
        .fixups = (uint32_t)code.fixup_count + described + 2,
        .wide_addends = 2,
        .priorities = 2,
    };
    Link link;
    memset(&link, 0, sizeof(link));
    const bool initialized = error == STREAM_OK && code.fixup_count + described < UINT32_MAX && link_initialize(&link, &tables, &names, capacity);
    error = error == STREAM_OK && !initialized ? STREAM_ERROR_LINK : error;
    bool linked = error == STREAM_OK;
    for (uint32_t s = 0; linked && s < symbol_count; s += 1)
    {
        char name[32];
        const uint32_t length = stream_symbol_name(program, s, name);
        const uint8_t flags = (s < functions ? BUSTER_LINK_SYMBOL_FUNCTION : BUSTER_LINK_SYMBOL_OBJECT) | BUSTER_LINK_SYMBOL_GLOBAL;
        linked = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, length), 0, 0, 0, flags, BUSTER_LINK_SOURCE_PROGRAM) == s + 1;
    }
    for (uint32_t i = 0; linked && i < functions; i += 1)
    {
        const uint32_t function = stream_function_at(program, order, i);
        const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, code.sizes[function], code.text + code.offsets[function], 0);
        linked = atom != 0 && link_define_symbol(&link, function + 1, atom, 0, code.sizes[function]);
        const uint64_t end = i + 1 < functions ? first_fixups[stream_function_at(program, order, i + 1)] : code.fixup_count;
        for (uint64_t fixup = first_fixups[function]; linked && fixup < end; fixup += 1)
        {
            // The program's symbols are the Link's, from 1: the functions, then the data symbols.
            const uint32_t target = code.targets[fixup];
            const int64_t addend = -(int64_t)(code.ends[fixup] - code.fields[fixup]);
            linked = link_add_fixup(&link, atom, code.fields[fixup] - code.offsets[function], target < functions ? BUSTER_LINK_FIXUP_CALL : BUSTER_LINK_FIXUP_PC32,
                                    target + 1, addend) != 0;
        }
    }
    // .data: each data symbol an atom of its bytes, the result slot first.
    for (uint32_t d = 0; linked && d < program->data_symbol_count; d += 1)
    {
        const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, program->data_sizes[d], program->data + program->data_offsets[d], 0);
        linked = atom != 0 && link_define_symbol(&link, functions + d + 1, atom, 0, program->data_sizes[d]);
    }
    // The unwind pairs: each from 48 bytes before its initial location to the next pair.
    for (uint32_t i = 0; linked && i < described; i += 1)
    {
        const uint64_t begin = code.eh_locations[i] - BUSTER_UNWIND_LOCATION;
        const uint64_t end = i + 1 < described ? code.eh_locations[i + 1] - BUSTER_UNWIND_LOCATION : code.eh_size;
        const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_EH_FRAME, 3, end - begin, code.eh + begin, 0);
        linked = atom != 0 && link_add_fixup(&link, atom, BUSTER_UNWIND_LOCATION, BUSTER_LINK_FIXUP_PC32, stream_function_at(program, order, i) + 1, 0) != 0;
    }
    SymbolId* ids = linked ? BUSTER_ARENA_PUSH_ARRAY(&arena, SymbolId, symbol_count) : 0;
    linked = linked && ids != 0;
    for (uint32_t s = 0; linked && s < symbol_count; s += 1)
    {
        ids[s] = s + 1;
    }
    result->link_error = linked ? link_check(&link) : link.error;
    linked = linked && result->link_error == LINK_ERROR_NONE;
    error = error == STREAM_OK && !linked ? STREAM_ERROR_LINK : error;
    // The same DWARF, its address slots left to the writer, which finalizes them at the functions' places.
    DebugUnit unit;
    DebugOutput debug;
    memset(&debug, 0, sizeof(debug));
    error = error == STREAM_OK ? stream_debug(program, code.sizes, 0, &arena, &unit, &debug) : error;
    result->linked = stream_now();
    bool succeeded = error == STREAM_OK;
    if (succeeded)
    {
        const ExecutableSymbols symbols = {
            .ids = ids,
            .count = symbol_count,
        };
        // The functions' symbols are the first of `ids`, in the program's order, the DWARF's.
        ExecutableDebug attached = {
            .functions = ids,
            .line_addresses = debug.line_addresses,
            .function_count = functions,
        };
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            attached.sections[section] = (String8){ .pointer = (char*)debug.sections[section].bytes, .length = debug.sections[section].size };
        }
        StreamLinkWrite write = {
            .link = &link,
            .options = {
                .path = path,
                .entry = functions,
                .debug = &attached,
                .symbols = &symbols,
            },
        };
        succeeded = lane_run(lanes, stream_link_lane, &write, 0) && write.succeeded;
        result->written = write.result;
        error = succeeded ? STREAM_OK : STREAM_ERROR_FILE;
    }
    result->closed = stream_now();
    result->error = error;
    for (uint32_t c = 0; c < 5; c += 1)
    {
        arena_release(&columns[c]);
    }
    arena_release(&tables);
    arena_release(&names);
    arena_release(&text);
    arena_release(&arena);
    return succeeded;
}
