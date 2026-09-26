/* Research only. No production defaults or data structures are changed.
   The runner extracts the pinned production resolver into reference.inc and
   adds only one loop-visit counter. See README.md for the adapter boundary. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct ByteSlice { u8* pointer; u64 length; } ByteSlice;
typedef struct String8 { char* pointer; u64 length; } String8;
typedef enum ObjectSectionKind
{
    OBJECT_SECTION_TEXT, OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS,
    OBJECT_SECTION_DEBUG_CODEVIEW_TYPES, OBJECT_SECTION_COUNT
} ObjectSectionKind;
typedef enum ObjectRelocationKind
{
    OBJECT_RELOCATION_COFF_SECREL32, OBJECT_RELOCATION_COFF_SECTION16,
    OBJECT_RELOCATION_OTHER
} ObjectRelocationKind;
typedef struct ObjectSection { ByteSlice data; } ObjectSection;
typedef struct ObjectSymbol { u64 value; u32 section; } ObjectSymbol;
typedef struct ObjectRelocation
{
    s64 addend;
    u64 offset;
    u32 section;
    u32 symbol;
    u32 comdat;
    ObjectRelocationKind kind;
} ObjectRelocation;
typedef struct ObjectDebugModule
{
    String8 name;
    u64 code_offset, code_size, symbols_offset, symbols_size, types_offset, types_size;
} ObjectDebugModule;
typedef struct ObjectFile
{
    ObjectSection* sections;
    ObjectSymbol* symbols;
    ObjectRelocation* relocations;
    ObjectDebugModule* debug_modules;
    u32 symbol_count, relocation_count, debug_module_count;
} ObjectFile;
typedef struct Arena { u8* bytes; size_t capacity, used; } Arena;

static void* allocate(size_t count, size_t width, bool clear)
{
    void* result = 0;
    if (width && count > SIZE_MAX / width)
    {
        fprintf(stderr, "allocation overflow\n");
        exit(2);
    }
    size_t bytes = count ? count * width : 1;
    result = clear ? calloc(1, bytes) : malloc(bytes);
    if (!result)
    {
        fprintf(stderr, "allocation failed\n");
        exit(2);
    }
    return result;
}

static void* arena_bytes(Arena* arena, size_t width, u64 count)
{
    void* result = 0;
    size_t aligned = (arena->used + 7) & ~(size_t)7;
    if (count > SIZE_MAX / width || aligned > arena->capacity || width * count > arena->capacity - aligned)
    {
        fprintf(stderr, "fixture arena exhausted\n");
        exit(2);
    }
    result = arena->bytes + aligned;
    arena->used = aligned + (size_t)count * width;
    return result;
}
#define arena_allocate(arena, type, count) ((type*)arena_bytes((arena), sizeof(type), (count)))
static u64 resolver_visits;
#include "reference.inc"

typedef struct Span { u32 first, count; } Span;
typedef struct Certificate
{
    ObjectRelocation const* rows;
    u64 epoch;
    u32 count;
    bool valid;
} Certificate;
typedef struct Work
{
    u64 setup_rows, key_comparisons, zero_slots, packing_rows;
    u64 sort_moves, metadata_writes, auxiliary_bytes, resolver_rows, output_bytes;
} Work;
typedef struct Fixture
{
    ObjectFile object;
    Span* spans;
    ObjectRelocation* old_rows; /* Test-only ownership for the stale-pointer case. */
    Certificate certificate;
    Work construction;
    u64 epoch;
    u32 objects, modules_per_object, mode;
    u64 input_bytes;
} Fixture;

typedef struct Boundary { u64 begin, end; u32 module, reserved; } Boundary;
typedef struct Index
{
    ObjectRelocation* packed;
    u32* starts;
    bool valid;
    Work work;
} Index;

static u64 digest(u64 value, u8 const* bytes, size_t count)
{
    for (size_t index = 0; index < count; index += 1)
    {
        value = (value ^ bytes[index]) * UINT64_C(1099511628211);
    }
    return value;
}

/* Producer-owned construction. Input sections are disjoint by placement;
   records are appended in input order after the modeled COMDAT keep decision.
   No offset-to-owner search is used to create a span. */
static Fixture fixture_build(u32 objects, u32 modules, u32 keys, u32 updates, u32 noise, u32 symbols, u32 mode)
{
    Fixture result = {0};
    result.objects = objects;
    result.modules_per_object = modules;
    result.mode = mode;
    result.epoch = 1;
    u32 module_count = objects * modules;
    u64 module_size = (u64)keys * 8 + 32;
    u64 contribution_size = module_size * modules;
    u64 bytes = contribution_size * objects;
    u32 per_module = keys * updates * 2 + noise;
    ObjectFile* object = &result.object;
    object->symbol_count = symbols;
    object->debug_module_count = module_count;
    object->sections = allocate(OBJECT_SECTION_COUNT, sizeof(*object->sections), true);
    object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data = (ByteSlice){allocate((size_t)bytes, 1, false), bytes};
    for (u64 byte = 0; byte < bytes; byte += 1)
    {
        object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.pointer[byte] = (u8)(byte * 13 + 7);
    }
    object->symbols = allocate(symbols, sizeof(*object->symbols), true);
    for (u32 symbol = 0; symbol < symbols; symbol += 1)
    {
        object->symbols[symbol] = (ObjectSymbol){(u64)symbol * 17 + 128, OBJECT_SECTION_TEXT};
    }
    object->debug_modules = allocate(module_count, sizeof(*object->debug_modules), true);
    object->relocations = allocate((size_t)module_count * per_module, sizeof(*object->relocations), false);
    result.spans = module_count > 1 ? allocate(module_count, sizeof(*result.spans), false) : 0;
    result.certificate.valid = module_count > 1;
    if (module_count > 1)
    {
        result.construction.auxiliary_bytes = (u64)module_count * sizeof(Span) + sizeof(Certificate);
    }
    for (u32 input = 0; input < objects; input += 1)
    {
        u64 base = contribution_size * input;
        u32 first = object->relocation_count;
        for (u32 local_module = 0; local_module < modules; local_module += 1)
        {
            u32 module = input * modules + local_module;
            u64 local_begin = (u64)local_module * module_size;
            u64 local_size = module_size;
            if (mode == 1 && input == 0 && local_module + 1 == modules)
            {
                local_size += module_size; /* Cross-contribution module: fallback, not rejection. */
            }
            if (mode == 14 && local_module && modules > 1)
            {
                local_begin = 0; /* Overlap inside one contribution is safe for spans. */
            }
            object->debug_modules[module].symbols_offset = base + local_begin;
            object->debug_modules[module].symbols_size = local_size;
            if (module_count > 1)
            {
                result.construction.metadata_writes += 1; /* Containment guard. */
                if (local_begin > contribution_size || local_size > contribution_size - local_begin)
                {
                    result.certificate.valid = false;
                }
            }
            for (u32 row = 0; row < per_module; row += 1)
            {
                bool codeview = row < keys * updates * 2;
                u32 pair = row / 2;
                u64 local_offset = (u64)local_module * module_size + 8 * (keys - 1 - pair % keys) + (row % 2 ? 4 : 0);
                ObjectRelocation relocation = {
                    .addend = (s64)(pair / keys),
                    .offset = local_offset,
                    .section = codeview ? OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS : OBJECT_SECTION_TEXT,
                    .symbol = (module + pair + 1) % symbols, /* Forward identity references. */
                    .kind = row % 2 ? OBJECT_RELOCATION_COFF_SECTION16 : OBJECT_RELOCATION_COFF_SECREL32,
                };
                if (mode == 2 && input == 0 && local_module == 0 && row == 0)
                {
                    relocation.offset = contribution_size; /* Escapes into the next input. */
                }
                if (mode == 3 && codeview && row % 2)
                {
                    relocation.offset -= 2; /* Overlapping writes; original order matters. */
                }
                if (mode == 4 && row == 0) relocation.symbol = symbols;
                if (mode == 5 && row == 0) relocation.kind = OBJECT_RELOCATION_OTHER;
                if (mode == 6 && row == 0) relocation.addend = -1024;
                if (mode == 7 && row == 0) relocation.addend = INT64_MAX;
                if (mode == 8 && row == 0) relocation.offset = local_begin + module_size - 1;
                if (mode == 9 && input == 0 && row == 0) relocation.offset = UINT64_MAX;
                bool keep = !(mode == 12 && row % 3 == 0);
                if (keep)
                {
                    if (module_count > 1)
                    {
                        result.construction.setup_rows += 1;
                        if (codeview && (relocation.offset >= contribution_size || relocation.offset > UINT64_MAX - base))
                        {
                            result.certificate.valid = false;
                        }
                    }
                    relocation.offset += base;
                    object->relocations[object->relocation_count++] = relocation;
                }
            }
        }
        for (u32 local_module = 0; local_module < modules; local_module += 1)
        {
            if (module_count > 1)
            {
                result.spans[input * modules + local_module] = (Span){first, object->relocation_count - first};
                result.construction.metadata_writes += 2;
            }
        }
    }
    if (mode == 16 && modules > 1)
    {
        for (u32 input = 0; input < objects; input += 1)
        {
            u32 first_module = input * modules;
            ObjectDebugModule swap = object->debug_modules[first_module];
            object->debug_modules[first_module] = object->debug_modules[first_module + modules - 1];
            object->debug_modules[first_module + modules - 1] = swap;
        }
    }
    result.certificate.rows = object->relocations;
    result.certificate.count = object->relocation_count;
    result.certificate.epoch = result.epoch;
    if (mode == 10 && object->relocation_count)
    {
        ObjectRelocation* copied = allocate(object->relocation_count, sizeof(*copied), false);
        memcpy(copied, object->relocations, (size_t)object->relocation_count * sizeof(*copied));
        result.old_rows = object->relocations;
        object->relocations = copied;
    }
    if (mode == 11 && object->relocation_count)
    {
        for (u32 index = 0; index < object->relocation_count / 2; index += 1)
        {
            ObjectRelocation swap = object->relocations[index];
            object->relocations[index] = object->relocations[object->relocation_count - 1 - index];
            object->relocations[object->relocation_count - 1 - index] = swap;
        }
        result.epoch += 1;
    }
    if (mode == 13)
    {
        u32 out = 0;
        for (u32 index = 0; index < object->relocation_count; index += 1)
        {
            if (index % 3) object->relocations[out++] = object->relocations[index];
        }
        object->relocation_count = out;
        result.epoch += 1;
    }
    if (mode == 15 && module_count)
    {
        object->debug_modules[module_count - 1].symbols_size = 0;
        result.epoch += 1;
    }
    result.input_bytes = bytes;
    return result;
}

static u32 boundary_owner(Boundary const* order, u32 count, u64 offset, Work* work)
{
    u32 first = 0;
    u32 end = count;
    while (first < end)
    {
        u32 middle = first + (end - first) / 2;
        work->key_comparisons += 1;
        if (order[middle].begin <= offset) first = middle + 1;
        else end = middle;
    }
    u32 result = UINT32_MAX;
    if (first)
    {
        work->key_comparisons += 1;
        if (offset < order[first - 1].end) result = order[first - 1].module;
    }
    return result;
}

/* General baseline: sort only unordered nonempty module intervals, verify
   disjointness, count-prefix-scatter relocations in their ORIGINAL order.
   Overlapping/malformed intervals retain the original full-scan semantics. */
static Index index_build(ObjectFile const* object)
{
    Index result = {0};
    u32 modules = object->debug_module_count;
    if (modules > 1)
    {
        Boundary* storage = allocate((size_t)modules * 2, sizeof(*storage), false);
        Boundary* rows = storage;
        Boundary* temporary = storage + modules;
        u32* counts = allocate(modules, sizeof(*counts), true);
        u32* cursors = allocate(modules, sizeof(*cursors), false);
        result.starts = allocate((size_t)modules + 1, sizeof(*result.starts), false);
        result.work.auxiliary_bytes = (u64)modules * (2 * sizeof(*storage) + 3 * sizeof(u32)) + sizeof(u32);
        result.work.zero_slots = modules;
        result.valid = true;
        u32 live = 0;
        bool ordered = true;
        u64 bytes = object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.length;
        for (u32 module = 0; module < modules; module += 1)
        {
            ObjectDebugModule const* source = object->debug_modules + module;
            result.work.metadata_writes += 1;
            if (source->symbols_offset > bytes || source->symbols_size > bytes - source->symbols_offset)
            {
                result.valid = false;
            }
            else if (source->symbols_size)
            {
                if (live && rows[live - 1].begin > source->symbols_offset) ordered = false;
                rows[live++] = (Boundary){source->symbols_offset, source->symbols_offset + source->symbols_size, module, 0};
            }
        }
        for (u64 width = 1; !ordered && width < live; width *= 2)
        {
            for (u64 begin = 0; begin < live; begin += width * 2)
            {
                u64 middle = begin + width < live ? begin + width : live;
                u64 end = begin + 2 * width < live ? begin + 2 * width : live;
                u64 left = begin, right = middle;
                for (u64 out = begin; out < end; out += 1)
                {
                    bool take_left = left < middle;
                    if (take_left && right < end)
                    {
                        result.work.key_comparisons += 1;
                        take_left = rows[left].begin <= rows[right].begin;
                    }
                    temporary[out] = rows[take_left ? left++ : right++];
                    result.work.sort_moves += 1;
                }
            }
            Boundary* swap = rows;
            rows = temporary;
            temporary = swap;
        }
        for (u32 index = 1; index < live; index += 1)
        {
            result.work.key_comparisons += 1;
            if (rows[index].begin < rows[index - 1].end) result.valid = false;
        }
        if (result.valid)
        {
            for (u32 index = 0; index < object->relocation_count; index += 1)
            {
                ObjectRelocation const* relocation = object->relocations + index;
                result.work.setup_rows += 1;
                if (relocation->section == OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS)
                {
                    u32 owner = boundary_owner(rows, live, relocation->offset, &result.work);
                    if (owner != UINT32_MAX) counts[owner] += 1;
                }
            }
            u32 total = 0;
            for (u32 module = 0; module < modules; module += 1)
            {
                result.starts[module] = total;
                cursors[module] = total;
                total += counts[module];
                result.work.metadata_writes += 2;
            }
            result.starts[modules] = total;
            result.packed = allocate(total, sizeof(*result.packed), false);
            result.work.auxiliary_bytes += (u64)total * sizeof(*result.packed);
            for (u32 index = 0; index < object->relocation_count; index += 1)
            {
                ObjectRelocation const* relocation = object->relocations + index;
                result.work.setup_rows += 1;
                if (relocation->section == OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS)
                {
                    u32 owner = boundary_owner(rows, live, relocation->offset, &result.work);
                    if (owner != UINT32_MAX)
                    {
                        result.packed[cursors[owner]++] = *relocation;
                        result.work.packing_rows += 1;
                    }
                }
            }
        }
        free(counts);
        free(cursors);
        free(storage);
    }
    return result;
}

static bool certificate_current(Fixture const* fixture)
{
    return fixture->certificate.valid && fixture->certificate.rows == fixture->object.relocations &&
           fixture->certificate.count == fixture->object.relocation_count && fixture->certificate.epoch == fixture->epoch;
}

static void fixture_free(Fixture* fixture)
{
    free(fixture->object.sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.pointer);
    free(fixture->object.sections);
    free(fixture->object.symbols);
    free(fixture->object.relocations);
    free(fixture->object.debug_modules);
    free(fixture->spans);
    free(fixture->old_rows);
}

static bool run_case(char const* family, u32 objects, u32 modules, u32 keys, u32 updates, u32 noise,
                     u32 symbols, u32 mode, bool print, u64* total_digest)
{
    Fixture fixture = fixture_build(objects, modules, keys, updates, noise, symbols, mode);
    ObjectFile const* source = &fixture.object;
    Index index = index_build(source);
    Work work[3] = {{0}, index.work, fixture.construction};
    Arena arenas[3] = {{0}};
    bool success = true;
    u64 output_digest = UINT64_C(1469598103934665603);
    u64 input_digest = digest(0, source->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.pointer, (size_t)fixture.input_bytes);
    u32 output_sections[OBJECT_SECTION_COUNT] = {0, 1, 2};
    u64 output_offsets[OBJECT_SECTION_COUNT] = {64, 0, 0};
    bool certified = certificate_current(&fixture);
    for (u32 method = 0; method < 3; method += 1)
    {
        arenas[method].capacity = (size_t)(fixture.input_bytes * 3 + (u64)source->debug_module_count * 128 + 65536);
        arenas[method].bytes = allocate(arenas[method].capacity, 1, false);
    }
    for (u32 module = 0; module < source->debug_module_count; module += 1)
    {
        ByteSlice outputs[3] = {{0}};
        for (u32 method = 0; method < 3; method += 1)
        {
            ObjectFile view = *source;
            if (method == 1 && index.valid)
            {
                view.relocations = index.packed + index.starts[module];
                view.relocation_count = index.starts[module + 1] - index.starts[module];
            }
            if (method == 2 && certified)
            {
                view.relocations = source->relocations + fixture.spans[module].first;
                view.relocation_count = fixture.spans[module].count;
            }
            u64 before = resolver_visits;
            outputs[method] = link_pe_resolved_codeview(arenas + method, &view, source->debug_modules + module,
                                                      output_sections, output_offsets, OBJECT_SECTION_COUNT);
            work[method].resolver_rows += resolver_visits - before;
            work[method].output_bytes += outputs[method].length;
        }
        for (u32 method = 1; method < 3; method += 1)
        {
            if (!!outputs[0].pointer != !!outputs[method].pointer || outputs[0].length != outputs[method].length ||
                (outputs[0].pointer && memcmp(outputs[0].pointer, outputs[method].pointer, (size_t)outputs[0].length)))
            {
                fprintf(stderr, "mismatch family=%s mode=%u module=%u method=%u\n", family, mode, module, method);
                success = false;
            }
        }
        u8 valid = outputs[0].pointer != 0;
        output_digest = digest(output_digest, &valid, 1);
        if (valid) output_digest = digest(output_digest, outputs[0].pointer, (size_t)outputs[0].length);
    }
    if (input_digest != digest(0, source->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data.pointer, (size_t)fixture.input_bytes))
    {
        fprintf(stderr, "input bytes changed\n");
        success = false;
    }
    if (mode == 0)
    {
        u64 expected_full = (u64)source->debug_module_count * source->relocation_count;
        u64 expected_span = expected_full;
        if (certified)
        {
            expected_span = 0;
            for (u32 module = 0; module < source->debug_module_count; module += 1) expected_span += fixture.spans[module].count;
        }
        u64 expected_index = index.valid ? (u64)source->debug_module_count * keys * updates * 2 : expected_full;
        if (work[0].resolver_rows != expected_full || work[1].resolver_rows != expected_index || work[2].resolver_rows != expected_span)
        {
            fprintf(stderr, "predeclared operation prediction failed\n");
            success = false;
        }
    }
    if (print)
    {
        char const* names[3] = {"full", "indexed", "provenance"};
        for (u32 method = 0; method < 3; method += 1)
        {
            Work const* w = work + method;
            printf("%s,%u,%u,%u,%u,%u,%u,%u,%s,%u,%u,%u,%u,",
                   family, objects, modules, keys, updates, noise, symbols, mode, names[method],
                   source->debug_module_count, source->relocation_count, (unsigned)certified, (unsigned)index.valid);
            printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%016" PRIx64 "\n",
                   w->setup_rows, w->resolver_rows, w->key_comparisons, w->zero_slots, w->packing_rows,
                   w->sort_moves, w->metadata_writes, w->auxiliary_bytes, w->output_bytes, output_digest);
        }
    }
    *total_digest = (*total_digest ^ output_digest) * UINT64_C(1099511628211);
    for (u32 method = 0; method < 3; method += 1) free(arenas[method].bytes);
    free(index.packed);
    free(index.starts);
    fixture_free(&fixture);
    return success;
}

int main(void)
{
    bool success = true;
    u32 cases = 0;
    u64 combined = UINT64_C(1469598103934665603);
    printf("family,inputs,modules_per_input,keys,updates,noise,symbols,mode,method,modules,relocations,certificate_current,index_usable,setup_rows,resolver_rows,key_comparisons,zero_slots,packing_rows,sort_moves,metadata_writes,auxiliary_bytes,output_bytes,digest\n");
    u32 inputs[] = {1, 2, 8, 32, 128, 512, 1024};
    for (u32 index = 0; index < sizeof(inputs) / sizeof(inputs[0]); index += 1)
    {
        success &= run_case("separate_inputs", inputs[index], 1, 4, 1, 8, 17, 0, true, &combined);
        cases += 1;
    }
    u32 partitions[] = {1, 2, 8, 32, 128};
    for (u32 index = 0; index < sizeof(partitions) / sizeof(partitions[0]); index += 1)
    {
        u32 count = partitions[index];
        success &= run_case("fixed_M_R", count, 128 / count, 4, 1, 8, 17, 0, true, &combined);
        cases += 1;
    }
    u32 dimensions[] = {1, 4, 16, 64};
    for (u32 index = 0; index < sizeof(dimensions) / sizeof(dimensions[0]); index += 1)
    {
        success &= run_case("keys", 8, 1, dimensions[index], 1, 8, 17, 0, true, &combined);
        success &= run_case("updates", 8, 1, 4, dimensions[index], 8, 17, 0, true, &combined);
        success &= run_case("unrelated", 8, 1, 4, 1, dimensions[index], 17, 0, true, &combined);
        success &= run_case("symbol_domain", 8, 1, 4, 1, 8, dimensions[index], 0, true, &combined);
        cases += 4;
    }
    success &= run_case("empty", 4, 0, 1, 1, 0, 1, 0, true, &combined);
    cases += 1;
    for (u32 mode = 1; mode <= 16; mode += 1)
    {
        success &= run_case("adversarial", 3, 2, 4, 3, 7, 17, mode, true, &combined);
        cases += 1;
    }
    u32 seed = 0x14ab57u;
    for (u32 iteration = 0; iteration < 256; iteration += 1)
    {
        seed = seed * 1664525u + 1013904223u;
        u32 objects = 1 + seed % 5;
        u32 modules = 1 + (seed >> 4) % 4;
        u32 keys = 1 + (seed >> 8) % 12;
        u32 updates = 1 + (seed >> 16) % 3;
        u32 noise = (seed >> 20) % 8;
        u32 mode = iteration % 17;
        success &= run_case("enumerated", objects, modules, keys, updates, noise, 1 + (seed >> 24), mode, false, &combined);
        cases += 1;
    }
    printf("RESULT cases=%u comparisons=%u status=%s digest=%016" PRIx64 "\n", cases, cases * 2,
           success ? "pass" : "FAIL", combined);
    return success ? 0 : 1;
}
