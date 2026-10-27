// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU assembly frontend. asm_assemble consumes a bounded source slice and caller-owned arena, returning the shared
// Link representation. No C parser, subprocess, allocation per record, or modification of the final encoder record.
// Layout map: asm_lex; symbols and expressions; operands; directives/statements; matching; fragment layout; emission;
// asm_assemble_code, a source's records for code that another function's records hold (C's inline assembly, #147).
// The public boundary rejects unsupported syntax and validates records before trusted encoding. Layout only promotes
// branches: each can grow once, alignment is recomputed every round, and a final fixed-layout check covers rel8-only
// instructions. This terminates after at most branch_count + 1 rounds even when alignment absorbs earlier growth.

#define BUSTER_ASM_MAX_SOURCE (16u << 20)
#define BUSTER_ASM_MAX_SECTION (256u << 20)
#define BUSTER_ASM_EXPR_DEPTH 128
#define BUSTER_ASM_WORD 256
#define BUSTER_ASM_STRING 257
#define BUSTER_ASM_MAX_SECTIONS 65536

typedef struct AsmToken AsmToken;
struct AsmToken
{
    uint32_t start;
    uint32_t end;
    uint32_t kind;
    uint32_t reserved; // Zero after lexing; the vector parser caches bounded register classification here.
};

typedef struct AsmExpr AsmExpr;
struct AsmExpr
{
    __int128 value;
    uint32_t plus;
    uint32_t minus;
    // Intel addresses keep register terms through parentheses. These fields occupy the record's former padding.
    uint8_t registers[2]; // Architectural GPR number plus one; 17 is RIP.
    uint8_t scales[2];
    uint8_t register_count;
    uint8_t indexed; // Multiplication explicitly chooses an index, including a scale of one.
};

enum
{
    ASM_EXPR_SHL = 300,
    ASM_EXPR_SHR,
    ASM_EXPR_EQ,
    ASM_EXPR_NE,
    ASM_EXPR_LE,
    ASM_EXPR_GE,
    ASM_EXPR_AND,
    ASM_EXPR_OR,
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
    uint32_t section;
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
    String8 source_name;
    ObjectOptions object_options;
    const char* error;
    uint32_t location;
    uint32_t rounds;
    uint32_t instructions;
    uint64_t lex_ns;
    uint64_t parse_ns;
    uint64_t match_ns;
    uint64_t layout_ns;
    uint64_t emit_ns;
    uint64_t cfi_ns;
};

typedef struct AsmSection AsmSection;
struct AsmSection
{
    ObjectAtomSection object;
    uint32_t size;
    AtomId atom;
    uint8_t class;
    uint8_t alignment;
    bool data;
    bool declared;
};

typedef struct AsmParser AsmParser;
typedef struct AsmCfiEvent AsmCfiEvent;
struct AsmCfiEvent
{
    uint32_t fragment;
    uint32_t opcode;
    int64_t first;
    int64_t second;
    uint8_t* bytes;
    uint32_t size;
};

typedef struct AsmCfiProcedure AsmCfiProcedure;
struct AsmCfiProcedure
{
    uint32_t symbol;
    uint32_t end;
    uint32_t first;
    uint32_t count;
    uint32_t return_column;
    uint32_t initial_count;
    uint32_t relocation;
    bool simple;
    bool signal;
};

typedef struct AsmCfiState AsmCfiState;
struct AsmCfiState
{
    int64_t offset;
    uint32_t reg;
};

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
    uint32_t* fragment_order;
    uint32_t* fragment_begin;
    uint32_t instruction_count;
    uint32_t numeric[10];
    AsmSection* sections;
    uint32_t* section_slots;
    uint32_t section_mask;
    uint32_t section_count;
    uint32_t section_capacity;
    uint32_t section;
    AsmCfiEvent* cfi;
    AsmCfiProcedure* procedures;
    AsmCfiState* cfi_stack;
    AsmCfiState cfa;
    uint32_t cfi_count;
    uint32_t procedure_count;
    uint32_t cfi_depth;
    bool cfi_active;
    bool prefix;
    // asm_assemble_code's source: code another function's records hold, which takes no directive but the syntax's.
    bool code;
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

static bool asm_is_case(const AsmParser* parser, uint32_t token, const char* name)
{
    const String8 text = asm_token_text(parser, token);
    bool equal = text.length == strlen(name);
    for (uint32_t i = 0; equal && i < text.length; i += 1)
    {
        const uint8_t byte = text.pointer[i];
        const uint8_t lower = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
        equal = lower == (uint8_t)name[i];
    }
    return equal;
}

static bool asm_name_token(const AsmParser* parser, uint32_t token)
{
    return parser->tokens[token].kind == BUSTER_ASM_WORD || parser->tokens[token].kind == BUSTER_ASM_STRING;
}

static String8 asm_name_text(AsmParser* parser, uint32_t token)
{
    String8 name = asm_token_text(parser, token);
    if (parser->tokens[token].kind == BUSTER_ASM_STRING)
    {
        name.pointer += 1;
        name.length -= 2;
        // GCC's ordinary quoted identifiers need no copy. Decode escapes only when the spelling contains one.
        if (memchr(name.pointer, '\\', name.length) != 0)
        {
            char* bytes = arena_push(parser->arena, name.length, 1);
            if (bytes == 0)
            {
                asm_fail(parser, parser->tokens[token].start, "arena exhausted decoding a symbol");
            }
            else
            {
                uint32_t count = 0;
                for (uint32_t i = 0; i < name.length; i += 1)
                {
                    if (name.pointer[i] == '\\' && i + 1 < name.length)
                    {
                        i += 1;
                        if (name.pointer[i] != '\\' && name.pointer[i] != '"')
                        {
                            asm_fail(parser, parser->tokens[token].start, "quoted names support only quote and backslash escapes");
                        }
                    }
                    bytes[count++] = name.pointer[i];
                }
                name = (String8){.pointer = bytes, .length = count};
            }
        }
        if (memchr(name.pointer, 0, name.length) != 0)
        {
            asm_fail(parser, parser->tokens[token].start, "a name cannot contain NUL");
        }
    }
    return name;
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
    if (parser->section == BUSTER_LINK_CLASS_NONE)
    {
        asm_fail(parser, location, ".note.GNU-stack must be empty");
    }
    else if (parser->fragment_count >= parser->capacity)
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

static uint32_t asm_symbol_scoped(AsmParser* parser, String8 name, uint32_t location, bool internal)
{
    if (name.length == 0 || (name.pointer[0] >= '0' && name.pointer[0] <= '9') || (name.length == 1 && name.pointer[0] == '.'))
    {
        asm_fail(parser, location, "invalid symbol name");
    }
    uint32_t slot = (link_hash((const uint8_t*)name.pointer, name.length) ^ (internal ? 0x9e3779b9u : 0)) & parser->symbol_mask;
    uint32_t result = parser->symbol_slots[slot];
    while (result != 0 && (parser->symbols[result].internal != internal || parser->symbols[result].name.length != name.length || memcmp(parser->symbols[result].name.pointer, name.pointer, name.length) != 0))
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
            parser->symbols[result] = (AsmSymbol){.name = name, .location = location, .internal = internal};
            parser->symbol_slots[slot] = result;
        }
    }
    return result;
}

static uint32_t asm_symbol(AsmParser* parser, String8 name, uint32_t location)
{
    return asm_symbol_scoped(parser, name, location, false);
}

static uint32_t asm_numeric(AsmParser* parser, uint32_t digit, bool forward, bool definition, uint32_t location)
{
    if (definition)
    {
        parser->numeric[digit] += 1;
    }
    const uint32_t serial = parser->numeric[digit] + forward;
    char name[48];
    // The internal namespace is distinct even from a quoted source name with exactly this spelling.
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
        result = asm_symbol_scoped(parser, (String8){.pointer = copy, .length = length}, location, true);
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
        if ((op == '-' && right.register_count != 0) || left.register_count + right.register_count > 2)
        {
            asm_fail(parser, location, "invalid address register expression");
        }
        else
        {
            for (uint32_t i = 0; i < left.register_count; i += 1)
            {
                result.registers[result.register_count] = left.registers[i];
                result.indexed |= ((left.indexed >> i) & 1u) << result.register_count;
                result.scales[result.register_count++] = left.scales[i];
            }
            for (uint32_t i = 0; i < right.register_count; i += 1)
            {
                result.registers[result.register_count] = right.registers[i];
                result.indexed |= ((right.indexed >> i) & 1u) << result.register_count;
                result.scales[result.register_count++] = right.scales[i];
            }
        }
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
    else if (left.register_count != 0 || right.register_count != 0)
    {
        const AsmExpr registers = left.register_count != 0 ? left : right;
        const AsmExpr factor = left.register_count != 0 ? right : left;
        if (op != '*' || factor.register_count != 0 || factor.value < 1 || factor.value > 8)
        {
            asm_fail(parser, location, "invalid address register expression");
        }
        else
        {
            result = registers;
            result.value *= factor.value;
            result.indexed = (1u << result.register_count) - 1;
            for (uint32_t i = 0; i < result.register_count; i += 1)
            {
                const uint32_t scale = result.scales[i] * (uint32_t)factor.value;
                if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
                {
                    asm_fail(parser, location, "address scale must be 1, 2, 4 or 8");
                }
                result.scales[i] = scale;
            }
        }
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
        case '!':
            result.value = (int64_t)((uint64_t)left.value | ~(uint64_t)right.value);
            break;
        case ASM_EXPR_SHL:
            result.value = right.value >= 0 && right.value < 64 ? (int64_t)((uint64_t)left.value << (uint32_t)right.value) : 0;
            break;
        case ASM_EXPR_SHR:
            result.value = right.value >= 0 && right.value < 64 ? (int64_t)((uint64_t)left.value >> (uint32_t)right.value) : 0;
            break;
        case ASM_EXPR_EQ:
            result.value = -((uint64_t)left.value == (uint64_t)right.value);
            break;
        case ASM_EXPR_NE:
            result.value = -((uint64_t)left.value != (uint64_t)right.value);
            break;
        case '<':
            result.value = -((int64_t)left.value < (int64_t)right.value);
            break;
        case '>':
            result.value = -((int64_t)left.value > (int64_t)right.value);
            break;
        case ASM_EXPR_LE:
            result.value = -((int64_t)left.value <= (int64_t)right.value);
            break;
        case ASM_EXPR_GE:
            result.value = -((int64_t)left.value >= (int64_t)right.value);
            break;
        case ASM_EXPR_AND:
            result.value = left.value != 0 && right.value != 0;
            break;
        case ASM_EXPR_OR:
            result.value = left.value != 0 || right.value != 0;
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
    // GAS shifts share multiplication's precedence; its bitwise operators bind more tightly than addition.
    return op == '*' || op == '/' || op == '%' || op == ASM_EXPR_SHL || op == ASM_EXPR_SHR ? 6 :
        op == '&' || op == '|' || op == '^' || op == '!' ? 5 :
        op == '+' || op == '-' ? 4 :
        op == '<' || op == '>' || op == ASM_EXPR_EQ || op == ASM_EXPR_NE || op == ASM_EXPR_LE || op == ASM_EXPR_GE ? 3 :
        op == ASM_EXPR_AND ? 2 : op == ASM_EXPR_OR ? 1 : 0;
}

static uint16_t asm_expression_operator(const AsmParser* parser, uint32_t end, uint32_t* width)
{
    const AsmToken* token = &parser->tokens[parser->at];
    uint16_t op = token->kind;
    *width = 1;
    if (parser->at + 1 < end && token->end == token[1].start)
    {
        const uint32_t pair = op | token[1].kind << 8;
        op = pair == ('<' | '<' << 8) ? ASM_EXPR_SHL : pair == ('>' | '>' << 8) ? ASM_EXPR_SHR :
            pair == ('=' | '=' << 8) ? ASM_EXPR_EQ : pair == ('!' | '=' << 8) || pair == ('<' | '>' << 8) ? ASM_EXPR_NE :
            pair == ('<' | '=' << 8) ? ASM_EXPR_LE : pair == ('>' | '=' << 8) ? ASM_EXPR_GE :
            pair == ('&' | '&' << 8) ? ASM_EXPR_AND : pair == ('|' | '|' << 8) ? ASM_EXPR_OR : op;
        *width = op >= ASM_EXPR_SHL && op <= ASM_EXPR_OR ? 2 : 1;
    }
    return op;
}

static AsmExpr asm_unary(AsmParser* parser, AsmExpr value, uint16_t op, uint32_t location)
{
    if (op == '-')
    {
        value = asm_combine(parser, (AsmExpr){0}, value, '-', location);
    }
    else if (op == '~' || op == '!')
    {
        if (value.plus || value.minus || value.register_count)
        {
            asm_fail(parser, location, "not requires an absolute operand");
        }
        value.value = op == '~' ? (int64_t)~(uint64_t)value.value : value.value == 0;
    }
    return value;
}

static bool asm_token_register(const AsmParser* parser, uint32_t at, X86Operand* operand);

static AsmExpr asm_expression_mode(AsmParser* parser, uint32_t end, bool address)
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
            if (token.kind == '+' || token.kind == '-' || token.kind == '~' || token.kind == '!')
            {
                operators[operator_count++] = token.kind | 0x8000;
                parser->at += 1;
            }
            else if (token.kind == '(')
            {
                operators[operator_count++] = '(';
                parser->at += 1;
            }
            else if (asm_name_token(parser, parser->at) || (address && parser->prefix && token.kind == '%' && parser->at + 1 < end && parser->tokens[parser->at + 1].kind == BUSTER_ASM_WORD))
            {
                const bool prefixed = token.kind == '%';
                parser->at += prefixed;
                const uint32_t name_at = parser->at++;
                const String8 text = asm_name_text(parser, name_at);
                AsmExpr value = {0};
                if (token.kind == BUSTER_ASM_WORD && text.length == 2 && text.pointer[0] >= '0' && text.pointer[0] <= '9' && (text.pointer[1] == 'b' || text.pointer[1] == 'f'))
                {
                    value.plus = asm_numeric(parser, text.pointer[0] - '0', text.pointer[1] == 'f', false, token.start);
                }
                else if (token.kind == BUSTER_ASM_WORD && text.pointer[0] >= '0' && text.pointer[0] <= '9')
                {
                    if (!asm_number(text, &value.value))
                    {
                        asm_fail(parser, token.start, "invalid or overflowing integer");
                    }
                }
                else
                {
                    // Without register prefixes, a register's name is never a symbol: GNU as refuses it in an expression
                    // (`.quad rsi`, `dword ptr eax`, `[rax + -rsi]`) rather than reading it as an external. Register
                    // names are at most five bytes, which keeps the check off the longer symbol names.
                    X86Operand reg;
                    const bool is_register = parser->options.intel && prefixed == parser->prefix && token.kind != BUSTER_ASM_STRING && text.length <= 5 && asm_token_register(parser, name_at, &reg);
                    if (is_register)
                    {
                        if (!address || (reg.class != BUSTER_X86_CLASS_GPR64 && reg.class != 255))
                        {
                            asm_fail(parser, token.start, "invalid use of register");
                        }
                        value.register_count = 1;
                        value.registers[0] = reg.class == 255 ? 17 : reg.reg + 1;
                        value.scales[0] = 1;
                    }
                    else
                    {
                        if (prefixed)
                        {
                            asm_fail(parser, token.start, "expected address register");
                        }
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
            uint32_t operator_width;
            const uint16_t binary = asm_expression_operator(parser, end, &operator_width);
            const uint32_t precedence = asm_precedence(binary);
            if (!close && precedence == 0)
            {
                break;
            }
            while (operator_count != 0 && operators[operator_count - 1] != '(' && (close || (operators[operator_count - 1] & 0x8000) || asm_precedence(operators[operator_count - 1]) >= precedence))
            {
                const uint16_t op = operators[--operator_count];
                if (op & 0x8000)
                {
                    values[value_count - 1] = asm_unary(parser, values[value_count - 1], op & 0x7fff, location);
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
                operators[operator_count++] = binary;
                parser->at += operator_width;
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
            values[value_count - 1] = asm_unary(parser, values[value_count - 1], op & 0x7fff, location);
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

static AsmExpr asm_expression(AsmParser* parser, uint32_t end)
{
    return asm_expression_mode(parser, end, false);
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

#include "assembler_words.c"

// A register's name as GNU as reads it: in any case (RAX is rax), with canonical numbers (r8 and xmm0, but r08, xmm00
// and k00 are symbols). No name is longer than five bytes (xmm31), so a longer spelling is never a register.
static bool asm_register(String8 spelling, X86Operand* operand)
{
    char lower[5];
    for (uint32_t i = 0; i < spelling.length && i < sizeof(lower); i += 1)
    {
        const char byte = spelling.pointer[i];
        lower[i] = byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
    }
    const String8 name = {.pointer = lower, .length = spelling.length <= sizeof(lower) ? spelling.length : 0};
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
            found = at > start && at == name.length && (name.pointer[start] != '0' || at == start + 1) && reg < (class == BUSTER_X86_CLASS_OPMASK ? 8u : class <= BUSTER_X86_CLASS_GPR64 ? 16u
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

static bool asm_token_register(const AsmParser* parser, uint32_t at, X86Operand* operand)
{
    const uint32_t cached = parser->tokens[at].reserved;
    bool found;
    if (parser->options.vectorized && (cached & BUSTER_ASM_REGISTER_CACHED))
    {
        found = (cached & 255) == X86_OPERAND_REGISTER;
        if (found)
        {
            *operand = (X86Operand){.kind = X86_OPERAND_REGISTER, .reg = cached >> 8 & 255, .class = cached >> 16 & 255, .width = cached >> 24 & 127};
        }
    }
    else
    {
        found = asm_register(asm_token_text(parser, at), operand);
    }
    return found;
}

static bool asm_read_register(AsmParser* parser, uint32_t end, X86Operand* operand)
{
    uint32_t at = parser->at;
    const bool prefix = at < end && parser->tokens[at].kind == '%';
    at += prefix;
    const bool found = prefix == parser->prefix && at < end && parser->tokens[at].kind == BUSTER_ASM_WORD && asm_token_register(parser, at, operand);
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
    const uint32_t location = parser->tokens[parser->at].start;
    operand->kind = X86_OPERAND_MEMORY;
    operand->mode = BUSTER_X86_MEMORY_ABSOLUTE;
    asm_expect(parser, '[', "expected '['");
    *expression = asm_expression_mode(parser, end, true);
    // Prefer RSP as the unscaled base: unlike the other GPRs, it cannot be an index. Otherwise retain source order.
    if (expression->register_count == 2 && expression->registers[1] == 5 && !(expression->indexed & 2))
    {
        const uint8_t reg = expression->registers[0];
        const uint8_t scale = expression->scales[0];
        expression->registers[0] = expression->registers[1];
        expression->scales[0] = expression->scales[1];
        expression->registers[1] = reg;
        expression->scales[1] = scale;
        expression->indexed = (expression->indexed >> 1) | ((expression->indexed & 1) << 1);
    }
    for (uint32_t i = 0; i < expression->register_count && parser->result.error == 0; i += 1)
    {
        const uint32_t factor = expression->scales[i];
        const uint32_t scale = factor == 2 ? 1 : factor == 4 ? 2 : factor == 8 ? 3 : 0;
        const X86Operand reg =
        {
            .kind = X86_OPERAND_REGISTER,
            .class = expression->registers[i] == 17 ? 255 : BUSTER_X86_CLASS_GPR64,
            .width = 8,
            .reg = expression->registers[i] - 1,
        };
        asm_address_register(parser, operand, reg, scale, (expression->indexed >> i) & 1, location);
    }
    expression->register_count = 0;
    expression->indexed = 0;
    memset(expression->registers, 0, sizeof(expression->registers));
    memset(expression->scales, 0, sizeof(expression->scales));
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
    else if (asm_is_case(parser, parser->at, "offset"))
    {
        parser->at += 1;
        immediate = true;
        if (asm_is_case(parser, parser->at, "flat") && parser->at + 1 < end)
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
            if (asm_is_case(parser, parser->at, names[i]))
            {
                operand->width = 1u << i;
                parser->at += 1;
                if (!asm_is_case(parser, parser->at, "ptr"))
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
    if (!immediate && segment_at + 1 < end && parser->tokens[segment_at + 1].kind == ':' && (asm_is_case(parser, segment_at, "fs") || asm_is_case(parser, segment_at, "gs")))
    {
        operand->segment = asm_is_case(parser, segment_at, "fs") ? 1 : 2;
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
        if (parser->options.intel && !immediate && parser->at < end && parser->tokens[parser->at].kind == '[')
        {
            // GCC writes symbol[rip] as well as [rip+symbol]. Keep the prefix's displacement and symbol.
            AsmExpr address = {0};
            asm_intel_address(parser, end, operand, &address);
            *expression = asm_combine(parser, *expression, address, '+', location);
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
        if (asm_token_register(parser, parser->at, &mask) && mask.class == BUSTER_X86_CLASS_OPMASK && mask.reg != 0 && (request->decorations & 7) == 0)
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
            {"cbtw", "cbw"},
            {"cwtl", "cwde"},
            {"cltq", "cdqe"},
            {"cwtd", "cwd"},
            {"cltd", "cdq"},
            {"cqto", "cqo"},
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

static bool asm_att_extension(String8* name, uint32_t* width, uint32_t* source_width)
{
    static const struct
    {
        const char* name;
        const char* canonical;
        uint8_t source;
        uint8_t destination;
    } aliases[] =
    {
        {"movzbw", "movzx", 1, 2},
        {"movzbl", "movzx", 1, 4},
        {"movzbq", "movzx", 1, 8},
        {"movzwl", "movzx", 2, 4},
        {"movzwq", "movzx", 2, 8},
        {"movsbw", "movsx", 1, 2},
        {"movsbl", "movsx", 1, 4},
        {"movsbq", "movsx", 1, 8},
        {"movswl", "movsx", 2, 4},
        {"movswq", "movsx", 2, 8},
        {"movslq", "movsxd", 4, 8},
        // The x87's memory widths: s a float's, l a double's and t the 10 bytes of an extended value.
        {"flds", "fld", 4, 4},
        {"fldl", "fld", 8, 8},
        {"fldt", "fld", 10, 10},
        {"fstps", "fstp", 4, 4},
        {"fstpl", "fstp", 8, 8},
        {"fstpt", "fstp", 10, 10},
    };
    bool found = false;
    for (uint32_t i = 0; !found && i < BUSTER_ARRAY_LENGTH(aliases); i += 1)
    {
        found = x86_match_name(*name, aliases[i].name);
        if (found)
        {
            *name = (String8){.pointer = (char*)aliases[i].canonical, .length = strlen(aliases[i].canonical)};
            *width = aliases[i].destination;
            *source_width = aliases[i].source;
        }
    }
    return found;
}

// A string instruction as the catalogue spells it, with its prefix (`rep movsb`, two tokens, is one mnemonic): repz and
// repnz are repe and repne, rep before a compare or a scan is repe, and AT&T's dword suffix l is d. `name` is the
// mnemonic's token; a prefix takes the next token, the instruction, which the parser then skips. Any other mnemonic
// stays as it is, and only a prefix's first letter or an AT&T dword's suffix makes one worth comparing.
static void asm_string_mnemonic(AsmParser* parser, String8* name, uint32_t end)
{
    static const char* const prefixes[][2] = { { "rep", "rep" }, { "repe", "repe" }, { "repz", "repe" }, { "repne", "repne" }, { "repnz", "repne" } };
    static const char* const bases[] = { "movs", "stos", "lods", "cmps", "scas" };
    const bool candidate = (name->length >= 3 && name->pointer[0] == 'r') || (!parser->options.intel && name->length == 5 && name->pointer[4] == 'l');
    const char* prefix = NULL;
    for (uint32_t i = 0; candidate && prefix == NULL && i < BUSTER_ARRAY_LENGTH(prefixes); i += 1)
    {
        prefix = x86_match_name(*name, prefixes[i][0]) ? prefixes[i][1] : NULL;
    }
    const bool prefixed = prefix != NULL && parser->at < end && parser->tokens[parser->at].kind == BUSTER_ASM_WORD;
    const String8 instruction = prefixed ? asm_token_text(parser, parser->at) : *name;
    uint32_t base = BUSTER_ARRAY_LENGTH(bases);
    for (uint32_t i = 0; candidate && instruction.length == 5 && i < BUSTER_ARRAY_LENGTH(bases); i += 1)
    {
        base = memcmp(instruction.pointer, bases[i], 4) == 0 ? i : base;
    }
    const char written = base < BUSTER_ARRAY_LENGTH(bases) ? instruction.pointer[4] : 0;
    const char size = !parser->options.intel && written == 'l' ? 'd' : written;
    if ((size == 'b' || size == 'w' || size == 'd' || size == 'q') && (prefixed || size != written))
    {
        const char* spelled = prefixed && base >= 3 && strcmp(prefix, "rep") == 0 ? "repe" : prefixed ? prefix : "";
        const uint64_t prefix_length = strlen(spelled);
        const uint64_t length = prefix_length + (prefixed ? 1 : 0) + 5;
        char* bytes = arena_push(parser->arena, length, 1);
        if (bytes == 0)
        {
            asm_fail(parser, parser->tokens[parser->at - 1].start, "arena exhausted");
        }
        else
        {
            memcpy(bytes, spelled, prefix_length);
            bytes[prefix_length] = ' ';
            memcpy(bytes + length - 5, bases[base], 4);
            bytes[length - 1] = size;
            *name = (String8){ .pointer = bytes, .length = length };
            parser->at += prefixed ? 1 : 0;
        }
    }
}

// Whether an AT&T instruction's operands (its tokens to `end`) name an XMM register: then movq is SSE2's, as GNU as
// reads it, not mov's 64-bit form.
static bool asm_vector_operands(const AsmParser* parser, uint32_t end)
{
    bool found = false;
    for (uint32_t i = parser->at; !found && i + 1 < end; i += 1)
    {
        X86Operand operand;
        found = parser->tokens[i].kind == '%' && parser->tokens[i + 1].kind == BUSTER_ASM_WORD && asm_token_register(parser, i + 1, &operand) &&
                operand.class == BUSTER_X86_CLASS_XMM;
    }
    return found;
}

static void asm_instruction(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    if (parser->instruction_count >= parser->instruction_capacity || object_class_is_zero_fill(parser->sections[parser->section].class))
    {
        asm_fail(parser, location, "instruction capacity exhausted or instruction in .bss");
    }
    else
    {
        AsmInstruction* instruction = &parser->instructions[parser->instruction_count++];
        *instruction = (AsmInstruction){.location = location};
        instruction->request.mnemonic = asm_token_text(parser, parser->at++);
        String8* name = &instruction->request.mnemonic;
        asm_string_mnemonic(parser, name, end);
        uint32_t width = 0;
        uint32_t source_width = 0;
        // A complete alias keeps its last letter: jnb, jnl and sal end in suffix letters.
        bool known = asm_alias(name) || (!parser->options.intel && asm_att_extension(name, &width, &source_width));
        if (!parser->options.intel && !known && name->length > 1)
        {
            // Strip integer suffixes only when the full mnemonic has no catalogue candidate, and movq's (mov's 64-bit
            // form) unless an XMM operand makes it SSE2's.
            const uint32_t bucket = x86_match_bucket(parser->index, *name);
            known = parser->index->names[bucket].pointer != 0 && (!x86_match_name(*name, "movq") || asm_vector_operands(parser, end));
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
        // Extension forms describe their source memory width; the destination is fixed by its register class.
        instruction->request.width = address ? 0 : source_width != 0 ? source_width : width;
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
                const uint32_t expected_width = i == 0 && source_width != 0 ? source_width : width;
                if (operand->width != 0 && operand->width != expected_width)
                {
                    asm_fail(parser, location, "mnemonic suffix and register width disagree");
                }
                operand->width = expected_width;
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
        parser->sections[parser->section].data = false;
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

static uint32_t asm_note_section(const AsmParser* parser, uint32_t end)
{
    const uint32_t at = parser->at;
    // '-' is an expression token. A section spelling joins it only when the three tokens are adjacent.
    const bool unquoted = at + 2 < end && asm_is(parser, at, ".note.GNU") && parser->tokens[at + 1].kind == '-' && asm_is(parser, at + 2, "stack") && parser->tokens[at].end == parser->tokens[at + 1].start && parser->tokens[at + 1].end == parser->tokens[at + 2].start;
    return unquoted ? 3 : at < end && asm_is(parser, at, "\".note.GNU-stack\"") ? 1 : 0;
}

#include "assembler_sections.c"
#include "assembler_cfi.c"

static void asm_directive(AsmParser* parser, uint32_t end)
{
    const uint32_t token = parser->at++;
    const uint32_t location = parser->tokens[token].start;
    if (parser->code && !asm_is(parser, token, ".intel_syntax") && !asm_is(parser, token, ".att_syntax"))
    {
        // Code that another function's records hold (asm_assemble_code) has no sections, data or symbols of its own.
        asm_fail(parser, location, "inline assembly takes no directive but .intel_syntax and .att_syntax");
    }
    else if (asm_is(parser, token, ".intel_syntax") || asm_is(parser, token, ".att_syntax"))
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
    else if (asm_section_prefix(asm_token_text(parser, token), ".cfi_"))
    {
        asm_cfi_directive(parser, token, end);
    }
    else if (asm_is(parser, token, ".file") || asm_is(parser, token, ".ident"))
    {
        if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, location, "expected a quoted metadata string");
        }
        else
        {
            const String8 text = asm_name_text(parser, parser->at++);
            if (asm_is(parser, token, ".file"))
            {
                parser->result.source_name = text;
            }
        }
    }
    else if (asm_is(parser, token, ".addrsig") || asm_is(parser, token, ".addrsig_sym"))
    {
        // Omitting LLVM's optional address-significance table conservatively marks every address significant.
        if (asm_is(parser, token, ".addrsig_sym"))
        {
            if (parser->at >= end || !asm_name_token(parser, parser->at))
            {
                asm_fail(parser, location, "expected an address-significant symbol name");
            }
            else
            {
                parser->at += 1;
            }
        }
    }
    else if (asm_is(parser, token, ".section") && asm_note_section(parser, end))
    {
        // The shared writer already supplies the empty non-executable-stack marker.
        parser->section = BUSTER_LINK_CLASS_NONE;
        parser->at += asm_note_section(parser, end);
        if (asm_take(parser, ','))
        {
            if (!asm_is(parser, parser->at++, "\"\""))
            {
                asm_fail(parser, location, "only a non-executable GNU-stack note is supported");
            }
            if (asm_take(parser, ','))
            {
                asm_expect(parser, '@', "expected section type");
                if (!asm_is(parser, parser->at++, "progbits"))
                {
                    asm_fail(parser, location, "GNU-stack note requires @progbits");
                }
            }
        }
    }
    else if (asm_is(parser, token, ".text") || asm_is(parser, token, ".data") || asm_is(parser, token, ".bss") || asm_is(parser, token, ".section"))
    {
        asm_section_directive(parser, token, end);
    }
    else if (asm_is(parser, token, ".globl") || asm_is(parser, token, ".global") || asm_is(parser, token, ".local") || asm_is(parser, token, ".weak"))
    {
        const uint8_t binding = asm_is(parser, token, ".local") ? BUSTER_LINK_SYMBOL_LOCAL : asm_is(parser, token, ".weak") ? BUSTER_LINK_SYMBOL_WEAK
                                                                                                                            : BUSTER_LINK_SYMBOL_GLOBAL;
        do
        {
            if (parser->at >= end || !asm_name_token(parser, parser->at))
            {
                asm_fail(parser, location, "binding directive requires a symbol name");
                break;
            }
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, asm_name_text(parser, parser->at++), location)];
            symbol->flags = (symbol->flags & ~BUSTER_LINK_SYMBOL_BINDING) | binding;
            symbol->explicit_binding = true;
        } while (asm_take(parser, ','));
    }
    else if (asm_is(parser, token, ".type") || asm_is(parser, token, ".size") || asm_is(parser, token, ".equ") || asm_is(parser, token, ".equiv") || asm_is(parser, token, ".set"))
    {
        if (parser->at >= end || !asm_name_token(parser, parser->at))
        {
            asm_fail(parser, location, "directive requires a symbol name");
        }
        else
        {
            AsmSymbol* symbol = &parser->symbols[asm_symbol(parser, asm_name_text(parser, parser->at++), location)];
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
                if (symbol->defined && (!symbol->absolute || asm_is(parser, token, ".equiv")))
                {
                    asm_fail(parser, location, "duplicate symbol definition");
                }
                symbol->constant = value;
                symbol->absolute = true;
                symbol->defined = true;
            }
        }
    }
    else if (asm_is(parser, token, ".byte") || asm_is(parser, token, ".word") || asm_is(parser, token, ".short") || asm_is(parser, token, ".value") || asm_is(parser, token, ".long") || asm_is(parser, token, ".int") || asm_is(parser, token, ".quad"))
    {
        const uint32_t width = asm_is(parser, token, ".byte") ? 1 : asm_is(parser, token, ".quad") ? 8 : asm_is(parser, token, ".long") || asm_is(parser, token, ".int") ? 4 : 2;
        do
        {
            // `.` is the address of this datum, so it is bound before the datum takes its place.
            const AsmExpr expression = asm_expression(parser, end);
            const uint32_t fragment = asm_fragment(parser, ASM_VALUE, location);
            parser->fragments[fragment].expression = expression;
            parser->fragments[fragment].size = width;
        } while (parser->result.error == 0 && asm_take(parser, ','));
        parser->sections[parser->section].data = true;
    }
    else if (asm_is(parser, token, ".ascii") || asm_is(parser, token, ".asciz") || asm_is(parser, token, ".string"))
    {
        asm_string(parser, end, !asm_is(parser, token, ".ascii"));
        parser->sections[parser->section].data = true;
    }
    else if (asm_is(parser, token, ".zero") || asm_is(parser, token, ".align") || asm_is(parser, token, ".balign") || asm_is(parser, token, ".p2align"))
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
        output->padding = !align || (parser->sections[parser->section].object.flags & SHF_EXECINSTR) == 0 ? ASM_PADDING_FILL : parser->sections[parser->section].data ? ASM_PADDING_NOPS_AFTER_DATA : ASM_PADDING_NOPS;
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
        parser->sections[parser->section].data |= !align;
        if (align && value > 0)
        {
            const uint32_t log = __builtin_ctzll((uint64_t)value);
            if (log > parser->sections[parser->section].alignment)
            {
                parser->sections[parser->section].alignment = log;
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

// Eight bounded spellings in lanes. Register descriptions come from the token cache. Decimal conversion reduces
// digit pairs and quartets, then combines eight digits without an eight-step multiply dependency. Uncommon
// spellings (long names, hexadecimal, expressions, addresses/decorations) go to the exact grammar.
static void asm_simple_words_vector(const uint64_t* keys, const uint64_t* lengths, uint32_t count, uint64_t* descriptors, uint64_t* values)
{
    for (uint32_t first = 0; first < count; first += 8)
    {
        const uint32_t left = count - first;
        const __mmask8 valid = left >= 8 ? 255 : (1u << left) - 1;
        const __m512i words = _mm512_maskz_loadu_epi64(valid, keys + first);
        const __m512i sizes = _mm512_maskz_loadu_epi64(valid, lengths + first);
        __m512i descriptions = _mm512_maskz_loadu_epi64(valid, descriptors + first);
        // RIP is valid in an address, but never as a standalone register operand. Keep its exact diagnostic and
        // source-order error handling in asm_operand instead of accepting the cached spelling into this batch.
        const __m512i class_mask = _mm512_set1_epi64(255u << 16);
        const __m512i classes = _mm512_and_si512(descriptions, class_mask);
        descriptions = _mm512_maskz_mov_epi64(_mm512_cmpneq_epi64_mask(classes, class_mask), descriptions);
        __mmask8 decimal = valid & _mm512_cmpgt_epi64_mask(sizes, _mm512_setzero_si512());
        const __m512i shift = _mm512_slli_epi64(_mm512_sub_epi64(_mm512_set1_epi64(8), sizes), 3);
        // Move the spelling to the high end of its qword; shifting supplies zero-valued leading digits and
        // discards every byte past the bounded spelling. Byte subtraction never borrows between digits.
        const __m512i digits = _mm512_sllv_epi64(_mm512_sub_epi8(words, _mm512_set1_epi8('0')), shift);
        decimal &= _mm512_cmpeq_epi64_mask(digits, _mm512_min_epu8(digits, _mm512_set1_epi8(9)));
        const __m512i pairs = _mm512_maddubs_epi16(digits, _mm512_set1_epi16(0x010a));
        const __m512i quartets = _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00010064));
        const __m512i numbers = _mm512_add_epi64(_mm512_mul_epu32(quartets, _mm512_set1_epi64(10000)), _mm512_srli_epi64(quartets, 32));
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
                    descriptors[words] = parser->tokens[at].reserved & ~BUSTER_ASM_REGISTER_CACHED;
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
        const uint32_t bucket = x86_match_bucket(parser->index, mnemonic);
        const bool named = parser->index->names[bucket].pointer != 0;
        supported &= named && !x86_match_name(mnemonic, "sal");
        if (!supported || parser->instruction_count >= parser->instruction_capacity || object_class_is_zero_fill(parser->sections[parser->section].class))
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
        parser->sections[parser->section].data = false;
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
        while (parser->at + 1 < parser->token_count && asm_name_token(parser, parser->at) && parser->tokens[parser->at + 1].kind == ':')
        {
            const uint32_t location = parser->tokens[parser->at].start;
            const String8 name = asm_name_text(parser, parser->at);
            uint32_t id;
            if (parser->tokens[parser->at].kind == BUSTER_ASM_WORD && name.length == 1 && name.pointer[0] >= '0' && name.pointer[0] <= '9')
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
    if (parser->cfi_active)
    {
        asm_fail(parser, parser->source.length, "missing .cfi_endproc");
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

static bool asm_local_target(const AsmParser* parser, AsmExpr expression, uint32_t section, bool global_jump)
{
    const AsmSymbol* symbol = &parser->symbols[expression.plus];
    const uint8_t binding = symbol->flags & BUSTER_LINK_SYMBOL_BINDING;
    const bool fixed = binding == BUSTER_LINK_SYMBOL_LOCAL || (global_jump && binding == BUSTER_LINK_SYMBOL_GLOBAL);
    return expression.plus != 0 && expression.minus == 0 && symbol->defined && !symbol->absolute && fixed && parser->fragments[symbol->fragment].section == section;
}

static void asm_match_instructions(AsmParser* parser)
{
    for (uint32_t i = 0; i < parser->instruction_count && parser->result.error == 0; i += 1)
    {
        AsmInstruction* instruction = &parser->instructions[i];
        AsmFragment* fragment = &parser->fragments[instruction->fragment];
        uint32_t width = instruction->request.width;
        for (uint32_t j = 0; j < instruction->request.count; j += 1)
        {
            const X86Operand* operand = &instruction->request.operands[j];
            if (operand->kind == X86_OPERAND_REGISTER || operand->kind == X86_OPERAND_MEMORY)
            {
                width = operand->width > width ? operand->width : width;
            }
        }
        for (uint32_t j = 0; j < instruction->request.count; j += 1)
        {
            X86Operand* operand = &instruction->request.operands[j];
            if (operand->kind == X86_OPERAND_IMMEDIATE && !operand->symbolic && width != 0 && width <= 8)
            {
                // GNU integer operands are bit patterns of their destination width. Canonicalize only values in
                // that width's unsigned range; oversized constants still fail the matcher's exact range check.
                const __int128 modulus = (__int128)1 << (width * 8);
                if (operand->value >= modulus / 2 && operand->value < modulus)
                {
                    operand->value -= modulus;
                }
            }
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
            // GNU as resolves same-section global jumps without -mshared; global calls remain interposable.
            instruction->local_branch = instruction->branch && asm_local_target(parser, instruction->expressions[0], fragment->section, strcmp(form->mnemonic, "call") != 0);
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
    const uint32_t section = parser->fragments[0].section;
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
                    parser->sections[section].size = layout.length;
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
    for (uint32_t section = 1; section < parser->section_count; section += 1)
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
        parser->sections[section].size = offset;
    }
}

static void asm_layout(AsmParser* parser)
{
    asm_partition_fragments(parser);
    bool changed = parser->result.error == 0 && !asm_layout_records(parser);
    const uint32_t* order = parser->fragment_order;
    const uint32_t* begin = parser->fragment_begin;
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
            if (pc_relative && !instruction->branch && asm_local_target(parser, expression, fragment->section, false))
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
                    const bool truncated_lea = strcmp(form->mnemonic, "lea") == 0 && !form->w;
                    const bool signed_field = (displacement && !truncated_lea) || x86_match_sign_extends(form);
                    const bool interposable = (parser->symbols[expression.plus].flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
                    const uint8_t kind = instruction->branch && interposable ? BUSTER_LINK_FIXUP_CALL : pc_relative ? BUSTER_LINK_FIXUP_PC32
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
            .atoms = parser->section_count,
            .symbols = parser->symbol_count,
            .fixups = parser->fragment_count + parser->instruction_count * 2,
            .wide_addends = parser->fragment_count + parser->instruction_count * 2,
    };
    if (!link_initialize(link, parser->arena, parser->arena, capacity))
    {
        asm_fail(parser, 0, "arena exhausted building link tables");
    }
    for (uint32_t f = 0; f < parser->fragment_count && parser->result.error == 0; f += 1)
    {
        const uint32_t section = parser->fragments[f].section;
        AsmSection* descriptor = &parser->sections[section];
        if (descriptor->atom == 0)
        {
            const uint32_t size = descriptor->size;
            // The trusted encoder requires worst-case capacity plus its documented store slack even when the
            // chosen encodings are shorter. Keep that padding outside the logical atom size, including the tail.
            const bool zero = object_class_is_zero_fill(descriptor->class);
            uint8_t* bytes = zero ? 0 : arena_push(parser->arena, (uint64_t)size + BUSTER_X86_ENCODER_CAPACITY(256), 64);
            if (bytes == 0 && !zero)
            {
                asm_fail(parser, parser->fragments[f].location, "arena exhausted allocating section");
            }
            else
            {
                if (bytes != 0)
                {
                    memset(bytes, 0, size);
                }
                descriptor->atom = link_add_atom(link, descriptor->class, descriptor->alignment, size, bytes, 0);
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
        symbol->link = link_add_symbol(link, name, symbol->defined ? parser->sections[definition->section].atom : 0, symbol->defined ? definition->offset : 0, size.value, symbol->flags, symbol->defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE);
    }
    for (uint32_t section = 1; section < parser->section_count && parser->result.error == 0; section += 1)
    {
        const AtomId atom = parser->sections[section].atom;
        if (atom == 0)
        {
            continue;
        }
        const uint32_t end = parser->fragment_begin[section + 1];
        for (uint32_t position = parser->fragment_begin[section]; position < end && parser->result.error == 0; position += 1)
        {
            const uint32_t f = parser->fragment_order[position];
            AsmFragment* fragment = &parser->fragments[f];
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
                uint32_t next = position;
                while (next < end && count < 256)
                {
                    const uint32_t id = parser->fragment_order[next];
                    AsmFragment* item = &parser->fragments[id];
                    if (item->kind != ASM_INSTRUCTION && item->kind != ASM_LABEL)
                    {
                        break;
                    }
                    if (item->kind == ASM_INSTRUCTION)
                    {
                        fragments[count] = id;
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
                position = next - 1;
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
    for (uint32_t i = 0; i < parser->procedure_count && parser->result.error == 0; i += 1)
    {
        const AsmCfiProcedure* procedure = &parser->procedures[i];
        link_add_fixup(link, parser->sections[BUSTER_LINK_CLASS_EH_FRAME].atom, procedure->relocation, BUSTER_LINK_FIXUP_PC32, parser->symbols[procedure->symbol].link, 0);
    }
    asm_object_options(parser);
    if (parser->result.error == 0 && link_check(link) != LINK_ERROR_NONE)
    {
        asm_fail(parser, 0, "invalid link representation (symbol size, capacity or relocation)");
    }
}

// The source's tokens, asm_assemble's and asm_assemble_code's first phase: the lexer's records, and four lookahead
// sentinels past them.
static void asm_lex_source(AsmParser* parser)
{
    const String8 source = parser->source;
    Arena* arena = parser->arena;
    if (source.length > BUSTER_ASM_MAX_SOURCE || (source.length != 0 && source.pointer == 0))
    {
        asm_fail(parser, 0, "invalid source or source exceeds the 16 MiB limit");
    }
    else
    {
        parser->tokens = BUSTER_ARENA_PUSH_ARRAY(arena, AsmToken, source.length + 4);
        if (parser->tokens == 0)
        {
            asm_fail(parser, 0, "arena exhausted lexing source");
        }
        else
        {
            asm_lex(parser);
            // Lexing may emit one token per byte, but retain only the actual records and four lookahead sentinels.
            // Every lexer store initializes the full record. No full-source clearing or unused-token working set.
            memset(parser->tokens + parser->token_count, 0, 4 * sizeof(AsmToken));
            parser->tokens[parser->token_count].start = source.length;
            parser->tokens[parser->token_count].end = source.length;
            arena->length = (uint8_t*)(parser->tokens + parser->token_count + 4) - arena->base;
        }
    }
}

// The second phase: the tables, as large as the tokens may need, and the statements parsed into them.
static void asm_parse_source(AsmParser* parser)
{
    Arena* arena = parser->arena;
    if (parser->result.error == 0)
    {
        parser->capacity = parser->token_count + 4;
        if (parser->options.vectorized)
        {
            asm_cache_registers(parser);
        }
        uint32_t slots = 16;
        while (slots < parser->capacity * 2)
        {
            slots *= 2;
        }
        parser->symbol_mask = slots - 1;
        parser->symbol_slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
        parser->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, AsmSymbol, parser->capacity);
        parser->fragments = BUSTER_ARENA_PUSH_ARRAY(arena, AsmFragment, parser->capacity);
        parser->instructions = BUSTER_ARENA_PUSH_ARRAY(arena, AsmInstruction, parser->instruction_capacity);
        parser->section_capacity = parser->instruction_capacity + BUSTER_LINK_CLASS_COUNT;
        parser->section_capacity = parser->section_capacity < BUSTER_ASM_MAX_SECTIONS ? parser->section_capacity : BUSTER_ASM_MAX_SECTIONS;
        parser->sections = BUSTER_ARENA_PUSH_ARRAY(arena, AsmSection, parser->section_capacity);
        if (parser->symbol_slots == 0 || parser->symbols == 0 || parser->fragments == 0 || parser->instructions == 0 || parser->sections == 0)
        {
            asm_fail(parser, 0, "arena exhausted allocating parser tables");
        }
        else
        {
            memset(parser->symbol_slots, 0, slots * sizeof(uint32_t));
            parser->symbols[0] = (AsmSymbol){0};
            asm_sections_initialize(parser);
            asm_fragment(parser, ASM_LABEL, 0);
            asm_parse(parser);
        }
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
    asm_lex_source(&parser);
    const uint64_t lexed = asm_clock();
    asm_parse_source(&parser);
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
        asm_cfi_emit(&parser);
    }
    const uint64_t cfi_emitted = asm_clock();
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
    parser.result.cfi_ns = cfi_emitted - laid_out;
    parser.result.instructions = parser.instruction_count;
    if (parser.result.error != 0)
    {
        arena->length = saved;
        parser.result.link = (Link){0};
        parser.result.object_options = (ObjectOptions){0};
    }
    return parser.result;
}

// A symbolic field of asm_assemble_code's records: what the field refers to, the symbol `name` (as the source spells it,
// in output's bytes) plus `addend`, as a LinkReference has it, the name standing for its target.
typedef struct AsmCodeReference AsmCodeReference;
struct AsmCodeReference
{
    String8 name;
    int64_t addend;
    uint32_t record;
    // X86_FIXUP_DISPLACEMENT or X86_FIXUP_IMMEDIATE, and BUSTER_LINK_FIXUP_*.
    uint8_t field;
    uint8_t kind;
};

// A label asm_assemble_code's source defines by name (a numeric label or `.` has none): its name, in output's bytes, and
// its next record (the count for the end), where the caller's symbol of it goes.
typedef struct AsmCodeLabel AsmCodeLabel;
struct AsmCodeLabel
{
    String8 name;
    uint32_t record;
};

// What asm_assemble_code makes of a source: `count` records, an instruction each, in order; every jmp and jcc to a label
// of the source's without an addend a rel8 with displacement 0, in `branches` (their indices, ascending) and `targets`
// (each label's next record, or the count for the end), branch sizing's columns; its symbolic fields (`references`, a
// record's displacement's before its immediate's, in record order), each record with one marked BUSTER_X86_FORM_FIXUP;
// and its named labels (`labels`, in the order the source first names them). Or an `error` at the source's byte
// `location`, with nothing kept.
typedef struct AsmCode AsmCode;
struct AsmCode
{
    X86Instruction* records;
    uint32_t* branches;
    uint32_t* targets;
    AsmCodeReference* references;
    AsmCodeLabel* labels;
    const char* error;
    uint32_t count;
    uint32_t branch_count;
    uint32_t reference_count;
    uint32_t label_count;
    uint32_t location;
};

// Assembles a source whose instructions another function's records hold (#147: a C asm statement's template, after its
// operands are substituted), in place among them: instructions and labels, in either syntax and switching with
// .intel_syntax and .att_syntax, which are its only directives. It has no sections, data or symbols of its own, so it
// refuses every other directive. A jmp or jcc to a label of its own, without an addend, is a branch that branch sizing
// sizes. Every other symbolic field refers to a symbol by name, which the caller resolves: one of the source's named
// labels (as data, a call's target, or with an addend), a label another source defines (a `.L` one among them), or any
// other symbol. Its numeric labels and `.` are only its own jmp's and jcc's targets. Matching is asm_assemble's, by
// options.vectorized's path; the parser's tables come from `scratch`, which is as it was after, and the result from
// `output`, which a failure leaves as it was too.
static AsmCode asm_assemble_code(String8 source, const X86MatchIndex* index, AsmOptions options, Arena* output, Arena* scratch)
{
    AsmParser parser =
        {
            .source = source,
            .arena = scratch,
            .index = index,
            .options = options,
            .section = BUSTER_LINK_CLASS_TEXT,
            .prefix = !options.intel,
            .symbol_count = 1,
            .code = true,
    };
    AsmCode result = { 0 };
    const uint64_t saved = output->length;
    const uint64_t temporary = scratch->length;
    asm_lex_source(&parser);
    asm_parse_source(&parser);
    if (parser.result.error == 0)
    {
        asm_match_instructions(&parser);
    }
    const uint32_t count = parser.instruction_count;
    uint32_t label_count = 0;
    for (uint32_t s = 1; parser.result.error == 0 && s < parser.symbol_count; s += 1)
    {
        label_count += parser.symbols[s].defined && !parser.symbols[s].internal;
    }
    // Each label's next record: the instructions before its fragment.
    uint32_t* positions = parser.result.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)parser.fragment_count + 1) : 0;
    result.records = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, count) : 0;
    result.branches = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
    result.targets = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
    result.references = parser.result.error == 0 && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AsmCodeReference, 2 * (uint64_t)count) : 0;
    result.labels = parser.result.error == 0 && label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AsmCodeLabel, label_count) : 0;
    if (parser.result.error == 0 && (positions == 0 || (count != 0 && (result.records == 0 || result.branches == 0 || result.targets == 0 || result.references == 0)) ||
                                     (label_count != 0 && result.labels == 0)))
    {
        asm_fail(&parser, 0, "arena exhausted placing inline assembly's records");
    }
    uint32_t at = 0;
    for (uint32_t f = 0; parser.result.error == 0 && f < parser.fragment_count; f += 1)
    {
        positions[f] = at;
        at += parser.fragments[f].kind == ASM_INSTRUCTION;
    }
    for (uint32_t s = 1; parser.result.error == 0 && s < parser.symbol_count; s += 1)
    {
        const AsmSymbol* symbol = &parser.symbols[s];
        char* name = symbol->defined && !symbol->internal ? arena_push(output, symbol->name.length, 1) : 0;
        if (symbol->defined && !symbol->internal && name == 0)
        {
            asm_fail(&parser, 0, "arena exhausted placing inline assembly's records");
        }
        else if (name != 0)
        {
            memcpy(name, symbol->name.pointer, symbol->name.length);
            result.labels[result.label_count++] = (AsmCodeLabel){ .name = { .pointer = name, .length = symbol->name.length }, .record = positions[symbol->fragment] };
        }
    }
    for (uint32_t i = 0; parser.result.error == 0 && i < count; i += 1)
    {
        AsmInstruction* instruction = &parser.instructions[i];
        X86Instruction record = instruction->record;
        const X86FormDescription* form = x86_form_description(record.form);
        // A branch to a label of the source's: sized as a jmp or jcc (its rel8 form, which grows to the rel32 one after
        // it) without an addend; else, to a named label, a reference to it from the branch's rel32 form.
        bool referred = !instruction->local_branch;
        if (instruction->local_branch)
        {
            const uint32_t row = record.form;
            const AsmSymbol* target = &parser.symbols[instruction->expressions[0].plus];
            const bool sized = form->relative_bytes == 1 && row + 1 < X86_FORM_COUNT && x86_form_descriptions[row + 1].relative_bytes == 4 &&
                               strcmp(form->mnemonic, x86_form_descriptions[row + 1].mnemonic) == 0;
            if (sized && instruction->expressions[0].value == 0)
            {
                result.branches[result.branch_count] = i;
                result.targets[result.branch_count++] = positions[target->fragment];
            }
            else if (!target->internal && (sized || form->relative_bytes == 4))
            {
                record.form = (uint16_t)(sized ? row + 1 : row);
                form = x86_form_description(record.form);
                referred = true;
            }
            else
            {
                // loop, loope, loopne and jrcxz have no rel32 form for a reference, and branch sizing sizes only jmp and jcc.
                asm_fail(&parser, instruction->location, form->relative_bytes == 1 && !sized ? "loop, loope, loopne and jrcxz in inline assembly" :
                                                                                                "a branch to inline assembly's own numeric label must be jmp or jcc, without an addend");
            }
        }
        for (uint32_t field = X86_FIXUP_DISPLACEMENT; referred && field <= X86_FIXUP_IMMEDIATE && parser.result.error == 0; field += 1)
        {
            for (uint32_t j = 0; j < instruction->request.count && parser.result.error == 0; j += 1)
            {
                const X86Operand* operand = &instruction->request.operands[j];
                const bool displacement = operand->kind == X86_OPERAND_MEMORY || instruction->branch;
                const AsmExpr expression = instruction->expressions[j];
                const AsmSymbol* symbol = &parser.symbols[expression.plus];
                if (expression.plus == 0 || (field == X86_FIXUP_DISPLACEMENT) != displacement)
                {
                    continue;
                }
                const bool pc_relative = instruction->branch || (operand->mode & BUSTER_X86_MODE_RIP);
                const uint32_t width = displacement ? 4 : form->immediate_bytes;
                const bool truncated_lea = strcmp(form->mnemonic, "lea") == 0 && !form->w;
                const bool signed_field = (displacement && !truncated_lea) || x86_match_sign_extends(form);
                char* name = !symbol->internal && symbol->name.length != 0 ? arena_push(output, symbol->name.length, 1) : 0;
                if (symbol->internal && symbol->defined)
                {
                    asm_fail(&parser, instruction->location, "inline assembly's numeric labels and `.` are only its own jmp's and jcc's targets");
                }
                else if (symbol->internal)
                {
                    asm_fail(&parser, symbol->location, "undefined local symbol");
                }
                else if (expression.value < INT64_MIN || expression.value > INT64_MAX || (width != 4 && width != 8))
                {
                    asm_fail(&parser, instruction->location, "unrepresentable instruction relocation");
                }
                else if (name == 0)
                {
                    asm_fail(&parser, instruction->location, "arena exhausted placing inline assembly's records");
                }
                else
                {
                    // A symbol the source doesn't define may be preempted, so a call or jump to it is a CALL (PLT32), as
                    // asm_instruction_fixups makes an undefined symbol's; and so is a branch to a label of the source's
                    // that it doesn't size.
                    memcpy(name, symbol->name.pointer, symbol->name.length);
                    result.references[result.reference_count++] = (AsmCodeReference){
                        .name = { .pointer = name, .length = symbol->name.length },
                        .addend = (int64_t)expression.value,
                        .record = i,
                        .field = (uint8_t)field,
                        .kind = instruction->branch ? BUSTER_LINK_FIXUP_CALL : pc_relative ? BUSTER_LINK_FIXUP_PC32 : width == 8 ? BUSTER_LINK_FIXUP_ABS64
                                                                                     : signed_field ? BUSTER_LINK_FIXUP_ABS32S : BUSTER_LINK_FIXUP_ABS32,
                    };
                    record.form |= BUSTER_X86_FORM_FIXUP;
                }
            }
        }
        result.records[i] = record;
    }
    if (parser.result.error != 0)
    {
        output->length = saved;
        result = (AsmCode){ .error = parser.result.error, .location = parser.result.location };
    }
    else
    {
        result.count = count;
    }
    scratch->length = temporary;
    return result;
}
