// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Stable exact string interning for foreign SHF_MERGE | SHF_STRINGS sections with one-, two- or four-byte code units. This producer
// builds a pool and a per-entry offset map without changing its input. The consumer must translate section-symbol
// references at S+A and named symbols at S (retaining their addend), before publishing the changed Link. It supplies
// group ids for compatible section names/types/flags; strings in different groups never share storage. Empty input
// sections are valid. Relocatable string contents and other code-unit widths are outside this interface.
//
// Terminator scans use bounded masked 64-byte loads; hashing uses the existing eight-string kernel, both with exact
// scalar twins. Hash-table probes and offset lookups are irregular and scalar. A duplicate keeps the first suitable
// entry's bytes, provided its output offset meets the requested effective alignment. A string's effective alignment
// is its section's alignment reduced by its input offset. The output pool is zeroed before its unique strings are
// copied, making padding deterministic. No suffix merging is attempted.
//
// Borrowed inputs remain live through build. The caller owns result arrays and pool in its arena until reset. A
// failure leaves both the result and arena length unchanged. Offsets and counts are 32-bit, with checked totals;
// neither input buffers nor output columns require vector padding. Include after foreign.c and link.c.

typedef enum ForeignStringsError
{
    FOREIGN_STRINGS_ERROR_NONE,
    FOREIGN_STRINGS_ERROR_INPUT,
    FOREIGN_STRINGS_ERROR_MEMORY,
    FOREIGN_STRINGS_ERROR_CAPACITY,
    FOREIGN_STRINGS_ERROR_UNSUPPORTED,
} ForeignStringsError;

typedef struct ForeignStringInput ForeignStringInput;
struct ForeignStringInput
{
    const uint8_t* bytes;
    uint32_t size;
    uint32_t group;
    uint8_t alignment_log2;
    uint8_t unit_log2;
};

typedef struct ForeignStrings ForeignStrings;
struct ForeignStrings
{
    uint8_t* bytes;
    uint32_t size;
    uint32_t count;
    uint32_t input_count;
    // Entry range per input section, including the final sentinel; entry offsets are ordered within each range.
    uint32_t* section_begin;
    uint32_t* input_offset;
    uint32_t* output_offset;
    uint32_t* lengths;
};

static __attribute__((unused)) ForeignStringsError foreign_strings_build(const ForeignStringInput* inputs, uint32_t input_count,
                                                                        ForeignPath path, Arena* arena, ForeignStrings* output);
static __attribute__((unused)) bool foreign_strings_offset(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* mapped);

// Zero-byte positions in this bounded block. The mask excludes inactive lanes even though a masked load zeros them.
static uint64_t foreign_strings_zeros(const uint8_t* bytes, uint32_t count, uint8_t unit_log2, ForeignPath path)
{
    const uint32_t units = count >> unit_log2;
    const uint64_t valid = units >= 64 ? ~0ull : (1ull << units) - 1;
    uint64_t zeros = 0;
    if (path == FOREIGN_PATH_VECTOR && unit_log2 == 0)
    {
        zeros = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, bytes), _mm512_setzero_si512());
    }
    else if (path == FOREIGN_PATH_VECTOR && unit_log2 == 1)
    {
        zeros = _mm512_mask_cmpeq_epi16_mask((__mmask32)valid, _mm512_maskz_loadu_epi16((__mmask32)valid, bytes), _mm512_setzero_si512());
    }
    else if (path == FOREIGN_PATH_VECTOR)
    {
        zeros = _mm512_mask_cmpeq_epi32_mask((__mmask16)valid, _mm512_maskz_loadu_epi32((__mmask16)valid, bytes), _mm512_setzero_si512());
    }
    else
    {
        #pragma clang loop vectorize(disable) interleave(disable)
        for (uint32_t i = 0; i < units; i += 1)
        {
            uint32_t value = 0;
            memcpy(&value, bytes + (i << unit_log2), 1u << unit_log2);
            zeros |= value == 0 ? 1ull << i : 0;
        }
    }
    return zeros;
}

static ForeignStringsError foreign_strings_count(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, uint32_t* count)
{
    ForeignStringsError error = FOREIGN_STRINGS_ERROR_NONE;
    uint64_t total = 0;
    for (uint32_t section = 0; error == FOREIGN_STRINGS_ERROR_NONE && section < input_count; section += 1)
    {
        const ForeignStringInput* input = &inputs[section];
        const uint32_t unit = input->unit_log2 <= 2 ? 1u << input->unit_log2 : 0;
        uint32_t last = 0;
        const bool bounded = unit != 0 && input->size % unit == 0 && (input->size == 0 || input->bytes != 0);
        if (bounded && input->size != 0)
        {
            memcpy(&last, input->bytes + input->size - unit, unit);
        }
        if (input->alignment_log2 >= 32 || !bounded || last != 0)
        {
            error = FOREIGN_STRINGS_ERROR_INPUT;
        }
        for (uint64_t at = 0; error == FOREIGN_STRINGS_ERROR_NONE && at < input->size; at += 64)
        {
            const uint32_t left = input->size - (uint32_t)at;
            total += (uint64_t)__builtin_popcountll(foreign_strings_zeros(input->bytes + at, left < 64 ? left : 64, input->unit_log2, path));
            if (total >= UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_CAPACITY;
            }
        }
    }
    *count = error == FOREIGN_STRINGS_ERROR_NONE ? (uint32_t)total : 0;
    return error;
}

// Fill the byte-pointer/length hash input columns and the consumer's ordered offset map in the same scan.
static void foreign_strings_entries(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, ForeignStrings* strings,
                                    const uint8_t** pointers, uint32_t* sections)
{
    uint32_t next = 0;
    for (uint32_t section = 0; section < input_count; section += 1)
    {
        const ForeignStringInput* input = &inputs[section];
        uint32_t begin = 0;
        strings->section_begin[section] = next;
        for (uint64_t at = 0; at < input->size; at += 64)
        {
            const uint32_t left = input->size - (uint32_t)at;
            for (uint64_t zeros = foreign_strings_zeros(input->bytes + at, left < 64 ? left : 64, input->unit_log2, path); zeros != 0; zeros &= zeros - 1)
            {
                const uint32_t end = (uint32_t)at + (((uint32_t)__builtin_ctzll(zeros) + 1) << input->unit_log2);
                pointers[next] = input->bytes + begin;
                sections[next] = section;
                strings->input_offset[next] = begin;
                strings->lengths[next] = end - begin;
                begin = end;
                next += 1;
            }
        }
    }
    strings->section_begin[input_count] = next;
}

static ForeignStringsError foreign_strings_intern(const ForeignStringInput* inputs, ForeignStrings* strings, const uint8_t* const* pointers,
                                                  const uint32_t* sections, const uint64_t* hashes, uint32_t* table, uint64_t slots)
{
    ForeignStringsError error = FOREIGN_STRINGS_ERROR_NONE;
    uint64_t size = 0;
    for (uint32_t entry = 0; error == FOREIGN_STRINGS_ERROR_NONE && entry < strings->count; entry += 1)
    {
        const ForeignStringInput* input = &inputs[sections[entry]];
        const uint32_t offset = strings->input_offset[entry];
        const uint32_t trailing = offset != 0 ? (uint32_t)__builtin_ctz(offset) : 32;
        const uint32_t alignment = 1u << (trailing < input->alignment_log2 ? trailing : input->alignment_log2);
        uint64_t slot = (hashes[entry] ^ ((uint64_t)input->group * 0x9e3779b97f4a7c15ull)) & (slots - 1);
        bool same = false;
        while (table[slot] != 0 && !same)
        {
            const uint32_t prior = table[slot] - 1;
            same = hashes[prior] == hashes[entry] && inputs[sections[prior]].group == input->group && inputs[sections[prior]].unit_log2 == input->unit_log2 &&
                   strings->lengths[prior] == strings->lengths[entry] && (strings->output_offset[prior] & (alignment - 1)) == 0 &&
                   memcmp(pointers[prior], pointers[entry], strings->lengths[entry]) == 0;
            if (!same)
            {
                slot = (slot + 1) & (slots - 1);
            }
        }
        if (same)
        {
            strings->output_offset[entry] = strings->output_offset[table[slot] - 1];
        }
        else
        {
            const uint64_t aligned = (size + alignment - 1) & ~(uint64_t)(alignment - 1);
            const uint64_t end = aligned + strings->lengths[entry];
            if (end > UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_CAPACITY;
            }
            else
            {
                strings->output_offset[entry] = (uint32_t)aligned;
                size = end;
                table[slot] = entry + 1;
            }
        }
    }
    strings->size = error == FOREIGN_STRINGS_ERROR_NONE ? (uint32_t)size : 0;
    return error;
}

static ForeignStringsError foreign_strings_build(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, Arena* arena, ForeignStrings* output)
{
    ForeignStringsError error = arena == 0 || output == 0 || (input_count != 0 && inputs == 0) ||
                                (path != FOREIGN_PATH_VECTOR && path != FOREIGN_PATH_EXACT) ? FOREIGN_STRINGS_ERROR_INPUT : FOREIGN_STRINGS_ERROR_NONE;
    const uint64_t start = arena != 0 ? arena->length : 0;
    ForeignStrings strings = { .input_count = input_count };
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        error = foreign_strings_count(inputs, input_count, path, &strings.count);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        strings.section_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input_count + 1);
        strings.input_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        strings.output_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        strings.lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        error = strings.section_begin == 0 || strings.input_offset == 0 || strings.output_offset == 0 || strings.lengths == 0 ?
                FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    Scratch scratch = scratch_begin(arena);
    const uint8_t** pointers = 0;
    uint32_t* sections = 0;
    uint64_t* hashes = 0;
    uint32_t* table = 0;
    uint64_t slots = 1;
    while (slots < 2 * (uint64_t)strings.count)
    {
        slots *= 2;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        pointers = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, const uint8_t*, (uint64_t)strings.count + 1);
        sections = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)strings.count + 1);
        hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)strings.count + 1);
        table = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, slots);
        error = pointers == 0 || sections == 0 || hashes == 0 || table == 0 ? FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        foreign_strings_entries(inputs, input_count, path, &strings, pointers, sections);
        if (path == FOREIGN_PATH_VECTOR)
        {
            link_hash_names(pointers, strings.lengths, strings.count, hashes);
        }
        else
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t entry = 0; entry < strings.count; entry += 1)
            {
                hashes[entry] = link_hash(pointers[entry], strings.lengths[entry]);
            }
        }
        memset(table, 0, slots * sizeof(uint32_t));
        error = foreign_strings_intern(inputs, &strings, pointers, sections, hashes, table, slots);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        strings.bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)strings.size + 1);
        error = strings.bytes == 0 ? FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        memset(strings.bytes, 0, strings.size);
        // Only table residents are unique. Copies are disjoint, including when equal strings need different alignment.
        for (uint64_t slot = 0; slot < slots; slot += 1)
        {
            if (table[slot] != 0)
            {
                const uint32_t entry = table[slot] - 1;
                memcpy(strings.bytes + strings.output_offset[entry], pointers[entry], strings.lengths[entry]);
            }
        }
        *output = strings;
    }
    scratch_end(scratch);
    if (error != FOREIGN_STRINGS_ERROR_NONE && arena != 0)
    {
        arena_pop_to(arena, start);
    }
    return error;
}

static bool foreign_strings_entry(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* entry)
{
    bool found = strings != 0 && entry != 0 && input < strings->input_count && offset <= UINT32_MAX;
    uint32_t first = found ? strings->section_begin[input] : 0;
    uint32_t last = found ? strings->section_begin[input + 1] : 0;
    const uint32_t begin = first;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        if (strings->input_offset[middle] <= offset)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    found = found && first > begin;
    if (found)
    {
        const uint32_t at = first - 1;
        const uint64_t within = offset - strings->input_offset[at];
        found = within < strings->lengths[at];
        if (found)
        {
            *entry = at;
        }
    }
    return found;
}

static bool foreign_strings_offset(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* mapped)
{
    uint32_t entry = 0;
    const bool found = mapped != 0 && foreign_strings_entry(strings, input, offset, &entry);
    if (found)
    {
        *mapped = strings->output_offset[entry] + (uint32_t)(offset - strings->input_offset[entry]);
    }
    return found;
}
