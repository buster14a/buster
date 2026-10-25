// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-debug: independent line-machine decoding, scalar/vector bytes, guarded tails, input and capacity failures,
// late-address patching, units written after others. --cross-check additionally asks readelf, gdb and perf to consume
// an original fixed-layout three-function ELF with these five debug sections. GNU as supplies that fixture's CFI, not
// this module: this checks debug consumption and integration with an unwinder, not a native code-generator unwind
// producer.
// benchmark-debug times validation, sizing and writing together, plus final address patching, into reused buffers.

#define BUSTER_DEBUG_TEST_BYTES (1u << 20)

static bool debug_test_native_write(const char* object_path, const char* executable_path, const DebugUnit* unit, const DebugOutput* output);

static uint64_t debug_test_leb(const uint8_t* bytes, uint64_t size, uint64_t* position, bool sign, bool* valid)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    uint8_t byte = 0x80;
    while (*valid && (byte & 0x80) != 0)
    {
        if (*position >= size || shift >= 64)
        {
            *valid = false;
        }
        else
        {
            byte = bytes[*position];
            *position += 1;
            value |= (uint64_t)(byte & 0x7f) << shift;
            shift += 7;
        }
    }
    if (sign && (byte & 0x40) != 0 && shift < 64)
    {
        value |= UINT64_MAX << shift;
    }
    return value;
}

static bool debug_test_rows(void)
{
    const uint32_t offsets[] = { 0, 1, 63, 64, 127, 128, 16383, 16384, 2097151, 2097152, 268435455, 268435456, UINT32_MAX };
    const int32_t lines[] = { 0, 1, -1, 63, 64, -64, -65, 8191, 8192, -8192, -8193, INT32_MIN, INT32_MAX };
    bool valid = true;
    uint64_t checks = 0;
    for (uint32_t pc = 0; pc < BUSTER_ARRAY_LENGTH(offsets); pc += 1)
    {
        for (uint32_t line = 0; line < BUSTER_ARRAY_LENGTH(lines); line += 1)
        {
            uint32_t delta[4];
            int32_t advance[4];
            for (uint32_t row = 0; row < 4; row += 1)
            {
                delta[row] = offsets[(pc + row) % BUSTER_ARRAY_LENGTH(offsets)];
                advance[row] = lines[(line + row) % BUSTER_ARRAY_LENGTH(lines)];
            }
            uint8_t scalar[64];
            uint8_t vector[64];
            memset(scalar, 0xa5, sizeof(scalar));
            memset(vector, 0xa5, sizeof(vector));
            DebugWriter a = { .bytes = scalar };
            DebugWriter b = { .bytes = vector };
            for (uint32_t row = 0; row < 4; row += 1)
            {
                debug_row_scalar(&a, delta[row], advance[row], false);
            }
            debug_rows_vector(&b, _mm_loadu_si128((const __m128i*)delta), _mm_loadu_si128((const __m128i*)advance));
            valid &= a.size == b.size && memcmp(scalar, vector, sizeof(scalar)) == 0;
            uint64_t position = 0;
            for (uint32_t row = 0; valid && row < 4; row += 1)
            {
                valid &= vector[position++] == 2;
                valid &= debug_test_leb(vector, b.size, &position, false, &valid) == delta[row];
                valid &= vector[position++] == 3;
                valid &= (int64_t)debug_test_leb(vector, b.size, &position, true, &valid) == advance[row];
                valid &= vector[position++] == 1;
            }
            valid &= position == b.size;
            checks += 1;
        }
    }
    printf("debug LEB rows: %llu boundary batches, %s\n", (unsigned long long)checks, valid ? "pass" : "FAIL");
    return valid;
}

static bool debug_test_random_rows(void)
{
    bool valid = true;
    uint64_t state = 88;
    for (uint32_t batch = 0; valid && batch < 4096; batch += 1)
    {
        uint32_t delta[4];
        int32_t advance[4];
        uint8_t scalar[64] = { 0 };
        uint8_t vector[64] = { 0 };
        DebugWriter a = { .bytes = scalar };
        DebugWriter b = { .bytes = vector };
        for (uint32_t row = 0; row < 4; row += 1)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            delta[row] = (uint32_t)(state >> 32);
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            advance[row] = (int32_t)(state >> 32);
            debug_row_scalar(&a, delta[row], advance[row], false);
        }
        debug_rows_vector(&b, _mm_loadu_si128((const __m128i*)delta), _mm_loadu_si128((const __m128i*)advance));
        valid &= a.size == b.size && memcmp(scalar, vector, sizeof(scalar)) == 0;
        uint64_t position = 0;
        for (uint32_t row = 0; valid && row < 4; row += 1)
        {
            valid &= vector[position++] == 2;
            valid &= debug_test_leb(vector, b.size, &position, false, &valid) == delta[row];
            valid &= vector[position++] == 3;
            valid &= (int64_t)debug_test_leb(vector, b.size, &position, true, &valid) == advance[row];
            valid &= vector[position++] == 1;
        }
        valid &= position == b.size;
    }
    printf("debug LEB rows: 4096 deterministic random batches, %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static DebugOutput debug_test_output(uint8_t* bytes, uint32_t* addresses, uint32_t capacity)
{
    DebugOutput result = {
        .line_addresses = addresses,
        .address_capacity = capacity,
    };
    for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        result.sections[section].bytes = bytes != 0 ? bytes + section * BUSTER_DEBUG_TEST_BYTES : 0;
        result.sections[section].capacity = BUSTER_DEBUG_TEST_BYTES;
    }
    return result;
}

// Walk the line machine independently; the header is skipped by its encoded length, not by emitter constants.
static bool debug_test_decode(const DebugUnit* unit, const DebugOutput* output, const uint64_t* addresses)
{
    const DebugSection* section = &output->sections[DEBUG_SECTION_LINE];
    uint32_t header = 0;
    memcpy(&header, section->bytes + 8, 4);
    uint64_t position = 12ull + header;
    uint64_t address = 0;
    int64_t line = 1;
    uint64_t file = 1;
    uint32_t function = 0;
    uint32_t row = 0;
    bool prologue_end = false;
    bool valid = position <= section->size;
    while (valid && position < section->size)
    {
        const uint8_t opcode = section->bytes[position++];
        if (opcode == 0)
        {
            const uint64_t size = debug_test_leb(section->bytes, section->size, &position, false, &valid);
            valid &= size != 0 && size <= section->size - position;
            if (valid)
            {
                const uint8_t extended = section->bytes[position++];
                if (extended == 2 && size == 9)
                {
                    memcpy(&address, section->bytes + position, 8);
                    position += 8;
                }
                else if (extended == 1 && size == 1 && function < unit->function_count)
                {
                    valid &= address == addresses[function] + unit->functions[function].size && row == unit->functions[function].row_count;
                    function += 1;
                    row = 0;
                    line = 1;
                    file = 1;
                    address = 0;
                }
                else
                {
                    valid = false;
                }
            }
        }
        else if (opcode == 2)
        {
            address += debug_test_leb(section->bytes, section->size, &position, false, &valid);
        }
        else if (opcode == 3)
        {
            line += (int64_t)debug_test_leb(section->bytes, section->size, &position, true, &valid);
        }
        else if (opcode == 4)
        {
            file = debug_test_leb(section->bytes, section->size, &position, false, &valid);
        }
        else if (opcode == 10)
        {
            prologue_end = true;
        }
        else if (opcode == 1 && function < unit->function_count && row < unit->functions[function].row_count)
        {
            // The row, with DW_LNS_set_prologue_end before it exactly when it is the function's body's (DW_LNS_copy then
            // clears the flag).
            const DebugFunction* entry = &unit->functions[function];
            const uint32_t index = entry->first_row + row;
            valid &= address == addresses[function] + unit->offsets[index] && line == unit->lines[index] && file == entry->file &&
                     prologue_end == (row + 1 == entry->prologue_end);
            prologue_end = false;
            row += 1;
        }
        else
        {
            valid = false;
        }
    }
    return valid && position == section->size && function == unit->function_count;
}

static bool debug_test_units(void)
{
    bool valid = true;
    uint8_t* a_bytes = malloc(BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
    uint8_t* b_bytes = malloc(BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
    uint32_t a_addresses[3];
    uint32_t b_addresses[3];
    uint32_t offsets[129];
    uint32_t lines[129];
    const uint64_t addresses[] = { 0x401000, 0x204000, 0x100000000ull };
    String8 files[] = { BUSTER_S8("start.c"), BUSTER_S8("leaf.c") };
    DebugFunction functions[3] = {
        {
            .name = BUSTER_S8("one"),
            .size = UINT32_MAX,
            .file = 0,
            .line = 1,
        },
        {
            .name = BUSTER_S8("two"),
            .size = 1,
            .file = 1,
            .line = 2,
        },
        {
            .name = BUSTER_S8("three"),
            .size = 1,
            .file = 0,
            .line = 3,
        },
    };
    DebugUnit unit = {
        .directory = BUSTER_S8("."),
        .files = files,
        .file_count = 2,
        .functions = functions,
        .function_count = 3,
        .offsets = offsets,
        .lines = lines,
    };
    valid &= a_bytes != 0 && b_bytes != 0;
    for (uint32_t count = 0; valid && count <= 129; count += 1)
    {
        for (uint32_t row = 0; row < count; row += 1)
        {
            offsets[row] = row * 30000000u;
            lines[row] = row % 3 == 0 ? INT32_MAX : row % 3 == 1 ? 0 : row * 10000000u;
        }
        unit.row_count = count;
        functions[0].row_count = count;
        // The body's row: none, the first, the second (as the C driver's functions with a prologue have it), a row past a
        // vector group and the last.
        const uint32_t bodies[] = { 0, 1, 2, 6, count };
        functions[0].prologue_end = bodies[count % BUSTER_ARRAY_LENGTH(bodies)] <= count ? bodies[count % BUSTER_ARRAY_LENGTH(bodies)] : count;
        functions[1].first_row = count;
        functions[2].first_row = count;
        DebugOutput a = debug_test_output(a_bytes, a_addresses, 3);
        DebugOutput b = debug_test_output(b_bytes, b_addresses, 3);
        valid &= debug_write(&unit, &a, true) == DEBUG_OK && debug_write(&unit, &b, false) == DEBUG_OK;
        for (uint32_t section = 0; valid && section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            valid &= a.sections[section].size == b.sections[section].size && memcmp(a.sections[section].bytes, b.sections[section].bytes, a.sections[section].size) == 0;
            const uint64_t capacity = b.sections[section].capacity;
            b.sections[section].capacity = b.sections[section].size - 1;
            memset(b_bytes, 0xa5, BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
            b_addresses[0] = UINT32_MAX;
            valid &= debug_write(&unit, &b, false) == DEBUG_ERROR_CAPACITY && b_addresses[0] == UINT32_MAX;
            for (uint32_t other = 0; other < BUSTER_DEBUG_SECTIONS; other += 1)
            {
                valid &= b.sections[other].bytes[0] == 0xa5 && b.sections[other].bytes[b.sections[other].size - 1] == 0xa5;
            }
            b.sections[section].capacity = capacity;
            valid &= debug_write(&unit, &b, false) == DEBUG_OK;
        }
        valid &= debug_finalize(&unit, &a, addresses) == DEBUG_OK && debug_test_decode(&unit, &a, addresses);
        const uint64_t invalid[] = { 0x401000, 0x204000, UINT64_MAX };
        valid &= debug_finalize(&unit, &b, invalid) == DEBUG_ERROR_ADDRESS;
        uint64_t first = 1;
        memcpy(&first, b.sections[DEBUG_SECTION_ADDR].bytes + 8, 8);
        valid &= first == 0;
        valid &= debug_finalize(&unit, &b, addresses) == DEBUG_OK;
        for (uint32_t section = 0; valid && section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            valid &= memcmp(a.sections[section].bytes, b.sections[section].bytes, a.sections[section].size) == 0;
        }
        const uint64_t moved[] = { 0x701000, 0x804000, 0x200000000ull };
        valid &= debug_finalize(&unit, &a, moved) == DEBUG_OK && debug_test_decode(&unit, &a, moved);
        valid &= memcmp(a.sections[DEBUG_SECTION_INFO].bytes, b.sections[DEBUG_SECTION_INFO].bytes, a.sections[DEBUG_SECTION_INFO].size) == 0;
    }
    DebugOutput output = debug_test_output(b_bytes, b_addresses, 3);
    if (valid)
    {
        unit.row_count = 1;
        functions[0].row_count = 1;
        functions[0].prologue_end = 0;
        functions[1].first_row = 1;
        functions[2].first_row = 1;
        offsets[0] = UINT32_MAX;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        offsets[0] = 0;
        lines[0] = UINT32_MAX;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        lines[0] = 1;
        functions[1].first_row = 0;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        functions[1].first_row = 1;
        functions[1].file = 2;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        functions[1].file = 1;
        // A body's row past the function's rows.
        functions[0].prologue_end = 2;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        functions[0].prologue_end = 1;
        unit.directory = (String8){
            .pointer = "a\0b",
            .length = 3,
        };
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_INPUT;
        DebugUnit empty = { 0 };
        valid &= debug_write(&empty, &output, false) == DEBUG_OK && debug_finalize(&empty, &output, 0) == DEBUG_OK && debug_test_decode(&empty, &output, 0);
        DebugUnit oversized = { .row_count = UINT32_MAX };
        valid &= debug_write(&oversized, &output, false) == DEBUG_ERROR_LIMIT;
        unit.directory = BUSTER_S8(".");
        output.address_capacity = 2;
        b_addresses[0] = UINT32_MAX;
        valid &= debug_write(&unit, &output, false) == DEBUG_ERROR_CAPACITY && b_addresses[0] == UINT32_MAX;
    }
    free(a_bytes);
    free(b_bytes);
    printf("debug units: every tail through 129, line decoding, bodies' rows, files, empty sequences, capacity, invalid input and late addresses: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

// A unit written after others (DebugOutput.bases) must be the unit written alone but for the four offsets its header
// and DIE give: its abbreviations', line program's, address table's (past its 8-byte header) and range lists' (past
// their 12), each its section's base; and a base that would carry the contribution past DWARF32 is refused, changing
// nothing.
static bool debug_test_bases(void)
{
    bool valid = true;
    uint8_t* alone_bytes = malloc(BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
    uint8_t* based_bytes = malloc(BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
    uint32_t alone_addresses[2];
    uint32_t based_addresses[2];
    const uint32_t offsets[] = { 0, 4, 9, 0, 3 };
    const uint32_t lines[] = { 7, 8, 9, 20, 21 };
    String8 files[] = { BUSTER_S8("based.c"), BUSTER_S8("other.c") };
    const DebugFunction functions[2] = {
        {
            .name = BUSTER_S8("first"),
            .size = 16,
            .first_row = 0,
            .row_count = 3,
            .file = 0,
            .line = 7,
        },
        {
            .name = BUSTER_S8("second"),
            .size = 8,
            .first_row = 3,
            .row_count = 2,
            .file = 1,
            .line = 20,
        },
    };
    const DebugUnit unit = {
        .directory = BUSTER_S8("/build/directory"),
        .files = files,
        .file_count = 2,
        .functions = functions,
        .function_count = 2,
        .offsets = offsets,
        .lines = lines,
        .row_count = 5,
    };
    valid &= alone_bytes != 0 && based_bytes != 0;
    DebugOutput alone = debug_test_output(alone_bytes, alone_addresses, 2);
    DebugOutput based = debug_test_output(based_bytes, based_addresses, 2);
    const uint64_t bases[BUSTER_DEBUG_SECTIONS] = {
        [DEBUG_SECTION_INFO] = 0x5000000,
        [DEBUG_SECTION_ABBREV] = 0x123,
        [DEBUG_SECTION_LINE] = 0x2000,
        [DEBUG_SECTION_ADDR] = 0x30008,
        [DEBUG_SECTION_RNGLISTS] = 0x40000c,
    };
    memcpy(based.bases, bases, sizeof(bases));
    valid &= valid && debug_write(&unit, &alone, false) == DEBUG_OK && debug_write(&unit, &based, false) == DEBUG_OK;
    for (uint32_t section = 0; valid && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        valid &= alone.sections[section].size == based.sections[section].size;
        valid &= section == DEBUG_SECTION_INFO || memcmp(alone.sections[section].bytes, based.sections[section].bytes, alone.sections[section].size) == 0;
    }
    // The header's abbreviation offset at 8; after the DIE's code, its three strings and the language, the line
    // program's, the address table's and the range lists' offsets.
    const uint64_t stmt_list = 12 + 1 + (files[0].length + 1) + (unit.directory.length + 1) + sizeof("buster") + 2;
    const uint64_t places[4] = { 8, stmt_list, stmt_list + 4, stmt_list + 8 };
    const uint32_t values[4] = { 0x123, 0x2000, 0x30008 + 8, 0x40000c + 12 };
    const uint32_t alone_values[4] = { 0, 0, 8, 12 };
    for (uint64_t at = 0; valid && at < alone.sections[DEBUG_SECTION_INFO].size; at += 1)
    {
        uint32_t field = 4;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            field = at >= places[i] && at < places[i] + 4 ? i : field;
        }
        valid &= field != 4 || alone.sections[DEBUG_SECTION_INFO].bytes[at] == based.sections[DEBUG_SECTION_INFO].bytes[at];
    }
    for (uint32_t i = 0; valid && i < 4; i += 1)
    {
        uint32_t alone_value = 0;
        uint32_t based_value = 0;
        memcpy(&alone_value, alone.sections[DEBUG_SECTION_INFO].bytes + places[i], 4);
        memcpy(&based_value, based.sections[DEBUG_SECTION_INFO].bytes + places[i], 4);
        valid &= alone_value == alone_values[i] && based_value == values[i];
    }
    // Past DWARF32: each section's base at which its contribution would end at the limit is refused, with nothing
    // written, and one byte lower is taken.
    for (uint32_t section = 0; valid && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        DebugOutput refused = debug_test_output(based_bytes, based_addresses, 2);
        refused.bases[section] = BUSTER_DEBUG_DWARF32_LIMIT - alone.sections[section].size;
        memset(based_bytes, 0xa5, BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
        based_addresses[0] = UINT32_MAX;
        valid &= debug_write(&unit, &refused, false) == DEBUG_ERROR_LIMIT && based_addresses[0] == UINT32_MAX && based_bytes[0] == 0xa5;
        refused.bases[section] -= 1;
        valid &= debug_write(&unit, &refused, false) == DEBUG_OK;
    }
    free(alone_bytes);
    free(based_bytes);
    printf("debug bases: a unit after others differs from it alone only by its four offsets, %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool debug_test_guards(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 6 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED;
    if (valid)
    {
        valid &= mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0 && mprotect(mapping + 2 * page, page, PROT_READ | PROT_WRITE) == 0 &&
                 mprotect(mapping + 4 * page, page, PROT_READ | PROT_WRITE) == 0;
        for (uint32_t count = 0; valid && count <= 65; count += 1)
        {
            uint32_t* offsets = (uint32_t*)(mapping + page - count * 4);
            uint32_t* lines = (uint32_t*)(mapping + 3 * page - count * 4);
            for (uint32_t row = 0; row < count; row += 1)
            {
                offsets[row] = row * 7;
                lines[row] = row % 2 ? INT32_MAX : 0;
            }
            const String8 file = BUSTER_S8("guard.c");
            const DebugFunction function = {
                .name = BUSTER_S8("guard"),
                .size = 1000,
                .row_count = count,
            };
            const DebugUnit unit = {
                .files = &file,
                .file_count = 1,
                .functions = &function,
                .function_count = 1,
                .offsets = offsets,
                .lines = lines,
                .row_count = count,
            };
            DebugWriter measure = { 0 };
            debug_line(&unit, &measure, 0, false);
            DebugWriter write = { .bytes = mapping + 5 * page - measure.size };
            debug_line(&unit, &write, 0, false);
            valid &= measure.size == write.size;
        }
        munmap(mapping, 6 * page);
    }
    printf("debug rows: protected input and output tails through 65: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool debug_test_write_file(const char* path, const void* bytes, uint64_t size)
{
    FILE* file = fopen(path, "wb");
    bool result = file != 0;
    if (file != 0)
    {
        result = fwrite(bytes, 1, size, file) == size;
        result &= fclose(file) == 0;
    }
    return result;
}

static bool debug_test_frame(const char* output, const char* frame, const char* function, const char* location)
{
    const char* start = output != 0 ? strstr(output, frame) : 0;
    const char* end = start != 0 ? strchr(start, '\n') : 0;
    const char* name = start != 0 ? strstr(start, function) : 0;
    const char* source = start != 0 ? strstr(start, location) : 0;
    const char* after = source != 0 ? source + strlen(location) : 0;
    return end != 0 && name != 0 && name < end && source != 0 && source < end && !(*after >= '0' && *after <= '9');
}

static bool debug_test_profile(const char* output)
{
    bool found = false;
    const char* sample = output;
    while (!found && sample != 0 && *sample != 0)
    {
        while (*sample == '\n')
        {
            sample += 1;
        }
        const char* end = strstr(sample, "\n\n");
        if (end == 0)
        {
            end = sample + strlen(sample);
        }
        const char* leaf = strstr(sample, "debug_leaf");
        const char* location = leaf != 0 ? strstr(leaf, "leaf.c:5") : 0;
        const bool exact_line = location != 0 && !(location[8] >= '0' && location[8] <= '9');
        const char* outer = exact_line ? strstr(location, "debug_outer") : 0;
        const char* start = outer != 0 ? strstr(outer, "_start") : 0;
        found = start != 0 && start < end;
        sample = *end != 0 ? end + 2 : 0;
    }
    return found;
}

static bool debug_test_consumers(bool keep, bool native)
{
    char directory[] = "/tmp/buster-debug-XXXXXX";
    bool valid = mkdtemp(directory) != 0;
    uint8_t* bytes = malloc(BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES);
    uint32_t address_offsets[3];
    const uint64_t addresses[] = { 0x401000, 0x401040, 0x401080 };
    String8 files[] = { BUSTER_S8("start.c"), BUSTER_S8("leaf.c") };
    const DebugFunction functions[] = {
        {
            .name = BUSTER_S8("_start"),
            .size = 14,
            .first_row = 0,
            .row_count = 4,
            .file = 0,
            .line = 1,
        },
        {
            .name = BUSTER_S8("debug_outer"),
            .size = 14,
            .first_row = 4,
            .row_count = 4,
            .file = 0,
            .line = 10,
        },
        {
            .name = BUSTER_S8("debug_leaf"),
            .size = 11,
            .first_row = 8,
            .row_count = 4,
            .file = 1,
            .line = 4,
        },
    };
    const uint32_t offsets[] = { 0, 5, 10, 12, 0, 4, 9, 13, 0, 5, 8, 10 };
    const uint32_t lines[] = { 1, 2, 3, 4, 10, 11, 12, 13, 4, 5, 5, 6 };
    const DebugUnit unit = {
        .directory = {
            .pointer = directory,
            .length = strlen(directory),
        },
        .files = files,
        .file_count = 2,
        .functions = functions,
        .function_count = 3,
        .offsets = offsets,
        .lines = lines,
        .row_count = 12,
    };
    DebugOutput output = debug_test_output(bytes, address_offsets, 3);
    valid &= bytes != 0;
    if (valid)
    {
        valid &= debug_write(&unit, &output, false) == DEBUG_OK && debug_finalize(&unit, &output, addresses) == DEBUG_OK;
    }
    char assembly[512];
    char object[512];
    char executable[512];
    char out[512];
    char err[512];
    char data[512];
    snprintf(assembly, sizeof(assembly), "%s/fixture.s", directory);
    snprintf(object, sizeof(object), "%s/fixture.o", directory);
    snprintf(executable, sizeof(executable), "%s/fixture", directory);
    snprintf(out, sizeof(out), "%s/stdout", directory);
    snprintf(err, sizeof(err), "%s/stderr", directory);
    snprintf(data, sizeof(data), "%s/perf.data", directory);
    FILE* source = valid ? fopen(assembly, "w") : 0;
    valid &= source != 0;
    if (source != 0)
    {
        fputs(".text\n.globl _start,debug_outer,debug_leaf\n.type _start,@function\n_start:\n.cfi_startproc\n.cfi_undefined 16\ncall debug_outer\nmov $60,%eax\nxor %edi,%edi\nsyscall\n.cfi_endproc\n.size _start,.-_start\n.balign 64\n.type debug_outer,@function\ndebug_outer:\n.cfi_startproc\nsub $8,%rsp\n.cfi_def_cfa_offset 16\ncall debug_leaf\nadd $8,%rsp\n.cfi_def_cfa_offset 8\nret\n.cfi_endproc\n.size debug_outer,.-debug_outer\n.balign 64\n.type debug_leaf,@function\ndebug_leaf:\n.cfi_startproc\nmov $100000000,%ecx\n1: sub $1,%ecx\njne 1b\nret\n.cfi_endproc\n.size debug_leaf,.-debug_leaf\n.section .note.GNU-stack,\"\",@progbits\n", source);
        const char* names[] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/section%u", directory, section);
            valid &= debug_test_write_file(path, output.sections[section].bytes, output.sections[section].size);
            fprintf(source, ".section %s,\"\",@progbits\n.incbin \"%s\"\n", names[section], path);
        }
        valid &= fclose(source) == 0;
        char path[512];
        snprintf(path, sizeof(path), "%s/start.c", directory);
        const char start[] = "void _start(void) {\n debug_outer();\n // exit\n}\n\n\n\n\n\nvoid debug_outer(void) {\n debug_leaf();\n // done\n}\n";
        valid &= debug_test_write_file(path, start, sizeof(start) - 1);
        snprintf(path, sizeof(path), "%s/leaf.c", directory);
        const char leaf[] = "// original debugger fixture\n\n\nvoid debug_leaf(void) {\n // counted loop\n}\n";
        valid &= debug_test_write_file(path, leaf, sizeof(leaf) - 1);
    }
    char* assemble[] = { "as", "--64", "-o", object, assembly, 0 };
    char* link[] = { "ld", "--section-start=.text=0x401000", "--eh-frame-hdr", "-o", executable, object, 0 };
    char* read[] = { "readelf", "--debug-dump=info,decodedline,addr,Ranges", executable, 0 };
    char* gdb[] = { "gdb", "-nx", "-batch", "-ex", "set debuginfod enabled off", "-ex", "break leaf.c:5", "-ex", "run", "-ex", "bt", executable, 0 };
    // A bounded sample rate and ring hold this short loop's stack snapshots without overflowing when pinned to one CPU.
    char* record[] = { "perf", "record", "-q", "-m", "4M", "-e", "cycles:u", "-c", "1000000", "--call-graph", "dwarf", "-o", data, "--", executable, 0 };
    char* script[] = { "perf", "script", "-i", data, "-F", "ip,sym,srcline", 0 };
    Arena arena = arena_reserve(1ull << 24);
    valid &= arena.base != 0;
    if (valid)
    {
        valid &= object_test_run(assemble, out, err) == 0;
        valid &= native ? debug_test_native_write(object, executable, &unit, &output) : object_test_run(link, out, err) == 0;
        valid &= object_test_run(read, out, err) == 0;
        String8 readout = object_test_read_file(&arena, out);
        valid &= readout.pointer != 0 && strstr(readout.pointer, "debug_leaf") != 0 && strstr(readout.pointer, "leaf.c") != 0;
        valid &= object_test_run(gdb, out, err) == 0;
        String8 backtrace = object_test_read_file(&arena, out);
        const bool gdb_ok = debug_test_frame(backtrace.pointer, "#0 ", "debug_leaf", "leaf.c:5") &&
                            debug_test_frame(backtrace.pointer, "#1 ", "debug_outer", "start.c:11") && debug_test_frame(backtrace.pointer, "#2 ", "_start", "start.c:1");
        valid &= gdb_ok;
        printf("debug gdb (%s output): source breakpoint and three-frame backtrace: %s\n", native ? "Buster" : "GNU ld", gdb_ok ? "pass" : "FAIL");
        if (!gdb_ok && backtrace.pointer != 0)
        {
            printf("%s\n", backtrace.pointer);
        }
        const bool recorded = object_test_run(record, out, err) == 0;
        const bool scripted = recorded && object_test_run(script, out, err) == 0;
        String8 profile = object_test_read_file(&arena, out);
        const bool perf_ok = scripted && debug_test_profile(profile.pointer);
        valid &= perf_ok;
        printf("debug perf (%s output): sampled DWARF call chains and source lines: %s\n", native ? "Buster" : "GNU ld", perf_ok ? "pass" : "FAIL");
    }
    arena_release(&arena);
    free(bytes);
    if (keep || !valid)
    {
        printf("debug fixture files: %s\n", directory);
    }
    else
    {
        char* remove[] = { "rm", "-r", directory, 0 };
        valid &= object_test_run(remove, "/dev/null", "/dev/null") == 0;
    }
    return valid;
}

static int run_debug_tests(int argc, char** argv)
{
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    if (valid)
    {
        valid &= debug_test_rows();
        valid &= debug_test_random_rows();
        valid &= debug_test_units();
        valid &= debug_test_bases();
        valid &= debug_test_guards();
        valid &= debug_test_profile("debug_leaf\nleaf.c:5\ndebug_outer\n_start\n\n");
        valid &= !debug_test_profile("debug_leaf\nleaf.c:5\n\ndebug_outer\n_start\n\n");
        valid &= !debug_test_profile("debug_leaf\nleaf.c:50\ndebug_outer\n_start\n\n");
        valid &= !debug_test_frame("#2 _start () at start.c:11\n", "#2 ", "_start", "start.c:1");
        if (cross_check)
        {
            valid &= debug_test_consumers(keep, false);
            valid &= debug_test_consumers(keep, true);
        }
    }
    else
    {
        fprintf(stderr, "usage: test-debug [--cross-check] [--keep]\n");
    }
    return valid ? 0 : 1;
}
