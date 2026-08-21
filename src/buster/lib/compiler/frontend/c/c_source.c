// Source loading, lexing, and preprocessing — the first stage of the
// frontend, entered through c_preprocess near the bottom of the file. It
// turns a root file into the flat CToken stream every later stage walks:
// translation (line splices and carriage returns folded out), lexing,
// directive handling, include resolution (real files and the builtin
// resource headers), macro expansion, and the source metrics behind the
// SOURCE table. c_prewarm at the bottom fills the lazily built tables on
// one thread before any parallel phase reads them (AGENTS.md).
//
// Layout, in file order; each anchor is a definition to search for:
//   c_space_local .. c_space_retoken           spelling spaces: append-only
//                                              storage token spellings point
//                                              into
//   c_source_map_append, c_lex_token_location  location checkpoints (see
//                                              CTranslatedSource below)
//   c_identifier_start ..                      character classes and the
//   c_literal_plain_run_end                    prewarmed run tables
//   c_translate_source                         phase-1/2 translation with
//                                              SWAR/AVX-512/scalar variants
//                                              kept in differential agreement
//   c_source_metrics_add                       the SOURCE table counters
//   c_lex_scan_one, c_lex_scalar               the scalar lexer
//   c_lex_compact                              the SIMD lexer (Validark
//                                              method, AGENTS.md); c_lex
//                                              dispatches, c_lex_reference is
//                                              the differential baseline
//   c_macro_name_hash .. c_symbol_intern       macro and symbol tables
//   c_macro_invocation_arguments ..            macro expansion: arguments,
//   c_preprocess_expand                        stringify, paste, rescan
//   c_conditional_* ,                          #if evaluation including
//   c_integer_expression_evaluate              __has_* feature tests
//   c_preprocess_pragma_*                      pragmas: once, pack, push/pop
//   c_include_read .. c_include_name           include resolution and the
//                                              builtin resource headers
//   c_preprocess_define_directive              #define parsing
//   c_preprocess                               the stage driver
//   c_prewarm                                  serial table prewarm

#include "c_internal.h"

// Locations are recorded as checkpoints instead of one entry per translated
// byte: within a run the original offset and the column both advance one per
// output byte and the line is constant, so a checkpoint is needed only where
// that linearity breaks — the byte after a newline and the byte after a line
// splice. This is what keeps the lexer from writing a location for every
// byte of every include it translates, and what lets tokens drop their eager
// location entirely: a token's line/column recover on demand from its offset
// through these checkpoints.
typedef struct CTranslatedSource CTranslatedSource;
struct CTranslatedSource
{
    String8 source;
    IrSourceCheckpoint* checkpoints;
    u32* checkpoint_offsets;
    u32* checkpoint_pages;
    u32 checkpoint_page_count;
    u32 checkpoint_count;
    // Spelling-space offset of source.pointer (0 without a space).
    u32 translated_offset;
    // Physical lines of the untranslated file, spliced ones included. The
    // line counter the checkpoints already need makes this free.
    u32 raw_lines;
};

// The single contiguous byte space every token offset of one preprocess run
// points into. All spelling bytes — the prelude, every translated include,
// and every synthesized spelling (stringify, paste, builtins, expansion
// copies) — are bump-allocated here so a spelling is always
// `base + token.offset`, one add on the hot paths. The block is carved from
// the caller's arena once; untouched tail pages never commit, so oversizing
// costs address space only.
BUSTER_C_INTERNAL char8* c_space_allocate(CSpellingSpace* space, u64 size)
{
    if (space->arena)
    {
        char8* result = (char8*)arena_allocate_bytes(space->arena, size, 1);
        space->used = (u64)(result - space->base) + size;
        BUSTER_CHECK(space->used <= UINT32_MAX);
        return result;
    }
    BUSTER_CHECK(space->used + size <= space->capacity);
    char8* result = space->base + space->used;
    space->used += size;
    return result;
}
// Hand back the tail of the space's most recent allocation.
BUSTER_C_INTERNAL void c_space_shrink(CSpellingSpace* space, u64 size)
{
    space->used -= size;
    if (space->arena)
    {
        arena_set_position(space->arena, space->arena->position - size);
    }
}

BUSTER_C_INTERNAL u32 c_space_offset(CSpellingSpace const* space, char8 const* pointer)
{
    return (u32)(pointer - space->base);
}

// Exact byte length of the literal spelling starting at `spelling`:
// optional encoding prefix, opening delimiter, escape-aware body, closing
// delimiter. The body loop mirrors the lexer's literal scan byte for byte —
// escape pairs skip two — so a spelling the lexer closed re-measures to the
// same length. `limit` bounds the scan for creation-time validation of a
// possibly unterminated spelling; a validated spelling passes UINT64_MAX
// because its closing delimiter, the spelling's own last byte, stops the
// scan. Returns 0 when no delimiter opens at the start.
BUSTER_C_INTERNAL u64 c_token_literal_scan_length(char8 const* spelling, u64 limit)
{
    u64 cursor = 0;
    char8 delimiter = 0;
    if (limit && (spelling[0] == '"' || spelling[0] == '\''))
    {
        delimiter = spelling[0];
        cursor = 1;
    }
    else if (limit >= 3 && spelling[0] == 'u' && spelling[1] == '8' && (spelling[2] == '"' || spelling[2] == '\''))
    {
        delimiter = spelling[2];
        cursor = 3;
    }
    else if (limit >= 2 && (spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L') && (spelling[1] == '"' || spelling[1] == '\''))
    {
        delimiter = spelling[1];
        cursor = 2;
    }
    else
    {
        return 0;
    }
    while (cursor < limit)
    {
        char8 character = spelling[cursor];
        if (character == delimiter)
        {
            return cursor + 1;
        }
        cursor += character == '\\' && cursor + 1 < limit ? 2 : 1;
    }
    return 0;
}

// The cold half of c_token_length: the field carries the sentinel, so the
// exact length is re-derived from the spelling. Creation only ever stores
// the sentinel for terminated string and character literals (see CToken).
u64 c_token_length_oversized(char8 const* spelling_base, CToken token)
{
    BUSTER_CHECK(token.kind == C_TOKEN_STRING_LITERAL || token.kind == C_TOKEN_CHARACTER_LITERAL);
    u64 length = c_token_literal_scan_length(spelling_base + token.offset, UINT64_MAX);
    BUSTER_CHECK(length >= C_TOKEN_LENGTH_OVERSIZED);
    return length;
}

// The u16 the length field stores for a spelling of `length` bytes. The
// caller owns the sentinel invariant: a spelling at or past the sentinel
// must be a terminated literal, or must be diagnosed and clamped instead.
BUSTER_C_SHARED u16 c_token_length_field(u64 length)
{
    return length < C_TOKEN_LENGTH_OVERSIZED ? (u16)length : C_TOKEN_LENGTH_OVERSIZED;
}

// A token whose spelling is a fresh copy of `text` in the space; used for
// the preprocessor's built-in macro definitions, whose replacement spellings
// must resolve through the shared base like any lexed token.
BUSTER_C_SHARED CToken c_space_token(CSpellingSpace* space, String8 text, CTokenKind kind, CPunctuator punctuator)
{
    // The sentinel is only valid on terminated literals; no other caller
    // synthesizes a spelling anywhere near it.
    BUSTER_CHECK(text.length < C_TOKEN_LENGTH_OVERSIZED || kind == C_TOKEN_STRING_LITERAL || kind == C_TOKEN_CHARACTER_LITERAL);
    char8* copy = c_space_allocate(space, text.length);
    memcpy(copy, text.pointer, text.length);
    return (CToken){
        .offset = c_space_offset(space, copy),
        .length = c_token_length_field(text.length),
        .kind = (u8)kind,
        .punctuator = (u8)punctuator,
    };
}

// Copy a token's spelling from another base into this space, keeping every
// other field; parse-side evaluators that mix real tokens with synthesized
// ones rebase everything into one local space this way.
BUSTER_C_SHARED CToken c_space_retoken(CSpellingSpace* space, char8 const* from_base, CToken token)
{
    String8 spelling = c_token_spelling(from_base, token);
    char8* copy = c_space_allocate(space, spelling.length);
    if (spelling.length)
    {
        memcpy(copy, spelling.pointer, spelling.length);
    }
    token.offset = c_space_offset(space, copy);
    return token;
}

// A transient prelude-seeded spelling space for parse-side expression
// evaluation over synthesized tokens.
BUSTER_C_SHARED CSpellingSpace c_space_local(Arena* arena, u64 capacity)
{
    CSpellingSpace space = {
        .capacity = capacity + C_SPELLING_PRELUDE_LENGTH,
    };
    space.base = arena_allocate(arena, char8, space.capacity);
    memcpy(c_space_allocate(&space, C_SPELLING_PRELUDE_LENGTH), C_SPELLING_PRELUDE_TEXT, C_SPELLING_PRELUDE_LENGTH);
    return space;
}

// The source map under construction: append-only while preprocessing runs
// (nothing queries it until the result is assembled), sorted once at the
// end. Only #line splits append out of order, so the finalize sort is a
// nearly-linear insertion pass.
typedef struct CSourceMap CSourceMap;
struct CSourceMap
{
    Arena* arena;
    IrSourceRegion* regions;
    u32 count;
    u32 capacity;
};

BUSTER_C_INTERNAL void c_source_map_append(CSourceMap* map, IrSourceRegion region)
{
    if (map->count == map->capacity)
    {
        u32 capacity = map->capacity ? map->capacity * 2 : 256;
        IrSourceRegion* regions = arena_allocate(map->arena, IrSourceRegion, capacity);
        if (map->count)
        {
            memcpy(regions, map->regions, sizeof(*regions) * map->count);
        }
        map->regions = regions;
        map->capacity = capacity;
    }
    map->regions[map->count++] = region;
}

// Hand the finished regions to the map every lookup reads, deriving the key
// array the region search and the source query run on. A trailing sentinel
// key ends the space, so a containment check needs no bounds test.
BUSTER_C_INTERNAL void c_source_map_publish(Arena* arena, CSourceMapRecovery* recovery, CSourceMap const* map)
{
    if (!map->count)
    {
        // A published key array always has a region to answer with, so the
        // lookups test the array alone and never a count as well.
        return;
    }
    IrSourceRegionKey* keys = arena_allocate(arena, IrSourceRegionKey, map->count + 1);
    for (u32 index = 0; index < map->count; index += 1)
    {
        keys[index] = (IrSourceRegionKey){
            .start = map->regions[index].start,
            .source = map->regions[index].source,
        };
    }
    keys[map->count] = (IrSourceRegionKey){.start = UINT32_MAX};
    recovery->map.keys = keys;
    recovery->map.regions = map->regions;
    recovery->map.count = map->count;
    recovery->capacity = map->capacity;
}

BUSTER_C_INTERNAL bool c_source_location_equal(CSourceLocation left, CSourceLocation right)
{
    return left.offset == right.offset && left.line == right.line && left.column == right.column && left.file == right.file;
}

BUSTER_C_SHARED bool c_ir_decode_character_value(Arena* arena, char8 const* spelling_base, CToken token, Target target, u64* value_out, CTypeKind* kind_out);

BUSTER_C_SHARED bool c_preprocess_dialect_is_c23(CPreprocessDialect dialect);

BUSTER_C_INTERNAL bool c_ascii_alpha(char8 character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

BUSTER_C_INTERNAL bool c_ascii_digit(char8 character)
{
    return character >= '0' && character <= '9';
}

BUSTER_C_INTERNAL bool c_identifier_start(char8 character)
{
    return c_ascii_alpha(character) || character == '_' || character == '$' || character >= 0x80;
}

BUSTER_C_INTERNAL bool c_identifier_continue(char8 character)
{
    return c_identifier_start(character) || c_ascii_digit(character);
}

// One byte per character value: nonzero when the character continues an
// identifier. Built from c_identifier_continue at first use so the two can
// never drift; the lexer's identifier run scan ANDs eight entries per step
// instead of branching per byte. c_prewarm() fills it ahead of any gang.
BUSTER_C_INTERNAL u8 c_identifier_continue_table[256];
BUSTER_C_INTERNAL bool c_identifier_continue_table_built;

BUSTER_C_INTERNAL void c_identifier_continue_table_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 character = 0; character < 256; character += 1)
    {
        c_identifier_continue_table[character] = c_identifier_continue((char8)character) ? 1 : 0;
    }
    c_identifier_continue_table_built = true;
}

BUSTER_C_INTERNAL u64 c_identifier_run_end(String8 source, u64 offset)
{
    if (!c_identifier_continue_table_built)
    {
        c_identifier_continue_table_build();
    }
    const u8* bytes = (const u8*)source.pointer;
    while (offset + 8 <= source.length)
    {
        u8 all = c_identifier_continue_table[bytes[offset]] & c_identifier_continue_table[bytes[offset + 1]] &
                 c_identifier_continue_table[bytes[offset + 2]] & c_identifier_continue_table[bytes[offset + 3]] &
                 c_identifier_continue_table[bytes[offset + 4]] & c_identifier_continue_table[bytes[offset + 5]] &
                 c_identifier_continue_table[bytes[offset + 6]] & c_identifier_continue_table[bytes[offset + 7]];
        if (!all)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length && c_identifier_continue_table[bytes[offset]])
    {
        offset += 1;
    }
    return offset;
}

// One byte per character value: 1 when the byte cannot end or escape a
// character/string literal (either quote kind, backslash, newline), so the
// literal scan ANDs eight entries per step like the identifier run.
BUSTER_C_INTERNAL u8 c_literal_plain_table[256];
BUSTER_C_INTERNAL bool c_literal_plain_table_built;

BUSTER_C_INTERNAL void c_literal_plain_table_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 character = 0; character < 256; character += 1)
    {
        bool special = character == '"' || character == '\'' || character == '\\' || character == '\n';
        c_literal_plain_table[character] = special ? 0 : 1;
    }
    c_literal_plain_table_built = true;
}

BUSTER_C_INTERNAL u64 c_literal_plain_run_end(String8 source, u64 offset)
{
    if (!c_literal_plain_table_built)
    {
        c_literal_plain_table_build();
    }
    const u8* bytes = (const u8*)source.pointer;
    while (offset + 8 <= source.length)
    {
        u8 all = c_literal_plain_table[bytes[offset]] & c_literal_plain_table[bytes[offset + 1]] &
                 c_literal_plain_table[bytes[offset + 2]] & c_literal_plain_table[bytes[offset + 3]] &
                 c_literal_plain_table[bytes[offset + 4]] & c_literal_plain_table[bytes[offset + 5]] &
                 c_literal_plain_table[bytes[offset + 6]] & c_literal_plain_table[bytes[offset + 7]];
        if (!all)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length && c_literal_plain_table[bytes[offset]])
    {
        offset += 1;
    }
    return offset;
}

BUSTER_C_INTERNAL bool c_horizontal_whitespace(char8 character)
{
    switch (character)
    {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    {
        return true;
    }
    default:
    {
        return false;
    }
    }
}

BUSTER_C_INLINE BUSTER_ALWAYS_INLINE u64 c_translate_plain_run_end_swar(String8 source, u64 offset)
{
    while (offset + 8 <= source.length)
    {
        u64 word;
        memcpy(&word, source.pointer + offset, sizeof(word));
        u64 carriage = word ^ UINT64_C(0x0D0D0D0D0D0D0D0D);
        u64 newline = word ^ UINT64_C(0x0A0A0A0A0A0A0A0A);
        u64 backslash = word ^ UINT64_C(0x5C5C5C5C5C5C5C5C);
        u64 low = UINT64_C(0x0101010101010101);
        u64 high = UINT64_C(0x8080808080808080);
        u64 found = (((carriage - low) & ~carriage) | ((newline - low) & ~newline) | ((backslash - low) & ~backslash)) & high;
        if (found)
        {
            break;
        }
        offset += 8;
    }
    while (offset < source.length)
    {
        char8 probe = source.pointer[offset];
        if (probe == '\r' || probe == '\n' || probe == '\\')
        {
            break;
        }
        offset += 1;
    }
    return offset;
}

#if BUSTER_C_TRANSLATE_AVX512
BUSTER_C_INLINE BUSTER_ALWAYS_INLINE u64 c_translate_plain_run_end_avx512(String8 source, u64 offset)
{
    if (source.length - offset >= 64)
    {
        __m512i carriage = _mm512_set1_epi8('\r');
        __m512i newline = _mm512_set1_epi8('\n');
        __m512i backslash = _mm512_set1_epi8('\\');
        do
        {
            __m512i chunk = _mm512_loadu_si512((const void*)(source.pointer + offset));
            __mmask64 found = _mm512_cmpeq_epi8_mask(chunk, carriage) | _mm512_cmpeq_epi8_mask(chunk, newline) |
                               _mm512_cmpeq_epi8_mask(chunk, backslash);
            if (found)
            {
                return offset + (u64)__builtin_ctzll((u64)found);
            }
            offset += 64;
        } while (source.length - offset >= 64);
    }
    return c_translate_plain_run_end_swar(source, offset);
}
#endif

#if BUSTER_INCLUDE_TESTS
BUSTER_C_INTERNAL u64 c_translate_plain_run_end_scalar(String8 source, u64 offset)
{
    while (offset < source.length)
    {
        char8 probe = source.pointer[offset];
        if (probe == '\r' || probe == '\n' || probe == '\\')
        {
            break;
        }
        offset += 1;
    }
    return offset;
}

bool c_test_translate_plain_run_paths_agree(String8 source)
{
    if (source.length && !source.pointer)
    {
        return false;
    }
    u64 offset = 0;
    for (;;)
    {
        u64 scalar_end = c_translate_plain_run_end_scalar(source, offset);
        if (c_translate_plain_run_end_swar(source, offset) != scalar_end)
        {
            return false;
        }
#if BUSTER_C_TRANSLATE_AVX512
        if (c_translate_plain_run_end_avx512(source, offset) != scalar_end)
        {
            return false;
        }
#endif
        if (offset == source.length)
        {
            break;
        }
        offset += 1;
    }
    return true;
}
#endif

BUSTER_C_INTERNAL CTranslatedSource c_translate_source(Arena* arena, CSpellingSpace* space, String8 source)
{
    CTranslatedSource result = {0};
    if (source.length <= UINT32_MAX - 2)
    {
        char8* translated = space ? c_space_allocate(space, source.length + 1) : arena_allocate(arena, char8, source.length + 1);
        result.translated_offset = space ? c_space_offset(space, translated) : 0;
        IrSourceCheckpoint* checkpoints = arena_allocate(arena, IrSourceCheckpoint, source.length + 2);
        u32* checkpoint_offsets = arena_allocate(arena, u32, source.length + 2);
        u32 checkpoint_count = 0;
        u64 input = 0;
        u64 output = 0;
        u32 line = 1;
        u32 column = 1;
        // The next output byte starts a new linear run; initially true so the
        // first byte records the first checkpoint.
        bool run_broken = true;
        while (input < source.length)
        {
            // A run containing no '\r', '\n', or '\\' keeps line/column linear,
            // so it copies through whole and only those three bytes reach the exact
            // scalar handling below. Native AVX-512 hosts classify 64 bytes at a
            // time; every fallback retains the previous eight-byte SWAR scan.
#if BUSTER_C_TRANSLATE_AVX512
            u64 plain_end = c_translate_plain_run_end_avx512(source, input);
#else
            u64 plain_end = c_translate_plain_run_end_swar(source, input);
#endif
            if (plain_end > input)
            {
                if (run_broken)
                {
                    checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                        .offset = (u32)input,
                        .line = line,
                        .column = column,
                    };
                    checkpoint_offsets[checkpoint_count] = (u32)output;
                    checkpoint_count += 1;
                    run_broken = false;
                }
                u64 plain_length = plain_end - input;
                memcpy(translated + output, source.pointer + input, plain_length);
                output += plain_length;
                column += (u32)plain_length;
                input = plain_end;
                continue;
            }
            char8 character = source.pointer[input];
            u64 newline_length = 0;
            if (character == '\r')
            {
                newline_length = input + 1 < source.length && source.pointer[input + 1] == '\n' ? 2 : 1;
            }
            else if (character == '\n')
            {
                newline_length = 1;
            }
            if (character == '\\' && input + 1 < source.length)
            {
                u64 splice_length = 0;
                if (source.pointer[input + 1] == '\n')
                {
                    splice_length = 2;
                }
                else if (source.pointer[input + 1] == '\r')
                {
                    splice_length = input + 2 < source.length && source.pointer[input + 2] == '\n' ? 3 : 2;
                }
                if (splice_length)
                {
                    input += splice_length;
                    line += 1;
                    column = 1;
                    run_broken = true;
                    continue;
                }
            }
            if (run_broken)
            {
                checkpoints[checkpoint_count] = (IrSourceCheckpoint){
                    .offset = (u32)input,
                    .line = line,
                    .column = column,
                };
                checkpoint_offsets[checkpoint_count] = (u32)output;
                checkpoint_count += 1;
                run_broken = false;
            }
            if (newline_length)
            {
                translated[output++] = '\n';
                input += newline_length;
                line += 1;
                column = 1;
                run_broken = true;
            }
            else
            {
                translated[output++] = character;
                input += 1;
                column += 1;
            }
        }
        translated[output] = 0;
        checkpoints[checkpoint_count] = (IrSourceCheckpoint){
            .offset = (u32)source.length,
            .line = line,
            .column = column,
        };
        checkpoint_offsets[checkpoint_count] = (u32)output;
        checkpoint_count += 1;
        if (space)
        {
            // The translated copy only shrinks (splices delete bytes); hand the
            // unused tail back so the next spelling packs against it.
            c_space_shrink(space, source.length - output);
        }
        result.source = (String8){
            .pointer = translated,
            .length = output,
        };
        result.checkpoints = checkpoints;
        result.checkpoint_offsets = checkpoint_offsets;
        result.checkpoint_count = checkpoint_count;
        // The page bracket for the checkpoints, built in the one linear pass the
        // finished offsets allow. It is what keeps a line-table consumer's
        // per-line lookup from binary-searching the whole file.
        result.checkpoint_page_count = (u32)(output >> IR_SOURCE_CHECKPOINT_PAGE_SHIFT) + 1;
        result.checkpoint_pages = arena_allocate(arena, u32, result.checkpoint_page_count);
        u32 fill_checkpoint = 0;
        for (u32 page = 0; page < result.checkpoint_page_count; page += 1)
        {
            u32 page_start = page << IR_SOURCE_CHECKPOINT_PAGE_SHIFT;
            while (fill_checkpoint + 1 < checkpoint_count && checkpoint_offsets[fill_checkpoint + 1] <= page_start)
            {
                fill_checkpoint += 1;
            }
            result.checkpoint_pages[page] = fill_checkpoint;
        }
        // `line` counts breaks, so it is one past the lines that ended; a column
        // past the first means bytes followed the last break and opened one more.
        result.raw_lines = line - 1 + (column > 1);
    }

    return result;
}

// Line/column recovery for a translated-source-local offset through the
// retained checkpoints, amortized by the result's cursor: queries at
// non-decreasing offsets advance instead of searching.
BUSTER_C_INTERNAL CSourceLocation c_lex_local_location(CLexResult* result, u64 offset)
{
    if (!result->checkpoint_count)
    {
        return (CSourceLocation){0};
    }
    u32 cursor = result->location_cursor;
    while (cursor + 1 < result->checkpoint_count && result->checkpoint_offsets[cursor + 1] <= offset)
    {
        cursor += 1;
    }
    while (cursor && result->checkpoint_offsets[cursor] > offset)
    {
        cursor -= 1;
    }
    result->location_cursor = cursor;
    IrSourceCheckpoint checkpoint = result->checkpoints[cursor];
    u32 delta = (u32)offset - result->checkpoint_offsets[cursor];
    return (CSourceLocation){
        .offset = checkpoint.offset + delta,
        .line = checkpoint.line,
        .column = checkpoint.column + delta,
        .map_offset = (u32)offset + result->translated_offset,
    };
}

CSourceLocation c_lex_token_location(CLexResult* lex, CToken token)
{
    return c_lex_local_location(lex, token.offset - lex->translated_offset);
}

// The stamp a macro invocation's output carries: one position every offset
// in the copied run resolves to.
BUSTER_C_INTERNAL IrSourcePosition c_position_from_source_location(CSourceLocation location)
{
    return (IrSourcePosition){
        .source = location.file,
        .offset = location.offset,
        .line = location.line,
        .column = location.column,
    };
}

// A position, in the shape this frontend's diagnostics take.
BUSTER_C_INTERNAL CSourceLocation c_source_location_from_position(u32 map_offset, IrSourcePosition position)
{
    return (CSourceLocation){
        .offset = position.offset,
        .line = position.line,
        .column = position.column,
        .file = position.source,
        .map_offset = map_offset,
    };
}

CSourceLocation c_preprocess_token_location(CPreprocessResult const* preprocess, CToken token)
{
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return c_source_location_from_position(token.offset, recovery ? ir_source_map_position(&recovery->map, token.offset, 0) : (IrSourcePosition){0});
}

// The amortized variant for consumers whose queries mostly ascend (parsing
// walks tokens roughly in stream order).
BUSTER_C_SHARED CSourceLocation c_preprocess_token_location_cursor(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor)
{
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return c_source_location_from_position(token.offset,
                                           recovery ? ir_source_map_position(&recovery->map, token.offset, cursor) : (IrSourcePosition){0});
}

// Which source a token belongs to, without recovering its line and column:
// what lowering wants, since an IR source range is a source plus the offset
// the token already carries.
BUSTER_C_SHARED u32 c_preprocess_token_source(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor)
{
    CSourceMapRecovery const* recovery = preprocess->recovery;
    return recovery ? ir_source_map_source(&recovery->map, token.offset, cursor) : 0;
}

// One finished line, classified by what the lexer saw on it. Branchless: the
// flags are 0/1 and every bucket is an unconditional add, so a line of code,
// a line of comment and a blank line all cost the same four adds.
BUSTER_C_INTERNAL void c_source_metrics_line(CSourceMetrics* metrics, u32 has_code, u32 has_comment)
{
    metrics->translated_lines += 1;
    metrics->code_lines += has_code;
    metrics->comment_lines += has_comment;
    metrics->mixed_lines += has_code & has_comment;
    metrics->blank_lines += (has_code | has_comment) ^ 1;
}

void c_source_metrics_add(CSourceMetrics* total, CSourceMetrics const* part)
{
    total->files += part->files;
    total->bytes += part->bytes;
    total->translated_bytes += part->translated_bytes;
    total->lines += part->lines;
    total->translated_lines += part->translated_lines;
    total->spliced_lines += part->spliced_lines;
    total->code_lines += part->code_lines;
    total->comment_lines += part->comment_lines;
    total->mixed_lines += part->mixed_lines;
    total->blank_lines += part->blank_lines;
    total->comment_bytes += part->comment_bytes;
    total->blank_bytes += part->blank_bytes;
    total->literal_bytes += part->literal_bytes;
    total->comments += part->comments;
    total->tokens += part->tokens;
}

u64 c_source_metrics_code_bytes(CSourceMetrics metrics)
{
    return metrics.translated_bytes - metrics.comment_bytes - metrics.blank_bytes;
}

// Paths already lexed, so the unique aggregate counts a header once however
// often it is included. Hashes only: two distinct paths colliding on 64 bits
// would merge into one entry, which is cheaper to accept than a string table
// on a counter that feeds no decision.
typedef struct CSourceMetricsFileSet CSourceMetricsFileSet;
struct CSourceMetricsFileSet
{
    u64* hashes;
    u32 capacity;
    u32 count;
};

BUSTER_C_INTERNAL bool c_source_metrics_file_first(Arena* arena, CSourceMetricsFileSet* set, String8 path)
{
    if (set->count * 2 >= set->capacity)
    {
        u32 capacity = set->capacity ? set->capacity * 2 : 256;
        u64* hashes = arena_allocate(arena, u64, capacity);
        memset(hashes, 0, capacity * sizeof(*hashes));
        for (u32 index = 0; index < set->capacity; index += 1)
        {
            u64 moved = set->hashes[index];
            if (moved)
            {
                u32 slot = (u32)moved & (capacity - 1);
                while (hashes[slot])
                {
                    slot = (slot + 1) & (capacity - 1);
                }
                hashes[slot] = moved;
            }
        }
        set->hashes = hashes;
        set->capacity = capacity;
    }
    // Zero marks an empty slot, so no path may hash to it.
    u64 hash = buster_hash_64((u8*)path.pointer, path.length) | 1;
    u32 slot = (u32)hash & (set->capacity - 1);
    while (set->hashes[slot])
    {
        if (set->hashes[slot] == hash)
        {
            return false;
        }
        slot = (slot + 1) & (set->capacity - 1);
    }
    set->hashes[slot] = hash;
    set->count += 1;
    return true;
}

// The cold tail of c_token_push: the item is 0xFFFF bytes or longer. A
// terminated literal — the scan bounded by the item's extent reproduces the
// exact length, which certifies both termination and escape parity — stores
// the sentinel c_token_length re-derives from. Everything else (an
// unterminated literal, an identifier or number the call site diagnoses)
// clamps to the largest direct length; the stream already carries an error
// diagnostic in every clamping case, so the lie never reaches a compile
// that succeeds, and a clamped read stays inside the real spelling.
BUSTER_C_INTERNAL u16 c_token_push_long_length(CTranslatedSource translated, u64 start, u64 end, CTokenKind kind)
{
    u64 length = end - start;
    bool literal = kind == C_TOKEN_STRING_LITERAL || kind == C_TOKEN_CHARACTER_LITERAL;
    bool exact = literal && c_token_literal_scan_length(translated.source.pointer + start, length) == length;
    return exact ? C_TOKEN_LENGTH_OVERSIZED : C_TOKEN_LENGTH_OVERSIZED - 1;
}

BUSTER_C_INTERNAL void c_token_push(CLexResult* result, CTranslatedSource translated, u64 start, u64 end, CTokenKind kind, CPunctuator punctuator)
{
    u64 length = end - start;
    u16 stored = length < C_TOKEN_LENGTH_OVERSIZED ? (u16)length : c_token_push_long_length(translated, start, end, kind);
    result->tokens[result->token_count++] = (CToken){
        .offset = translated.translated_offset + (u32)start,
        .length = stored,
        .kind = (u8)kind,
        .punctuator = (u8)punctuator,
    };
}

BUSTER_C_INTERNAL void c_diagnostic_push(CLexResult* result, Arena* diagnostic_arena, u64* diagnostic_capacity, u64 maximum_diagnostic_count,
                                           u64 offset, CDiagnosticKind kind, String8 message)
{
    BUSTER_CHECK(result->diagnostic_count < maximum_diagnostic_count);
    if (result->diagnostic_count == *diagnostic_capacity)
    {
        u64 capacity = *diagnostic_capacity > maximum_diagnostic_count / 2 ? maximum_diagnostic_count : *diagnostic_capacity * 2;
        CDiagnostic* diagnostics = arena_allocate(diagnostic_arena, CDiagnostic, capacity);
        memcpy(diagnostics, result->diagnostics, sizeof(*diagnostics) * result->diagnostic_count);
        result->diagnostics = diagnostics;
        *diagnostic_capacity = capacity;
    }
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = c_lex_local_location(result, offset),
        .kind = kind,
        .severity = C_DIAGNOSTIC_ERROR,
    };
}

// Indexed by CPunctuator, so the id and the spelling cannot drift apart.  The
// scan walks the table in enum order, which CPunctuator declares longest-first
// for maximal munch.
BUSTER_C_INTERNAL String8 const c_punctuator_spellings[C_PUNCTUATOR_COUNT] = {
    [C_PUNCTUATOR_HASH_HASH_DIGRAPH] = S8_INITIALIZER("%:%:"),
    [C_PUNCTUATOR_SHIFT_LEFT_ASSIGN] = S8_INITIALIZER("<<="),
    [C_PUNCTUATOR_SHIFT_RIGHT_ASSIGN] = S8_INITIALIZER(">>="),
    [C_PUNCTUATOR_ELLIPSIS] = S8_INITIALIZER("..."),
    [C_PUNCTUATOR_ARROW] = S8_INITIALIZER("->"),
    [C_PUNCTUATOR_PLUS_PLUS] = S8_INITIALIZER("++"),
    [C_PUNCTUATOR_MINUS_MINUS] = S8_INITIALIZER("--"),
    [C_PUNCTUATOR_SHIFT_LEFT] = S8_INITIALIZER("<<"),
    [C_PUNCTUATOR_SHIFT_RIGHT] = S8_INITIALIZER(">>"),
    [C_PUNCTUATOR_LESS_EQUAL] = S8_INITIALIZER("<="),
    [C_PUNCTUATOR_GREATER_EQUAL] = S8_INITIALIZER(">="),
    [C_PUNCTUATOR_EQUAL] = S8_INITIALIZER("=="),
    [C_PUNCTUATOR_NOT_EQUAL] = S8_INITIALIZER("!="),
    [C_PUNCTUATOR_AMPERSAND_AMPERSAND] = S8_INITIALIZER("&&"),
    [C_PUNCTUATOR_PIPE_PIPE] = S8_INITIALIZER("||"),
    [C_PUNCTUATOR_STAR_ASSIGN] = S8_INITIALIZER("*="),
    [C_PUNCTUATOR_SLASH_ASSIGN] = S8_INITIALIZER("/="),
    [C_PUNCTUATOR_PERCENT_ASSIGN] = S8_INITIALIZER("%="),
    [C_PUNCTUATOR_PLUS_ASSIGN] = S8_INITIALIZER("+="),
    [C_PUNCTUATOR_MINUS_ASSIGN] = S8_INITIALIZER("-="),
    [C_PUNCTUATOR_AMPERSAND_ASSIGN] = S8_INITIALIZER("&="),
    [C_PUNCTUATOR_CARET_ASSIGN] = S8_INITIALIZER("^="),
    [C_PUNCTUATOR_PIPE_ASSIGN] = S8_INITIALIZER("|="),
    [C_PUNCTUATOR_HASH_HASH] = S8_INITIALIZER("##"),
    [C_PUNCTUATOR_LEFT_BRACKET_DIGRAPH] = S8_INITIALIZER("<:"),
    [C_PUNCTUATOR_RIGHT_BRACKET_DIGRAPH] = S8_INITIALIZER(":>"),
    [C_PUNCTUATOR_LEFT_BRACE_DIGRAPH] = S8_INITIALIZER("<%"),
    [C_PUNCTUATOR_RIGHT_BRACE_DIGRAPH] = S8_INITIALIZER("%>"),
    [C_PUNCTUATOR_HASH_DIGRAPH] = S8_INITIALIZER("%:"),
    [C_PUNCTUATOR_LEFT_BRACKET] = S8_INITIALIZER("["),
    [C_PUNCTUATOR_RIGHT_BRACKET] = S8_INITIALIZER("]"),
    [C_PUNCTUATOR_LEFT_PARENTHESIS] = S8_INITIALIZER("("),
    [C_PUNCTUATOR_RIGHT_PARENTHESIS] = S8_INITIALIZER(")"),
    [C_PUNCTUATOR_LEFT_BRACE] = S8_INITIALIZER("{"),
    [C_PUNCTUATOR_RIGHT_BRACE] = S8_INITIALIZER("}"),
    [C_PUNCTUATOR_DOT] = S8_INITIALIZER("."),
    [C_PUNCTUATOR_AMPERSAND] = S8_INITIALIZER("&"),
    [C_PUNCTUATOR_STAR] = S8_INITIALIZER("*"),
    [C_PUNCTUATOR_PLUS] = S8_INITIALIZER("+"),
    [C_PUNCTUATOR_MINUS] = S8_INITIALIZER("-"),
    [C_PUNCTUATOR_TILDE] = S8_INITIALIZER("~"),
    [C_PUNCTUATOR_EXCLAMATION] = S8_INITIALIZER("!"),
    [C_PUNCTUATOR_SLASH] = S8_INITIALIZER("/"),
    [C_PUNCTUATOR_PERCENT] = S8_INITIALIZER("%"),
    [C_PUNCTUATOR_LESS] = S8_INITIALIZER("<"),
    [C_PUNCTUATOR_GREATER] = S8_INITIALIZER(">"),
    [C_PUNCTUATOR_CARET] = S8_INITIALIZER("^"),
    [C_PUNCTUATOR_PIPE] = S8_INITIALIZER("|"),
    [C_PUNCTUATOR_QUESTION] = S8_INITIALIZER("?"),
    [C_PUNCTUATOR_COLON] = S8_INITIALIZER(":"),
    [C_PUNCTUATOR_SEMICOLON] = S8_INITIALIZER(";"),
    [C_PUNCTUATOR_ASSIGN] = S8_INITIALIZER("="),
    [C_PUNCTUATOR_COMMA] = S8_INITIALIZER(","),
    [C_PUNCTUATOR_HASH] = S8_INITIALIZER("#"),
    [C_PUNCTUATOR_AT] = S8_INITIALIZER("@"),
    [C_PUNCTUATOR_BACKSLASH] = S8_INITIALIZER("\\"),
};

// First-byte dispatch over c_punctuator_spellings, derived from the table at
// first use so the spellings stay the single source of truth. Enum order is
// longest-first, so each per-byte group inherits maximal munch. Without it,
// every lexed punctuator paid a linear scan of the whole table — a `;` sat
// behind ~52 failed memcmp probes.
typedef struct CPunctuatorDispatch CPunctuatorDispatch;
struct CPunctuatorDispatch
{
    u8 start;
    u8 count;
};

BUSTER_C_INTERNAL u8 c_punctuator_dispatch_order[C_PUNCTUATOR_COUNT];
BUSTER_C_INTERNAL CPunctuatorDispatch c_punctuator_dispatch[256];
BUSTER_C_INTERNAL bool c_punctuator_dispatch_built;

BUSTER_C_INTERNAL void c_punctuator_dispatch_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    u32 cursor = 0;
    for (u32 first = 0; first < 256; first += 1)
    {
        u32 start = cursor;
        for (u32 punctuator_index = C_PUNCTUATOR_NONE + 1; punctuator_index < C_PUNCTUATOR_COUNT; punctuator_index += 1)
        {
            String8 spelling = c_punctuator_spellings[punctuator_index];
            if (spelling.length && (u8)spelling.pointer[0] == first)
            {
                c_punctuator_dispatch_order[cursor++] = (u8)punctuator_index;
            }
        }
        c_punctuator_dispatch[first] = (CPunctuatorDispatch){
            .start = (u8)start,
            .count = (u8)(cursor - start),
        };
    }
    c_punctuator_dispatch_built = true;
}

BUSTER_C_INTERNAL u64 c_punctuator_length(String8 source, u64 offset, CPunctuator* punctuator_out)
{
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    if (offset < source.length)
    {
        CPunctuatorDispatch dispatch = c_punctuator_dispatch[(u8)source.pointer[offset]];
        for (u32 order_index = 0; order_index < dispatch.count; order_index += 1)
        {
            u32 punctuator_index = c_punctuator_dispatch_order[dispatch.start + order_index];
            String8 punctuator = c_punctuator_spellings[punctuator_index];
            if (punctuator.length > source.length - offset)
            {
                continue;
            }
            bool match = true;
            for (u64 byte_index = 1; byte_index < punctuator.length; byte_index += 1)
            {
                if (source.pointer[offset + byte_index] != punctuator.pointer[byte_index])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                *punctuator_out = (CPunctuator)punctuator_index;
                return punctuator.length;
            }
        }
    }
    *punctuator_out = C_PUNCTUATOR_NONE;
    return 0;
}

BUSTER_C_INTERNAL bool c_literal_prefix(String8 source, u64 offset, u64* prefix_length, char8* delimiter)
{
    *prefix_length = 0;
    *delimiter = 0;
    if (offset < source.length)
    {
        char8 character = source.pointer[offset];
        if (character == '\'' || character == '"')
        {
            *delimiter = character;
            return true;
        }
        if (character == 'u' && offset + 2 < source.length && source.pointer[offset + 1] == '8' &&
            (source.pointer[offset + 2] == '\'' || source.pointer[offset + 2] == '"'))
        {
            *prefix_length = 2;
            *delimiter = source.pointer[offset + 2];
            return true;
        }
        if ((character == 'u' || character == 'U' || character == 'L') && offset + 1 < source.length &&
            (source.pointer[offset + 1] == '\'' || source.pointer[offset + 1] == '"'))
        {
            *prefix_length = 1;
            *delimiter = source.pointer[offset + 1];
            return true;
        }
    }

    return false;
}

// Everything one lex mutates, so the scalar reference loop and the compaction
// emitter below drive tokens, diagnostics and measurement through exactly the
// same code.  The three line fields are the only state that crosses a
// compaction window: they describe the line under construction, never the
// tokenizer, so the window pipeline still starts every window from scratch.
typedef struct CLexState CLexState;
struct CLexState
{
    CLexResult* result;
    CTranslatedSource translated;
    Arena* diagnostic_arena;
    u64 diagnostic_capacity;
    u64 maximum_diagnostic_count;
    // Offset just past the newline that ended the last line, so a file whose
    // last line has no terminating newline still ends a line.
    u64 line_start;
    u64 newline_tokens;
    u32 line_has_code;
    u32 line_has_comment;
};

// One item of the scalar lexer: a whitespace byte, a newline, a comment, or a
// token, starting at `offset` and returning the offset just past it.  The
// compaction emitter escapes to this for every shape its masks do not model,
// so the two paths agree on the hard cases by construction rather than by
// duplicated reasoning.
BUSTER_C_INTERNAL u64 c_lex_scan_one(CLexState* state, u64 offset)
{
    CLexResult* result = state->result;
    CTranslatedSource translated = state->translated;
    char8 character = translated.source.pointer[offset];
    if (c_horizontal_whitespace(character))
    {
        result->metrics.blank_bytes += 1;
        return offset + 1;
    }
    if (character == '\n')
    {
        c_token_push(result, translated, offset, offset + 1, C_TOKEN_NEWLINE, C_PUNCTUATOR_NONE);
        c_source_metrics_line(&result->metrics, state->line_has_code, state->line_has_comment);
        result->metrics.blank_bytes += 1;
        state->newline_tokens += 1;
        state->line_has_code = 0;
        state->line_has_comment = 0;
        state->line_start = offset + 1;
        return offset + 1;
    }
    if (character == '/' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '/')
    {
        u64 comment_start = offset;
        offset += 2;
        while (offset < translated.source.length && translated.source.pointer[offset] != '\n')
        {
            offset += 1;
        }
        result->metrics.comment_bytes += offset - comment_start;
        result->metrics.comments += 1;
        state->line_has_comment = 1;
        return offset;
    }
    if (character == '/' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '*')
    {
        u64 comment_start = offset;
        offset += 2;
        bool terminated = false;
        // Set before the scan so the lines a multi-line comment crosses are
        // all attributed to it as they end.
        state->line_has_comment = 1;
        while (offset < translated.source.length)
        {
            if (translated.source.pointer[offset] == '*' && offset + 1 < translated.source.length && translated.source.pointer[offset + 1] == '/')
            {
                offset += 2;
                terminated = true;
                break;
            }
            if (translated.source.pointer[offset] == '\n')
            {
                c_token_push(result, translated, offset, offset + 1, C_TOKEN_NEWLINE, C_PUNCTUATOR_NONE);
                c_source_metrics_line(&result->metrics, state->line_has_code, 1);
                state->newline_tokens += 1;
                state->line_has_code = 0;
                state->line_start = offset + 1;
            }
            offset += 1;
        }
        result->metrics.comment_bytes += offset - comment_start;
        result->metrics.comments += 1;
        if (!terminated)
        {
            c_diagnostic_push(result, state->diagnostic_arena, &state->diagnostic_capacity, state->maximum_diagnostic_count, comment_start,
                              C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT, S8("unterminated block comment"));
        }
        return offset;
    }
    // Everything past this point produces a token, so one store here covers
    // every kind instead of one per lexing branch.
    state->line_has_code = 1;
    u64 literal_prefix_length = 0;
    char8 literal_delimiter = 0;
    if (c_literal_prefix(translated.source, offset, &literal_prefix_length, &literal_delimiter))
    {
        u64 start = offset;
        offset += literal_prefix_length + 1;
        bool terminated = false;
        while (offset < translated.source.length)
        {
            offset = c_literal_plain_run_end(translated.source, offset);
            if (offset >= translated.source.length)
            {
                break;
            }
            character = translated.source.pointer[offset];
            if (character == literal_delimiter)
            {
                offset += 1;
                terminated = true;
                break;
            }
            if (character == '\n')
            {
                break;
            }
            if (character == '\\' && offset + 1 < translated.source.length)
            {
                offset += 2;
            }
            else
            {
                offset += 1;
            }
        }
        CTokenKind kind = literal_delimiter == '\'' ? C_TOKEN_CHARACTER_LITERAL : C_TOKEN_STRING_LITERAL;
        c_token_push(result, translated, start, offset, kind, C_PUNCTUATOR_NONE);
        result->metrics.literal_bytes += offset - start;
        if (!terminated)
        {
            c_diagnostic_push(result, state->diagnostic_arena, &state->diagnostic_capacity, state->maximum_diagnostic_count, start,
                              literal_delimiter == '\'' ? C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL : C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL,
                              literal_delimiter == '\'' ? S8("unterminated character literal") : S8("unterminated string literal"));
        }
        return offset;
    }
    if (c_identifier_start(character))
    {
        u64 start = offset;
        offset = c_identifier_run_end(translated.source, offset + 1);
        c_token_push(result, translated, start, offset, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        if (BUSTER_UNLIKELY(offset - start >= C_TOKEN_LENGTH_OVERSIZED))
        {
            c_diagnostic_push(result, state->diagnostic_arena, &state->diagnostic_capacity, state->maximum_diagnostic_count, start,
                              C_DIAGNOSTIC_TOKEN_TOO_LONG, S8("identifier exceeds 65534 bytes"));
        }
        return offset;
    }
    if (c_ascii_digit(character) || (character == '.' && offset + 1 < translated.source.length && c_ascii_digit(translated.source.pointer[offset + 1])))
    {
        u64 start = offset++;
        while (offset < translated.source.length)
        {
            character = translated.source.pointer[offset];
            bool exponent_sign = (character == '+' || character == '-') && offset > start &&
                                 (translated.source.pointer[offset - 1] == 'e' || translated.source.pointer[offset - 1] == 'E' ||
                                  translated.source.pointer[offset - 1] == 'p' || translated.source.pointer[offset - 1] == 'P');
            if (!c_identifier_continue(character) && character != '.' && character != '\'' && !exponent_sign)
            {
                break;
            }
            offset += 1;
        }
        c_token_push(result, translated, start, offset, C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
        if (BUSTER_UNLIKELY(offset - start >= C_TOKEN_LENGTH_OVERSIZED))
        {
            c_diagnostic_push(result, state->diagnostic_arena, &state->diagnostic_capacity, state->maximum_diagnostic_count, start,
                              C_DIAGNOSTIC_TOKEN_TOO_LONG, S8("preprocessing number exceeds 65534 bytes"));
        }
        return offset;
    }
    CPunctuator punctuator = C_PUNCTUATOR_NONE;
    u64 punctuator_length = c_punctuator_length(translated.source, offset, &punctuator);
    if (punctuator_length)
    {
        c_token_push(result, translated, offset, offset + punctuator_length, C_TOKEN_PUNCTUATOR, punctuator);
        return offset + punctuator_length;
    }
    c_token_push(result, translated, offset, offset + 1, C_TOKEN_INVALID, C_PUNCTUATOR_NONE);
    c_diagnostic_push(result, state->diagnostic_arena, &state->diagnostic_capacity, state->maximum_diagnostic_count, offset, C_DIAGNOSTIC_INVALID_CHARACTER,
                      string_format(state->diagnostic_arena, S8("invalid character byte {u32} in C source"), (u32)character));
    return offset + 1;
}

BUSTER_C_INTERNAL void c_lex_scalar(CLexState* state)
{
    u64 length = state->translated.source.length;
    u64 offset = 0;
    while (offset < length)
    {
        offset = c_lex_scan_one(state, offset);
    }
}

#if BUSTER_C_LEX_COMPACT

// Deus-Lex-Machina compaction emitter for the C lexer (see the AGENTS.md SIMD
// lexing/parsing method notes and validark.dev/posts/deus-lex-machina), the
// same architecture the buster tokenizer runs: the translated source is walked
// in token-aligned 64-byte windows; each window classifies once into per-class
// bitmasks; quote, comment and escape spans resolve by mask arithmetic with a
// forward-seeking cursor (escape parity per the simdjson backslash algorithm);
// multi-character punctuators legalize through a bit-channel vpermi2b NFA
// (three per-position tables AND-ed, one channel per punctuator family);
// preprocessing-number extents fall out of one count-trailing-ones over a
// continuation mask; and every complete token then materializes at once —
// vpcompressb pulls start and end positions of an iota vector through the
// token-boundary masks, a byte subtract yields all lengths, and the kind and
// punctuator vectors compress by the same starts mask before widening
// interleaved stores write the 12-byte CToken rows eight at a time.
//
// Windows always begin at an item boundary, so no lexer state crosses a
// window: the item touching a window's last byte is deferred and rescanned by
// the next window.  The shapes the masks do not model — invalid bytes,
// unterminated literals, unterminated block comments, and any single item
// longer than a window (long comments, long identifiers, long literals) —
// escape to c_lex_scan_one, which is the code the scalar loop runs, so both
// paths agree on every hard case by construction.

BUSTER_CT_CHECK(C_PUNCTUATOR_NONE == 0);
BUSTER_CT_CHECK(C_TOKEN_INVALID == 0);

// Bit channels of the punctuator NFA.  A byte pair (or triple) spells a
// punctuator when the AND of the per-position table lookups is nonzero in some
// channel; the two channels marked "equal-byte" additionally require the first
// two bytes to be identical, which one vpcmpeqb supplies and which is what
// keeps the seven doubled spellings from needing a channel each.
enum
{
    C_LEX_NFA_ASSIGN = 1 << 0,
    C_LEX_NFA_DOUBLE = 1 << 1,
    C_LEX_NFA_ARROW = 1 << 2,
    C_LEX_NFA_LESS_PAIR = 1 << 3,
    C_LEX_NFA_PERCENT_PAIR = 1 << 4,
    C_LEX_NFA_SHIFT_ASSIGN = 1 << 5,
    C_LEX_NFA_ELLIPSIS = 1 << 6,
    C_LEX_NFA_PAIR_CHANNELS = C_LEX_NFA_ASSIGN | C_LEX_NFA_ARROW | C_LEX_NFA_LESS_PAIR | C_LEX_NFA_PERCENT_PAIR,
};

enum
{
    C_LEX_PAIR_TABLE_SIZE = 16,
};

BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_single_punctuators[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_first[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_second[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_nfa_third[128];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_iota[64];
BUSTER_C_INTERNAL _Alignas(64) u8 c_lex_rotate_eight[64];
// Two-character spellings, keyed by densely numbered first and second bytes so
// the whole cross product fits one cache line instead of a 64 KB table.
BUSTER_C_INTERNAL u8 c_lex_pair_row[128];
BUSTER_C_INTERNAL u8 c_lex_pair_column[128];
BUSTER_C_INTERNAL u8 c_lex_pair_punctuators[C_LEX_PAIR_TABLE_SIZE][C_LEX_PAIR_TABLE_SIZE];
BUSTER_C_INTERNAL u8 c_lex_triple_punctuators[128];
BUSTER_C_INTERNAL bool c_lex_compact_tables_built;

// Every spelling table below is derived from c_punctuator_spellings, so a new
// punctuator cannot drift out of the emitter's tables.  The NFA channels
// themselves are hand-assigned; c_test_frontend_lex_punctuator_nfa validates
// them exhaustively against c_punctuator_length.
BUSTER_C_INTERNAL void c_lex_compact_tables_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 index = 0; index < 64; index += 1)
    {
        c_lex_iota[index] = (u8)index;
        c_lex_rotate_eight[index] = (u8)((index + 8) & 63);
    }
    memset(c_lex_pair_row, 0xFF, sizeof(c_lex_pair_row));
    memset(c_lex_pair_column, 0xFF, sizeof(c_lex_pair_column));
    u32 row_count = 0;
    u32 column_count = 0;
    for (u32 index = C_PUNCTUATOR_NONE + 1; index < C_PUNCTUATOR_COUNT; index += 1)
    {
        String8 spelling = c_punctuator_spellings[index];
        BUSTER_CHECK(spelling.length && (u8)spelling.pointer[0] < 128);
        u8 first = (u8)spelling.pointer[0];
        if (spelling.length == 1)
        {
            c_lex_single_punctuators[first] = (u8)index;
        }
        else if (spelling.length == 2)
        {
            u8 second = (u8)spelling.pointer[1];
            BUSTER_CHECK(second < 128);
            if (c_lex_pair_row[first] == 0xFF)
            {
                c_lex_pair_row[first] = (u8)row_count++;
            }
            if (c_lex_pair_column[second] == 0xFF)
            {
                c_lex_pair_column[second] = (u8)column_count++;
            }
            BUSTER_CHECK(row_count <= C_LEX_PAIR_TABLE_SIZE && column_count <= C_LEX_PAIR_TABLE_SIZE);
            c_lex_pair_punctuators[c_lex_pair_row[first]][c_lex_pair_column[second]] = (u8)index;
        }
        else if (spelling.length == 3)
        {
            BUSTER_CHECK(!c_lex_triple_punctuators[first]);
            c_lex_triple_punctuators[first] = (u8)index;
        }
        else
        {
            // The one four-character spelling, %:%:, which the emitter finds
            // as two adjacent %: pairs rather than through a fourth table.
            BUSTER_CHECK(spelling.length == 4 && index == C_PUNCTUATOR_HASH_HASH_DIGRAPH);
        }
    }

    for (const char8* it = "!%&*+-/<=>^|"; *it; it += 1)
    {
        c_lex_nfa_first[(u8)*it] |= C_LEX_NFA_ASSIGN;
    }
    c_lex_nfa_second['='] |= C_LEX_NFA_ASSIGN;

    for (const char8* it = "#&+-<>|"; *it; it += 1)
    {
        c_lex_nfa_first[(u8)*it] |= C_LEX_NFA_DOUBLE;
        c_lex_nfa_second[(u8)*it] |= C_LEX_NFA_DOUBLE;
    }

    c_lex_nfa_first['-'] |= C_LEX_NFA_ARROW;
    c_lex_nfa_first[':'] |= C_LEX_NFA_ARROW;
    c_lex_nfa_second['>'] |= C_LEX_NFA_ARROW;

    c_lex_nfa_first['<'] |= C_LEX_NFA_LESS_PAIR;
    c_lex_nfa_second[':'] |= C_LEX_NFA_LESS_PAIR;
    c_lex_nfa_second['%'] |= C_LEX_NFA_LESS_PAIR;

    c_lex_nfa_first['%'] |= C_LEX_NFA_PERCENT_PAIR;
    c_lex_nfa_second[':'] |= C_LEX_NFA_PERCENT_PAIR;
    c_lex_nfa_second['>'] |= C_LEX_NFA_PERCENT_PAIR;

    c_lex_nfa_first['<'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_first['>'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_second['<'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_second['>'] |= C_LEX_NFA_SHIFT_ASSIGN;
    c_lex_nfa_third['='] |= C_LEX_NFA_SHIFT_ASSIGN;

    c_lex_nfa_first['.'] |= C_LEX_NFA_ELLIPSIS;
    c_lex_nfa_second['.'] |= C_LEX_NFA_ELLIPSIS;
    c_lex_nfa_third['.'] |= C_LEX_NFA_ELLIPSIS;

    c_lex_compact_tables_built = true;
}

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_mask_below(u64 bit_index)
{
    return bit_index >= 64 ? ~(u64)0 : (((u64)1 << bit_index) - 1);
}

BUSTER_C_INLINE BUSTER_INLINE u64 c_lex_mask_range(u64 low, u64 high)
{
    return c_lex_mask_below(high) & ~c_lex_mask_below(low);
}

BUSTER_C_INLINE BUSTER_INLINE __mmask64 c_lex_lane_mask(u64 count)
{
    return count >= 64 ? ~(__mmask64)0 : (__mmask64)((((u64)1) << count) - 1);
}

// Lanes of a lookahead load that hold a real byte.  The window bounds do not
// serve: a punctuator or comment delimiter near the window's end is classified
// from bytes the next window owns, and reading them as zero would mis-spell it.
BUSTER_C_INLINE BUSTER_INLINE __mmask64 c_lex_lookahead_mask(u64 remaining, u64 ahead)
{
    return c_lex_lane_mask(remaining > ahead ? remaining - ahead : 0);
}

// Eight finished rows: the compressed start, length, kind and punctuator
// bytes widen to 32-bit lanes (masked, so lanes 8..15 stay zero), the
// metadata dword assembles as length | kind<<16 | punctuator<<24, and two
// vpermi2d interleave offsets, zero symbol dwords and metadata into the 24
// dwords of eight 12-byte CToken rows — 64 stored bytes plus a 32-byte
// tail, no slop. The symbol dword is zero at lex time and comes from the
// offset vector's masked-off upper lanes.
BUSTER_C_INLINE BUSTER_INLINE void c_lex_store_rows(CToken* out, __m512i starts, __m512i lengths, __m512i kinds, __m512i punctuators, __m512i base,
                                                        __m512i index_low, __m512i index_high)
{
    __m512i start_wide = _mm512_maskz_cvtepu8_epi32(0x00FF, _mm512_castsi512_si128(starts));
    __m512i length_wide = _mm512_maskz_cvtepu8_epi32(0x00FF, _mm512_castsi512_si128(lengths));
    __m512i kind_wide = _mm512_maskz_cvtepu8_epi32(0x00FF, _mm512_castsi512_si128(kinds));
    __m512i punctuator_wide = _mm512_maskz_cvtepu8_epi32(0x00FF, _mm512_castsi512_si128(punctuators));
    __m512i offsets = _mm512_maskz_add_epi32(0x00FF, start_wide, base);
    __m512i metadata = _mm512_or_si512(length_wide, _mm512_or_si512(_mm512_slli_epi32(kind_wide, 16), _mm512_slli_epi32(punctuator_wide, 24)));
    u32* dwords = (u32*)out;
    _mm512_storeu_si512((__m512i*)dwords, _mm512_permutex2var_epi32(offsets, index_low, metadata));
    _mm256_storeu_si256((__m256i*)(dwords + 16), _mm512_castsi512_si256(_mm512_permutex2var_epi32(offsets, index_high, metadata)));
}

BUSTER_C_INTERNAL void c_lex_compact(CLexState* state)
{
    CLexResult* result = state->result;
    String8 source = state->translated.source;
    u32 translated_offset = state->translated.translated_offset;

    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }

    const __m512i single_low = _mm512_load_si512((const __m512i*)c_lex_single_punctuators);
    const __m512i single_high = _mm512_load_si512((const __m512i*)(c_lex_single_punctuators + 64));
    const __m512i nfa_first_low = _mm512_load_si512((const __m512i*)c_lex_nfa_first);
    const __m512i nfa_first_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_first + 64));
    const __m512i nfa_second_low = _mm512_load_si512((const __m512i*)c_lex_nfa_second);
    const __m512i nfa_second_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_second + 64));
    const __m512i nfa_third_low = _mm512_load_si512((const __m512i*)c_lex_nfa_third);
    const __m512i nfa_third_high = _mm512_load_si512((const __m512i*)(c_lex_nfa_third + 64));
    const __m512i iota = _mm512_load_si512((const __m512i*)c_lex_iota);
    const __m512i rotate_eight = _mm512_load_si512((const __m512i*)c_lex_rotate_eight);
    const __m512i row_index_low = _mm512_setr_epi32(0, 8, 16, 1, 9, 17, 2, 10, 18, 3, 11, 19, 4, 12, 20, 5);
    const __m512i row_index_high = _mm512_setr_epi32(13, 21, 6, 14, 22, 7, 15, 23, 0, 0, 0, 0, 0, 0, 0, 0);

    u64 offset = 0;
    while (offset < source.length)
    {
        const char8* it = source.pointer + offset;
        u64 remaining = source.length - offset;
        u64 window_length = remaining < 64 ? remaining : 64;
        bool at_end_of_file = window_length == remaining;
        __mmask64 valid_lanes = c_lex_lane_mask(window_length);
        u64 valid = (u64)valid_lanes;

        __m512i chunk0 = _mm512_maskz_loadu_epi8(valid_lanes, it);
        __m512i chunk1 = _mm512_maskz_loadu_epi8(c_lex_lookahead_mask(remaining, 1), it + 1);
        __m512i chunk2 = _mm512_maskz_loadu_epi8(c_lex_lookahead_mask(remaining, 2), it + 2);

        // One masked load, every byte class in lockstep.
        __m512i lowered = _mm512_or_si512(chunk0, _mm512_set1_epi8(0x20));
        u64 alpha = (u64)_mm512_cmplt_epu8_mask(_mm512_sub_epi8(lowered, _mm512_set1_epi8('a')), _mm512_set1_epi8(26));
        u64 digit = (u64)_mm512_cmplt_epu8_mask(_mm512_sub_epi8(chunk0, _mm512_set1_epi8('0')), _mm512_set1_epi8(10));
        u64 high_byte = (u64)_mm512_movepi8_mask(chunk0);
        u64 underscore = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('_'));
        u64 dollar = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('$'));
        u64 space = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8(' '));
        u64 tab = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\t'));
        u64 vertical_tab = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\v'));
        u64 form_feed = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\f'));
        u64 line_feed = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\n'));
        u64 quote = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('"'));
        u64 apostrophe = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\''));
        u64 backslash = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('\\'));
        u64 slash = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('/'));
        u64 star = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('*'));
        u64 dot = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('.'));
        u64 plus = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('+'));
        u64 minus = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('-'));
        u64 percent = (u64)_mm512_cmpeq_epi8_mask(chunk0, _mm512_set1_epi8('%'));
        u64 exponent_letter = (u64)_mm512_cmpeq_epi8_mask(lowered, _mm512_set1_epi8('e')) | (u64)_mm512_cmpeq_epi8_mask(lowered, _mm512_set1_epi8('p'));
        u64 slash_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('/'));
        u64 star_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8('*'));
        u64 colon_next = (u64)_mm512_cmpeq_epi8_mask(chunk1, _mm512_set1_epi8(':'));
        u64 repeated = (u64)_mm512_cmpeq_epi8_mask(chunk0, chunk1);
        // c_identifier_start admits `$` and every byte above ASCII, so those
        // join the identifier class rather than the invalid one.
        u64 word = alpha | digit | underscore | dollar | high_byte;
        u64 white = space | tab | vertical_tab | form_feed;

        // Single-character punctuator ids double as the "byte can start a
        // punctuator" class, since C_PUNCTUATOR_NONE is zero.
        __m512i single_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_byte, single_low, chunk0, single_high);
        u64 punctuator_byte = (u64)_mm512_test_epi8_mask(single_vector, single_vector);

        // Bit-channel NFA: three per-position lookups AND together, one
        // channel per punctuator family, plus the equal-byte refinement.
        u64 high_next = (u64)_mm512_movepi8_mask(chunk1);
        u64 high_third = (u64)_mm512_movepi8_mask(chunk2);
        __m512i first_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_byte, nfa_first_low, chunk0, nfa_first_high);
        __m512i second_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_next, nfa_second_low, chunk1, nfa_second_high);
        __m512i third_vector = _mm512_maskz_permutex2var_epi8((__mmask64)~high_third, nfa_third_low, chunk2, nfa_third_high);
        __m512i pair_vector = _mm512_and_si512(first_vector, second_vector);
        __m512i triple_vector = _mm512_and_si512(pair_vector, third_vector);
        u64 punctuator2 = (u64)_mm512_test_epi8_mask(pair_vector, _mm512_set1_epi8((char)C_LEX_NFA_PAIR_CHANNELS)) |
                          ((u64)_mm512_test_epi8_mask(pair_vector, _mm512_set1_epi8((char)C_LEX_NFA_DOUBLE)) & repeated);
        u64 ellipsis = (u64)_mm512_test_epi8_mask(triple_vector, _mm512_set1_epi8((char)C_LEX_NFA_ELLIPSIS));
        u64 punctuator3 = ellipsis | ((u64)_mm512_test_epi8_mask(triple_vector, _mm512_set1_epi8((char)C_LEX_NFA_SHIFT_ASSIGN)) & repeated);
        u64 percent_colon = percent & colon_next;
        u64 punctuator4 = percent_colon & (percent_colon >> 2);

        // A preprocessing number swallows identifier bytes, dots, digit
        // separators, and the sign of an exponent — the one context-sensitive
        // rule, and one mask shift models it exactly.
        u64 exponent_sign = (plus | minus) & (exponent_letter << 1);
        u64 number_continue = word | dot | apostrophe | exponent_sign;
        u64 number_seed = (digit & ~(word << 1)) | (dot & (digit >> 1));
        u64 line_comment_open = slash & slash_next;
        u64 block_comment_open = slash & star_next;
        u64 block_comment_close = star & slash_next;
        // `...` is the one punctuator that can swallow another item's start:
        // its last dot would otherwise seed the number in `...5`.  Every other
        // spelling is built from bytes no item can begin with, so the cursor
        // below needs no other punctuator.
        u64 extent_candidates = (number_seed | line_comment_open | block_comment_open | quote | apostrophe | ellipsis) & valid;

        // Numbers, comments, literals and ellipses are the only items that can
        // swallow another item's bytes, so one left-to-right cursor over their
        // candidate starts resolves all four; identifiers, punctuators and
        // whitespace then fall out of pure mask arithmetic below.
        u64 comment_span = 0;
        u64 comment_starts = 0;
        u64 literal_span = 0;
        u64 number_span = 0;
        u64 number_starts = 0;
        u64 string_starts = 0;
        u64 character_starts = 0;
        u64 defer_at = 64;
        u64 trigger_at = 64;
        u64 cursor = 0;
        for (u64 rest = extent_candidates; rest; rest = extent_candidates & ~c_lex_mask_below(cursor))
        {
            u64 start = (u64)__builtin_ctzll(rest);
            u64 end;
            if ((number_seed >> start) & 1)
            {
                u64 tail = ~(number_continue >> start);
                end = tail ? start + (u64)__builtin_ctzll(tail) : 64;
                if (end >= window_length)
                {
                    if (!at_end_of_file)
                    {
                        defer_at = start;
                        break;
                    }
                    end = window_length;
                }
                number_span |= c_lex_mask_range(start, end);
                number_starts |= (u64)1 << start;
            }
            else if (((line_comment_open | block_comment_open) >> start) & 1)
            {
                if ((line_comment_open >> start) & 1)
                {
                    u64 stop = line_feed & ~c_lex_mask_below(start);
                    if (stop)
                    {
                        // The newline that ends a line comment is its own
                        // token and is not part of the comment.
                        end = (u64)__builtin_ctzll(stop);
                    }
                    else if (at_end_of_file)
                    {
                        end = window_length;
                    }
                    else
                    {
                        defer_at = start;
                        break;
                    }
                }
                else
                {
                    u64 stop = block_comment_close & ~c_lex_mask_below(start + 2);
                    if (!stop)
                    {
                        // Unterminated at the end of the file is a diagnostic;
                        // otherwise the comment continues past this window.
                        if (at_end_of_file)
                        {
                            trigger_at = start;
                        }
                        else
                        {
                            defer_at = start;
                        }
                        break;
                    }
                    end = (u64)__builtin_ctzll(stop) + 2;
                    if (end >= window_length && !at_end_of_file)
                    {
                        defer_at = start;
                        break;
                    }
                }
                comment_span |= c_lex_mask_range(start, end);
                comment_starts |= (u64)1 << start;
            }
            else if ((ellipsis >> start) & 1)
            {
                // Nothing to record: the punctuator scan below finds it in
                // `available` like any other operator.  The cursor is the
                // whole point, so its third dot cannot also seed a number.
                end = start + 3;
            }
            else
            {
                // c_literal_prefix reads u8/u/U/L only at an item start, so a
                // quote preceded by word bytes is prefixed exactly when those
                // bytes are a one- or two-byte run of their own.
                u64 opener = start;
                if (start > cursor && ((word >> (start - 1)) & 1))
                {
                    u64 gaps = ~word & c_lex_mask_range(cursor, start);
                    u64 run_start = gaps ? (u64)(63 - __builtin_clzll(gaps)) + 1 : cursor;
                    u64 run_length = start - run_start;
                    u8 run_first = (u8)it[run_start];
                    if ((run_length == 1 && (run_first == 'u' || run_first == 'U' || run_first == 'L')) ||
                        (run_length == 2 && run_first == 'u' && (u8)it[run_start + 1] == '8'))
                    {
                        opener = run_start;
                    }
                }
                bool is_string = ((quote >> start) & 1) != 0;
                // The body begins after the delimiter, wherever the token did.
                u64 content = start + 1;
                // Escape-run parity, simdjson's backslash algorithm, scoped to
                // the literal body so a backslash before the opener cannot
                // escape the opening delimiter.
                u64 escaped = 0;
                u64 run = backslash & ~c_lex_mask_below(content);
                if (run)
                {
                    u64 run_starts = run & ~(run << 1);
                    u64 even_starts = run_starts & UINT64_C(0x5555555555555555);
                    u64 odd_starts = run_starts & UINT64_C(0xAAAAAAAAAAAAAAAA);
                    u64 even_ends = (run + even_starts) & ~run;
                    u64 odd_ends = (run + odd_starts) & ~run;
                    escaped = (even_ends & UINT64_C(0xAAAAAAAAAAAAAAAA)) | (odd_ends & UINT64_C(0x5555555555555555));
                }
                u64 terminator = (is_string ? quote : apostrophe) & ~escaped;
                u64 stop = (terminator | line_feed) & ~c_lex_mask_below(content);
                if (!stop)
                {
                    if (at_end_of_file)
                    {
                        trigger_at = opener;
                    }
                    else
                    {
                        defer_at = opener;
                    }
                    break;
                }
                u64 stop_at = (u64)__builtin_ctzll(stop);
                if (!((terminator >> stop_at) & 1))
                {
                    // A newline before the closing delimiter: unterminated,
                    // which the scalar path reports.
                    trigger_at = opener;
                    break;
                }
                end = stop_at + 1;
                if (end >= window_length && !at_end_of_file)
                {
                    defer_at = opener;
                    break;
                }
                literal_span |= c_lex_mask_range(opener, end);
                if (is_string)
                {
                    string_starts |= (u64)1 << opener;
                }
                else
                {
                    character_starts |= (u64)1 << opener;
                }
            }
            cursor = end;
        }

        u64 available = valid & ~comment_span & ~literal_span & ~number_span;
        u64 word_available = word & available;
        u64 identifier_starts = word_available & ~(word_available << 1);
        // Newlines are tokens wherever they appear, block comments included.
        u64 newline_starts = line_feed & valid;

        // Maximal munch over the punctuator runs.  A punctuator byte with no
        // punctuator neighbour cannot be part of a longer spelling, so only
        // the clustered ones need the left-greedy walk.
        _Alignas(64) u8 punctuators[64];
        _mm512_store_si512((__m512i*)punctuators, single_vector);
        u64 operators = punctuator_byte & available;
        u64 operator_starts = operators & ~(operators << 1) & ~(operators >> 1);
        for (u64 clustered = operators & ~operator_starts; clustered;)
        {
            u64 start = (u64)__builtin_ctzll(clustered);
            u64 bit = (u64)1 << start;
            u64 spelling_length = 1;
            operator_starts |= bit;
            if (punctuator4 & bit)
            {
                spelling_length = 4;
                punctuators[start] = (u8)C_PUNCTUATOR_HASH_HASH_DIGRAPH;
            }
            else if (punctuator3 & bit)
            {
                spelling_length = 3;
                punctuators[start] = c_lex_triple_punctuators[(u8)it[start]];
            }
            else if (punctuator2 & bit)
            {
                spelling_length = 2;
                punctuators[start] = c_lex_pair_punctuators[c_lex_pair_row[(u8)it[start]]][c_lex_pair_column[(u8)it[start + 1]]];
            }
            clustered &= ~c_lex_mask_below(start + spelling_length);
        }

        // Nothing left over may reach the emitter: a byte that is neither a
        // resolved span, an identifier byte, whitespace, a newline nor a
        // punctuator byte is C_TOKEN_INVALID, whose diagnostic the scalar path
        // formats.
        u64 unclassified = available & ~word & ~white & ~line_feed & ~punctuator_byte;
        if (unclassified)
        {
            u64 candidate = (u64)__builtin_ctzll(unclassified);
            if (candidate < trigger_at)
            {
                trigger_at = candidate;
            }
        }

        u64 token_starts = identifier_starts | number_starts | string_starts | character_starts | operator_starts | newline_starts;
        // Bytes owned by no token: whitespace outside a literal, and comment
        // text other than the newlines a block comment contains.
        u64 comment_body = comment_span & ~line_feed;
        u64 non_token = (white & valid & ~literal_span) | comment_body;
        u64 token_span = valid & ~non_token;
        // Every position that begins something, so the byte before each one
        // ends the item in front of it.  Whitespace contributes per byte, as
        // the scalar loop consumes it, so a window of pure whitespace still
        // advances a whole window instead of one byte.
        u64 boundary = token_starts | (non_token & ~(non_token << 1)) | (white & valid & ~literal_span);

        u64 bound;
        bool escape_after = false;
        if (trigger_at < 64 && trigger_at <= defer_at)
        {
            bound = trigger_at;
            escape_after = true;
        }
        else if (defer_at < 64)
        {
            bound = defer_at;
        }
        else if (at_end_of_file)
        {
            bound = window_length;
        }
        else
        {
            BUSTER_CHECK(boundary);
            bound = (u64)(63 - __builtin_clzll(boundary));
        }

        u64 emitted = c_lex_mask_below(bound);
        // No span ever straddles the bound: a span's start is an item start,
        // and an incomplete one sets defer_at, so these popcounts are exact.
        result->metrics.blank_bytes += (u64)__builtin_popcountll((white | line_feed) & emitted & ~comment_span & ~literal_span);
        result->metrics.comment_bytes += (u64)__builtin_popcountll(comment_span & emitted);
        result->metrics.literal_bytes += (u64)__builtin_popcountll(literal_span & emitted);
        result->metrics.comments += (u64)__builtin_popcountll(comment_starts & emitted);

        // One finished line per newline in the window, classified by whether
        // any code or comment byte falls in the range since the last one.
        u64 code_bytes = token_span & ~line_feed & emitted;
        u64 comment_bytes = comment_span & emitted;
        u64 newlines = newline_starts & emitted;
        u64 line_base = 0;
        for (u64 rest = newlines; rest; rest &= rest - 1)
        {
            u64 position = (u64)__builtin_ctzll(rest);
            u64 range = c_lex_mask_range(line_base, position);
            c_source_metrics_line(&result->metrics, state->line_has_code | ((code_bytes & range) != 0),
                                  state->line_has_comment | ((comment_bytes & range) != 0));
            state->line_has_code = 0;
            // A newline inside a block comment leaves the comment open, so the
            // next line is a comment line too; a plain newline clears it.
            state->line_has_comment = (u32)((comment_span >> position) & 1);
            line_base = position + 1;
        }
        u64 tail = c_lex_mask_range(line_base, bound);
        state->line_has_code |= (code_bytes & tail) != 0;
        state->line_has_comment |= (comment_bytes & tail) != 0;
        if (newlines)
        {
            state->line_start = offset + (u64)(63 - __builtin_clzll(newlines)) + 1;
        }
        state->newline_tokens += (u64)__builtin_popcountll(newlines);

        if (bound)
        {
            // Starts and ends compress through the token-boundary masks, one
            // byte subtract yields every length in the window, and the kind
            // and punctuator vectors ride the same starts mask.
            u64 start_mask = token_starts & emitted;
            u64 end_mask = ((boundary >> 1) | ((u64)1 << (bound - 1))) & token_span & emitted;
            u32 count = (u32)__builtin_popcountll(start_mask);
            BUSTER_CHECK(count == (u32)__builtin_popcountll(end_mask));

            __m512i kind_vector = _mm512_maskz_set1_epi8((__mmask64)newline_starts, (char)C_TOKEN_NEWLINE);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)identifier_starts, (char)C_TOKEN_IDENTIFIER);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)number_starts, (char)C_TOKEN_PREPROCESSING_NUMBER);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)operator_starts, (char)C_TOKEN_PUNCTUATOR);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)string_starts, (char)C_TOKEN_STRING_LITERAL);
            kind_vector = _mm512_mask_set1_epi8(kind_vector, (__mmask64)character_starts, (char)C_TOKEN_CHARACTER_LITERAL);

            __m512i start_positions = _mm512_maskz_compress_epi8((__mmask64)start_mask, iota);
            __m512i end_positions = _mm512_maskz_compress_epi8((__mmask64)end_mask, iota);
            __m512i lengths = _mm512_add_epi8(_mm512_sub_epi8(end_positions, start_positions), _mm512_set1_epi8(1));
            __m512i kinds = _mm512_maskz_compress_epi8((__mmask64)start_mask, kind_vector);
            // A non-punctuator start keeps whatever its first byte spells —
            // `.` opening a number, say — so the field is cleared first.
            __m512i punctuator_vector = _mm512_maskz_mov_epi8((__mmask64)operator_starts, _mm512_load_si512((const __m512i*)punctuators));
            __m512i punctuator_ids = _mm512_maskz_compress_epi8((__mmask64)start_mask, punctuator_vector);

            CToken* out = result->tokens + result->token_count;
            __m512i base = _mm512_set1_epi32((s32)(u32)((u64)translated_offset + offset));
            // Eight rows per pass, tail rows included: the token array carries
            // eight slots of slack and the next window overwrites them.  The
            // first pass is unconditional on purpose — guarding it on a
            // non-empty window measured +2.0 M stage-1 instructions, because a
            // window with no token at all needs 64 bytes of whitespace or
            // comment, which escapes to the scalar scanner long before it
            // reaches here.
            for (u32 written = 0;;)
            {
                c_lex_store_rows(out + written, start_positions, lengths, kinds, punctuator_ids, base, row_index_low, row_index_high);
                written += 8;
                if (written >= count)
                {
                    break;
                }
                start_positions = _mm512_permutexvar_epi8(rotate_eight, start_positions);
                lengths = _mm512_permutexvar_epi8(rotate_eight, lengths);
                kinds = _mm512_permutexvar_epi8(rotate_eight, kinds);
                punctuator_ids = _mm512_permutexvar_epi8(rotate_eight, punctuator_ids);
            }
            result->token_count += count;
            offset += bound;
            if (!escape_after)
            {
                continue;
            }
        }

        // Either the window holds one unfinished item or a scalar-escape
        // trigger sits at the emission bound: scan exactly that item with the
        // reference scanner and re-enter the window loop after it.
        offset = c_lex_scan_one(state, offset);
    }
}

#endif

#if BUSTER_INCLUDE_TESTS
bool c_test_lex_compact_tables_ready(void)
{
#if BUSTER_C_LEX_COMPACT
    return c_lex_compact_tables_built;
#else
    return true;
#endif
}

// Test seam: reconcile the emitter's hand-assigned NFA channels with the
// spelling table the scalar path scans. For every byte sequence the window
// pipeline can classify, the channels must legalize exactly the spellings
// c_punctuator_length finds, at exactly its length. Counting mismatches keeps
// the exhaustive sweep to one test assertion.
u64 c_test_lex_punctuator_nfa_mismatches(void)
{
#if BUSTER_C_LEX_COMPACT
    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    u64 mismatches = 0;
    char8 probe[4];
    for (u32 first = 0; first < 128; first += 1)
    {
        if (!c_lex_single_punctuators[first])
        {
            continue;
        }
        for (u32 second = 0; second < 256; second += 1)
        {
            for (u32 third = 0; third < 256; third += 1)
            {
                // The fourth byte only ever matters to %:%:, so it sweeps just
                // that prefix instead of multiplying the whole cross product.
                u32 fourth_low = 0;
                u32 fourth_high = 1;
                if (first == '%' && second == ':' && third == '%')
                {
                    fourth_high = 256;
                }
                for (u32 fourth = fourth_low; fourth < fourth_high; fourth += 1)
                {
                    probe[0] = (char8)first;
                    probe[1] = (char8)second;
                    probe[2] = (char8)third;
                    probe[3] = (char8)fourth;
                    // What the emitter's channels decide. Bytes above ASCII
                    // zero their lookups, exactly as the maskz permutes do.
                    u8 first_bits = c_lex_nfa_first[first];
                    u8 second_bits = second < 128 ? c_lex_nfa_second[second] : 0;
                    u8 third_bits = third < 128 ? c_lex_nfa_third[third] : 0;
                    u8 pair = first_bits & second_bits;
                    u8 triple = pair & third_bits;
                    bool repeated = first == second;
                    bool is_four = first == '%' && second == ':' && third == '%' && fourth == ':';
                    bool is_three = (triple & C_LEX_NFA_ELLIPSIS) != 0 || ((triple & C_LEX_NFA_SHIFT_ASSIGN) != 0 && repeated);
                    bool is_two = (pair & C_LEX_NFA_PAIR_CHANNELS) != 0 || ((pair & C_LEX_NFA_DOUBLE) != 0 && repeated);
                    u64 emitted_length = is_four ? 4 : is_three ? 3 : is_two ? 2 : 1;
                    u8 emitted_punctuator = (u8)C_PUNCTUATOR_HASH_HASH_DIGRAPH;
                    if (emitted_length == 3)
                    {
                        emitted_punctuator = c_lex_triple_punctuators[first];
                    }
                    else if (emitted_length == 2)
                    {
                        u8 row = c_lex_pair_row[first];
                        u8 column = second < 128 ? c_lex_pair_column[second] : 0xFF;
                        emitted_punctuator = row == 0xFF || column == 0xFF ? 0 : c_lex_pair_punctuators[row][column];
                    }
                    else if (emitted_length == 1)
                    {
                        emitted_punctuator = c_lex_single_punctuators[first];
                    }
                    CPunctuator reference = C_PUNCTUATOR_NONE;
                    u64 reference_length = c_punctuator_length((String8){probe, BUSTER_ARRAY_LENGTH(probe)}, 0, &reference);
                    mismatches += emitted_length != reference_length || emitted_punctuator != (u8)reference;
                }
            }
        }
    }
    return mismatches;
#else
    return 0;
#endif
}
#endif

BUSTER_C_INTERNAL CLexResult c_lex_dispatch(Arena* arena, CSpellingSpace* space, String8 source, bool force_scalar)
{
    CLexResult result = {0};
    if (arena && (!source.length || source.pointer))
    {
        CTranslatedSource translated = c_translate_source(arena, space, source);
        result.translated_source = translated.source;
        result.spelling_base = space ? space->base : translated.source.pointer;
        result.checkpoints = translated.checkpoints;
        result.checkpoint_offsets = translated.checkpoint_offsets;
        result.checkpoint_pages = translated.checkpoint_pages;
        result.checkpoint_page_count = translated.checkpoint_page_count;
        result.checkpoint_count = translated.checkpoint_count;
        result.translated_offset = translated.translated_offset;
        // One token per byte bounds the stream including the end marker exactly as
        // the scalar loop needs; eight more absorb the tail rows of the emitter's
        // full-width interleaved stores, which the next window overwrites.
        result.tokens = arena_allocate(arena, CToken, translated.source.length + 1 + 8);
        if (translated.source.length > UINT64_MAX / sizeof(CDiagnostic) - 1)
        {
            return result;
        }
        u64 diagnostic_bytes = (translated.source.length + 1) * sizeof(CDiagnostic);
        if (diagnostic_bytes > (UINT64_MAX - BUSTER_MB(1)) / 2)
        {
            return result;
        }
        u64 diagnostic_reserve_size = diagnostic_bytes * 2 + BUSTER_MB(1);
        Arena* conflicts[] = {
            arena,
        };
        TemporalArena diagnostic_temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
        Arena* diagnostic_arena = diagnostic_temporary.arena;
        bool diagnostic_arena_is_scratch = diagnostic_reserve_size <= diagnostic_arena->reserved_size - diagnostic_arena->position;
        if (!diagnostic_arena_is_scratch)
        {
            scratch_end(diagnostic_temporary);
            diagnostic_arena = arena_create((ArenaCreation){
                .reserved_size = BUSTER_MAX(BUSTER_MB(64), diagnostic_reserve_size),
            });
        }
        if (!diagnostic_arena)
        {
            return result;
        }
        CLexState state = {
            .result = &result,
            .translated = translated,
            .diagnostic_arena = diagnostic_arena,
            .maximum_diagnostic_count = translated.source.length + 1,
        };
        state.diagnostic_capacity = BUSTER_MIN(state.maximum_diagnostic_count, UINT64_C(64));
        result.diagnostics = arena_allocate(diagnostic_arena, CDiagnostic, state.diagnostic_capacity);
        // Source measurement rides the branches the lexer already takes; see
        // CSourceMetrics.
        result.metrics.files = 1;
        result.metrics.bytes = source.length;
        result.metrics.translated_bytes = translated.source.length;
        result.metrics.lines = translated.raw_lines;
#if BUSTER_C_LEX_COMPACT
        if (force_scalar)
        {
            c_lex_scalar(&state);
        }
        else
        {
            c_lex_compact(&state);
        }
#else
        BUSTER_UNUSED(force_scalar);
        c_lex_scalar(&state);
#endif
        // A file whose last line has no terminating newline still ends a line.
        if (translated.source.length > state.line_start)
        {
            c_source_metrics_line(&result.metrics, state.line_has_code, state.line_has_comment);
        }
        result.metrics.spliced_lines = result.metrics.lines - result.metrics.translated_lines;
        result.metrics.tokens = result.token_count - state.newline_tokens;
        c_token_push(&result, translated, translated.source.length, translated.source.length, C_TOKEN_END_OF_FILE, C_PUNCTUATOR_NONE);
        CDiagnostic* diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_count);
        if (result.diagnostic_count)
        {
            memcpy(diagnostics, result.diagnostics, sizeof(*diagnostics) * result.diagnostic_count);
        }
        result.diagnostics = diagnostics;
        if (diagnostic_arena_is_scratch)
        {
            scratch_end(diagnostic_temporary);
        }
        else
        {
            arena_destroy(diagnostic_arena, 1);
        }
    }

    return result;
}

BUSTER_C_INTERNAL CLexResult c_lex_space(Arena* arena, CSpellingSpace* space, String8 source)
{
    return c_lex_dispatch(arena, space, source, false);
}

CLexResult c_lex(Arena* arena, String8 source)
{
    return c_lex_dispatch(arena, 0, source, false);
}

// The scalar reference loop, whatever the host: the differential gate asserts
// the dispatched lexer agrees with it byte for byte.
CLexResult c_lex_reference(Arena* arena, String8 source)
{
    return c_lex_dispatch(arena, 0, source, true);
}

typedef struct CMacro CMacro;
typedef struct CMacroDefinition CMacroDefinition;
struct CMacroDefinition
{
    CToken* replacement;
    String8* parameters;
    u32 replacement_count;
    u32 parameter_count;
    bool defined;
    bool function_like;
    bool variadic;
    bool pragma_like;
};

typedef enum CMacroBuiltin
{
    C_MACRO_BUILTIN_NONE,
    C_MACRO_BUILTIN_LINE,
    C_MACRO_BUILTIN_FILE,
} CMacroBuiltin;

struct CMacro
{
    CMacro* next;
    CMacro* hash_next;
    CMacro** buckets;
    String8 name;
    u32 symbol;
    CMacroDefinition definition;
    u32 bucket_count;
    u32 hash_count;
    bool disabled;
    u8 builtin;
    // Head-of-list state for the lazy builtin macros: the main preprocess
    // loop stores the current frame and token offset here each iteration
    // (two stores, no location recovery), and __LINE__/__FILE__ recover the
    // logical line from them only when actually expanded.
    u32 builtin_line;
    struct CPreprocessSourceFrame* builtin_frame;
    u32 builtin_token_offset;
    String8 builtin_path;
};

typedef struct CMacroPushMacro CMacroPushMacro;
struct CMacroPushMacro
{
    CMacroPushMacro* previous;
    CMacro* macro;
    String8 name;
    CMacroDefinition definition;
};

BUSTER_C_SHARED u64 c_macro_name_hash(String8 name)
{
    u64 hash = 1469598103934665603ull;
    for (u64 index = 0; index < name.length; index += 1)
    {
        hash ^= name.pointer[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

BUSTER_C_INTERNAL void c_macro_hash_rebuild(Arena* arena, CMacro* first, u32 capacity)
{
    BUSTER_CHECK(first && capacity && !(capacity & (capacity - 1)));
    CMacro** buckets = arena_allocate(arena, CMacro*, capacity);
    memset(buckets, 0, sizeof(*buckets) * capacity);
    u32 count = 0;
    for (CMacro* macro = first; macro; macro = macro->next)
    {
        u32 bucket = macro->symbol & (capacity - 1);
        macro->hash_next = buckets[bucket];
        buckets[bucket] = macro;
        count += 1;
    }
    first->buckets = buckets;
    first->bucket_count = capacity;
    first->hash_count = count;
}

// Identifier interning: the preprocessor assigns every identifier token a
// u32 symbol id (stored in token.location.symbol) so downstream consumers
// compare integers instead of re-hashing and re-comparing spellings. The
// names below are interned first, in array order, so their ids are the
// compile-time-known range [1, C_SYMBOL_PREDEFINED_COUNT] and one dense
// byte table classifies them. Tokens that never pass through the intern
// pass (pasted, synthesized, or test-built tokens) keep symbol 0 and every
// consumer falls back to the spelling ladder, so a missed path costs speed,
// never correctness.
typedef struct CSymbolPredefined CSymbolPredefined;
struct CSymbolPredefined
{
    String8 name;
    u8 builtin;
};

BUSTER_C_INTERNAL CSymbolPredefined const c_symbol_predefined[] = {
    { S8_INITIALIZER("__builtin_expect"), C_SYMBOL_BUILTIN_EXPECT },
    { S8_INITIALIZER("__builtin_expect_with_probability"), C_SYMBOL_BUILTIN_EXPECT },
    { S8_INITIALIZER("__builtin_constant_p"), C_SYMBOL_BUILTIN_CONSTANT_P },
    { S8_INITIALIZER("__builtin_choose_expr"), C_SYMBOL_BUILTIN_CHOOSE_EXPR },
    { S8_INITIALIZER("__builtin_types_compatible_p"), C_SYMBOL_BUILTIN_TYPES_COMPATIBLE_P },
    { S8_INITIALIZER("__builtin_object_size"), C_SYMBOL_BUILTIN_OBJECT_SIZE },
    { S8_INITIALIZER("__builtin_assume_aligned"), C_SYMBOL_BUILTIN_ASSUME_ALIGNED },
    { S8_INITIALIZER("__builtin_debugtrap"), C_SYMBOL_BUILTIN_DEBUGTRAP },
    { S8_INITIALIZER("__builtin_trap"), C_SYMBOL_BUILTIN_DEBUGTRAP },
    { S8_INITIALIZER("__builtin_unreachable"), C_SYMBOL_BUILTIN_UNREACHABLE },
    { S8_INITIALIZER("__builtin_strlen"), C_SYMBOL_BUILTIN_STRLEN },
    { S8_INITIALIZER("__builtin___clear_cache"), C_SYMBOL_BUILTIN_CLEAR_CACHE },
    { S8_INITIALIZER("__builtin_prefetch"), C_SYMBOL_BUILTIN_PREFETCH },
    { S8_INITIALIZER("__builtin_va_arg"), C_SYMBOL_BUILTIN_VA_ARG },
    { S8_INITIALIZER("__builtin_va_start"), C_SYMBOL_BUILTIN_VA_START },
    { S8_INITIALIZER("__va_start"), C_SYMBOL_BUILTIN_VA_START },
    { S8_INITIALIZER("__builtin_c23_va_start"), C_SYMBOL_BUILTIN_VA_START_C23 },
    { S8_INITIALIZER("__builtin_va_copy"), C_SYMBOL_BUILTIN_VA_COPY },
    { S8_INITIALIZER("__builtin_va_end"), C_SYMBOL_BUILTIN_VA_END },
    { S8_INITIALIZER("_Generic"), C_SYMBOL_BUILTIN_GENERIC },
    { S8_INITIALIZER("__c11_atomic_load"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_store"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_init"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_add"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_sub"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_and"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_or"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_fetch_xor"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_exchange"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_compare_exchange_strong"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_compare_exchange_weak"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_is_lock_free"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_thread_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__c11_atomic_signal_fence"), C_SYMBOL_BUILTIN_ATOMIC },
    { S8_INITIALIZER("__builtin_floorf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_floor"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ceilf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_ceil"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_sqrtf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_sqrt"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_powf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_pow"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmodf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fmod"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_cosf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_cos"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_acosf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_acos"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fabsf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_fabs"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_roundf"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_round"), C_SYMBOL_BUILTIN_MATH },
    { S8_INITIALIZER("__builtin_clz"), C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS },
    { S8_INITIALIZER("__builtin_clzll"), C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS },
    { S8_INITIALIZER("__builtin_ctz"), C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS },
    { S8_INITIALIZER("__builtin_ctzll"), C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS },
    { S8_INITIALIZER("__builtin_popcount"), C_SYMBOL_BUILTIN_POPULATION_COUNT },
    { S8_INITIALIZER("__builtin_popcountll"), C_SYMBOL_BUILTIN_POPULATION_COUNT },
    // The target-fixed 512-bit vocabulary. These names are a buster extension
    // and exist so `<buster/lib/simd.h>` can write one kernel that the host
    // compilers and the self-hosted stages both compile; see the SIMD section
    // of AGENTS.md before adding to the list.
    { S8_INITIALIZER("__builtin_buster_simd_load"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_load_masked"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_store"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_store_masked"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_splat_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_equal_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_less_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_sign_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_test_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_permute2_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_compress_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_compress_store_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_widen_byte"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_shift_left_word"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_ternary_word"), C_SYMBOL_BUILTIN_SIMD },
    { S8_INITIALIZER("__builtin_buster_simd_equal_word"), C_SYMBOL_BUILTIN_SIMD },
};

#define C_SYMBOL_PREDEFINED_COUNT BUSTER_ARRAY_LENGTH(c_symbol_predefined)

// Dense kind classification for interned symbols; index 0 stays
// C_SYMBOL_BUILTIN_NONE so `kinds[symbol]` is valid for every id in
// [0, C_SYMBOL_PREDEFINED_COUNT].
// The cold path for symbol-less identifier tokens: the same classification
// by spelling, scanning the single source-of-truth table above.
BUSTER_C_SHARED CSymbolBuiltin c_symbol_builtin_from_spelling(String8 spelling)
{
    if (spelling.length && spelling.pointer[0] == '_')
    {
        for (u64 index = 0; index < C_SYMBOL_PREDEFINED_COUNT; index += 1)
        {
            if (string_equal(spelling, c_symbol_predefined[index].name))
            {
                return (CSymbolBuiltin)c_symbol_predefined[index].builtin;
            }
        }
    }

    return C_SYMBOL_BUILTIN_NONE;
}

// One probe entry of the intern table. The identity of a name is its first
// and last 8 bytes plus its length: for names up to 16 bytes the two
// overlapped words cover every byte, so matching (low, high, length) is byte
// equality with no further loads; longer names use the triple as a 17-byte
// filter and verify only the middle bytes against the stored spelling.
// length_and_id packs (length << 32) | id, and 0 marks an empty slot (id 0
// is never assigned).
struct CSymbolSlot
{
    u64 low;
    u64 high;
    u64 length_and_id;
};

typedef struct CSymbolKey
{
    u64 low;
    u64 high;
} CSymbolKey;

BUSTER_C_INTERNAL CSymbolKey c_symbol_key(String8 name)
{
    CSymbolKey key = {0};
    if (name.length >= 8)
    {
        memcpy(&key.low, name.pointer, sizeof(key.low));
        memcpy(&key.high, name.pointer + name.length - 8, sizeof(key.high));
    }
    else
    {
        for (u64 index = 0; index < name.length; index += 1)
        {
            key.low |= (u64)(u8)name.pointer[index] << (8 * index);
        }
    }
    return key;
}

// Differences in the top bytes of a key word only propagate upward through a
// multiply, so the slot bits must come from a folded value: fold the first
// product's high word down, remultiply, and take the high half.
BUSTER_C_INTERNAL u32 c_symbol_slot_hash(CSymbolKey key, u64 length)
{
    u64 hash = key.low * UINT64_C(0x9E3779B97F4A7C15) ^ key.high * UINT64_C(0xC2B2AE3D27D4EB4F) ^ length;
    hash ^= hash >> 32;
    hash *= UINT64_C(0xD6E8FEB86659FD93);
    return (u32)(hash >> 32);
}

// Both spellings already match on their first 8 bytes, last 8 bytes, and
// length (> 16); confirm the middle [8, length - 8) in overlapped words.
BUSTER_C_INTERNAL bool c_symbol_middle_equal(String8 stored, String8 name)
{
    u64 end = name.length - 8;
    u64 offset = 8;
    for (; offset + 8 <= end; offset += 8)
    {
        u64 stored_word;
        u64 name_word;
        memcpy(&stored_word, stored.pointer + offset, sizeof(stored_word));
        memcpy(&name_word, name.pointer + offset, sizeof(name_word));
        if (stored_word != name_word)
        {
            return false;
        }
    }
    u64 stored_word;
    u64 name_word;
    memcpy(&stored_word, stored.pointer + end - 8, sizeof(stored_word));
    memcpy(&name_word, name.pointer + end - 8, sizeof(name_word));
    return stored_word == name_word;
}

BUSTER_C_SHARED u32 c_symbol_intern(CSymbolTable* table, String8 name)
{
    CSymbolKey key = c_symbol_key(name);
    u64 length_word = (u64)name.length << 32;
    u32 mask = table->slot_capacity - 1;
    u32 slot = c_symbol_slot_hash(key, name.length) & mask;
    for (;;)
    {
        CSymbolSlot* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;
        if (!length_and_id)
        {
            break;
        }
        if (entry->low == key.low && entry->high == key.high && (length_and_id & UINT64_C(0xFFFFFFFF00000000)) == length_word)
        {
            u32 id = (u32)length_and_id;
            if (name.length <= 16 || c_symbol_middle_equal(table->names[id], name))
            {
                return id;
            }
        }
        slot = (slot + 1) & mask;
    }
    if (table->count + 1 == table->name_capacity)
    {
        u32 name_capacity = table->name_capacity * 2;
        String8* names = arena_allocate(table->arena, String8, name_capacity);
        memcpy(names, table->names, sizeof(*names) * (table->count + 1));
        table->names = names;
        table->name_capacity = name_capacity;
    }
    // Keep the probe table at most half full so lookups stay short; the
    // rebuild re-places every entry at the doubled capacity from the key
    // words the entries already carry.
    if (table->count + 1 > table->slot_capacity / 2)
    {
        u32 slot_capacity = table->slot_capacity * 2;
        CSymbolSlot* slots = arena_allocate(table->arena, CSymbolSlot, slot_capacity);
        memset(slots, 0, sizeof(*slots) * slot_capacity);
        for (u32 old_slot = 0; old_slot < table->slot_capacity; old_slot += 1)
        {
            CSymbolSlot entry = table->slots[old_slot];
            if (!entry.length_and_id)
            {
                continue;
            }
            u32 rebuilt_slot = c_symbol_slot_hash((CSymbolKey){.low = entry.low, .high = entry.high}, entry.length_and_id >> 32) & (slot_capacity - 1);
            while (slots[rebuilt_slot].length_and_id)
            {
                rebuilt_slot = (rebuilt_slot + 1) & (slot_capacity - 1);
            }
            slots[rebuilt_slot] = entry;
        }
        table->slots = slots;
        table->slot_capacity = slot_capacity;
        slot = c_symbol_slot_hash(key, name.length) & (slot_capacity - 1);
        while (table->slots[slot].length_and_id)
        {
            slot = (slot + 1) & (slot_capacity - 1);
        }
    }
    u32 id = table->count + 1;
    table->count = id;
    table->names[id] = name;
    table->slots[slot] = (CSymbolSlot){
        .low = key.low,
        .high = key.high,
        .length_and_id = length_word | id,
    };
    return id;
}

BUSTER_C_SHARED String8 const c_declaration_keyword_spellings[] = {
    S8_INITIALIZER("auto"),          S8_INITIALIZER("break"),     S8_INITIALIZER("case"),           S8_INITIALIZER("char"),
    S8_INITIALIZER("const"),         S8_INITIALIZER("continue"),  S8_INITIALIZER("default"),        S8_INITIALIZER("do"),
    S8_INITIALIZER("double"),        S8_INITIALIZER("else"),      S8_INITIALIZER("enum"),           S8_INITIALIZER("extern"),
    S8_INITIALIZER("float"),         S8_INITIALIZER("for"),       S8_INITIALIZER("goto"),           S8_INITIALIZER("if"),
    S8_INITIALIZER("inline"),        S8_INITIALIZER("int"),       S8_INITIALIZER("long"),           S8_INITIALIZER("register"),
    S8_INITIALIZER("restrict"),      S8_INITIALIZER("return"),    S8_INITIALIZER("short"),          S8_INITIALIZER("signed"),
    S8_INITIALIZER("sizeof"),        S8_INITIALIZER("static"),    S8_INITIALIZER("struct"),         S8_INITIALIZER("switch"),
    S8_INITIALIZER("typedef"),       S8_INITIALIZER("union"),     S8_INITIALIZER("unsigned"),       S8_INITIALIZER("void"),
    S8_INITIALIZER("volatile"),      S8_INITIALIZER("while"),     S8_INITIALIZER("_Alignas"),       S8_INITIALIZER("_Alignof"),
    S8_INITIALIZER("_Atomic"),       S8_INITIALIZER("_Bool"),     S8_INITIALIZER("_Complex"),       S8_INITIALIZER("_Generic"),
    S8_INITIALIZER("_Imaginary"),    S8_INITIALIZER("_Noreturn"), S8_INITIALIZER("_Static_assert"), S8_INITIALIZER("_Thread_local"),
    S8_INITIALIZER("__attribute__"), S8_INITIALIZER("__declspec"), S8_INITIALIZER("__auto_type"),   S8_INITIALIZER("__thread"),
    S8_INITIALIZER("__typeof__"),    S8_INITIALIZER("__extension__"), S8_INITIALIZER("__inline"),   S8_INITIALIZER("__inline__"),
    S8_INITIALIZER("__const"),       S8_INITIALIZER("__const__"), S8_INITIALIZER("__volatile"),     S8_INITIALIZER("__volatile__"),
    S8_INITIALIZER("__restrict"),    S8_INITIALIZER("__restrict__"), S8_INITIALIZER("__signed"),    S8_INITIALIZER("__signed__"),
    S8_INITIALIZER("__asm"),         S8_INITIALIZER("__asm__"),   S8_INITIALIZER("__alignof"),      S8_INITIALIZER("__alignof__"),
    S8_INITIALIZER("_Nonnull"),      S8_INITIALIZER("_Nullable"), S8_INITIALIZER("_Null_unspecified"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings) < C_DECLARATION_KEYWORD_SLOT_COUNT / 2);

// Names whose parse-side token class is nonzero but that are not part of the
// builtin ladder; interned at create time so every classifiable name has a
// predefined id and `symbol > c_symbol_predefined_limit` is a constant-time
// "ordinary identifier" answer.
BUSTER_C_INTERNAL String8 const c_symbol_classified_extras[] = {
    S8_INITIALIZER("true"),          S8_INITIALIZER("false"),  S8_INITIALIZER("nullptr"),
    S8_INITIALIZER("alignof"),       S8_INITIALIZER("constexpr"), S8_INITIALIZER("typeof_unqual"),
    S8_INITIALIZER("typeof"),        S8_INITIALIZER("vector_size"), S8_INITIALIZER("__vector_size"),
    S8_INITIALIZER("__vector_size__"),
};

enum
{
    C_SYMBOL_PREDEFINED_LIMIT_CAPACITY = 256,
};

BUSTER_C_SHARED u8 c_parse_token_class_compute(String8 spelling);

BUSTER_C_INTERNAL CSymbolTable c_symbol_table_create(Arena* arena)
{
    CSymbolTable table = {
        .arena = arena,
        .slot_capacity = 1u << 14,
        .name_capacity = 1u << 12,
    };
    table.names = arena_allocate(arena, String8, table.name_capacity);
    table.slots = arena_allocate(arena, CSymbolSlot, table.slot_capacity);
    memset(table.slots, 0, sizeof(*table.slots) * table.slot_capacity);
    table.builtin_kinds = arena_allocate(arena, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    table.class_bits = arena_allocate(arena, u8, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    memset(table.builtin_kinds, 0, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    memset(table.class_bits, 0, C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    for (u64 index = 0; index < C_SYMBOL_PREDEFINED_COUNT; index += 1)
    {
        u32 id = c_symbol_intern(&table, c_symbol_predefined[index].name);
        BUSTER_CHECK(id == index + 1);
        // Routed through the spelling scan instead of reading
        // c_symbol_predefined[index].builtin directly, so the two
        // classification paths cannot drift.
        table.builtin_kinds[id] = (u8)c_symbol_builtin_from_spelling(c_symbol_predefined[index].name);
    }
    // The declaration keywords and the classified extras join the predefined
    // range; their class bits come from the same compute the spelling
    // fallback uses, so the two classification paths cannot drift.
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings); index += 1)
    {
        c_symbol_intern(&table, c_declaration_keyword_spellings[index]);
    }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(c_symbol_classified_extras); index += 1)
    {
        c_symbol_intern(&table, c_symbol_classified_extras[index]);
    }
    BUSTER_CHECK(table.count < C_SYMBOL_PREDEFINED_LIMIT_CAPACITY);
    table.predefined_limit = table.count;
    for (u32 id = 1; id <= table.count; id += 1)
    {
        table.class_bits[id] = c_parse_token_class_compute(table.names[id]);
    }
    return table;
}

BUSTER_C_INTERNAL void c_symbols_intern_tokens(CSymbolTable* table, char8 const* spelling_base, CToken* tokens, u64 token_count)
{
    for (u64 index = 0; index < token_count; index += 1)
    {
        if (tokens[index].kind == C_TOKEN_IDENTIFIER)
        {
            tokens[index].symbol = c_symbol_intern(table, c_token_spelling(spelling_base, tokens[index]));
        }
    }
}

// A token in flight through the preprocessor's expansion machinery, carrying
// the source location the final stream and diagnostics must preserve.
// `foreign` marks tokens whose location no longer derives from their
// spelling offset (macro replacement stamps the invocation location onto
// every replacement token); output materialization copies their spellings
// into the spelling space under an expansion source-map entry so on-demand
// recovery reproduces the stamped location exactly.
typedef struct CPpToken CPpToken;
struct CPpToken
{
    CToken token;
    CSourceLocation location;
    bool foreign;
};

typedef struct CPreprocessTokenNode CPreprocessTokenNode;
struct CPreprocessTokenNode
{
    CPreprocessTokenNode* next;
    CPpToken token;
};

// The preprocessed output is a list of contiguous token ranges — one per
// expansion-free logical line (referencing the line's staging array
// directly) or per expanded line (materialized once from its node list) —
// so final assembly is a few bulk copies instead of a per-token node walk.
typedef struct CPreprocessTokenRange CPreprocessTokenRange;
struct CPreprocessTokenRange
{
    CPreprocessTokenRange* next;
    CToken* tokens;
    u64 count;
};

BUSTER_C_INTERNAL void c_preprocess_output_append_range(Arena* arena, CPreprocessTokenRange** first, CPreprocessTokenRange** last, CToken* tokens, u64 count)
{
    if (*last && (*last)->tokens + (*last)->count == tokens)
    {
        (*last)->count += count;
        return;
    }
    CPreprocessTokenRange* range = arena_allocate(arena, CPreprocessTokenRange, 1);
    *range = (CPreprocessTokenRange){
        .tokens = tokens,
        .count = count,
    };
    if (*last)
    {
        (*last)->next = range;
    }
    else
    {
        *first = range;
    }
    *last = range;
}

typedef enum CMacroExpansionTaskKind
{
    C_MACRO_EXPANSION_TOKEN,
    C_MACRO_EXPANSION_ENABLE,
} CMacroExpansionTaskKind;

typedef struct CMacroExpansionTask CMacroExpansionTask;
struct CMacroExpansionTask
{
    CMacroExpansionTask* previous;
    CMacro* macro;
    CPpToken token;
    CMacroExpansionTaskKind kind;
};

typedef struct CMacroArgument CMacroArgument;
struct CMacroArgument
{
    CPreprocessTokenNode* first;
    CPreprocessTokenNode* last;
    CPpToken* tokens;
    CPpToken* expanded_tokens;
    u64 token_count;
    u64 expanded_token_count;
};

typedef struct CMacroExpansionContinuation CMacroExpansionContinuation;

typedef struct CMacroExpansionContext CMacroExpansionContext;
struct CMacroExpansionContext
{
    CMacroExpansionContext* parent;
    CMacroExpansionContinuation* continuation;
    CMacroExpansionTask* top;
    CPreprocessTokenNode* first_output;
    CPreprocessTokenNode* last_output;
    u64 output_count;
};

struct CMacroExpansionContinuation
{
    CMacroExpansionContext* parent;
    CMacro* macro;
    CMacroArgument* arguments;
    CPpToken invocation;
    u32 argument_count;
    u32 argument_index;
};

typedef struct CMacroReplacementToken CMacroReplacementToken;
struct CMacroReplacementToken
{
    CPpToken token;
    bool placemarker;
};

BUSTER_C_INTERNAL bool c_token_spelling_equal(char8 const* spelling_base, CToken token, String8 spelling)
{
    return string_equal(c_token_spelling(spelling_base, token), spelling);
}

// One compare, and deliberately no kind test: only a C_TOKEN_PUNCTUATOR token
// ever carries a punctuator id, so the id alone answers the question.  Every
// site that retypes a token must keep that invariant.
BUSTER_C_SHARED bool c_token_is_punctuator(const CToken* token, CPunctuator punctuator)
{
    return token->punctuator == punctuator;
}

// Macros are found by interned symbol id: the buckets hold sequential ids
// (uniform under a power-of-two mask) and the chains compare one integer.
// Symbol 0 (an uninterned token) matches nothing because every definition
// interns its name.
BUSTER_C_INTERNAL CMacro* c_macro_find(CMacro* first, u32 symbol)
{
    if (symbol)
    {
        if (first && first->bucket_count)
        {
            u32 bucket = symbol & (first->bucket_count - 1);
            for (CMacro* macro = first->buckets[bucket]; macro; macro = macro->hash_next)
            {
                if (macro->symbol == symbol)
                {
                    return macro;
                }
            }
            return 0;
        }
        for (CMacro* macro = first; macro; macro = macro->next)
        {
            if (macro->symbol == symbol)
            {
                return macro;
            }
        }
    }

    return 0;
}

// The lookup for tokens that may not have passed the intern pass (pasted or
// synthesized): intern on demand when a table is available so the fast
// symbol lookup stays authoritative.
BUSTER_C_INTERNAL CMacro* c_macro_find_token(CMacro* first, CSymbolTable* symbols, char8 const* spelling_base, CToken const* token)
{
    u32 symbol = token->symbol;
    if (!symbol && symbols)
    {
        symbol = c_symbol_intern(symbols, c_token_spelling(spelling_base, *token));
    }
    return c_macro_find(first, symbol);
}

BUSTER_C_INTERNAL CMacro* c_macro_define(Arena* arena, CSymbolTable* symbols, CMacro** first, CMacro** last, String8 name, CToken* replacement,
                                           u32 replacement_count, String8* parameters, u32 parameter_count, bool function_like, bool variadic)
{
    u32 symbol = c_symbol_intern(symbols, name);
    CMacro* macro = c_macro_find(*first, symbol);
    if (!macro)
    {
        macro = arena_allocate(arena, CMacro, 1);
        *macro = (CMacro){
            .name = name,
            .symbol = symbol,
        };
        if (*last)
        {
            (*last)->next = macro;
        }
        else
        {
            *first = macro;
        }
        *last = macro;
        if (!(*first)->bucket_count)
        {
            c_macro_hash_rebuild(arena, *first, 64);
        }
        else if (((*first)->hash_count + 1) * 4 > (*first)->bucket_count * 3)
        {
            c_macro_hash_rebuild(arena, *first, (*first)->bucket_count * 2);
        }
        else
        {
            u32 bucket = symbol & ((*first)->bucket_count - 1);
            macro->hash_next = (*first)->buckets[bucket];
            (*first)->buckets[bucket] = macro;
            (*first)->hash_count += 1;
        }
    }
    macro->definition = (CMacroDefinition){
        .replacement = replacement,
        .replacement_count = replacement_count,
        .parameters = parameters,
        .parameter_count = parameter_count,
        .function_like = function_like,
        .variadic = variadic,
        .defined = true,
    };
    return macro;
}

BUSTER_C_INTERNAL void c_macro_define_object_text(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro** first, CMacro** last, String8 name,
                                                    String8 replacement_text)
{
    CLexResult lex = c_lex_space(arena, space, replacement_text);
    c_symbols_intern_tokens(symbols, lex.spelling_base, lex.tokens, lex.token_count);
    u32 replacement_count = 0;
    while (replacement_count < lex.token_count && lex.tokens[replacement_count].kind != C_TOKEN_NEWLINE &&
           lex.tokens[replacement_count].kind != C_TOKEN_END_OF_FILE)
    {
        replacement_count += 1;
    }
    c_macro_define(arena, symbols, first, last, name, lex.tokens, replacement_count, 0, 0, false, false);
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_reserve(Arena* arena, CPreprocessResult* result)
{
    if (result->diagnostic_count < result->diagnostic_capacity)
    {
        return;
    }
    u64 capacity = result->diagnostic_capacity ? result->diagnostic_capacity * 2 : 1;
    if (capacity <= result->diagnostic_count)
    {
        capacity = result->diagnostic_count + 1;
    }
    CDiagnostic* diagnostics = arena_allocate(arena, CDiagnostic, capacity);
    if (result->diagnostic_count)
    {
        memcpy(diagnostics, result->diagnostics, sizeof(*diagnostics) * result->diagnostic_count);
    }
    result->diagnostics = diagnostics;
    result->diagnostic_capacity = capacity;
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_push_severity(Arena* arena, CPreprocessResult* result, CSourceLocation location, CDiagnosticKind kind,
                                                               CDiagnosticSeverity severity, String8 message)
{
    c_preprocess_diagnostic_reserve(arena, result);
    result->diagnostics[result->diagnostic_count++] = (CDiagnostic){
        .message = message,
        .location = location,
        .kind = kind,
        .severity = severity,
    };
    if (severity == C_DIAGNOSTIC_WARNING)
    {
        result->warning_count += 1;
    }
    else
    {
        result->error_count += 1;
    }
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_push(Arena* arena, CPreprocessResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)
{
    c_preprocess_diagnostic_push_severity(arena, result, location, kind, C_DIAGNOSTIC_ERROR, message);
}

BUSTER_C_INTERNAL void c_preprocess_diagnostic_copy(Arena* arena, CPreprocessResult* result, CDiagnostic diagnostic)
{
    c_preprocess_diagnostic_reserve(arena, result);
    result->diagnostics[result->diagnostic_count++] = diagnostic;
    if (diagnostic.severity == C_DIAGNOSTIC_WARNING)
    {
        result->warning_count += 1;
    }
    else
    {
        result->error_count += 1;
    }
}

BUSTER_C_INTERNAL void c_preprocess_output_push(Arena* arena, CPreprocessTokenNode** first, CPreprocessTokenNode** last, CPpToken token, u64* count)
{
    CPreprocessTokenNode* node = arena_allocate(arena, CPreprocessTokenNode, 1);
    node->next = 0;
    node->token = token;
    if (*last)
    {
        (*last)->next = node;
    }
    else
    {
        *first = node;
    }
    *last = node;
    *count += 1;
}

BUSTER_C_INTERNAL void c_macro_expansion_task_push(Arena* arena, CMacroExpansionTask** top, CPpToken token, CMacro* macro, CMacroExpansionTaskKind kind)
{
    CMacroExpansionTask* task = arena_allocate(arena, CMacroExpansionTask, 1);
    *task = (CMacroExpansionTask){
        .previous = *top,
        .macro = macro,
        .token = token,
        .kind = kind,
    };
    *top = task;
}

BUSTER_C_INTERNAL s32 c_macro_parameter_index(CMacro* macro, String8 name)
{
    for (u32 parameter_index = 0; parameter_index < macro->definition.parameter_count; parameter_index += 1)
    {
        if (string_equal(macro->definition.parameters[parameter_index], name))
        {
            return (s32)parameter_index;
        }
    }
    return -1;
}

BUSTER_C_INTERNAL bool c_macro_invocation_arguments(Arena* arena, CMacroExpansionTask** top, CMacro* macro, CSourceLocation location,
                                                      CMacroArgument** arguments_out, u32* argument_count_out, CPreprocessResult* result)
{
    CMacroExpansionTask* open = *top;
    while (open && open->kind == C_MACRO_EXPANSION_ENABLE)
    {
        *top = open->previous;
        open->macro->disabled = false;
        open = *top;
    }
    if (!open || open->kind != C_MACRO_EXPANSION_TOKEN || !c_token_is_punctuator(&open->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        return false;
    }
    *top = open->previous;
    u32 capacity = macro->definition.parameter_count + 1;
    CMacroArgument* arguments = arena_allocate(arena, CMacroArgument, capacity);
    for (u32 argument_index = 0; argument_index < capacity; argument_index += 1)
    {
        arguments[argument_index] = (CMacroArgument){0};
    }
    u32 argument_count = macro->definition.parameter_count ? 1 : 0;
    u32 current = 0;
    u32 depth = 0;
    bool closed = false;
    while (*top)
    {
        CMacroExpansionTask* task = *top;
        *top = task->previous;
        if (task->kind == C_MACRO_EXPANSION_ENABLE)
        {
            task->macro->disabled = false;
            continue;
        }
        CPpToken token = task->token;
        if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            depth += 1;
        }
        else if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (!depth)
            {
                closed = true;
                break;
            }
            depth -= 1;
        }
        else if (c_token_is_punctuator(&token.token, C_PUNCTUATOR_COMMA) && !depth)
        {
            bool collect_variadic = macro->definition.variadic && current + 1 >= macro->definition.parameter_count;
            if (!collect_variadic)
            {
                current += 1;
                if (current >= capacity)
                {
                    c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, S8("too many arguments in macro invocation"));
                    return true;
                }
                argument_count = BUSTER_MAX(argument_count, current + 1);
                continue;
            }
        }
        if (!argument_count)
        {
            argument_count = 1;
        }
        c_preprocess_output_push(arena, &arguments[current].first, &arguments[current].last, token, &arguments[current].token_count);
    }
    if (!closed)
    {
        c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION,
                                     string_format(arena, S8("unterminated invocation of macro '{S8}'"), macro->name));
        return true;
    }
    if (macro->definition.variadic && argument_count + 1 == macro->definition.parameter_count)
    {
        argument_count += 1;
    }
    if (argument_count != macro->definition.parameter_count)
    {
        c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_MACRO_INVOCATION, S8("macro argument count does not match its definition"));
        return true;
    }
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        CMacroArgument* argument = arguments + argument_index;
        argument->tokens = arena_allocate(arena, CPpToken, argument->token_count);
        u64 token_index = 0;
        for (CPreprocessTokenNode* node = argument->first; node; node = node->next)
        {
            argument->tokens[token_index++] = node->token;
        }
    }
    *arguments_out = arguments;
    *argument_count_out = argument_count;
    return true;
}

BUSTER_C_INTERNAL CPpToken c_macro_stringify(CSpellingSpace* space, CMacroArgument argument, CSourceLocation location)
{
    char8 const* base = space->base;
    u64 length = 2;
    for (u64 token_index = 0; token_index < argument.token_count; token_index += 1)
    {
        String8 spelling = c_token_spelling(base, argument.tokens[token_index].token);
        length += spelling.length;
        length += token_index != 0;
        for (u64 character_index = 0; character_index < spelling.length; character_index += 1)
        {
            char8 character = spelling.pointer[character_index];
            length += character == '\\' || character == '"';
        }
    }
    char8* spelling = c_space_allocate(space, length + 1);
    u64 output = 0;
    spelling[output++] = '"';
    for (u64 token_index = 0; token_index < argument.token_count; token_index += 1)
    {
        String8 token_spelling = c_token_spelling(base, argument.tokens[token_index].token);
        if (token_index)
        {
            spelling[output++] = ' ';
        }
        for (u64 character_index = 0; character_index < token_spelling.length; character_index += 1)
        {
            char8 character = token_spelling.pointer[character_index];
            if (character == '\\' || character == '"')
            {
                spelling[output++] = '\\';
            }
            spelling[output++] = character;
        }
    }
    spelling[output++] = '"';
    spelling[output] = 0;
    return (CPpToken){
        .token =
            {
                .offset = c_space_offset(space, spelling),
                // Every interior quote and backslash was escaped above, so
                // the closing quote terminates the literal exactly and an
                // oversized result satisfies the sentinel's contract.
                .length = c_token_length_field(output),
                .kind = C_TOKEN_STRING_LITERAL,
            },
        .location = location,
        .foreign = true,
    };
}

BUSTER_C_INTERNAL bool c_macro_is_paste(CToken token)
{
    return c_token_is_punctuator(&token, C_PUNCTUATOR_HASH_HASH);
}

BUSTER_C_INTERNAL u32 c_preprocess_builtin_line(CMacro* first);

BUSTER_C_INTERNAL CPpToken c_macro_builtin_token(CSpellingSpace* space, CMacro* first, u8 builtin, CSourceLocation location)
{
    if (builtin == C_MACRO_BUILTIN_LINE)
    {
        char8* digits = c_space_allocate(space, 11);
        u32 value = c_preprocess_builtin_line(first);
        u32 length = 0;
        do
        {
            digits[9 - length] = (char8)('0' + value % 10);
            value /= 10;
            length += 1;
        } while (value);
        digits[10] = 0;
        return (CPpToken){
            .token =
                {
                    .offset = c_space_offset(space, digits + 10 - length),
                    .length = (u16)length,
                    .kind = C_TOKEN_PREPROCESSING_NUMBER,
                },
            .location = location,
            .foreign = true,
        };
    }
    String8 path = first->builtin_path;
    u64 capacity = path.length * 2 + 3;
    char8* quoted = c_space_allocate(space, capacity);
    u64 output = 0;
    quoted[output++] = '"';
    for (u64 index = 0; index < path.length; index += 1)
    {
        char8 character = path.pointer[index];
        if (character == '\\' || character == '"')
        {
            quoted[output++] = '\\';
        }
        quoted[output++] = character;
    }
    quoted[output++] = '"';
    quoted[output] = 0;
    c_space_shrink(space, capacity - (output + 1));
    return (CPpToken){
        .token =
            {
                .offset = c_space_offset(space, quoted),
                .length = c_token_length_field(output),
                .kind = C_TOKEN_STRING_LITERAL,
            },
        .location = location,
        .foreign = true,
    };
}

BUSTER_C_INTERNAL bool c_macro_replacement_tokens(Arena* arena, CSpellingSpace* space, CMacro* first, CMacro* macro, CMacroArgument* arguments,
                                                    CSourceLocation location, CPreprocessResult* result, CPpToken** tokens_out, u32* token_count_out)
{
    char8 const* base = space->base;
    if (macro->builtin)
    {
        CPpToken* builtin_token = arena_allocate(arena, CPpToken, 1);
        builtin_token[0] = c_macro_builtin_token(space, first, macro->builtin, location);
        *tokens_out = builtin_token;
        *token_count_out = 1;
        return true;
    }
    u64 capacity = macro->definition.replacement_count + 1;
    for (u32 replacement_index = 0; replacement_index < macro->definition.replacement_count; replacement_index += 1)
    {
        CToken replacement = macro->definition.replacement[replacement_index];
        s32 parameter_index =
            replacement.kind == C_TOKEN_IDENTIFIER && macro->definition.function_like ? c_macro_parameter_index(macro, c_token_spelling(base, replacement)) : -1;
        if (parameter_index >= 0)
        {
            CMacroArgument argument = arguments[(u32)parameter_index];
            capacity += BUSTER_MAX(argument.token_count, argument.expanded_token_count);
        }
    }
    CMacroReplacementToken* materialized = arena_allocate(arena, CMacroReplacementToken, capacity);
    u32 materialized_count = 0;
    for (u32 replacement_index = 0; replacement_index < macro->definition.replacement_count; replacement_index += 1)
    {
        CToken replacement = macro->definition.replacement[replacement_index];
        if (macro->definition.function_like && c_token_is_punctuator(&replacement, C_PUNCTUATOR_HASH) &&
            replacement_index + 1 < macro->definition.replacement_count)
        {
            CToken parameter = macro->definition.replacement[replacement_index + 1];
            s32 parameter_index = parameter.kind == C_TOKEN_IDENTIFIER ? c_macro_parameter_index(macro, c_token_spelling(base, parameter)) : -1;
            if (parameter_index >= 0)
            {
                materialized[materialized_count++] = (CMacroReplacementToken){
                    .token = c_macro_stringify(space, arguments[(u32)parameter_index], location),
                };
                replacement_index += 1;
                continue;
            }
        }
        s32 parameter_index =
            replacement.kind == C_TOKEN_IDENTIFIER && macro->definition.function_like ? c_macro_parameter_index(macro, c_token_spelling(base, replacement)) : -1;
        if (parameter_index < 0)
        {
            materialized[materialized_count++] = (CMacroReplacementToken){
                .token = {.token = replacement, .location = location, .foreign = true},
            };
            continue;
        }
        CMacroArgument argument = arguments[(u32)parameter_index];
        bool raw_argument = (replacement_index && c_macro_is_paste(macro->definition.replacement[replacement_index - 1])) ||
                            (replacement_index + 1 < macro->definition.replacement_count && c_macro_is_paste(macro->definition.replacement[replacement_index + 1]));
        CPpToken* argument_tokens = raw_argument ? argument.tokens : argument.expanded_tokens;
        u64 argument_token_count = raw_argument ? argument.token_count : argument.expanded_token_count;
        if (!argument_token_count)
        {
            materialized[materialized_count++] = (CMacroReplacementToken){
                .token = {.token = replacement, .location = location, .foreign = true},
                .placemarker = true,
            };
            continue;
        }
        for (u64 argument_index = 0; argument_index < argument_token_count; argument_index += 1)
        {
            CPpToken argument_token = argument_tokens[argument_index];
            argument_token.location = location;
            argument_token.foreign = true;
            materialized[materialized_count++] = (CMacroReplacementToken){
                .token = argument_token,
            };
        }
    }
    CPpToken* output = arena_allocate(arena, CPpToken, materialized_count);
    u32 output_count = 0;
    for (u32 index = 0; index < materialized_count; index += 1)
    {
        CMacroReplacementToken item = materialized[index];
        if (!c_macro_is_paste(item.token.token))
        {
            if (!item.placemarker)
            {
                output[output_count++] = item.token;
            }
            continue;
        }
        if (!output_count || index + 1 >= materialized_count)
        {
            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_TOKEN_PASTE,
                                         string_format(arena, S8("'##' appears at the edge of macro '{S8}'"), macro->name));
            return false;
        }
        CMacroReplacementToken right = materialized[++index];
        if (right.placemarker)
        {
            if (macro->definition.variadic && c_token_is_punctuator(&output[output_count - 1].token, C_PUNCTUATOR_COMMA))
            {
                output_count -= 1;
            }
            continue;
        }
        CPpToken left = output[--output_count];
        String8 left_spelling = c_token_spelling(base, left.token);
        String8 right_spelling = c_token_spelling(base, right.token.token);
        u64 joined_length = left_spelling.length + right_spelling.length;
        // The joined text lives in the spelling space so the pasted token's
        // offset resolves like any other; pasting never crosses a newline or
        // splice, so relexing it cannot change its bytes and the relex is
        // validation plus kind classification only.
        char8* joined = c_space_allocate(space, joined_length + 1);
        memcpy(joined, left_spelling.pointer, left_spelling.length);
        memcpy(joined + left_spelling.length, right_spelling.pointer, right_spelling.length);
        joined[joined_length] = 0;
        TemporalArena paste_temporary = scratch_begin(&arena, 1);
        CLexResult lex = c_lex(paste_temporary.arena, (String8){
                                                          .pointer = joined,
                                                          .length = joined_length,
                                                      });
        // The oversized-token diagnostics count here: a pasted identifier or
        // number past the length field's reach fails as an invalid paste
        // instead of storing a sentinel only literals may carry.
        bool paste_valid = !lex.diagnostic_count && lex.token_count == 2 && lex.tokens[0].kind != C_TOKEN_END_OF_FILE &&
                           c_token_length(lex.spelling_base, lex.tokens[0]) == joined_length;
        CToken pasted_shape = paste_valid ? lex.tokens[0] : (CToken){0};
        scratch_end(paste_temporary);
        if (!paste_valid)
        {
            c_preprocess_diagnostic_push(arena, result, location, C_DIAGNOSTIC_INVALID_TOKEN_PASTE,
                                         string_format(arena, S8("token paste '{S8}##{S8}' in macro '{S8}' does not form one preprocessing token"),
                                                       left_spelling, right_spelling, macro->name));
            return false;
        }
        output[output_count++] = (CPpToken){
            .token =
                {
                    .offset = c_space_offset(space, joined),
                    .length = c_token_length_field(joined_length),
                    .kind = pasted_shape.kind,
                    .punctuator = pasted_shape.punctuator,
                },
            .location = location,
            .foreign = true,
        };
    }
    *tokens_out = output;
    *token_count_out = output_count;
    return true;
}

BUSTER_C_INTERNAL CPpToken c_macro_pragma_token(CSpellingSpace* space, CMacro* macro, CMacroArgument argument, CPpToken invocation)
{
    char8 const* base = space->base;
    CPpToken result = invocation;
    result.token.kind = C_TOKEN_PRAGMA;
    result.token.punctuator = C_PUNCTUATOR_NONE;
    result.token.offset = 0;
    result.token.length = 0;
    result.token.symbol = 0;
    result.foreign = true;
    CPpToken* tokens = argument.expanded_tokens;
    u64 token_count = argument.expanded_token_count;
    if (string_equal(macro->name, S8("_Pragma")))
    {
        String8 first_spelling = token_count == 1 ? c_token_spelling(base, tokens[0].token) : (String8){0};
        // Text past the length field's reach stays length 0 like any other
        // malformed operand: the marker is dropped, and no real pragma body
        // approaches 64 KB.
        if (token_count == 1 && tokens[0].token.kind == C_TOKEN_STRING_LITERAL && first_spelling.length >= 2 &&
            first_spelling.length - 2 < C_TOKEN_LENGTH_OVERSIZED)
        {
            String8 inner = {
                .pointer = first_spelling.pointer + 1,
                .length = first_spelling.length - 2,
            };
            char8* spelling = c_space_allocate(space, inner.length + 1);
            u64 output = 0;
            for (u64 index = 0; index < inner.length; index += 1)
            {
                if (inner.pointer[index] == '\\' && index + 1 < inner.length)
                {
                    index += 1;
                }
                spelling[output++] = inner.pointer[index];
            }
            spelling[output] = 0;
            result.token.offset = c_space_offset(space, spelling);
            result.token.length = (u16)output;
        }
        return result;
    }
    u64 length = 0;
    for (u64 token_index = 0; token_index < token_count; token_index += 1)
    {
        length += c_token_length(base, tokens[token_index].token) + (token_index != 0);
    }
    if (length && length < C_TOKEN_LENGTH_OVERSIZED)
    {
        char8* spelling = c_space_allocate(space, length + 1);
        u64 output = 0;
        for (u64 token_index = 0; token_index < token_count; token_index += 1)
        {
            String8 token_spelling = c_token_spelling(base, tokens[token_index].token);
            if (token_index)
            {
                spelling[output++] = ' ';
            }
            memcpy(spelling + output, token_spelling.pointer, token_spelling.length);
            output += token_spelling.length;
        }
        spelling[output] = 0;
        result.token.offset = c_space_offset(space, spelling);
        result.token.length = (u16)output;
    }
    return result;
}

BUSTER_C_INTERNAL bool c_preprocess_expand(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro, CPpToken* input, u32 input_count,
                                             CPreprocessTokenNode** first_output, CPreprocessTokenNode** last_output, u64* output_count, u32 expansion_limit,
                                             CPreprocessResult* result)
{
    CMacroExpansionContext* context = arena_allocate(arena, CMacroExpansionContext, 1);
    *context = (CMacroExpansionContext){
        .first_output = *first_output,
        .last_output = *last_output,
        .output_count = *output_count,
    };
    for (u32 input_index = input_count; input_index; input_index -= 1)
    {
        c_macro_expansion_task_push(arena, &context->top, input[input_index - 1], 0, C_MACRO_EXPANSION_TOKEN);
    }
    u32 expansion_count = 0;
    while (context)
    {
        if (!context->top)
        {
            CMacroExpansionContinuation* continuation = context->continuation;
            if (!continuation)
            {
                *first_output = context->first_output;
                *last_output = context->last_output;
                *output_count = context->output_count;
                return true;
            }
            CMacroArgument* argument = continuation->arguments + continuation->argument_index;
            argument->expanded_tokens = arena_allocate(arena, CPpToken, context->output_count);
            argument->expanded_token_count = context->output_count;
            u64 expanded_index = 0;
            for (CPreprocessTokenNode* node = context->first_output; node; node = node->next)
            {
                argument->expanded_tokens[expanded_index++] = node->token;
            }
            continuation->argument_index += 1;
            if (continuation->argument_index < continuation->argument_count)
            {
                argument = continuation->arguments + continuation->argument_index;
                CMacroExpansionContext* child = arena_allocate(arena, CMacroExpansionContext, 1);
                *child = (CMacroExpansionContext){
                    .parent = continuation->parent,
                    .continuation = continuation,
                };
                for (u64 argument_index = argument->token_count; argument_index; argument_index -= 1)
                {
                    c_macro_expansion_task_push(arena, &child->top, argument->tokens[argument_index - 1], 0, C_MACRO_EXPANSION_TOKEN);
                }
                context = child;
                continue;
            }
            CMacroExpansionContext* parent = continuation->parent;
            CMacro* macro = continuation->macro;
            CPpToken invocation = continuation->invocation;
            CPpToken* replacement_tokens = 0;
            u32 replacement_count = 0;
            if (!c_macro_replacement_tokens(arena, space, first_macro, macro, continuation->arguments, invocation.location, result, &replacement_tokens,
                                            &replacement_count))
            {
                return false;
            }
            if (macro->definition.pragma_like)
            {
                if (continuation->argument_count == 1)
                {
                    CPpToken pragma = c_macro_pragma_token(space, macro, continuation->arguments[0], invocation);
                    c_preprocess_output_push(arena, &parent->first_output, &parent->last_output, pragma, &parent->output_count);
                }
                context = parent;
                continue;
            }
            macro->disabled = true;
            c_macro_expansion_task_push(arena, &parent->top, (CPpToken){0}, macro, C_MACRO_EXPANSION_ENABLE);
            for (u32 replacement_index = replacement_count; replacement_index; replacement_index -= 1)
            {
                c_macro_expansion_task_push(arena, &parent->top, replacement_tokens[replacement_index - 1], 0, C_MACRO_EXPANSION_TOKEN);
            }
            context = parent;
            continue;
        }
        CMacroExpansionTask* task = context->top;
        context->top = task->previous;
        if (task->kind == C_MACRO_EXPANSION_ENABLE)
        {
            task->macro->disabled = false;
            continue;
        }
        CPpToken token = task->token;
        CMacro* macro = token.token.kind == C_TOKEN_IDENTIFIER ? c_macro_find_token(first_macro, symbols, space->base, &token.token) : 0;
        if (!macro || !macro->definition.defined || macro->disabled)
        {
            c_preprocess_output_push(arena, &context->first_output, &context->last_output, token, &context->output_count);
            continue;
        }
        CMacroArgument* arguments = 0;
        u32 argument_count = 0;
        if (macro->definition.function_like)
        {
            bool invocation = c_macro_invocation_arguments(arena, &context->top, macro, token.location, &arguments, &argument_count, result);
            if (!invocation)
            {
                c_preprocess_output_push(arena, &context->first_output, &context->last_output, token, &context->output_count);
                continue;
            }
            if (argument_count != macro->definition.parameter_count)
            {
                continue;
            }
        }
        expansion_count += 1;
        result->preprocessed.expansions += 1;
        if (expansion_count > expansion_limit)
        {
            c_preprocess_diagnostic_push(arena, result, token.location, C_DIAGNOSTIC_MACRO_EXPANSION_LIMIT, S8("macro expansion limit exceeded"));
            return false;
        }
        if (macro->definition.function_like && argument_count)
        {
            CMacroExpansionContinuation* continuation = arena_allocate(arena, CMacroExpansionContinuation, 1);
            *continuation = (CMacroExpansionContinuation){
                .parent = context,
                .macro = macro,
                .arguments = arguments,
                .invocation = token,
                .argument_count = argument_count,
            };
            CMacroArgument* argument = arguments;
            CMacroExpansionContext* child = arena_allocate(arena, CMacroExpansionContext, 1);
            *child = (CMacroExpansionContext){
                .parent = context,
                .continuation = continuation,
            };
            for (u64 argument_index = argument->token_count; argument_index; argument_index -= 1)
            {
                c_macro_expansion_task_push(arena, &child->top, argument->tokens[argument_index - 1], 0, C_MACRO_EXPANSION_TOKEN);
            }
            context = child;
            continue;
        }
        CPpToken* replacement_tokens = 0;
        u32 replacement_count = 0;
        if (!c_macro_replacement_tokens(arena, space, first_macro, macro, arguments, token.location, result, &replacement_tokens, &replacement_count))
        {
            return false;
        }
        macro->disabled = true;
        c_macro_expansion_task_push(arena, &context->top, (CPpToken){0}, macro, C_MACRO_EXPANSION_ENABLE);
        for (u32 replacement_index = replacement_count; replacement_index; replacement_index -= 1)
        {
            c_macro_expansion_task_push(arena, &context->top, replacement_tokens[replacement_index - 1], 0, C_MACRO_EXPANSION_TOKEN);
        }
    }
    return false;
}

BUSTER_C_SHARED u32 c_conditional_precedence(CConditionalOperator operation)
{
    switch (operation)
    {
    case C_CONDITIONAL_COMMA:
        return 0;
    case C_CONDITIONAL_QUESTION:
    case C_CONDITIONAL_SELECT:
        return 1;
    case C_CONDITIONAL_LOGICAL_OR:
        return 2;
    case C_CONDITIONAL_LOGICAL_AND:
        return 3;
    case C_CONDITIONAL_BITWISE_OR:
        return 4;
    case C_CONDITIONAL_BITWISE_XOR:
        return 5;
    case C_CONDITIONAL_BITWISE_AND:
        return 6;
    case C_CONDITIONAL_EQUAL:
    case C_CONDITIONAL_NOT_EQUAL:
        return 7;
    case C_CONDITIONAL_LESS:
    case C_CONDITIONAL_LESS_EQUAL:
    case C_CONDITIONAL_GREATER:
    case C_CONDITIONAL_GREATER_EQUAL:
        return 8;
    case C_CONDITIONAL_SHIFT_LEFT:
    case C_CONDITIONAL_SHIFT_RIGHT:
        return 9;
    case C_CONDITIONAL_ADD:
    case C_CONDITIONAL_SUBTRACT:
        return 10;
    case C_CONDITIONAL_MULTIPLY:
    case C_CONDITIONAL_DIVIDE:
    case C_CONDITIONAL_REMAINDER:
        return 11;
    case C_CONDITIONAL_UNARY_PLUS:
    case C_CONDITIONAL_UNARY_MINUS:
    case C_CONDITIONAL_LOGICAL_NOT:
    case C_CONDITIONAL_BITWISE_NOT:
    case C_CONDITIONAL_ADDRESS_OF:
    case C_CONDITIONAL_DEREFERENCE:
    case C_CONDITIONAL_CAST:
        return 12;
    case C_CONDITIONAL_OPEN:
    case C_CONDITIONAL_INDEX_OPEN:
    case C_CONDITIONAL_OPERATOR_COUNT:
        return 0;
    }
    return 0;
}

BUSTER_C_SHARED bool c_conditional_is_unary(CConditionalOperator operation)
{
    return operation >= C_CONDITIONAL_UNARY_PLUS && operation <= C_CONDITIONAL_CAST;
}

BUSTER_C_SHARED bool c_conditional_operator(CToken token, bool unary, CConditionalOperator* operation)
{
    if (token.kind != C_TOKEN_PUNCTUATOR)
    {
        return false;
    }
    // Punctuator ids and spellings are one-to-one, so matching the id is the
    // spelling compare the previous ladder spelled out.
#define C_CONDITIONAL_MATCH(punctuator, binary_operation, unary_operation)                                                                                     \
    case punctuator:                                                                                                                                           \
    {                                                                                                                                                          \
        *operation = unary ? (unary_operation) : (binary_operation);                                                                                           \
        return *operation != C_CONDITIONAL_OPERATOR_COUNT;                                                                                                     \
    }
    switch (token.punctuator)
    {
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PLUS, C_CONDITIONAL_ADD, C_CONDITIONAL_UNARY_PLUS)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_MINUS, C_CONDITIONAL_SUBTRACT, C_CONDITIONAL_UNARY_MINUS)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_EXCLAMATION, C_CONDITIONAL_OPERATOR_COUNT, C_CONDITIONAL_LOGICAL_NOT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_TILDE, C_CONDITIONAL_OPERATOR_COUNT, C_CONDITIONAL_BITWISE_NOT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_STAR, C_CONDITIONAL_MULTIPLY, C_CONDITIONAL_DEREFERENCE)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SLASH, C_CONDITIONAL_DIVIDE, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PERCENT, C_CONDITIONAL_REMAINDER, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SHIFT_LEFT, C_CONDITIONAL_SHIFT_LEFT, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_SHIFT_RIGHT, C_CONDITIONAL_SHIFT_RIGHT, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_LESS, C_CONDITIONAL_LESS, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_LESS_EQUAL, C_CONDITIONAL_LESS_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_GREATER, C_CONDITIONAL_GREATER, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_GREATER_EQUAL, C_CONDITIONAL_GREATER_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_EQUAL, C_CONDITIONAL_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_NOT_EQUAL, C_CONDITIONAL_NOT_EQUAL, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_AMPERSAND, C_CONDITIONAL_BITWISE_AND, C_CONDITIONAL_ADDRESS_OF)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_CARET, C_CONDITIONAL_BITWISE_XOR, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PIPE, C_CONDITIONAL_BITWISE_OR, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_COMMA, C_CONDITIONAL_COMMA, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_AMPERSAND_AMPERSAND, C_CONDITIONAL_LOGICAL_AND, C_CONDITIONAL_OPERATOR_COUNT)
        C_CONDITIONAL_MATCH(C_PUNCTUATOR_PIPE_PIPE, C_CONDITIONAL_LOGICAL_OR, C_CONDITIONAL_OPERATOR_COUNT)
    default:
    {
        break;
    }
    }
#undef C_CONDITIONAL_MATCH
    return false;
}

BUSTER_C_SHARED bool c_conditional_number(String8 spelling, u64* value)
{
    u32 base = 10;
    u64 index = 0;
    if (spelling.length >= 2 && spelling.pointer[0] == '0')
    {
        if (spelling.pointer[1] == 'x' || spelling.pointer[1] == 'X')
        {
            base = 16;
            index = 2;
        }
        else if (spelling.pointer[1] == 'b' || spelling.pointer[1] == 'B')
        {
            base = 2;
            index = 2;
        }
        else
        {
            base = 8;
            index = 0;
        }
    }
    u64 parsed = 0;
    bool any = false;
    for (; index < spelling.length; index += 1)
    {
        char8 character = spelling.pointer[index];
        if (character == '\'')
        {
            continue;
        }
        u32 digit = UINT32_MAX;
        if (character >= '0' && character <= '9')
        {
            digit = (u32)(character - '0');
        }
        else if (character >= 'a' && character <= 'f')
        {
            digit = (u32)(character - 'a') + 10;
        }
        else if (character >= 'A' && character <= 'F')
        {
            digit = (u32)(character - 'A') + 10;
        }
        else
        {
            break;
        }
        if (digit >= base)
        {
            break;
        }
        parsed = parsed * base + digit;
        any = true;
    }
    *value = parsed;
    return any;
}

BUSTER_C_INTERNAL bool c_conditional_apply(CConditionalOperator operation, u64* values, u32* value_count)
{
    if (c_conditional_is_unary(operation))
    {
        if (!*value_count)
        {
            return false;
        }
        u64* value = values + *value_count - 1;
        switch (operation)
        {
        case C_CONDITIONAL_UNARY_PLUS:
            break;
        case C_CONDITIONAL_UNARY_MINUS:
            *value = 0 - *value;
            break;
        case C_CONDITIONAL_LOGICAL_NOT:
            *value = !*value;
            break;
        case C_CONDITIONAL_BITWISE_NOT:
            *value = ~*value;
            break;
        default:
            return false;
        }
        return true;
    }
    if (operation == C_CONDITIONAL_SELECT)
    {
        if (*value_count < 3)
        {
            return false;
        }
        u64 false_value = values[--*value_count];
        u64 true_value = values[--*value_count];
        u64* condition = values + *value_count - 1;
        *condition = *condition ? true_value : false_value;
        return true;
    }
    if (*value_count < 2)
    {
        return false;
    }
    u64 right = values[--*value_count];
    u64* left = values + *value_count - 1;
    switch (operation)
    {
    case C_CONDITIONAL_MULTIPLY:
        *left *= right;
        break;
    case C_CONDITIONAL_DIVIDE:
        if (!right)
            return false;
        *left /= right;
        break;
    case C_CONDITIONAL_REMAINDER:
        if (!right)
            return false;
        *left %= right;
        break;
    case C_CONDITIONAL_ADD:
        *left += right;
        break;
    case C_CONDITIONAL_SUBTRACT:
        *left -= right;
        break;
    case C_CONDITIONAL_SHIFT_LEFT:
        *left = right < 64 ? *left << right : 0;
        break;
    case C_CONDITIONAL_SHIFT_RIGHT:
        *left = right < 64 ? *left >> right : 0;
        break;
    case C_CONDITIONAL_LESS:
        *left = *left < right;
        break;
    case C_CONDITIONAL_LESS_EQUAL:
        *left = *left <= right;
        break;
    case C_CONDITIONAL_GREATER:
        *left = *left > right;
        break;
    case C_CONDITIONAL_GREATER_EQUAL:
        *left = *left >= right;
        break;
    case C_CONDITIONAL_EQUAL:
        *left = *left == right;
        break;
    case C_CONDITIONAL_NOT_EQUAL:
        *left = *left != right;
        break;
    case C_CONDITIONAL_BITWISE_AND:
        *left &= right;
        break;
    case C_CONDITIONAL_BITWISE_XOR:
        *left ^= right;
        break;
    case C_CONDITIONAL_BITWISE_OR:
        *left |= right;
        break;
    case C_CONDITIONAL_LOGICAL_AND:
        *left = *left && right;
        break;
    case C_CONDITIONAL_LOGICAL_OR:
        *left = *left || right;
        break;
    default:
        return false;
    }
    return true;
}

typedef enum CIncludeSearchKind
{
    C_INCLUDE_SEARCH_NONE,
    C_INCLUDE_SEARCH_QUOTED,
    C_INCLUDE_SEARCH_INCLUDE_PATH,
    C_INCLUDE_SEARCH_BUILTIN,
    C_INCLUDE_SEARCH_SYSTEM_PATH,
} CIncludeSearchKind;

typedef struct CIncludeSearchOrigin CIncludeSearchOrigin;
struct CIncludeSearchOrigin
{
    CIncludeSearchKind kind;
    u32 index;
};

BUSTER_C_INTERNAL bool c_include_resolve(Arena* arena, CPreprocessOptions options, String8 including_path, String8 name, bool quoted, String8* path_out,
                                           String8* source_out, FileMapRead* map_out, CIncludeSearchOrigin* origin_out);

BUSTER_C_INTERNAL bool c_include_resolve_next(Arena* arena, CPreprocessOptions options, String8 name, String8* path_out,
                                                String8* source_out, FileMapRead* map_out, CIncludeSearchOrigin origin,
                                                CIncludeSearchOrigin* origin_out);

BUSTER_C_INTERNAL bool c_include_name(Arena* arena, char8 const* base, CToken* tokens, u32 token_count, String8* name_out, bool* quoted_out);

BUSTER_C_INTERNAL bool c_conditional_builtin_supported(String8 name)
{
    static char const* supported[] = {
        "__builtin___clear_cache", "__builtin_acos",
        "__builtin_acosf",         "__builtin_ceil",
        "__builtin_ceilf",         "__builtin_clz",
        "__builtin_clzll",         "__builtin_cos",
        "__builtin_cosf",          "__builtin_ctz",
        "__builtin_ctzll",         "__builtin_debugtrap",
        "__builtin_popcount",      "__builtin_popcountll",
        "__builtin_assume_aligned", "__builtin_choose_expr",
        "__builtin_constant_p",    "__builtin_object_size",
        "__builtin_expect",        "__builtin_expect_with_probability",
        "__builtin_fabs",          "__builtin_fabsf",
        "__builtin_floor",         "__builtin_floorf",
        "__builtin_fmod",          "__builtin_fmodf",
        "__builtin_pow",           "__builtin_powf",
        "__builtin_prefetch",
        "__builtin_round",         "__builtin_roundf",
        "__builtin_sqrt",          "__builtin_sqrtf",
        "__builtin_strlen",        "__builtin_trap",
        "__builtin_types_compatible_p",
        "__builtin_unreachable",   "__builtin_c23_va_start",
        "__builtin_va_arg",        "__builtin_va_copy",
        "__builtin_va_end",        "__builtin_va_start",
        "__is_target_arch",        "__is_target_environment",
        "__is_target_os",          "__is_target_vendor",
    };
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(supported) && !result; index += 1)
    {
        u64 length = strlen(supported[index]);
        result = name.length == length && memcmp(name.pointer, supported[index], length) == 0;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_conditional_attribute_supported(String8 name)
{
    return string_equal(name, S8("vector_size")) || string_equal(name, S8("__vector_size")) || string_equal(name, S8("__vector_size__"));
}

BUSTER_C_INTERNAL bool c_conditional_feature_operators(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                                         CPreprocessTokenNode* first, CPreprocessOptions* options, String8 including_path,
                                                         CIncludeSearchOrigin including_origin)
{
    char8 const* base = space->base;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        CToken token = node->token.token;
        if (token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("defined")))
        {
            CPreprocessTokenNode* name = node->next;
            bool parenthesized = name && c_token_is_punctuator(&name->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            name = parenthesized ? name->next : name;
            if (!name || name->token.token.kind != C_TOKEN_IDENTIFIER)
            {
                return false;
            }
            CPreprocessTokenNode* after = name->next;
            if (parenthesized)
            {
                if (!after || !c_token_is_punctuator(&after->token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    return false;
                }
                after = after->next;
            }
            CMacro* macro = c_macro_find_token(first_macro, symbols, base, &name->token.token);
            node->token.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
            node->token.token.punctuator = C_PUNCTUATOR_NONE;
            node->token.token.offset = macro && macro->definition.defined ? C_SPELLING_ONE : C_SPELLING_ZERO;
            node->token.token.length = 1;
            node->token.token.symbol = 0;
            node->next = after;
            continue;
        }
        bool has_include = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_include"));
        bool has_include_next = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_include_next"));
        bool has_builtin = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__has_builtin"));
        bool has_attribute =
            token.kind == C_TOKEN_IDENTIFIER && (c_token_spelling_equal(base, token, S8("__has_attribute")) || c_token_spelling_equal(base, token, S8("__has_c_attribute")));
        bool has_feature =
            token.kind == C_TOKEN_IDENTIFIER && (c_token_spelling_equal(base, token, S8("__has_feature")) || c_token_spelling_equal(base, token, S8("__has_extension")) ||
                                                 c_token_spelling_equal(base, token, S8("__building_module")));
        bool is_target_arch = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_arch"));
        bool is_target_environment = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_environment"));
        bool is_target_os = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_os"));
        bool is_target_vendor = token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token, S8("__is_target_vendor"));
        if (!has_include && !has_include_next && !has_builtin && !has_attribute && !has_feature && !is_target_arch && !is_target_environment && !is_target_os &&
            !is_target_vendor)
        {
            continue;
        }
        CPreprocessTokenNode* open = node->next;
        if (!open || !c_token_is_punctuator(&open->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            return false;
        }
        CPreprocessTokenNode* close = 0;
        CPreprocessTokenNode* scan = open->next;
        u32 depth = 0;
        u32 argument_count = 0;
        for (; scan; scan = scan->next)
        {
            if (c_token_is_punctuator(&scan->token.token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&scan->token.token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                if (!depth)
                {
                    close = scan;
                    break;
                }
                depth -= 1;
            }
            argument_count += 1;
        }
        if (!close)
        {
            return false;
        }
        CToken* arguments = arena_allocate(arena, CToken, argument_count);
        u32 argument_index = 0;
        for (scan = open->next; scan != close; scan = scan->next)
        {
            arguments[argument_index++] = scan->token.token;
        }
        bool supported = false;
        if (has_include || has_include_next)
        {
            String8 include_name = {0};
            bool quoted = false;
            String8 resolved_path = {0};
            String8 resolved_source = {0};
            supported = c_include_name(arena, base, arguments, argument_count, &include_name, &quoted) &&
                        options &&
                        (has_include_next ? c_include_resolve_next(arena, *options, include_name, &resolved_path, &resolved_source, 0, including_origin, 0)
                                          : c_include_resolve(arena, *options, including_path, include_name, quoted, &resolved_path, &resolved_source, 0, 0));
        }
        else if ((has_builtin || has_attribute) && argument_count == 1 && arguments[0].kind == C_TOKEN_IDENTIFIER)
        {
            supported = has_builtin ? c_conditional_builtin_supported(c_token_spelling(base, arguments[0]))
                                    : c_conditional_attribute_supported(c_token_spelling(base, arguments[0]));
        }
        else if ((is_target_arch || is_target_environment || is_target_os || is_target_vendor) && argument_count == 1 &&
                 arguments[0].kind == C_TOKEN_IDENTIFIER && options)
        {
            String8 argument = c_token_spelling(base, arguments[0]);
            supported = is_target_arch        ? (options->target.cpu_arch == CPU_ARCH_AARCH64
                                                      ? string_equal(argument, S8("arm64")) || string_equal(argument, S8("aarch64"))
                                                  : options->target.cpu_arch == CPU_ARCH_WASM64 ? string_equal(argument, S8("wasm64"))
                                                  : options->target.cpu_arch == CPU_ARCH_BPFEL
                                                      ? string_equal(argument, S8("bpfel")) || string_equal(argument, S8("bpf")) ||
                                                            string_equal(argument, S8("ebpf"))
                                                      : string_equal(argument, S8("x86_64")))
                        : is_target_os          ? (options->target.os == OPERATING_SYSTEM_MACOS
                                                       ? string_equal(argument, S8("macos"))
                                                   : options->target.os == OPERATING_SYSTEM_IOS
                                                       ? string_equal(argument, S8("ios"))
                                                   : options->target.os == OPERATING_SYSTEM_LINUX
                                                       ? string_equal(argument, S8("linux"))
                                                       : false)
                        : is_target_vendor      ? (options->target.os == OPERATING_SYSTEM_MACOS || options->target.os == OPERATING_SYSTEM_IOS) &&
                                                      string_equal(argument, S8("apple"))
                        : is_target_environment ? false
                                                : false;
        }
        else if (!has_feature)
        {
            return false;
        }
        node->token.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
        node->token.token.punctuator = C_PUNCTUATOR_NONE;
        node->token.token.offset = supported ? C_SPELLING_ONE : C_SPELLING_ZERO;
        node->token.token.length = 1;
        node->token.token.symbol = 0;
        node->next = close->next;
    }
    return true;
}

BUSTER_C_INTERNAL bool c_integer_expression_evaluate_with_features(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                                                     CPpToken* tokens, u32 token_count, u32 expansion_limit, CPreprocessResult* result,
                                                                     CPreprocessOptions* options, String8 including_path,
                                                                     CIncludeSearchOrigin including_origin, u64* value_out)
{
    char8 const* base = space->base;
    CPpToken* transformed = arena_allocate(arena, CPpToken, token_count);
    u32 transformed_count = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        CPpToken token = tokens[token_index];
        if (token.token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token.token, S8("defined")))
        {
            u32 name_index = token_index + 1;
            bool parenthesized = name_index < token_count && c_token_is_punctuator(&tokens[name_index].token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            name_index += parenthesized;
            if (name_index >= token_count || tokens[name_index].token.kind != C_TOKEN_IDENTIFIER)
            {
                return false;
            }
            CMacro* macro = c_macro_find_token(first_macro, symbols, base, &tokens[name_index].token);
            CPpToken replacement = token;
            replacement.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
            replacement.token.punctuator = C_PUNCTUATOR_NONE;
            replacement.token.offset = macro && macro->definition.defined ? C_SPELLING_ONE : C_SPELLING_ZERO;
            replacement.token.length = 1;
            replacement.token.symbol = 0;
            transformed[transformed_count++] = replacement;
            token_index = name_index;
            if (parenthesized)
            {
                if (token_index + 1 >= token_count || !c_token_is_punctuator(&tokens[token_index + 1].token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
                {
                    return false;
                }
                token_index += 1;
            }
            continue;
        }
        transformed[transformed_count++] = token;
    }
    CPreprocessTokenNode* first_expanded = 0;
    CPreprocessTokenNode* last_expanded = 0;
    u64 expanded_count = 0;
    if (!c_preprocess_expand(arena, space, symbols, first_macro, transformed, transformed_count, &first_expanded, &last_expanded, &expanded_count, expansion_limit, result))
    {
        return false;
    }
    if (!c_conditional_feature_operators(arena, space, symbols, first_macro, first_expanded, options, including_path, including_origin))
    {
        return false;
    }
    u64* values = arena_allocate(arena, u64, expanded_count + 1);
    CConditionalOperator* operations = arena_allocate(arena, CConditionalOperator, expanded_count + 1);
    u32 value_count = 0;
    u32 operation_count = 0;
    bool expect_operand = true;
    for (CPreprocessTokenNode* node = first_expanded; node; node = node->next)
    {
        CToken token = node->token.token;
        if (token.kind == C_TOKEN_PREPROCESSING_NUMBER)
        {
            u64 value = 0;
            if (!expect_operand || !c_conditional_number(c_token_spelling(base, token), &value))
            {
                return false;
            }
            values[value_count++] = value;
            expect_operand = false;
            continue;
        }
        if (token.kind == C_TOKEN_CHARACTER_LITERAL)
        {
            u64 character = 0;
            CTypeKind character_kind = C_TYPE_INVALID;
            if (!expect_operand || !c_ir_decode_character_value(arena, base, token, result->target, &character, &character_kind))
            {
                return false;
            }
            (void)character_kind;
            values[value_count++] = character;
            expect_operand = false;
            continue;
        }
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            if (!expect_operand)
            {
                return false;
            }
            values[value_count++] = c_preprocess_dialect_is_c23(result->dialect) && c_token_spelling_equal(base, token, S8("true"));
            expect_operand = false;
            continue;
        }
        if (token.kind != C_TOKEN_PUNCTUATOR)
        {
            return false;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            if (!expect_operand)
            {
                return false;
            }
            operations[operation_count++] = C_CONDITIONAL_OPEN;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
        {
            if (expect_operand)
            {
                return false;
            }
            while (operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
            {
                if (!c_conditional_apply(operations[--operation_count], values, &value_count))
                {
                    return false;
                }
            }
            if (!operation_count)
            {
                return false;
            }
            operation_count -= 1;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_QUESTION))
        {
            if (expect_operand)
            {
                return false;
            }
            u32 precedence = c_conditional_precedence(C_CONDITIONAL_QUESTION);
            while (operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN &&
                   c_conditional_precedence(operations[operation_count - 1]) > precedence)
            {
                if (!c_conditional_apply(operations[--operation_count], values, &value_count))
                {
                    return false;
                }
            }
            operations[operation_count++] = C_CONDITIONAL_QUESTION;
            expect_operand = true;
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_COLON))
        {
            if (expect_operand)
            {
                return false;
            }
            while (operation_count && operations[operation_count - 1] != C_CONDITIONAL_QUESTION)
            {
                CConditionalOperator previous = operations[--operation_count];
                if (previous == C_CONDITIONAL_OPEN || !c_conditional_apply(previous, values, &value_count))
                {
                    return false;
                }
            }
            if (!operation_count)
            {
                return false;
            }
            operations[operation_count - 1] = C_CONDITIONAL_SELECT;
            expect_operand = true;
            continue;
        }
        CConditionalOperator operation = C_CONDITIONAL_OPERATOR_COUNT;
        if (!c_conditional_operator(token, expect_operand, &operation))
        {
            return false;
        }
        bool unary = c_conditional_is_unary(operation);
        if (expect_operand != unary)
        {
            return false;
        }
        u32 precedence = c_conditional_precedence(operation);
        while (operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
        {
            CConditionalOperator previous = operations[operation_count - 1];
            u32 previous_precedence = c_conditional_precedence(previous);
            if (previous_precedence < precedence || (unary && previous_precedence == precedence))
            {
                break;
            }
            operation_count -= 1;
            if (!c_conditional_apply(previous, values, &value_count))
            {
                return false;
            }
        }
        operations[operation_count++] = operation;
        expect_operand = true;
    }
    if (expect_operand)
    {
        return false;
    }
    while (operation_count)
    {
        CConditionalOperator operation = operations[--operation_count];
        if (operation == C_CONDITIONAL_OPEN || operation == C_CONDITIONAL_QUESTION || !c_conditional_apply(operation, values, &value_count))
        {
            return false;
        }
    }
    if (value_count != 1)
    {
        return false;
    }
    *value_out = values[0];
    return true;
}

// The parse-side entry point: no macro table, so expansion is a pass-through
// and nothing ever allocates spelling bytes — the read-only space view over
// the finished result is enough, and any allocation attempt fails loudly.
BUSTER_C_SHARED bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                       CPreprocessResult* result, u64* value_out)
{
    CSpellingSpace view = {
        .base = (char8*)spelling_base,
    };
    CPpToken* wrapped = arena_allocate(arena, CPpToken, token_count);
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        wrapped[token_index] = (CPpToken){
            .token = tokens[token_index],
        };
    }
    return c_integer_expression_evaluate_with_features(arena, &view, 0, 0, wrapped, token_count, expansion_limit, result, 0, (String8){0},
                                                       (CIncludeSearchOrigin){0}, value_out);
}

BUSTER_C_INTERNAL bool c_conditional_evaluate(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro, CPpToken* tokens,
                                                u32 token_count, u32 expansion_limit, CPreprocessResult* result, CPreprocessOptions options,
                                                String8 including_path, CIncludeSearchOrigin including_origin, bool* value_out)
{
    u64 value = 0;
    bool valid =
        c_integer_expression_evaluate_with_features(arena, space, symbols, first_macro, tokens, token_count, expansion_limit, result, &options, including_path,
                                                    including_origin, &value);
    *value_out = value != 0;
    return valid;
}

typedef struct CConditionalFrame CConditionalFrame;
struct CConditionalFrame
{
    CConditionalFrame* previous;
    CSourceLocation location;
    bool parent_active;
    bool active;
    bool branch_taken;
    bool else_seen;
};

BUSTER_C_INTERNAL bool c_preprocess_is_active(CConditionalFrame* conditional)
{
    return !conditional || conditional->active;
}

BUSTER_C_INTERNAL u64 c_preprocess_line_end(CLexResult lex, u64 token_index)
{
    while (token_index < lex.token_count && lex.tokens[token_index].kind != C_TOKEN_NEWLINE && lex.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
    {
        token_index += 1;
    }
    return token_index;
}

typedef struct CPreprocessSourceFrame CPreprocessSourceFrame;
struct CPreprocessSourceFrame
{
    CPreprocessSourceFrame* previous;
    CConditionalFrame* conditional_base;
    CLexResult lex;
    String8 path;
    String8 logical_path;
    s64 line_delta;
    u64 token_index;
    // Index of the frame's current source-map entry (the file span or the
    // latest #line split); its file id is assigned lazily when the region
    // first stages a token, so files that contribute nothing never enter
    // the file table — include resolution differs between hosts (the
    // self-hosted stages lack the clang resource headers), and an eagerly
    // registered but token-free header would break the fixed point.
    u32 map_entry;
    u32 depth;
    bool line_start;
    FileMapRead source_map;
    CIncludeSearchOrigin include_origin;
};

typedef struct CPragmaPackStack CPragmaPackStack;
struct CPragmaPackStack
{
    CPragmaPackStack* previous;
    u16 alignment;
};

// The pack change list under construction. Entries are appended lazily at
// output-append time — the first token that lands after a state change
// carries the new value's span start — so pragmas on directive lines and
// _Pragma markers mid-expansion record through the same comparison, and
// consecutive changes with no token between them collapse to one entry.
typedef struct CPackAlignmentRecorder CPackAlignmentRecorder;
struct CPackAlignmentRecorder
{
    Arena* arena;
    CPackAlignment* changes;
    u32 count;
    u32 capacity;
    u16 recorded;
};

BUSTER_C_INTERNAL void c_pack_alignment_record(CPackAlignmentRecorder* recorder, u64 token_index, u16 alignment)
{
    if (alignment == recorder->recorded)
    {
        return;
    }
    if (recorder->count == recorder->capacity)
    {
        u32 capacity = recorder->capacity ? recorder->capacity * 2 : 16;
        CPackAlignment* changes = arena_allocate(recorder->arena, CPackAlignment, capacity);
        if (recorder->count)
        {
            memcpy(changes, recorder->changes, sizeof(*changes) * recorder->count);
        }
        recorder->changes = changes;
        recorder->capacity = capacity;
    }
    recorder->changes[recorder->count++] = (CPackAlignment){
        .token_index = (u32)token_index,
        .alignment = alignment,
    };
    recorder->recorded = alignment;
}

u32 c_preprocess_pack_alignment(CPreprocessResult const* preprocess, u64 token_index)
{
    u32 low = 0;
    u32 high = preprocess->pack_change_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (preprocess->pack_changes[middle].token_index <= token_index)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low ? preprocess->pack_changes[low - 1].alignment : 0;
}

typedef struct CPreprocessPragmaContext CPreprocessPragmaContext;
struct CPreprocessPragmaContext
{
    Arena* arena;
    CSymbolTable* symbols;
    CMacro** first_macro;
    CMacro** last_macro;
    CMacroPushMacro** macro_push_stack;
    CPragmaPackStack** pack_stack;
    u16* pack_alignment;
    CPackAlignmentRecorder* pack_changes;
    String8* once_paths;
    u32* once_path_count;
    u32 once_path_capacity;
    String8 current_path;
};

BUSTER_C_INTERNAL bool c_preprocess_pragma_macro_name(char8 const* base, CToken* tokens, u32 token_count, String8* name_out)
{
    if (token_count == 4 && tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_is_punctuator(&tokens[1], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
        tokens[2].kind == C_TOKEN_STRING_LITERAL && tokens[2].length >= 2 && c_token_is_punctuator(&tokens[3], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        String8 spelling = c_token_spelling(base, tokens[2]);
        *name_out = (String8){
            .pointer = spelling.pointer + 1,
            .length = spelling.length - 2,
        };
        return true;
    }
    return false;
}

BUSTER_C_INTERNAL void c_macro_push_definition(CPreprocessPragmaContext context, String8 name)
{
    CMacro* macro = c_macro_find(*context.first_macro, c_symbol_intern(context.symbols, name));
    CMacroPushMacro* entry = arena_allocate(context.arena, CMacroPushMacro, 1);
    *entry = (CMacroPushMacro){
        .previous = *context.macro_push_stack,
        .macro = macro,
        .name = string_duplicate_arena(context.arena, name, false),
        .definition = macro ? macro->definition : (CMacroDefinition){0},
    };
    if (macro && macro->definition.replacement_count)
    {
        entry->definition.replacement = arena_allocate(context.arena, CToken, macro->definition.replacement_count);
        memcpy(entry->definition.replacement, macro->definition.replacement,
               sizeof(*entry->definition.replacement) * macro->definition.replacement_count);
    }
    if (macro && macro->definition.parameter_count)
    {
        entry->definition.parameters = arena_allocate(context.arena, String8, macro->definition.parameter_count);
        memcpy(entry->definition.parameters, macro->definition.parameters,
               sizeof(*entry->definition.parameters) * macro->definition.parameter_count);
    }
    *context.macro_push_stack = entry;
}

BUSTER_C_INTERNAL void c_macro_clear_definition(CMacro* macro)
{
    if (!macro)
    {
        return;
    }
    macro->definition = (CMacroDefinition){0};
}

BUSTER_C_INTERNAL void c_macro_pop_definition(CPreprocessPragmaContext context, String8 name)
{
    CMacroPushMacro** cursor = context.macro_push_stack;
    while (*cursor && !string_equal((*cursor)->name, name))
    {
        cursor = &(*cursor)->previous;
    }
    if (!*cursor)
    {
        return;
    }
    CMacroPushMacro* entry = *cursor;
    *cursor = entry->previous;
    if (!entry->macro)
    {
        c_macro_clear_definition(c_macro_find(*context.first_macro, c_symbol_intern(context.symbols, name)));
        return;
    }
    CMacro* macro = entry->macro;
    macro->definition = entry->definition;
    macro->disabled = false;
}

// The bound is UINT16_MAX because CToken carries the alignment in a u16, and
// no ABI has a structure alignment that large to begin with.
BUSTER_C_INTERNAL bool c_preprocess_pragma_pack_value(char8 const* base, CToken token, u16* value_out)
{
    u64 value = 0;
    bool valid =
        token.kind == C_TOKEN_PREPROCESSING_NUMBER && c_conditional_number(c_token_spelling(base, token), &value) && value <= UINT16_MAX && value && !(value & (value - 1));
    if (valid)
    {
        *value_out = (u16)value;
    }
    return valid;
}

BUSTER_C_INTERNAL void c_preprocess_pragma_pack(CPreprocessPragmaContext context, char8 const* base, CToken* tokens, u32 token_count)
{
    if (token_count < 3 || tokens[0].kind != C_TOKEN_IDENTIFIER || !c_token_spelling_equal(base, tokens[0], S8("pack")) ||
        !c_token_is_punctuator(&tokens[1], C_PUNCTUATOR_LEFT_PARENTHESIS) || !c_token_is_punctuator(&tokens[token_count - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS))
    {
        return;
    }
    CToken* arguments = tokens + 2;
    u32 argument_count = token_count - 3;
    if (!argument_count)
    {
        *context.pack_alignment = 0;
        return;
    }
    bool push = argument_count >= 1 && arguments[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, arguments[0], S8("push"));
    bool pop = argument_count >= 1 && arguments[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, arguments[0], S8("pop"));
    u32 value_index = UINT32_MAX;
    if (push)
    {
        if (argument_count == 1)
        {
            value_index = UINT32_MAX;
        }
        else if (argument_count == 3 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA))
        {
            value_index = 2;
        }
        else if (argument_count == 5 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA) && arguments[2].kind == C_TOKEN_IDENTIFIER &&
                 c_token_is_punctuator(&arguments[3], C_PUNCTUATOR_COMMA))
        {
            value_index = 4;
        }
        else
        {
            return;
        }
        u16 value = 0;
        bool valid_value = value_index != UINT32_MAX && c_preprocess_pragma_pack_value(base, arguments[value_index], &value);
        if (value_index != UINT32_MAX && !valid_value)
        {
            return;
        }
        CPragmaPackStack* entry = arena_allocate(context.arena, CPragmaPackStack, 1);
        *entry = (CPragmaPackStack){
            .previous = *context.pack_stack,
            .alignment = *context.pack_alignment,
        };
        *context.pack_stack = entry;
        if (valid_value)
        {
            *context.pack_alignment = value;
        }
        return;
    }
    if (pop)
    {
        if (argument_count != 1 &&
            !(argument_count == 3 && c_token_is_punctuator(&arguments[1], C_PUNCTUATOR_COMMA) && arguments[2].kind == C_TOKEN_IDENTIFIER))
        {
            return;
        }
        if (*context.pack_stack)
        {
            *context.pack_alignment = (*context.pack_stack)->alignment;
            *context.pack_stack = (*context.pack_stack)->previous;
        }
        return;
    }
    if (argument_count == 1)
    {
        u16 value = 0;
        if (c_preprocess_pragma_pack_value(base, arguments[0], &value))
        {
            *context.pack_alignment = value;
        }
    }
}

BUSTER_C_INTERNAL void c_preprocess_handle_pragma(CPreprocessPragmaContext context, char8 const* base, CToken* tokens, u32 token_count)
{
    if (!token_count)
    {
        return;
    }
    if (token_count == 1 && tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("once")))
    {
        bool found = false;
        for (u32 index = 0; index < *context.once_path_count; index += 1)
        {
            found |= string_equal(context.once_paths[index], context.current_path);
        }
        if (!found && *context.once_path_count < context.once_path_capacity)
        {
            u32 once_path_index = *context.once_path_count;
            context.once_paths[once_path_index] = context.current_path;
            *context.once_path_count = once_path_index + 1;
        }
        return;
    }
    if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("push_macro")))
    {
        String8 name = {0};
        if (c_preprocess_pragma_macro_name(base, tokens, token_count, &name))
        {
            c_macro_push_definition(context, name);
        }
        return;
    }
    if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("pop_macro")))
    {
        String8 name = {0};
        if (c_preprocess_pragma_macro_name(base, tokens, token_count, &name))
        {
            c_macro_pop_definition(context, name);
        }
        return;
    }
    if (tokens[0].kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, tokens[0], S8("pack")))
    {
        c_preprocess_pragma_pack(context, base, tokens, token_count);
    }
    // GCC/Clang/MSVC diagnostic, visibility, system-header, warning,
    // comment, region, and OpenMP pragmas are compatibility no-ops. Unknown
    // pragma bodies are intentionally ignored as well.
}

BUSTER_C_INTERNAL void c_preprocess_pragma_marker(CPreprocessPragmaContext context, char8 const* base, CToken marker)
{
    if (!marker.length)
    {
        return;
    }
    // The marker relex is standalone: its tokens' offsets are relative to its
    // own translated copy, so the handler reads them through that base.
    CLexResult lex = c_lex(context.arena, c_token_spelling(base, marker));
    u32 token_count = 0;
    while (token_count < lex.token_count && lex.tokens[token_count].kind != C_TOKEN_NEWLINE && lex.tokens[token_count].kind != C_TOKEN_END_OF_FILE)
    {
        token_count += 1;
    }
    c_preprocess_handle_pragma(context, lex.spelling_base, lex.tokens, token_count);
}

// Materialize an expanded line into the output stream. Foreign tokens (their
// location is a macro-invocation stamp, not a function of their spelling
// offset) get their spellings copied to fresh space offsets under an
// expansion source-map entry; one invocation's tokens share one stamped
// location, so runs of equal locations share one entry.
BUSTER_C_INTERNAL void c_preprocess_process_expanded_line(Arena* arena, CPreprocessPragmaContext context, CSpellingSpace* space, CSourceMap* map,
                                                            CPreprocessTokenNode* first_line, u64 line_output_count,
                                                            CPreprocessTokenRange** first_output_range, CPreprocessTokenRange** last_output_range,
                                                            u64* output_count)
{
    CToken* tokens = arena_allocate(arena, CToken, line_output_count);
    u64 count = 0;
    u64 foreign_length = 0;
    for (CPreprocessTokenNode* node = first_line; node; node = node->next)
    {
        foreign_length += node->token.foreign ? c_token_length(space->base, node->token.token) : 0;
    }
    char8* copy = foreign_length ? c_space_allocate(space, foreign_length) : 0;
    bool run_open = false;
    CSourceLocation run_location = {0};
    for (CPreprocessTokenNode* node = first_line; node; node = node->next)
    {
        if (node->token.token.kind == C_TOKEN_PRAGMA)
        {
            c_preprocess_pragma_marker(context, space->base, node->token.token);
            continue;
        }
        CPpToken item = node->token;
        c_pack_alignment_record(context.pack_changes, *output_count + count, *context.pack_alignment);
        if (item.foreign)
        {
            String8 spelling = c_token_spelling(space->base, item.token);
            for (u64 byte_index = 0; byte_index < spelling.length; byte_index += 1)
            {
                copy[byte_index] = spelling.pointer[byte_index];
            }
            if (!run_open || !c_source_location_equal(run_location, item.location))
            {
                c_source_map_append(map, (IrSourceRegion){
                                             .start = c_space_offset(space, copy),
                                             .source = item.location.file,
                                             .stamp = c_position_from_source_location(item.location),
                                             .kind = IR_SOURCE_REGION_STAMP,
                                         });
                run_open = true;
                run_location = item.location;
            }
            item.token.offset = c_space_offset(space, copy);
            copy += spelling.length;
        }
        else
        {
            run_open = false;
        }
        tokens[count] = item.token;
        count += 1;
    }
    if (count)
    {
        c_preprocess_output_append_range(arena, first_output_range, last_output_range, tokens, count);
        *output_count += count;
    }
}

BUSTER_C_INTERNAL u32 c_preprocess_tokens_from_nodes(CPreprocessTokenNode* first, Arena* arena, CToken** tokens_out)
{
    u32 token_count = 0;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        token_count += node->token.token.kind != C_TOKEN_PRAGMA;
    }
    CToken* tokens = arena_allocate(arena, CToken, token_count);
    u32 output = 0;
    for (CPreprocessTokenNode* node = first; node; node = node->next)
    {
        if (node->token.token.kind != C_TOKEN_PRAGMA)
        {
            tokens[output++] = node->token.token;
        }
    }
    *tokens_out = tokens;
    return token_count;
}

BUSTER_C_INTERNAL String8 c_preprocess_message_from_tokens(Arena* arena, char8 const* base, CToken* tokens, u32 token_count)
{
    u64 length = 0;
    u32 message_token_count = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        if (tokens[token_index].kind != C_TOKEN_PRAGMA)
        {
            length += c_token_length(base, tokens[token_index]);
            message_token_count += 1;
        }
    }
    if (!message_token_count)
    {
        return (String8){0};
    }
    length += message_token_count - 1;
    char8* message = arena_allocate(arena, char8, length + 1);
    u64 output = 0;
    u64 emitted = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        CToken token = tokens[token_index];
        if (token.kind == C_TOKEN_PRAGMA)
        {
            continue;
        }
        if (emitted++)
        {
            message[output++] = ' ';
        }
        String8 spelling = c_token_spelling(base, token);
        if (spelling.length)
        {
            memcpy(message + output, spelling.pointer, spelling.length);
            output += spelling.length;
        }
    }
    message[output] = 0;
    return (String8){
        .pointer = message,
        .length = output,
    };
}

BUSTER_C_INTERNAL CSourceLocation c_preprocess_logical_location(CPreprocessSourceFrame* frame, CSourceLocation location);

// Wrap a frame's lex tokens for the expansion machinery (directive lines
// only; text lines wrap during staging where the file id is also stamped).
// Directive-line expansion output is transient — only diagnostics ever read
// these locations — so one recovery of the line head serves every token.
BUSTER_C_INTERNAL CPpToken* c_frame_wrap_tokens(Arena* arena, CPreprocessSourceFrame* frame, u64 start, u64 end)
{
    CPpToken* wrapped = arena_allocate(arena, CPpToken, end - start);
    CSourceLocation location = {0};
    if (start < end)
    {
        location = c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, frame->lex.tokens[start]));
    }
    for (u64 index = start; index < end; index += 1)
    {
        wrapped[index - start] = (CPpToken){
            .token = frame->lex.tokens[index],
            .location = location,
        };
    }
    return wrapped;
}

BUSTER_C_INTERNAL CSourceLocation c_preprocess_logical_location(CPreprocessSourceFrame* frame, CSourceLocation location)
{
    s64 line = (s64)location.line + frame->line_delta;
    if (line < 1)
    {
        line = 1;
    }
    if (line > (s64)UINT32_MAX)
    {
        line = (s64)UINT32_MAX;
    }
    location.line = (u32)line;
    return location;
}

// __LINE__'s value, recovered from the main loop's two-store breadcrumb only
// when the macro actually expands.
BUSTER_C_INTERNAL u32 c_preprocess_builtin_line(CMacro* first)
{
    CPreprocessSourceFrame* frame = first->builtin_frame;
    u32 result;
    if (!frame)
    {
        result = first->builtin_line;
    }
    else
    {
        CToken probe = {
            .offset = first->builtin_token_offset,
        };
        result = c_preprocess_logical_location(frame, c_lex_token_location(&frame->lex, probe)).line;
    }

    return result;
}

typedef struct CPreprocessFileTable CPreprocessFileTable;
struct CPreprocessFileTable
{
    String8* files;
    String8 memo_path;
    u32 memo_index;
    u32 count;
    u32 capacity;
    u32 reserved;
};

BUSTER_C_INTERNAL u32 c_preprocess_file_index(Arena* arena, CPreprocessFileTable* table, String8 path)
{
    u32 result;
    if (table->count && table->memo_path.pointer == path.pointer && table->memo_path.length == path.length)
    {
        result = table->memo_index;
    }
    else
    {
        u32 index = table->count;
        for (u32 existing = 0; existing < table->count; existing += 1)
        {
            if (string_equal(table->files[existing], path))
            {
                index = existing;
                break;
            }
        }
        if (index == table->count)
        {
            if (table->count == table->capacity)
            {
                u32 capacity = table->capacity ? table->capacity * 2 : 16;
                String8* files = arena_allocate(arena, String8, capacity);
                if (table->count)
                {
                    memcpy(files, table->files, table->count * sizeof(*files));
                }
                table->files = files;
                table->capacity = capacity;
            }
            table->files[table->count++] = path;
        }
        table->memo_path = path;
        table->memo_index = index;
        result = index;
    }

    return result;
}

BUSTER_C_INTERNAL String8 c_path_directory(String8 path)
{
    for (u64 index = path.length; index; index -= 1)
    {
        char8 character = path.pointer[index - 1];
        if (character == '/' || character == '\\')
        {
            return (String8){
                .pointer = path.pointer,
                .length = index - 1,
            };
        }
    }
    return S8(".");
}

BUSTER_C_INTERNAL bool c_path_is_absolute(String8 path)
{
    return path.length && (path.pointer[0] == '/' || path.pointer[0] == '\\' || (path.length >= 2 && c_ascii_alpha(path.pointer[0]) && path.pointer[1] == ':'));
}

BUSTER_C_INTERNAL bool c_include_read(Arena* arena, String8 directory, String8 name, String8* path_out, String8* source_out, FileMapRead* map_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    String8 path = c_path_is_absolute(name) ? string_format_z(arena, S8("{S8}"), name) : string_format_z(arena, S8("{S8}/{S8}"), directory, name);
    FileMapRead map = file_map_read(arena, path, (FileReadOptions){0});
    ByteSlice bytes = map.bytes;
    bool result;
    if (!bytes.pointer)
    {
        result = false;
    }
    else
    {
        *path_out = path;
        *source_out = BYTE_SLICE_TO_STRING(8, bytes);
        if (map_out)
        {
            *map_out = map;
        }
        result = true;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_include_builtin(String8 name, String8* path_out, String8* source_out)
{
    String8 source = {0};
    if (string_equal(name, S8("stdbool.h")))
    {
        source = S8("#ifndef __STDBOOL_H\n"
                    "#define __STDBOOL_H\n"
                    "#if __STDC_VERSION__ < 202311L\n"
                    "#define bool _Bool\n"
                    "#define true 1\n"
                    "#define false 0\n"
                    "#endif\n"
                    "#define __bool_true_false_are_defined 1\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stdalign.h")))
    {
        source = S8("#ifndef __STDALIGN_H\n"
                    "#define __STDALIGN_H\n"
                    "#if __STDC_VERSION__ < 202311L\n"
                    "#define alignas _Alignas\n"
                    "#define alignof _Alignof\n"
                    "#endif\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stdarg.h")))
    {
        source = S8("#ifndef __STDARG_H\n"
                    "#define __STDARG_H\n"
                    "typedef __builtin_va_list __gnuc_va_list;\n"
                    "typedef __gnuc_va_list va_list;\n"
                    "#define va_start(arguments, last) "
                    "__builtin_va_start(arguments, last)\n"
                    "#define va_end(arguments) "
                    "__builtin_va_end(arguments)\n"
                    "#define va_arg(arguments, type) "
                    "__builtin_va_arg(arguments, type)\n"
                    "#define va_copy(destination, source) "
                    "__builtin_va_copy(destination, source)\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("stddef.h")))
    {
        source = S8("#ifndef __BUSTER_PTRDIFF_T\n"
                    "#define __BUSTER_PTRDIFF_T\n"
                    "typedef __PTRDIFF_TYPE__ ptrdiff_t;\n"
                    "#endif\n"
                    "#ifndef __BUSTER_SIZE_T\n"
                    "#define __BUSTER_SIZE_T\n"
                    "typedef __SIZE_TYPE__ size_t;\n"
                    "#endif\n"
                    "#ifndef __BUSTER_WCHAR_T\n"
                    "#define __BUSTER_WCHAR_T\n"
                    "typedef __WCHAR_TYPE__ wchar_t;\n"
                    "#endif\n"
                    "#ifndef __BUSTER_MAX_ALIGN_T\n"
                    "#define __BUSTER_MAX_ALIGN_T\n"
                    "typedef union {\n"
                    "    long long integer;\n"
                    "    long double real;\n"
                    "} max_align_t;\n"
                    "#endif\n"
                    "#if __STDC_VERSION__ >= 202311L\n"
                    "typedef typeof(nullptr) nullptr_t;\n"
                    "#endif\n"
                    "#ifndef NULL\n"
                    "#define NULL ((void *)0)\n"
                    "#endif\n"
                    "#define offsetof(type, member) "
                    "__builtin_offsetof(type, member)\n"
                    "#undef __need_ptrdiff_t\n"
                    "#undef __need_size_t\n"
                    "#undef __need_rsize_t\n"
                    "#undef __need_wchar_t\n"
                    "#undef __need_NULL\n"
                    "#undef __need_max_align_t\n"
                    "#undef __need_offsetof\n");
    }
    else if (string_equal(name, S8("limits.h")))
    {
        source = S8("#ifndef __LIMITS_H\n"
                    "#define __LIMITS_H\n"
                    "#define CHAR_BIT 8\n"
                    "#define SCHAR_MIN (-128)\n"
                    "#define SCHAR_MAX 127\n"
                    "#define UCHAR_MAX 255\n"
                    "#define CHAR_MIN 0\n"
                    "#define CHAR_MAX UCHAR_MAX\n"
                    "#define SHRT_MIN (-32768)\n"
                    "#define SHRT_MAX 32767\n"
                    "#define USHRT_MAX 65535\n"
                    "#define INT_MIN (-2147483647 - 1)\n"
                    "#define INT_MAX 2147483647\n"
                    "#define UINT_MAX 4294967295U\n"
                    "#if defined(_WIN64)\n"
                    "#define LONG_MIN INT_MIN\n"
                    "#define LONG_MAX INT_MAX\n"
                    "#define ULONG_MAX UINT_MAX\n"
                    "#else\n"
                    "#define LONG_MIN (-9223372036854775807L - 1)\n"
                    "#define LONG_MAX 9223372036854775807L\n"
                    "#define ULONG_MAX 18446744073709551615UL\n"
                    "#endif\n"
                    "#define LLONG_MIN "
                    "(-9223372036854775807LL - 1)\n"
                    "#define LLONG_MAX 9223372036854775807LL\n"
                    "#define ULLONG_MAX 18446744073709551615ULL\n"
                    "#define MB_LEN_MAX 16\n"
                    "#endif\n");
    }
    else if (string_equal(name, S8("buster_test_builtin_include_next.h")))
    {
        source = S8("#include_next <buster_test_builtin_include_next.h>\n");
    }
    bool result;
    if (!source.length)
    {
        result = false;
    }
    else
    {
        *path_out = name;
        *source_out = source;
        result = true;
    }

    return result;
}

BUSTER_C_INTERNAL bool c_include_resolve(Arena* arena, CPreprocessOptions options, String8 including_path, String8 name, bool quoted, String8* path_out,
                                           String8* source_out, FileMapRead* map_out, CIncludeSearchOrigin* origin_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    if (origin_out)
    {
        *origin_out = (CIncludeSearchOrigin){0};
    }
    if (options.disable_external_includes)
    {
        if (c_include_builtin(name, path_out, source_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
            }
            return true;
        }
        return false;
    }
    if (c_path_is_absolute(name))
    {
        return c_include_read(arena, S8("."), name, path_out, source_out, map_out);
    }
    if (quoted && c_include_read(arena, c_path_directory(including_path), name, path_out, source_out, map_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_QUOTED};
        }
        return true;
    }
    for (u32 index = 0; index < options.include_path_count; index += 1)
    {
        if (c_include_read(arena, options.include_paths[index], name, path_out, source_out, map_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_INCLUDE_PATH, .index = index};
            }
            return true;
        }
    }
    if (c_include_builtin(name, path_out, source_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
        }
        return true;
    }
    for (u32 index = 0; index < options.system_include_path_count; index += 1)
    {
        if (c_include_read(arena, options.system_include_paths[index], name, path_out, source_out, map_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_SYSTEM_PATH, .index = index};
            }
            return true;
        }
    }
    return false;
}

BUSTER_C_INTERNAL bool c_include_resolve_next(Arena* arena, CPreprocessOptions options, String8 name, String8* path_out,
                                                String8* source_out, FileMapRead* map_out, CIncludeSearchOrigin origin,
                                                CIncludeSearchOrigin* origin_out)
{
    if (map_out)
    {
        *map_out = (FileMapRead){0};
    }
    if (origin_out)
    {
        *origin_out = (CIncludeSearchOrigin){0};
    }
    if (options.disable_external_includes)
    {
        if (c_include_builtin(name, path_out, source_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
            }
            return true;
        }
        return false;
    }
    u32 include_start = 0;
    u32 system_start = 0;
    if (origin.kind == C_INCLUDE_SEARCH_INCLUDE_PATH)
    {
        include_start = origin.index < options.include_path_count ? origin.index + 1 : options.include_path_count;
    }
    else if (origin.kind == C_INCLUDE_SEARCH_SYSTEM_PATH)
    {
        include_start = options.include_path_count;
        system_start = origin.index < options.system_include_path_count ? origin.index + 1 : options.system_include_path_count;
    }
    else if (origin.kind == C_INCLUDE_SEARCH_BUILTIN)
    {
        include_start = options.include_path_count;
    }
    for (u32 index = include_start; index < options.include_path_count; index += 1)
    {
        if (c_include_read(arena, options.include_paths[index], name, path_out, source_out, map_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_INCLUDE_PATH, .index = index};
            }
            return true;
        }
    }
    if (origin.kind != C_INCLUDE_SEARCH_SYSTEM_PATH && origin.kind != C_INCLUDE_SEARCH_BUILTIN && c_include_builtin(name, path_out, source_out))
    {
        if (origin_out)
        {
            *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_BUILTIN};
        }
        return true;
    }
    for (u32 index = system_start; index < options.system_include_path_count; index += 1)
    {
        if (c_include_read(arena, options.system_include_paths[index], name, path_out, source_out, map_out))
        {
            if (origin_out)
            {
                *origin_out = (CIncludeSearchOrigin){.kind = C_INCLUDE_SEARCH_SYSTEM_PATH, .index = index};
            }
            return true;
        }
    }
    return false;
}

BUSTER_C_INTERNAL bool c_include_name(Arena* arena, char8 const* base, CToken* tokens, u32 token_count, String8* name_out, bool* quoted_out)
{
    if (token_count == 1 && tokens[0].kind == C_TOKEN_STRING_LITERAL && tokens[0].length >= 2)
    {
        String8 spelling = c_token_spelling(base, tokens[0]);
        *name_out = (String8){
            .pointer = spelling.pointer + 1,
            .length = spelling.length - 2,
        };
        *quoted_out = true;
        return true;
    }
    if (token_count >= 3 && c_token_is_punctuator(&tokens[0], C_PUNCTUATOR_LESS) && c_token_is_punctuator(&tokens[token_count - 1], C_PUNCTUATOR_GREATER))
    {
        u64 length = 0;
        for (u32 index = 1; index + 1 < token_count; index += 1)
        {
            length += c_token_length(base, tokens[index]);
        }
        char8* name = arena_allocate(arena, char8, length + 1);
        u64 output = 0;
        for (u32 index = 1; index + 1 < token_count; index += 1)
        {
            String8 spelling = c_token_spelling(base, tokens[index]);
            memcpy(name + output, spelling.pointer, spelling.length);
            output += spelling.length;
        }
        name[output] = 0;
        *name_out = (String8){
            .pointer = name,
            .length = output,
        };
        *quoted_out = false;
        return true;
    }
    return false;
}

BUSTER_C_INTERNAL void c_preprocess_command_definitions(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CPreprocessOptions options,
                                                          CMacro** first_macro, CMacro** last_macro)
{
    for (u32 definition_index = 0; definition_index < options.definition_count; definition_index += 1)
    {
        CPreprocessorDefinition definition = options.definitions[definition_index];
        String8 value = definition.value.length ? definition.value : S8("1");
        CLexResult lex = c_lex_space(arena, space, value);
        c_symbols_intern_tokens(symbols, lex.spelling_base, lex.tokens, lex.token_count);
        u32 replacement_count = 0;
        for (u64 token_index = 0; token_index < lex.token_count; token_index += 1)
        {
            CTokenKind kind = lex.tokens[token_index].kind;
            replacement_count += kind != C_TOKEN_END_OF_FILE && kind != C_TOKEN_NEWLINE;
        }
        CToken* replacement = arena_allocate(arena, CToken, replacement_count);
        u32 replacement_index = 0;
        for (u64 token_index = 0; token_index < lex.token_count; token_index += 1)
        {
            CToken token = lex.tokens[token_index];
            if (token.kind != C_TOKEN_END_OF_FILE && token.kind != C_TOKEN_NEWLINE)
            {
                replacement[replacement_index++] = token;
            }
        }
        c_macro_define(arena, symbols, first_macro, last_macro, definition.name, replacement, replacement_count, 0, 0, false, false);
    }
    for (u32 undefinition_index = 0; undefinition_index < options.undefinition_count; undefinition_index += 1)
    {
        CMacro* macro = c_macro_find(*first_macro, c_symbol_intern(symbols, options.undefinitions[undefinition_index]));
        if (macro)
        {
            macro->definition.defined = false;
        }
    }
}

// __LINE__ and __FILE__ are defined once as builtin-kind macros; the main
// preprocess loop refreshes the head-of-list line/path state each iteration
// and c_macro_replacement_tokens materializes the value only on expansion.
BUSTER_C_INTERNAL void c_preprocess_builtins(Arena* arena, CSymbolTable* symbols, CMacro** first_macro, CMacro** last_macro, String8 path, CSourceLocation location)
{
    CMacro* line_macro = c_macro_define(arena, symbols, first_macro, last_macro, S8("__LINE__"), 0, 0, 0, 0, false, false);
    line_macro->builtin = C_MACRO_BUILTIN_LINE;
    CMacro* file_macro = c_macro_define(arena, symbols, first_macro, last_macro, S8("__FILE__"), 0, 0, 0, 0, false, false);
    file_macro->builtin = C_MACRO_BUILTIN_FILE;
    (*first_macro)->builtin_line = location.line;
    (*first_macro)->builtin_path = path;
}

BUSTER_C_INTERNAL void c_preprocess_define_directive(Arena* arena, CSymbolTable* symbols, CLexResult lex, u64* token_index, CMacro** first_macro, CMacro** last_macro,
                                                       CPreprocessResult* result, CSourceLocation directive_location)
{
    if (*token_index >= lex.token_count || lex.tokens[*token_index].kind != C_TOKEN_IDENTIFIER)
    {
        c_preprocess_diagnostic_push(arena, result, directive_location, C_DIAGNOSTIC_EXPECTED_MACRO_NAME, S8("expected macro name after '#define'"));
        return;
    }
    CToken name = lex.tokens[(*token_index)++];
    // Adjacency in translated offsets: a '(' that starts a parameter list
    // must follow the name with nothing between (a line splice deletes its
    // bytes in translation, which matches the standard's post-splice view).
    bool function_like = *token_index < lex.token_count && c_token_is_punctuator(&lex.tokens[*token_index], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                         lex.tokens[*token_index].offset == name.offset + name.length;
    String8* parameters = 0;
    u32 parameter_count = 0;
    bool variadic = false;
    bool valid = true;
    if (function_like)
    {
        *token_index += 1;
        u64 parameter_capacity = 0;
        while (*token_index + parameter_capacity < lex.token_count)
        {
            CToken token = lex.tokens[*token_index + parameter_capacity];
            if (token.kind == C_TOKEN_NEWLINE || token.kind == C_TOKEN_END_OF_FILE)
            {
                break;
            }
            parameter_capacity += 1;
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                break;
            }
        }
        parameters = arena_allocate(arena, String8, parameter_capacity);
        bool expect_parameter = true;
        while (*token_index < lex.token_count)
        {
            CToken token = lex.tokens[*token_index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                *token_index += 1;
                break;
            }
            if (!expect_parameter)
            {
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
                {
                    expect_parameter = true;
                    *token_index += 1;
                    continue;
                }
                valid = false;
                break;
            }
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_ELLIPSIS))
            {
                parameters[parameter_count++] = S8("__VA_ARGS__");
                variadic = true;
                expect_parameter = false;
                *token_index += 1;
                continue;
            }
            if (token.kind != C_TOKEN_IDENTIFIER)
            {
                valid = false;
                break;
            }
            parameters[parameter_count++] = c_token_spelling(lex.spelling_base, token);
            *token_index += 1;
            if (*token_index < lex.token_count && c_token_is_punctuator(&lex.tokens[*token_index], C_PUNCTUATOR_ELLIPSIS))
            {
                variadic = true;
                *token_index += 1;
            }
            expect_parameter = false;
        }
        if (*token_index > lex.token_count || (*token_index && !c_token_is_punctuator(&lex.tokens[*token_index - 1], C_PUNCTUATOR_RIGHT_PARENTHESIS)))
        {
            valid = false;
        }
    }
    u64 replacement_start = *token_index;
    while (*token_index < lex.token_count && lex.tokens[*token_index].kind != C_TOKEN_NEWLINE && lex.tokens[*token_index].kind != C_TOKEN_END_OF_FILE)
    {
        *token_index += 1;
    }
    if (!valid)
    {
        c_preprocess_diagnostic_push(arena, result, c_lex_token_location(&lex, name), C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
                                     S8("invalid function-like macro parameter list"));
        return;
    }
    u64 replacement_count = *token_index - replacement_start;
    CToken* replacement = arena_allocate(arena, CToken, replacement_count);
    for (u64 index = 0; index < replacement_count; index += 1)
    {
        replacement[index] = lex.tokens[replacement_start + index];
    }
    c_macro_define(arena, symbols, first_macro, last_macro, c_token_spelling(lex.spelling_base, name), replacement, (u32)replacement_count, parameters,
                   parameter_count, function_like, variadic);
}

bool c_preprocess_dialect_is_gnu(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_GNU11 || dialect == C_PREPROCESS_DIALECT_GNU17 || dialect == C_PREPROCESS_DIALECT_GNU23;
}

BUSTER_C_INTERNAL String8 c_preprocess_standard_version(CPreprocessDialect dialect)
{
    switch (dialect)
    {
    case C_PREPROCESS_DIALECT_GNU11:
    case C_PREPROCESS_DIALECT_C11:
        return S8("201112L");
    case C_PREPROCESS_DIALECT_GNU17:
    case C_PREPROCESS_DIALECT_C17:
        return S8("201710L");
    case C_PREPROCESS_DIALECT_GNU23:
    case C_PREPROCESS_DIALECT_C23:
        return S8("202311L");
    case C_PREPROCESS_DIALECT_COUNT:
        return (String8){0};
    }
    return (String8){0};
}

BUSTER_C_SHARED bool c_preprocess_dialect_is_c23(CPreprocessDialect dialect)
{
    return dialect == C_PREPROCESS_DIALECT_GNU23 || dialect == C_PREPROCESS_DIALECT_C23;
}

// C23 spells the underscore keywords through aliases; the respell runs at
// the end of preprocessing while the spelling space is still open, so each
// respelled token points at prelude bytes copied under an expansion map
// entry that preserves its recovered source location.
BUSTER_C_INTERNAL void c_preprocess_respell_c23(CSpellingSpace* space, CSourceMap* map, CPreprocessResult* result)
{
    if (!c_preprocess_dialect_is_c23(result->dialect))
    {
        return;
    }
    char8 const* base = space->base;
    IrSourceMapCursor cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
    for (u64 index = 0; index < result->token_count; index += 1)
    {
        CToken* token = result->tokens + index;
        if (token->kind != C_TOKEN_IDENTIFIER)
        {
            continue;
        }
        String8 spelling = c_token_spelling(base, *token);
        String8 respelled = {0};
        if (string_equal(spelling, S8("bool")))
        {
            respelled = S8("_Bool");
        }
        else if (string_equal(spelling, S8("alignas")))
        {
            respelled = S8("_Alignas");
        }
        else if (string_equal(spelling, S8("alignof")))
        {
            respelled = S8("_Alignof");
        }
        else if (string_equal(spelling, S8("static_assert")))
        {
            respelled = S8("_Static_assert");
        }
        else if (string_equal(spelling, S8("thread_local")))
        {
            respelled = S8("_Thread_local");
        }
        if (respelled.length)
        {
            CSourceLocation location = c_preprocess_token_location_cursor(result, *token, &cursor);
            CToken copy = c_space_token(space, respelled, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
            c_source_map_append(map, (IrSourceRegion){
                                         .start = copy.offset,
                                         .source = location.file,
                                         .stamp = c_position_from_source_location(location),
                                         .kind = IR_SOURCE_REGION_STAMP,
                                     });
            // Appends can move the region array; keep the result's view (the
            // recovery above reads through it) current. The new region is at
            // the tail, past every key already published, so the lookups keep
            // answering through the keys until the map is republished.
            result->recovery->map.regions = map->regions;
            token->offset = copy.offset;
            token->length = copy.length;
            // The symbol travels with the spelling: a respelled token must
            // re-intern or every symbol-keyed consumer would classify it as
            // the old name.
            token->symbol = result->symbols ? c_symbol_intern(result->symbols, respelled) : 0;
        }
    }
}

typedef struct CTargetFeatureMacro CTargetFeatureMacro;
struct CTargetFeatureMacro
{
    String8 name;
    TargetCpuFeature feature;
    CpuArch cpu_arch;
};

// Exactly the features `<buster/lib/simd.h>` tests, in the GNU spelling.  The
// list is deliberately short: every entry is a promise that the frontend and
// the canonical backend can compile code written behind it.
BUSTER_C_INTERNAL CTargetFeatureMacro const c_target_feature_macros[] = {
    { S8_INITIALIZER("__SSE2__"), TARGET_CPU_FEATURE_X86_SSE2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX__"), TARGET_CPU_FEATURE_X86_AVX, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX2__"), TARGET_CPU_FEATURE_X86_AVX2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512F__"), TARGET_CPU_FEATURE_X86_AVX512F, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512BW__"), TARGET_CPU_FEATURE_X86_AVX512BW, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VL__"), TARGET_CPU_FEATURE_X86_AVX512VL, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512DQ__"), TARGET_CPU_FEATURE_X86_AVX512DQ, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VBMI__"), TARGET_CPU_FEATURE_X86_AVX512VBMI, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__AVX512VBMI2__"), TARGET_CPU_FEATURE_X86_AVX512VBMI2, CPU_ARCH_X86_64 },
    { S8_INITIALIZER("__ARM_NEON"), TARGET_CPU_FEATURE_AARCH64_NEON, CPU_ARCH_AARCH64 },
};

CPreprocessResult c_preprocess(Arena* arena, String8 source, CPreprocessOptions options)
{
    if (options.dialect >= C_PREPROCESS_DIALECT_COUNT)
    {
        options.dialect = C_PREPROCESS_DIALECT_GNU17;
    }
    if (!target_data_layout_is_valid(options.data_layout))
    {
        options.data_layout = target_data_layout(options.target);
    }
    CPreprocessResult result = {
        .target = options.target,
        .data_layout = options.data_layout,
        .dialect = options.dialect,
    };
    if (!arena)
    {
        return result;
    }
    // The spelling space lives in its own commit-on-demand arena so its
    // offsets stay contiguous under one base without stealing reserve from
    // the caller's arena; the result carries the arena so a caller that
    // compiles many units can release it. The fixed prelude seeds the
    // well-known spellings, and its expansion entry gives synthesized-token
    // offsets the same all-zero location eagerly-built tokens used to carry.
    Arena* spelling_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_GB(1),
        .flags = {.pool_reuse = 1},
    });
    if (!spelling_arena)
    {
        return result;
    }
    CSpellingSpace space_storage = {
        .base = (char8*)spelling_arena + arena_minimum_position,
        .arena = spelling_arena,
    };
    CSpellingSpace* space = &space_storage;
    CSourceMapRecovery* recovery = arena_allocate(arena, CSourceMapRecovery, 1);
    *recovery = (CSourceMapRecovery){
        .spelling_arena = spelling_arena,
    };
    result.recovery = recovery;
    result.spelling_base = space->base;
    memcpy(c_space_allocate(space, C_SPELLING_PRELUDE_LENGTH), C_SPELLING_PRELUDE_TEXT, C_SPELLING_PRELUDE_LENGTH);
    CSourceMap map = {
        .arena = arena,
    };
    c_source_map_append(&map, (IrSourceRegion){
                                  .start = 0,
                                  .kind = IR_SOURCE_REGION_STAMP,
                              });
    CLexResult root_lex = c_lex_space(arena, space, source);
    CSourceMetricsFileSet metrics_files = {0};
    c_source_metrics_add(&result.source_lexed, &root_lex.metrics);
    if (c_source_metrics_file_first(arena, &metrics_files, options.source_path))
    {
        c_source_metrics_add(&result.source_unique, &root_lex.metrics);
    }
    CSymbolTable* symbol_table = arena_allocate(arena, CSymbolTable, 1);
    *symbol_table = c_symbol_table_create(arena);
    result.symbols = symbol_table;
    c_symbols_intern_tokens(symbol_table, root_lex.spelling_base, root_lex.tokens, root_lex.token_count);
    result.diagnostic_capacity = BUSTER_MIN(source.length + options.definition_count + 1, UINT64_C(64));
    result.diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_capacity);
    for (u64 diagnostic_index = 0; diagnostic_index < root_lex.diagnostic_count; diagnostic_index += 1)
    {
        c_preprocess_diagnostic_copy(arena, &result, root_lex.diagnostics[diagnostic_index]);
    }
    CMacro* first_macro = 0;
    CMacro* last_macro = 0;
    c_preprocess_command_definitions(arena, space, symbol_table, options, &first_macro, &last_macro);
    CToken* standard_replacement = arena_allocate(arena, CToken, 2);
    standard_replacement[0] = (CToken){
        .offset = C_SPELLING_ONE,
        .length = 1,
        .kind = C_TOKEN_PREPROCESSING_NUMBER,
    };
    standard_replacement[1] = c_space_token(space, c_preprocess_standard_version(options.dialect), C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
    c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__STDC__"), standard_replacement, 1, 0, 0, false, false);
    c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__BUSTER__"), standard_replacement, 1, 0, 0, false, false);
    if (c_preprocess_dialect_is_gnu(options.dialect))
    {
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC__"), S8("4"));
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC_MINOR__"), S8("2"));
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__GNUC_PATCHLEVEL__"), S8("1"));
    }
    else
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__STRICT_ANSI__"), standard_replacement, 1, 0, 0, false, false);
    }
    static char const* atomic_order_names[] = {
        "__ATOMIC_RELAXED", "__ATOMIC_CONSUME", "__ATOMIC_ACQUIRE", "__ATOMIC_RELEASE", "__ATOMIC_ACQ_REL", "__ATOMIC_SEQ_CST",
    };
    for (u32 order = 0; order < BUSTER_ARRAY_LENGTH(atomic_order_names); order += 1)
    {
        CToken* replacement = arena_allocate(arena, CToken, 1);
        replacement[0] = c_space_token(space, string_format(arena, S8("{u32}"), order), C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)atomic_order_names[order]), replacement, 1, 0, 0, false, false);
    }
    static char const* lock_free_macro_names[] = {
        "__CLANG_ATOMIC_BOOL_LOCK_FREE",     "__CLANG_ATOMIC_CHAR_LOCK_FREE",    "__CLANG_ATOMIC_CHAR8_T_LOCK_FREE", "__CLANG_ATOMIC_CHAR16_T_LOCK_FREE",
        "__CLANG_ATOMIC_CHAR32_T_LOCK_FREE", "__CLANG_ATOMIC_WCHAR_T_LOCK_FREE", "__CLANG_ATOMIC_SHORT_LOCK_FREE",   "__CLANG_ATOMIC_INT_LOCK_FREE",
        "__CLANG_ATOMIC_LONG_LOCK_FREE",     "__CLANG_ATOMIC_LLONG_LOCK_FREE",   "__CLANG_ATOMIC_POINTER_LOCK_FREE",
    };
    CToken* lock_free_replacement = arena_allocate(arena, CToken, 1);
    lock_free_replacement[0] = c_space_token(space, S8("2"), C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
    for (u32 macro_index = 0; macro_index < BUSTER_ARRAY_LENGTH(lock_free_macro_names); macro_index += 1)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)lock_free_macro_names[macro_index]), lock_free_replacement, 1, 0, 0, false,
                       false);
    }
    String8* feature_parameters = arena_allocate(arena, String8, 1);
    feature_parameters[0] = S8("feature");
    static char const* feature_macro_names[] = {
        "__building_module", "__has_attribute", "__has_builtin", "__has_c_attribute", "__has_extension", "__has_feature", "__has_include", "__has_include_next",
        "__is_target_arch", "__is_target_environment", "__is_target_os", "__is_target_vendor",
    };
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(feature_macro_names); feature_index += 1)
    {
        String8 feature_name = string_from_pointer((char8*)feature_macro_names[feature_index]);
        CToken* feature_replacement = arena_allocate(arena, CToken, 4);
        feature_replacement[0] = c_space_token(space, feature_name, C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        feature_replacement[1] = (CToken){
            .offset = C_SPELLING_LEFT_PARENTHESIS,
            .length = 1,
            .kind = C_TOKEN_PUNCTUATOR,
            .punctuator = C_PUNCTUATOR_LEFT_PARENTHESIS,
        };
        feature_replacement[2] = c_space_token(space, S8("feature"), C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
        feature_replacement[3] = (CToken){
            .offset = C_SPELLING_RIGHT_PARENTHESIS,
            .length = 1,
            .kind = C_TOKEN_PUNCTUATOR,
            .punctuator = C_PUNCTUATOR_RIGHT_PARENTHESIS,
        };
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, feature_name, feature_replacement, 4, feature_parameters, 1, true, false);
    }
    String8* pragma_parameters = arena_allocate(arena, String8, 1);
    pragma_parameters[0] = S8("value");
    CMacro* pragma_macro = c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("_Pragma"), 0, 0, pragma_parameters, 1, true, false);
    pragma_macro->definition.pragma_like = true;
    if (options.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        CMacro* windows_pragma_macro = c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__pragma"), 0, 0, pragma_parameters, 1, true, false);
        windows_pragma_macro->definition.pragma_like = true;
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("C_ASSERT"), 0, 0, pragma_parameters, 1, true, false);
        static char const* c_windows_calling_convention_names[] = {
            "__cdecl", "__stdcall", "__fastcall", "__thiscall", "__vectorcall", "__ptr32", "__ptr64", "__unaligned", "_W64",
        };
        for (u32 calling_convention_index = 0; calling_convention_index < BUSTER_ARRAY_LENGTH(c_windows_calling_convention_names);
             calling_convention_index += 1)
        {
            c_macro_define(arena, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)c_windows_calling_convention_names[calling_convention_index]), 0, 0,
                           0, 0, false, false);
        }
    }
    CToken* constant_parameter_replacement = arena_allocate(arena, CToken, 1);
    constant_parameter_replacement[0] = c_space_token(space, S8("value"), C_TOKEN_IDENTIFIER, C_PUNCTUATOR_NONE);
    String8* constant_parameters = arena_allocate(arena, String8, 1);
    constant_parameters[0] = S8("value");
    static char const* constant_macro_names[] = {
        "__INT8_C", "__UINT8_C", "__INT16_C", "__UINT16_C", "__INT32_C", "__UINT32_C", "__INT64_C", "__UINT64_C", "__INTMAX_C", "__UINTMAX_C",
    };
    for (u32 macro_index = 0; macro_index < BUSTER_ARRAY_LENGTH(constant_macro_names); macro_index += 1)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, string_from_pointer((char8*)constant_macro_names[macro_index]), constant_parameter_replacement, 1,
                       constant_parameters, 1, true, false);
    }
    TargetDataLayout layout = options.data_layout;
    bool windows_target = options.target.os == OPERATING_SYSTEM_WINDOWS;
    bool llp64_target = target_uses_llp64_data_model(options.target);
    bool short_wchar_target = target_uses_16_bit_wchar(options.target);
    String8 signed_pointer_type = layout.long_integer.size == layout.pointer.size ? S8("long") : S8("long long");
    String8 unsigned_pointer_type = layout.unsigned_long_integer.size == layout.pointer.size ? S8("unsigned long") : S8("unsigned long long");
#define C_DEFINE_TYPE_MACRO(name, replacement) c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8(name), (replacement))
    C_DEFINE_TYPE_MACRO("__SIZE_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__PTRDIFF_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__INTPTR_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__UINTPTR_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__INTMAX_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__UINTMAX_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__INT8_TYPE__", S8("signed char"));
    C_DEFINE_TYPE_MACRO("__UINT8_TYPE__", S8("unsigned char"));
    C_DEFINE_TYPE_MACRO("__INT16_TYPE__", S8("short"));
    C_DEFINE_TYPE_MACRO("__UINT16_TYPE__", S8("unsigned short"));
    C_DEFINE_TYPE_MACRO("__INT32_TYPE__", S8("int"));
    C_DEFINE_TYPE_MACRO("__UINT32_TYPE__", S8("unsigned int"));
    C_DEFINE_TYPE_MACRO("__INT64_TYPE__", signed_pointer_type);
    C_DEFINE_TYPE_MACRO("__UINT64_TYPE__", unsigned_pointer_type);
    C_DEFINE_TYPE_MACRO("__WCHAR_TYPE__", short_wchar_target ? S8("unsigned short") : S8("int"));
    C_DEFINE_TYPE_MACRO("__WINT_TYPE__", options.target.os == OPERATING_SYSTEM_UEFI ? S8("unsigned short") : S8("unsigned int"));
    C_DEFINE_TYPE_MACRO("__CHAR8_TYPE__", S8("unsigned char"));
    C_DEFINE_TYPE_MACRO("__CHAR16_TYPE__", S8("unsigned short"));
    C_DEFINE_TYPE_MACRO("__CHAR32_TYPE__", S8("unsigned int"));
    C_DEFINE_TYPE_MACRO("__builtin_va_list", S8("void *"));
    C_DEFINE_TYPE_MACRO("__SIZEOF_POINTER__", string_format(arena, S8("{u32}"), layout.pointer.size));
    C_DEFINE_TYPE_MACRO("__POINTER_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__SIZE_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__INTPTR_WIDTH__", string_format(arena, S8("{u32}"), layout.pointer.bit_width));
    C_DEFINE_TYPE_MACRO("__INT_WIDTH__", string_format(arena, S8("{u32}"), layout.integer.bit_width));
    C_DEFINE_TYPE_MACRO("__LONG_WIDTH__", string_format(arena, S8("{u32}"), layout.long_integer.bit_width));
    C_DEFINE_TYPE_MACRO("__LONG_LONG_WIDTH__", string_format(arena, S8("{u32}"), layout.long_long_integer.bit_width));
    C_DEFINE_TYPE_MACRO("__SIZEOF_SHORT__", string_format(arena, S8("{u32}"), layout.short_integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_INT__", string_format(arena, S8("{u32}"), layout.integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG__", string_format(arena, S8("{u32}"), layout.long_integer.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG_LONG__", string_format(arena, S8("{u32}"), layout.long_long_integer.size));
    if (layout.has_128_bit_integer)
    {
        C_DEFINE_TYPE_MACRO("__SIZEOF_INT128__", string_format(arena, S8("{u32}"), layout.integer128.size));
    }
    C_DEFINE_TYPE_MACRO("__SIZEOF_FLOAT__", string_format(arena, S8("{u32}"), layout.float_type.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_DOUBLE__", string_format(arena, S8("{u32}"), layout.double_type.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_LONG_DOUBLE__", string_format(arena, S8("{u32}"), layout.long_double_type.size));
    C_DEFINE_TYPE_MACRO("__SIZEOF_VA_LIST__", string_format(arena, S8("{u32}"), layout.va_list.size));
    C_DEFINE_TYPE_MACRO("__LONG_DOUBLE_WIDTH__", string_format(arena, S8("{u32}"), layout.long_double_type.bit_width));
    C_DEFINE_TYPE_MACRO("__WCHAR_WIDTH__", short_wchar_target ? S8("16") : S8("32"));
    C_DEFINE_TYPE_MACRO("__ORDER_LITTLE_ENDIAN__", S8("1234"));
    C_DEFINE_TYPE_MACRO("__ORDER_BIG_ENDIAN__", S8("4321"));
    C_DEFINE_TYPE_MACRO("__BYTE_ORDER__", layout.endianness == TARGET_ENDIAN_LITTLE ? S8("__ORDER_LITTLE_ENDIAN__") : S8("__ORDER_BIG_ENDIAN__"));
    C_DEFINE_TYPE_MACRO("__clang__", S8("1"));
    C_DEFINE_TYPE_MACRO("__clang_major__", S8("18"));
    if (!layout.plain_char_is_signed)
    {
        C_DEFINE_TYPE_MACRO("__CHAR_UNSIGNED__", S8("1"));
    }
    if (windows_target)
    {
        C_DEFINE_TYPE_MACRO("__int8", S8("signed char"));
        C_DEFINE_TYPE_MACRO("__int16", S8("short"));
        C_DEFINE_TYPE_MACRO("__int32", S8("int"));
        C_DEFINE_TYPE_MACRO("__int64", S8("long long"));
        C_DEFINE_TYPE_MACRO("_MSC_VER", S8("1940"));
        C_DEFINE_TYPE_MACRO("_MSC_FULL_VER", S8("194000000"));
        C_DEFINE_TYPE_MACRO("_MSC_EXTENSIONS", S8("1"));
        C_DEFINE_TYPE_MACRO("__inline", S8("static inline"));
        C_DEFINE_TYPE_MACRO("__forceinline", S8("static inline"));
        C_DEFINE_TYPE_MACRO("SORTPP_PASS", S8("1"));
        C_DEFINE_TYPE_MACRO("_WIN64", S8("1"));
        if (options.target.cpu_arch == CPU_ARCH_X86_64)
        {
            C_DEFINE_TYPE_MACRO("_AMD64_", S8("1"));
            C_DEFINE_TYPE_MACRO("_M_AMD64", S8("100"));
            C_DEFINE_TYPE_MACRO("_M_X64", S8("100"));
        }
        else if (options.target.cpu_arch == CPU_ARCH_AARCH64)
        {
            C_DEFINE_TYPE_MACRO("_ARM64_", S8("1"));
            C_DEFINE_TYPE_MACRO("_M_ARM64", S8("1"));
        }
    }
    if (!llp64_target)
    {
        C_DEFINE_TYPE_MACRO("__LP64__", S8("1"));
        C_DEFINE_TYPE_MACRO("_LP64", S8("1"));
    }
    if (options.target.os == OPERATING_SYSTEM_ANDROID && options.target.os_version_major)
    {
        C_DEFINE_TYPE_MACRO("__ANDROID_MIN_SDK_VERSION__", string_format(arena, S8("{u32}"), (u32)options.target.os_version_major));
        C_DEFINE_TYPE_MACRO("__ANDROID_API__", S8("__ANDROID_MIN_SDK_VERSION__"));
    }
#undef C_DEFINE_TYPE_MACRO
    String8 operating_system_macros[7] = {0};
    u32 operating_system_macro_count = 0;
    switch (options.target.os)
    {
    case OPERATING_SYSTEM_WINDOWS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("_WIN32");
    }
    break;
    case OPERATING_SYSTEM_UEFI:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__UEFI__");
    }
    break;
    case OPERATING_SYSTEM_MACOS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__APPLE__");
        operating_system_macros[operating_system_macro_count++] = S8("__MACH__");
        operating_system_macros[operating_system_macro_count++] = S8("__BUSTER_TARGET_MACOS__");
    }
    break;
    case OPERATING_SYSTEM_IOS:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__APPLE__");
        operating_system_macros[operating_system_macro_count++] = S8("__MACH__");
    }
    break;
    case OPERATING_SYSTEM_ANDROID:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__ANDROID__");
        operating_system_macros[operating_system_macro_count++] = S8("__linux");
        operating_system_macros[operating_system_macro_count++] = S8("__linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__unix");
        operating_system_macros[operating_system_macro_count++] = S8("__unix__");
        operating_system_macros[operating_system_macro_count++] = S8("__ELF__");
    }
    break;
    case OPERATING_SYSTEM_LINUX:
    {
        operating_system_macros[operating_system_macro_count++] = S8("__gnu_linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__linux");
        operating_system_macros[operating_system_macro_count++] = S8("__linux__");
        operating_system_macros[operating_system_macro_count++] = S8("__unix");
        operating_system_macros[operating_system_macro_count++] = S8("__unix__");
        operating_system_macros[operating_system_macro_count++] = S8("__ELF__");
    }
    break;
    case OPERATING_SYSTEM_FREESTANDING:
    case OPERATING_SYSTEM_COUNT:
    {
    }
    break;
    }
    for (u32 macro_index = 0; macro_index < operating_system_macro_count; macro_index += 1)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, operating_system_macros[macro_index], standard_replacement, 1, 0, 0, false, false);
    }
    if (options.target.os == OPERATING_SYSTEM_MACOS || options.target.os == OPERATING_SYSTEM_IOS)
    {
        c_macro_define_object_text(arena, space, symbol_table, &first_macro, &last_macro, S8("__APPLE_CC__"), S8("6000"));
    }
    String8 architecture_macro = options.target.cpu_arch == CPU_ARCH_AARCH64 ? S8("__aarch64__")
                               : options.target.cpu_arch == CPU_ARCH_WASM64  ? S8("__wasm64__")
                               : options.target.cpu_arch == CPU_ARCH_BPFEL   ? S8("__bpfel__")
                                                                             : S8("__x86_64__");
    c_macro_define(arena, symbol_table, &first_macro, &last_macro, architecture_macro, standard_replacement, 1, 0, 0, false, false);
    if (options.target.cpu_arch == CPU_ARCH_WASM64)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__wasm__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__wasm_memory64__"), standard_replacement, 1, 0, 0, false, false);
    }
    if (options.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__bpf__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__BPF__"), standard_replacement, 1, 0, 0, false, false);
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__BUSTER_EBPF__"), standard_replacement, 1, 0, 0, false, false);
    }
    if ((options.target.os == OPERATING_SYSTEM_MACOS || options.target.os == OPERATING_SYSTEM_IOS) && options.target.cpu_arch == CPU_ARCH_AARCH64)
    {
        c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__arm64__"), standard_replacement, 1, 0, 0, false, false);
    }
    // The vector-extension macros the GNU family predefines from -march, so a
    // header guarding an explicit SIMD kernel writes one condition that holds
    // for clang, gcc, and the self-hosted stages alike.  Only the features the
    // buster SIMD vocabulary can actually lower are published; announcing more
    // would invite a host header down a path this frontend cannot compile.
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(c_target_feature_macros); feature_index += 1)
    {
        CTargetFeatureMacro entry = c_target_feature_macros[feature_index];
        if (entry.cpu_arch == options.target.cpu_arch && target_cpu_feature_has(options.target, entry.feature))
        {
            c_macro_define(arena, symbol_table, &first_macro, &last_macro, entry.name, standard_replacement, 1, 0, 0, false, false);
        }
    }
    CToken hosted_replacement = standard_replacement[0];
    if (options.target.os == OPERATING_SYSTEM_FREESTANDING || options.target.os == OPERATING_SYSTEM_UEFI ||
        options.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        hosted_replacement.offset = C_SPELLING_ZERO;
    }
    c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__STDC_HOSTED__"), &hosted_replacement, 1, 0, 0, false, false);
    c_macro_define(arena, symbol_table, &first_macro, &last_macro, S8("__STDC_VERSION__"), standard_replacement + 1, 1, 0, 0, false, false);
    CPreprocessTokenRange* first_output_range = 0;
    CPreprocessTokenRange* last_output_range = 0;
    u64 output_count = 0;
    CPragmaPackStack* pack_stack = 0;
    CMacroPushMacro* macro_push_stack = 0;
    u16 pack_alignment = 0;
    CPackAlignmentRecorder pack_changes = {
        .arena = arena,
    };
    u32 expansion_limit = options.expansion_limit ? options.expansion_limit : 65536;
    bool expansion_ok = true;
    CConditionalFrame* conditional = 0;
    CPreprocessSourceFrame root_frame = {
        .lex = root_lex,
        .path = options.source_path.length ? options.source_path : S8("."),
        .logical_path = options.source_path.length ? options.source_path : S8("."),
        .line_start = true,
    };
    CPreprocessSourceFrame* source_frame = &root_frame;
    CPreprocessFileTable file_table = {0};
    root_frame.map_entry = map.count;
    c_source_map_append(&map, (IrSourceRegion){
                                  .start = root_lex.translated_offset,
                                  .source = c_preprocess_file_index(arena, &file_table, root_frame.logical_path),
                                  .checkpoints = root_lex.checkpoints,
                                  .checkpoint_offsets = root_lex.checkpoint_offsets,
                                  .checkpoint_pages = root_lex.checkpoint_pages,
                                  .checkpoint_page_count = root_lex.checkpoint_page_count,
                                  .checkpoint_count = root_lex.checkpoint_count,
                                  .base = root_lex.translated_offset,
                                  .kind = IR_SOURCE_REGION_TEXT,
                              });
    u32 include_depth_limit = options.include_depth_limit ? options.include_depth_limit : 256;
    c_preprocess_builtins(arena, symbol_table, &first_macro, &last_macro, root_frame.logical_path,
                          (CSourceLocation){.line = 1, .column = 1});
    String8* once_paths = arena_allocate(arena, String8, source.length + 1);
    u32 once_path_count = 0;
    CPreprocessPragmaContext pragma_context = {
        .arena = arena,
        .symbols = symbol_table,
        .first_macro = &first_macro,
        .last_macro = &last_macro,
        .macro_push_stack = &macro_push_stack,
        .pack_stack = &pack_stack,
        .pack_alignment = &pack_alignment,
        .pack_changes = &pack_changes,
        .once_paths = once_paths,
        .once_path_count = &once_path_count,
        .once_path_capacity = (u32)BUSTER_MIN(source.length + 1, (u64)UINT32_MAX),
    };
    while (source_frame)
    {
        pragma_context.current_path = source_frame->path;
        CLexResult lex = source_frame->lex;
        u64 token_index = source_frame->token_index;
        bool line_start = source_frame->line_start;
        CToken token = lex.tokens[token_index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            while (conditional != source_frame->conditional_base)
            {
                c_preprocess_diagnostic_push(arena, &result, conditional->location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL, S8("unterminated preprocessing conditional"));
                conditional = conditional->previous;
            }
            if (source_frame != &root_frame)
            {
                file_map_unmap(source_frame->source_map);
            }
            source_frame = source_frame->previous;
            continue;
        }
        if (token.kind == C_TOKEN_NEWLINE)
        {
            source_frame->line_start = true;
            source_frame->token_index += 1;
            continue;
        }
        first_macro->builtin_frame = source_frame;
        first_macro->builtin_token_offset = token.offset;
        first_macro->builtin_path = source_frame->logical_path;
        if (line_start && c_token_is_punctuator(&token, C_PUNCTUATOR_HASH))
        {
            CPreprocessSourceFrame* include_frame = 0;
            token_index += 1;
            bool is_line_marker = token_index < lex.token_count && lex.tokens[token_index].kind == C_TOKEN_PREPROCESSING_NUMBER;
            if (token_index >= lex.token_count || (lex.tokens[token_index].kind != C_TOKEN_IDENTIFIER && !is_line_marker))
            {
                c_preprocess_diagnostic_push(arena, &result, c_lex_token_location(&source_frame->lex, token), C_DIAGNOSTIC_EXPECTED_DIRECTIVE,
                                             S8("expected preprocessing directive after '#'"));
            }
            else
            {
                CToken directive = lex.tokens[token_index];
                if (!is_line_marker)
                {
                    token_index += 1;
                }
                u32 physical_directive_line = c_lex_token_location(&source_frame->lex, directive).line;
                CSourceLocation directive_location = c_preprocess_logical_location(source_frame, c_lex_token_location(&source_frame->lex, directive));
                u64 line_end = c_preprocess_line_end(lex, token_index);
                bool active = c_preprocess_is_active(conditional);
                char8 const* base = space->base;
                bool is_if = c_token_spelling_equal(base, directive, S8("if"));
                bool is_ifdef = c_token_spelling_equal(base, directive, S8("ifdef"));
                bool is_ifndef = c_token_spelling_equal(base, directive, S8("ifndef"));
                bool is_elif = c_token_spelling_equal(base, directive, S8("elif"));
                bool is_else = c_token_spelling_equal(base, directive, S8("else"));
                bool is_endif = c_token_spelling_equal(base, directive, S8("endif"));
                bool is_include = c_token_spelling_equal(base, directive, S8("include"));
                bool is_include_next = c_token_spelling_equal(base, directive, S8("include_next"));
                bool is_import = c_token_spelling_equal(base, directive, S8("import"));
                bool is_line = is_line_marker || c_token_spelling_equal(base, directive, S8("line"));
                bool is_pragma = c_token_spelling_equal(base, directive, S8("pragma"));
                bool is_error = c_token_spelling_equal(base, directive, S8("error"));
                bool is_warning = c_token_spelling_equal(base, directive, S8("warning"));
                if (is_if || is_ifdef || is_ifndef)
                {
                    bool condition_value = false;
                    bool valid = true;
                    if (is_if)
                    {
                        if (active)
                        {
                            valid = c_conditional_evaluate(arena, space, symbol_table, first_macro,
                                                           c_frame_wrap_tokens(arena, source_frame, token_index, line_end), (u32)(line_end - token_index),
                                                           expansion_limit, &result, options, source_frame->path, source_frame->include_origin, &condition_value);
                        }
                    }
                    else if (token_index + 1 != line_end || lex.tokens[token_index].kind != C_TOKEN_IDENTIFIER)
                    {
                        valid = false;
                    }
                    else
                    {
                        CMacro* macro = c_macro_find_token(first_macro, symbol_table, base, &lex.tokens[token_index]);
                        condition_value = macro && macro->definition.defined;
                        condition_value ^= is_ifndef;
                    }
                    if (!valid)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_CONDITIONAL,
                                                     string_format(arena, S8("invalid preprocessing conditional expression in {S8}"), source_frame->path));
                        condition_value = false;
                    }
                    CConditionalFrame* frame = arena_allocate(arena, CConditionalFrame, 1);
                    *frame = (CConditionalFrame){
                        .previous = conditional,
                        .location = directive_location,
                        .parent_active = active,
                        .active = active && condition_value,
                        .branch_taken = active && condition_value,
                    };
                    conditional = frame;
                }
                else if (is_elif)
                {
                    if (conditional == source_frame->conditional_base || conditional->else_seen)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                                     S8("'#elif' has no matching '#if', or follows '#else'"));
                    }
                    else
                    {
                        bool condition_value = false;
                        bool evaluate = conditional->parent_active && !conditional->branch_taken;
                        bool valid = true;
                        if (evaluate)
                        {
                            valid = c_conditional_evaluate(arena, space, symbol_table, first_macro,
                                                           c_frame_wrap_tokens(arena, source_frame, token_index, line_end), (u32)(line_end - token_index),
                                                           expansion_limit, &result, options, source_frame->path, source_frame->include_origin, &condition_value);
                        }
                        if (!valid)
                        {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_CONDITIONAL, S8("invalid '#elif' expression"));
                            condition_value = false;
                        }
                        conditional->active = evaluate && condition_value;
                        conditional->branch_taken |= conditional->active;
                    }
                }
                else if (is_else)
                {
                    if (conditional == source_frame->conditional_base || conditional->else_seen || token_index != line_end)
                    {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                                     S8("invalid or unmatched '#else' directive"));
                    }
                    else
                    {
                        conditional->else_seen = true;
                        conditional->active = conditional->parent_active && !conditional->branch_taken;
                        conditional->branch_taken |= conditional->active;
                    }
                }
                else if (is_endif)
                {
                    if (conditional == source_frame->conditional_base || token_index != line_end)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
                                                     S8("invalid or unmatched '#endif' directive"));
                    }
                    else
                    {
                        conditional = conditional->previous;
                    }
                }
                else if (active && (is_error || is_warning))
                {
                    c_preprocess_diagnostic_push_severity(arena, &result, directive_location,
                                                           is_error ? C_DIAGNOSTIC_PREPROCESSOR_ERROR : C_DIAGNOSTIC_PREPROCESSOR_WARNING,
                                                           is_error ? C_DIAGNOSTIC_ERROR : C_DIAGNOSTIC_WARNING,
                                                           c_preprocess_message_from_tokens(arena, base, lex.tokens + token_index,
                                                                                            (u32)(line_end - token_index)));
                }
                else if (active && is_line)
                {
                    CPreprocessTokenNode* first_line = 0;
                    CPreprocessTokenNode* last_line = 0;
                    u64 line_token_count = 0;
                    bool line_expanded = c_preprocess_expand(arena, space, symbol_table, first_macro,
                                                             c_frame_wrap_tokens(arena, source_frame, token_index, line_end), (u32)(line_end - token_index),
                                                             &first_line, &last_line, &line_token_count, expansion_limit, &result);
                    CToken* line_tokens = arena_allocate(arena, CToken, line_token_count);
                    u32 line_index = 0;
                    for (CPreprocessTokenNode* node = first_line; node; node = node->next)
                    {
                        line_tokens[line_index++] = node->token.token;
                    }
                    u64 requested_line = 0;
                    bool has_file_name = line_token_count >= 2 && line_tokens[1].kind == C_TOKEN_STRING_LITERAL && line_tokens[1].length >= 2;
                    bool valid_line = line_expanded && line_token_count >= 1 && line_tokens[0].kind == C_TOKEN_PREPROCESSING_NUMBER &&
                                      c_conditional_number(c_token_spelling(base, line_tokens[0]), &requested_line) && requested_line >= 1 &&
                                      requested_line <= UINT32_MAX &&
                                      ((!is_line_marker && (line_token_count == 1 || (line_token_count == 2 && has_file_name))) ||
                                       (is_line_marker && (line_token_count == 1 || has_file_name)));
                    if (valid_line && is_line_marker && line_token_count > 2)
                    {
                        for (u32 flag_index = 2; flag_index < line_token_count; flag_index += 1)
                        {
                            u64 flag = 0;
                            valid_line &= line_tokens[flag_index].kind == C_TOKEN_PREPROCESSING_NUMBER &&
                                          c_conditional_number(c_token_spelling(base, line_tokens[flag_index]), &flag) && flag >= 1 && flag <= 4;
                        }
                    }
                    if (!valid_line)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_LINE,
                                                     S8("expected '#line' followed by a positive line number and optional file name"));
                    }
                    else
                    {
                        source_frame->line_delta = (s64)requested_line - ((s64)physical_directive_line + 1);
                        if (has_file_name)
                        {
                            String8 name_spelling = c_token_spelling(base, line_tokens[1]);
                            source_frame->logical_path = (String8){
                                .pointer = name_spelling.pointer + 1,
                                .length = name_spelling.length - 2,
                            };
                        }
                        // The region after the directive maps through the new
                        // delta and logical file; entries partition the file's
                        // span by offset, so the split is one append.
                        source_frame->map_entry = map.count;
                        c_source_map_append(&map, (IrSourceRegion){
                                                      .start = lex.tokens[line_end].offset,
                                                      .source = UINT32_MAX,
                                                      .checkpoints = source_frame->lex.checkpoints,
                                                      .checkpoint_offsets = source_frame->lex.checkpoint_offsets,
                                                      .checkpoint_pages = source_frame->lex.checkpoint_pages,
                                                      .checkpoint_page_count = source_frame->lex.checkpoint_page_count,
                                                      .checkpoint_count = source_frame->lex.checkpoint_count,
                                                      .base = source_frame->lex.translated_offset,
                                                      .line_delta = source_frame->line_delta,
                                                      .kind = IR_SOURCE_REGION_TEXT,
                                                  });
                    }
                }
                else if (active && c_token_spelling_equal(base, directive, S8("define")))
                {
                    c_preprocess_define_directive(arena, symbol_table, lex, &token_index, &first_macro, &last_macro, &result, directive_location);
                    // Directives reached, not macros surviving: a header
                    // included twice defines its macros twice.
                    result.preprocessed.definitions += 1;
                }
                else if (active && c_token_spelling_equal(base, directive, S8("undef")))
                {
                    if (token_index >= lex.token_count || lex.tokens[token_index].kind != C_TOKEN_IDENTIFIER)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_EXPECTED_MACRO_NAME, S8("expected macro name after '#undef'"));
                    }
                    else
                    {
                        CMacro* macro = c_macro_find_token(first_macro, symbol_table, base, &lex.tokens[token_index]);
                        // The per-line rebuild used to redefine __LINE__/__FILE__ right
                        // after any #undef, so builtins stay effectively un-undefinable.
                        if (macro && !macro->builtin)
                        {
                            macro->definition.defined = false;
                        }
                        token_index += 1;
                    }
                }
                else if (active && (is_include || is_include_next || is_import))
                {
                    CPreprocessTokenNode* first_include = 0;
                    CPreprocessTokenNode* last_include = 0;
                    u64 include_token_count = 0;
                    String8 include_name = {0};
                    bool quoted = false;
                    u32 raw_include_count = (u32)(line_end - token_index);
                    bool include_expanded = c_include_name(arena, base, lex.tokens + token_index, raw_include_count, &include_name, &quoted);
                    if (!include_expanded)
                    {
                        include_expanded = c_preprocess_expand(arena, space, symbol_table, first_macro,
                                                               c_frame_wrap_tokens(arena, source_frame, token_index, line_end), raw_include_count,
                                                               &first_include, &last_include, &include_token_count, expansion_limit, &result);
                        CToken* include_tokens = arena_allocate(arena, CToken, include_token_count);
                        u32 include_index = 0;
                        for (CPreprocessTokenNode* node = first_include; node; node = node->next)
                        {
                            include_tokens[include_index++] = node->token.token;
                        }
                        include_expanded = include_expanded && c_include_name(arena, base, include_tokens, include_index, &include_name, &quoted);
                    }
                    if (!include_expanded)
                    {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INVALID_INCLUDE,
                                                     S8("expected a quoted or angle-bracket header name"));
                    }
                    else if (source_frame->depth >= include_depth_limit)
                    {
                        c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INCLUDE_DEPTH, S8("preprocessor include depth limit exceeded"));
                    }
                    else
                    {
                        String8 include_path = {0};
                        String8 include_source = {0};
                        FileMapRead include_source_map = {0};
                        CIncludeSearchOrigin include_origin = {0};
                        bool include_resolved =
                            is_include_next ? c_include_resolve_next(arena, options, include_name, &include_path, &include_source, &include_source_map,
                                                                      source_frame->include_origin, &include_origin)
                                            : c_include_resolve(arena, options, source_frame->path, include_name, quoted, &include_path, &include_source,
                                                                &include_source_map, &include_origin);
                        if (!include_resolved)
                        {
                            c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_INCLUDE_NOT_FOUND,
                                                         string_format(arena, S8("included file was not found: {S8}"), include_name));
                        }
                        else
                        {
                            bool include_once = false;
                            for (u32 once_index = 0; once_index < once_path_count; once_index += 1)
                            {
                                include_once |= string_equal(once_paths[once_index], include_path);
                            }
                            if (is_import && !include_once)
                            {
                                once_paths[once_path_count++] = include_path;
                            }
                            CLexResult include_lex = include_once ? (CLexResult){0} : c_lex_space(arena, space, include_source);
                            // A suppressed include lexed nothing and adds
                            // zeroes; its path was already counted by the
                            // inclusion that did the lexing.
                            c_source_metrics_add(&result.source_lexed, &include_lex.metrics);
                            if (c_source_metrics_file_first(arena, &metrics_files, include_path))
                            {
                                c_source_metrics_add(&result.source_unique, &include_lex.metrics);
                            }
                            c_symbols_intern_tokens(symbol_table, include_lex.spelling_base, include_lex.tokens, include_lex.token_count);
                            for (u64 index = 0; index < include_lex.diagnostic_count; index += 1)
                            {
                                CDiagnostic diagnostic = include_lex.diagnostics[index];
                                diagnostic.message = string_format(arena, S8("{S8}: {S8}"), include_path, diagnostic.message);
                                c_preprocess_diagnostic_copy(arena, &result, diagnostic);
                            }
                            if (!include_once)
                            {
                                include_frame = arena_allocate(arena, CPreprocessSourceFrame, 1);
                                *include_frame = (CPreprocessSourceFrame){
                                    .previous = source_frame,
                                    .conditional_base = conditional,
                                    .lex = include_lex,
                                    .path = include_path,
                                    .logical_path = include_path,
                                    .source_map = include_source_map,
                                    .depth = source_frame->depth + 1,
                                    .line_start = true,
                                    .include_origin = include_origin,
                                };
                                include_frame->map_entry = map.count;
                                c_source_map_append(&map, (IrSourceRegion){
                                                              .start = include_lex.translated_offset,
                                                              .source = UINT32_MAX,
                                                              .checkpoints = include_lex.checkpoints,
                                                              .checkpoint_offsets = include_lex.checkpoint_offsets,
                                                              .checkpoint_pages = include_lex.checkpoint_pages,
                                                              .checkpoint_page_count = include_lex.checkpoint_page_count,
                                                              .checkpoint_count = include_lex.checkpoint_count,
                                                              .base = include_lex.translated_offset,
                                                              .kind = IR_SOURCE_REGION_TEXT,
                                                          });
                            }
                            else
                            {
                                file_map_unmap(include_source_map);
                            }
                        }
                    }
                }
                else if (active && is_pragma)
                {
                    CPreprocessTokenNode* first_pragma = 0;
                    CPreprocessTokenNode* last_pragma = 0;
                    u64 pragma_token_count = 0;
                    bool pragma_expanded = c_preprocess_expand(arena, space, symbol_table, first_macro,
                                                               c_frame_wrap_tokens(arena, source_frame, token_index, line_end), (u32)(line_end - token_index),
                                                               &first_pragma, &last_pragma, &pragma_token_count, expansion_limit, &result);
                    CToken* pragma_tokens = 0;
                    u32 expanded_pragma_count = c_preprocess_tokens_from_nodes(first_pragma, arena, &pragma_tokens);
                    if (pragma_expanded)
                    {
                        c_preprocess_handle_pragma(pragma_context, base, pragma_tokens, expanded_pragma_count);
                    }
                }
                else if (active)
                {
                    c_preprocess_diagnostic_push(arena, &result, directive_location, C_DIAGNOSTIC_UNSUPPORTED_DIRECTIVE,
                                                 string_format(arena, S8("{S8}: preprocessing directive is not implemented yet: {S8}"), source_frame->path,
                                                               c_token_spelling(base, directive)));
                }
            }
            while (token_index < lex.token_count && lex.tokens[token_index].kind != C_TOKEN_NEWLINE && lex.tokens[token_index].kind != C_TOKEN_END_OF_FILE)
            {
                token_index += 1;
            }
            source_frame->token_index = token_index;
            source_frame->line_start = true;
            if (include_frame)
            {
                source_frame = include_frame;
            }
            continue;
        }
        source_frame->line_start = false;
        u64 line_end = c_preprocess_line_end(lex, token_index);
        if (!c_preprocess_is_active(conditional))
        {
            source_frame->token_index = line_end;
            continue;
        }
        u64 logical_end = line_end;
        u32 parenthesis_depth = 0;
        for (u64 scan = token_index; scan < logical_end; scan += 1)
        {
            if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                parenthesis_depth += 1;
            }
            else if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS) && parenthesis_depth)
            {
                parenthesis_depth -= 1;
            }
        }
        while (parenthesis_depth && logical_end < lex.token_count && lex.tokens[logical_end].kind == C_TOKEN_NEWLINE)
        {
            u64 next_line_start = logical_end + 1;
            if (next_line_start < lex.token_count && c_token_is_punctuator(&lex.tokens[next_line_start], C_PUNCTUATOR_HASH))
            {
                break;
            }
            logical_end += 1;
            u64 next_line_end = c_preprocess_line_end(lex, logical_end);
            for (u64 scan = logical_end; scan < next_line_end; scan += 1)
            {
                if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_LEFT_PARENTHESIS))
                {
                    parenthesis_depth += 1;
                }
                else if (c_token_is_punctuator(&lex.tokens[scan], C_PUNCTUATOR_RIGHT_PARENTHESIS) && parenthesis_depth)
                {
                    parenthesis_depth -= 1;
                }
            }
            logical_end = next_line_end;
        }
        CToken* logical_tokens = arena_allocate(arena, CToken, logical_end - token_index);
        u32 logical_token_count = 0;
        for (u64 scan = token_index; scan < logical_end; scan += 1)
        {
            if (lex.tokens[scan].kind != C_TOKEN_NEWLINE)
            {
                logical_tokens[logical_token_count] = lex.tokens[scan];
                logical_token_count += 1;
            }
        }
        // A line whose identifiers name no defined macro expands to itself,
        // so its staging array is the output range and the expansion
        // machinery (a task node and an output node per token) is skipped.
        // Locations are not materialized at all on this path: the file's
        // source-map entry recovers them from the offsets on demand.
        bool needs_expansion = false;
        for (u32 scan = 0; scan < logical_token_count && !needs_expansion; scan += 1)
        {
            CToken logical_token = logical_tokens[scan];
            if (logical_token.kind == C_TOKEN_IDENTIFIER)
            {
                CMacro* line_macro = c_macro_find_token(first_macro, symbol_table, space->base, &logical_tokens[scan]);
                needs_expansion = line_macro && line_macro->definition.defined;
            }
        }
        if (logical_token_count && map.regions[source_frame->map_entry].source == UINT32_MAX)
        {
            map.regions[source_frame->map_entry].source = c_preprocess_file_index(arena, &file_table, source_frame->logical_path);
        }
        if (!needs_expansion)
        {
            if (logical_token_count)
            {
                // Pragmas cannot fire inside a text line on this path (only
                // directive lines and _Pragma markers change pack state), so
                // one sample covers the whole batch.
                c_pack_alignment_record(&pack_changes, output_count, pack_alignment);
                c_preprocess_output_append_range(arena, &first_output_range, &last_output_range, logical_tokens, logical_token_count);
                output_count += logical_token_count;
            }
        }
        else
        {
            u32 token_file = c_preprocess_file_index(arena, &file_table, source_frame->logical_path);
            CPpToken* wrapped_tokens = arena_allocate(arena, CPpToken, logical_token_count);
            for (u32 scan = 0; scan < logical_token_count; scan += 1)
            {
                CSourceLocation location = c_preprocess_logical_location(source_frame, c_lex_token_location(&source_frame->lex, logical_tokens[scan]));
                location.file = token_file;
                wrapped_tokens[scan] = (CPpToken){
                    .token = logical_tokens[scan],
                    .location = location,
                };
            }
            CPreprocessTokenNode* first_line = 0;
            CPreprocessTokenNode* last_line = 0;
            u64 line_output_count = 0;
            expansion_ok = c_preprocess_expand(arena, space, symbol_table, first_macro, wrapped_tokens, logical_token_count, &first_line, &last_line,
                                               &line_output_count, expansion_limit, &result);
            if (expansion_ok)
            {
                c_preprocess_process_expanded_line(arena, pragma_context, space, &map, first_line, line_output_count, &first_output_range, &last_output_range,
                                                   &output_count);
            }
        }
        source_frame->token_index = logical_end;
        if (!expansion_ok)
        {
            break;
        }
    }
    result.tokens = arena_allocate(arena, CToken, output_count + 1);
    u64 output_index = 0;
    for (CPreprocessTokenRange* range = first_output_range; range; range = range->next)
    {
        memcpy(result.tokens + output_index, range->tokens, sizeof(*result.tokens) * range->count);
        output_index += range->count;
    }
    result.tokens[output_index++] = (CToken){
        .offset = root_lex.tokens[root_lex.token_count - 1].offset,
        .kind = C_TOKEN_END_OF_FILE,
    };
    result.token_count = output_index;
    result.pack_changes = pack_changes.changes;
    result.pack_change_count = pack_changes.count;
    // The stream is contiguous by now, so the spellings sum in one linear
    // pass rather than one add per token as the ranges were appended.
    result.preprocessed.tokens = output_count;
    for (u64 token_index = 0; token_index < output_count; token_index += 1)
    {
        result.preprocessed.bytes += c_token_length(space->base, result.tokens[token_index]);
    }
    result.preprocessed.spelling_bytes = space->used;
    result.files = file_table.files;
    result.file_count = file_table.count;
    // The map was append-only while the stream was built; sort it once so
    // recovery can binary-search. Only #line splits append out of order, so
    // the insertion pass is effectively linear.
    for (u32 entry_index = 1; entry_index < map.count; entry_index += 1)
    {
        IrSourceRegion region = map.regions[entry_index];
        u32 probe = entry_index;
        while (probe && map.regions[probe - 1].start > region.start)
        {
            map.regions[probe] = map.regions[probe - 1];
            probe -= 1;
        }
        map.regions[probe] = region;
    }
    c_source_map_publish(arena, recovery, &map);
    // Respelling appends regions at the tail of the space, in order, and
    // queries the map as it goes; publish once so those queries land, and
    // again for what they added.
    c_preprocess_respell_c23(space, &map, &result);
    c_source_map_publish(arena, recovery, &map);
    u32 page_count = (u32)((space->used >> IR_SOURCE_MAP_PAGE_SHIFT) + 1);
    u32* pages = arena_allocate(arena, u32, page_count);
    u32 fill_entry = 0;
    for (u32 page = 0; page < page_count; page += 1)
    {
        u32 page_start = page << IR_SOURCE_MAP_PAGE_SHIFT;
        while (fill_entry + 1 < map.count && map.regions[fill_entry + 1].start <= page_start)
        {
            fill_entry += 1;
        }
        pages[page] = fill_entry;
    }
    recovery->map.pages = pages;
    recovery->map.page_count = page_count;
    return result;
}

BUSTER_C_SHARED bool c_parse_auto_type_word(String8 spelling)
{
    return string_equal(spelling, S8("__auto_type"));
}

// The keyword set is probed once per identifier from several parser scan
// loops, so it is a hashed set built once from the spellings table rather
// than a linear walk that re-derived every keyword's length per query.

BUSTER_C_SHARED u8 c_declaration_keyword_slots[C_DECLARATION_KEYWORD_SLOT_COUNT];
BUSTER_C_SHARED bool c_declaration_keyword_slots_built;

BUSTER_C_SHARED void c_declaration_keyword_slots_build(void)
{
    BUSTER_CHECK_SERIAL_INITIALIZATION();
    for (u32 keyword_index = 0; keyword_index < BUSTER_ARRAY_LENGTH(c_declaration_keyword_spellings); keyword_index += 1)
    {
        u32 slot = (u32)(c_macro_name_hash(c_declaration_keyword_spellings[keyword_index]) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1));
        while (c_declaration_keyword_slots[slot])
        {
            slot = (slot + 1) & (C_DECLARATION_KEYWORD_SLOT_COUNT - 1);
        }
        c_declaration_keyword_slots[slot] = (u8)(keyword_index + 1);
    }
    c_declaration_keyword_slots_built = true;
}

// Every C frontend table that is built on first use is filled here on the
// calling thread. The character-class tables remain runtime-derived because
// spelling their predicates through the preprocessor measurably inflates the
// frontend translation unit.
void c_prewarm(void)
{
#if BUSTER_C_LEX_COMPACT
    if (!c_lex_compact_tables_built)
    {
        c_lex_compact_tables_build();
    }
#endif
    if (!c_identifier_continue_table_built)
    {
        c_identifier_continue_table_build();
    }
    if (!c_literal_plain_table_built)
    {
        c_literal_plain_table_build();
    }
    if (!c_punctuator_dispatch_built)
    {
        c_punctuator_dispatch_build();
    }
    if (!c_declaration_keyword_slots_built)
    {
        c_declaration_keyword_slots_build();
    }
}

#if !BUSTER_UNITY_BUILD
String8 c_token_kind_name(CTokenKind kind)
{
    switch (kind)
    {
    case C_TOKEN_INVALID:
        return S8("invalid");
    case C_TOKEN_END_OF_FILE:
        return S8("end of file");
    case C_TOKEN_IDENTIFIER:
        return S8("identifier");
    case C_TOKEN_PREPROCESSING_NUMBER:
        return S8("preprocessing number");
    case C_TOKEN_CHARACTER_LITERAL:
        return S8("character literal");
    case C_TOKEN_STRING_LITERAL:
        return S8("string literal");
    case C_TOKEN_PUNCTUATOR:
        return S8("punctuator");
    case C_TOKEN_NEWLINE:
        return S8("newline");
    case C_TOKEN_PRAGMA:
        return S8("pragma");
    case C_TOKEN_KIND_COUNT:
        return S8("invalid token kind");
    }
    return S8("invalid token kind");
}
#endif
