// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent decoding of the emitted call-frame program, scalar/vector equality, bounded-tail checks and native
// consumers. The row oracle models the stack after each machine transition; it does not reuse the writer's tables.

typedef struct UnwindTestReader UnwindTestReader;
struct UnwindTestReader
{
    const uint8_t* bytes;
    uint64_t at;
    uint64_t end;
    bool valid;
};

static uint64_t unwind_test_integer(UnwindTestReader* reader, uint32_t width)
{
    uint64_t value = 0;
    if (width > 8 || reader->at > reader->end || width > reader->end - reader->at)
    {
        reader->valid = false;
    }
    else
    {
        for (uint32_t i = 0; i < width; i += 1)
        {
            value |= (uint64_t)reader->bytes[reader->at + i] << (8 * i);
        }
        reader->at += width;
    }
    return value;
}

static uint64_t unwind_test_uleb(UnwindTestReader* reader)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    uint64_t byte = 0;
    do
    {
        byte = unwind_test_integer(reader, 1);
        reader->valid &= shift < 63;
        if (reader->valid)
        {
            value |= (byte & 127) << shift;
        }
        shift += 7;
    } while (reader->valid && (byte & 128) != 0);
    return value;
}

typedef struct UnwindTestRow UnwindTestRow;
struct UnwindTestRow
{
    uint64_t cfa;
    uint32_t cfa_register;
    int64_t rules[17];
};

static bool unwind_test_row(const UnwindFunction* function, uint32_t pc, const UnwindTestRow* row)
{
    const uint32_t registers[] = { 3, 6, 12, 13, 14, 15 };
    const uint32_t pushes = (uint32_t)__builtin_popcount(function->saved);
    uint32_t pushed = 0;
    uint32_t popped = 0;
    for (uint32_t i = 0; i < pushes; i += 1)
    {
        pushed += pc >= function->offsets[i];
        popped += pc >= function->offsets[pushes + 2 * (function->frame_size != 0) + i];
    }
    const bool allocated = function->frame_size != 0 && pc >= function->offsets[pushes] && pc < function->offsets[pushes + 1];
    bool valid = popped <= pushed && row->cfa_register == 7 && row->cfa == 8 + 8 * (pushed - popped) + (allocated ? function->frame_size : 0) &&
                 row->rules[16] == -8;
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < 6; i += 1)
    {
        int64_t expected = 0;
        if ((function->saved & (1u << i)) != 0)
        {
            expected = ordinal < pushed - popped ? -(int64_t)(16 + 8 * ordinal) : 0;
            ordinal += 1;
        }
        valid &= row->rules[registers[i]] == expected;
    }
    return valid;
}

static bool unwind_test_instructions(UnwindTestReader* reader, UnwindTestRow* row, const UnwindTestRow* initial, const UnwindFunction* function)
{
    uint32_t pc = 0;
    uint32_t transitions = 0;
    while (reader->valid && reader->at < reader->end)
    {
        const uint32_t op = (uint32_t)unwind_test_integer(reader, 1);
        if (op == 0)
        {
            // Padding does not change the row.
        }
        else if (op == 4)
        {
            const uint64_t delta = unwind_test_integer(reader, 4);
            reader->valid &= function != 0 && delta != 0 && delta <= UINT32_MAX - pc;
            if (reader->valid)
            {
                reader->valid &= unwind_test_row(function, pc, row);
                pc += (uint32_t)delta;
                reader->valid &= transitions < function->event_count && pc == function->offsets[transitions];
                transitions += 1;
            }
        }
        else if (op == 0x0c)
        {
            row->cfa_register = (uint32_t)unwind_test_uleb(reader);
            row->cfa = unwind_test_uleb(reader);
        }
        else if (op == 0x0e)
        {
            row->cfa = unwind_test_uleb(reader);
        }
        else if (op == 8)
        {
            const uint64_t reg = unwind_test_uleb(reader);
            reader->valid &= reg < 17;
            if (reader->valid)
            {
                row->rules[reg] = 0;
            }
        }
        else if ((op & 0xc0) == 0x80)
        {
            const uint32_t reg = op & 63;
            const uint64_t offset = unwind_test_uleb(reader);
            reader->valid &= reg < 17 && offset <= INT64_MAX / 8;
            if (reader->valid)
            {
                row->rules[reg] = -(int64_t)(offset * 8);
            }
        }
        else if ((op & 0xc0) == 0xc0)
        {
            const uint32_t reg = op & 63;
            reader->valid &= reg < 17 && initial != 0;
            if (reader->valid)
            {
                row->rules[reg] = initial->rules[reg];
            }
        }
        else
        {
            reader->valid = false;
        }
    }
    if (function != 0 && reader->valid)
    {
        reader->valid &= transitions == function->event_count && unwind_test_row(function, pc, row);
    }
    return reader->valid;
}

static bool unwind_test_decode(const UnwindFunction* functions, uint32_t count, const UnwindOutput* output)
{
    UnwindTestReader reader = {
        .bytes = output->bytes,
        .end = output->size,
        .valid = true,
    };
    for (uint32_t i = 0; reader.valid && i < count; i += 1)
    {
        const uint64_t begin = reader.at;
        const uint64_t cie_length = unwind_test_integer(&reader, 4);
        const uint64_t cie_end = reader.at + cie_length;
        reader.valid &= cie_length != 0 && cie_end <= output->size && (begin & 7) == 0;
        reader.valid &= unwind_test_integer(&reader, 4) == 0 && unwind_test_integer(&reader, 1) == 1;
        reader.valid &= unwind_test_integer(&reader, 1) == 'z' && unwind_test_integer(&reader, 1) == 'R' && unwind_test_integer(&reader, 1) == 0;
        reader.valid &= unwind_test_uleb(&reader) == 1 && unwind_test_integer(&reader, 1) == 0x78 && unwind_test_integer(&reader, 1) == 16;
        reader.valid &= unwind_test_uleb(&reader) == 1 && unwind_test_integer(&reader, 1) == 0x1b;
        UnwindTestRow initial = { 0 };
        for (uint32_t reg = 0; reg < 17; reg += 1)
        {
            initial.rules[reg] = INT64_MAX;
        }
        reader.end = cie_end;
        reader.valid &= unwind_test_instructions(&reader, &initial, 0, 0);
        reader.end = output->size;
        const uint64_t fde_length = unwind_test_integer(&reader, 4);
        const uint64_t fde_end = reader.at + fde_length;
        const uint64_t pointer = reader.at;
        reader.valid &= fde_end <= output->size && (fde_end & 7) == 0 && unwind_test_integer(&reader, 4) == pointer - begin;
        reader.valid &= reader.at == output->locations[i] && unwind_test_integer(&reader, 4) == 0;
        reader.valid &= unwind_test_integer(&reader, 4) == functions[i].size && unwind_test_uleb(&reader) == 0;
        UnwindTestRow row = initial;
        reader.end = fde_end;
        reader.valid &= unwind_test_instructions(&reader, &row, &initial, &functions[i]);
        reader.end = output->size;
    }
    return reader.valid && reader.at == output->size;
}

static UnwindFunction unwind_test_description(uint32_t saved, uint32_t frame)
{
    UnwindFunction function = {
        .frame_size = frame,
        .saved = (uint8_t)saved,
        .event_count = (uint8_t)(2 * __builtin_popcount(saved) + 2 * (frame != 0)),
    };
    uint32_t offset = 0;
    for (uint32_t event = 0; event < function.event_count; event += 1)
    {
        offset += event % 3 == 0 ? 65537 : event + 1;
        function.offsets[event] = offset;
    }
    function.size = offset + 1;
    return function;
}

static bool unwind_test_emitters(void)
{
    const uint32_t thresholds[] = { 128, 16384, 2097152, 268435456 };
    UnwindFunction functions[64 * 11];
    uint32_t count = 0;
    for (uint32_t saved = 0; saved < 64; saved += 1)
    {
        const uint32_t base = 8 + 8 * (uint32_t)__builtin_popcount(saved);
        functions[count++] = unwind_test_description(saved, 0);
        functions[count++] = unwind_test_description(saved, 1);
        functions[count++] = unwind_test_description(saved, INT32_MAX);
        for (uint32_t width = 0; width < BUSTER_ARRAY_LENGTH(thresholds); width += 1)
        {
            functions[count++] = unwind_test_description(saved, thresholds[width] - base - 1);
            functions[count++] = unwind_test_description(saved, thresholds[width] - base);
        }
    }
    bool valid = unwind_check(functions, count) == UNWIND_OK;
    uint8_t bytes[2][64 * 11 * 192];
    uint32_t locations[2][64 * 11];
    UnwindOutput outputs[2] = { 0 };
    for (uint32_t way = 0; way < 2; way += 1)
    {
        outputs[way] = (UnwindOutput){
            .bytes = bytes[way],
            .capacity = sizeof(bytes[way]),
            .locations = locations[way],
            .location_capacity = count,
        };
        memset(bytes[way], 0xa5, sizeof(bytes[way]));
        valid &= unwind_write(functions, count, &outputs[way], way == 0) == UNWIND_OK && unwind_test_decode(functions, count, &outputs[way]);
    }
    valid &= outputs[0].size == outputs[1].size && memcmp(bytes[0], bytes[1], outputs[0].size) == 0 && memcmp(locations[0], locations[1], sizeof(locations[0])) == 0;
    // Negative controls for the independent interpreter: corrupt the return-address rule and the first FDE's range.
    bytes[0][21] ^= 3;
    valid &= !unwind_test_decode(functions, count, &outputs[0]);
    bytes[0][21] ^= 3;
    bytes[0][locations[0][0] + 4] ^= 1;
    valid &= !unwind_test_decode(functions, count, &outputs[0]);
    bytes[0][locations[0][0] + 4] ^= 1;
    // Every refusal is before stores, including an insufficient location slice after the byte slice fits.
    for (uint32_t way = 0; way < 2; way += 1)
    {
        for (uint32_t failure = 0; failure < 2; failure += 1)
        {
            UnwindOutput short_output = outputs[way];
            short_output.capacity = failure == 0 ? outputs[way].size - 1 : outputs[way].size;
            short_output.location_capacity = count - (failure != 0);
            memset(bytes[way], 0xa5, sizeof(bytes[way]));
            memset(locations[way], 0xa5, sizeof(locations[way]));
            valid &= unwind_write(functions, count, &short_output, way == 0) == UNWIND_ERROR_CAPACITY && short_output.size == outputs[way].size;
            for (uint32_t byte = 0; byte < sizeof(bytes[way]); byte += 1)
            {
                valid &= bytes[way][byte] == 0xa5;
            }
            for (uint32_t location = 0; location < count; location += 1)
            {
                valid &= locations[way][location] == 0xa5a5a5a5;
            }
        }
    }
    printf("unwind emitters: %u save/frame combinations, independent CFI rows and atomic capacity refusal: %s\n", count, valid ? "pass" : "FAIL");
    return valid;
}

static bool unwind_test_diagnostics(void)
{
    const UnwindFunction good = unwind_test_description(63, 4096);
    bool valid = unwind_check(0, 0) == UNWIND_OK && unwind_check(0, 1) == UNWIND_ERROR_INVALID;
    for (uint32_t change = 0; change < 8; change += 1)
    {
        UnwindFunction bad = good;
        switch (change)
        {
            case 0: bad.size = 0; break;
            case 1: bad.saved = 64; break;
            case 2: bad.reserved = 1; break;
            case 3: bad.event_count -= 1; break;
            case 4: bad.offsets[1] = bad.offsets[0]; break;
            case 5: bad.offsets[13] = bad.size; break;
            case 6: bad.size = (uint32_t)INT32_MAX + 1; break;
            case 7: bad.frame_size = (uint32_t)INT32_MAX + 1; break;
        }
        valid &= unwind_check(&bad, 1) == (change >= 6 ? UNWIND_ERROR_LIMIT : UNWIND_ERROR_INVALID);
        if (change >= 6)
        {
            for (uint32_t way = 0; way < 2; way += 1)
            {
                uint8_t byte = 0x5a;
                uint32_t location = 0x12345678;
                UnwindOutput output = {
                    .bytes = &byte,
                    .capacity = 1,
                    .size = 456,
                    .locations = &location,
                    .location_capacity = 1,
                };
                const UnwindOutput before = output;
                valid &= unwind_write(&bad, 1, &output, way == 0) == UNWIND_ERROR_LIMIT && memcmp(&output, &before, sizeof(output)) == 0 &&
                         byte == 0x5a && location == 0x12345678;
            }
        }
    }
    UnwindFunction inactive = unwind_test_description(0, 0);
    inactive.offsets[13] = 1;
    valid &= unwind_check(&inactive, 1) == UNWIND_ERROR_INVALID;
    printf("unwind descriptor diagnostics and representability refusal: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool unwind_test_guards(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t region = 4 * page;
    uint8_t* mapping = mmap(0, 6 * region, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED;
    if (valid)
    {
        valid &= mprotect(mapping, region, PROT_READ | PROT_WRITE) == 0 && mprotect(mapping + 2 * region, region, PROT_READ | PROT_WRITE) == 0 &&
                 mprotect(mapping + 4 * region, region, PROT_READ | PROT_WRITE) == 0;
        for (uint32_t count = 0; valid && count <= 65; count += 1)
        {
            UnwindFunction* functions = (UnwindFunction*)(mapping + region - count * sizeof(UnwindFunction));
            for (uint32_t i = 0; i < count; i += 1)
            {
                functions[i] = unwind_test_description(i & 63, i % 2 != 0 ? INT32_MAX : 0);
            }
            for (uint32_t way = 0; valid && way < 2; way += 1)
            {
                UnwindOutput output = { 0 };
                const UnwindError measured = unwind_write(functions, count, &output, way == 0);
                valid &= measured == (count == 0 ? UNWIND_OK : UNWIND_ERROR_CAPACITY) && output.size <= region;
                output.bytes = mapping + 3 * region - output.size;
                output.capacity = output.size;
                output.locations = (uint32_t*)(mapping + 5 * region - count * sizeof(uint32_t));
                output.location_capacity = count;
                valid &= unwind_write(functions, count, &output, way == 0) == UNWIND_OK && unwind_test_decode(functions, count, &output);
            }
        }
        munmap(mapping, 6 * region);
    }
    printf("unwind emitters: guarded descriptor/byte/location tails 0..65: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool unwind_test_consumers(bool keep);
static bool unwind_native_oracle_tests(void);

static int run_unwind_tests(int argc, char** argv)
{
    bool cross = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross = true;
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
    valid &= x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize();
    if (valid)
    {
        valid &= unwind_test_emitters();
        valid &= unwind_test_diagnostics();
        valid &= unwind_test_guards();
        valid &= unwind_tier0_test();
        valid &= unwind_native_oracle_tests();
        if (cross)
        {
            valid &= unwind_test_consumers(keep);
        }
    }
    return valid ? 0 : 1;
}
