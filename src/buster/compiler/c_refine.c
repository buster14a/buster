// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Token refinement: turns the lexer's output for a file (c_lexer.c: kinds, starts and ends) into what the
// preprocessor and every later stage read, once per file however many translation units include it
// (docs/c-refinement.md). For each token:
//   - its value: the IdentId of an identifier (interned in an InternTable, c_intern.c), or else the token's end, so
//     that a literal's or punctuator's spelling stays reachable without the ends array;
//   - its leading-space bit: whether whitespace or a comment separates it from the token before it. A gap of line
//     splices alone is no whitespace (translation phase 2 removed it), so `#define F\<newline>(x)` defines a
//     function-like macro and stringizing `a\<newline>+b` gives "a+b"; a comment is whitespace.
// The kinds and starts stay as the lexer wrote them, and `values` may be the lexer's `ends` itself: refinement writes
// it in place.
//
// Entry points: refine_tokens (AVX-512) and refine_tokens_scalar (the reference, token by token, which test-refinement
// compares it with), refine_identifier_key (an identifier's lookup key), refine_identifier_end (an identifier's
// physical end, which refinement overwrote with its id) and refine_gap_is_splices.
//
// refine_tokens works on blocks of BUSTER_REFINE_BLOCK_TOKENS tokens, in two passes:
//   A. Per 16 tokens: the gap bits (starts[i] != ends[i - 1], one compare). A gap that is only line splices needs a
//      backslash, so a step of 64 tokens whose gaps lie in 64-byte chunks without one (refine_backslash_chunks marks
//      them for the whole source first) is done; in the others, the byte before each start comes from a 128-byte
//      window of the source with one vpermi2b. A non-first-of-line token whose gap ends in a newline byte can only
//      follow a line splice (a newline outside comments starts a logical line, and a comment ends with "*/"), so those
//      rare tokens have the scalar refine_gap_is_splices decide their bit. The identifiers' starts, lengths and
//      token indices are compressed (vpcompressd) into dense arrays.
//   B. The block's identifiers are interned in batches of 64 (refine_tokens; RefineVariant has the others the benchmark
//      compares). Stage 1 reads each with one masked 64-byte load (never outside the token, so never outside the
//      source), sends any with a backslash (a splice or a UCN; with trigraphs, any '?') or longer than
//      BUSTER_INTERN_FAST_LENGTH to the scalar path, hashes the others (intern_hash_vector) and prefetches their
//      buckets. Stage 2 compares each bucket's four slots with the identifier's first 12 bytes (zero-padded) in one
//      compare, which identifies a spelling of up to 11 bytes outright; a longer one's first candidate is confirmed in
//      stage 2b with one masked compare of its record's bytes and terminating zero. Nothing branches on the outcome:
//      an identifier that isn't found this way (a new spelling, a second candidate, a probe past its first bucket, or
//      the scalar path) is deferred, and stage 3 interns the deferred ones in token order with intern_find_or_add, so
//      that new spellings get the same ids as refine_tokens_scalar gives them.
//
// main.c includes this file after c_lexer.c (whose scalar lexer it uses for splices and UCNs), lane.c and
// c_intern.c; every function is static. Layout, in this order: the identifier key and end (refine_encode_utf8,
// refine_identifier_key, refine_identifier_end, refine_gap_is_splices), refine_tokens_scalar, then the vectorized
// path (refine_byte_before_newlines, refine_pass_a, refine_intern_batch, refine_tokens).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

// Tokens per block: pass A's dense identifier arrays for a block stay in the L1 cache.
#define BUSTER_REFINE_BLOCK_TOKENS 1024
// Pass A's step: the kinds of 64 tokens are one vector, their starts and ends four.
#define BUSTER_REFINE_STEP_TOKENS 64
#define BUSTER_REFINE_LANES 16
// The window of source bytes pass A reads the bytes before 16 starts from: two vectors.
#define BUSTER_REFINE_WINDOW_BYTES 128
// The dense arrays get whole-vector stores past their last entry.
#define BUSTER_REFINE_DENSE_SLACK BUSTER_REFINE_LANES

// ---------------------------------------------------------------------------------------------------------------------
// Identifier keys and ends

// Writes the UTF-8 encoding of a code point (at most 0x10FFFF, not a surrogate: read_ucn only accepts those).
static uint32_t refine_encode_utf8(uint32_t code_point, uint8_t* out)
{
    uint32_t result;
    if (code_point < 0x80)
    {
        out[0] = (uint8_t)code_point;
        result = 1;
    }
    else if (code_point < 0x800)
    {
        out[0] = (uint8_t)(0xC0 | (code_point >> 6));
        out[1] = (uint8_t)(0x80 | (code_point & 0x3F));
        result = 2;
    }
    else if (code_point < 0x10000)
    {
        out[0] = (uint8_t)(0xE0 | (code_point >> 12));
        out[1] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (code_point & 0x3F));
        result = 3;
    }
    else
    {
        out[0] = (uint8_t)(0xF0 | (code_point >> 18));
        out[1] = (uint8_t)(0x80 | ((code_point >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (code_point & 0x3F));
        result = 4;
    }
    return result;
}

// The lookup key of the identifier token at [start, end): its characters after translation phases 1 and 2 (trigraphs
// if enabled, line splices removed), with each UCN as the UTF-8 of its code point, as clang interns them, so that
// fo\<newline>o is foo and é is é. A named UCN (\N{...}) stays as written: this table has no character names.
// `out` needs end - start bytes, which is enough: removing splices and encoding a UCN only make the spelling shorter.
static uint32_t refine_identifier_key(const uint8_t* source, uint64_t length, uint32_t start, uint32_t end, uint32_t flags, uint8_t* out)
{
    const ScalarLexer lexer = {
        .source = source,
        .length = length,
        .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = (flags & C_TOKENIZER_CPLUSPLUS) != 0,
    };
    uint32_t result = 0;
    uint64_t p = start;
    while (p < end)
    {
        uint64_t size;
        uint64_t char_start;
        const int32_t c = get_char(&lexer, p, &size, &char_start);
        uint64_t ucn_end;
        uint32_t code_point;
        if (c == '\\' && read_ucn(&lexer, p, &ucn_end, &code_point) && code_point != BUSTER_LEX_NAMED_UCN)
        {
            result += refine_encode_utf8(code_point, out + result);
            p = ucn_end;
        }
        else
        {
            // Every other character, and each character of a named UCN.
            out[result] = (uint8_t)c;
            result += 1;
            p += size;
        }
    }
    return result;
}

// The end of the identifier token that starts at `start` (refinement replaced it with the identifier's id).
static __attribute__((unused)) uint32_t refine_identifier_end(const uint8_t* source, uint64_t length, uint32_t start, uint32_t flags)
{
    const ScalarLexer lexer = {
        .source = source,
        .length = length,
        .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = (flags & C_TOKENIZER_CPLUSPLUS) != 0,
    };
    uint64_t size;
    uint64_t char_start;
    const int32_t c = get_char(&lexer, start, &size, &char_start);
    uint64_t p = start + size;
    uint64_t ucn_end;
    uint32_t code_point;
    if (c == '\\' && read_ucn(&lexer, start, &ucn_end, &code_point))
    {
        p = ucn_end;
    }
    else if (c >= 0x80)
    {
        p = char_start + decode_utf8(&lexer, char_start, &code_point);
    }
    return (uint32_t)lex_identifier_continue(&lexer, p);
}

// Is [gap_start, gap_end) of the source nothing but line splices (a backslash, or "??/" with trigraphs, then
// whitespace and a newline)? Then it isn't whitespace.
static bool refine_gap_is_splices(const uint8_t* source, uint64_t length, uint32_t gap_start, uint32_t gap_end, uint32_t flags)
{
    const ScalarLexer lexer = {
        .source = source,
        .length = length,
        .trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = false,
    };
    uint64_t p = gap_start;
    bool splices = true;
    while (splices && p < gap_end)
    {
        uint64_t backslash = 0;
        if (source[p] == '\\')
        {
            backslash = 1;
        }
        else if (lexer.trigraphs && p + 2 < length && source[p] == '?' && source[p + 1] == '?' && source[p + 2] == '/')
        {
            backslash = 3;
        }
        const uint64_t newline = backslash != 0 ? escaped_newline_size(&lexer, p + backslash) : 0;
        splices = newline != 0;
        p += backslash + newline;
    }
    return splices && p == gap_end;
}

// Interns the identifier token of `span` bytes at `start`: its bytes as they are, or its key (refine_identifier_key) if
// they have a backslash (or, with trigraphs, a '?'). Returns 0 if the table or the scratch arena runs out of room.
static IdentId refine_intern_identifier(InternTable* table, const uint8_t* source, uint64_t length, uint32_t start, uint32_t span, uint32_t flags)
{
    const bool trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0;
    bool cleaned = false;
    for (uint32_t k = 0; !cleaned && k < span; k += 1)
    {
        cleaned = source[start + k] == '\\' || (source[start + k] == '?' && trigraphs);
    }
    IdentId result = 0;
    if (!cleaned)
    {
        result = intern_find_or_add(table, source + start, span);
    }
    else
    {
        // The key is never longer than the token.
        Scratch scratch = scratch_begin(0);
        uint8_t* key = arena_push(scratch.arena, span, 1);
        if (key != 0)
        {
            const uint32_t key_length = refine_identifier_key(source, length, start, start + span, flags, key);
            result = intern_find_or_add(table, key, key_length);
        }
        scratch_end(scratch);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The scalar reference

// Refines `count` tokens of `source`, a token at a time: `values[i]` is token i's IdentId if it is an identifier and
// its end otherwise, and bit i % 64 of `space[i / 64]` is its leading-space bit. `values` may be `ends`. `space` needs
// (count + 63) / 64 words. Returns false if the intern table runs out of room.
static bool refine_tokens_scalar(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends,
                                 uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags)
{
    bool result = true;
    uint32_t previous_end = 0;
    for (uint64_t i = 0; i < count; i += 64)
    {
        space[i / 64] = 0;
    }
    for (uint64_t i = 0; result && i < count; i += 1)
    {
        const uint32_t start = starts[i];
        const uint32_t end = ends[i];
        const bool start_of_line = (kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) != 0;
        bool leading_space = start != previous_end;
        if (leading_space && !start_of_line)
        {
            leading_space = !refine_gap_is_splices(source, length, previous_end, start, flags);
        }
        space[i / 64] |= (uint64_t)leading_space << (i % 64);
        previous_end = end;
        uint32_t value = end;
        if ((kinds[i] & (uint8_t)~BUSTER_C_TOKEN_START_OF_LINE) == C_TOKEN_RAW_IDENTIFIER)
        {
            value = refine_intern_identifier(table, source, length, start, end - start, flags);
            result = value != 0;
        }
        values[i] = value;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The vectorized path

// What pass A collects about a block's identifiers, for pass B.
typedef struct RefineIdentifiers RefineIdentifiers;
struct RefineIdentifiers
{
    uint32_t starts[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    uint32_t lengths[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    uint32_t tokens[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    uint32_t count;
};

// The 16 tokens' lanes of `starts` (`valid` of them) whose previous byte is a newline ('\n' or '\r'). The bytes come
// from a 128-byte window from the first start's previous byte, with one vpermi2b; 16 starts that span more than that
// (long tokens or comments between them) read their bytes one at a time.
BUSTER_INLINE uint32_t refine_newlines_before(const uint8_t* source, uint64_t length, __m512i starts, const uint32_t* start_values, uint32_t valid)
{
    uint32_t result = 0;
    const uint32_t lanes = (uint32_t)__builtin_popcount(valid);
    // A start of 0 has no previous byte; the window then starts at 0 and that lane's index wraps, so it is masked out.
    const uint32_t first = start_values[0];
    const uint64_t base = first != 0 ? (uint64_t)first - 1 : 0;
    const uint64_t span = (uint64_t)start_values[lanes - 1] - 1 - base;
    if (BUSTER_LIKELY(span < BUSTER_REFINE_WINDOW_BYTES))
    {
        const uint64_t available = length - base;
        const __mmask64 low_mask = available >= 64 ? ~0ull : _bzhi_u64(~0ull, (uint32_t)available);
        const __mmask64 high_mask = available >= 128 ? ~0ull : available <= 64 ? 0 : _bzhi_u64(~0ull, (uint32_t)(available - 64));
        const __m512i low = _mm512_maskz_loadu_epi8(low_mask, source + base);
        const __m512i high = _mm512_maskz_loadu_epi8(high_mask, source + base + 64);
        const __m512i offsets = _mm512_sub_epi32(starts, _mm512_set1_epi32((int)(base + 1)));
        const __m512i indices = _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(offsets));
        const __m128i before = _mm512_castsi512_si128(_mm512_permutex2var_epi8(low, indices, high));
        result = (uint32_t)(_mm_cmpeq_epi8_mask(before, _mm_set1_epi8('\n')) | _mm_cmpeq_epi8_mask(before, _mm_set1_epi8('\r')));
    }
    else
    {
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint32_t start = start_values[lane];
            const uint8_t c = start != 0 ? source[start - 1] : 0;
            result |= (uint32_t)(c == '\n' || c == '\r') << lane;
        }
    }
    return result & valid & ~(uint32_t)(first == 0);
}

// The source's 64-byte chunks that hold a backslash (or, with trigraphs, a '?': "??/" is one): bit c % 64 of
// `chunks[c / 64]` for chunk c. A line splice needs a backslash, so a gap in chunks without one isn't only splices.
// `chunks` needs (length + 4095) / 4096 words.
BUSTER_INLINE void refine_backslash_chunks(const uint8_t* source, uint64_t length, uint64_t* chunks, const bool trigraphs)
{
    uint64_t word = 0;
    uint64_t chunk = 0;
    for (; chunk * 64 < length; chunk += 1)
    {
        const uint64_t remaining = length - chunk * 64;
        const __m512i bytes = _mm512_maskz_loadu_epi8(remaining >= 64 ? ~0ull : _bzhi_u64(~0ull, (uint32_t)remaining), source + chunk * 64);
        uint64_t found = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\'));
        if (trigraphs)
        {
            found |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('?'));
        }
        word |= (uint64_t)(found != 0) << (chunk % 64);
        if (chunk % 64 == 63)
        {
            chunks[chunk / 64] = word;
            word = 0;
        }
    }
    if (chunk % 64 != 0)
    {
        chunks[chunk / 64] = word;
    }
}

// Does any chunk of source[begin, end) hold a backslash?
BUSTER_INLINE bool refine_chunks_have_backslash(const uint64_t* chunks, uint64_t begin, uint64_t end)
{
    uint64_t found = 0;
    if (begin < end)
    {
        const uint64_t first = begin / 64;
        const uint64_t last = (end - 1) / 64;
        for (uint64_t word = first / 64; word <= last / 64; word += 1)
        {
            const uint64_t low = word == first / 64 ? first % 64 : 0;
            const uint64_t high = word == last / 64 ? last % 64 : 63;
            found |= chunks[word] & (_bzhi_u64(~0ull, (uint32_t)(high + 1)) & ~_bzhi_u64(~0ull, (uint32_t)low));
        }
    }
    return found != 0;
}

// Pass A over tokens [begin, end) of the block (a multiple of 64 from `begin`, but at the source's last tokens):
// the leading-space words, `values` for every token when it isn't `ends` (the ends; pass B overwrites the
// identifiers'), and the dense identifier arrays. `previous_ends` carries the ends of the 16 tokens before `begin`
// (lane 15 is the last one's), read before pass B could overwrite them in place. A step of 64 tokens whose gaps hold
// no backslash (most: `backslash_chunks`, from refine_backslash_chunks) needs no byte before a start.
BUSTER_INLINE __m512i refine_pass_a(const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint64_t begin,
                                    uint64_t end, uint32_t* values, uint64_t* space, uint32_t flags, const uint64_t* backslash_chunks, __m512i previous_ends,
                                    RefineIdentifiers* identifiers)
{
    const __m512i iota = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const bool separate_values = values != ends;
    uint32_t identifier_count = 0;
    for (uint64_t step = begin; step < end; step += BUSTER_REFINE_STEP_TOKENS)
    {
        const uint64_t remaining = end - step;
        const uint64_t step_count = remaining >= BUSTER_REFINE_STEP_TOKENS ? BUSTER_REFINE_STEP_TOKENS : remaining;
        const __mmask64 valid = _bzhi_u64(~0ull, (uint32_t)step_count);
        const __m512i kind_vector = _mm512_maskz_loadu_epi8(valid, kinds + step);
        const __mmask64 identifier = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_and_si512(kind_vector, _mm512_set1_epi8(0x7F)), _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        const __mmask64 start_of_line = _mm512_movepi8_mask(kind_vector);
        // The step's gaps lie between the end of the token before it and the start of its last token.
        const uint32_t gaps_begin = (uint32_t)_mm_extract_epi32(_mm512_extracti32x4_epi32(previous_ends, 3), 3);
        const bool splices_possible = refine_chunks_have_backslash(backslash_chunks, gaps_begin, starts[step + step_count - 1]);
        uint64_t space_word = 0;
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const uint64_t first = step + group * BUSTER_REFINE_LANES;
            const __mmask16 lanes = (__mmask16)(valid >> (group * BUSTER_REFINE_LANES));
            if (lanes != 0)
            {
                const __m512i start_vector = _mm512_maskz_loadu_epi32(lanes, starts + first);
                const __m512i end_vector = _mm512_maskz_loadu_epi32(lanes, ends + first);
                const __m512i before_ends = _mm512_alignr_epi32(end_vector, previous_ends, 15);
                const uint32_t gaps = _mm512_mask_cmpneq_epi32_mask(lanes, start_vector, before_ends);
                uint32_t space_bits = gaps;
                if (BUSTER_UNLIKELY(splices_possible))
                {
                    // Gaps that end in a newline byte, after a token that isn't the first of its line: line splices.
                    const uint32_t continued = gaps & ~(uint32_t)(uint16_t)(start_of_line >> (group * BUSTER_REFINE_LANES));
                    uint32_t splices = continued != 0 ? continued & refine_newlines_before(source, length, start_vector, starts + first, lanes) : 0;
                    for (; splices != 0; splices &= splices - 1)
                    {
                        const uint32_t lane = (uint32_t)__builtin_ctz(splices);
                        const uint32_t gap_start = lane != 0 ? ends[first + lane - 1] : (uint32_t)_mm_extract_epi32(_mm512_extracti32x4_epi32(previous_ends, 3), 3);
                        const bool only_splices = refine_gap_is_splices(source, length, gap_start, starts[first + lane], flags);
                        space_bits &= ~((uint32_t)only_splices << lane);
                    }
                }
                space_word |= (uint64_t)space_bits << (group * BUSTER_REFINE_LANES);
                if (separate_values)
                {
                    _mm512_mask_storeu_epi32(values + first, lanes, end_vector);
                }
                const __mmask16 identifiers_here = (__mmask16)(identifier >> (group * BUSTER_REFINE_LANES));
                const __m512i lengths = _mm512_sub_epi32(end_vector, start_vector);
                const __m512i indices = _mm512_add_epi32(iota, _mm512_set1_epi32((int)first));
                _mm512_storeu_si512(identifiers->starts + identifier_count, _mm512_maskz_compress_epi32(identifiers_here, start_vector));
                _mm512_storeu_si512(identifiers->lengths + identifier_count, _mm512_maskz_compress_epi32(identifiers_here, lengths));
                _mm512_storeu_si512(identifiers->tokens + identifier_count, _mm512_maskz_compress_epi32(identifiers_here, indices));
                identifier_count += (uint32_t)__builtin_popcount(identifiers_here);
                previous_ends = end_vector;
            }
        }
        space[step / 64] = space_word;
    }
    identifiers->count = identifier_count;
    return previous_ends;
}

// Pass B for identifiers [begin, begin + count) of the dense arrays (count <= `batch`, at most 64): stages 1 to 3.
// `fused` merges stages 1 and 2 (each identifier probed right after it is hashed, without a prefetch). The table's
// arrays are read once per batch into locals (stage 3 may grow it), so that the stores of ids can't make Clang read
// them again for every identifier. Deferred identifiers are bits of a mask, which stage 3 goes through in token order.
// Returns false if the intern table runs out of room.
BUSTER_INLINE bool refine_intern_batch(InternTable* table, const uint8_t* source, uint64_t length, const RefineIdentifiers* identifiers, uint32_t begin,
                                       uint32_t count, uint32_t* values, uint32_t flags, const bool trigraphs, const uint32_t batch, const bool fused)
{
    const uint8_t* const buckets = table->buckets;
    const uint8_t* const records = table->records;
    const uint32_t* const record_of = table->record_of;
    const uint64_t bucket_mask = table->bucket_mask;
    const uint32_t* const starts = identifiers->starts + begin;
    const uint32_t* const lengths = identifiers->lengths + begin;
    const uint32_t* const tokens = identifiers->tokens + begin;
    uint64_t hashes[64];
    __m128i firsts[64];
    uint64_t slow = 0;
    uint64_t deferred = 0;
    uint64_t long_candidates = 0;
    (void)batch;

    for (uint32_t j = 0; j < count; j += 1)
    {
        // 1. Load, check and hash; prefetch the bucket. A length past 64 loads 64 bytes (bzhi keeps every bit for an index
        // of 64 or more, and the length is capped below 256), whose hash is unused: the identifier is slow.
        const uint32_t capped = lengths[j] < BUSTER_INTERN_VECTOR_BYTES ? lengths[j] : BUSTER_INTERN_VECTOR_BYTES;
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, capped), source + starts[j]);
        __mmask64 special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\'));
        if (trigraphs)
        {
            special |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('?'));
        }
        const uint64_t is_slow = (special != 0) | (capped > BUSTER_INTERN_FAST_LENGTH);
        slow |= is_slow << j;
        const uint64_t hash = intern_hash_vector(bytes, capped);
        hashes[j] = hash;
        firsts[j] = _mm512_castsi512_si128(bytes);
        if (fused)
        {
            // 2, at once.
            const uint8_t* bucket_bytes = buckets + (hash & bucket_mask) * BUSTER_INTERN_BUCKET_BYTES;
            const uint32_t matches = intern_bucket_matches(_mm512_load_si512(bucket_bytes), intern_bucket_key(_mm512_castsi512_si128(bytes)));
            const uint32_t bit = (uint32_t)__builtin_ctz(matches | 0x10000) & 15;
            values[tokens[j]] = intern_slot_id(bucket_bytes, bit);
            const uint64_t candidate = (matches != 0) & !is_slow;
            long_candidates |= (candidate & (capped > BUSTER_INTERN_SHORT_LENGTH)) << j;
            deferred |= (candidate ^ 1) << j;
        }
        else
        {
            _mm_prefetch((const char*)(buckets + (hash & bucket_mask) * BUSTER_INTERN_BUCKET_BYTES), _MM_HINT_T0);
        }
    }

    // 2. Probe each identifier's bucket for its prefix, with one compare. No branch depends on the outcome: a match of a
    // short identifier is found, a match of a longer one is a candidate for 2b, and no match is deferred.
    for (uint32_t j = 0; !fused && j < count; j += 1)
    {
        const uint8_t* bucket_bytes = buckets + (hashes[j] & bucket_mask) * BUSTER_INTERN_BUCKET_BYTES;
        const uint32_t capped = lengths[j] < BUSTER_INTERN_VECTOR_BYTES ? lengths[j] : BUSTER_INTERN_VECTOR_BYTES;
        const uint32_t matches = intern_bucket_matches(_mm512_load_si512(bucket_bytes), intern_bucket_key(firsts[j]));
        const uint32_t bit = (uint32_t)__builtin_ctz(matches | 0x10000) & 15;
        values[tokens[j]] = intern_slot_id(bucket_bytes, bit);
        const uint64_t candidate = (matches != 0) & !((slow >> j) & 1);
        long_candidates |= (candidate & (capped > BUSTER_INTERN_SHORT_LENGTH)) << j;
        deferred |= (candidate ^ 1) << j;
    }

    // 2b. A longer identifier whose prefix matched: one masked compare of its bytes and terminating zero with its record.
    for (uint64_t candidates = long_candidates; candidates != 0; candidates &= candidates - 1)
    {
        const uint32_t j = (uint32_t)__builtin_ctzll(candidates);
        const uint32_t capped = lengths[j];
        const __mmask64 compare_mask = _bzhi_u64(~0ull, capped + 1);
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, capped), source + starts[j]);
        const __m512i record = _mm512_maskz_loadu_epi8(compare_mask, records + record_of[values[tokens[j]]]);
        deferred |= (uint64_t)(_mm512_mask_cmpneq_epi8_mask(compare_mask, bytes, record) != 0) << j;
    }

    // 3. The deferred ones, in token order: new spellings get the ids the scalar reference gives them. A fast one reuses
    // its hash.
    bool result = true;
    for (; result && deferred != 0; deferred &= deferred - 1)
    {
        const uint32_t j = (uint32_t)__builtin_ctzll(deferred);
        IdentId id;
        if ((slow >> j) & 1)
        {
            id = refine_intern_identifier(table, source, length, starts[j], lengths[j], flags);
        }
        else
        {
            id = intern_find_or_add_hashed(table, source + starts[j], lengths[j], hashes[j]);
        }
        values[tokens[j]] = id;
        result = id != 0;
    }
    return result;
}

// The vectorized path: an instantiation per trigraph setting (so that neither tests the flag per identifier), batch size
// and form of pass B.
BUSTER_INLINE bool refine_tokens_with(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends,
                                      uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags, const bool trigraphs, const uint32_t batch, const bool fused)
{
    RefineIdentifiers identifiers;
    __m512i previous_ends = _mm512_setzero_si512();
    Scratch scratch = scratch_begin(0);
    uint64_t* backslash_chunks = arena_push_array(scratch.arena, (length + 4095) / 4096 + 1, sizeof(uint64_t), 64);
    bool result = backslash_chunks != 0;
    if (result)
    {
        refine_backslash_chunks(source, length, backslash_chunks, trigraphs);
    }
    for (uint64_t block = 0; result && block < count; block += BUSTER_REFINE_BLOCK_TOKENS)
    {
        const uint64_t block_end = count - block < BUSTER_REFINE_BLOCK_TOKENS ? count : block + BUSTER_REFINE_BLOCK_TOKENS;
        previous_ends = refine_pass_a(source, length, kinds, starts, ends, block, block_end, values, space, flags, backslash_chunks, previous_ends, &identifiers);
        for (uint32_t first = 0; result && first < identifiers.count; first += batch)
        {
            const uint32_t batch_count = identifiers.count - first < batch ? identifiers.count - first : batch;
            result = refine_intern_batch(table, source, length, &identifiers, first, batch_count, values, flags, trigraphs, batch, fused);
        }
    }
    scratch_end(scratch);
    return result;
}

// The variants the benchmark compares: pass B with `batch` identifiers per batch, or fused.
typedef enum RefineVariant
{
    REFINE_VARIANT_BATCH_16,
    REFINE_VARIANT_BATCH_32,
    REFINE_VARIANT_BATCH_64,
    REFINE_VARIANT_FUSED_64,
    REFINE_VARIANT_COUNT,
} RefineVariant;

static const char* const refine_variant_names[REFINE_VARIANT_COUNT] = { "refine_tokens_16", "refine_tokens_32", "refine_tokens_64", "refine_tokens_fused" };

#define BUSTER_REFINE_INSTANTIATE(name, trigraphs, batch, fused) \
    __attribute__((noinline)) static bool name(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, \
                                               const uint32_t* ends, uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags) \
    { \
        return refine_tokens_with(table, source, length, kinds, starts, ends, count, values, space, flags, trigraphs, batch, fused); \
    }

BUSTER_REFINE_INSTANTIATE(refine_tokens_16_plain, false, 16, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_16_trigraphs, true, 16, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_32_plain, false, 32, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_32_trigraphs, true, 32, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_64_plain, false, 64, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_64_trigraphs, true, 64, false)
BUSTER_REFINE_INSTANTIATE(refine_tokens_fused_plain, false, 64, true)
BUSTER_REFINE_INSTANTIATE(refine_tokens_fused_trigraphs, true, 64, true)

// refine_tokens with a variant of pass B.
static bool refine_tokens_variant(RefineVariant variant, InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts,
                                  const uint32_t* ends, uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags)
{
    const bool trigraphs = (flags & C_TOKENIZER_TRIGRAPHS) != 0;
    bool result = false;
    switch (variant)
    {
        case REFINE_VARIANT_BATCH_16:
            result = (trigraphs ? refine_tokens_16_trigraphs : refine_tokens_16_plain)(table, source, length, kinds, starts, ends, count, values, space, flags);
            break;
        case REFINE_VARIANT_BATCH_32:
            result = (trigraphs ? refine_tokens_32_trigraphs : refine_tokens_32_plain)(table, source, length, kinds, starts, ends, count, values, space, flags);
            break;
        case REFINE_VARIANT_BATCH_64:
            result = (trigraphs ? refine_tokens_64_trigraphs : refine_tokens_64_plain)(table, source, length, kinds, starts, ends, count, values, space, flags);
            break;
        case REFINE_VARIANT_FUSED_64:
            result = (trigraphs ? refine_tokens_fused_trigraphs : refine_tokens_fused_plain)(table, source, length, kinds, starts, ends, count, values, space, flags);
            break;
        case REFINE_VARIANT_COUNT:
            break;
    }
    return result;
}

// Refines `count` tokens of `source` like refine_tokens_scalar, with AVX-512 (see the top of the file).
static __attribute__((unused)) bool refine_tokens(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends,
                          uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags)
{
    return refine_tokens_variant(REFINE_VARIANT_BATCH_64, table, source, length, kinds, starts, ends, count, values, space, flags);
}
