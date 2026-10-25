// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// zlib streams (RFC 1950) of DEFLATE data (RFC 1951), decompressed into a buffer of the size their container gives:
// the SHF_COMPRESSED sections of type ELFCOMPRESS_ZLIB that GNU as writes for -g by default (binutils'
// --compress-debug-sections=zlib) and that GCC's and clang's -gz ask for. Written from the RFCs.
//
// A stream is one chain of dependent symbols, each starting where the one before ended, so a stream decompresses in
// order: no vector of a stream's symbols exists to decode at once. Streams don't depend on each other, and a caller with
// several spreads them over its lanes (foreign_debug.c). Within a stream, a 64-bit bit buffer refilled eight bytes at a
// time without a branch while eight remain (the bits above its count are zero or the next byte's own, so that a refill
// may OR a byte in twice); Huffman codes decoded through a table of their first BUSTER_INFLATE_TABLE_BITS bits whose
// entries hold what a symbol means (a literal's byte, or a length's or a distance's base and extra bits, the code's
// length in the low bits that a shift by the entry takes), a longer code (rare: lengths past 11 bits) through its
// canonical counts, out of line; and a fast loop, its state in registers, while a refill takes eight bytes and the
// output has room for two literals, a longest match and a copy's overshoot: each step takes up to three literals or the
// literals before a match and the match, checking neither the buffer's count nor the output's room, then a careful loop
// for the rest. Matches are copied 16 or 8 bytes at a time where the distance and the room after them allow. The
// Adler-32 is the data-parallel part, 64 bytes at a time (inflate_adler32_vector, with a twin a byte at a time). Every
// read stays within the input and every write within the output. A stream that doesn't decode into exactly the output's
// size, ends before its input's last byte, or whose Adler-32 differs is an error; a preset dictionary (FDICT) is
// refused.

#define BUSTER_INFLATE_TABLE_BITS 11
#define BUSTER_INFLATE_MAX_BITS 15
// A table entry's kind (InflateCode). A table's empty entry, 0, is of kind INVALID, so that one compare tells a literal
// (the fast loop's commonest case) from every other entry.
#define BUSTER_INFLATE_KIND_INVALID 0u
#define BUSTER_INFLATE_KIND_LITERAL 1u
#define BUSTER_INFLATE_KIND_BASE 2u
#define BUSTER_INFLATE_KIND_END 3u
// An entry's kind in place (its bits 12 and 13).
#define BUSTER_INFLATE_KIND_MASK 0x3000u

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

// What a code's symbols mean: the code length code's are themselves, the literal/length code's literals, the end of a
// block and lengths, and the distance code's distances.
typedef enum InflateAlphabet
{
    INFLATE_ALPHABET_PLAIN,
    INFLATE_ALPHABET_LITERALS,
    INFLATE_ALPHABET_DISTANCES,
} InflateAlphabet;

// A Huffman code: `table` maps a code's first BUSTER_INFLATE_TABLE_BITS bits, as the stream holds them, to its entry
// (inflate_entry), or 0 for a longer code (or none); `counts` and `symbols` are its canonical form, the codes of each
// length and the symbols in the codes' order, which a longer code's decoding reads.
typedef struct InflateCode InflateCode;
struct InflateCode
{
    uint32_t table[1u << BUSTER_INFLATE_TABLE_BITS];
    uint16_t counts[BUSTER_INFLATE_MAX_BITS + 1];
    uint16_t symbols[288];
    InflateAlphabet alphabet;
};

static const uint16_t inflate_length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t inflate_length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t inflate_distance_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t inflate_distance_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
// The order a dynamic block gives the code length code's lengths in.
static const uint8_t inflate_length_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

// The entry of symbol `symbol` of `alphabet`, whose code is `length` bits long: the code's length in bits 0 to 3 (4 and
// 5 are zero, so that a shift by the entry's low six bits, which x86's shifts take of a register, is one by the
// length), the symbol's extra bits in 8 to 11, its kind in 12 and 13 (a literal's or a plain code's symbol, a base, the
// end of a block, or a symbol the alphabet hasn't: 286 and 287, distances 30 and 31) and its value from bit 16 (the
// literal's byte or the plain symbol, or the length's or the distance's base).
static uint32_t inflate_entry(InflateAlphabet alphabet, uint32_t symbol, uint32_t length)
{
    const bool literal = alphabet == INFLATE_ALPHABET_PLAIN || (alphabet == INFLATE_ALPHABET_LITERALS && symbol < 256);
    const bool end = alphabet == INFLATE_ALPHABET_LITERALS && symbol == 256;
    const bool length_symbol = alphabet == INFLATE_ALPHABET_LITERALS && symbol > 256 && symbol - 257 < 29;
    const bool distance = alphabet == INFLATE_ALPHABET_DISTANCES && symbol < 30;
    const uint32_t kind = literal ? BUSTER_INFLATE_KIND_LITERAL : end ? BUSTER_INFLATE_KIND_END : length_symbol || distance ? BUSTER_INFLATE_KIND_BASE : BUSTER_INFLATE_KIND_INVALID;
    const uint32_t extra = length_symbol ? inflate_length_extra[symbol - 257] : distance ? inflate_distance_extra[symbol] : 0;
    const uint32_t value = literal ? symbol : length_symbol ? inflate_length_base[symbol - 257] : distance ? inflate_distance_base[symbol] : 0;
    return value << 16 | kind << 12 | extra << 8 | length;
}

// Fills the bit buffer to at least 56 bits, or with what the input has left.
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

// A code of `alphabet` from `lengths`, `count` of them (0 for a symbol without a code). False for an oversubscribed one;
// an incomplete code is a code, whose missing codes fail to decode.
static bool inflate_build(InflateCode* code, InflateAlphabet alphabet, const uint8_t* lengths, uint32_t count)
{
    code->alphabet = alphabet;
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
            const uint32_t entry = inflate_entry(alphabet, code->symbols[index], length);
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

// A code past the table's bits (or none), through the canonical form, a bit at a time: its symbol's entry
// (inflate_entry), whose low four bits are how many of `bits` it takes, or 0 when the buffer's `count` bits hold no code
// of it there. By value, so that the callers' buffers stay in registers; cold: few streams have codes longer than
// BUSTER_INFLATE_TABLE_BITS, and those are their rarest symbols.
static __attribute__((noinline, cold)) uint32_t inflate_decode_long(uint64_t bits, uint32_t count, const InflateCode* code)
{
    // A code of `length` bits is among that length's codes when it is less than their end.
    uint32_t result = 0;
    int32_t value = 0;
    int32_t first = 0;
    int32_t index = 0;
    for (uint32_t length = 1; result == 0 && length <= BUSTER_INFLATE_MAX_BITS && length <= count; length += 1)
    {
        value |= (int32_t)((bits >> (length - 1)) & 1);
        const int32_t codes = code->counts[length];
        if (value - codes < first)
        {
            result = inflate_entry(code->alphabet, code->symbols[index + (value - first)], length);
        }
        index += codes;
        first = (first + codes) << 1;
        value <<= 1;
    }
    return result;
}

// The next code of `code`, consumed, as its symbol's entry (inflate_entry), or 0 when the input has no code of it there
// (or not enough bits).
static uint32_t inflate_decode(InflateBits* in, const InflateCode* code)
{
    if (in->count < BUSTER_INFLATE_MAX_BITS)
    {
        inflate_refill(in);
    }
    const uint32_t entry = code->table[in->bits & ((1u << BUSTER_INFLATE_TABLE_BITS) - 1)];
    const uint32_t result = entry != 0 && (entry & 15) <= in->count ? entry : inflate_decode_long(in->bits, in->count, code);
    in->bits >>= result & 63;
    in->count -= result & 15;
    return result;
}

// Copies a match of `length` bytes from `distance` bytes back to `output + at`: 16 or 8 bytes at a time where the match
// doesn't overlap within a step and the output has room for a step's overshoot, which later bytes overwrite.
static void inflate_copy(uint8_t* output, uint64_t at, uint64_t size, uint32_t length, uint32_t distance)
{
    uint8_t* to = output + at;
    const uint8_t* from = to - distance;
    const uint32_t step = distance >= 16 && at + length + 16 <= size ? 16 : distance >= 8 && at + length + 8 <= size ? 8 : 1;
    if (step == 16)
    {
        for (uint32_t done = 0; done < length; done += 16)
        {
            memcpy(to + done, from + done, 16);
        }
    }
    else if (step == 8)
    {
        for (uint32_t done = 0; done < length; done += 8)
        {
            memcpy(to + done, from + done, 8);
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
    inflate_build(literals, INFLATE_ALPHABET_LITERALS, lengths, 288);
    memset(lengths, 5, 30);
    inflate_build(distances, INFLATE_ALPHABET_DISTANCES, lengths, 30);
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
    error = error == INFLATE_ERROR_NONE && !inflate_build(code_lengths, INFLATE_ALPHABET_PLAIN, ordered, 19) ? INFLATE_ERROR_DATA : error;
    uint8_t lengths[286 + 30] = { 0 };
    // The literal/length and distance codes' lengths, one run: a repeat may cross from one into the other.
    uint32_t filled = 0;
    while (error == INFLATE_ERROR_NONE && filled < literal_count + distance_count)
    {
        const uint32_t entry = inflate_decode(in, code_lengths);
        const uint32_t symbol = entry >> 16;
        uint32_t extra = 0;
        uint32_t repeat = 0;
        uint8_t value = 0;
        if (entry == 0)
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
    error = error == INFLATE_ERROR_NONE && !inflate_build(literals, INFLATE_ALPHABET_LITERALS, lengths, literal_count) ? INFLATE_ERROR_DATA : error;
    error = error == INFLATE_ERROR_NONE && !inflate_build(distances, INFLATE_ALPHABET_DISTANCES, lengths + literal_count, distance_count) ? INFLATE_ERROR_DATA : error;
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
    // The fast loop, its state in registers: each step refills the buffer to 56 bits or more without a branch, then
    // takes up to three literals (11 bits each at most, from the table) or the literals before a match and the match;
    // a match's codes and extra bits take 48 bits at most, which a second refill gives when the literals left fewer.
    // The output has room for two literals, a longest match and a copy's overshoot; a code past the table's bits goes
    // through inflate_decode_long.
    const uint64_t mask = (1u << BUSTER_INFLATE_TABLE_BITS) - 1;
    uint64_t bits = in->bits;
    uint32_t count = in->count;
    const uint8_t* next = in->next;
    const uint8_t* end = in->end;
    uint8_t* out = output + *at;
    uint8_t* const limit = output + size;
    while (error == INFLATE_ERROR_NONE && !ended && end - next >= 8 && limit - out >= 2 + 258 + 16)
    {
        uint64_t word = 0;
        memcpy(&word, next, sizeof(word));
        bits |= word << count;
        next += (63 - count) >> 3;
        count |= 56;
        uint32_t entry = literals->table[bits & mask];
        uint32_t literal_count = 0;
        if ((entry & BUSTER_INFLATE_KIND_MASK) == BUSTER_INFLATE_KIND_LITERAL << 12)
        {
            out[0] = (uint8_t)(entry >> 16);
            bits >>= entry & 63;
            count -= entry & 15;
            entry = literals->table[bits & mask];
            literal_count = 1;
            if ((entry & BUSTER_INFLATE_KIND_MASK) == BUSTER_INFLATE_KIND_LITERAL << 12)
            {
                out[1] = (uint8_t)(entry >> 16);
                bits >>= entry & 63;
                count -= entry & 15;
                entry = literals->table[bits & mask];
                literal_count = 2;
                if ((entry & BUSTER_INFLATE_KIND_MASK) == BUSTER_INFLATE_KIND_LITERAL << 12)
                {
                    out[2] = (uint8_t)(entry >> 16);
                    bits >>= entry & 63;
                    count -= entry & 15;
                    literal_count = 3;
                }
            }
        }
        out += literal_count;
        if (literal_count < 3 && count < 48 && end - next >= 8)
        {
            memcpy(&word, next, sizeof(word));
            bits |= word << count;
            next += (63 - count) >> 3;
            count |= 56;
        }
        if (literal_count == 3 || count < 48)
        {
            // Three literals, or a match without the bits it may take in the buffer: the next step (or the careful
            // loop) goes on from here.
        }
        else
        {
            entry = entry != 0 ? entry : inflate_decode_long(bits, count, literals);
            bits >>= entry & 63;
            count -= entry & 15;
            const uint32_t kind = entry & BUSTER_INFLATE_KIND_MASK;
            if (kind == BUSTER_INFLATE_KIND_LITERAL << 12)
            {
                *out = (uint8_t)(entry >> 16);
                out += 1;
            }
            else if (kind == BUSTER_INFLATE_KIND_BASE << 12)
            {
                const uint32_t length_extra = (entry >> 8) & 15;
                const uint32_t length = (entry >> 16) + (uint32_t)(bits & ((1u << length_extra) - 1));
                bits >>= length_extra;
                count -= length_extra;
                uint32_t place = distances->table[bits & mask];
                place = place != 0 ? place : inflate_decode_long(bits, count, distances);
                bits >>= place & 63;
                count -= place & 15;
                const uint32_t distance_extra = (place >> 8) & 15;
                const uint32_t distance = (place >> 16) + (uint32_t)(bits & ((1u << distance_extra) - 1));
                bits >>= distance_extra;
                count -= distance_extra;
                const uint64_t done = (uint64_t)(out - output);
                error = (place & BUSTER_INFLATE_KIND_MASK) != BUSTER_INFLATE_KIND_BASE << 12 || distance > done ? INFLATE_ERROR_DATA : INFLATE_ERROR_NONE;
                if (error == INFLATE_ERROR_NONE)
                {
                    inflate_copy(output, done, size, length, distance);
                    out += length;
                }
            }
            else
            {
                ended = kind == BUSTER_INFLATE_KIND_END << 12;
                error = ended ? INFLATE_ERROR_NONE : INFLATE_ERROR_DATA;
            }
        }
    }
    in->bits = bits;
    in->count = count;
    in->next = next;
    *at = (uint64_t)(out - output);
    // The careful loop: each code's bits and the output's room checked.
    while (error == INFLATE_ERROR_NONE && !ended)
    {
        const uint32_t entry = inflate_decode(in, literals);
        const uint32_t kind = (entry >> 12) & 3;
        if (kind == BUSTER_INFLATE_KIND_INVALID)
        {
            error = INFLATE_ERROR_DATA;
        }
        else if (kind == BUSTER_INFLATE_KIND_LITERAL)
        {
            error = *at < size ? INFLATE_ERROR_NONE : INFLATE_ERROR_SIZE;
            if (error == INFLATE_ERROR_NONE)
            {
                output[*at] = (uint8_t)(entry >> 16);
                *at += 1;
            }
        }
        else if (kind == BUSTER_INFLATE_KIND_END)
        {
            ended = true;
        }
        else
        {
            uint32_t length_extra = 0;
            uint32_t distance_extra = 0;
            error = !inflate_take(in, (entry >> 8) & 15, &length_extra) ? INFLATE_ERROR_TRUNCATED : INFLATE_ERROR_NONE;
            const uint32_t place = error == INFLATE_ERROR_NONE ? inflate_decode(in, distances) : 0;
            error = error == INFLATE_ERROR_NONE && (place == 0 || ((place >> 12) & 3) != BUSTER_INFLATE_KIND_BASE) ? INFLATE_ERROR_DATA : error;
            error = error == INFLATE_ERROR_NONE && !inflate_take(in, (place >> 8) & 15, &distance_extra) ? INFLATE_ERROR_TRUNCATED : error;
            const uint32_t length = (entry >> 16) + length_extra;
            const uint32_t distance = (place >> 16) + distance_extra;
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

// The Adler-32 of `bytes`, a byte at a time: inflate_adler32_vector's twin.
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

// The Adler-32 of `bytes`, 64 bytes at a time. Over n bytes x_0 .. x_n-1 from a and b, a gains the bytes' sum and b
// gains n times a and each byte times n - t, its place from the end: for chunk j of k, 64 (k - 1 - j) times the chunk's
// sum plus the sum of its byte i times 64 - i. Each chunk's sum comes from vpsadbw (eight bytes to a quadword lane), its
// weighted sum from vpdpbusd (four bytes times their weights to a doubleword lane); and the sums of the chunks after
// each, from adding the chunks' running sum before each chunk. A block of 4,096 chunks keeps a doubleword lane's
// weighted sums under 2^28, then the sums fold into a and b modulo 65,521; the last bytes go a byte at a time.
static uint32_t inflate_adler32_vector(const uint8_t* bytes, uint64_t length)
{
    const __m512i weights = _mm512_set_epi8(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                            33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64);
    uint64_t a = 1;
    uint64_t b = 0;
    uint64_t at = 0;
    while (length - at >= 64)
    {
        const uint64_t chunks = (length - at) / 64 < 4096 ? (length - at) / 64 : 4096;
        __m512i sums = _mm512_setzero_si512();
        __m512i before = _mm512_setzero_si512();
        __m512i weighted = _mm512_setzero_si512();
        for (uint64_t chunk = 0; chunk < chunks; chunk += 1)
        {
            const __m512i data = _mm512_loadu_si512(bytes + at + chunk * 64);
            before = _mm512_add_epi64(before, sums);
            sums = _mm512_add_epi64(sums, _mm512_sad_epu8(data, _mm512_setzero_si512()));
            weighted = _mm512_dpbusd_epi32(weighted, data, weights);
        }
        const uint64_t sum = (uint64_t)_mm512_reduce_add_epi64(sums);
        const uint64_t prefixes = (uint64_t)_mm512_reduce_add_epi64(before);
        const uint64_t weight = (uint64_t)(uint32_t)_mm512_reduce_add_epi32(weighted);
        b = (b + chunks * 64 * a + 64 * prefixes + weight) % 65521;
        a = (a + sum) % 65521;
        at += chunks * 64;
    }
    for (; at < length; at += 1)
    {
        a += bytes[at];
        b += a;
    }
    a %= 65521;
    b %= 65521;
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
    error = error == INFLATE_ERROR_NONE && inflate_adler32_vector(output, size) != expected ? INFLATE_ERROR_CHECKSUM : error;
    return error;
}
