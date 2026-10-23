// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// DWARF 5 source lines and named function ranges (#88), before and after final layout. debug_write produces five
// DWARF32 sections with zero address slots; debug_finalize changes only .debug_addr and each line sequence's
// DW_LNE_set_address operand. The producer supplies instruction-relative rows after branch sizing. Types, variables,
// inline scopes and CFI are separate interfaces; this module does not infer them from machine instructions.
//
// Rows are contiguous columns, with each function owning its next row range; offsets are nondecreasing and less than
// the function size. Files are zero-based, lines are in [0, INT32_MAX], and a sequence has one file. The line program
// explicitly selects that file, avoiding DWARF 5's initial file register of 1. Functions need not be in address order.
// Four rows at a time: form the address/line deltas, spread their seven-bit groups with vpmultishiftqb, select their
// valid bytes and compact directly into the line program. No temporary encoded rows or per-row allocations. The
// exact path independently writes each opcode and LEB128 value; tails use it too. All vector loads/stores are bounded.
//
// The caller owns every slice and keeps input and output disjoint. No padding is read or written. Empty units are
// valid. debug_write checks inputs and measures all sections before writing; on DEBUG_ERROR_CAPACITY it sets the
// required section sizes but changes no output bytes or address-offset entries. Other errors change nothing.
// line_addresses has one uint32_t per function and records the operand offsets in .debug_line. Its capacity is
// explicit. debug_finalize takes a successful, unmodified output and checks every address before patching any.
// Constants and encodings follow DWARF 5 (2017), sections 3.1, 3.3, 6.2, 7.5, 7.27 and 7.28; no upstream code copied.

#define BUSTER_DEBUG_SECTIONS 5
#define BUSTER_DEBUG_DWARF32_LIMIT 0xfffffff0u

typedef enum DebugSectionKind
{
    DEBUG_SECTION_INFO,
    DEBUG_SECTION_ABBREV,
    DEBUG_SECTION_LINE,
    DEBUG_SECTION_ADDR,
    DEBUG_SECTION_RNGLISTS,
} DebugSectionKind;

typedef enum DebugError
{
    DEBUG_OK,
    DEBUG_ERROR_INPUT,
    DEBUG_ERROR_LIMIT,
    DEBUG_ERROR_CAPACITY,
    DEBUG_ERROR_ADDRESS,
} DebugError;

typedef struct DebugFunction DebugFunction;
struct DebugFunction
{
    String8 name;
    uint32_t size;
    uint32_t first_row;
    uint32_t row_count;
    uint32_t file;
    uint32_t line;
};

typedef struct DebugUnit DebugUnit;
struct DebugUnit
{
    String8 directory;
    const String8* files;
    uint32_t file_count;
    const DebugFunction* functions;
    uint32_t function_count;
    const uint32_t* offsets;
    const uint32_t* lines;
    uint32_t row_count;
};

typedef struct DebugSection DebugSection;
struct DebugSection
{
    uint8_t* bytes;
    uint64_t capacity;
    uint64_t size;
};

typedef struct DebugOutput DebugOutput;
struct DebugOutput
{
    DebugSection sections[BUSTER_DEBUG_SECTIONS];
    uint32_t* line_addresses;
    uint32_t address_capacity;
};

typedef struct DebugWriter DebugWriter;
struct DebugWriter
{
    uint8_t* bytes;
    uint64_t size;
};

static void debug_bytes(DebugWriter* writer, const void* bytes, uint64_t count)
{
    if (writer->bytes != 0 && count != 0)
    {
        memcpy(writer->bytes + writer->size, bytes, count);
    }
    writer->size += count;
}

static void debug_integer(DebugWriter* writer, uint64_t value, uint32_t bytes)
{
    debug_bytes(writer, &value, bytes);
}

static void debug_string(DebugWriter* writer, String8 value)
{
    debug_bytes(writer, value.pointer, value.length);
    debug_integer(writer, 0, 1);
}

static void debug_uleb(DebugWriter* writer, uint64_t value)
{
    do
    {
        const uint8_t byte = (value & 0x7f) | (value > 0x7f ? 0x80 : 0);
        debug_integer(writer, byte, 1);
        value >>= 7;
    } while (value != 0);
}

static void debug_sleb(DebugWriter* writer, int64_t value)
{
    bool more = true;
    while (more)
    {
        uint8_t byte = (uint8_t)value & 0x7f;
        value >>= 7;
        more = !((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0));
        byte |= more ? 0x80 : 0;
        debug_integer(writer, byte, 1);
    }
}

static void debug_length(DebugWriter* writer, uint64_t offset, uint64_t value)
{
    const uint32_t length = (uint32_t)value;
    if (writer->bytes != 0)
    {
        memcpy(writer->bytes + offset, &length, 4);
    }
}

static void debug_row_scalar(DebugWriter* writer, uint32_t offset, int32_t line)
{
    debug_integer(writer, 2, 1); // DW_LNS_advance_pc
    debug_uleb(writer, offset);
    debug_integer(writer, 3, 1); // DW_LNS_advance_line
    debug_sleb(writer, line);
    debug_integer(writer, 1, 1); // DW_LNS_copy
}

// Four rows, their previous offsets and lines already formed by the caller. Eight qwords carry alternating unsigned
// address and signed line deltas; each pair becomes one 16-byte template, whose unused LEB groups are compressed out.
static void debug_rows_vector(DebugWriter* writer, __m128i offset, __m128i line)
{
    const __m512i pc = _mm512_cvtepu32_epi64(_mm256_zextsi128_si256(offset));
    const __m512i ln = _mm512_cvtepi32_epi64(_mm256_zextsi128_si256(line));
    const __m512i interleave = _mm512_set_epi64(11, 3, 10, 2, 9, 1, 8, 0);
    const __m512i values = _mm512_permutex2var_epi64(pc, interleave, ln);
    const __m512i sign = _mm512_srai_epi64(values, 63);
    const __m512i magnitude = _mm512_xor_si512(values, sign);
    const __m512i bits = _mm512_sub_epi64(_mm512_set1_epi64(64), _mm512_lzcnt_epi64(magnitude));
    // ceil((bits + is_signed)/7), at least one, without vector division. At most five bytes for these deltas.
    const __m512i adjusted = _mm512_mask_add_epi64(bits, 0xaa, bits, _mm512_set1_epi64(1));
    __m512i counts = _mm512_set1_epi64(1);
    for (uint32_t threshold = 7; threshold <= 28; threshold += 7)
    {
        counts = _mm512_mask_add_epi64(counts, _mm512_cmpgt_epi64_mask(adjusted, _mm512_set1_epi64(threshold)), counts, _mm512_set1_epi64(1));
    }
    const __m512i shifts = _mm512_set1_epi64(0x312a231c150e0700ull);
    __m512i groups = _mm512_and_si512(_mm512_multishift_epi64_epi8(shifts, values), _mm512_set1_epi8(0x7f));
    const __m512i positions = _mm512_set1_epi64(0x0706050403020100ull);
    const __m512i repeated = _mm512_mullo_epi64(counts, _mm512_set1_epi64(0x0101010101010101ull));
    const __mmask64 continuation = _mm512_cmplt_epu8_mask(_mm512_add_epi8(positions, _mm512_set1_epi8(1)), repeated);
    groups = _mm512_or_si512(groups, _mm512_maskz_set1_epi8(continuation, (char)0x80));
    // Within each pair of qwords: opcode, five PC bytes, opcode, five line bytes, opcode, padding.
    const __m512i order = _mm512_set_epi64(0x3f3f3f3f3c3b3a39ull, 0x383f34333231303full, 0x2f2f2f2f2c2b2a29ull, 0x282f24232221202full,
                                          0x1f1f1f1f1c1b1a19ull, 0x181f14131211101full, 0x0f0f0f0f0c0b0a09ull, 0x080f04030201000full);
    __m512i bytes = _mm512_permutexvar_epi8(order, groups);
    bytes = _mm512_mask_set1_epi8(bytes, 0x0001000100010001ull, 2);
    bytes = _mm512_mask_set1_epi8(bytes, 0x0040004000400040ull, 3);
    bytes = _mm512_mask_set1_epi8(bytes, 0x1000100010001000ull, 1);
    const __m512i valid_groups = _mm512_maskz_set1_epi8(_mm512_cmplt_epu8_mask(positions, repeated), 1);
    const __m512i valid = _mm512_permutexvar_epi8(order, valid_groups);
    const __mmask64 keep = (_mm512_test_epi8_mask(valid, valid) & 0x0fbe0fbe0fbe0fbeull) | 0x1041104110411041ull;
    const uint32_t size = (uint32_t)__builtin_popcountll(keep);
    if (writer->bytes != 0)
    {
        const __m512i packed = _mm512_maskz_compress_epi8(keep, bytes);
        _mm512_mask_storeu_epi8(writer->bytes + writer->size, (__mmask64)((1ull << size) - 1), packed);
    }
    writer->size += size;
}

static void debug_line(const DebugUnit* unit, DebugWriter* writer, uint32_t* addresses, bool exact)
{
    debug_integer(writer, 0, 4);
    debug_integer(writer, 5, 2);
    debug_integer(writer, 8, 1);
    debug_integer(writer, 0, 1);
    debug_integer(writer, 0, 4);
    const uint8_t header[] = { 1, 1, 1, 0xfb, 14, 13, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
    debug_bytes(writer, header, sizeof(header));
    // One directory format: path/string. One directory, the compilation directory.
    debug_integer(writer, 1, 1);
    debug_uleb(writer, 1);
    debug_uleb(writer, 8);
    debug_uleb(writer, 1);
    debug_string(writer, unit->directory);
    // File formats: path/string and directory_index/udata. The file table is zero-based.
    const uint8_t file_formats[] = { 2, 1, 8, 2, 15 };
    debug_bytes(writer, file_formats, sizeof(file_formats));
    debug_uleb(writer, unit->file_count);
    for (uint32_t file = 0; file < unit->file_count; file += 1)
    {
        debug_string(writer, unit->files[file]);
        debug_uleb(writer, 0);
    }
    debug_length(writer, 8, writer->size - 12);
    for (uint32_t function = 0; function < unit->function_count; function += 1)
    {
        const DebugFunction* entry = &unit->functions[function];
        debug_integer(writer, 0, 1);
        debug_uleb(writer, 9);
        debug_integer(writer, 2, 1); // DW_LNE_set_address
        if (addresses != 0)
        {
            addresses[function] = (uint32_t)writer->size;
        }
        debug_integer(writer, 0, 8);
        debug_integer(writer, 4, 1); // DW_LNS_set_file
        debug_uleb(writer, entry->file);
        uint32_t row = 0;
        uint32_t offset = 0;
        int32_t line = 1;
        for (; !exact && entry->row_count - row >= 4; row += 4)
        {
            const uint32_t first = entry->first_row + row;
            const __m128i offsets = _mm_loadu_si128((const __m128i*)(unit->offsets + first));
            const __m128i lines = _mm_loadu_si128((const __m128i*)(unit->lines + first));
            const __m128i previous_offsets = _mm_insert_epi32(_mm_slli_si128(offsets, 4), offset, 0);
            const __m128i previous_lines = _mm_insert_epi32(_mm_slli_si128(lines, 4), line, 0);
            debug_rows_vector(writer, _mm_sub_epi32(offsets, previous_offsets), _mm_sub_epi32(lines, previous_lines));
            offset = unit->offsets[first + 3];
            line = (int32_t)unit->lines[first + 3];
        }
        for (; row < entry->row_count; row += 1)
        {
            const uint32_t index = entry->first_row + row;
            debug_row_scalar(writer, unit->offsets[index] - offset, (int32_t)unit->lines[index] - line);
            offset = unit->offsets[index];
            line = (int32_t)unit->lines[index];
        }
        debug_integer(writer, 2, 1);
        debug_uleb(writer, entry->size - offset);
        const uint8_t end[] = { 0, 1, 1 }; // DW_LNE_end_sequence
        debug_bytes(writer, end, sizeof(end));
    }
    debug_length(writer, 0, writer->size - 4);
}

static void debug_sections(const DebugUnit* unit, DebugWriter* writers, uint32_t* addresses, bool exact)
{
    // compile_unit: name/string, comp_dir/string, producer/string, language/data2, stmt_list/sec_offset,
    // addr_base/sec_offset, ranges/sec_offset; subprogram: name/string, low_pc/addrx, high_pc/data4,
    // decl_file/udata, decl_line/udata. All abbreviations are position-independent.
    const uint8_t abbreviations[] = {
        1, 0x11, 1, 3, 8, 0x1b, 8, 0x25, 8, 0x13, 5, 0x10, 0x17, 0x73, 0x17, 0x55, 0x17, 0, 0,
        2, 0x2e, 0, 3, 8, 0x11, 0x1b, 0x12, 6, 0x3a, 15, 0x3b, 15, 0, 0, 0,
    };
    debug_bytes(&writers[DEBUG_SECTION_ABBREV], abbreviations, sizeof(abbreviations));
    DebugWriter* info = &writers[DEBUG_SECTION_INFO];
    debug_integer(info, 0, 4);
    debug_integer(info, 5, 2);
    debug_integer(info, 1, 1); // DW_UT_compile
    debug_integer(info, 8, 1);
    debug_integer(info, 0, 4);
    debug_uleb(info, 1);
    debug_string(info, unit->file_count != 0 ? unit->files[0] : BUSTER_S8(""));
    debug_string(info, unit->directory);
    debug_string(info, BUSTER_S8("buster"));
    debug_integer(info, 0x1d, 2); // DW_LANG_C11
    debug_integer(info, 0, 4);
    debug_integer(info, 8, 4);
    debug_integer(info, 12, 4);
    DebugWriter* addr = &writers[DEBUG_SECTION_ADDR];
    debug_integer(addr, 4 + 8ull * unit->function_count, 4);
    debug_integer(addr, 5, 2);
    debug_integer(addr, 8, 1);
    debug_integer(addr, 0, 1);
    DebugWriter* ranges = &writers[DEBUG_SECTION_RNGLISTS];
    debug_integer(ranges, 0, 4);
    debug_integer(ranges, 5, 2);
    debug_integer(ranges, 8, 1);
    debug_integer(ranges, 0, 1);
    debug_integer(ranges, 0, 4); // offset_entry_count; DW_AT_ranges is a section offset.
    for (uint32_t function = 0; function < unit->function_count; function += 1)
    {
        const DebugFunction* entry = &unit->functions[function];
        debug_uleb(info, 2);
        debug_string(info, entry->name);
        debug_uleb(info, function);
        debug_integer(info, entry->size, 4);
        debug_uleb(info, entry->file);
        debug_uleb(info, entry->line);
        debug_integer(addr, 0, 8);
        debug_integer(ranges, 3, 1); // DW_RLE_startx_length
        debug_uleb(ranges, function);
        debug_uleb(ranges, entry->size);
    }
    debug_integer(info, 0, 1);
    debug_integer(ranges, 0, 1);
    debug_length(info, 0, info->size - 4);
    debug_length(ranges, 0, ranges->size - 4);
    debug_line(unit, &writers[DEBUG_SECTION_LINE], addresses, exact);
}

static bool debug_valid_string(String8 string)
{
    return string.length < BUSTER_DEBUG_DWARF32_LIMIT && (string.length == 0 || (string.pointer != 0 && memchr(string.pointer, 0, string.length) == 0));
}

static DebugError debug_validate(const DebugUnit* unit)
{
    bool valid = debug_valid_string(unit->directory) && (unit->file_count == 0 || unit->files != 0) &&
                 (unit->function_count == 0 || unit->functions != 0) && (unit->row_count == 0 || (unit->offsets != 0 && unit->lines != 0));
    uint64_t rows = 0;
    for (uint32_t file = 0; valid && file < unit->file_count; file += 1)
    {
        valid = debug_valid_string(unit->files[file]);
    }
    for (uint32_t function = 0; valid && function < unit->function_count; function += 1)
    {
        const DebugFunction* entry = &unit->functions[function];
        valid = debug_valid_string(entry->name) && entry->size != 0 && entry->file < unit->file_count &&
                entry->line <= INT32_MAX && entry->first_row == rows && entry->row_count <= unit->row_count - rows;
        uint32_t previous = 0;
        for (uint32_t row = 0; valid && row < entry->row_count; row += 1)
        {
            const uint32_t index = entry->first_row + row;
            valid = unit->offsets[index] >= previous && unit->offsets[index] < entry->size && unit->lines[index] <= INT32_MAX;
            previous = unit->offsets[index];
        }
        rows += entry->row_count;
    }
    valid = valid && rows == unit->row_count;
    return valid ? DEBUG_OK : DEBUG_ERROR_INPUT;
}

static DebugError debug_write(const DebugUnit* unit, DebugOutput* output, bool exact)
{
    // Even the smallest encodings exceed DWARF32 at these counts. Check before traversing any supplied slices;
    // these bounds also keep the sizing pass's sums below uint64, including the largest permitted strings.
    DebugError result = unit->row_count > BUSTER_DEBUG_DWARF32_LIMIT / 5 || unit->function_count > BUSTER_DEBUG_DWARF32_LIMIT / 8 ||
                        unit->file_count > BUSTER_DEBUG_DWARF32_LIMIT / 2 ? DEBUG_ERROR_LIMIT : debug_validate(unit);
    DebugWriter measured[BUSTER_DEBUG_SECTIONS] = { 0 };
    if (result == DEBUG_OK)
    {
        debug_sections(unit, measured, 0, exact);
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            if (measured[section].size >= BUSTER_DEBUG_DWARF32_LIMIT)
            {
                result = DEBUG_ERROR_LIMIT;
            }
        }
    }
    if (result == DEBUG_OK)
    {
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            output->sections[section].size = measured[section].size;
            if (measured[section].size > output->sections[section].capacity || output->sections[section].bytes == 0)
            {
                result = DEBUG_ERROR_CAPACITY;
            }
        }
        if (unit->function_count > output->address_capacity || (unit->function_count != 0 && output->line_addresses == 0))
        {
            result = DEBUG_ERROR_CAPACITY;
        }
    }
    if (result == DEBUG_OK)
    {
        DebugWriter writers[BUSTER_DEBUG_SECTIONS] = { 0 };
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            writers[section].bytes = output->sections[section].bytes;
        }
        debug_sections(unit, writers, output->line_addresses, exact);
    }
    return result;
}

static DebugError debug_finalize(const DebugUnit* unit, DebugOutput* output, const uint64_t* addresses)
{
    DebugError result = DEBUG_OK;
    for (uint32_t function = 0; result == DEBUG_OK && function < unit->function_count; function += 1)
    {
        if (addresses[function] > UINT64_MAX - unit->functions[function].size)
        {
            result = DEBUG_ERROR_ADDRESS;
        }
    }
    if (result == DEBUG_OK)
    {
        for (uint32_t function = 0; function < unit->function_count; function += 1)
        {
            memcpy(output->sections[DEBUG_SECTION_ADDR].bytes + 8 + 8ull * function, &addresses[function], 8);
            memcpy(output->sections[DEBUG_SECTION_LINE].bytes + output->line_addresses[function], &addresses[function], 8);
        }
    }
    return result;
}
