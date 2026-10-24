// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// zlib streams (RFC 1950) of DEFLATE data (RFC 1951), decompressed into a buffer of the size their container gives:
// the SHF_COMPRESSED sections of type ELFCOMPRESS_ZLIB that GNU as writes for -g by default (binutils'
// --compress-debug-sections=zlib) and that GCC's and clang's -gz ask for. Written from the RFCs.
//
// A stream is one chain of dependent symbols, each starting where the one before ended, so a stream decompresses in
// order; streams don't depend on each other, and a caller with several spreads them over its lanes. Within a stream:
// a 64-bit bit buffer refilled eight bytes at a time while eight remain (the bits above its count are zero or the next
// byte's own, so that a refill may OR a byte in twice), Huffman codes decoded through a table of their first
// BUSTER_INFLATE_TABLE_BITS bits, a longer code (rare: lengths past 10 bits) through its canonical counts, and
// matches copied 16 bytes at a time where the distance and the room after them allow. Every read stays within the
// input and every write within the output. A stream that doesn't decode into exactly the output's size, ends before
// its input's last byte, or whose Adler-32 differs is an error; a preset dictionary (FDICT) is refused.

#define BUSTER_INFLATE_TABLE_BITS 10
#define BUSTER_INFLATE_MAX_BITS 15

typedef enum InflateError
{
    INFLATE_ERROR_NONE,
    // The zlib header: not DEFLATE, a window past 32 KB, a check that fails, or a preset dictionary.
    INFLATE_ERROR_HEADER,
    // The DEFLATE data: a block type 3, a stored block's lengths, a code that is oversubscribed or that the data uses
    // and doesn't have, code lengths past their count, or a distance before the output's start.
    INFLATE_ERROR_DATA,
    // The data needs bits past the input's end.
    INFLATE_ERROR_TRUNCATED,
    // The data decodes into more or fewer bytes than the output has, or ends before the input's last byte.
    INFLATE_ERROR_SIZE,
    INFLATE_ERROR_CHECKSUM,
} InflateError;

typedef struct InflateBits InflateBits;
struct InflateBits
{
    const uint8_t* next;
    const uint8_t* end;
    uint64_t bits;
    uint32_t count;
};

// A Huffman code: `table` maps a code's first BUSTER_INFLATE_TABLE_BITS bits, as the stream holds them, to its symbol
// << 4 | its length, or 0 for a longer code (or none); `counts` and `symbols` are its canonical form, the codes of
// each length and the symbols in the codes' order, which a longer code's decoding reads.
typedef struct InflateCode InflateCode;
struct InflateCode
{
    uint16_t table[1u << BUSTER_INFLATE_TABLE_BITS];
    uint16_t counts[BUSTER_INFLATE_MAX_BITS + 1];
    uint16_t symbols[288];
};

static const uint16_t inflate_length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t inflate_length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t inflate_distance_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t inflate_distance_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
// The order a dynamic block gives the code length code's lengths in.
static const uint8_t inflate_length_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

// Fills the bit buffer to at least 57 bits, or with what the input has left.
static void inflate_refill(InflateBits* in)
{
    if (in->end - in->next >= 8)
    {
        uint64_t word = 0;
        memcpy(&word, in->next, sizeof(word));
        in->bits |= word << in->count;
        in->next += (63 - in->count) >> 3;
        in->count |= 56;
    }
    else
    {
        while (in->count <= 56 && in->next < in->end)
        {
            in->bits |= (uint64_t)*in->next << in->count;
            in->next += 1;
            in->count += 8;
        }
    }
}

// The next `n` bits (at most 32), consumed; false when the input doesn't have them.
static bool inflate_take(InflateBits* in, uint32_t n, uint32_t* value)
{
    if (in->count < n)
    {
        inflate_refill(in);
    }
    const bool present = in->count >= n;
    *value = present ? (uint32_t)(in->bits & ((1ull << n) - 1)) : 0;
    in->bits = present ? in->bits >> n : in->bits;
    in->count = present ? in->count - n : in->count;
    return present;
}

// A code from `lengths`, `count` of them (0 for a symbol without a code). False for an oversubscribed one; an
// incomplete code is a code, whose missing codes fail to decode.
static bool inflate_build(InflateCode* code, const uint8_t* lengths, uint32_t count)
{
    memset(code->counts, 0, sizeof(code->counts));
    for (uint32_t i = 0; i < count; i += 1)
    {
        code->counts[lengths[i]] += 1;
    }
    code->counts[0] = 0;
    // The codes each length leaves free, from one of no length: none may go below zero.
    int32_t left = 1;
    bool valid = true;
    for (uint32_t length = 1; length <= BUSTER_INFLATE_MAX_BITS; length += 1)
    {
        left = left * 2 - code->counts[length];
        valid = valid && left >= 0;
        left = left < 0 ? 0 : left;
    }
    uint16_t offsets[BUSTER_INFLATE_MAX_BITS + 2] = { 0 };
    for (uint32_t length = 1; length <= BUSTER_INFLATE_MAX_BITS; length += 1)
    {
        offsets[length + 1] = (uint16_t)(offsets[length] + code->counts[length]);
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        if (lengths[i] != 0)
        {
            code->symbols[offsets[lengths[i]]] = (uint16_t)i;
            offsets[lengths[i]] += 1;
        }
    }
    memset(code->table, 0, sizeof(code->table));
    // Canonical codes, in order: each length's start is the last's end, doubled. The stream holds a code from its
    // first bit, the table's index from its lowest: each short code fills every index its bits reversed start.
    uint32_t value = 0;
    uint32_t index = 0;
    for (uint32_t length = 1; valid && length <= BUSTER_INFLATE_TABLE_BITS; length += 1)
    {
        for (uint32_t k = 0; k < code->counts[length]; k += 1)
        {
            uint32_t reversed = 0;
            for (uint32_t bit = 0; bit < length; bit += 1)
            {
                reversed |= ((value >> bit) & 1) << (length - 1 - bit);
            }
            const uint16_t entry = (uint16_t)(code->symbols[index] << 4 | length);
            for (uint32_t fill = reversed; fill < (1u << BUSTER_INFLATE_TABLE_BITS); fill += 1u << length)
            {
                code->table[fill] = entry;
            }
            value += 1;
            index += 1;
        }
        value <<= 1;
    }
    return valid;
}

// The next symbol of `code`, consumed, or -1 when the input has no code of it there (or not enough bits).
static int32_t inflate_decode(InflateBits* in, const InflateCode* code)
{
    if (in->count < BUSTER_INFLATE_MAX_BITS)
    {
        inflate_refill(in);
    }
    const uint16_t entry = code->table[in->bits & ((1u << BUSTER_INFLATE_TABLE_BITS) - 1)];
    const uint32_t length = entry & 15;
    int32_t result = -1;
    if (entry != 0 && length <= in->count)
    {
        result = entry >> 4;
        in->bits >>= length;
        in->count -= length;
    }
    else
    {
        // The canonical decoding, a bit at a time: a code of `length` bits is among that length's codes when it is
        // less than their end.
        int32_t value = 0;
        int32_t first = 0;
        int32_t index = 0;
        for (uint32_t bits = 1; result < 0 && bits <= BUSTER_INFLATE_MAX_BITS && bits <= in->count; bits += 1)
        {
            value |= (int32_t)((in->bits >> (bits - 1)) & 1);
            const int32_t count = code->counts[bits];
            if (value - count < first)
            {
                result = code->symbols[index + (value - first)];
                in->bits >>= bits;
                in->count -= bits;
            }
            index += count;
            first = (first + count) << 1;
            value <<= 1;
        }
    }
    return result;
}

// Copies a match of `length` bytes from `distance` bytes back to `output + at`: 16 bytes at a time where the match
// doesn't overlap within a step and the output has room for a step's overshoot, which later bytes overwrite.
static void inflate_copy(uint8_t* output, uint64_t at, uint64_t size, uint32_t length, uint32_t distance)
{
    uint8_t* to = output + at;
    const uint8_t* from = to - distance;
    if (distance >= 16 && at + length + 16 <= size)
    {
        for (uint32_t done = 0; done < length; done += 16)
        {
            memcpy(to + done, from + done, 16);
        }
    }
    else
    {
        for (uint32_t done = 0; done < length; done += 1)
        {
            to[done] = from[done];
        }
    }
}

// The fixed codes of a block of type 1.
static void inflate_fixed(InflateCode* literals, InflateCode* distances)
{
    uint8_t lengths[288];
    memset(lengths, 8, 144);
    memset(lengths + 144, 9, 112);
    memset(lengths + 256, 7, 24);
    memset(lengths + 280, 8, 8);
    inflate_build(literals, lengths, 288);
    memset(lengths, 5, 30);
    inflate_build(distances, lengths, 30);
}

// A dynamic block's codes, from its header.
static InflateError inflate_dynamic(InflateBits* in, InflateCode* literals, InflateCode* distances)
{
    uint32_t literal_count = 0;
    uint32_t distance_count = 0;
    uint32_t length_count = 0;
    InflateError error = inflate_take(in, 5, &literal_count) && inflate_take(in, 5, &distance_count) && inflate_take(in, 4, &length_count) ? INFLATE_ERROR_NONE :
                                                                                                                                             INFLATE_ERROR_TRUNCATED;
    literal_count += 257;
    distance_count += 1;
    length_count += 4;
    error = error == INFLATE_ERROR_NONE && (literal_count > 286 || distance_count > 30) ? INFLATE_ERROR_DATA : error;
    uint8_t ordered[19] = { 0 };
    for (uint32_t i = 0; error == INFLATE_ERROR_NONE && i < length_count; i += 1)
    {
        uint32_t length = 0;
        error = inflate_take(in, 3, &length) ? INFLATE_ERROR_NONE : INFLATE_ERROR_TRUNCATED;
        ordered[inflate_length_order[i]] = (uint8_t)length;
    }
    InflateCode* code_lengths = literals;
    error = error == INFLATE_ERROR_NONE && !inflate_build(code_lengths, ordered, 19) ? INFLATE_ERROR_DATA : error;
    uint8_t lengths[286 + 30] = { 0 };
    // The literal/length and distance codes' lengths, one run: a repeat may cross from one into the other.
    uint32_t filled = 0;
    while (error == INFLATE_ERROR_NONE && filled < literal_count + distance_count)
    {
        const int32_t symbol = inflate_decode(in, code_lengths);
        uint32_t extra = 0;
        uint32_t repeat = 0;
        uint8_t value = 0;
        if (symbol < 0)
        {
            error = INFLATE_ERROR_DATA;
        }
        else if (symbol < 16)
        {
            repeat = 1;
            value = (uint8_t)symbol;
        }
        else if (symbol == 16)
        {
            error = filled == 0 ? INFLATE_ERROR_DATA : !inflate_take(in, 2, &extra) ? INFLATE_ERROR_TRUNCATED : INFLATE_ERROR_NONE;
            repeat = 3 + extra;
            value = filled != 0 ? lengths[filled - 1] : 0;
        }
        else
        {
            const bool short_run = symbol == 17;
            error = inflate_take(in, short_run ? 3 : 7, &extra) ? INFLATE_ERROR_NONE : INFLATE_ERROR_TRUNCATED;
            repeat = (short_run ? 3 : 11) + extra;
        }
        error = error == INFLATE_ERROR_NONE && filled + repeat > literal_count + distance_count ? INFLATE_ERROR_DATA : error;
        for (uint32_t r = 0; error == INFLATE_ERROR_NONE && r < repeat; r += 1)
        {
            lengths[filled] = value;
            filled += 1;
        }
    }
    // A block must be able to end.
    error = error == INFLATE_ERROR_NONE && lengths[256] == 0 ? INFLATE_ERROR_DATA : error;
    error = error == INFLATE_ERROR_NONE && !inflate_build(literals, lengths, literal_count) ? INFLATE_ERROR_DATA : error;
    error = error == INFLATE_ERROR_NONE && !inflate_build(distances, lengths + literal_count, distance_count) ? INFLATE_ERROR_DATA : error;
    return error;
}

// A stored block's bytes, after its 3 header bits.
static InflateError inflate_stored(InflateBits* in, uint8_t* output, uint64_t size, uint64_t* at)
{
    // To the next byte, then LEN and NLEN.
    uint32_t ignored = 0;
    uint32_t lengths = 0;
    InflateError error = inflate_take(in, in->count & 7, &ignored) && inflate_take(in, 32, &lengths) ? INFLATE_ERROR_NONE : INFLATE_ERROR_TRUNCATED;
    const uint32_t length = lengths & 0xffff;
    error = error == INFLATE_ERROR_NONE && (lengths >> 16) != (~length & 0xffff) ? INFLATE_ERROR_DATA : error;
    error = error == INFLATE_ERROR_NONE && length > size - *at ? INFLATE_ERROR_SIZE : error;
    // The whole bytes in the bit buffer first, then the input's own.
    uint32_t done = 0;
    while (error == INFLATE_ERROR_NONE && done < length && in->count >= 8)
    {
        output[*at + done] = (uint8_t)in->bits;
        in->bits >>= 8;
        in->count -= 8;
        done += 1;
    }
    const uint64_t rest = length - done;
    error = error == INFLATE_ERROR_NONE && rest > (uint64_t)(in->end - in->next) ? INFLATE_ERROR_TRUNCATED : error;
    if (error == INFLATE_ERROR_NONE && rest != 0)
    {
        // The buffer is empty here: whatever its bits above the count held was the next byte's own.
        memcpy(output + *at + done, in->next, rest);
        in->next += rest;
        in->bits = 0;
        in->count = 0;
    }
    *at += error == INFLATE_ERROR_NONE ? length : 0;
    return error;
}

// A block's symbols, through its codes, to its end.
static InflateError inflate_symbols(InflateBits* in, const InflateCode* literals, const InflateCode* distances, uint8_t* output, uint64_t size, uint64_t* at)
{
    InflateError error = INFLATE_ERROR_NONE;
    bool ended = false;
    while (error == INFLATE_ERROR_NONE && !ended)
    {
        const int32_t symbol = inflate_decode(in, literals);
        if (symbol < 0)
        {
            error = INFLATE_ERROR_DATA;
        }
        else if (symbol < 256)
        {
            error = *at < size ? INFLATE_ERROR_NONE : INFLATE_ERROR_SIZE;
            if (error == INFLATE_ERROR_NONE)
            {
                output[*at] = (uint8_t)symbol;
                *at += 1;
            }
        }
        else if (symbol == 256)
        {
            ended = true;
        }
        else
        {
            const uint32_t length_symbol = (uint32_t)symbol - 257;
            uint32_t length_extra = 0;
            uint32_t distance_extra = 0;
            error = length_symbol >= 29 ? INFLATE_ERROR_DATA : INFLATE_ERROR_NONE;
            error = error == INFLATE_ERROR_NONE && !inflate_take(in, inflate_length_extra[length_symbol], &length_extra) ? INFLATE_ERROR_TRUNCATED : error;
            const int32_t distance_symbol = error == INFLATE_ERROR_NONE ? inflate_decode(in, distances) : 0;
            error = error == INFLATE_ERROR_NONE && (distance_symbol < 0 || distance_symbol >= 30) ? INFLATE_ERROR_DATA : error;
            error = error == INFLATE_ERROR_NONE && !inflate_take(in, inflate_distance_extra[distance_symbol], &distance_extra) ? INFLATE_ERROR_TRUNCATED : error;
            const uint32_t length = error == INFLATE_ERROR_NONE ? inflate_length_base[length_symbol] + length_extra : 0;
            const uint32_t distance = error == INFLATE_ERROR_NONE ? inflate_distance_base[distance_symbol] + distance_extra : 0;
            error = error == INFLATE_ERROR_NONE && distance > *at ? INFLATE_ERROR_DATA : error;
            error = error == INFLATE_ERROR_NONE && length > size - *at ? INFLATE_ERROR_SIZE : error;
            if (error == INFLATE_ERROR_NONE)
            {
                inflate_copy(output, *at, size, length, distance);
                *at += length;
            }
        }
    }
    return error;
}

// The Adler-32 of `bytes`.
static uint32_t inflate_adler32(const uint8_t* bytes, uint64_t length)
{
    uint64_t a = 1;
    uint64_t b = 0;
    // 5552 bytes keep b from overflowing 32 bits; in 64 bits a run could be longer, but the modulus is cheap.
    for (uint64_t start = 0; start < length; start += 5552)
    {
        const uint64_t end = length - start > 5552 ? start + 5552 : length;
        for (uint64_t i = start; i < end; i += 1)
        {
            a += bytes[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (uint32_t)(b << 16 | a);
}

// Decompresses the zlib stream `input` into `output`, which must come out exactly `size` bytes long.
static InflateError inflate_zlib(const uint8_t* input, uint64_t input_length, uint8_t* output, uint64_t size)
{
    InflateError error = input_length >= 6 ? INFLATE_ERROR_NONE : INFLATE_ERROR_TRUNCATED;
    const uint32_t method = error == INFLATE_ERROR_NONE ? input[0] : 0;
    const uint32_t flags = error == INFLATE_ERROR_NONE ? input[1] : 0;
    error = error == INFLATE_ERROR_NONE && ((method & 15) != 8 || (method >> 4) > 7 || (method << 8 | flags) % 31 != 0 || (flags & 0x20) != 0) ? INFLATE_ERROR_HEADER :
                                                                                                                                                 error;
    InflateBits in = { .next = input + 2, .end = input + (input_length >= 6 ? input_length - 4 : 2) };
    uint64_t at = 0;
    bool last = false;
    // The two codes: a dynamic block's code length code is built in the literals' place, which it fills after.
    InflateCode literals;
    InflateCode distances;
    while (error == INFLATE_ERROR_NONE && !last)
    {
        uint32_t header = 0;
        error = inflate_take(&in, 3, &header) ? INFLATE_ERROR_NONE : INFLATE_ERROR_TRUNCATED;
        last = (header & 1) != 0;
        const uint32_t type = header >> 1;
        if (error == INFLATE_ERROR_NONE && type == 0)
        {
            error = inflate_stored(&in, output, size, &at);
        }
        else if (error == INFLATE_ERROR_NONE && type == 1)
        {
            inflate_fixed(&literals, &distances);
            error = inflate_symbols(&in, &literals, &distances, output, size, &at);
        }
        else if (error == INFLATE_ERROR_NONE && type == 2)
        {
            error = inflate_dynamic(&in, &literals, &distances);
            error = error == INFLATE_ERROR_NONE ? inflate_symbols(&in, &literals, &distances, output, size, &at) : error;
        }
        else if (error == INFLATE_ERROR_NONE)
        {
            error = INFLATE_ERROR_DATA;
        }
    }
    // The data ends in its last byte, which may have up to 7 bits left; then the Adler-32, big-endian.
    const uint64_t left = (uint64_t)(in.end - in.next) + in.count / 8;
    error = error == INFLATE_ERROR_NONE && (at != size || left != 0) ? INFLATE_ERROR_SIZE : error;
    const uint8_t* check = input + input_length - 4;
    const uint32_t expected = error == INFLATE_ERROR_NONE ? (uint32_t)check[0] << 24 | (uint32_t)check[1] << 16 | (uint32_t)check[2] << 8 | check[3] : 0;
    error = error == INFLATE_ERROR_NONE && inflate_adler32(output, size) != expected ? INFLATE_ERROR_CHECKSUM : error;
    return error;
}
