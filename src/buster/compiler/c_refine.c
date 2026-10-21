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
// refine_tokens works on blocks of BUSTER_REFINE_BLOCK_TOKENS tokens. Before a block, refine_scan_chunks marks the
// 64-byte chunks of its source bytes that hold a backslash (with trigraphs, a '?' too), 4 KB at a time and just before
// the block's passes read those bytes. Then three passes:
//   A. Per 16 tokens: the gap bits (starts[i] != ends[i - 1], one compare), the identifiers' starts and lengths
//      compressed (vpcompressd) into dense arrays, and per 64 a mask of the identifiers. A gap that is only line splices
//      needs a backslash, so a step of 64 tokens whose gaps lie in chunks without one is done; the others go to
//      refine_step_splices, where a non-first-of-line token whose gap ends in a newline byte (only a line splice can do
//      that) has the scalar refine_gap_is_splices decide its bit. Each step also prefetches the next block's step of
//      the columns and some source bytes past the scan: the lexer's pass over a whole file has evicted them.
//   B. refine_pass_b interns the block's identifiers in batches of 64 (RefineVariant has the other sizes the benchmark
//      compares), pipelined: a batch's stage 1 runs in the middle of the batch before's stages 2 to 3.
//      1. refine_hash_batch reads each identifier's first 32 bytes with a masked load (never outside the token, so
//         never outside the source), hashes four at a time (intern_hash_vectors4_short; the rare longer ones again,
//         whole) and prefetches their buckets. A batch whose loads had a backslash checks each identifier for one
//         (refine_batch_specials): those, and identifiers past BUSTER_INTERN_FAST_LENGTH, go to the scalar key.
//      2. Each bucket's four slots are compared with the identifier's first 12 bytes in one compare (refine_probe),
//         which identifies a spelling of up to 11 bytes outright. The bookkeeping (slow, unmatched, longer) is 16
//         identifiers at a time from their lengths and ids, as bits of masks.
//      2b. A longer identifier's match is confirmed with one masked compare of its record's bytes and terminating
//         zero, its record prefetched from its span before the next batch's stage 1 and compared after it.
//      3. Nothing above branches on an identifier's outcome: one not found this way (a new spelling, a second
//         candidate, a probe past its first bucket, or the scalar path) is deferred, and the deferred ones are interned
//         in token order (intern_find_or_add_vector, from stage 1's bucket), so that new spellings get the same ids
//         as refine_tokens_scalar gives them.
//   C. The block's ids, from the dense array, into its identifiers' values (vpexpandd, a masked store per 16 tokens).
//
// main.c includes this file after c_lexer.c (whose scalar lexer it uses for splices and UCNs), lane.c and
// c_intern.c; every function is static. Layout, in this order: the identifier key and end (refine_encode_utf8,
// refine_identifier_key, refine_identifier_end, refine_gap_is_splices, refine_intern_identifier),
// refine_tokens_scalar, then the vectorized path (RefineIdentifiers, refine_newlines_before, refine_scan_chunks,
// refine_chunks_have_backslash, refine_step_splices, refine_pass_a, refine_key, refine_probe, refine_batch_specials,
// refine_intern_batch (the fused variant's pass B), RefineHashes, refine_hash_batch, refine_pass_b, refine_pass_c,
// the instantiations, refine_tokens_with, RefineVariant, refine_tokens_variant and refine_tokens).

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
// The source bytes pass A prefetches per step, ahead of the scan: 16 steps' worth, 8 KB, is about a block's.
#define BUSTER_REFINE_PREFETCH_SOURCE 512

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

// What pass A collects about a block's identifiers for pass B, and what pass B gives back for pass C: dense arrays with
// an entry per identifier, in token order.
typedef struct RefineIdentifiers RefineIdentifiers;
struct RefineIdentifiers
{
    uint32_t starts[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    uint32_t lengths[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    // Pass B's ids, which pass C stores into the identifiers' values.
    uint32_t ids[BUSTER_REFINE_BLOCK_TOKENS + BUSTER_REFINE_DENSE_SLACK] __attribute__((aligned(64)));
    // A word per step of BUSTER_REFINE_STEP_TOKENS tokens: bit i is set when the step's token i is an identifier.
    uint64_t masks[BUSTER_REFINE_BLOCK_TOKENS / BUSTER_REFINE_STEP_TOKENS];
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

// The source's 64-byte chunks that hold a backslash (or, with trigraphs, a '?': "??/" is one), a word of 64 chunks (4 KB
// of the source) at a time from word `word` on, until the words cover the source's first `end` bytes: bit c % 64 of
// `chunks[c / 64]` for chunk c. A line splice needs a backslash, so a gap in chunks without one isn't only splices.
// `chunks` needs (length + 4095) / 4096 words. Returns the next word to scan. refine_tokens_with scans each block's
// bytes just before the block, so that its passes find them in the caches. A run of 8 chunks is first tested whole, by
// the least of its bytes XORed with '\\' (and with '?'), and only a run that has one (5% of sqlite3.c's) is tested chunk
// by chunk.
BUSTER_INLINE uint64_t refine_scan_chunks(const uint8_t* source, uint64_t length, uint64_t* chunks, uint64_t word, uint64_t end, const bool trigraphs)
{
    const uint64_t words = (end + 4095) / 4096;
    for (; word < words; word += 1)
    {
        const uint8_t* bytes = source + word * 4096;
        const uint64_t available = length - word * 4096;
        uint64_t found = 0;
        if (available >= 4096)
        {
            for (uint32_t run = 0; run < 64; run += 8)
            {
                const uint8_t* run_bytes = bytes + run * 64;
                __m512i least = _mm512_set1_epi8(-1);
                for (uint32_t chunk = 0; chunk < 8; chunk += 1)
                {
                    const __m512i chunk_bytes = _mm512_loadu_si512(run_bytes + chunk * 64);
                    least = _mm512_min_epu8(least, _mm512_xor_si512(chunk_bytes, _mm512_set1_epi8('\\')));
                    if (trigraphs)
                    {
                        least = _mm512_min_epu8(least, _mm512_xor_si512(chunk_bytes, _mm512_set1_epi8('?')));
                    }
                }
                if (BUSTER_UNLIKELY(_mm512_testn_epi8_mask(least, least) != 0))
                {
                    for (uint32_t chunk = 0; chunk < 8; chunk += 1)
                    {
                        const __m512i chunk_bytes = _mm512_loadu_si512(run_bytes + chunk * 64);
                        uint64_t special = _mm512_cmpeq_epi8_mask(chunk_bytes, _mm512_set1_epi8('\\'));
                        if (trigraphs)
                        {
                            special |= _mm512_cmpeq_epi8_mask(chunk_bytes, _mm512_set1_epi8('?'));
                        }
                        found |= (uint64_t)(special != 0) << (run + chunk);
                    }
                }
            }
        }
        else
        {
            // The source's last word: masked loads, which read nothing past its end.
            for (uint32_t chunk = 0; (uint64_t)chunk * 64 < available; chunk += 1)
            {
                const uint64_t remaining = available - (uint64_t)chunk * 64;
                const __m512i chunk_bytes = _mm512_maskz_loadu_epi8(remaining >= 64 ? ~0ull : _bzhi_u64(~0ull, (uint32_t)remaining), bytes + chunk * 64);
                uint64_t special = _mm512_cmpeq_epi8_mask(chunk_bytes, _mm512_set1_epi8('\\'));
                if (trigraphs)
                {
                    special |= _mm512_cmpeq_epi8_mask(chunk_bytes, _mm512_set1_epi8('?'));
                }
                found |= (uint64_t)(special != 0) << chunk;
            }
        }
        chunks[word] = found;
    }
    return word;
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

// The leading-space bits of a step of 64 tokens from `step` (`step_count` of them) whose gaps may hold line splices (a
// chunk they lie in has a backslash): the gap bits `gaps`, less those of the tokens whose gap is only splices. Those are
// the tokens that aren't the first of their line whose gap ends in a newline byte (a newline outside comments starts a
// logical line, and a comment ends with "*/"), and the scalar refine_gap_is_splices decides each. `gaps_begin` is the
// end of the token before the step. Out of line: most steps' gaps have no backslash.
static __attribute__((noinline)) uint64_t refine_step_splices(const uint8_t* source, uint64_t length, const uint32_t* starts, const uint32_t* ends, uint64_t step,
                                                              uint64_t step_count, uint32_t gaps_begin, uint64_t start_of_line, uint64_t gaps, uint32_t flags)
{
    uint64_t result = gaps;
    for (uint32_t group = 0; (uint64_t)group * BUSTER_REFINE_LANES < step_count; group += 1)
    {
        const uint64_t first = step + group * BUSTER_REFINE_LANES;
        const __mmask16 lanes = (__mmask16)(_bzhi_u64(~0ull, (uint32_t)step_count) >> (group * BUSTER_REFINE_LANES));
        const uint32_t continued = (uint32_t)(uint16_t)((gaps & ~start_of_line) >> (group * BUSTER_REFINE_LANES));
        uint32_t splices = 0;
        if (continued != 0)
        {
            const __m512i start_vector = _mm512_maskz_loadu_epi32(lanes, starts + first);
            splices = continued & refine_newlines_before(source, length, start_vector, starts + first, lanes);
        }
        for (; splices != 0; splices &= splices - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(splices);
            const uint32_t gap_start = first + lane == step ? gaps_begin : ends[first + lane - 1];
            const bool only_splices = refine_gap_is_splices(source, length, gap_start, starts[first + lane], flags);
            result &= ~((uint64_t)only_splices << (group * BUSTER_REFINE_LANES + lane));
        }
    }
    return result;
}

// Pass A over tokens [begin, end) of the block (a multiple of 64 from `begin`, but at the source's last tokens):
// the leading-space words, `values` for every token when it isn't `ends` (`separate`: the ends; pass C overwrites the
// identifiers'), the dense identifier arrays and the steps' identifier masks. `previous_ends` carries the ends of the 16
// tokens before `begin` (lane 15 is the last one's), read before pass C could overwrite them in place. A step of 64
// tokens whose gaps hold no backslash (most: `backslash_chunks`, from refine_scan_chunks) is done with its gap bits; the
// others go to refine_step_splices.
BUSTER_INLINE __m512i refine_pass_a(const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends, uint64_t begin,
                                    uint64_t end, uint32_t* values, uint64_t* space, uint32_t flags, const uint64_t* backslash_chunks, __m512i previous_ends,
                                    RefineIdentifiers* identifiers, uint64_t count, uint64_t scanned_bytes, const bool separate)
{
    uint32_t identifier_count = 0;
    for (uint64_t step = begin; step < end; step += BUSTER_REFINE_STEP_TOKENS)
    {
        // The next block's step in the token columns, and BUSTER_REFINE_PREFETCH_SOURCE bytes of the source past what the
        // scan has read: fetched from memory while this block's pass B runs, so that the next block finds them in the
        // caches (the columns were written a whole file before, and the lexer's pass over the source evicted them).
        const uint64_t ahead = step + BUSTER_REFINE_BLOCK_TOKENS;
        if (ahead < count)
        {
            _mm_prefetch((const char*)(kinds + ahead), _MM_HINT_T0);
            for (uint32_t line = 0; line < BUSTER_REFINE_STEP_TOKENS * sizeof(uint32_t); line += 64)
            {
                _mm_prefetch((const char*)starts + ahead * sizeof(uint32_t) + line, _MM_HINT_T0);
                _mm_prefetch((const char*)ends + ahead * sizeof(uint32_t) + line, _MM_HINT_T0);
            }
        }
        const uint64_t source_ahead = scanned_bytes + (step - begin) / BUSTER_REFINE_STEP_TOKENS * BUSTER_REFINE_PREFETCH_SOURCE;
        for (uint64_t line = 0; line < BUSTER_REFINE_PREFETCH_SOURCE && source_ahead + line < length; line += 64)
        {
            _mm_prefetch((const char*)(source + source_ahead + line), _MM_HINT_T0);
        }
        const uint64_t remaining = end - step;
        const uint64_t step_count = remaining >= BUSTER_REFINE_STEP_TOKENS ? BUSTER_REFINE_STEP_TOKENS : remaining;
        const __mmask64 valid = _bzhi_u64(~0ull, (uint32_t)step_count);
        const __m512i kind_vector = _mm512_maskz_loadu_epi8(valid, kinds + step);
        const __mmask64 identifier = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_and_si512(kind_vector, _mm512_set1_epi8(0x7F)), _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        identifiers->masks[(step - begin) / BUSTER_REFINE_STEP_TOKENS] = identifier;
        // The step's gaps lie between the end of the token before it and the start of its last token.
        const uint32_t gaps_begin = (uint32_t)_mm_extract_epi32(_mm512_extracti32x4_epi32(previous_ends, 3), 3);
        uint64_t gaps = 0;
        // Groups past the source's last token load nothing and add nothing (their lanes are 0).
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const uint64_t first = step + group * BUSTER_REFINE_LANES;
            const __mmask16 lanes = (__mmask16)(valid >> (group * BUSTER_REFINE_LANES));
            const __m512i start_vector = _mm512_maskz_loadu_epi32(lanes, starts + first);
            const __m512i end_vector = _mm512_maskz_loadu_epi32(lanes, ends + first);
            const __m512i before_ends = _mm512_alignr_epi32(end_vector, previous_ends, 15);
            gaps |= (uint64_t)_mm512_mask_cmpneq_epi32_mask(lanes, start_vector, before_ends) << (group * BUSTER_REFINE_LANES);
            if (separate)
            {
                _mm512_mask_storeu_epi32(values + first, lanes, end_vector);
            }
            const __mmask16 identifiers_here = (__mmask16)(identifier >> (group * BUSTER_REFINE_LANES));
            const __m512i lengths = _mm512_sub_epi32(end_vector, start_vector);
            _mm512_storeu_si512(identifiers->starts + identifier_count, _mm512_maskz_compress_epi32(identifiers_here, start_vector));
            _mm512_storeu_si512(identifiers->lengths + identifier_count, _mm512_maskz_compress_epi32(identifiers_here, lengths));
            identifier_count += (uint32_t)__builtin_popcount(identifiers_here);
            previous_ends = end_vector;
        }
        if (BUSTER_UNLIKELY(refine_chunks_have_backslash(backslash_chunks, gaps_begin, starts[step + step_count - 1])))
        {
            gaps = refine_step_splices(source, length, starts, ends, step, step_count, gaps_begin, _mm512_movepi8_mask(kind_vector), gaps, flags);
        }
        space[step / 64] = gaps;
    }
    identifiers->count = identifier_count;
    return previous_ends;
}

// A probe's key: the spelling's first 16 bytes (zero past its end) with the last dword all ones, which no slot's id is
// (ids are below BUSTER_INTERN_MAX_IDS), so that a compare of every dword finds only the prefixes' matches.
BUSTER_INLINE __m128i refine_key(__m128i first)
{
    return _mm_or_si128(first, _mm_setr_epi32(0, 0, 0, -1));
}

// The id in the slot of `bucket` whose prefix is the key's (refine_key's), or 0 if no slot's is: the first match, which
// identifies a spelling of up to BUSTER_INTERN_SHORT_LENGTH bytes and is a candidate for a longer one. The matches are
// intern_bucket_matches', from a compare without its mask. No branch depends on the outcome: without a match, the id
// read (the first slot's first dword, tzcnt's 32 masked to 0) is discarded.
BUSTER_INLINE IdentId refine_probe(const uint8_t* bucket, __m128i key)
{
    const uint32_t matches = (_mm512_cmpeq_epi32_mask(_mm512_load_si512(bucket), intern_bucket_key(key)) + 0x1111) & 0x8888;
    IdentId id = intern_slot_id(bucket, _tzcnt_u32(matches) & 15);
    // A select after the load, not a branch around it, which Clang makes of a plain `?:` (with the matches' zero as the
    // result): a branch on the outcome would mispredict for every new spelling.
    __asm__("" : "+r"(id));
    return id & (uint32_t)-(int32_t)(matches != 0);
}

// The identifiers of a batch that have a backslash (or, with trigraphs, a '?'): bit j for identifier j. Out of line,
// since stage 1 calls it only when one of the batch's loads had such a byte, which no identifier in the C corpora has.
static __attribute__((noinline)) uint64_t refine_batch_specials(const uint8_t* source, const uint32_t* starts, const uint32_t* lengths, uint32_t count, bool trigraphs)
{
    uint64_t result = 0;
    for (uint32_t j = 0; j < count; j += 1)
    {
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, lengths[j]), source + starts[j]);
        uint64_t special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\'));
        if (trigraphs)
        {
            special |= _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('?'));
        }
        result |= (uint64_t)(special != 0) << j;
    }
    return result;
}

// Pass B for identifiers [begin, begin + count) of the dense arrays (count <= `batch`, at most 64): stages 1 to 3, with
// each identifier's id in `identifiers->ids`. `fused` merges stages 1 and 2 (each identifier probed right after it is
// hashed, without a prefetch). The table's arrays are read once per batch into locals (stage 3 may grow it), so that the
// stores of ids can't make Clang read them again for every identifier. The bookkeeping (which identifiers are slow,
// unmatched or candidates) is done 16 identifiers at a time, from their lengths and ids, as bits of masks; stage 3 goes
// through the deferred ones in token order. Returns false if the intern table runs out of room.
BUSTER_INLINE bool refine_intern_batch(InternTable* table, const uint8_t* source, uint64_t length, RefineIdentifiers* identifiers, uint32_t begin, uint32_t count,
                                       uint32_t flags, const bool trigraphs, const bool fused)
{
    const uint8_t* const buckets = table->buckets;
    const uint8_t* const records = table->records;
    const InternSpan* const spans = table->spans;
    const uint64_t bucket_mask = table->bucket_mask;
    const uint64_t offset_mask = bucket_mask * BUSTER_INTERN_BUCKET_BYTES;
    const uint32_t* const starts = identifiers->starts + begin;
    const uint32_t* const lengths = identifiers->lengths + begin;
    uint32_t* const ids = identifiers->ids + begin;
    // Each identifier's bucket offset, in every other qword (stage 1 stores four at a time from
    // intern_hash_vectors4_short), and its key (refine_key).
    uint64_t offsets[2 * 64] __attribute__((aligned(64)));
    __m128i firsts[64] __attribute__((aligned(64)));
    // Per byte position, the least of every loaded byte XORed with '\\' (and with '?'): a zero byte means some identifier
    // has one.
    __m512i backslashes = _mm512_set1_epi8(-1);
    __m512i questions = _mm512_set1_epi8(-1);

    // 1. Load and hash; prefetch the bucket. bzhi keeps every bit for an index of 64 to 255 and the low bits of a longer
    // one's, so a load never reads past its identifier; a length past BUSTER_INTERN_FAST_LENGTH makes the identifier
    // slow, and its hash is unused.
    // Four at a time, each identifier's first 32 bytes are hashed (intern_hash_vectors4_short): all of it but in the rare
    // longer ones, which are hashed again from all their bytes after.
    const __m512i zero_lanes = intern_hash_zero_lanes();
    uint32_t j = 0;
    for (; !fused && j + 4 <= count; j += 4)
    {
        __m256i bytes[4];
        for (uint32_t k = 0; k < 4; k += 1)
        {
            bytes[k] = _mm256_maskz_loadu_epi8(_bzhi_u32(~0u, lengths[j + k]), source + starts[j + k]);
        }
        const __m512i ab = _mm512_inserti64x4(_mm512_castsi256_si512(bytes[0]), bytes[1], 1);
        const __m512i cd = _mm512_inserti64x4(_mm512_castsi256_si512(bytes[2]), bytes[3], 1);
        backslashes = _mm512_min_epu8(backslashes, _mm512_min_epu8(_mm512_xor_si512(ab, _mm512_set1_epi8('\\')), _mm512_xor_si512(cd, _mm512_set1_epi8('\\'))));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_min_epu8(_mm512_xor_si512(ab, _mm512_set1_epi8('?')), _mm512_xor_si512(cd, _mm512_set1_epi8('?'))));
        }
        // The four lengths in dwords 0, 4, 8 and 12.
        const __m512i spread = _mm512_setr_epi32(0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0);
        const __m512i group_lengths = _mm512_maskz_permutexvar_epi32(0x1111, spread, _mm512_castsi128_si512(_mm_loadu_si128((const __m128i*)(lengths + j))));
        __m512i first_lanes;
        const __m512i hashes = intern_hash_vectors4_short(ab, cd, group_lengths, zero_lanes, &first_lanes);
        _mm512_store_si512(firsts + j, _mm512_or_si512(first_lanes, _mm512_set4_epi32(-1, 0, 0, 0)));
        const __m512i group_offsets = _mm512_and_si512(_mm512_slli_epi64(hashes, 6), _mm512_set1_epi64((long long)offset_mask));
        _mm512_store_si512(offsets + 2 * j, group_offsets);
        // The prefetches' offsets come from the vector's lanes: loads of them from the store just made would wait for it
        // to commit (it doesn't forward to them), and hold up the batch.
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_castsi512_si128(group_offsets))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 1))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 2))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 3))), _MM_HINT_T0);
    }
    // Those of 33 bytes or more among them: their whole loads, hashes and prefetches (a longer one than
    // BUSTER_INTERN_FAST_LENGTH is slow, and its hash unused, but its bytes are checked all the same).
    uint64_t longer_than_32 = 0;
    for (uint32_t group = 0; group < j; group += BUSTER_REFINE_LANES)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, j - group);
        longer_than_32 |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, lengths + group), _mm512_set1_epi32(32)) << group;
    }
    for (; longer_than_32 != 0; longer_than_32 &= longer_than_32 - 1)
    {
        const uint32_t i = (uint32_t)__builtin_ctzll(longer_than_32);
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, lengths[i]), source + starts[i]);
        backslashes = _mm512_min_epu8(backslashes, _mm512_xor_si512(bytes, _mm512_set1_epi8('\\')));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_xor_si512(bytes, _mm512_set1_epi8('?')));
        }
        const uint64_t offset = (intern_hash_vector(bytes, lengths[i]) * BUSTER_INTERN_BUCKET_BYTES) & offset_mask;
        offsets[2 * i] = offset;
        _mm_prefetch((const char*)(buckets + offset), _MM_HINT_T0);
    }
    for (; j < count; j += 1)
    {
        const uint32_t identifier_length = lengths[j];
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, identifier_length), source + starts[j]);
        backslashes = _mm512_min_epu8(backslashes, _mm512_xor_si512(bytes, _mm512_set1_epi8('\\')));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_xor_si512(bytes, _mm512_set1_epi8('?')));
        }
        const uint64_t offset = (intern_hash_vector(bytes, identifier_length) * BUSTER_INTERN_BUCKET_BYTES) & offset_mask;
        offsets[2 * j] = offset;
        if (fused)
        {
            // 2, at once.
            ids[j] = refine_probe(buckets + offset, refine_key(_mm512_castsi512_si128(bytes)));
        }
        else
        {
            firsts[j] = refine_key(_mm512_castsi512_si128(bytes));
            _mm_prefetch((const char*)(buckets + offset), _MM_HINT_T0);
        }
    }

    // 2. Probe each identifier's bucket for its prefix, with one compare: four per iteration, then the rest.
    uint32_t probed = 0;
    for (; !fused && probed + 4 <= count; probed += 4)
    {
        ids[probed] = refine_probe(buckets + offsets[2 * probed], firsts[probed]);
        ids[probed + 1] = refine_probe(buckets + offsets[2 * probed + 2], firsts[probed + 1]);
        ids[probed + 2] = refine_probe(buckets + offsets[2 * probed + 4], firsts[probed + 2]);
        ids[probed + 3] = refine_probe(buckets + offsets[2 * probed + 6], firsts[probed + 3]);
    }
    for (; !fused && probed < count; probed += 1)
    {
        ids[probed] = refine_probe(buckets + offsets[2 * probed], firsts[probed]);
    }

    // Slow: longer than BUSTER_INTERN_FAST_LENGTH, or with a backslash (or, with trigraphs, a '?'). Unmatched: no
    // slot had the prefix. Longer: past BUSTER_INTERN_SHORT_LENGTH, so that a match is only a candidate.
    uint64_t slow = 0;
    uint64_t unmatched = 0;
    uint64_t longer = 0;
    for (uint32_t group = 0; group < count; group += BUSTER_REFINE_LANES)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, count - group);
        const __m512i group_lengths = _mm512_maskz_loadu_epi32(lanes, lengths + group);
        const __m512i group_ids = _mm512_maskz_loadu_epi32(lanes, ids + group);
        slow |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, group_lengths, _mm512_set1_epi32(BUSTER_INTERN_FAST_LENGTH)) << group;
        longer |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, group_lengths, _mm512_set1_epi32(BUSTER_INTERN_SHORT_LENGTH)) << group;
        unmatched |= (uint64_t)_mm512_mask_testn_epi32_mask(lanes, group_ids, group_ids) << group;
    }
    uint64_t special = _mm512_testn_epi8_mask(backslashes, backslashes);
    if (trigraphs)
    {
        special |= _mm512_testn_epi8_mask(questions, questions);
    }
    if (BUSTER_UNLIKELY(special != 0))
    {
        slow |= refine_batch_specials(source, starts, lengths, count, trigraphs);
    }
    uint64_t deferred = slow | unmatched;

    // 2b. A longer identifier whose prefix matched: one masked compare of its bytes and terminating zero with its record.
    for (uint64_t candidates = longer & ~deferred; candidates != 0; candidates &= candidates - 1)
    {
        const uint32_t j = (uint32_t)__builtin_ctzll(candidates);
        const uint32_t identifier_length = lengths[j];
        const __mmask64 compare_mask = _bzhi_u64(~0ull, identifier_length + 1);
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, identifier_length), source + starts[j]);
        const __m512i record = _mm512_maskz_loadu_epi8(compare_mask, records + spans[ids[j]].offset);
        deferred |= (uint64_t)(_mm512_mask_cmpneq_epi8_mask(compare_mask, bytes, record) != 0) << j;
    }

    // 3. The deferred ones, in token order: new spellings get the ids the scalar reference gives them. Each starts its
    // probe at the bucket stage 1 found (unless an insert has grown the table since).
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
            id = intern_find_or_add_vector(table, source + starts[j], lengths[j], bucket_mask, offsets[2 * j] / BUSTER_INTERN_BUCKET_BYTES);
        }
        ids[j] = id;
        result = id != 0;
    }
    return result;
}

// Stage 1's results for a batch, which its stages 2 to 3 read: each identifier's bucket offset (in every other qword:
// stage 1 stores four at a time from intern_hash_vectors4_short) and key (refine_key), the table's bucket mask they are
// for, and whether one of the batch's loads had a backslash (or, with trigraphs, a '?').
typedef struct RefineHashes RefineHashes;
struct RefineHashes
{
    uint64_t offsets[2 * 64] __attribute__((aligned(64)));
    __m128i keys[64] __attribute__((aligned(64)));
    uint64_t bucket_mask;
    bool special;
};

// Stage 1 for `count` identifiers (at most 64) of the dense arrays from `starts` and `lengths`: each identifier loaded,
// hashed and its bucket prefetched. bzhi keeps every bit for an index of 64 to 255 and the low bits of a longer one's, so
// a load never reads past its identifier; a length past BUSTER_INTERN_FAST_LENGTH makes the identifier slow, and its
// hash is unused. Four at a time, each identifier's first 32 bytes are hashed (intern_hash_vectors4_short): all of it
// but in the rare longer ones, which are hashed again from all their bytes after; the rest one at a time.
BUSTER_INLINE void refine_hash_batch(const InternTable* table, const uint8_t* source, const uint32_t* starts, const uint32_t* lengths, uint32_t count,
                                     RefineHashes* hashes, const bool trigraphs)
{
    const uint8_t* const buckets = table->buckets;
    const uint64_t offset_mask = table->bucket_mask * BUSTER_INTERN_BUCKET_BYTES;
    uint64_t* const offsets = hashes->offsets;
    __m128i* const keys = hashes->keys;
    // Per byte position, the least of every loaded byte XORed with '\\' (and with '?'): a zero byte means some identifier
    // has one.
    __m512i backslashes = _mm512_set1_epi8(-1);
    __m512i questions = _mm512_set1_epi8(-1);
    const __m512i zero_lanes = intern_hash_zero_lanes();
    uint32_t j = 0;
    for (; j + 4 <= count; j += 4)
    {
        __m256i bytes[4];
        for (uint32_t k = 0; k < 4; k += 1)
        {
            bytes[k] = _mm256_maskz_loadu_epi8(_bzhi_u32(~0u, lengths[j + k]), source + starts[j + k]);
        }
        const __m512i ab = _mm512_inserti64x4(_mm512_castsi256_si512(bytes[0]), bytes[1], 1);
        const __m512i cd = _mm512_inserti64x4(_mm512_castsi256_si512(bytes[2]), bytes[3], 1);
        backslashes = _mm512_min_epu8(backslashes, _mm512_min_epu8(_mm512_xor_si512(ab, _mm512_set1_epi8('\\')), _mm512_xor_si512(cd, _mm512_set1_epi8('\\'))));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_min_epu8(_mm512_xor_si512(ab, _mm512_set1_epi8('?')), _mm512_xor_si512(cd, _mm512_set1_epi8('?'))));
        }
        // The four lengths in dwords 0, 4, 8 and 12.
        const __m512i spread = _mm512_setr_epi32(0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0);
        const __m512i group_lengths = _mm512_maskz_permutexvar_epi32(0x1111, spread, _mm512_castsi128_si512(_mm_loadu_si128((const __m128i*)(lengths + j))));
        __m512i first_lanes;
        const __m512i group_hashes = intern_hash_vectors4_short(ab, cd, group_lengths, zero_lanes, &first_lanes);
        _mm512_store_si512(keys + j, _mm512_or_si512(first_lanes, _mm512_set4_epi32(-1, 0, 0, 0)));
        const __m512i group_offsets = _mm512_and_si512(_mm512_slli_epi64(group_hashes, 6), _mm512_set1_epi64((long long)offset_mask));
        _mm512_store_si512(offsets + 2 * j, group_offsets);
        // The prefetches' offsets come from the vector's lanes: loads of them from the store just made would wait for it
        // to commit (it doesn't forward to them).
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_castsi512_si128(group_offsets))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 1))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 2))), _MM_HINT_T0);
        _mm_prefetch((const char*)(buckets + _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(group_offsets, 3))), _MM_HINT_T0);
    }
    // Those of 33 bytes or more among them: their whole loads, hashes and prefetches (a longer one than
    // BUSTER_INTERN_FAST_LENGTH is slow, and its hash unused, but its bytes are checked all the same).
    uint64_t longer_than_32 = 0;
    for (uint32_t group = 0; group < j; group += BUSTER_REFINE_LANES)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, j - group);
        longer_than_32 |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, lengths + group), _mm512_set1_epi32(32)) << group;
    }
    for (; longer_than_32 != 0; longer_than_32 &= longer_than_32 - 1)
    {
        const uint32_t i = (uint32_t)__builtin_ctzll(longer_than_32);
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, lengths[i]), source + starts[i]);
        backslashes = _mm512_min_epu8(backslashes, _mm512_xor_si512(bytes, _mm512_set1_epi8('\\')));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_xor_si512(bytes, _mm512_set1_epi8('?')));
        }
        const uint64_t offset = (intern_hash_vector(bytes, lengths[i]) * BUSTER_INTERN_BUCKET_BYTES) & offset_mask;
        offsets[2 * i] = offset;
        _mm_prefetch((const char*)(buckets + offset), _MM_HINT_T0);
    }
    for (; j < count; j += 1)
    {
        const uint32_t identifier_length = lengths[j];
        const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, identifier_length), source + starts[j]);
        backslashes = _mm512_min_epu8(backslashes, _mm512_xor_si512(bytes, _mm512_set1_epi8('\\')));
        if (trigraphs)
        {
            questions = _mm512_min_epu8(questions, _mm512_xor_si512(bytes, _mm512_set1_epi8('?')));
        }
        const uint64_t offset = (intern_hash_vector(bytes, identifier_length) * BUSTER_INTERN_BUCKET_BYTES) & offset_mask;
        offsets[2 * j] = offset;
        keys[j] = refine_key(_mm512_castsi512_si128(bytes));
        _mm_prefetch((const char*)(buckets + offset), _MM_HINT_T0);
    }
    uint64_t special = _mm512_testn_epi8_mask(backslashes, backslashes);
    if (trigraphs)
    {
        special |= _mm512_testn_epi8_mask(questions, questions);
    }
    hashes->bucket_mask = table->bucket_mask;
    hashes->special = special != 0;
}

// Pass B over a block's identifiers (identifiers->count of them), in batches of `batch` (at most 64), with each
// identifier's id in `identifiers->ids`, pipelined: a batch's stages 2 to 3 run with the next batch's stage 1 in their
// middle, after 2b has loaded its candidates' spans and prefetched their records (two dependent loads per candidate, from
// the table's spans and records) and before it compares them, so that the hashing hides those loads; and the buckets a
// batch probes were prefetched a batch earlier. A batch's stages:
//   2. Each identifier's bucket is probed for its prefix with one compare.
//   Bookkeeping, 16 identifiers at a time from their lengths and ids, as bits of masks: which are slow (longer than
//   BUSTER_INTERN_FAST_LENGTH, or with a backslash), unmatched, or candidates (longer than BUSTER_INTERN_SHORT_LENGTH,
//   with a match).
//   2b. A candidate is confirmed against its record.
//   3. The deferred ones (slow, unmatched or not confirmed) in token order, which may grow the table: then the next
//      batch, hashed for the table as it was, is hashed again.
// The table's arrays are read once per batch into locals, so that the stores of ids can't make Clang read them again for
// every identifier. Returns false if the intern table runs out of room.
BUSTER_INLINE bool refine_pass_b(InternTable* table, const uint8_t* source, uint64_t length, RefineIdentifiers* identifiers, uint32_t flags, const bool trigraphs,
                                 const uint32_t batch)
{
    RefineHashes hashes[2];
    const uint32_t total = identifiers->count;
    bool result = true;
    if (total != 0)
    {
        refine_hash_batch(table, source, identifiers->starts, identifiers->lengths, total < batch ? total : batch, &hashes[0], trigraphs);
    }
    uint32_t parity = 0;
    for (uint32_t first = 0; result && first < total; first += batch)
    {
        const uint32_t count = total - first < batch ? total - first : batch;
        const uint32_t* const starts = identifiers->starts + first;
        const uint32_t* const lengths = identifiers->lengths + first;
        uint32_t* const ids = identifiers->ids + first;
        RefineHashes* const current = &hashes[parity];
        if (current->bucket_mask != table->bucket_mask)
        {
            refine_hash_batch(table, source, starts, lengths, count, current, trigraphs);
        }
        const uint8_t* const buckets = table->buckets;
        const uint8_t* const records = table->records;
        const InternSpan* const spans = table->spans;
        const uint64_t* const offsets = current->offsets;
        const __m128i* const keys = current->keys;

        // 2. Probe each identifier's bucket for its prefix, with one compare: four per iteration, then the rest.
        uint32_t probed = 0;
        for (; probed + 4 <= count; probed += 4)
        {
            ids[probed] = refine_probe(buckets + offsets[2 * probed], keys[probed]);
            ids[probed + 1] = refine_probe(buckets + offsets[2 * probed + 2], keys[probed + 1]);
            ids[probed + 2] = refine_probe(buckets + offsets[2 * probed + 4], keys[probed + 2]);
            ids[probed + 3] = refine_probe(buckets + offsets[2 * probed + 6], keys[probed + 3]);
        }
        for (; probed < count; probed += 1)
        {
            ids[probed] = refine_probe(buckets + offsets[2 * probed], keys[probed]);
        }

        // Slow: longer than BUSTER_INTERN_FAST_LENGTH, or with a backslash (or, with trigraphs, a '?'). Unmatched: no
        // slot had the prefix. Longer: past BUSTER_INTERN_SHORT_LENGTH, so that a match is only a candidate.
        uint64_t slow = 0;
        uint64_t unmatched = 0;
        uint64_t longer = 0;
        for (uint32_t group = 0; group < count; group += BUSTER_REFINE_LANES)
        {
            const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, count - group);
            const __m512i group_lengths = _mm512_maskz_loadu_epi32(lanes, lengths + group);
            const __m512i group_ids = _mm512_maskz_loadu_epi32(lanes, ids + group);
            slow |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, group_lengths, _mm512_set1_epi32(BUSTER_INTERN_FAST_LENGTH)) << group;
            longer |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, group_lengths, _mm512_set1_epi32(BUSTER_INTERN_SHORT_LENGTH)) << group;
            unmatched |= (uint64_t)_mm512_mask_testn_epi32_mask(lanes, group_ids, group_ids) << group;
        }
        if (BUSTER_UNLIKELY(current->special))
        {
            slow |= refine_batch_specials(source, starts, lengths, count, trigraphs);
        }
        uint64_t deferred = slow | unmatched;
        const uint64_t candidates = longer & ~deferred;

        // 2b, first half: each candidate's record prefetched, from its span.
        for (uint64_t remaining = candidates; remaining != 0; remaining &= remaining - 1)
        {
            const uint32_t j = (uint32_t)__builtin_ctzll(remaining);
            _mm_prefetch((const char*)(records + spans[ids[j]].offset), _MM_HINT_T0);
        }

        // The next batch's stage 1, while those loads are in flight.
        const uint32_t next = first + count;
        if (next < total)
        {
            refine_hash_batch(table, source, starts + count, lengths + count, total - next < batch ? total - next : batch, &hashes[parity ^ 1], trigraphs);
        }

        // 2b, second half: one masked compare of each candidate's bytes and terminating zero with its record.
        for (uint64_t remaining = candidates; remaining != 0; remaining &= remaining - 1)
        {
            const uint32_t j = (uint32_t)__builtin_ctzll(remaining);
            const uint32_t identifier_length = lengths[j];
            const __mmask64 compare_mask = _bzhi_u64(~0ull, identifier_length + 1);
            const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, identifier_length), source + starts[j]);
            const __m512i record = _mm512_maskz_loadu_epi8(compare_mask, records + spans[ids[j]].offset);
            deferred |= (uint64_t)(_mm512_mask_cmpneq_epi8_mask(compare_mask, bytes, record) != 0) << j;
        }

        // 3. The deferred ones, in token order: new spellings get the ids the scalar reference gives them. Each starts its
        // probe at the bucket stage 1 found (unless an insert has grown the table since).
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
                id = intern_find_or_add_vector(table, source + starts[j], lengths[j], current->bucket_mask, offsets[2 * j] / BUSTER_INTERN_BUCKET_BYTES);
            }
            ids[j] = id;
            result = id != 0;
        }
        parity ^= 1;
    }
    return result;
}

// Pass C over tokens [begin, end) of the block: each identifier's id, from the dense array, into its token's value, 16
// tokens at a time (an expand of the next ids by the identifier mask, and a masked store).
BUSTER_INLINE void refine_pass_c(const RefineIdentifiers* identifiers, uint64_t begin, uint64_t end, uint32_t* values)
{
    uint32_t position = 0;
    for (uint64_t first = begin; first < end; first += BUSTER_REFINE_LANES)
    {
        const uint64_t index = first - begin;
        const __mmask16 lanes = (__mmask16)(identifiers->masks[index / BUSTER_REFINE_STEP_TOKENS] >> (index % BUSTER_REFINE_STEP_TOKENS));
        const __m512i group_ids = _mm512_maskz_expand_epi32(lanes, _mm512_loadu_si512(identifiers->ids + position));
        _mm512_mask_storeu_epi32(values + first, lanes, group_ids);
        position += (uint32_t)__builtin_popcount(lanes);
    }
}

// Pass B's instantiations, a function each, so that the block loop's state doesn't take pass B's registers: per trigraph
// setting (so that neither tests the flag per identifier), and per batch size for the pipelined form (refine_pass_b) or
// per batch for the fused one (refine_intern_batch).
#define BUSTER_REFINE_PASS_B_INSTANTIATE(name, trigraphs, batch) \
    __attribute__((noinline)) static bool name(InternTable* table, const uint8_t* source, uint64_t length, RefineIdentifiers* identifiers, uint32_t flags) \
    { \
        return refine_pass_b(table, source, length, identifiers, flags, trigraphs, batch); \
    }

BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_16_plain, false, 16)
BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_16_trigraphs, true, 16)
BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_32_plain, false, 32)
BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_32_trigraphs, true, 32)
BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_64_plain, false, 64)
BUSTER_REFINE_PASS_B_INSTANTIATE(refine_pass_b_64_trigraphs, true, 64)

#define BUSTER_REFINE_BATCH_INSTANTIATE(name, trigraphs, fused) \
    __attribute__((noinline)) static bool name(InternTable* table, const uint8_t* source, uint64_t length, RefineIdentifiers* identifiers, uint32_t begin, \
                                               uint32_t count, uint32_t flags) \
    { \
        return refine_intern_batch(table, source, length, identifiers, begin, count, flags, trigraphs, fused); \
    }

BUSTER_REFINE_BATCH_INSTANTIATE(refine_intern_batch_fused_plain, false, true)
BUSTER_REFINE_BATCH_INSTANTIATE(refine_intern_batch_fused_trigraphs, true, true)

// The vectorized path: an instantiation per trigraph setting, batch size and form of pass B. Each block's source bytes
// are scanned for backslashes just before its passes.
BUSTER_INLINE bool refine_tokens_with(InternTable* table, const uint8_t* source, uint64_t length, const uint8_t* kinds, const uint32_t* starts, const uint32_t* ends,
                                      uint64_t count, uint32_t* values, uint64_t* space, uint32_t flags, const bool trigraphs, const uint32_t batch, const bool fused)
{
    RefineIdentifiers identifiers;
    __m512i previous_ends = _mm512_setzero_si512();
    Scratch scratch = scratch_begin(0);
    uint64_t* backslash_chunks = arena_push_array(scratch.arena, (length + 4095) / 4096 + 1, sizeof(uint64_t), 64);
    uint64_t scanned = 0;
    bool result = backslash_chunks != 0;
    for (uint64_t block = 0; result && block < count; block += BUSTER_REFINE_BLOCK_TOKENS)
    {
        const uint64_t block_end = count - block < BUSTER_REFINE_BLOCK_TOKENS ? count : block + BUSTER_REFINE_BLOCK_TOKENS;
        // The block's gaps and identifiers end by its last token's end.
        scanned = refine_scan_chunks(source, length, backslash_chunks, scanned, ends[block_end - 1], trigraphs);
        if (values != ends)
        {
            previous_ends = refine_pass_a(source, length, kinds, starts, ends, block, block_end, values, space, flags, backslash_chunks, previous_ends, &identifiers, count,
                                          scanned * 4096, true);
        }
        else
        {
            previous_ends = refine_pass_a(source, length, kinds, starts, ends, block, block_end, values, space, flags, backslash_chunks, previous_ends, &identifiers, count,
                                          scanned * 4096, false);
        }
        if (fused)
        {
            for (uint32_t first = 0; result && first < identifiers.count; first += batch)
            {
                const uint32_t batch_count = identifiers.count - first < batch ? identifiers.count - first : batch;
                result = (trigraphs ? refine_intern_batch_fused_trigraphs : refine_intern_batch_fused_plain)(table, source, length, &identifiers, first, batch_count, flags);
            }
        }
        else if (batch == 16)
        {
            result = (trigraphs ? refine_pass_b_16_trigraphs : refine_pass_b_16_plain)(table, source, length, &identifiers, flags);
        }
        else if (batch == 32)
        {
            result = (trigraphs ? refine_pass_b_32_trigraphs : refine_pass_b_32_plain)(table, source, length, &identifiers, flags);
        }
        else
        {
            result = (trigraphs ? refine_pass_b_64_trigraphs : refine_pass_b_64_plain)(table, source, length, &identifiers, flags);
        }
        if (result)
        {
            refine_pass_c(&identifiers, block, block_end, values);
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
