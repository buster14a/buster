// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU assembly frontend. asm_assemble consumes a bounded source slice and caller-owned arena, returning the shared
// Link representation. No C parser, subprocess, allocation per record, or modification of the final encoder record.
// Layout map: asm_lex; symbols and expressions; operands; directives/statements; matching; fragment layout; emission.
// The public boundary rejects unsupported syntax and validates records before trusted encoding. Layout only promotes
// branches: each can grow once, alignment is recomputed every round, and a final fixed-layout check covers rel8-only
// instructions. This terminates after at most branch_count + 1 rounds even when alignment absorbs earlier growth.

#define BUSTER_ASM_MAX_SOURCE (16u << 20)
#define BUSTER_ASM_MAX_SECTION (256u << 20)
#define BUSTER_ASM_EXPR_DEPTH 128
#define BUSTER_ASM_WORD 256
#define BUSTER_ASM_STRING 257

typedef struct AsmToken AsmToken;
struct AsmToken
{
    uint32_t start;
    uint32_t end;
    uint32_t kind;
    uint32_t reserved;
};

typedef struct AsmExpr AsmExpr;
struct AsmExpr
{
    __int128 value;
    uint32_t plus;
    uint32_t minus;
};

typedef struct AsmSymbol AsmSymbol;
struct AsmSymbol
{
    String8 name;
    AsmExpr size;
    __int128 constant;
    uint32_t fragment;
    uint32_t location;
    SymbolId link;
    uint8_t flags;
    bool defined;
    bool absolute;
    bool explicit_binding;
    bool has_size;
    bool internal;
};

typedef enum AsmFragmentKind
{
    ASM_LABEL,
    ASM_INSTRUCTION,
    ASM_VALUE,
    ASM_BYTES,
    ASM_FILL,
    ASM_ALIGN,
} AsmFragmentKind;

// How an ASM_ALIGN fragment pads: with its fill byte, or in code (no fill, or 0x90) with GNU as's NOPs, which start
// with a one-byte nop when the section's last statement was data.
typedef enum AsmPadding
{
    ASM_PADDING_FILL,
    ASM_PADDING_NOPS,
    ASM_PADDING_NOPS_AFTER_DATA,
} AsmPadding;

typedef struct AsmFragment AsmFragment;
struct AsmFragment
{
    AsmExpr expression;
    uint8_t* bytes;
    uint32_t offset;
    uint32_t size;
    uint32_t location;
    uint32_t instruction;
    uint32_t alignment;
    uint32_t maximum;
    uint8_t kind;
    uint8_t section;
    uint8_t fill;
    uint8_t padding;
};

typedef struct AsmInstruction AsmInstruction;
struct AsmInstruction
{
    X86MatchRequest request;
    AsmExpr expressions[4];
    X86Instruction record;
    uint32_t location;
    uint32_t fragment;
    bool branch;
    bool local_branch;
};

typedef struct AsmOptions AsmOptions;
struct AsmOptions
{
    bool vectorized;
    bool intel;
};

typedef struct AsmResult AsmResult;
struct AsmResult
{
    Link link;
    const char* error;
    uint32_t location;
    uint32_t rounds;
    uint32_t instructions;
    uint64_t lex_ns;
    uint64_t parse_ns;
    uint64_t match_ns;
    uint64_t layout_ns;
    uint64_t emit_ns;
};

typedef struct AsmParser AsmParser;
struct AsmParser
{
    String8 source;
    Arena* arena;
    const X86MatchIndex* index;
    AsmOptions options;
    AsmResult result;
    AsmToken* tokens;
    AsmSymbol* symbols;
    AsmFragment* fragments;
    AsmInstruction* instructions;
    uint32_t* symbol_slots;
    uint32_t symbol_mask;
    uint32_t token_count;
    uint32_t at;
    uint32_t capacity;
    uint32_t instruction_capacity;
    uint32_t symbol_count;
    uint32_t fragment_count;
    uint32_t instruction_count;
    uint32_t numeric[10];
    uint32_t section_size[BUSTER_LINK_CLASS_COUNT];
    uint8_t section_alignment[BUSTER_LINK_CLASS_COUNT];
    // Whether each section's last data or instruction statement was data, which code padding starts after with a nop.
    bool section_data[BUSTER_LINK_CLASS_COUNT];
    uint8_t section;
    bool prefix;
};

static uint64_t asm_clock(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static void asm_fail(AsmParser* parser, uint32_t location, const char* message)
{
    if (parser->result.error == 0)
    {
        parser->result.error = message;
        parser->result.location = location;
    }
}

static bool asm_word_byte(uint8_t byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';
}

static uint32_t asm_word_length(const uint8_t* source, uint32_t length, bool vectorized)
{
    uint32_t count = 0;
    if (vectorized)
    {
        while (count < length)
        {
            const uint32_t left = length - count;
            const uint32_t n = left < 64 ? left : 64;
            const uint64_t valid = n == 64 ? UINT64_MAX : (1ull << n) - 1;
            const __m512i bytes = _mm512_maskz_loadu_epi8(valid, source + count);
            const __m512i lower = _mm512_or_si512(bytes, _mm512_set1_epi8(32));
            uint64_t word = _mm512_cmple_epu8_mask(_mm512_sub_epi8(lower, _mm512_set1_epi8('a')), _mm512_set1_epi8(25));
            word |= _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('0')), _mm512_set1_epi8(9));
            word |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('_')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('.'));
            const uint64_t stop = valid & ~word;
            count += stop != 0 ? __builtin_ctzll(stop) : n;
            if (stop != 0)
            {
                break;
            }
        }
    }
    else
    {
        while (count < length && asm_word_byte(source[count]))
        {
            count += 1;
        }
    }
    return count;
}

static void asm_lex_scalar(AsmParser* parser)
{
    const uint8_t* bytes = (const uint8_t*)parser->source.pointer;
    const uint32_t length = parser->source.length;
    uint32_t at = 0;
    while (at < length && parser->result.error == 0)
    {
        const uint32_t start = at;
        const uint8_t byte = bytes[at++];
        uint16_t kind = byte;
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\v' || byte == '\f')
        {
            continue;
        }
        if (byte == '#' || (byte == '/' && at < length && bytes[at] == '/'))
        {
            while (at < length && bytes[at] != '\n')
            {
                at += 1;
            }
            continue;
        }
        if (byte == '/' && at < length && bytes[at] == '*')
        {
            at += 1;
            while (at + 1 < length && !(bytes[at] == '*' && bytes[at + 1] == '/'))
            {
                // A block comment is whitespace; newlines still terminate assembly statements.
                if (bytes[at] == '\n')
                {
                    parser->tokens[parser->token_count++] = (AsmToken){.start = at, .end = at + 1, .kind = '\n'};
                }
                at += 1;
            }
            if (at + 1 >= length)
            {
                asm_fail(parser, start, "unterminated block comment");
            }
            at += 2;
            continue;
        }
        if (asm_word_byte(byte))
        {
            at = start + asm_word_length(bytes + start, length - start, parser->options.vectorized);
            kind = BUSTER_ASM_WORD;
        }
        else if (byte == '"')
        {
            bool closed = false;
            while (at < length && !closed && bytes[at] != '\n')
            {
                if (bytes[at] == '\\' && at + 1 < length)
                {
                    at += 2;
                }
                else
                {
                    closed = bytes[at++] == '"';
                }
            }
            if (!closed)
            {
                asm_fail(parser, start, "unterminated string");
            }
            kind = BUSTER_ASM_STRING;
        }
        else if (byte == ';')
        {
            kind = '\n';
        }
        else if (byte == 0 || byte >= 128)
        {
            asm_fail(parser, start, "unsupported source byte");
        }
        parser->tokens[parser->token_count++] = (AsmToken){.start = start, .end = at, .kind = kind};
    }
    parser->tokens[parser->token_count] = (AsmToken){.start = length, .end = length, .kind = 0};
}

// Transpose compacted columns into the parser's fixed 16-byte records, four records per store. The lexer computes
// token boundaries over 64 characters, never walks each ordinary token or character. The parse view remains records
// because expression/diagnostic recovery needs all three columns of one token together.
static void asm_lex_store(AsmToken* output, const uint32_t* starts, const uint32_t* ends, const uint32_t* kinds, uint32_t count, uint32_t base)
{
    const __m512i offsets = _mm512_set1_epi32(base);
    for (uint32_t at = 0; at < count; at += 16)
    {
        const uint32_t left = count - at;
        const uint32_t n = left < 16 ? left : 16;
        const __mmask16 valid = (1u << n) - 1;
        const __m512i begin = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, starts + at), offsets);
        const __m512i end = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, ends + at), offsets);
        const __m512i kind = _mm512_maskz_loadu_epi32(valid, kinds + at);
        for (uint32_t group = 0; group < n; group += 4)
        {
            const __m512i places = _mm512_add_epi32(_mm512_setr_epi32(0, 16, 0, 0, 1, 17, 0, 0, 2, 18, 0, 0, 3, 19, 0, 0), _mm512_set1_epi32(group));
            const __m512i kind_places = _mm512_add_epi32(_mm512_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3), _mm512_set1_epi32(group));
            __m512i records = _mm512_maskz_permutex2var_epi32(0x3333, begin, places, end);
            records = _mm512_mask_mov_epi32(records, 0x4444, _mm512_permutexvar_epi32(kind_places, kind));
            const uint32_t records_left = n - group;
            const __mmask16 store = records_left >= 4 ? 0xffff : (1u << (records_left * 4)) - 1;
            _mm512_mask_storeu_epi32(output + at + group, store, records);
        }
    }
}

static void asm_lex_vector(AsmParser* parser)
{
    const uint8_t* source = (const uint8_t*)parser->source.pointer;
    const uint32_t length = parser->source.length;
    const __m512i iota = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t at = 0;
    while (at < length && parser->result.error == 0)
    {
        const uint32_t left = length - at;
        uint32_t n = left < 64 ? left : 64;
        uint64_t valid = n == 64 ? UINT64_MAX : (1ull << n) - 1;
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, source + at);
        const __m512i lower = _mm512_or_si512(bytes, _mm512_set1_epi8(32));
        uint64_t word = _mm512_cmple_epu8_mask(_mm512_sub_epi8(lower, _mm512_set1_epi8('a')), _mm512_set1_epi8(25));
        word |= _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('0')), _mm512_set1_epi8(9));
        word |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('_')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('.'));
        uint64_t special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('"')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('#'));
        special |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('/')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512());
        special |= _mm512_movepi8_mask(bytes);
        special &= valid;
        if (special != 0)
        {
            n = __builtin_ctzll(special);
            valid = n == 0 ? 0 : (1ull << n) - 1;
        }
        // Defer a word crossing the load boundary; the next load starts at that word. A word longer than a vector
        // is one rare token, scanned with full-width comparisons by asm_word_length.
        if (n == 64 && (word >> 63) != 0 && at + n < length && asm_word_byte(source[at + n]))
        {
            const uint64_t nonword = ~word;
            n = nonword != 0 ? 64 - __builtin_clzll(nonword) : 0;
            valid = n == 0 ? 0 : (1ull << n) - 1;
        }
        if (n != 0)
        {
            word &= valid;
            const uint64_t space = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(' ')) |
                                   _mm512_cmple_epu8_mask(_mm512_sub_epi8(bytes, _mm512_set1_epi8('\t')), _mm512_set1_epi8(4));
            const uint64_t newline = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n')) | _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(';'));
            const uint64_t punctuation = valid & ~word & (~space | newline);
            const uint64_t starts_mask = (word & ~(word << 1)) | punctuation;
            const uint64_t ends_mask = (word & ~(word >> 1)) | punctuation;
            uint32_t starts[64];
            uint32_t ends[64];
            uint32_t kinds[64];
            uint32_t start_count = 0;
            uint32_t end_count = 0;
            uint8_t byte_values[64] __attribute__((aligned(64)));
            _mm512_store_si512(byte_values, bytes);
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const __mmask16 start = starts_mask >> (16 * group);
                const __mmask16 end = ends_mask >> (16 * group);
                const __m512i positions = _mm512_add_epi32(iota, _mm512_set1_epi32(16 * group));
                __m512i kind = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)(byte_values + group * 16)));
                kind = _mm512_mask_mov_epi32(kind, word >> (16 * group), _mm512_set1_epi32(BUSTER_ASM_WORD));
                kind = _mm512_mask_mov_epi32(kind, newline >> (16 * group), _mm512_set1_epi32('\n'));
                const __m512i compact_starts = _mm512_maskz_compress_epi32(start, positions);
                const __m512i compact_ends = _mm512_maskz_compress_epi32(end, _mm512_add_epi32(positions, _mm512_set1_epi32(1)));
                const __m512i compact_kinds = _mm512_maskz_compress_epi32(start, kind);
                const uint32_t a = __builtin_popcount(start);
                const uint32_t b = __builtin_popcount(end);
                _mm512_mask_storeu_epi32(starts + start_count, (1u << a) - 1, compact_starts);
                _mm512_mask_storeu_epi32(kinds + start_count, (1u << a) - 1, compact_kinds);
                _mm512_mask_storeu_epi32(ends + end_count, (1u << b) - 1, compact_ends);
                start_count += a;
                end_count += b;
            }
            asm_lex_store(parser->tokens + parser->token_count, starts, ends, kinds, start_count, at);
            parser->token_count += start_count;
            at += n;
        }
        else if (asm_word_byte(source[at]))
        {
            const uint32_t end = at + asm_word_length(source + at, length - at, true);
            parser->tokens[parser->token_count++] = (AsmToken){.start = at, .end = end, .kind = BUSTER_ASM_WORD};
            at = end;
        }
        else
        {
            uint32_t end = at + 1;
            if (source[at] == '"')
            {
                while (end < length && source[end] != '"' && source[end] != '\n')
                {
                    end += source[end] == '\\' && end + 1 < length ? 2 : 1;
                }
                end += end < length && source[end] == '"';
            }
            else if (source[at] == '#' || (source[at] == '/' && end < length && source[end] == '/'))
            {
                while (end < length && source[end] != '\n')
                {
                    end += 1;
                }
            }
            else if (source[at] == '/' && end < length && source[end] == '*')
            {
                end += 1;
                while (end + 1 < length && !(source[end] == '*' && source[end + 1] == '/'))
                {
                    end += 1;
                }
                end = end + 1 < length ? end + 2 : length;
            }
            AsmParser rare = *parser;
            rare.source = (String8){.pointer = parser->source.pointer + at, .length = end - at};
            rare.tokens += parser->token_count;
            rare.token_count = 0;
            rare.options.vectorized = false;
            asm_lex_scalar(&rare);
            for (uint32_t i = 0; i < rare.token_count; i += 1)
            {
                rare.tokens[i].start += at;
                rare.tokens[i].end += at;
            }
            if (rare.result.error != 0)
            {
                asm_fail(parser, at + rare.result.location, rare.result.error);
            }
            parser->token_count += rare.token_count;
            at = end;
        }
    }
    parser->tokens[parser->token_count] = (AsmToken){.start = length, .end = length, .kind = 0};
}

static void asm_lex(AsmParser* parser)
{
    parser->instruction_capacity = 1;
    if (parser->options.vectorized)
    {
        asm_lex_vector(parser);
        // At most one instruction per statement. Count separators in the kind column without gathers, and reserve
        // instruction records for statements instead of all operand tokens. Only the third word of each token counts.
        for (uint32_t at = 0; at < parser->token_count; at += 4)
        {
            const uint32_t left = parser->token_count - at;
            const uint32_t count = left < 4 ? left : 4;
            const __mmask16 valid = (1u << (count * 4)) - 1;
            const __m512i records = _mm512_maskz_loadu_epi32(valid, parser->tokens + at);
            parser->instruction_capacity += __builtin_popcount(_mm512_mask_cmpeq_epi32_mask(valid & 0x4444, records, _mm512_set1_epi32('\n')));
        }
    }
    else
    {
        asm_lex_scalar(parser);
        for (uint32_t at = 0; at < parser->token_count; at += 1)
        {
            parser->instruction_capacity += parser->tokens[at].kind == '\n';
        }
    }
}

static String8 asm_token_text(const AsmParser* parser, uint32_t token)
{
    const AsmToken* value = &parser->tokens[token];
    return (String8){.pointer = parser->source.pointer + value->start, .length = value->end - value->start};
}

static bool asm_is(const AsmParser* parser, uint32_t token, const char* name)
{
    return x86_match_name(asm_token_text(parser, token), name);
}

static bool asm_take(AsmParser* parser, uint16_t kind)
{
    const bool result = parser->tokens[parser->at].kind == kind;
    parser->at += result;
    return result;
}

static void asm_expect(AsmParser* parser, uint16_t kind, const char* message)
{
    if (!asm_take(parser, kind))
    {
        asm_fail(parser, parser->tokens[parser->at].start, message);
    }
}

static uint32_t asm_fragment(AsmParser* parser, uint8_t kind, uint32_t location)
{
    uint32_t result = 0;
    if (parser->fragment_count >= parser->capacity)
    {
        asm_fail(parser, location, "fragment capacity exhausted");
    }
    else
    {
        result = parser->fragment_count++;
        parser->fragments[result] = (AsmFragment){.kind = kind, .section = parser->section, .location = location};
    }
    return result;
}

static uint32_t asm_symbol(AsmParser* parser, String8 name, uint32_t location)
{
    if (name.length == 0 || (name.pointer[0] >= '0' && name.pointer[0] <= '9') || (name.length == 1 && name.pointer[0] == '.'))
    {
        asm_fail(parser, location, "invalid symbol name");
    }
    uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->symbol_mask;
    uint32_t result = parser->symbol_slots[slot];
    while (result != 0 && (parser->symbols[result].name.length != name.length || memcmp(parser->symbols[result].name.pointer, name.pointer, name.length) != 0))
    {
        slot = (slot + 1) & parser->symbol_mask;
        result = parser->symbol_slots[slot];
    }
    if (result == 0)
    {
        if (parser->symbol_count >= parser->capacity)
        {
            asm_fail(parser, location, "symbol capacity exhausted");
        }
        else
        {
            result = parser->symbol_count++;
            parser->symbols[result] = (AsmSymbol){.name = name, .location = location};
            parser->symbol_slots[slot] = result;
        }
    }
    return result;
}

static uint32_t asm_numeric(AsmParser* parser, uint32_t digit, bool forward, bool definition, uint32_t location)
{
    if (definition)
    {
        parser->numeric[digit] += 1;
    }
    const uint32_t serial = parser->numeric[digit] + forward;
    char name[48];
    // ':' cannot occur in a source symbol spelling, so numeric labels cannot collide with user-written names.
    const int length = snprintf(name, sizeof(name), ":numeric.%u.%u", digit, serial);
    char* copy = arena_push(parser->arena, length, 1);
    uint32_t result = 0;
    if (copy == 0 || serial == 0)
    {
        asm_fail(parser, location, copy == 0 ? "arena exhausted" : "numeric backward label has no definition");
    }
    else
    {
        memcpy(copy, name, length);
        result = asm_symbol(parser, (String8){.pointer = copy, .length = length}, location);
        parser->symbols[result].internal = true;
    }
    return result;
}

static uint32_t asm_dot(AsmParser* parser, uint32_t location)
{
    uint32_t result = 0;
    if (parser->symbol_count >= parser->capacity)
    {
        asm_fail(parser, location, "symbol capacity exhausted");
    }
    else
    {
        result = parser->symbol_count++;
        parser->symbols[result] = (AsmSymbol){.fragment = asm_fragment(parser, ASM_LABEL, location), .location = location, .defined = true, .internal = true};
    }
    return result;
}

static bool asm_number(String8 text, __int128* value)
{
    uint32_t base = 10;
    uint32_t at = 0;
    if (text.length > 1 && text.pointer[0] == '0')
    {
        base = 8;
        if (text.pointer[1] == 'x' || text.pointer[1] == 'X' || text.pointer[1] == 'b' || text.pointer[1] == 'B')
        {
            base = text.pointer[1] == 'b' || text.pointer[1] == 'B' ? 2 : 16;
            at = 2;
        }
    }
    bool valid = at < text.length;
    __int128 result = 0;
    for (; valid && at < text.length; at += 1)
    {
        const uint8_t byte = text.pointer[at];
        const uint32_t digit = byte >= '0' && byte <= '9' ? byte - '0' : (byte | 32) >= 'a' && (byte | 32) <= 'f' ? (byte | 32) - 'a' + 10
                                                                                                                  : 255;
        valid = digit < base && result <= (((__int128)UINT64_MAX - digit) / base);
        if (valid)
        {
            result = result * base + digit;
        }
    }
    *value = result;
    return valid;
}

static bool asm_expr_range(__int128 value)
{
    return value >= INT64_MIN && value <= (__int128)UINT64_MAX;
}

static AsmExpr asm_combine(AsmParser* parser, AsmExpr left, AsmExpr right, uint16_t op, uint32_t location)
{
    AsmExpr result = {0};
    const bool symbolic = left.plus || left.minus || right.plus || right.minus;
    if (op == '+' || op == '-')
    {
        result.value = left.value + (op == '+' ? right.value : -right.value);
        const uint32_t plus[2] = {left.plus, op == '+' ? right.plus : right.minus};
        const uint32_t minus[2] = {left.minus, op == '+' ? right.minus : right.plus};
        uint32_t p[2] = {plus[0], plus[1]};
        uint32_t m[2] = {minus[0], minus[1]};
        for (uint32_t i = 0; i < 2; i += 1)
        {
            for (uint32_t j = 0; j < 2; j += 1)
            {
                if (p[i] != 0 && p[i] == m[j])
                {
                    p[i] = 0;
                    m[j] = 0;
                }
            }
        }
        if ((p[0] && p[1]) || (m[0] && m[1]))
        {
            asm_fail(parser, location, "expression needs more than a symbol difference");
        }
        result.plus = p[0] ? p[0] : p[1];
        result.minus = m[0] ? m[0] : m[1];
    }
    else if (symbolic)
    {
        asm_fail(parser, location, "operator requires absolute operands");
    }
    else
    {
        switch (op)
        {
        case '*':
        {
            if (__builtin_mul_overflow(left.value, right.value, &result.value))
            {
                asm_fail(parser, location, "expression overflow");
            }
        }
        break;
        case '/':
        case '%':
        {
            if (right.value == 0)
            {
                asm_fail(parser, location, "division by zero");
            }
            else
            {
                result.value = op == '/' ? left.value / right.value : left.value % right.value;
            }
        }
        break;
        case '&':
            result.value = (int64_t)((uint64_t)left.value & (uint64_t)right.value);
            break;
        case '|':
            result.value = (int64_t)((uint64_t)left.value | (uint64_t)right.value);
            break;
        case '^':
            result.value = (int64_t)((uint64_t)left.value ^ (uint64_t)right.value);
            break;
        default:
            asm_fail(parser, location, "unsupported expression operator");
            break;
        }
    }
    if (!asm_expr_range(result.value))
    {
        asm_fail(parser, location, "expression overflow");
    }
    return result;
}

static uint32_t asm_precedence(uint16_t op)
{
    // GAS precedence: multiplicative, bitwise, then additive. No C precedence assumptions.
    return op == '*' || op == '/' || op == '%' ? 3 : op == '&' || op == '|' || op == '^' ? 2
                                                 : op == '+' || op == '-'                ? 1
                                                                                         : 0;
}

static AsmExpr asm_expression(AsmParser* parser, uint32_t end)
{
    AsmExpr values[BUSTER_ASM_EXPR_DEPTH];
    uint16_t operators[BUSTER_ASM_EXPR_DEPTH];
    uint32_t value_count = 0;
    uint32_t operator_count = 0;
    bool need_value = true;
    const uint32_t location = parser->tokens[parser->at].start;
    while (parser->at < end && parser->result.error == 0)
    {
        const AsmToken token = parser->tokens[parser->at];
        if (value_count + 1 >= BUSTER_ASM_EXPR_DEPTH || operator_count + 1 >= BUSTER_ASM_EXPR_DEPTH)
        {
            asm_fail(parser, token.start, "expression nesting limit exceeded");
            break;
        }
        if (need_value)
        {
            if (token.kind == '+' || token.kind == '-' || token.kind == '~')
            {
                operators[operator_count++] = token.kind | 0x8000;
                parser->at += 1;
            }
            else if (token.kind == '(')
            {
                operators[operator_count++] = '(';
                parser->at += 1;
            }
            else if (token.kind == BUSTER_ASM_WORD)
            {
                const String8 text = asm_token_text(parser, parser->at++);
                AsmExpr value = {0};
                if (text.length == 2 && text.pointer[0] >= '0' && text.pointer[0] <= '9' && (text.pointer[1] == 'b' || text.pointer[1] == 'f'))
                {
                    value.plus = asm_numeric(parser, text.pointer[0] - '0', text.pointer[1] == 'f', false, token.start);
                }
                else if (text.pointer[0] >= '0' && text.pointer[0] <= '9')
                {
                    if (!asm_number(text, &value.value))
                    {
                        asm_fail(parser, token.start, "invalid or overflowing integer");
                    }
                }
                else
                {
                    const uint32_t symbol = text.length == 1 && text.pointer[0] == '.' ? asm_dot(parser, token.start) : asm_symbol(parser, text, token.start);
                    if (parser->symbols[symbol].absolute)
                    {
                        value.value = parser->symbols[symbol].constant;
                    }
                    else
                    {
                        value.plus = symbol;
                    }
                }
                values[value_count++] = value;
                need_value = false;
            }
            else
            {
                asm_fail(parser, token.start, "expected expression operand");
            }
        }
        else
        {
            const bool close = token.kind == ')';
            const uint32_t precedence = asm_precedence(token.kind);
            if (!close && precedence == 0)
            {
                break;
            }
            while (operator_count != 0 && operators[operator_count - 1] != '(' && (close || (operators[operator_count - 1] & 0x8000) || asm_precedence(operators[operator_count - 1]) >= precedence))
            {
                const uint16_t op = operators[--operator_count];
                if (op & 0x8000)
                {
                    if ((op & 0x7fff) == '-')
                    {
                        values[value_count - 1] = asm_combine(parser, (AsmExpr){0}, values[value_count - 1], '-', location);
                    }
                    else if ((op & 0x7fff) == '~')
                    {
                        if (values[value_count - 1].plus || values[value_count - 1].minus)
                        {
                            asm_fail(parser, location, "bitwise not requires an absolute operand");
                        }
                        values[value_count - 1].value = (int64_t)~(uint64_t)values[value_count - 1].value;
                    }
                }
                else if (value_count >= 2)
                {
                    values[value_count - 2] = asm_combine(parser, values[value_count - 2], values[value_count - 1], op, location);
                    value_count -= 1;
                }
            }
            if (close)
            {
                if (operator_count == 0)
                {
                    break;
                }
                operator_count -= 1;
                parser->at += 1;
            }
            else
            {
                operators[operator_count++] = token.kind;
                parser->at += 1;
                need_value = true;
            }
        }
    }
    if (need_value)
    {
        asm_fail(parser, location, "incomplete expression");
    }
    while (operator_count != 0 && parser->result.error == 0)
    {
        const uint16_t op = operators[--operator_count];
        if (op == '(')
        {
            asm_fail(parser, location, "unclosed expression parenthesis");
        }
        else if (op & 0x8000)
        {
            if ((op & 0x7fff) == '-')
            {
                values[value_count - 1] = asm_combine(parser, (AsmExpr){0}, values[value_count - 1], '-', location);
            }
            else if ((op & 0x7fff) == '~')
            {
                if (values[value_count - 1].plus || values[value_count - 1].minus)
                {
                    asm_fail(parser, location, "bitwise not requires an absolute operand");
                }
                values[value_count - 1].value = (int64_t)~(uint64_t)values[value_count - 1].value;
            }
        }
        else if (value_count >= 2)
        {
            values[value_count - 2] = asm_combine(parser, values[value_count - 2], values[value_count - 1], op, location);
            value_count -= 1;
        }
        else
        {
            asm_fail(parser, location, "incomplete expression");
        }
    }
    return value_count != 0 ? values[0] : (AsmExpr){0};
}

static __int128 asm_absolute(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    const AsmExpr expression = asm_expression(parser, end);
    if (expression.plus || expression.minus)
    {
        asm_fail(parser, location, "absolute expression required here (forward constants are unsupported)");
    }
    return expression.value;
}

static bool asm_register(String8 name, X86Operand* operand)
{
    static const char* const names[4][8] =
        {
            {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"},
            {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"},
            {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"},
            {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"},
    };
    bool found = false;
    for (uint32_t width = 0; width < 4; width += 1)
    {
        for (uint32_t reg = 0; reg < 8; reg += 1)
        {
            if (x86_match_name(name, names[width][reg]))
            {
                *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = width + 1, .width = 1u << width, .reg = reg};
                found = true;
            }
        }
    }
    if (name.length == 2 && name.pointer[1] == 'h' && name.pointer[0] >= 'a' && name.pointer[0] <= 'd')
    {
        const uint8_t high[] = {X86_REGISTER_AH, X86_REGISTER_BH, X86_REGISTER_CH, X86_REGISTER_DH};
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = BUSTER_X86_CLASS_GPR8, .width = 1, .reg = high[name.pointer[0] - 'a']};
        found = true;
    }
    if (!found && name.length >= 2)
    {
        uint32_t start = 0;
        uint32_t class = 0;
        uint32_t width = 0;
        if (name.pointer[0] == 'r' && name.pointer[1] >= '0' && name.pointer[1] <= '9')
        {
            start = 1;
            class = BUSTER_X86_CLASS_GPR64;
            width = 8;
        }
        else if (name.length > 3 && name.pointer[1] == 'm' && name.pointer[2] == 'm')
        {
            class = name.pointer[0] == 'x' ? BUSTER_X86_CLASS_XMM : name.pointer[0] == 'y' ? BUSTER_X86_CLASS_YMM
                                                                : name.pointer[0] == 'z'   ? BUSTER_X86_CLASS_ZMM
                                                                                           : 0;
            start = 3;
            width = class != 0 ? 16u << (class - BUSTER_X86_CLASS_XMM) : 0;
        }
        else if (name.pointer[0] == 'k')
        {
            class = BUSTER_X86_CLASS_OPMASK;
            start = 1;
        }
        if (class != 0)
        {
            uint32_t at = start;
            uint32_t reg = 0;
            while (at < name.length && name.pointer[at] >= '0' && name.pointer[at] <= '9' && reg < 100)
            {
                reg = reg * 10 + name.pointer[at++] - '0';
            }
            if (class == BUSTER_X86_CLASS_GPR64 && at + 1 == name.length)
            {
                const char suffix = name.pointer[at++];
                width = suffix == 'b' ? 1 : suffix == 'w' ? 2
                                        : suffix == 'd'   ? 4
                                                          : 0;
                class = width == 1 ? BUSTER_X86_CLASS_GPR8 : width == 2 ? BUSTER_X86_CLASS_GPR16
                                                                        : BUSTER_X86_CLASS_GPR32;
            }
            found = at > start && at == name.length && reg < (class == BUSTER_X86_CLASS_OPMASK ? 8u : class <= BUSTER_X86_CLASS_GPR64 ? 16u
                                                                                                                                      : 32u);
            found &= class > BUSTER_X86_CLASS_GPR64 || (reg >= 8 && width != 0);
            if (found)
            {
                *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = class, .width = width, .reg = reg};
            }
        }
    }
    if (x86_match_name(name, "rip"))
    {
        *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .class = 255, .width = 8};
        found = true;
    }
    return found;
}

static bool asm_read_register(AsmParser* parser, uint32_t end, X86Operand* operand)
{
    uint32_t at = parser->at;
    const bool prefix = at < end && parser->tokens[at].kind == '%';
    at += prefix;
    const bool found = prefix == parser->prefix && at < end && parser->tokens[at].kind == BUSTER_ASM_WORD && asm_register(asm_token_text(parser, at), operand);
    if (found)
    {
        parser->at = at + 1;
    }
    return found;
}

static void asm_address_register(AsmParser* parser, X86Operand* memory, X86Operand reg, uint32_t scale, bool index, uint32_t location)
{
    if (reg.class == 255 && !index && !(memory->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX)))
    {
        memory->mode = BUSTER_X86_MEMORY_RIP;
    }
    else if (reg.class != BUSTER_X86_CLASS_GPR64 || (memory->mode & BUSTER_X86_MODE_RIP) || scale > 3)
    {
        asm_fail(parser, location, "address requires 64-bit base/index registers and a scale of 1, 2, 4 or 8");
    }
    else if (!index && !(memory->mode & BUSTER_X86_MODE_BASE))
    {
        memory->mode |= BUSTER_X86_MODE_BASE;
        memory->base = reg.reg;
    }
    else if (!(memory->mode & BUSTER_X86_MODE_INDEX))
    {
        memory->mode |= BUSTER_X86_MODE_INDEX | scale;
        memory->index = reg.reg;
    }
    else
    {
        asm_fail(parser, location, "too many address registers");
    }
}

static uint32_t asm_scale(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    const __int128 value = asm_absolute(parser, end);
    if (value != 1 && value != 2 && value != 4 && value != 8)
    {
        asm_fail(parser, location, "address scale must be 1, 2, 4 or 8");
    }
    return value == 2 ? 1 : value == 4 ? 2
                        : value == 8   ? 3
                                       : 0;
}

static void asm_intel_address(AsmParser* parser, uint32_t end, X86Operand* operand, AsmExpr* expression)
{
    operand->kind = X86_OPERAND_MEMORY;
    operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
    asm_expect(parser, '[', "expected '['");
    bool any = false;
    while (parser->at < end && parser->tokens[parser->at].kind != ']' && parser->result.error == 0)
    {
        const uint32_t location = parser->tokens[parser->at].start;
        bool negative = asm_take(parser, '-');
        if (!negative)
        {
            if (any)
            {
                asm_expect(parser, '+', "expected '+' or '-' in address");
            }
            else
            {
                asm_take(parser, '+');
            }
        }
        uint32_t term_end = parser->at;
        uint32_t depth = 0;
        while (term_end < end)
        {
            const uint16_t kind = parser->tokens[term_end].kind;
            // A '+' or '-' after an operand separates terms; at a term's start or after an operator, it is a sign.
            const bool after_operand = term_end != parser->at && (parser->tokens[term_end - 1].kind == BUSTER_ASM_WORD || parser->tokens[term_end - 1].kind == ')');
            if (depth == 0 && (kind == ']' || ((kind == '+' || kind == '-') && after_operand)))
            {
                break;
            }
            depth += kind == '(';
            if (kind == ')' && depth != 0)
            {
                depth -= 1;
            }
            term_end += 1;
        }
        X86Operand reg;
        // A scaled index is written either way round: `rsi*4`, or `4*rsi` as LLVM's Intel printer writes it.
        const uint32_t term = parser->at;
        const uint32_t named = term_end - (parser->prefix ? 2 : 1);
        bool scale_first = false;
        if (term_end >= term + (parser->prefix ? 4 : 3) && parser->tokens[named - 1].kind == '*')
        {
            parser->at = named;
            scale_first = asm_read_register(parser, term_end, &reg);
            parser->at = term;
        }
        if (scale_first)
        {
            const uint32_t scale = asm_scale(parser, named - 1);
            if (parser->at != named - 1)
            {
                asm_fail(parser, location, "unsupported address term");
            }
            if (negative)
            {
                asm_fail(parser, location, "address register cannot be subtracted");
            }
            parser->at = term_end;
            asm_address_register(parser, operand, reg, scale, true, location);
        }
        else if (asm_read_register(parser, term_end, &reg))
        {
            const bool index = asm_take(parser, '*');
            const uint32_t scale = index ? asm_scale(parser, term_end) : 0;
            if (negative)
            {
                asm_fail(parser, location, "address register cannot be subtracted");
            }
            asm_address_register(parser, operand, reg, scale, index, location);
        }
        else
        {
            const AsmExpr value = asm_expression(parser, term_end);
            *expression = asm_combine(parser, *expression, value, negative ? '-' : '+', location);
        }
        if (parser->at != term_end)
        {
            asm_fail(parser, location, "unsupported address term");
        }
        any = true;
    }
    if (!any)
    {
        asm_fail(parser, parser->tokens[parser->at].start, "empty address");
    }
    asm_expect(parser, ']', "unclosed address");
}

static void asm_operand(AsmParser* parser, uint32_t end, bool branch, X86Operand* operand, AsmExpr* expression)
{
    const uint32_t location = parser->tokens[parser->at].start;
    bool immediate = false;
    bool indirect = false;
    if (!parser->options.intel)
    {
        indirect = asm_take(parser, '*');
        immediate = asm_take(parser, '$');
    }
    else if (asm_is(parser, parser->at, "offset"))
    {
        parser->at += 1;
        immediate = true;
        if (asm_is(parser, parser->at, "flat") && parser->at + 1 < end)
        {
            parser->at += 1;
            asm_expect(parser, ':', "expected ':' after flat");
        }
    }
    if (!immediate && parser->options.intel)
    {
        static const char* const names[] = {"byte", "word", "dword", "qword", "xmmword", "ymmword", "zmmword"};
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            if (asm_is(parser, parser->at, names[i]))
            {
                operand->width = 1u << i;
                parser->at += 1;
                if (!asm_is(parser, parser->at, "ptr"))
                {
                    asm_fail(parser, location, "expected 'ptr' after memory width");
                }
                else
                {
                    parser->at += 1;
                }
                break;
            }
        }
    }
    uint32_t segment_at = parser->at;
    segment_at += segment_at < end && parser->tokens[segment_at].kind == '%';
    if (!immediate && segment_at + 1 < end && parser->tokens[segment_at + 1].kind == ':' && (asm_is(parser, segment_at, "fs") || asm_is(parser, segment_at, "gs")))
    {
        operand->segment = asm_is(parser, segment_at, "fs") ? 1 : 2;
        parser->at = segment_at + 2;
    }
    const uint16_t width = operand->width;
    const uint8_t segment = operand->segment;
    if (!immediate && width == 0 && segment == 0 && asm_read_register(parser, end, operand))
    {
        if (operand->class == 255 || (branch && !parser->options.intel && !indirect))
        {
            asm_fail(parser, location, "invalid register operand (AT&T indirect branches require '*')");
        }
    }
    else if (parser->options.intel && !immediate && parser->tokens[parser->at].kind == '[')
    {
        asm_intel_address(parser, end, operand, expression);
    }
    else if (!parser->options.intel && !immediate && (!branch || indirect))
    {
        operand->kind = X86_OPERAND_MEMORY;
        operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
        uint32_t address = parser->at;
        while (address < end && parser->tokens[address].kind != '(')
        {
            address += 1;
        }
        if (address != parser->at)
        {
            *expression = asm_expression(parser, address);
        }
        if (asm_take(parser, '('))
        {
            X86Operand reg;
            if (asm_read_register(parser, end, &reg))
            {
                asm_address_register(parser, operand, reg, 0, false, location);
            }
            if (asm_take(parser, ','))
            {
                if (!asm_read_register(parser, end, &reg))
                {
                    asm_fail(parser, location, "expected address index register");
                }
                else
                {
                    const uint32_t scale = asm_take(parser, ',') ? asm_scale(parser, end) : 0;
                    asm_address_register(parser, operand, reg, scale, true, location);
                }
            }
            asm_expect(parser, ')', "unclosed address");
        }
    }
    else
    {
        *expression = asm_expression(parser, end);
        operand->kind = X86_OPERAND_IMMEDIATE;
        // A bare symbol is a branch's destination but any other instruction's memory; a width or a segment always
        // asks for memory (`call qword ptr callback` calls through the pointer at callback).
        if (parser->options.intel && !immediate && (width != 0 || segment != 0 || (!branch && (expression->plus != 0 || expression->minus != 0))))
        {
            operand->kind = X86_OPERAND_MEMORY;
            operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
        }
    }
    if (segment != 0 && operand->kind != X86_OPERAND_MEMORY)
    {
        asm_fail(parser, location, "segment override requires memory");
    }
    operand->value = expression->value;
    operand->symbolic = expression->plus != 0 || expression->minus != 0;
}

static void asm_decorations(AsmParser* parser, uint32_t end, X86MatchRequest* request, uint32_t operand)
{
    while (parser->at < end && asm_take(parser, '{') && parser->result.error == 0)
    {
        asm_take(parser, '%');
        const String8 text = asm_token_text(parser, parser->at);
        X86Operand mask;
        if (asm_register(text, &mask) && mask.class == BUSTER_X86_CLASS_OPMASK && mask.reg != 0 && (request->decorations & 7) == 0)
        {
            if (operand != (parser->options.intel ? 0u : request->count - 1))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "mask must decorate the destination");
            }
            request->decorations |= mask.reg;
        }
        else if (x86_match_name(text, "z") && !(request->decorations & BUSTER_X86_DECORATION_ZEROING))
        {
            if (operand != (parser->options.intel ? 0u : request->count - 1))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "zeroing must decorate the destination");
            }
            request->decorations |= BUSTER_X86_DECORATION_ZEROING;
        }
        else if (text.length > 3 && memcmp(text.pointer, "1to", 3) == 0 && request->broadcast == 0 && request->operands[operand].kind == X86_OPERAND_MEMORY)
        {
            __int128 count;
            if (!asm_number((String8){.pointer = text.pointer + 3, .length = text.length - 3}, &count) || (count != 8 && count != 16))
            {
                asm_fail(parser, parser->tokens[parser->at].start, "unsupported broadcast");
            }
            request->broadcast = count;
        }
        else
        {
            asm_fail(parser, parser->tokens[parser->at].start, "unsupported or duplicate instruction decoration");
        }
        parser->at += 1;
        asm_expect(parser, '}', "unclosed decoration");
    }
}

// Replaces a common condition or shift alias with the canonical catalogue name it shares an identity with.
static bool asm_alias(String8* name)
{
    static const char* const aliases[][2] =
        {
            {"jz", "je"},
            {"jnz", "jne"},
            {"jc", "jb"},
            {"jnc", "jae"},
            {"jna", "jbe"},
            {"jnbe", "ja"},
            {"jnae", "jb"},
            {"jnb", "jae"},
            {"jnge", "jl"},
            {"jnl", "jge"},
            {"jng", "jle"},
            {"jnle", "jg"},
            {"jpe", "jp"},
            {"jpo", "jnp"},
            {"sal", "shl"},
    };
    bool found = false;
    for (uint32_t i = 0; !found && i < BUSTER_ARRAY_LENGTH(aliases); i += 1)
    {
        found = x86_match_name(*name, aliases[i][0]);
        if (found)
        {
            *name = (String8){.pointer = (char*)aliases[i][1], .length = strlen(aliases[i][1])};
        }
    }
    return found;
}

static void asm_instruction(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    if (parser->instruction_count >= parser->instruction_capacity || parser->section == BUSTER_LINK_CLASS_BSS)
    {
        asm_fail(parser, location, "instruction capacity exhausted or instruction in .bss");
    }
    else
    {
        AsmInstruction* instruction = &parser->instructions[parser->instruction_count++];
        *instruction = (AsmInstruction){.location = location};
        instruction->request.mnemonic = asm_token_text(parser, parser->at++);
        String8* name = &instruction->request.mnemonic;
        uint32_t width = 0;
        // A complete alias keeps its last letter: jnb, jnl and sal end in suffix letters.
        bool known = asm_alias(name);
        if (!parser->options.intel && !known && name->length > 1)
        {
            // Strip integer suffixes only when the full mnemonic has no catalogue candidate.
            const uint32_t bucket = x86_match_hash(*name);
            for (uint32_t i = parser->index->begin[bucket]; i < parser->index->begin[bucket + 1]; i += 1)
            {
                known |= x86_match_name(*name, x86_form_descriptions[parser->index->forms[i]].mnemonic);
            }
            if (!known)
            {
                const char suffix = name->pointer[name->length - 1];
                width = suffix == 'b' ? 1 : suffix == 'w' ? 2
                                        : suffix == 'l'   ? 4
                                        : suffix == 'q'   ? 8
                                                          : 0;
                name->length -= width != 0;
                asm_alias(name);
            }
        }
        // LEA's memory operand is an address, not a load: its suffix sizes the destination register alone.
        const bool address = x86_match_name(*name, "lea");
        instruction->request.width = address ? 0 : width;
        instruction->branch = name->pointer[0] == 'j' || x86_match_name(*name, "call") || (name->length >= 4 && memcmp(name->pointer, "loop", 4) == 0);
        uint32_t begins[4];
        uint32_t ends[4];
        uint32_t first = parser->at;
        uint32_t depth = 0;
        for (uint32_t i = first; i <= end && parser->result.error == 0; i += 1)
        {
            const uint16_t kind = parser->tokens[i].kind;
            if (i == end || (kind == ',' && depth == 0))
            {
                if (i > first && instruction->request.count < 4)
                {
                    const uint32_t operand = instruction->request.count++;
                    begins[operand] = first;
                    ends[operand] = i;
                }
                else if (i != first || i != end || first != parser->at)
                {
                    asm_fail(parser, location, "empty operand or too many operands");
                }
                first = i + 1;
            }
            depth += kind == '[' || kind == '(' || kind == '{';
            if (kind == ']' || kind == ')' || kind == '}')
            {
                if (depth == 0)
                {
                    asm_fail(parser, parser->tokens[i].start, "unmatched operand delimiter");
                }
                else
                {
                    depth -= 1;
                }
            }
        }
        if (depth != 0)
        {
            asm_fail(parser, location, "unclosed operand delimiter");
        }
        for (uint32_t i = 0; parser->result.error == 0 && i < instruction->request.count; i += 1)
        {
            parser->at = begins[i];
            asm_operand(parser, ends[i], instruction->branch, &instruction->request.operands[i], &instruction->expressions[i]);
            asm_decorations(parser, ends[i], &instruction->request, i);
            X86Operand* operand = &instruction->request.operands[i];
            const bool shift = x86_match_name(*name, "shl") || x86_match_name(*name, "shr") || x86_match_name(*name, "sar") || x86_match_name(*name, "rol") || x86_match_name(*name, "ror") || x86_match_name(*name, "shld") || x86_match_name(*name, "shrd");
            const bool count_register = !parser->options.intel && i == 0 && shift && operand->kind == X86_OPERAND_REGISTER && operand->class == BUSTER_X86_CLASS_GPR8 && operand->reg == 1;
            if (width != 0 && operand->kind != X86_OPERAND_IMMEDIATE && !count_register && !(address && operand->kind == X86_OPERAND_MEMORY))
            {
                if (operand->width != 0 && operand->width != width)
                {
                    asm_fail(parser, location, "mnemonic suffix and register width disagree");
                }
                operand->width = width;
            }
            if (parser->at != ends[i])
            {
                asm_fail(parser, parser->tokens[parser->at].start, "unsupported operand syntax");
            }
        }
        if (!parser->options.intel)
        {
            for (uint32_t i = 0; i < instruction->request.count / 2; i += 1)
            {
                const uint32_t j = instruction->request.count - 1 - i;
                const X86Operand operand = instruction->request.operands[i];
                const AsmExpr expression = instruction->expressions[i];
                instruction->request.operands[i] = instruction->request.operands[j];
                instruction->expressions[i] = instruction->expressions[j];
                instruction->request.operands[j] = operand;
                instruction->expressions[j] = expression;
            }
        }
        instruction->fragment = asm_fragment(parser, ASM_INSTRUCTION, location);
        parser->fragments[instruction->fragment].instruction = parser->instruction_count - 1;
        parser->section_data[parser->section] = false;
        parser->at = end;
    }
}

static void asm_string(AsmParser* parser, uint32_t end, bool terminated)
{
    do
    {
        const AsmToken token = parser->tokens[parser->at];
        if (parser->at >= end || token.kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, token.start, "expected string literal");
            break;
        }
        const uint32_t fragment = asm_fragment(parser, ASM_BYTES, token.start);
        AsmFragment* output = &parser->fragments[fragment];
        output->bytes = arena_push(parser->arena, token.end - token.start, 1);
        if (output->bytes == 0)
        {
            asm_fail(parser, token.start, "arena exhausted");
            break;
        }
        for (uint32_t at = token.start + 1; at + 1 < token.end && parser->result.error == 0; at += 1)
        {
            uint32_t byte = (uint8_t)parser->source.pointer[at];
            if (byte == '\\')
            {
                byte = (uint8_t)parser->source.pointer[++at];
                if (byte == 'n')
                {
                    byte = '\n';
                }
                else if (byte == 'r')
                {
                    byte = '\r';
                }
                else if (byte == 't')
                {
                    byte = '\t';
                }
                else if (byte == 'b')
                {
                    byte = '\b';
                }
                else if (byte == 'f')
                {
                    byte = '\f';
                }
                else if (byte == '\\' || byte == '"')
                {
                }
                else if (byte >= '0' && byte <= '7')
                {
                    byte -= '0';
                    for (uint32_t n = 1; n < 3 && at + 2 < token.end && parser->source.pointer[at + 1] >= '0' && parser->source.pointer[at + 1] <= '7'; n += 1)
                    {
                        byte = byte * 8 + parser->source.pointer[++at] - '0';
                    }
                }
                else
                {
                    asm_fail(parser, at, "unsupported string escape (use octal for arbitrary bytes)");
                }
                if (byte > 255)
                {
                    asm_fail(parser, at, "string escape overflows a byte");
                }
            }
            output->bytes[output->size++] = byte;
        }
        if (terminated)
        {
            output->bytes[output->size++] = 0;
        }
        parser->at += 1;
    } while (parser->result.error == 0 && asm_take(parser, ','));
}

static void asm_directive(AsmParser* parser, uint32_t end)
{
    const uint32_t token = parser->at++;
    const uint32_t location = parser->tokens[token].start;
    if (asm_is(parser, token, ".intel_syntax") || asm_is(parser, token, ".att_syntax"))
    {
        parser->options.intel = asm_is(parser, token, ".intel_syntax");
        parser->prefix = true;
        if (parser->at < end)
        {
            if (!asm_is(parser, parser->at, "prefix") && !asm_is(parser, parser->at, "noprefix"))
            {
                asm_fail(parser, location, "syntax directive expects prefix or noprefix");
            }
            parser->prefix = !asm_is(parser, parser->at++, "noprefix");
        }
    }
    else if (asm_is(parser, token, ".text") || asm_is(parser, token, ".data") || asm_is(parser, token, ".bss") || asm_is(parser, token, ".section"))
    {
        const bool section = asm_is(parser, token, ".section");
        const uint32_t name = section ? parser->at++ : token;
        uint8_t class = 0;
        if (asm_is(parser, name, ".text"))
        {
            class = BUSTER_LINK_CLASS_TEXT;
        }
        else if (asm_is(parser, name, ".rodata"))
        {
            class = BUSTER_LINK_CLASS_RODATA;
        }
        else if (asm_is(parser, name, ".data"))
        {
            class = BUSTER_LINK_CLASS_DATA;
        }
        else if (asm_is(parser, name, ".bss"))
        {
            class = BUSTER_LINK_CLASS_BSS;
        }
        if (class == 0)
        {
            asm_fail(parser, location, "unsupported section (supported: .text, .rodata, .data, .bss)");
        }
        else
        {
            parser->section = class;
            // Even an empty declared section has an atom.
            asm_fragment(parser, ASM_LABEL, location);
            if (section && asm_take(parser, ','))
            {
                const char* flags = class == BUSTER_LINK_CLASS_TEXT ? "\"ax\"" : class == BUSTER_LINK_CLASS_RODATA ? "\"a\""
                                                                                                                   : "\"aw\"";
                if (!asm_is(parser, parser->at++, flags))
                {
                    asm_fail(parser, location, "section flags disagree with the supported section");
                }
                if (asm_take(parser, ','))
                {
                    asm_expect(parser, '@', "expected section type");
                    if (!asm_is(parser, parser->at++, class == BUSTER_LINK_CLASS_BSS ? "nobits" : "progbits"))
                    {
                        asm_fail(parser, location, "section type disagrees with the supported section");
                    }
                }
            }
        }
    }
    else if (asm_is(parser, token, ".globl") || asm_is(parser, token, ".global") || asm_is(parser, token, ".local") || asm_is(parser, token, ".weak"))
    {
        const uint8_t binding = asm_is(parser, token, ".local") ? BUSTER_LINK_SYMBOL_LOCAL : asm_is(parser, token, ".weak") ? BUSTER_LINK_SYMBOL_WEAK
                                                                                                                            : BUSTER_LINK_SYMBOL_GLOBAL;
        do
        {
            if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_WORD)
            {
                asm_fail(parser, location, "binding directive requires a symbol name");
                break;
            }
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, asm_token_text(parser, parser->at++), location)];
            symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_BINDING) | binding;
            symbol->explicit_binding = true;
        } while (asm_take(parser, ','));
    }
    else if (asm_is(parser, token, ".type") || asm_is(parser, token, ".size") || asm_is(parser, token, ".equ") || asm_is(parser, token, ".set"))
    {
        if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_WORD)
        {
            asm_fail(parser, location, "directive requires a symbol name");
        }
        else
        {
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, asm_token_text(parser, parser->at++), location)];
            asm_expect(parser, ',', "expected ',' after symbol");
            if (asm_is(parser, token, ".type"))
            {
                asm_expect(parser, '@', "expected '@function', '@object' or '@notype'");
                uint8_t type = BUSTER_LINK_SYMBOL_NOTYPE;
                if (asm_is(parser, parser->at, "function"))
                {
                    type = BUSTER_LINK_SYMBOL_FUNCTION;
                }
                else if (asm_is(parser, parser->at, "object"))
                {
                    type = BUSTER_LINK_SYMBOL_OBJECT;
                }
                else if (!asm_is(parser, parser->at, "notype"))
                {
                    asm_fail(parser, location, "unsupported symbol type");
                }
                symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_TYPE) | type;
                parser->at += 1;
            }
            else if (asm_is(parser, token, ".size"))
            {
                symbol->size = asm_expression(parser, end);
                symbol->has_size = true;
            }
            else
            {
                const __int128 value = asm_absolute(parser, end);
                if (symbol->defined && (!symbol->absolute || asm_is(parser, token, ".equ")))
                {
                    asm_fail(parser, location, "duplicate symbol definition");
                }
                symbol->constant = value;
                symbol->absolute = true;
                symbol->defined = true;
            }
        }
    }
    else if (asm_is(parser, token, ".byte") || asm_is(parser, token, ".word") || asm_is(parser, token, ".long") || asm_is(parser, token, ".quad"))
    {
        const uint32_t width = asm_is(parser, token, ".byte") ? 1 : asm_is(parser, token, ".word") ? 2
                                                                : asm_is(parser, token, ".long")   ? 4
                                                                                                   : 8;
        do
        {
            // `.` is the address of this datum, so it is bound before the datum takes its place.
            const AsmExpr expression = asm_expression(parser, end);
            const uint32_t fragment = asm_fragment(parser, ASM_VALUE, location);
            parser->fragments[fragment].expression = expression;
            parser->fragments[fragment].size = width;
        } while (parser->result.error == 0 && asm_take(parser, ','));
        parser->section_data[parser->section] = true;
    }
    else if (asm_is(parser, token, ".ascii") || asm_is(parser, token, ".asciz") || asm_is(parser, token, ".string"))
    {
        asm_string(parser, end, !asm_is(parser, token, ".ascii"));
        parser->section_data[parser->section] = true;
    }
    else if (asm_is(parser, token, ".zero") || asm_is(parser, token, ".balign") || asm_is(parser, token, ".p2align"))
    {
        const bool align = !asm_is(parser, token, ".zero");
        __int128 value = asm_absolute(parser, end);
        if (asm_is(parser, token, ".p2align"))
        {
            if (value < 0 || value > 28)
            {
                asm_fail(parser, location, "alignment exponent must be in [0, 28]");
                value = 0;
            }
            value = (__int128)1 << (uint32_t)value;
        }
        if (value < 0 || value > BUSTER_ASM_MAX_SECTION || (align && (value == 0 || ((uint64_t)value & ((uint64_t)value - 1)))))
        {
            asm_fail(parser, location, "invalid size or alignment (alignment must be a power of two)");
        }
        const uint32_t fragment = asm_fragment(parser, align ? ASM_ALIGN : ASM_FILL, location);
        AsmFragment* output = &parser->fragments[fragment];
        output->size = align ? 0 : (uint32_t)value;
        output->alignment = align ? (uint32_t)value : 0;
        output->maximum = BUSTER_ASM_MAX_SECTION;
        output->fill = 0;
        output->padding = !align || parser->section != BUSTER_LINK_CLASS_TEXT ? ASM_PADDING_FILL : parser->section_data[parser->section] ? ASM_PADDING_NOPS_AFTER_DATA : ASM_PADDING_NOPS;
        if (asm_take(parser, ','))
        {
            if (parser->tokens[parser->at].kind != ',')
            {
                const __int128 fill = asm_absolute(parser, end);
                if (fill < -128 || fill > 255)
                {
                    asm_fail(parser, location, "fill must fit in a byte");
                }
                output->fill = (uint8_t)fill;
                // As in GNU as, code is padded with NOPs for a fill of 0x90 too, and with any other fill byte as given.
                output->padding = output->fill == 0x90 ? output->padding : ASM_PADDING_FILL;
            }
            if (align && asm_take(parser, ','))
            {
                const __int128 maximum = asm_absolute(parser, end);
                if (maximum < 0 || maximum > BUSTER_ASM_MAX_SECTION)
                {
                    asm_fail(parser, location, "invalid alignment maximum");
                }
                // A maximum of 0 is no maximum, as in GNU as.
                output->maximum = maximum == 0 ? BUSTER_ASM_MAX_SECTION : (uint32_t)maximum;
            }
        }
        parser->section_data[parser->section] |= !align;
        if (align && value > 0)
        {
            const uint32_t log = __builtin_ctzll((uint64_t)value);
            if (log > parser->section_alignment[parser->section])
            {
                parser->section_alignment[parser->section] = log;
            }
        }
    }
    else
    {
        asm_fail(parser, location, "unsupported assembler directive");
    }
    if (parser->at != end)
    {
        asm_fail(parser, parser->tokens[parser->at <= parser->token_count ? parser->at : parser->token_count].start, "unexpected directive argument");
    }
}

// Eight bounded spellings in lanes. Register lookup compares packed keys, and decimal conversion multiplies all
// active lanes together. The uncommon spellings (long names, hexadecimal, expressions, addresses/decorations) go to
// the exact grammar. No scalar asm_register/asm_number call is made for a successful batch.
static void asm_simple_words_vector(const uint64_t* keys, const uint64_t* lengths, uint32_t count, uint64_t* descriptors, uint64_t* values)
{
    static const struct
    {
        char name[8];
        uint8_t reg;
        uint8_t class;
        uint8_t width;
    } registers[] =
        {
            {"rax", 0, 4, 8},
            {"rcx", 1, 4, 8},
            {"rdx", 2, 4, 8},
            {"rbx", 3, 4, 8},
            {"rsp", 4, 4, 8},
            {"rbp", 5, 4, 8},
            {"rsi", 6, 4, 8},
            {"rdi", 7, 4, 8},
            {"eax", 0, 3, 4},
            {"ecx", 1, 3, 4},
            {"edx", 2, 3, 4},
            {"ebx", 3, 3, 4},
            {"esp", 4, 3, 4},
            {"ebp", 5, 3, 4},
            {"esi", 6, 3, 4},
            {"edi", 7, 3, 4},
            {"ax", 0, 2, 2},
            {"cx", 1, 2, 2},
            {"dx", 2, 2, 2},
            {"bx", 3, 2, 2},
            {"sp", 4, 2, 2},
            {"bp", 5, 2, 2},
            {"si", 6, 2, 2},
            {"di", 7, 2, 2},
            {"al", 0, 1, 1},
            {"cl", 1, 1, 1},
            {"dl", 2, 1, 1},
            {"bl", 3, 1, 1},
            {"spl", 4, 1, 1},
            {"bpl", 5, 1, 1},
            {"sil", 6, 1, 1},
            {"dil", 7, 1, 1},
            {"ah", 68, 1, 1},
            {"ch", 69, 1, 1},
            {"dh", 70, 1, 1},
            {"bh", 71, 1, 1},
            {"r8", 8, 4, 8},
            {"r9", 9, 4, 8},
            {"r10", 10, 4, 8},
            {"r11", 11, 4, 8},
            {"r12", 12, 4, 8},
            {"r13", 13, 4, 8},
            {"r14", 14, 4, 8},
            {"r15", 15, 4, 8},
            {"r8d", 8, 3, 4},
            {"r9d", 9, 3, 4},
            {"r10d", 10, 3, 4},
            {"r11d", 11, 3, 4},
            {"r12d", 12, 3, 4},
            {"r13d", 13, 3, 4},
            {"r14d", 14, 3, 4},
            {"r15d", 15, 3, 4},
    };
    for (uint32_t first = 0; first < count; first += 8)
    {
        const uint32_t left = count - first;
        const __mmask8 valid = left >= 8 ? 255 : (1u << left) - 1;
        const __m512i words = _mm512_maskz_loadu_epi64(valid, keys + first);
        const __m512i sizes = _mm512_maskz_loadu_epi64(valid, lengths + first);
        __m512i descriptions = _mm512_setzero_si512();
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(registers); r += 1)
        {
            uint64_t key;
            memcpy(&key, registers[r].name, sizeof(key));
            const uint64_t descriptor = X86_OPERAND_REGISTER | (uint64_t)registers[r].reg << 8 | (uint64_t)registers[r].class << 16 | (uint64_t)registers[r].width << 24;
            descriptions = _mm512_mask_mov_epi64(descriptions, _mm512_cmpeq_epi64_mask(words, _mm512_set1_epi64(key)), _mm512_set1_epi64(descriptor));
        }
        __mmask8 decimal = valid & _mm512_cmpgt_epi64_mask(sizes, _mm512_setzero_si512());
        __m512i numbers = _mm512_setzero_si512();
        for (uint32_t digit = 0; digit < 8; digit += 1)
        {
            const __mmask8 active = _mm512_cmpgt_epi64_mask(sizes, _mm512_set1_epi64(digit));
            const __m512i bytes = _mm512_and_epi64(_mm512_srlv_epi64(words, _mm512_set1_epi64(digit * 8)), _mm512_set1_epi64(255));
            const __m512i number = _mm512_sub_epi64(bytes, _mm512_set1_epi64('0'));
            decimal &= ~active | _mm512_cmple_epu64_mask(number, _mm512_set1_epi64(9));
            numbers = _mm512_mask_mov_epi64(numbers, active, _mm512_add_epi64(_mm512_mullo_epi64(numbers, _mm512_set1_epi64(10)), number));
        }
        // Leading-zero integers use GAS's octal rule and take the grammar instead of this decimal kernel.
        decimal &= ~(_mm512_cmpgt_epi64_mask(sizes, _mm512_set1_epi64(1)) & _mm512_cmpeq_epi64_mask(_mm512_and_epi64(words, _mm512_set1_epi64(255)), _mm512_set1_epi64('0')));
        descriptions = _mm512_mask_mov_epi64(descriptions, decimal, _mm512_set1_epi64(X86_OPERAND_IMMEDIATE));
        _mm512_mask_storeu_epi64(descriptors + first, valid, descriptions);
        _mm512_mask_storeu_epi64(values + first, valid, numbers);
    }
}

// Common Intel statements are classified in batches rather than calling the scalar operand grammar for every
// register and integer. The first uncommon row is retried by asm_instruction; no source is consumed speculatively.
// State-changing directives and labels delimit batches. Both paths produce exactly the same request/fragment stream.
static bool asm_parse_vector_batch(AsmParser* parser)
{
    uint32_t row_begin[16];
    uint32_t row_end[16];
    uint32_t operand_begin[17] = {0};
    uint64_t keys[64] = {0};
    uint64_t lengths[64] = {0};
    uint64_t descriptors[64];
    uint64_t values[64];
    bool negative[64] = {0};
    uint32_t rows = 0;
    uint32_t words = 0;
    uint32_t at = parser->at;
    bool available = parser->options.intel && !parser->prefix;
    while (available && rows < 16 && at < parser->token_count)
    {
        const uint32_t first = at;
        if (parser->tokens[at].kind != BUSTER_ASM_WORD || asm_token_text(parser, at).pointer[0] == '.')
        {
            break;
        }
        row_begin[rows] = at++;
        const uint32_t saved_words = words;
        uint32_t operands = 0;
        while (available && at < parser->token_count && parser->tokens[at].kind != '\n')
        {
            if (operands != 0)
            {
                available = parser->tokens[at++].kind == ',';
            }
            if (available && at < parser->token_count && operands < 4)
            {
                const bool sign = parser->tokens[at].kind == '-';
                at += sign;
                const String8 text = asm_token_text(parser, at);
                available = parser->tokens[at].kind == BUSTER_ASM_WORD && text.length <= 8;
                if (available)
                {
                    memcpy(&keys[words], text.pointer, text.length);
                    lengths[words] = text.length;
                    negative[words++] = sign;
                    at += 1;
                    operands += 1;
                }
            }
            else
            {
                available = false;
            }
        }
        if (available)
        {
            row_end[rows] = at;
            operand_begin[++rows] = words;
            at += at < parser->token_count;
        }
        else
        {
            words = saved_words;
            at = first;
        }
    }
    if (rows != 0)
    {
        asm_simple_words_vector(keys, lengths, words, descriptors, values);
    }
    uint32_t done = 0;
    for (uint32_t row = 0; row < rows; row += 1)
    {
        bool supported = true;
        for (uint32_t word = operand_begin[row]; word < operand_begin[row + 1]; word += 1)
        {
            supported &= descriptors[word] != 0 && (!negative[word] || (descriptors[word] & 255) == X86_OPERAND_IMMEDIATE);
        }
        const String8 mnemonic = asm_token_text(parser, row_begin[row]);
        // Canonical names only: aliases, branch symbols, width suffixes and other grammar keep their exact path.
        const uint32_t bucket = x86_match_hash(mnemonic);
        bool named = false;
        for (uint32_t candidate = parser->index->begin[bucket]; candidate < parser->index->begin[bucket + 1]; candidate += 1)
        {
            named |= x86_match_name(mnemonic, x86_form_descriptions[parser->index->forms[candidate]].mnemonic);
        }
        supported &= named && !x86_match_name(mnemonic, "sal");
        if (!supported || parser->instruction_count >= parser->instruction_capacity || parser->section == BUSTER_LINK_CLASS_BSS)
        {
            break;
        }
        const uint32_t location = parser->tokens[row_begin[row]].start;
        AsmInstruction* instruction = &parser->instructions[parser->instruction_count++];
        *instruction = (AsmInstruction){.location = location};
        instruction->request.mnemonic = mnemonic;
        instruction->request.count = operand_begin[row + 1] - operand_begin[row];
        for (uint32_t word = operand_begin[row]; word < operand_begin[row + 1]; word += 1)
        {
            const uint32_t operand = word - operand_begin[row];
            const uint64_t descriptor = descriptors[word];
            const bool immediate = (descriptor & 255) == X86_OPERAND_IMMEDIATE;
            const __int128 value = immediate ? negative[word] ? -(__int128)values[word] : values[word] : 0;
            instruction->request.operands[operand] = (X86Operand){
                .value = value,
                .width = (descriptor >> 24) & 255,
                .kind = descriptor & 255,
                .class = (descriptor >> 16) & 255,
                .reg = (descriptor >> 8) & 255,
            };
            instruction->expressions[operand].value = value;
        }
        instruction->fragment = asm_fragment(parser, ASM_INSTRUCTION, location);
        parser->fragments[instruction->fragment].instruction = parser->instruction_count - 1;
        parser->section_data[parser->section] = false;
        parser->at = row_end[row];
        parser->at += parser->at < parser->token_count;
        done += 1;
    }
    return done != 0;
}

static void asm_parse(AsmParser* parser)
{
    while (parser->at < parser->token_count && parser->result.error == 0)
    {
        if (parser->options.vectorized && parser->options.intel && !parser->prefix && asm_parse_vector_batch(parser))
        {
            continue;
        }
        if (asm_take(parser, '\n'))
        {
            continue;
        }
        while (parser->at + 1 < parser->token_count && parser->tokens[parser->at].kind == BUSTER_ASM_WORD && parser->tokens[parser->at + 1].kind == ':')
        {
            const uint32_t location = parser->tokens[parser->at].start;
            const String8 name = asm_token_text(parser, parser->at);
            uint32_t id;
            if (name.length == 1 && name.pointer[0] >= '0' && name.pointer[0] <= '9')
            {
                id = asm_numeric(parser, name.pointer[0] - '0', false, true, location);
            }
            else
            {
                id = asm_symbol(parser, name, location);
                if ((name.pointer[0] >= '0' && name.pointer[0] <= '9') || (name.length == 1 && name.pointer[0] == '.'))
                {
                    asm_fail(parser, location, "invalid label (numeric labels are 0 through 9)");
                }
            }
            if (parser->symbols[id].defined)
            {
                asm_fail(parser, location, "duplicate label definition");
            }
            parser->symbols[id].defined = true;
            parser->symbols[id].fragment = asm_fragment(parser, ASM_LABEL, location);
            parser->at += 2;
        }
        uint32_t end = parser->at;
        while (end < parser->token_count && parser->tokens[end].kind != '\n')
        {
            end += 1;
        }
        if (parser->at < end && parser->result.error == 0)
        {
            if (parser->tokens[parser->at].kind != BUSTER_ASM_WORD)
            {
                asm_fail(parser, parser->tokens[parser->at].start, "expected mnemonic, directive or label");
            }
            else if (asm_token_text(parser, parser->at).pointer[0] == '.')
            {
                asm_directive(parser, end);
            }
            else
            {
                asm_instruction(parser, end);
            }
        }
        parser->at = end;
    }
}

static AsmExpr asm_resolve(AsmParser* parser, AsmExpr expression, uint32_t location)
{
    if ((expression.plus != 0 && parser->symbols[expression.plus].absolute) || (expression.minus != 0 && parser->symbols[expression.minus].absolute))
    {
        asm_fail(parser, location, "constant used before its definition is unsupported");
    }
    if (expression.minus != 0)
    {
        const AsmSymbol* positive = &parser->symbols[expression.plus];
        const AsmSymbol* negative = &parser->symbols[expression.minus];
        if (expression.plus == 0 || !positive->defined || !negative->defined || positive->absolute || negative->absolute || parser->fragments[positive->fragment].section != parser->fragments[negative->fragment].section)
        {
            asm_fail(parser, location, "symbol difference requires two defined labels in the same section");
        }
        else
        {
            expression.value += (__int128)parser->fragments[positive->fragment].offset - parser->fragments[negative->fragment].offset;
            expression.plus = 0;
            expression.minus = 0;
        }
    }
    if (!asm_expr_range(expression.value))
    {
        asm_fail(parser, location, "expression overflow after layout");
    }
    return expression;
}

static bool asm_local_target(const AsmParser* parser, AsmExpr expression, uint8_t section)
{
    const AsmSymbol* symbol = &parser->symbols[expression.plus];
    return expression.plus != 0 && expression.minus == 0 && symbol->defined && !symbol->absolute && (symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL && parser->fragments[symbol->fragment].section == section;
}

static void asm_match_instructions(AsmParser* parser)
{
    for (uint32_t i = 0; i < parser->instruction_count && parser->result.error == 0; i += 1)
    {
        AsmInstruction* instruction = &parser->instructions[i];
        AsmFragment* fragment = &parser->fragments[instruction->fragment];
        for (uint32_t j = 0; j < instruction->request.count; j += 1)
        {
            if (instruction->expressions[j].plus != 0 && parser->symbols[instruction->expressions[j].plus].absolute)
            {
                asm_fail(parser, instruction->location, "constant used before its definition is unsupported");
            }
            if (instruction->expressions[j].minus != 0)
            {
                asm_fail(parser, instruction->location, "instruction operands require an absolute value or symbol plus addend; use data directives for label differences");
            }
        }
        const X86Match match = x86_match(parser->index, &instruction->request, parser->options.vectorized);
        if (!match.found)
        {
            asm_fail(parser, instruction->location, match.ambiguous ? "ambiguous memory width; specify a size" : "no supported value-preserving form for these operands and ISA");
        }
        else
        {
            instruction->record = match.instruction;
            fragment->size = match.length;
            const X86FormDescription* form = x86_form_description(instruction->record.form);
            instruction->branch = form->relative_bytes != 0;
            if (instruction->branch && instruction->expressions[0].plus == 0)
            {
                // GAS treats a literal as an absolute destination, not a displacement from this instruction.
                // The Link contract has no absolute-symbol target; require a label instead of changing its meaning.
                asm_fail(parser, instruction->location, "direct branch requires a label or symbol target; absolute destinations are unsupported");
            }
            instruction->local_branch = instruction->branch && asm_local_target(parser, instruction->expressions[0], fragment->section);
            if (instruction->branch && !instruction->local_branch && form->relative_bytes == 1)
            {
                const uint32_t row = instruction->record.form;
                const bool grows = row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4 && strcmp(form->mnemonic, x86_form_descriptions[row + 1].mnemonic) == 0;
                if (!grows)
                {
                    asm_fail(parser, instruction->location, "rel8-only branch requires a defined local target in its section");
                }
                else
                {
                    instruction->record.form += 1;
                    fragment->size += branch_sizing_growth(row);
                }
            }
        }
    }
}

// The compiler's existing branch sizer already has the right columnar AVX-512/scalar algorithms when assembly has
// only records and labels in one section. Adapt labels to record boundaries after proving that contract. Arbitrary
// data and alignment take the fragment adapter below; no instruction index is ever used as a byte position there.
static bool asm_layout_records(AsmParser* parser)
{
    bool eligible = parser->fragment_count != 0;
    const uint8_t section = parser->fragments[0].section;
    for (uint32_t i = 0; eligible && i < parser->fragment_count; i += 1)
    {
        const AsmFragment* fragment = &parser->fragments[i];
        eligible = fragment->section == section && (fragment->kind == ASM_LABEL || fragment->kind == ASM_INSTRUCTION);
    }
    for (uint32_t i = 0; eligible && i < parser->instruction_count; i += 1)
    {
        const AsmInstruction* instruction = &parser->instructions[i];
        if (instruction->local_branch)
        {
            const uint32_t row = instruction->record.form;
            eligible = instruction->expressions[0].value == 0 && x86_form_descriptions[row].relative_bytes == 1 && row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4;
        }
    }
    if (eligible)
    {
        const uint64_t saved = parser->arena->length;
        uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->fragment_count + 1);
        uint32_t* branches = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        uint32_t* targets = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->instruction_count + 1);
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(parser->arena, X86Instruction, parser->instruction_count + 1);
        if (positions == 0 || branches == 0 || targets == 0 || offsets == 0 || records == 0)
        {
            asm_fail(parser, 0, "arena exhausted adapting instruction layout");
        }
        else
        {
            uint32_t at = 0;
            for (uint32_t i = 0; i < parser->fragment_count; i += 1)
            {
                positions[i] = at;
                at += parser->fragments[i].kind == ASM_INSTRUCTION;
            }
            uint32_t count = 0;
            for (uint32_t i = 0; i < parser->instruction_count; i += 1)
            {
                const AsmInstruction* instruction = &parser->instructions[i];
                records[i] = instruction->record;
                if (instruction->local_branch)
                {
                    branches[count] = i;
                    targets[count++] = positions[parser->symbols[instruction->expressions[0].plus].fragment];
                }
            }
            const BranchSizingCheck check = branch_sizing_validate(records, parser->instruction_count, branches, targets, count);
            if (check.error != BRANCH_SIZING_OK)
            {
                asm_fail(parser, 0, "instruction layout adapter rejected its input");
            }
            else
            {
                const BranchSizing layout = parser->options.vectorized ? size_branches(records, parser->instruction_count, branches, targets, count, offsets) : size_branches_scalar(records, parser->instruction_count, branches, targets, count, offsets);
                if (layout.error != BRANCH_SIZING_OK || layout.length > BUSTER_ASM_MAX_SECTION)
                {
                    asm_fail(parser, 0, "instruction layout failed or exceeds section limit");
                }
                else
                {
                    offsets[parser->instruction_count] = layout.length;
                    parser->section_size[section] = layout.length;
                    parser->result.rounds = layout.rounds;
                    for (uint32_t i = 0; i < parser->instruction_count; i += 1)
                    {
                        parser->instructions[i].record = records[i];
                        parser->fragments[parser->instructions[i].fragment].size = offsets[i + 1] - offsets[i];
                    }
                    for (uint32_t i = 0; i < parser->fragment_count; i += 1)
                    {
                        parser->fragments[i].offset = offsets[positions[i]];
                    }
                }
            }
        }
        parser->arena->length = saved;
    }
    return eligible;
}

static __m512i asm_prefix16(__m512i values)
{
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_setr_epi32(0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_setr_epi32(0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7), values));
    return values;
}

// Alignment is an ordered location-counter operation. Between those operations, the vector path computes sixteen
// fragment offsets by a prefix network; the exact path is a plain sum. Partition by section once, so neither path
// scans unrelated sections in each relaxation round. Scalar column loads/stores avoid Zen 5's costly gather/scatter.
static void asm_fragment_offsets(AsmParser* parser, const uint32_t* order, const uint32_t* begin)
{
    for (uint32_t section = 1; section < BUSTER_LINK_CLASS_COUNT; section += 1)
    {
        uint32_t offset = 0;
        uint32_t at = begin[section];
        while (at < begin[section + 1] && parser->result.error == 0)
        {
            AsmFragment* fragment = &parser->fragments[order[at]];
            if (fragment->kind == ASM_ALIGN)
            {
                const uint32_t padding = -offset & (fragment->alignment - 1);
                fragment->size = padding <= fragment->maximum ? padding : 0;
                fragment->offset = offset;
                offset += fragment->size;
                at += 1;
            }
            else if (parser->options.vectorized)
            {
                uint32_t sizes[16] = {0};
                uint32_t offsets[16];
                uint32_t count = 0;
                uint64_t total = offset;
                while (count < 16 && at + count < begin[section + 1] && parser->fragments[order[at + count]].kind != ASM_ALIGN)
                {
                    sizes[count] = parser->fragments[order[at + count]].size;
                    total += sizes[count++];
                }
                const __m512i values = _mm512_loadu_si512(sizes);
                const __m512i prefix = asm_prefix16(values);
                _mm512_storeu_si512(offsets, _mm512_add_epi32(_mm512_sub_epi32(prefix, values), _mm512_set1_epi32(offset)));
                for (uint32_t i = 0; i < count; i += 1)
                {
                    parser->fragments[order[at + i]].offset = offsets[i];
                }
                if (total > BUSTER_ASM_MAX_SECTION)
                {
                    // Name the fragment that crosses the limit, as the exact path does, not the group's first.
                    uint64_t crossed = offset;
                    uint32_t i = 0;
                    while ((crossed += sizes[i]) <= BUSTER_ASM_MAX_SECTION)
                    {
                        i += 1;
                    }
                    asm_fail(parser, parser->fragments[order[at + i]].location, "section exceeds the 256 MiB layout limit");
                }
                offset = total;
                at += count;
            }
            else
            {
                fragment->offset = offset;
                offset += fragment->size;
                at += 1;
            }
            if (offset > BUSTER_ASM_MAX_SECTION)
            {
                asm_fail(parser, fragment->location, "section exceeds the 256 MiB layout limit");
            }
        }
        parser->section_size[section] = offset;
    }
}

static void asm_layout(AsmParser* parser)
{
    bool changed = !asm_layout_records(parser);
    const uint64_t saved = parser->arena->length;
    uint32_t* order = changed ? BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->fragment_count + 1) : 0;
    uint32_t begin[BUSTER_LINK_CLASS_COUNT + 1] = {0};
    if (changed && order == 0)
    {
        asm_fail(parser, 0, "arena exhausted partitioning layout fragments");
    }
    if (changed && order != 0)
    {
        for (uint32_t i = 0; i < parser->fragment_count; i += 1)
        {
            begin[parser->fragments[i].section + 1] += 1;
        }
        for (uint32_t s = 0; s < BUSTER_LINK_CLASS_COUNT; s += 1)
        {
            begin[s + 1] += begin[s];
        }
        uint32_t cursors[BUSTER_LINK_CLASS_COUNT];
        memcpy(cursors, begin, sizeof(cursors));
        for (uint32_t i = 0; i < parser->fragment_count; i += 1)
        {
            order[cursors[parser->fragments[i].section]++] = i;
        }
    }
    while (changed && parser->result.error == 0)
    {
        changed = false;
        parser->result.rounds += 1;
        asm_fragment_offsets(parser, order, begin);
        for (uint32_t i = 0; i < parser->instruction_count; i += 1)
        {
            AsmInstruction* instruction = &parser->instructions[i];
            if (instruction->local_branch)
            {
                AsmFragment* fragment = &parser->fragments[instruction->fragment];
                const AsmExpr expression = instruction->expressions[0];
                const uint32_t target = parser->fragments[parser->symbols[expression.plus].fragment].offset;
                const __int128 distance = (__int128)target + expression.value - fragment->offset - fragment->size;
                const uint32_t row = instruction->record.form;
                const X86FormDescription* form = x86_form_description(row);
                if (form->relative_bytes == 1 && (distance < -128 || distance > 127) && row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4 && strcmp(form->mnemonic, x86_form_descriptions[row + 1].mnemonic) == 0)
                {
                    instruction->record.form += 1;
                    fragment->size += branch_sizing_growth(row);
                    changed = true;
                }
            }
        }
    }
    parser->arena->length = saved;
    for (uint32_t i = 0; i < parser->instruction_count && parser->result.error == 0; i += 1)
    {
        AsmInstruction* instruction = &parser->instructions[i];
        if (instruction->local_branch)
        {
            const AsmFragment* fragment = &parser->fragments[instruction->fragment];
            const AsmExpr expression = instruction->expressions[0];
            const uint32_t target = parser->fragments[parser->symbols[expression.plus].fragment].offset;
            const __int128 distance = (__int128)target + expression.value - fragment->offset - fragment->size;
            const uint32_t width = x86_form_description(instruction->record.form)->relative_bytes;
            if (distance < INT32_MIN || distance > INT32_MAX || (width == 1 && (distance < -128 || distance > 127)))
            {
                asm_fail(parser, instruction->location, "branch displacement is out of range");
            }
            else
            {
                instruction->record.displacement = (int32_t)distance;
            }
        }
        if (x86_target_validate(parser->index->allowed, &instruction->record, 1) != 1)
        {
            asm_fail(parser, instruction->location, "resolved instruction is invalid for the target");
        }
    }
}

static void asm_data_value(AsmParser* parser, AtomId atom, AsmFragment* fragment)
{
    AsmExpr expression = asm_resolve(parser, fragment->expression, fragment->location);
    uint8_t* bytes = parser->result.link.atoms.bytes[atom];
    if (expression.plus == 0)
    {
        const __int128 low = -((__int128)1 << (fragment->size * 8 - 1));
        const __int128 high = ((__int128)1 << (fragment->size * 8)) - 1;
        if (expression.value < low || expression.value > high)
        {
            asm_fail(parser, fragment->location, "data value overflows its field");
        }
        else if (bytes != 0)
        {
            const uint64_t value = expression.value;
            memcpy(bytes + fragment->offset, &value, fragment->size);
        }
        else if (expression.value != 0)
        {
            asm_fail(parser, fragment->location, ".bss may contain only zero fill");
        }
    }
    else if (bytes == 0 || (fragment->size != 4 && fragment->size != 8) || expression.value < INT64_MIN || expression.value > INT64_MAX)
    {
        asm_fail(parser, fragment->location, "symbol relocation needs a 32/64-bit data field and a signed 64-bit addend");
    }
    else
    {
        const uint8_t kind = fragment->size == 4 ? BUSTER_LINK_FIXUP_ABS32 : BUSTER_LINK_FIXUP_ABS64;
        memset(bytes + fragment->offset, 0, fragment->size);
        link_add_fixup(&parser->result.link, atom, fragment->offset, kind, parser->symbols[expression.plus].link, (int64_t)expression.value);
    }
}

static void asm_instruction_fixups(AsmParser* parser, AtomId atom, AsmInstruction* instruction)
{
    const AsmFragment* fragment = &parser->fragments[instruction->fragment];
    const X86FormDescription* form = x86_form_description(instruction->record.form);
    // Displacement precedes immediate in the bytes, independently of the syntax's operand order.
    for (uint32_t field = X86_FIXUP_DISPLACEMENT; field <= X86_FIXUP_IMMEDIATE; field += 1)
    {
        for (uint32_t i = 0; i < instruction->request.count; i += 1)
        {
            const X86Operand* operand = &instruction->request.operands[i];
            const bool displacement = operand->kind == X86_OPERAND_MEMORY || instruction->branch;
            const AsmExpr expression = instruction->expressions[i];
            if (expression.plus == 0 || (field == X86_FIXUP_DISPLACEMENT) != displacement || instruction->local_branch)
            {
                continue;
            }
            const uint32_t distance = x86_fixup_distance(instruction->record.form, field);
            const uint32_t offset = fragment->offset + fragment->size - distance;
            uint8_t* bytes = parser->result.link.atoms.bytes[atom] + offset;
            const bool pc_relative = instruction->branch || (operand->mode & BUSTER_X86_MODE_RIP);
            const uint32_t width = displacement ? 4 : form->immediate_bytes;
            if (pc_relative && !instruction->branch && asm_local_target(parser, expression, fragment->section))
            {
                const __int128 value = (__int128)parser->fragments[parser->symbols[expression.plus].fragment].offset + expression.value - fragment->offset - fragment->size;
                if (value < INT32_MIN || value > INT32_MAX)
                {
                    asm_fail(parser, instruction->location, "RIP-relative displacement out of range");
                }
                else
                {
                    const int32_t narrowed = value;
                    memcpy(bytes, &narrowed, 4);
                }
            }
            else
            {
                const __int128 addend = expression.value - (pc_relative ? distance : 0);
                if (addend < INT64_MIN || addend > INT64_MAX || (width != 4 && width != 8))
                {
                    asm_fail(parser, instruction->location, "unrepresentable instruction relocation");
                }
                else
                {
                    // A disp32 and an immediate the form sign-extends (REX.W's, and PUSH's) take 32S, other imm32s 32.
                    const bool signed_field = displacement || x86_match_sign_extends(form);
                    const uint8_t kind = instruction->branch ? BUSTER_LINK_FIXUP_CALL : pc_relative ? BUSTER_LINK_FIXUP_PC32
                                                                                    : width == 8    ? BUSTER_LINK_FIXUP_ABS64
                                                                                    : signed_field  ? BUSTER_LINK_FIXUP_ABS32S
                                                                                                    : BUSTER_LINK_FIXUP_ABS32;
                    memset(bytes, 0, width);
                    link_add_fixup(&parser->result.link, atom, offset, kind, parser->symbols[expression.plus].link, addend);
                }
            }
        }
    }
}

// GNU as's code padding (binutils 2.47, 64-bit code): after data, a one-byte nop first; past seven NOPs' worth, a jump
// over the rest (rel8 while it reaches); then the remainder's NOP and 11-byte ones. Executed padding before an aligned
// loop head then costs a few instructions instead of one per byte, and objects compare with GNU as's byte for byte.
static void asm_code_padding(uint8_t* bytes, uint32_t count, bool after_data)
{
    static const uint8_t nops[11][11] =
        {
            {0x90},
            {0x66, 0x90},
            {0x0f, 0x1f, 0x00},
            {0x0f, 0x1f, 0x40, 0x00},
            {0x0f, 0x1f, 0x44, 0x00, 0x00},
            {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
            {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
            {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
            {0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    uint32_t at = 0;
    if (after_data && count != 0)
    {
        bytes[at++] = 0x90;
        count -= 1;
    }
    if (count / 11 > 7)
    {
        if (count - 2 <= 127)
        {
            count -= 2;
            bytes[at++] = 0xeb;
            bytes[at++] = (uint8_t)count;
        }
        else
        {
            count -= 5;
            bytes[at++] = 0xe9;
            memcpy(bytes + at, &count, 4);
            at += 4;
        }
    }
    const uint32_t first = count % 11;
    if (first != 0)
    {
        memcpy(bytes + at, nops[first - 1], first);
        at += first;
    }
    for (uint32_t i = first; i < count; i += 11)
    {
        memcpy(bytes + at, nops[10], 11);
        at += 11;
    }
}

static void asm_emit(AsmParser* parser)
{
    Link* link = &parser->result.link;
    const LinkCapacity capacity =
        {
            .names = parser->symbol_count,
            .atoms = BUSTER_LINK_CLASS_COUNT,
            .symbols = parser->symbol_count,
            .fixups = parser->fragment_count + parser->instruction_count * 2,
            .wide_addends = parser->fragment_count + parser->instruction_count * 2,
    };
    if (!link_initialize(link, parser->arena, parser->arena, capacity))
    {
        asm_fail(parser, 0, "arena exhausted building link tables");
    }
    AtomId atoms[BUSTER_LINK_CLASS_COUNT] = {0};
    for (uint32_t f = 0; f < parser->fragment_count && parser->result.error == 0; f += 1)
    {
        const uint8_t section = parser->fragments[f].section;
        if (atoms[section] == 0)
        {
            const uint32_t size = parser->section_size[section];
            // The trusted encoder requires worst-case capacity plus its documented store slack even when the
            // chosen encodings are shorter. Keep that padding outside the logical atom size, including the tail.
            uint8_t* bytes = section == BUSTER_LINK_CLASS_BSS ? 0 : arena_push(parser->arena, (uint64_t)size + BUSTER_X86_ENCODER_CAPACITY(256), 64);
            if (bytes == 0 && section != BUSTER_LINK_CLASS_BSS)
            {
                asm_fail(parser, parser->fragments[f].location, "arena exhausted allocating section");
            }
            else
            {
                if (bytes != 0)
                {
                    memset(bytes, 0, size);
                }
                atoms[section] = link_add_atom(link, section, parser->section_alignment[section], size, bytes, 0);
            }
        }
    }
    for (uint32_t i = 1; i < parser->symbol_count && parser->result.error == 0; i += 1)
    {
        AsmSymbol* symbol = &parser->symbols[i];
        if (symbol->absolute)
        {
            if (symbol->explicit_binding || symbol->has_size)
            {
                asm_fail(parser, symbol->location, "exported absolute symbols are not supported by the shared object writer");
            }
            continue;
        }
        if (!symbol->defined && (symbol->internal || (symbol->name.length >= 2 && memcmp(symbol->name.pointer, ".L", 2) == 0) || (symbol->explicit_binding && (symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL)))
        {
            asm_fail(parser, symbol->location, "undefined local symbol");
            continue;
        }
        if (!symbol->defined && !symbol->explicit_binding)
        {
            symbol->flags |= BUSTER_LINK_SYMBOL_GLOBAL;
        }
        const AsmFragment* definition = &parser->fragments[symbol->fragment];
        AsmExpr size = symbol->has_size ? asm_resolve(parser, symbol->size, symbol->location) : (AsmExpr){0};
        if (size.plus || size.minus || size.value < 0 || size.value > UINT64_MAX)
        {
            asm_fail(parser, symbol->location, "symbol size must resolve to a nonnegative absolute value");
        }
        const NameId name = !symbol->internal && symbol->name.length != 0 ? link_add_name(link, (const uint8_t*)symbol->name.pointer, symbol->name.length) : 0;
        symbol->link = link_add_symbol(link, name, symbol->defined ? atoms[definition->section] : 0, symbol->defined ? definition->offset : 0, size.value, symbol->flags, symbol->defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE);
    }
    for (uint32_t section = 1; section < BUSTER_LINK_CLASS_COUNT && parser->result.error == 0; section += 1)
    {
        const AtomId atom = atoms[section];
        if (atom == 0)
        {
            continue;
        }
        for (uint32_t f = 0; f < parser->fragment_count && parser->result.error == 0; f += 1)
        {
            AsmFragment* fragment = &parser->fragments[f];
            if (fragment->section != section)
            {
                continue;
            }
            uint8_t* bytes = link->atoms.bytes[atom];
            if (fragment->kind == ASM_VALUE)
            {
                asm_data_value(parser, atom, fragment);
            }
            else if (fragment->kind == ASM_INSTRUCTION)
            {
                X86Instruction records[256];
                uint32_t fragments[256];
                uint32_t count = 0;
                uint32_t size = 0;
                uint32_t next = f;
                while (next < parser->fragment_count && count < 256)
                {
                    AsmFragment* item = &parser->fragments[next];
                    if (item->section != section || (item->kind != ASM_INSTRUCTION && item->kind != ASM_LABEL))
                    {
                        break;
                    }
                    if (item->kind == ASM_INSTRUCTION)
                    {
                        fragments[count] = next;
                        records[count++] = parser->instructions[item->instruction].record;
                        size += item->size;
                    }
                    next += 1;
                }
                const uint64_t capacity = link->atoms.size[atom] - fragment->offset + BUSTER_X86_ENCODER_CAPACITY(256);
                const int64_t encoded = parser->options.vectorized ? x86_encode(records, count, bytes + fragment->offset, capacity, 0, 0) : x86_encode_scalar(records, count, bytes + fragment->offset, capacity, 0, 0);
                if (encoded != size)
                {
                    asm_fail(parser, fragment->location, "encoder length disagrees with assembly layout");
                }
                else
                {
                    for (uint32_t j = 0; j < count; j += 1)
                    {
                        asm_instruction_fixups(parser, atom, &parser->instructions[parser->fragments[fragments[j]].instruction]);
                    }
                }
                f = next - 1;
            }
            else if (fragment->kind == ASM_BYTES)
            {
                if (bytes == 0)
                {
                    for (uint32_t j = 0; j < fragment->size; j += 1)
                    {
                        if (fragment->bytes[j] != 0)
                        {
                            asm_fail(parser, fragment->location, ".bss may contain only zero fill");
                        }
                    }
                }
                else
                {
                    memcpy(bytes + fragment->offset, fragment->bytes, fragment->size);
                }
            }
            else if (fragment->kind == ASM_FILL || fragment->kind == ASM_ALIGN)
            {
                if (bytes != 0 && fragment->padding != ASM_PADDING_FILL)
                {
                    asm_code_padding(bytes + fragment->offset, fragment->size, fragment->padding == ASM_PADDING_NOPS_AFTER_DATA);
                }
                else if (bytes != 0)
                {
                    memset(bytes + fragment->offset, fragment->fill, fragment->size);
                }
                else if (fragment->fill != 0 && fragment->size != 0)
                {
                    asm_fail(parser, fragment->location, ".bss may contain only zero fill");
                }
            }
        }
    }
    if (parser->result.error == 0 && link_check(link) != LINK_ERROR_NONE)
    {
        asm_fail(parser, 0, "invalid link representation (symbol size, capacity or relocation)");
    }
}

static AsmResult asm_assemble(String8 source, const X86MatchIndex* index, AsmOptions options, Arena* arena)
{
    AsmParser parser =
        {
            .source = source,
            .arena = arena,
            .index = index,
            .options = options,
            .section = BUSTER_LINK_CLASS_TEXT,
            .prefix = !options.intel,
            .symbol_count = 1,
    };
    const uint64_t saved = arena->length;
    const uint64_t start = asm_clock();
    if (source.length > BUSTER_ASM_MAX_SOURCE || (source.length != 0 && source.pointer == 0))
    {
        asm_fail(&parser, 0, "invalid source or source exceeds the 16 MiB limit");
    }
    else
    {
        parser.tokens = BUSTER_ARENA_PUSH_ARRAY(arena, AsmToken, source.length + 4);
        if (parser.tokens == 0)
        {
            asm_fail(&parser, 0, "arena exhausted lexing source");
        }
        else
        {
            asm_lex(&parser);
            // Lexing may emit one token per byte, but retain only the actual records and four lookahead sentinels.
            // Every lexer store initializes the full record. No full-source clearing or unused-token working set.
            memset(parser.tokens + parser.token_count, 0, 4 * sizeof(AsmToken));
            parser.tokens[parser.token_count].start = source.length;
            parser.tokens[parser.token_count].end = source.length;
            arena->length = (uint8_t*)(parser.tokens + parser.token_count + 4) - arena->base;
        }
    }
    const uint64_t lexed = asm_clock();
    if (parser.result.error == 0)
    {
        parser.capacity = parser.token_count + 4;
        uint32_t slots = 16;
        while (slots < parser.capacity * 2)
        {
            slots *= 2;
        }
        parser.symbol_mask = slots - 1;
        parser.symbol_slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
        parser.symbols = BUSTER_ARENA_PUSH_ARRAY(arena, AsmSymbol, parser.capacity);
        parser.fragments = BUSTER_ARENA_PUSH_ARRAY(arena, AsmFragment, parser.capacity);
        parser.instructions = BUSTER_ARENA_PUSH_ARRAY(arena, AsmInstruction, parser.instruction_capacity);
        if (parser.symbol_slots == 0 || parser.symbols == 0 || parser.fragments == 0 || parser.instructions == 0)
        {
            asm_fail(&parser, 0, "arena exhausted allocating parser tables");
        }
        else
        {
            memset(parser.symbol_slots, 0, slots * sizeof(uint32_t));
            parser.symbols[0] = (AsmSymbol){0};
            asm_fragment(&parser, ASM_LABEL, 0);
            asm_parse(&parser);
        }
    }
    const uint64_t parsed = asm_clock();
    if (parser.result.error == 0)
    {
        asm_match_instructions(&parser);
    }
    const uint64_t matched = asm_clock();
    if (parser.result.error == 0)
    {
        asm_layout(&parser);
    }
    const uint64_t laid_out = asm_clock();
    if (parser.result.error == 0)
    {
        asm_emit(&parser);
    }
    const uint64_t emitted = asm_clock();
    parser.result.lex_ns = lexed - start;
    parser.result.parse_ns = parsed - lexed;
    parser.result.match_ns = matched - parsed;
    parser.result.layout_ns = laid_out - matched;
    parser.result.emit_ns = emitted - laid_out;
    parser.result.instructions = parser.instruction_count;
    if (parser.result.error != 0)
    {
        arena->length = saved;
        parser.result.link = (Link){0};
    }
    return parser.result;
}
