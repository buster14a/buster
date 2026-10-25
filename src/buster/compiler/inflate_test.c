// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-inflate [--seed N] [--directory PATH] [--keep]: inflate.c against streams of the test's own making and of
// gzip's.
//   - Adler-32: the known values, and random inputs against a sum taken modulo at every byte, by bytes and by vectors;
//     and the vector path against the byte path at every length to 300 and around its chunks and blocks, of random
//     bytes and of 0xff, ending at a PROT_NONE page;
//   - streams by hand, bit by bit: the empty stream, a stored block, fixed blocks with a literal and with a match, and
//     each error the decoder reports: the header's (another method, a window past 32 KB, a check that fails, a preset
//     dictionary), a block of type 3, a stored block whose lengths disagree, a dynamic block whose code length code is
//     oversubscribed, one without an end-of-block code, a length repeat with nothing to repeat, a distance before the
//     output's start, a stream with a byte after its data, and a checksum that differs;
//   - gzip's DEFLATE data (its `-c -n` output without its 10-byte header and its trailer) of generated inputs, wrapped
//     as zlib streams: random bytes, text of DWARF's strings, runs of a byte, and a mix of the three, of 0 bytes to 1
//     MB, at levels 1, 6 and 9: each must decompress into its input, and into an output a byte short or long fail;
//   - each stream to 4 KB cut at every length, and with each byte changed three ways, ending at a PROT_NONE page and
//     decompressed into an output ending at one: a read or a write past either faults; a cut must fail, and a change
//     the decoder accepts must give the input back (it changed bits the data doesn't use).
// The files go in a directory under --directory (TMPDIR or /tmp), removed unless --keep.

typedef struct InflateTestTally InflateTestTally;
struct InflateTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void inflate_test_expect(InflateTestTally* tally, bool condition, const char* what, uint64_t detail)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 20)
        {
            printf("  failed: %s (%llu)\n", what, (unsigned long long)detail);
        }
    }
}

// Bits written as DEFLATE packs them: a value from its lowest bit, a Huffman code from its first.
typedef struct InflateTestBits InflateTestBits;
struct InflateTestBits
{
    uint8_t bytes[512];
    uint32_t count;
};

static void inflate_test_value(InflateTestBits* bits, uint32_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count && bits->count < 8 * sizeof(bits->bytes); i += 1)
    {
        bits->bytes[bits->count / 8] |= (uint8_t)(((value >> i) & 1) << (bits->count % 8));
        bits->count += 1;
    }
}

static void inflate_test_code(InflateTestBits* bits, uint32_t code, uint32_t length)
{
    for (uint32_t i = 0; i < length; i += 1)
    {
        inflate_test_value(bits, (code >> (length - 1 - i)) & 1, 1);
    }
}

// A zlib stream of `data` (`length` bytes of DEFLATE data) with `header`'s two bytes and the Adler-32 of `output`'s
// `size` bytes, into `stream`; its length.
static uint64_t inflate_test_wrap(uint8_t* stream, const uint8_t header[2], const uint8_t* data, uint64_t length, const uint8_t* output, uint64_t size)
{
    const uint32_t adler = inflate_adler32(output, size);
    stream[0] = header[0];
    stream[1] = header[1];
    memcpy(stream + 2, data, length);
    stream[length + 2] = (uint8_t)(adler >> 24);
    stream[length + 3] = (uint8_t)(adler >> 16);
    stream[length + 4] = (uint8_t)(adler >> 8);
    stream[length + 5] = (uint8_t)adler;
    return length + 6;
}

// A header whose check passes for `method`, with FDICT when asked.
static void inflate_test_header(uint8_t header[2], uint8_t method, bool dictionary)
{
    header[0] = method;
    uint32_t flags = dictionary ? 0x20 : 0;
    flags += 31 - (((uint32_t)method << 8 | flags) % 31);
    flags = flags >= 32 && !dictionary ? flags - 31 : flags;
    header[1] = (uint8_t)flags;
}

// `bytes` rounded up to whole pages and ending at a PROT_NONE page: the mapping and where the bytes start.
typedef struct InflateTestGuarded InflateTestGuarded;
struct InflateTestGuarded
{
    uint8_t* mapping;
    uint64_t mapped;
    uint8_t* start;
};

static InflateTestGuarded inflate_test_guard(uint64_t bytes)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t pages = (bytes + page - 1) / page + 1;
    InflateTestGuarded result = { .mapped = (pages + 1) * page };
    uint8_t* mapping = mmap(0, result.mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED && mprotect(mapping, pages * page, PROT_READ | PROT_WRITE) == 0)
    {
        result.mapping = mapping;
        result.start = mapping + pages * page - bytes;
    }
    return result;
}

static void inflate_test_unguard(InflateTestGuarded* guarded)
{
    if (guarded->mapping != 0)
    {
        munmap(guarded->mapping, guarded->mapped);
    }
    *guarded = (InflateTestGuarded){ 0 };
}

// Decompresses `stream` from a copy ending at a PROT_NONE page into an output of `size` bytes ending at one, which
// `expected` (when given) must equal when it decodes; the error.
static InflateError inflate_test_guarded(const uint8_t* stream, uint64_t length, uint64_t size, const uint8_t* expected, bool* same)
{
    InflateTestGuarded input = inflate_test_guard(length);
    InflateTestGuarded output = inflate_test_guard(size);
    InflateError result = INFLATE_ERROR_DATA;
    *same = false;
    if (input.mapping != 0 && output.mapping != 0)
    {
        memcpy(input.start, stream, length);
        result = inflate_zlib(input.start, length, output.start, size);
        *same = result == INFLATE_ERROR_NONE && (expected == 0 || memcmp(output.start, expected, size) == 0);
    }
    inflate_test_unguard(&input);
    inflate_test_unguard(&output);
    return result;
}

static uint64_t inflate_test_next(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static void inflate_test_adler(InflateTestTally* tally, uint64_t seed)
{
    static const struct
    {
        const char* text;
        uint32_t adler;
    } known[] = {
        { "", 0x00000001u },
        { "a", 0x00620062u },
        { "abc", 0x024d0127u },
        { "Wikipedia", 0x11e60398u },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(known); i += 1)
    {
        inflate_test_expect(tally, inflate_adler32((const uint8_t*)known[i].text, strlen(known[i].text)) == known[i].adler, "a known Adler-32", i);
        inflate_test_expect(tally, inflate_adler32_vector((const uint8_t*)known[i].text, strlen(known[i].text)) == known[i].adler, "a known Adler-32, by vectors", i);
    }
    uint64_t state = seed;
    static uint8_t bytes[200000];
    for (uint32_t run = 0; run < 8; run += 1)
    {
        const uint64_t length = inflate_test_next(&state) % sizeof(bytes);
        for (uint64_t i = 0; i < length; i += 1)
        {
            bytes[i] = run & 1 ? 0xff : (uint8_t)inflate_test_next(&state);
        }
        uint32_t a = 1;
        uint32_t b = 0;
        for (uint64_t i = 0; i < length; i += 1)
        {
            a = (a + bytes[i]) % 65521;
            b = (b + a) % 65521;
        }
        inflate_test_expect(tally, inflate_adler32(bytes, length) == (b << 16 | a), "Adler-32 against the sum taken modulo at every byte", length);
        inflate_test_expect(tally, inflate_adler32_vector(bytes, length) == (b << 16 | a), "Adler-32 by vectors against the sum taken modulo at every byte", length);
    }
    // The vector path against its twin: every length to 300 and around the 64-byte chunks and the blocks of 4,096 of
    // them, of random bytes and of 0xff (the largest sums), each ending at a PROT_NONE page, where a read past the end
    // faults.
    const uint64_t block = 64 * 4096;
    const uint64_t lengths[] = {
        511, 512, 513, 4095, 4096, 4097, 5551, 5552, 5553, 65535, 65536, 65537,
        block - 1, block, block + 1, 2 * block - 1, 2 * block, 2 * block + 1, 3 * block - 1, 3 * block, 3 * block + 63,
    };
    const uint64_t largest = 3 * block + 64;
    InflateTestGuarded guarded = inflate_test_guard(largest);
    inflate_test_expect(tally, guarded.mapping != 0, "map an input ending at a PROT_NONE page", largest);
    for (uint32_t fill = 0; guarded.mapping != 0 && fill < 2; fill += 1)
    {
        for (uint64_t length = 0; length <= 300 + BUSTER_ARRAY_LENGTH(lengths); length += 1)
        {
            const uint64_t size = length <= 300 ? length : lengths[length - 301];
            uint8_t* start = guarded.start + largest - size;
            for (uint64_t i = 0; i < size; i += 1)
            {
                start[i] = fill != 0 ? 0xff : (uint8_t)inflate_test_next(&state);
            }
            inflate_test_expect(tally, inflate_adler32_vector(start, size) == inflate_adler32(start, size), "Adler-32 by vectors as by bytes, at a page's end", size);
        }
    }
    inflate_test_unguard(&guarded);
}

static void inflate_test_by_hand(InflateTestTally* tally)
{
    uint8_t header[2];
    inflate_test_header(header, 0x78, false);
    uint8_t stream[1024];
    uint8_t output[64];
    bool same = false;
    // The empty stream: a fixed block of an end-of-block code alone.
    InflateTestBits bits = { 0 };
    inflate_test_value(&bits, 1, 1);
    inflate_test_value(&bits, 1, 2);
    inflate_test_code(&bits, 0, 7);
    uint64_t length = inflate_test_wrap(stream, header, bits.bytes, (bits.count + 7) / 8, output, 0);
    inflate_test_expect(tally, inflate_test_guarded(stream, length, 0, 0, &same) == INFLATE_ERROR_NONE, "the empty stream", 0);
    // A stored block of "abc".
    static const uint8_t stored[] = { 0x01, 0x03, 0x00, 0xfc, 0xff, 'a', 'b', 'c' };
    length = inflate_test_wrap(stream, header, stored, sizeof(stored), (const uint8_t*)"abc", 3);
    inflate_test_expect(tally, inflate_test_guarded(stream, length, 3, (const uint8_t*)"abc", &same) == INFLATE_ERROR_NONE && same, "a stored block", 0);
    // A stored block whose lengths disagree.
    static const uint8_t disagreeing[] = { 0x01, 0x03, 0x00, 0xfd, 0xff, 'a', 'b', 'c' };
    length = inflate_test_wrap(stream, header, disagreeing, sizeof(disagreeing), (const uint8_t*)"abc", 3);
    inflate_test_expect(tally, inflate_test_guarded(stream, length, 3, 0, &same) == INFLATE_ERROR_DATA, "a stored block whose lengths disagree", 0);
    // A fixed block of a literal 'a' (8 bits: 0x30 + 97), and of 'a' then a match of 3 at distance 1 (length symbol
    // 257, 7 bits, and distance code 0, 5 bits): "a" and "aaaa".
    for (uint32_t matched = 0; matched < 2; matched += 1)
    {
        bits = (InflateTestBits){ 0 };
        inflate_test_value(&bits, 1, 1);
        inflate_test_value(&bits, 1, 2);
        inflate_test_code(&bits, 0x30 + 'a', 8);
        if (matched)
        {
            inflate_test_code(&bits, 1, 7);
            inflate_test_code(&bits, 0, 5);
        }
        inflate_test_code(&bits, 0, 7);
        const char* text = matched ? "aaaa" : "a";
        length = inflate_test_wrap(stream, header, bits.bytes, (bits.count + 7) / 8, (const uint8_t*)text, strlen(text));
        inflate_test_expect(tally, inflate_test_guarded(stream, length, strlen(text), (const uint8_t*)text, &same) == INFLATE_ERROR_NONE && same, "a fixed block", matched);
        // The same with a byte after its data, and with its checksum's last byte changed.
        uint8_t longer[64] = { 0 };
        memcpy(longer, bits.bytes, (bits.count + 7) / 8);
        length = inflate_test_wrap(stream, header, longer, (bits.count + 7) / 8 + 1, (const uint8_t*)text, strlen(text));
        inflate_test_expect(tally, inflate_test_guarded(stream, length, strlen(text), 0, &same) == INFLATE_ERROR_SIZE, "a byte after the data", matched);
        length = inflate_test_wrap(stream, header, bits.bytes, (bits.count + 7) / 8, (const uint8_t*)text, strlen(text));
        stream[length - 1] ^= 1;
        inflate_test_expect(tally, inflate_test_guarded(stream, length, strlen(text), 0, &same) == INFLATE_ERROR_CHECKSUM, "a checksum that differs", matched);
    }
    // A match before the output's start: a fixed block's match first.
    bits = (InflateTestBits){ 0 };
    inflate_test_value(&bits, 1, 1);
    inflate_test_value(&bits, 1, 2);
    inflate_test_code(&bits, 1, 7);
    inflate_test_code(&bits, 0, 5);
    inflate_test_code(&bits, 0, 7);
    length = inflate_test_wrap(stream, header, bits.bytes, (bits.count + 7) / 8, (const uint8_t*)"aaa", 3);
    inflate_test_expect(tally, inflate_test_guarded(stream, length, 3, 0, &same) == INFLATE_ERROR_DATA, "a distance before the output's start", 0);
    // A block of type 3.
    bits = (InflateTestBits){ 0 };
    inflate_test_value(&bits, 1, 1);
    inflate_test_value(&bits, 3, 2);
    length = inflate_test_wrap(stream, header, bits.bytes, 2, output, 0);
    inflate_test_expect(tally, inflate_test_guarded(stream, length, 0, 0, &same) == INFLATE_ERROR_DATA, "a block of type 3", 0);
    // Dynamic blocks: HLIT 257, HDIST 1, and a code length code of the lengths given in the header's order.
    // Shape 3 is shape 1 with three zeros for its repeat: the block of 'a' it would be.
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        bits = (InflateTestBits){ 0 };
        inflate_test_value(&bits, 1, 1);
        inflate_test_value(&bits, 2, 2);
        inflate_test_value(&bits, 0, 5);
        inflate_test_value(&bits, 0, 5);
        if (shape == 0)
        {
            // Four codes of one bit (16, 17, 18 and 0): oversubscribed.
            inflate_test_value(&bits, 0, 4);
            for (uint32_t i = 0; i < 4; i += 1)
            {
                inflate_test_value(&bits, 1, 3);
            }
        }
        else if (shape == 1 || shape == 3)
        {
            // 0, 1, 16 and 18 of two bits each (codes 00, 01, 10 and 11), 18 entries to reach 1's place: a repeat
            // first, with nothing to repeat, then what would make a block of 'a' if the repeat were three zeros (94
            // more zeros, 'a' of length 1, 158 zeros, end-of-block of length 1, no distance code) and its data.
            inflate_test_value(&bits, 14, 4);
            for (uint32_t i = 0; i < 18; i += 1)
            {
                inflate_test_value(&bits, i == 0 || i == 2 || i == 3 || i == 17 ? 2 : 0, 3);
            }
            if (shape == 1)
            {
                inflate_test_code(&bits, 2, 2);
                inflate_test_value(&bits, 0, 2);
            }
            else
            {
                inflate_test_code(&bits, 0, 2);
                inflate_test_code(&bits, 0, 2);
                inflate_test_code(&bits, 0, 2);
            }
            inflate_test_code(&bits, 3, 2);
            inflate_test_value(&bits, 94 - 11, 7);
            inflate_test_code(&bits, 1, 2);
            inflate_test_code(&bits, 3, 2);
            inflate_test_value(&bits, 138 - 11, 7);
            inflate_test_code(&bits, 3, 2);
            inflate_test_value(&bits, 20 - 11, 7);
            inflate_test_code(&bits, 1, 2);
            inflate_test_code(&bits, 0, 2);
            inflate_test_code(&bits, 0, 1);
            inflate_test_code(&bits, 1, 1);
        }
        else
        {
            // 18 and 1 of one bit each (1's code 0, 18's 1), 18 entries to reach 1's place: 'a' of length 1, the
            // distance code 0 of length 1, and every other length 0, end-of-block's too.
            inflate_test_value(&bits, 14, 4);
            for (uint32_t i = 0; i < 18; i += 1)
            {
                inflate_test_value(&bits, i == 2 || i == 17 ? 1 : 0, 3);
            }
            inflate_test_code(&bits, 1, 1);
            inflate_test_value(&bits, 97 - 11, 7);
            inflate_test_code(&bits, 0, 1);
            inflate_test_code(&bits, 1, 1);
            inflate_test_value(&bits, 138 - 11, 7);
            inflate_test_code(&bits, 1, 1);
            inflate_test_value(&bits, 21 - 11, 7);
            inflate_test_code(&bits, 0, 1);
        }
        const bool block = shape == 1 || shape == 3;
        const uint64_t size = block ? 1 : 0;
        length = inflate_test_wrap(stream, header, bits.bytes, (bits.count + 7) / 8 + (block ? 0 : 2), (const uint8_t*)"a", size);
        const InflateError error = inflate_test_guarded(stream, length, size, (const uint8_t*)"a", &same);
        inflate_test_expect(tally, shape == 3 ? error == INFLATE_ERROR_NONE && same : error == INFLATE_ERROR_DATA, "a dynamic block's code refused, or its control decoded", shape);
    }
    // The header's errors, on the empty stream's data.
    bits = (InflateTestBits){ 0 };
    inflate_test_value(&bits, 1, 1);
    inflate_test_value(&bits, 1, 2);
    inflate_test_code(&bits, 0, 7);
    static const struct
    {
        uint8_t method;
        bool dictionary;
        bool broken;
    } headers[] = {
        { 0x77, false, false },
        { 0x88, false, false },
        { 0x78, true, false },
        { 0x78, false, true },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(headers); i += 1)
    {
        uint8_t wrong[2];
        inflate_test_header(wrong, headers[i].method, headers[i].dictionary);
        wrong[1] ^= headers[i].broken ? 1 : 0;
        length = inflate_test_wrap(stream, wrong, bits.bytes, (bits.count + 7) / 8, output, 0);
        inflate_test_expect(tally, inflate_test_guarded(stream, length, 0, 0, &same) == INFLATE_ERROR_HEADER, "a header refused", i);
    }
    inflate_test_expect(tally, inflate_test_guarded(stream, 5, 0, 0, &same) == INFLATE_ERROR_TRUNCATED, "a stream shorter than its header and checksum", 5);
}

// A generated input of `size` bytes of `kind`: random bytes, text of DWARF's strings, runs of a byte, or a mix.
static void inflate_test_input(uint8_t* bytes, uint64_t size, uint32_t kind, uint64_t* state)
{
    static const char* const words[] = { "DW_TAG_subprogram", "DW_AT_name", "int", "char", "unsigned long", "/usr/include/stdio.h", "main", "\0", "\x01\x02\x03", "buster" };
    uint64_t at = 0;
    while (at < size)
    {
        const uint32_t part = kind == 3 ? (uint32_t)((at / 4096) % 3) : kind;
        if (part == 0)
        {
            bytes[at] = (uint8_t)inflate_test_next(state);
            at += 1;
        }
        else if (part == 1)
        {
            const char* word = words[inflate_test_next(state) % BUSTER_ARRAY_LENGTH(words)];
            const uint64_t length = word[0] == 0 ? 1 : strlen(word);
            for (uint64_t i = 0; i < length && at < size; i += 1)
            {
                bytes[at] = (uint8_t)word[i];
                at += 1;
            }
        }
        else
        {
            const uint8_t value = (uint8_t)(inflate_test_next(state) % 3);
            const uint64_t run = 1 + inflate_test_next(state) % 300;
            for (uint64_t i = 0; i < run && at < size; i += 1)
            {
                bytes[at] = value;
                at += 1;
            }
        }
    }
}

// gzip's DEFLATE data of `input` at `level` as a zlib stream into `stream`; its length, or 0 when gzip didn't make it.
static uint64_t inflate_test_gzip(const char* directory, const uint8_t* input, uint64_t size, uint32_t level, uint8_t* stream, uint64_t capacity, Arena* arena)
{
    char path[4096];
    char compressed[4096 + 8];
    char level_option[8];
    snprintf(path, sizeof(path), "%s/input", directory);
    snprintf(compressed, sizeof(compressed), "%s/input.gz", directory);
    snprintf(level_option, sizeof(level_option), "-%u", level);
    uint64_t result = 0;
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool written = fd != -1 && (size == 0 || write(fd, input, size) == (ssize_t)size);
    if (fd != -1)
    {
        close(fd);
    }
    char* arguments[] = {
        (char*)"gzip",
        (char*)"-c",
        (char*)"-n",
        level_option,
        path,
        0,
    };
    const uint64_t mark = arena->length;
    if (written && object_test_run(arguments, compressed, "/dev/null") == 0)
    {
        const String8 file = object_test_read_file(arena, compressed);
        const uint8_t* bytes = (const uint8_t*)file.pointer;
        // The header: the magic, DEFLATE, no flags (-n leaves out the name); the trailer: CRC-32 and the size.
        const bool plain = file.pointer != 0 && file.length >= 18 && bytes[0] == 0x1f && bytes[1] == 0x8b && bytes[2] == 8 && bytes[3] == 0;
        if (plain && file.length - 18 + 6 <= capacity)
        {
            uint8_t header[2];
            inflate_test_header(header, 0x78, false);
            result = inflate_test_wrap(stream, header, bytes + 10, file.length - 18, input, size);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static void inflate_test_streams(InflateTestTally* tally, const char* directory, uint64_t seed, Arena* arena)
{
    static const uint64_t sizes[] = { 0, 1, 2, 15, 16, 17, 255, 256, 257, 4095, 4096, 4097, 65535, 65536, 65537, 1u << 20 };
    static const uint32_t levels[] = { 1, 6, 9 };
    const uint64_t largest = 1u << 20;
    uint8_t* input = arena_push(arena, largest, 64);
    const uint64_t capacity = largest + largest / 8 + 4096;
    uint8_t* stream = arena_push(arena, capacity, 64);
    uint64_t state = seed;
    uint64_t made = 0;
    uint64_t cut = 0;
    uint64_t changed = 0;
    uint64_t accepted = 0;
    for (uint32_t s = 0; input != 0 && stream != 0 && s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
    {
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            inflate_test_input(input, sizes[s], kind, &state);
            for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(levels); l += 1)
            {
                const uint64_t length = inflate_test_gzip(directory, input, sizes[s], levels[l], stream, capacity, arena);
                const uint64_t detail = sizes[s] * 100 + kind * 10 + levels[l];
                inflate_test_expect(tally, length != 0, "gzip's stream made", detail);
                bool same = false;
                if (length != 0)
                {
                    made += 1;
                    inflate_test_expect(tally, inflate_test_guarded(stream, length, sizes[s], input, &same) == INFLATE_ERROR_NONE && same, "gzip's stream decompressed", detail);
                    inflate_test_expect(tally, sizes[s] == 0 || inflate_test_guarded(stream, length, sizes[s] - 1, 0, &same) == INFLATE_ERROR_SIZE, "an output a byte short", detail);
                    inflate_test_expect(tally, inflate_test_guarded(stream, length, sizes[s] + 1, 0, &same) == INFLATE_ERROR_SIZE, "an output a byte long", detail);
                }
                for (uint64_t at = 0; length != 0 && length <= 4096 && at < length; at += 1)
                {
                    inflate_test_expect(tally, inflate_test_guarded(stream, at, sizes[s], input, &same) != INFLATE_ERROR_NONE, "a cut stream refused", detail * 10000 + at);
                    cut += 1;
                    const uint8_t original = stream[at];
                    const uint8_t values[3] = { (uint8_t)(original ^ 1), (uint8_t)(original ^ 0x80), (uint8_t)inflate_test_next(&state) };
                    for (uint32_t v = 0; v < 3; v += 1)
                    {
                        stream[at] = values[v];
                        const InflateError error = inflate_test_guarded(stream, length, sizes[s], input, &same);
                        inflate_test_expect(tally, error != INFLATE_ERROR_NONE || same, "a changed byte accepted gives the input back", detail * 10000 + at);
                        accepted += error == INFLATE_ERROR_NONE ? 1 : 0;
                        changed += 1;
                    }
                    stream[at] = original;
                }
            }
        }
    }
    printf("  %llu of gzip's streams, %llu cuts, %llu changed bytes (%llu accepted, all giving the input back)\n", (unsigned long long)made, (unsigned long long)cut,
           (unsigned long long)changed, (unsigned long long)accepted);
}

static int run_inflate_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    bool keep = false;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            seed = strtoull(argv[i + 1], 0, 0);
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && has_value)
        {
            base = argv[i + 1];
            i += 1;
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
    char directory[4096];
    snprintf(directory, sizeof(directory), "%s/buster-inflate-XXXXXX", base);
    Arena arena = arena_reserve(1ull << 32);
    InflateTestTally tally = { 0 };
    bool passed = valid;
    if (!valid)
    {
        printf("usage: test-inflate [--seed N] [--directory PATH] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("test-inflate: no room for the test's arena or directory\n");
        passed = false;
    }
    else
    {
        inflate_test_adler(&tally, seed);
        inflate_test_by_hand(&tally);
        inflate_test_streams(&tally, directory, seed, &arena);
        printf("inflate: %llu checks, %llu failures\n", (unsigned long long)tally.checks, (unsigned long long)tally.failures);
        passed = tally.failures == 0;
        char* remove[] = {
            (char*)"rm",
            (char*)"-rf",
            directory,
            0,
        };
        if (!keep)
        {
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("test-inflate: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}
