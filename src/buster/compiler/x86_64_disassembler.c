// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded decoding of the encoder catalogue's 64-bit encodings, including 32-bit addresses. Initialize
// once before sharing the read-only tables. Initialization retains two compact allocated table regions; decoding
// itself allocates nothing. Source and output storage must not overlap; no source padding is required.
// The decoder publishes complete records
// before the first error; consumed is that error's byte offset. Unsupported can include valid instructions outside
// this catalogue. Actual input lengths are retained, including non-shortest displacements and VEX prefixes.
// AVX-512 classifies 64 source bytes with nibble tables and matches eight form signatures at once. The batched path
// derives ordinary legacy lengths and constructs up to sixteen records in lanes; complex forms use the matcher.
// The scalar twin retains the full validator; optimized checks rely only on invariants established by raw parsing.
// The form catalogue supplies facts, not instruction boundaries. Reserved prefix bits and all decoded operands are
// checked before publishing a record; the decoder never executes the bytes or uses input bytes as table indices
// without checking their range. Formatting preserves semantics but does not promise identical reassembled bytes.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"
#include <stdio.h>
#include <stdlib.h>

#define BUSTER_X86_DECODE_MAPS 11
#define BUSTER_X86_DECODE_BUCKETS (3 * BUSTER_X86_DECODE_MAPS * 256)
#define BUSTER_X86_DECODE_ENTRIES (X86_FORM_COUNT * 8 + BUSTER_X86_DECODE_BUCKETS * 7)

typedef enum X86DecodeStatus
{
    X86_DECODE_OK,
    X86_DECODE_UNSUPPORTED,
    X86_DECODE_TRUNCATED,
    X86_DECODE_OUTPUT_FULL,
    X86_DECODE_BAD_ARGUMENT,
} X86DecodeStatus;

typedef struct X86Decoded X86Decoded;
struct X86Decoded
{
    X86Instruction instruction;
    uint32_t offset;
    uint8_t length;
    uint8_t address32;
    uint16_t reserved;
};

typedef struct X86DecodeResult X86DecodeResult;
struct X86DecodeResult
{
    uint64_t count;
    uint64_t consumed;
    X86DecodeStatus status;
};

typedef struct X86DecodeBucket X86DecodeBucket;
struct X86DecodeBucket
{
    uint32_t first;
    uint32_t count;
};

typedef struct X86DecodeHeader X86DecodeHeader;
struct X86DecodeHeader
{
    uint64_t key;
    uint32_t bucket;
    uint32_t prefixes;
    uint8_t end;
    uint8_t opcode;
    uint8_t family;
    uint8_t rex;
    uint8_t r;
    uint8_t x;
    uint8_t b;
    uint8_t high_rm;
    uint8_t high_index;
    uint8_t vvvv;
    uint8_t decorations;
    uint8_t segment;
    uint8_t address32;
};

typedef struct X86DecodeCheck X86DecodeCheck;
struct X86DecodeCheck
{
    uint32_t registers;
    uint32_t memory;
    uint16_t flags;
};

typedef struct X86DecodeIndex X86DecodeIndex;
struct X86DecodeIndex
{
    uint64_t common_mask;
    uint64_t common_key;
    uint64_t varying;
    uint32_t first;
};

static X86DecodeBucket x86_decode_buckets[BUSTER_X86_DECODE_BUCKETS];
static uint64_t* x86_decode_keys;
static uint64_t* x86_decode_masks;
static uint16_t* x86_decode_forms;
static uint64_t x86_decode_storage_bytes;
static X86DecodeIndex x86_decode_indices[2 * BUSTER_X86_DECODE_MAPS * 256];
static uint16_t* x86_decode_index_rows;
static uint32_t x86_decode_index_count;
static uint8_t x86_decode_length_table[256] __attribute__((aligned(64)));
static X86DecodeCheck x86_decode_checks[X86_FORM_COUNT];
static uint32_t x86_decode_simple[256 * 32];
static bool x86_decode_initialized;
static uint32_t x86_decode_entry_count;

static void x86_disassembly_initialize(void);

static bool x86_decode_indices_initialize(void)
{
    // A vector bucket's common signature bits are checked once. PEXT packs only its varying bits into a compact
    // table index; wildcard rows populate each of their cells. An ambiguous cell retains the original SIMD scan,
    // so aliases, address-size distinctions and exceptional validators keep the same candidate ordering.
    uint32_t total = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_decode_indices); i += 1)
    {
        const X86DecodeBucket bucket = x86_decode_buckets[i + BUSTER_X86_DECODE_MAPS * 256];
        X86DecodeIndex* index = &x86_decode_indices[i];
        *index = (X86DecodeIndex){.first = UINT32_MAX};
        if (bucket.count != 0)
        {
            uint64_t common = UINT64_MAX;
            uint64_t all = 0;
            uint64_t different = 0;
            const uint64_t key = x86_decode_keys[bucket.first];
            for (uint32_t j = 0; j < bucket.count; j += 1)
            {
                common &= x86_decode_masks[bucket.first + j];
                all |= x86_decode_masks[bucket.first + j];
                different |= key ^ x86_decode_keys[bucket.first + j];
            }
            common &= ~different;
            const uint64_t varying = all & ~common;
            const uint32_t bits = (uint32_t)__builtin_popcountll(varying);
            if (bits <= 12)
            {
                index->common_mask = common;
                index->common_key = key & common;
                index->varying = varying;
                index->first = total;
                total += 1u << bits;
            }
        }
    }
    x86_decode_index_rows = total != 0 ? malloc((uint64_t)total * sizeof(*x86_decode_index_rows)) : 0;
    const bool valid = total == 0 || x86_decode_index_rows != 0;
    if (valid)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_decode_indices); i += 1)
        {
            const X86DecodeIndex* index = &x86_decode_indices[i];
            if (index->first != UINT32_MAX)
            {
                const X86DecodeBucket bucket = x86_decode_buckets[i + BUSTER_X86_DECODE_MAPS * 256];
                const uint32_t count = 1u << __builtin_popcountll(index->varying);
                for (uint32_t cell = 0; cell < count; cell += 1)
                {
                    const uint64_t key = index->common_key | _pdep_u64(cell, index->varying);
                    uint16_t row = 0;
                    for (uint32_t j = 0; j < bucket.count; j += 1)
                    {
                        const uint32_t entry = bucket.first + j;
                        if ((key & x86_decode_masks[entry]) == x86_decode_keys[entry])
                        {
                            row = row == 0 ? x86_decode_forms[entry] : UINT16_MAX;
                        }
                    }
                    x86_decode_index_rows[index->first + cell] = row;
                }
            }
        }
        x86_decode_index_count = total;
    }
    return valid;
}

static uint32_t x86_decode_bucket(uint32_t family, uint32_t map, uint32_t opcode)
{
    return (family * BUSTER_X86_DECODE_MAPS + map) * 256 + opcode;
}

static uint32_t x86_decode_form_map(const X86FormDescription* form, uint32_t* opcode_index)
{
    uint32_t map = 0;
    uint32_t index = 0;
    if (form->family != BUSTER_X86_FAMILY_LEGACY)
    {
        map = x86_opcode_map(form);
        index = form->opcode_bytes - 1;
    }
    else if (x86_opcode_byte(form, 0) == 0x0f && form->opcode_bytes > 1)
    {
        map = 1;
        index = 1;
        const uint8_t second = x86_opcode_byte(form, 1);
        if ((second == 0x38 || second == 0x3a) && form->opcode_bytes > 2)
        {
            map = second == 0x38 ? 2 : 3;
            index = 2;
        }
    }
    *opcode_index = index;
    return map;
}

static bool x86_decode_form_supported(uint32_t row)
{
    const X86FormDescription* form = &x86_form_descriptions[row];
    uint32_t index;
    const uint32_t map = form->opcode_bytes != 0 ? x86_decode_form_map(form, &index) : 0;
    const bool vectors = x86_form_has_class(form, BUSTER_X86_CLASS_XMM) || x86_form_has_class(form, BUSTER_X86_CLASS_YMM) || x86_form_has_class(form, BUSTER_X86_CLASS_ZMM);
    // EVEX's maps 4 and 7 are APX's and USER_MSR's; maps 5 and 6 are AVX512-FP16's and AVX10.2's vectors.
    const bool apx = form->family == BUSTER_X86_FAMILY_EVEX && (map == 4 || map >= 7 || (!vectors && !(form->evex & BUSTER_X86_EVEX_MASK)));
    // UD0/UD1's operand-less spellings omit ModRM in the encoder catalogue. Raw decoding uses their ModRM forms.
    return form->opcode_bytes != 0 && x86_form_exists_in_mode(row, X86_ENCODER_MODE_64) &&
           !x86_form_has_prefix(form, 0xd5) && map < BUSTER_X86_DECODE_MAPS &&
           !apx && row != X86_FORM_UD0 && row != X86_FORM_UD1;
}

static uint64_t x86_decode_form_key(const X86FormDescription* form)
{
    const uint32_t prefixes = form->family == BUSTER_X86_FAMILY_LEGACY ? x86_prefixes_without(form->prefixes, 0x67) : 0;
    const uint64_t pp = form->family == BUSTER_X86_FAMILY_LEGACY ? 0 : x86_prefix_pp(form);
    const uint64_t broadcast = (form->evex & BUSTER_X86_EVEX_B) != 0;
    const uint32_t length = (form->evex & BUSTER_X86_EVEX_SAE) ? 0 : form->vector_length;
    return prefixes | ((uint64_t)form->w << 32) | ((uint64_t)length << 33) | (broadcast << 35) | (pp << 36);
}

static uint64_t x86_decode_signature(const X86FormDescription* form, uint64_t* key)
{
    uint64_t mask = (1ull << 38) - 1;
    *key = x86_decode_form_key(form);
    if (form->evex & BUSTER_X86_EVEX_ROUNDING)
    {
        mask &= ~(3ull << 33);
    }
    uint32_t index;
    x86_decode_form_map(form, &index);
    const uint32_t count = form->opcode_bytes - (x86_form_has_operation_byte(form) ? 1 : 0);
    if (index + 1 < count)
    {
        const uint64_t fixed = form->encoding == BUSTER_X86_ENCODING_PR && index + 2 == count ? 0xf8 : 0xff;
        mask |= fixed << 40;
        *key |= (uint64_t)x86_opcode_byte(form, index + 1) << 40;
    }
    else if (form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT))
    {
        if (form->encoding & BUSTER_X86_ENCODING_DIGIT)
        {
            mask |= 0x38ull << 40;
            *key |= (uint64_t)(form->encoding & 7) << 43;
        }
        const bool memory = (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0;
        const bool reg = (form->rm & BUSTER_X86_OPERAND_REGISTER) != 0;
        if (memory != reg)
        {
            mask |= 1ull << 56;
            *key |= (uint64_t)memory << 56;
        }
    }
    *key &= mask;
    return mask;
}

static void x86_decode_lengths_initialize(void)
{
    // A fast length hint exists only if every unprefixed one-byte catalogue form for this opcode agrees on the
    // layout. 0x40 marks a known row, 0x10 ModRM, the low nibble trailing bytes, 0x20 adds four bytes with REX.W,
    // and 0x80 requires REX.W. Conflicting opcode extensions (F6/F7, for example) have no hint.
    uint8_t metadata[2][256] = {{0}};
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[row];
        if (x86_decode_form_supported(row) && form->family == BUSTER_X86_FAMILY_LEGACY && form->prefixes == 0 && form->opcode_bytes == 1 && form->opcode != 0x9b)
        {
            const bool modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
            const uint8_t meta = 0x40 | (modrm ? 0x10 : 0) | (form->immediate_bytes + form->relative_bytes);
            const uint32_t count = form->encoding == BUSTER_X86_ENCODING_PR ? 8 : 1;
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint8_t* entry = &metadata[form->w][form->opcode | i];
                *entry = *entry == 0 || *entry == meta ? meta : 0xff;
            }
        }
    }
    for (uint32_t opcode = 0; opcode < 256; opcode += 1)
    {
        const uint8_t narrow = metadata[0][opcode];
        const uint8_t wide = metadata[1][opcode];
        uint8_t meta = 0;
        if (narrow != 0xff && wide != 0xff)
        {
            if (narrow == wide || wide == 0)
            {
                meta = narrow;
            }
            else if (narrow == 0)
            {
                meta = wide | 0x80;
            }
            else if (wide == narrow + 4)
            {
                meta = narrow | 0x20;
            }
        }
        x86_decode_length_table[opcode] = meta;
    }
}

static void x86_decode_checks_initialize(void)
{
    // Raw parsing already establishes consistent modes, bounded general bases/indices, high-byte/REX exclusion,
    // immediate widths and relative ranges. For ordinary classes, the remaining checks are packed register and
    // decoration masks plus memory admission. Exceptional classes and relationships retain the full validator.
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[row];
        const uint16_t simple = BUSTER_X86_OPERAND_CLASS | BUSTER_X86_OPERAND_REGISTER | BUSTER_X86_OPERAND_MEMORY;
        const bool ordinary = ((form->reg | form->rm | form->vvvv) & ~simple) == 0 &&
                              (form->reg & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_OPMASK &&
                              (form->rm & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_OPMASK &&
                              (form->vvvv & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_OPMASK;
        if (x86_decode_form_supported(row) && ordinary && !x86_form_has_operation_byte(form) && !x86_form_has_is4(form) &&
            !x86_form_has_distinct_destination(form) && !x86_form_has_prefix(form, 0x3e))
        {
            X86DecodeCheck* check = &x86_decode_checks[row];
            const uint32_t common = x86_template_checks(form) & 0xffff00ffu;
            check->registers = common | ((uint32_t)x86_forbidden_register_bits(form->rm, form->family) << 8);
            check->memory = common | (0xf0u << 8);
            check->flags = 1 | ((form->rm == 0 || (form->rm & BUSTER_X86_OPERAND_REGISTER)) ? 2 : 0);
            if (form->rm & BUSTER_X86_OPERAND_MEMORY)
            {
                for (uint32_t address32 = 0; address32 < 2; address32 += 1)
                {
                    for (uint32_t segment = 0; segment < 3; segment += 1)
                    {
                        const uint32_t number = row + segment * X86_FORM_COUNT;
                        if (!x86_form_lacks_room(number, address32 ? X86_ENCODER_MODE_ADDRESS32 : X86_ENCODER_MODE_64))
                        {
                            check->flags |= 1u << (2 + address32 * 3 + segment);
                        }
                    }
                }
            }
            check->flags |= (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM) ? 1u << 8 : 0;
        }
    }
}

static void x86_decode_simple_initialize(void)
{
    // Direct admission for ordinary, unprefixed one-byte legacy forms. The table distinguishes REX.W, /digit and
    // memory admission. Ambiguous aliases retain the ordinary matcher; no input-dependent form scan is hidden here.
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[row];
        if ((x86_decode_checks[row].flags & 1) && form->family == BUSTER_X86_FAMILY_LEGACY && form->prefixes == 0 &&
            form->opcode_bytes == 1 && form->opcode != 0x9b && form->opcode != 0x90 &&
            (form->reg & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_GPR64 && (form->rm & BUSTER_X86_OPERAND_CLASS) <= BUSTER_X86_CLASS_GPR64)
        {
            const bool modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
            const bool plus = form->encoding == BUSTER_X86_ENCODING_PR;
            const uint32_t meta = row | ((uint32_t)modrm << 16) | ((uint32_t)(form->encoding == BUSTER_X86_ENCODING_R) << 17) |
                                  ((uint32_t)plus << 18) | ((uint32_t)((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8) << 19) |
                                  ((uint32_t)((form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8) << 20) |
                                  ((uint32_t)form->immediate_bytes << 24) | ((uint32_t)form->relative_bytes << 28);
            for (uint32_t opcode = form->opcode; opcode < form->opcode + (plus ? 8u : 1u); opcode += 1)
            {
                for (uint32_t digit = 0; digit < 8; digit += 1)
                {
                    for (uint32_t memory = 0; memory < 2; memory += 1)
                    {
                        const bool admitted = !modrm || (memory ? (x86_decode_checks[row].flags & 4) != 0 : (x86_decode_checks[row].flags & 2) != 0);
                        if (admitted && (!(form->encoding & BUSTER_X86_ENCODING_DIGIT) || digit == (form->encoding & 7)))
                        {
                            uint32_t* entry = &x86_decode_simple[opcode * 32 + form->w * 16 + digit * 2 + memory];
                            *entry = *entry == 0 || *entry == meta ? meta : UINT32_MAX;
                        }
                    }
                }
            }
        }
    }
}

static bool x86_disassembler_initialize(void)
{
    bool result = (x86_encoder_initialized || x86_encoder_initialize()) &&
                  (x86_encoder_initialized_address32 || x86_encoder_initialize_address32());
    if (result && !x86_decode_initialized)
    {
        memset(x86_decode_buckets, 0, sizeof(x86_decode_buckets));
        for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
        {
            if (x86_decode_form_supported(row))
            {
                const X86FormDescription* form = &x86_form_descriptions[row];
                uint32_t index;
                const uint32_t map = x86_decode_form_map(form, &index);
                const uint32_t count = form->encoding == BUSTER_X86_ENCODING_PR && index + 1 == form->opcode_bytes ? 8 : 1;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    x86_decode_buckets[x86_decode_bucket(form->family, map, x86_opcode_byte(form, index) | i)].count += 1;
                }
            }
        }
        uint32_t total = 0;
        for (uint32_t i = 0; i < BUSTER_X86_DECODE_BUCKETS; i += 1)
        {
            X86DecodeBucket* bucket = &x86_decode_buckets[i];
            bucket->first = total;
            total += (bucket->count + 7) & ~7u;
            bucket->count = 0;
        }
        result = total <= BUSTER_X86_DECODE_ENTRIES;
        if (result)
        {
            // One region sized from the actual padded buckets, rather than the catalogue's worst-case expansion.
            // Padding each bucket to eight also aligns both 64-bit arrays for their vector loads.
            x86_decode_storage_bytes = ((uint64_t)total * 18 + 63) & ~63ull;
            x86_decode_keys = aligned_alloc(64, x86_decode_storage_bytes);
            result = x86_decode_keys != 0;
        }
        if (result)
        {
            x86_decode_masks = x86_decode_keys + total;
            x86_decode_forms = (uint16_t*)(x86_decode_masks + total);
            for (uint32_t i = 0; i < total; i += 1)
            {
                x86_decode_keys[i] = UINT64_MAX;
                x86_decode_masks[i] = UINT64_MAX;
            }
            for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
            {
                if (x86_decode_form_supported(row))
                {
                    const X86FormDescription* form = &x86_form_descriptions[row];
                    uint32_t index;
                    const uint32_t map = x86_decode_form_map(form, &index);
                    const uint32_t count = form->encoding == BUSTER_X86_ENCODING_PR && index + 1 == form->opcode_bytes ? 8 : 1;
                    uint64_t key;
                    const uint64_t mask = x86_decode_signature(form, &key);
                    for (uint32_t i = 0; i < count; i += 1)
                    {
                        X86DecodeBucket* bucket = &x86_decode_buckets[x86_decode_bucket(form->family, map, x86_opcode_byte(form, index) | i)];
                        const uint32_t entry = bucket->first + bucket->count;
                        bucket->count += 1;
                        x86_decode_keys[entry] = key;
                        x86_decode_masks[entry] = mask;
                        x86_decode_forms[entry] = (uint16_t)row;
                    }
                }
            }
            result = x86_decode_indices_initialize();
            if (result)
            {
                x86_decode_lengths_initialize();
                x86_decode_checks_initialize();
                x86_decode_simple_initialize();
                x86_disassembly_initialize();
                x86_decode_entry_count = total;
                x86_decode_initialized = true;
            }
            else
            {
                free(x86_decode_keys);
                x86_decode_keys = 0;
                x86_decode_masks = 0;
                x86_decode_forms = 0;
                x86_decode_storage_bytes = 0;
            }
        }
    }
    return result;
}

static bool x86_decode_is_prefix(uint8_t byte)
{
    return (byte >= 0x40 && byte <= 0x4f) || byte == 0x26 || byte == 0x2e || byte == 0x36 || byte == 0x3e ||
           (byte >= 0x64 && byte <= 0x67) || byte == 0x9b || byte == 0xf0 || byte == 0xf2 || byte == 0xf3;
}

static uint64_t x86_decode_classify_scalar(const uint8_t* source, uint32_t length)
{
    uint64_t mask = 0;
    for (uint32_t i = 0; i < length; i += 1)
    {
        mask |= (uint64_t)x86_decode_is_prefix(source[i]) << i;
    }
    return mask;
}

static uint64_t x86_decode_classify(const uint8_t* source, uint32_t length)
{
#if !BUSTER_HOST_VECTOR
    return x86_decode_classify_scalar(source, length);
#else
    // Each nibble selects a set; their intersection identifies exactly the legacy/REX prefix bytes.
    const __m128i low = _mm_setr_epi8(5, 1, 5, 5, 3, 3, 11, 3, 1, 1, 1, 17, 1, 1, 9, 1);
    const __m128i high = _mm_setr_epi8(0, 0, 8, 8, 1, 0, 2, 0, 0, 16, 0, 0, 0, 0, 0, 4);
    const uint64_t active = length == 64 ? UINT64_MAX : (1ull << length) - 1;
    const __m512i bytes = _mm512_maskz_loadu_epi8(active, source);
    const __m512i lo = _mm512_shuffle_epi8(_mm512_broadcast_i32x4(low), _mm512_and_si512(bytes, _mm512_set1_epi8(15)));
    const __m512i hi = _mm512_shuffle_epi8(_mm512_broadcast_i32x4(high), _mm512_and_si512(_mm512_srli_epi16(bytes, 4), _mm512_set1_epi8(15)));
    return _mm512_test_epi8_mask(lo, hi) & active;
#endif
}

static __m512i x86_decode_window(const uint8_t* source, uint64_t size, uint32_t offset)
{
    const uint64_t available = size > offset ? size - offset : 0;
    const uint64_t active = available >= 64 ? UINT64_MAX : (1ull << available) - 1;
    return _mm512_maskz_loadu_epi8(active, source + (available != 0 ? offset : 0));
}

static void x86_decode_lengths(const uint8_t* source, uint64_t size, uint8_t* lengths)
{
    const __m512i first = x86_decode_window(source, size, 0);
    const __m512i next = x86_decode_window(source, size, 1);
    const __m512i third = x86_decode_window(source, size, 2);
    const __m512i fourth = x86_decode_window(source, size, 3);
    const uint64_t rex = _mm512_cmpeq_epi8_mask(_mm512_and_si512(first, _mm512_set1_epi8((char)0xf0)), _mm512_set1_epi8(0x40));
    const uint64_t wide = rex & _mm512_test_epi8_mask(first, _mm512_set1_epi8(8));
    const __m512i opcode = _mm512_mask_blend_epi8(rex, first, next);
    const __m512i modrm = _mm512_mask_blend_epi8(rex, next, third);
    const __m512i sib = _mm512_mask_blend_epi8(rex, third, fourth);
    const __m512i low = _mm512_permutex2var_epi8(_mm512_load_si512(x86_decode_length_table), opcode, _mm512_load_si512(x86_decode_length_table + 64));
    const __m512i high = _mm512_permutex2var_epi8(_mm512_load_si512(x86_decode_length_table + 128), opcode, _mm512_load_si512(x86_decode_length_table + 192));
    const __m512i meta = _mm512_mask_blend_epi8(_mm512_movepi8_mask(opcode), low, high);
    const uint64_t has_modrm = _mm512_test_epi8_mask(meta, _mm512_set1_epi8(16));
    const __m512i mod = _mm512_and_si512(_mm512_srli_epi16(modrm, 6), _mm512_set1_epi8(3));
    const __m512i rm = _mm512_and_si512(modrm, _mm512_set1_epi8(7));
    const uint64_t memory = has_modrm & _mm512_cmpneq_epi8_mask(mod, _mm512_set1_epi8(3));
    const uint64_t has_sib = memory & _mm512_cmpeq_epi8_mask(rm, _mm512_set1_epi8(4));
    const uint64_t mod0 = memory & _mm512_cmpeq_epi8_mask(mod, _mm512_setzero_si512());
    const uint64_t disp1 = memory & _mm512_cmpeq_epi8_mask(mod, _mm512_set1_epi8(1));
    const uint64_t base5 = _mm512_cmpeq_epi8_mask(_mm512_and_si512(sib, _mm512_set1_epi8(7)), _mm512_set1_epi8(5));
    const uint64_t disp4 = (memory & _mm512_cmpeq_epi8_mask(mod, _mm512_set1_epi8(2))) | (mod0 & (_mm512_cmpeq_epi8_mask(rm, _mm512_set1_epi8(5)) | (has_sib & base5)));
    __m512i length = _mm512_add_epi8(_mm512_and_si512(meta, _mm512_set1_epi8(15)), _mm512_set1_epi8(1));
    length = _mm512_mask_add_epi8(length, rex, length, _mm512_set1_epi8(1));
    length = _mm512_mask_add_epi8(length, has_modrm, length, _mm512_set1_epi8(1));
    length = _mm512_mask_add_epi8(length, has_sib, length, _mm512_set1_epi8(1));
    length = _mm512_mask_add_epi8(length, disp1, length, _mm512_set1_epi8(1));
    length = _mm512_mask_add_epi8(length, disp4, length, _mm512_set1_epi8(4));
    length = _mm512_mask_add_epi8(length, wide & _mm512_test_epi8_mask(meta, _mm512_set1_epi8(32)), length, _mm512_set1_epi8(4));
    static const uint8_t positions[64] __attribute__((aligned(64))) =
    {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
        48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    };
    const __m512i ends = _mm512_add_epi8(length, _mm512_load_si512(positions));
    uint64_t known = _mm512_test_epi8_mask(meta, _mm512_set1_epi8(64));
    known &= ~_mm512_movepi8_mask(meta) | wide;
    known &= _mm512_cmple_epu8_mask(ends, _mm512_set1_epi8((char)(size < 127 ? size : 127)));
    _mm512_storeu_si512(lengths, _mm512_maskz_mov_epi8(known, length));
}

static X86DecodeStatus x86_decode_header(const uint8_t* bytes, uint32_t size, uint64_t prefix_bits, bool vectorized, X86DecodeHeader* header)
{
    (void)vectorized;

    X86DecodeStatus status = X86_DECODE_OK;
    *header = (X86DecodeHeader){0};
    uint32_t at = 0;
    uint32_t prefix_count = 0;
    // Clang emits long alignment NOPs with repeated 66 and ignored CS overrides. Recognize only the 0F 1F
    // opcode after this bounded padding prefix sequence; all other instructions retain exact prefix matching.
    uint32_t padding = 0;
    bool padding_word = false;
    while (padding < size && padding < 15 && (bytes[padding] == 0x66 || bytes[padding] == 0x2e))
    {
        padding_word = padding_word || bytes[padding] == 0x66;
        padding += 1;
    }
    if (padding != 0 && padding + 1 < size && bytes[padding] == 0x0f && bytes[padding + 1] == 0x1f)
    {
        at = padding;
        header->prefixes = padding_word ? 0x66 : 0;
        prefix_count = padding_word;
    }
    // FWAIT is a prefix only when followed by an x87 instruction; the caller retries its standalone form.
    while (at < size && at < 15 && (BUSTER_VECTOR_PATH(vectorized) ? ((prefix_bits >> at) & 1) != 0 : x86_decode_is_prefix(bytes[at])) && header->rex == 0)
    {
        const uint8_t byte = bytes[at];
        at += 1;
        if (byte >= 0x40 && byte <= 0x4f)
        {
            header->rex = byte;
        }
        else if (byte == 0x64 || byte == 0x65)
        {
            status = header->segment != 0 ? X86_DECODE_UNSUPPORTED : status;
            header->segment = byte - 0x63;
        }
        else if (byte == 0x67)
        {
            status = header->address32 != 0 ? X86_DECODE_UNSUPPORTED : status;
            header->address32 = 1;
        }
        else if (prefix_count < 4)
        {
            header->prefixes |= (uint32_t)byte << (8 * prefix_count);
            prefix_count += 1;
        }
        else
        {
            status = X86_DECODE_UNSUPPORTED;
        }
    }
    uint32_t map = 0;
    uint32_t w = (header->rex >> 3) & 1;
    uint32_t length = 0;
    uint32_t pp = 0;
    uint32_t broadcast = 0;
    header->r = (header->rex & 4) << 1;
    header->x = (header->rex & 2) << 2;
    header->b = (header->rex & 1) << 3;
    if (at >= size)
    {
        status = size < 15 ? X86_DECODE_TRUNCATED : X86_DECODE_UNSUPPORTED;
    }
    if (status == X86_DECODE_OK)
    {
        const uint8_t lead = bytes[at];
        const bool xop = lead == 0x8f && at + 1 < size && (bytes[at + 1] & 31) >= 8;
        const uint32_t vector_bytes = lead == 0xc5 ? 2 : lead == 0xc4 || xop ? 3 : lead == 0x62 ? 4 : 0;
        if (vector_bytes != 0)
        {
            if (at + vector_bytes >= size)
            {
                status = X86_DECODE_TRUNCATED;
            }
            else if (header->prefixes != 0 || header->rex != 0)
            {
                status = X86_DECODE_UNSUPPORTED;
            }
            else
            {
                header->family = vector_bytes == 4 ? BUSTER_X86_FAMILY_EVEX : BUSTER_X86_FAMILY_VEX;
                const uint8_t p0 = bytes[at + 1];
                const uint8_t p1 = bytes[at + vector_bytes - 1];
                header->r = (~p0 & 0x80) >> 4;
                if (vector_bytes == 2)
                {
                    map = 1;
                    w = 0;
                    pp = p0 & 3;
                    length = (p0 >> 2) & 1;
                    header->vvvv = (~p0 >> 3) & 15;
                }
                else
                {
                    const uint8_t middle = bytes[at + 2];
                    map = p0 & (vector_bytes == 4 ? 7 : 31);
                    w = middle >> 7;
                    pp = middle & 3;
                    length = (middle >> 2) & 1;
                    header->vvvv = (~middle >> 3) & 15;
                    header->x = (~p0 & 0x40) >> 3;
                    header->b = (~p0 & 0x20) >> 2;
                    if (vector_bytes == 4)
                    {
                        status = (p0 & 8) != 0 || (middle & 4) == 0 || map == 4 || map == 7 ? X86_DECODE_UNSUPPORTED : status;
                        header->r |= ~p0 & 16;
                        header->high_rm = (~p0 & 0x40) >> 2;
                        header->high_index = (~p1 & 8) << 1;
                        header->vvvv |= header->high_index;
                        header->decorations = p1 & 0xe7;
                        broadcast = (p1 >> 4) & 1;
                        length = (p1 >> 5) & 3;
                    }
                }
                at += vector_bytes;
            }
        }
        else if (lead == 0x0f)
        {
            at += 1;
            map = 1;
            if (at < size && (bytes[at] == 0x38 || bytes[at] == 0x3a))
            {
                map = bytes[at] == 0x38 ? 2 : 3;
                at += 1;
            }
        }
    }
    if (status == X86_DECODE_OK)
    {
        if (at >= size)
        {
            status = X86_DECODE_TRUNCATED;
        }
        else if (map >= BUSTER_X86_DECODE_MAPS)
        {
            status = X86_DECODE_UNSUPPORTED;
        }
        else
        {
            header->opcode = bytes[at];
            header->end = (uint8_t)(at + 1);
            header->bucket = x86_decode_bucket(header->family, map, header->opcode);
            header->key = header->prefixes | ((uint64_t)w << 32) | ((uint64_t)length << 33) | ((uint64_t)broadcast << 35) | ((uint64_t)pp << 36);
            if (at + 1 < size)
            {
                const uint64_t next = bytes[at + 1];
                header->key |= (next << 40) | ((uint64_t)((next >> 6) != 3) << 56);
            }
        }
    }
    return status;
}

static uint64_t x86_decode_read(const uint8_t* bytes, uint32_t size, uint32_t* at, uint32_t width, bool* complete, bool* overlong)
{
    uint64_t value = 0;
    *overlong = *overlong || *at > 15 || width > 15 - *at;
    if (!*overlong && *at <= size && width <= size - *at)
    {
        for (uint32_t i = 0; i < width; i += 1)
        {
            value |= (uint64_t)bytes[*at + i] << (8 * i);
        }
        *at += width;
    }
    else
    {
        *complete = false;
    }
    return value;
}

static bool x86_decode_valid(const X86Instruction* instruction, uint16_t row, uint32_t segment, bool address32, bool compact)
{
    const X86DecodeCheck check = x86_decode_checks[row];
    bool valid;
    if (compact && (check.flags & 1))
    {
        const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
        const uint32_t fields = instruction->reg | ((uint32_t)instruction->rm << 8) |
                                ((uint32_t)instruction->vvvv << 16) | ((uint32_t)instruction->decorations << 24);
        const uint32_t allowed = memory ? 1u << (2 + address32 * 3 + segment) : segment == 0 ? 2 : 0;
        const bool zeroing = (instruction->decorations & BUSTER_X86_DECORATION_ZEROING) != 0;
        valid = (check.flags & allowed) != 0 && (fields & (memory ? check.memory : check.registers)) == 0 &&
                !(zeroing && ((instruction->decorations & BUSTER_X86_DECORATION_MASK) == 0 || (memory && (check.flags & (1u << 8)))));
    }
    else
    {
        valid = address32 ? x86_instruction_is_valid_address32(instruction) : x86_instruction_is_valid(instruction);
    }
    return valid;
}

static X86DecodeStatus x86_decode_candidate(const uint8_t* bytes, uint32_t size, const X86DecodeHeader* header, uint16_t row, bool compact, X86Decoded* output)
{
    const X86FormDescription* form = &x86_form_descriptions[row];
    X86Instruction instruction = {.form = (uint16_t)(row + header->segment * X86_FORM_COUNT)};
    const bool address32 = header->address32 && !x86_form_has_prefix(form, 0x67);
    bool valid = header->address32 || !x86_form_has_prefix(form, 0x67);
    // REX.B turns opcode 90 into XCHG with R8D: the one-byte NOP alias must not swallow that register.
    valid = valid && (row != X86_FORM_NOP || header->b == 0);
    bool complete = true;
    bool overlong = false;
    uint32_t at = header->end;
    uint32_t opcode_index;
    x86_decode_form_map(form, &opcode_index);
    const uint32_t opcode_count = form->opcode_bytes - (x86_form_has_operation_byte(form) ? 1 : 0);
    for (uint32_t i = opcode_index + 1; i < opcode_count; i += 1)
    {
        const uint8_t byte = (uint8_t)x86_decode_read(bytes, size, &at, 1, &complete, &overlong);
        const uint8_t mask = form->encoding == BUSTER_X86_ENCODING_PR && i + 1 == opcode_count ? 0xf8 : 0xff;
        valid = valid && (!complete || (byte & mask) == x86_opcode_byte(form, i));
        if (mask == 0xf8)
        {
            instruction.rm = (byte & 7) | header->b;
        }
    }
    if (form->encoding == BUSTER_X86_ENCODING_PR && opcode_index + 1 == opcode_count)
    {
        instruction.rm = (header->opcode & 7) | header->b;
    }
    const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
    if (has_modrm)
    {
        const uint8_t modrm = (uint8_t)x86_decode_read(bytes, size, &at, 1, &complete, &overlong);
        const uint32_t mod = modrm >> 6;
        const uint32_t rm = modrm & 7;
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            instruction.reg = ((modrm >> 3) & 7) | header->r;
        }
        else
        {
            valid = valid && (!complete || ((modrm >> 3) & 7) == (form->encoding & 7));
        }
        if (mod == 3)
        {
            instruction.rm = rm | header->b | header->high_rm;
        }
        else
        {
            instruction.mode = BUSTER_X86_MODE_MEMORY;
            uint32_t displacement_bytes = mod == 1 ? 1 : mod == 2 ? 4 : 0;
            if (rm == 4)
            {
                const uint8_t sib = (uint8_t)x86_decode_read(bytes, size, &at, 1, &complete, &overlong);
                const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
                const uint8_t index = ((sib >> 3) & 7) | header->x | (vsib ? header->high_index : 0);
                if (index != 4 || vsib || (form->rm & BUSTER_X86_OPERAND_SIB))
                {
                    instruction.index = index;
                    instruction.mode |= BUSTER_X86_MODE_INDEX | (sib >> 6);
                }
                if (mod != 0 || (sib & 7) != 5)
                {
                    instruction.rm = (sib & 7) | header->b;
                    instruction.mode |= BUSTER_X86_MODE_BASE;
                }
                else
                {
                    displacement_bytes = 4;
                }
            }
            else if (mod == 0 && rm == 5)
            {
                instruction.mode |= BUSTER_X86_MODE_RIP;
                displacement_bytes = 4;
            }
            else
            {
                instruction.rm = rm | header->b;
                instruction.mode |= BUSTER_X86_MODE_BASE;
            }
            const uint32_t displacement = (uint32_t)x86_decode_read(bytes, size, &at, displacement_bytes, &complete, &overlong);
            instruction.displacement = displacement_bytes == 1 ? (int8_t)displacement * form->disp8_scale : (int32_t)displacement;
        }
    }
    if (form->relative_bytes != 0)
    {
        const uint32_t displacement = (uint32_t)x86_decode_read(bytes, size, &at, form->relative_bytes, &complete, &overlong);
        instruction.displacement = form->relative_bytes == 1 ? (int8_t)displacement : form->relative_bytes == 2 ? (int16_t)displacement : (int32_t)displacement;
    }
    const uint64_t immediate = x86_decode_read(bytes, size, &at, form->immediate_bytes, &complete, &overlong);
    instruction.immediate = (int32_t)immediate;
    if (form->immediate_bytes == 8)
    {
        instruction.displacement = (int32_t)(immediate >> 32);
    }
    if (header->family != BUSTER_X86_FAMILY_LEGACY)
    {
        instruction.vvvv = header->vvvv;
        if ((form->rm & BUSTER_X86_OPERAND_VSIB) && header->family == BUSTER_X86_FAMILY_EVEX)
        {
            instruction.vvvv &= 15;
        }
        instruction.decorations = header->decorations & ((form->evex & BUSTER_X86_EVEX_ROUNDING) ? 0xe7 : 0x87);
    }
    else if (header->rex == 0)
    {
        if ((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && instruction.reg >= 4)
        {
            instruction.reg |= BUSTER_X86_REGISTER_HIGH_BYTE;
        }
        if (!(instruction.mode & BUSTER_X86_MODE_MEMORY) && (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && instruction.rm >= 4)
        {
            instruction.rm |= BUSTER_X86_REGISTER_HIGH_BYTE;
        }
    }
    X86DecodeStatus status = X86_DECODE_UNSUPPORTED;
    if (valid && !complete && !overlong)
    {
        status = X86_DECODE_TRUNCATED;
    }
    else if (valid && complete && at <= 15)
    {
        if (x86_decode_valid(&instruction, row, header->segment, address32, compact))
        {
            output->instruction = instruction;
            output->length = (uint8_t)at;
            output->address32 = address32;
            output->reserved = 0;
            status = X86_DECODE_OK;
        }
    }
    return status;
}

static X86DecodeStatus x86_decode_one(const uint8_t* bytes, uint32_t size, uint64_t prefixes, bool vectorized, bool compact, bool indexed, X86Decoded* output)
{
    (void)vectorized;

    X86DecodeHeader header;
    X86DecodeStatus status = x86_decode_header(bytes, size, prefixes, BUSTER_VECTOR_PATH(vectorized), &header);
    if (status == X86_DECODE_OK)
    {
        status = X86_DECODE_UNSUPPORTED;
        const X86DecodeBucket bucket = x86_decode_buckets[header.bucket];
        bool scan = true;
        if (indexed && header.family != BUSTER_X86_FAMILY_LEGACY && header.end < size)
        {
            const X86DecodeIndex* index = &x86_decode_indices[header.bucket - BUSTER_X86_DECODE_MAPS * 256];
            if (index->first != UINT32_MAX)
            {
                uint16_t row = 0;
                if ((header.key & index->common_mask) == index->common_key)
                {
                    row = x86_decode_index_rows[index->first + _pext_u64(header.key, index->varying)];
                }
                scan = row == UINT16_MAX;
                if (row != 0 && !scan)
                {
                    status = x86_decode_candidate(bytes, size, &header, row, compact, output);
                }
            }
        }
        for (uint32_t i = 0; scan && i < bucket.count && status != X86_DECODE_OK; i += BUSTER_VECTOR_PATH(vectorized) ? 8 : 1)
        {
            const uint32_t first = bucket.first + i;
            uint32_t matches = 0;
            if (BUSTER_VECTOR_PATH(vectorized))
            {
                const __m512i key = _mm512_set1_epi64((long long)header.key);
                __m512i masks = _mm512_load_si512(x86_decode_masks + first);
                __m512i keys = _mm512_load_si512(x86_decode_keys + first);
                if (header.end == size)
                {
                    const __m512i available = _mm512_set1_epi64((1ull << 38) - 1);
                    masks = _mm512_and_si512(masks, available);
                    keys = _mm512_and_si512(keys, available);
                }
                matches = _mm512_cmpeq_epi64_mask(_mm512_and_si512(key, masks), keys);
            }
            else
            {
                const uint64_t available = header.end == size ? (1ull << 38) - 1 : UINT64_MAX;
                matches = (header.key & x86_decode_masks[first] & available) == (x86_decode_keys[first] & available);
            }
            while (matches != 0 && status != X86_DECODE_OK)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(matches);
                matches &= matches - 1;
                const X86DecodeStatus candidate = x86_decode_candidate(bytes, size, &header, x86_decode_forms[first + lane], compact, output);
                if (candidate != X86_DECODE_UNSUPPORTED)
                {
                    status = candidate;
                }
            }
        }
    }
    if (status != X86_DECODE_OK && size != 0 && bytes[0] == 0x9b)
    {
        // A failed waiting-x87 match leaves FWAIT as a complete one-byte instruction.
        X86DecodeHeader wait = {.bucket = x86_decode_bucket(0, 0, 0x9b), .opcode = 0x9b, .end = 1};
        const X86DecodeBucket bucket = x86_decode_buckets[wait.bucket];
        for (uint32_t i = 0; i < bucket.count && status != X86_DECODE_OK; i += 1)
        {
            status = x86_decode_candidate(bytes, size, &wait, x86_decode_forms[bucket.first + i], compact, output);
        }
    }
    return status;
}

static X86DecodeResult x86_disassemble_impl(const uint8_t* source, uint64_t size, X86Decoded* output, uint64_t capacity, bool vectorized, bool compact)
{
    (void)vectorized;

    X86DecodeResult result = {0};
    if (!x86_decode_initialized || size > UINT32_MAX || (size != 0 && source == 0) || (capacity != 0 && output == 0))
    {
        result.status = X86_DECODE_BAD_ARGUMENT;
    }
    uint64_t window_end = 0;
    uint64_t window_start = 0;
    uint64_t prefixes = 0;
    while (result.status == X86_DECODE_OK && result.consumed < size)
    {
        if (result.count == capacity)
        {
            result.status = X86_DECODE_OUTPUT_FULL;
        }
        else
        {
            const uint64_t remaining = size - result.consumed;
            const uint32_t available = remaining < 15 ? (uint32_t)remaining : 15;
            if (BUSTER_VECTOR_PATH(vectorized) && result.consumed + available > window_end)
            {
                const uint32_t span = remaining < 64 ? (uint32_t)remaining : 64;
                window_start = result.consumed;
                window_end = window_start + span;
                prefixes = x86_decode_classify(source + window_start, span);
            }
            X86Decoded decoded = {.offset = (uint32_t)result.consumed};
            const uint64_t prefix_bits = BUSTER_VECTOR_PATH(vectorized) ? prefixes >> (result.consumed - window_start) : 0;
            result.status = x86_decode_one(source + result.consumed, available, prefix_bits, BUSTER_VECTOR_PATH(vectorized), compact, false, &decoded);
            if (result.status == X86_DECODE_OK)
            {
                output[result.count] = decoded;
                result.count += 1;
                result.consumed += decoded.length;
            }
        }
    }
    return result;
}

static X86DecodeResult x86_disassemble_scalar(const uint8_t* source, uint64_t size, X86Decoded* output, uint64_t capacity)
{
    return x86_disassemble_impl(source, size, output, capacity, false, false);
}

static __m512i x86_decode_extract(__m512i bytes, __m512i positions)
{
    const __m512i indices = _mm512_add_epi32(_mm512_mullo_epi32(positions, _mm512_set1_epi32(0x01010101)), _mm512_set1_epi32(0x03020100));
    return _mm512_permutexvar_epi8(indices, bytes);
}

static uint32_t x86_decode_simple_batch(const uint8_t* source, uint32_t span, const uint8_t* offsets, const uint8_t* lengths, uint32_t count, uint32_t start, X86Decoded* output)
{
    const __m512i bytes = x86_decode_window(source, span, 0);
    const __m512i positions = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)offsets));
    const __m512i initial = x86_decode_extract(bytes, positions);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i seven = _mm512_set1_epi32(7);
    const __m512i byte_mask = _mm512_set1_epi32(255);
    const __m512i first = _mm512_and_si512(initial, byte_mask);
    const uint16_t rex_mask = _mm512_cmpeq_epi32_mask(_mm512_and_si512(first, _mm512_set1_epi32(0xf0)), _mm512_set1_epi32(0x40));
    const __m512i rex = _mm512_maskz_mov_epi32(rex_mask, first);
    const __m512i fields = _mm512_mask_srli_epi32(initial, rex_mask, initial, 8);
    const __m512i opcode = _mm512_and_si512(fields, byte_mask);
    const __m512i modrm = _mm512_and_si512(_mm512_srli_epi32(fields, 8), byte_mask);
    const __m512i sib = _mm512_srli_epi32(fields, 16);
    const __m512i mod = _mm512_srli_epi32(modrm, 6);
    const __m512i rm = _mm512_and_si512(modrm, seven);
    const uint16_t memory_mask = _mm512_cmpneq_epi32_mask(mod, _mm512_set1_epi32(3));
    __m512i keys = _mm512_slli_epi32(opcode, 5);
    keys = _mm512_or_si512(keys, _mm512_slli_epi32(_mm512_and_si512(rex, _mm512_set1_epi32(8)), 1));
    keys = _mm512_or_si512(keys, _mm512_and_si512(_mm512_srli_epi32(modrm, 2), _mm512_set1_epi32(14)));
    keys = _mm512_mask_or_epi32(keys, memory_mask, keys, one);
    uint32_t key_values[16];
    uint32_t metadata[16] = {0};
    _mm512_storeu_si512(key_values, keys);
    uint32_t admitted = 0;
    while (admitted < count && lengths[offsets[admitted]] != 0)
    {
        const uint32_t meta = x86_decode_simple[key_values[admitted]];
        if (meta == 0 || meta == UINT32_MAX)
        {
            break;
        }
        metadata[admitted] = meta;
        admitted += 1;
    }
    if (admitted >= 2)
    {
        const __m512i meta = _mm512_loadu_si512(metadata);
        const uint16_t has_modrm = _mm512_test_epi32_mask(meta, _mm512_set1_epi32(1u << 16));
        const uint16_t has_reg = _mm512_test_epi32_mask(meta, _mm512_set1_epi32(1u << 17));
        const uint16_t plus = _mm512_test_epi32_mask(meta, _mm512_set1_epi32(1u << 18));
        const uint16_t memory = has_modrm & memory_mask;
        const uint16_t has_sib = memory & _mm512_cmpeq_epi32_mask(rm, _mm512_set1_epi32(4));
        const uint16_t mod0 = memory & _mm512_cmpeq_epi32_mask(mod, zero);
        const uint16_t disp1 = memory & _mm512_cmpeq_epi32_mask(mod, one);
        const uint16_t disp4 = (memory & _mm512_cmpeq_epi32_mask(mod, _mm512_set1_epi32(2))) |
                               (mod0 & (_mm512_cmpeq_epi32_mask(rm, _mm512_set1_epi32(5)) |
                                        (has_sib & _mm512_cmpeq_epi32_mask(_mm512_and_si512(sib, seven), _mm512_set1_epi32(5)))));
        const uint16_t rip = mod0 & _mm512_cmpeq_epi32_mask(rm, _mm512_set1_epi32(5));
        const uint16_t base = memory & ~rip & ~(has_sib & mod0 & _mm512_cmpeq_epi32_mask(_mm512_and_si512(sib, seven), _mm512_set1_epi32(5)));
        const __m512i b = _mm512_slli_epi32(_mm512_and_si512(rex, one), 3);
        const __m512i x = _mm512_slli_epi32(_mm512_and_si512(rex, _mm512_set1_epi32(2)), 2);
        const __m512i r = _mm512_slli_epi32(_mm512_and_si512(rex, _mm512_set1_epi32(4)), 1);
        __m512i index = _mm512_or_si512(_mm512_and_si512(_mm512_srli_epi32(sib, 3), seven), x);
        const uint16_t has_index = has_sib & _mm512_cmpneq_epi32_mask(index, _mm512_set1_epi32(4));
        index = _mm512_maskz_mov_epi32(has_index, index);
        __m512i reg = _mm512_maskz_mov_epi32(has_reg, _mm512_or_si512(_mm512_and_si512(_mm512_srli_epi32(modrm, 3), seven), r));
        __m512i operand = _mm512_mask_blend_epi32(plus, rm, _mm512_and_si512(opcode, seven));
        operand = _mm512_mask_blend_epi32(has_sib, operand, _mm512_and_si512(sib, seven));
        operand = _mm512_maskz_mov_epi32((has_modrm & ~memory) | base | plus, _mm512_or_si512(operand, b));
        const uint16_t byte_reg = ~rex_mask & _mm512_test_epi32_mask(meta, _mm512_set1_epi32(1u << 19)) & _mm512_cmpge_epi32_mask(reg, _mm512_set1_epi32(4));
        const uint16_t byte_rm = ~rex_mask & ~memory & _mm512_test_epi32_mask(meta, _mm512_set1_epi32(1u << 20)) & _mm512_cmpge_epi32_mask(operand, _mm512_set1_epi32(4));
        reg = _mm512_mask_or_epi32(reg, byte_reg, reg, _mm512_set1_epi32(BUSTER_X86_REGISTER_HIGH_BYTE));
        operand = _mm512_mask_or_epi32(operand, byte_rm, operand, _mm512_set1_epi32(BUSTER_X86_REGISTER_HIGH_BYTE));
        __m512i mode = _mm512_maskz_mov_epi32(memory, _mm512_set1_epi32(BUSTER_X86_MODE_MEMORY));
        mode = _mm512_mask_or_epi32(mode, base, mode, _mm512_set1_epi32(BUSTER_X86_MODE_BASE));
        mode = _mm512_mask_or_epi32(mode, rip, mode, _mm512_set1_epi32(BUSTER_X86_MODE_RIP));
        mode = _mm512_mask_or_epi32(mode, has_index, mode, _mm512_or_si512(_mm512_set1_epi32(BUSTER_X86_MODE_INDEX), _mm512_and_si512(_mm512_srli_epi32(sib, 6), _mm512_set1_epi32(3))));
        __m512i at = _mm512_add_epi32(positions, one);
        at = _mm512_mask_add_epi32(at, rex_mask, at, one);
        at = _mm512_mask_add_epi32(at, has_modrm, at, one);
        at = _mm512_mask_add_epi32(at, has_sib, at, one);
        const __m512i displacement_bytes = _mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(disp1, one), disp4, _mm512_set1_epi32(4));
        const __m512i displacement_raw = x86_decode_extract(bytes, at);
        __m512i displacement = _mm512_maskz_mov_epi32(disp1 | disp4, displacement_raw);
        displacement = _mm512_mask_srai_epi32(displacement, disp1, _mm512_slli_epi32(displacement_raw, 24), 24);
        at = _mm512_add_epi32(at, displacement_bytes);
        const __m512i relative_width = _mm512_srli_epi32(meta, 28);
        const __m512i immediate_width = _mm512_and_si512(_mm512_srli_epi32(meta, 24), _mm512_set1_epi32(15));
        const __m512i relative_raw = x86_decode_extract(bytes, at);
        const __m512i relative_shift = _mm512_sub_epi32(_mm512_set1_epi32(32), _mm512_slli_epi32(relative_width, 3));
        displacement = _mm512_mask_mov_epi32(displacement, _mm512_cmpneq_epi32_mask(relative_width, zero), _mm512_srav_epi32(_mm512_sllv_epi32(relative_raw, relative_shift), relative_shift));
        at = _mm512_add_epi32(at, relative_width);
        const __m512i immediate_raw = x86_decode_extract(bytes, at);
        const __m512i immediate_shift = _mm512_sub_epi32(_mm512_set1_epi32(32), _mm512_slli_epi32(_mm512_min_epu32(immediate_width, _mm512_set1_epi32(4)), 3));
        const __m512i immediate = _mm512_and_si512(immediate_raw, _mm512_srlv_epi32(_mm512_set1_epi32(-1), immediate_shift));
        const uint16_t imm64 = _mm512_cmpeq_epi32_mask(immediate_width, _mm512_set1_epi32(8));
        displacement = _mm512_mask_mov_epi32(displacement, imm64, x86_decode_extract(bytes, _mm512_add_epi32(at, _mm512_set1_epi32(4))));
        const __m512i packed = _mm512_or_si512(_mm512_or_si512(reg, _mm512_slli_epi32(operand, 8)), _mm512_or_si512(_mm512_slli_epi32(index, 16), _mm512_slli_epi32(mode, 24)));
        uint32_t immediates[16];
        uint32_t displacements[16];
        uint32_t registers[16];
        _mm512_storeu_si512(immediates, immediate);
        _mm512_storeu_si512(displacements, displacement);
        _mm512_storeu_si512(registers, packed);
        for (uint32_t i = 0; i < admitted; i += 1)
        {
            X86Decoded decoded = {.instruction = {.immediate = (int32_t)immediates[i], .displacement = (int32_t)displacements[i], .form = (uint16_t)metadata[i]},
                                  .offset = start + offsets[i], .length = lengths[offsets[i]]};
            memcpy(&decoded.instruction.reg, &registers[i], sizeof(registers[i]));
            output[i] = decoded;
        }
    }
    else
    {
        admitted = 0;
    }
    return admitted;
}

static X86DecodeResult x86_disassemble_batched(const uint8_t* source, uint64_t size, X86Decoded* output, uint64_t capacity)
{
    X86DecodeResult result = {0};
    if (!x86_decode_initialized || size > UINT32_MAX || (size != 0 && source == 0) || (capacity != 0 && output == 0))
    {
        result.status = X86_DECODE_BAD_ARGUMENT;
    }
    while (result.status == X86_DECODE_OK && result.consumed < size)
    {
        if (result.count == capacity)
        {
            result.status = X86_DECODE_OUTPUT_FULL;
        }
        else
        {
            const uint64_t start = result.consumed;
            const uint64_t remaining = size - start;
            const uint32_t span = remaining < 64 ? (uint32_t)remaining : 64;
            const uint64_t prefixes = x86_decode_classify(source + start, span);
            uint8_t lengths[64];
            x86_decode_lengths(source + start, remaining, lengths);
            uint32_t offset = 0;
            // Reuse this classification/length window across unsupported hints too. Rebuilding it after every
            // VEX/EVEX instruction would charge the entire speculative pass to a single scalar fallback.
            while (result.status == X86_DECODE_OK && offset < span && offset <= 49)
            {
                if (result.count == capacity)
                {
                    result.status = X86_DECODE_OUTPUT_FULL;
                }
                else
                {
                    uint8_t offsets[16] = {0};
                    uint32_t count = 0;
                    uint32_t end = offset;
                    bool known = true;
                    while (known && count < 16 && count < capacity - result.count && end < span && end <= 49)
                    {
                        offsets[count] = (uint8_t)end;
                        count += 1;
                        known = lengths[end] != 0;
                        end += lengths[end];
                    }
                    bool consistent = true;
                    const uint32_t fast = count >= 2 ? x86_decode_simple_batch(source + start, span, offsets, lengths, count, (uint32_t)start, output + result.count) : 0;
                    if (fast != 0)
                    {
                        result.count += fast;
                        result.consumed = start + offsets[fast - 1] + lengths[offsets[fast - 1]];
                    }
                    for (uint32_t i = fast; i < count && consistent && result.status == X86_DECODE_OK; i += 1)
                    {
                        const uint32_t at = offsets[i];
                        const uint64_t available = remaining - at;
                        X86Decoded decoded = {.offset = (uint32_t)(start + at)};
                        result.status = x86_decode_one(source + start + at, available < 15 ? (uint32_t)available : 15, prefixes >> at, true, true, true, &decoded);
                        if (result.status == X86_DECODE_OK)
                        {
                            output[result.count] = decoded;
                            result.count += 1;
                            result.consumed = start + at + decoded.length;
                            consistent = lengths[at] == decoded.length;
                        }
                    }
                    offset = (uint32_t)(result.consumed - start);
                }
            }
        }
    }
    return result;
}

static X86DecodeResult x86_disassemble(const uint8_t* source, uint64_t size, X86Decoded* output, uint64_t capacity)
{
#if !BUSTER_HOST_VECTOR
    return x86_disassemble_scalar(source, size, output, capacity);
#else
    return x86_disassemble_batched(source, size, output, capacity);
#endif
}

typedef struct X86DisassemblyText X86DisassemblyText;
struct X86DisassemblyText
{
    char* data;
    uint64_t capacity;
    uint64_t length;
    bool full;
};

static void x86_disassembly_bytes(X86DisassemblyText* text, const char* bytes, uint64_t length)
{
    if (!text->full)
    {
        if (length >= text->capacity - text->length)
        {
            text->full = true;
        }
        else
        {
            memcpy(text->data + text->length, bytes, length);
            text->length += length;
            text->data[text->length] = 0;
        }
    }
}

static void x86_disassembly_append(X86DisassemblyText* text, const char* literal)
{
    x86_disassembly_bytes(text, literal, strlen(literal));
}

static void x86_disassembly_unsigned(X86DisassemblyText* text, uint64_t value, uint32_t base)
{
    char digits[20];
    uint32_t at = sizeof(digits);
    do
    {
        at -= 1;
        const uint32_t digit = (uint32_t)(value % base);
        digits[at] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    x86_disassembly_bytes(text, digits + at, sizeof(digits) - at);
}

static void x86_disassembly_signed(X86DisassemblyText* text, int64_t value, bool plus)
{
    if (value < 0 || plus)
    {
        x86_disassembly_append(text, value < 0 ? "-" : "+");
    }
    const uint64_t magnitude = value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
    x86_disassembly_unsigned(text, magnitude, 10);
}

static void x86_disassembly_register(X86DisassemblyText* text, uint32_t reg, uint32_t operand)
{
    static const char* const general[4][8] =
    {
        {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"},
        {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"},
        {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"},
        {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"},
    };
    static const char* const high[4] = {"ah", "ch", "dh", "bh"};
    static const char* const segments[6] = {"es", "cs", "ss", "ds", "fs", "gs"};
    static const char* const prefixes[16] = {"?", "", "", "", "", "xmm", "ymm", "zmm", "k", "st", "bnd", "mm", "cr", "dr", "", "tmm"};
    const uint32_t kind = operand & BUSTER_X86_OPERAND_CLASS;
    const uint32_t number = reg & BUSTER_X86_REGISTER_NUMBER;
    if (kind >= BUSTER_X86_CLASS_GPR8 && kind <= BUSTER_X86_CLASS_GPR64)
    {
        if (reg & BUSTER_X86_REGISTER_HIGH_BYTE)
        {
            x86_disassembly_append(text, high[number & 3]);
        }
        else if (number < 8)
        {
            x86_disassembly_append(text, general[kind - 1][number]);
        }
        else
        {
            static const char* const suffix[4] = {"b", "w", "d", ""};
            x86_disassembly_append(text, "r");
            x86_disassembly_unsigned(text, number, 10);
            x86_disassembly_append(text, suffix[kind - 1]);
        }
    }
    else if (kind == BUSTER_X86_CLASS_SEGMENT)
    {
        x86_disassembly_append(text, segments[number]);
    }
    else
    {
        x86_disassembly_append(text, prefixes[kind]);
        if (kind == BUSTER_X86_CLASS_ST)
        {
            x86_disassembly_append(text, "(");
        }
        x86_disassembly_unsigned(text, number, 10);
        if (kind == BUSTER_X86_CLASS_ST)
        {
            x86_disassembly_append(text, ")");
        }
    }
}

static void x86_disassembly_memory(X86DisassemblyText* text, const X86Decoded* decoded, const X86FormDescription* form)
{
    const X86Instruction* instruction = &decoded->instruction;
    const uint32_t bytes = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : form->operand_bytes;
    const char* size = bytes == 1 ? "byte" : bytes == 2 ? "word" : bytes == 4 ? "dword" : bytes == 6 ? "fword" : bytes == 8 ? "qword" : bytes == 10 ? "tbyte" : bytes == 16 ? "xmmword" : bytes == 32 ? "ymmword" : bytes == 64 ? "zmmword" : 0;
    if (size != 0)
    {
        x86_disassembly_append(text, size);
        x86_disassembly_append(text, " ptr ");
    }
    const uint32_t segment = x86_form_segment(instruction->form);
    if (segment != 0)
    {
        x86_disassembly_append(text, segment == 1 ? "fs:" : "gs:");
    }
    const bool hidden_index = (form->rm & BUSTER_X86_OPERAND_SIB) && instruction->index == 4;
    const bool absolute = !(instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_RIP)) && (!(instruction->mode & BUSTER_X86_MODE_INDEX) || hidden_index);
    if (absolute && segment == 0)
    {
        x86_disassembly_append(text, "ds:");
    }
    if (!absolute)
    {
        x86_disassembly_append(text, "[");
    }
    bool part = false;
    const uint32_t address_class = decoded->address32 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
    if (instruction->mode & BUSTER_X86_MODE_RIP)
    {
        x86_disassembly_append(text, decoded->address32 ? "eip" : "rip");
        part = true;
    }
    if (instruction->mode & BUSTER_X86_MODE_BASE)
    {
        x86_disassembly_register(text, instruction->rm, address_class);
        part = true;
    }
    if ((instruction->mode & BUSTER_X86_MODE_INDEX) && !((form->rm & BUSTER_X86_OPERAND_SIB) && instruction->index == 4))
    {
        x86_disassembly_append(text, part ? "+" : "");
        x86_disassembly_register(text, instruction->index, (form->rm & BUSTER_X86_OPERAND_VSIB) ? form->rm : address_class);
        x86_disassembly_append(text, "*");
        x86_disassembly_unsigned(text, 1u << (instruction->mode & BUSTER_X86_MODE_SCALE), 10);
        part = true;
    }
    if (!part || instruction->displacement != 0)
    {
        x86_disassembly_signed(text, instruction->displacement, part);
    }
    if (!absolute)
    {
        x86_disassembly_append(text, "]");
    }
    if (form->evex & BUSTER_X86_EVEX_BROADCAST)
    {
        x86_disassembly_append(text, "{1to");
        x86_disassembly_unsigned(text, form->operand_bytes / form->disp8_scale, 10);
        x86_disassembly_append(text, "}");
    }
}

// Returns a NUL-terminated Intel-syntax instruction's length, or -1 on insufficient capacity. The decoder's records
// are the input contract. Relative targets use the current instruction's location, preserving their displacement.
static int64_t x86_disassembly_format_scalar(const X86Decoded* decoded, char* output, uint64_t capacity)
{
    static const char* const syntax[] =
    {
        [X86_SYNTAX_NONE] = "",
        [X86_SYNTAX_RM] = "m",
        [X86_SYNTAX_RM_REG] = "mr",
        [X86_SYNTAX_REG_RM] = "rm",
        [X86_SYNTAX_RM_IMM] = "mi",
        [X86_SYNTAX_REG_RM_IMM] = "rmi",
        [X86_SYNTAX_RM_REG_IMM] = "mri",
        [X86_SYNTAX_RM_REG_CL] = "mrc",
        [X86_SYNTAX_RM_ONE] = "m1",
        [X86_SYNTAX_RM_CL] = "mc",
        [X86_SYNTAX_IMM] = "i",
        [X86_SYNTAX_IMM_IMM] = "lh",
        [X86_SYNTAX_REL] = "t",
        [X86_SYNTAX_ACC_IMM] = "ai",
        [X86_SYNTAX_ACC_MOFFS] = "ao",
        [X86_SYNTAX_MOFFS_ACC] = "oa",
        [X86_SYNTAX_REG_VVVV_RM] = "rvm",
        [X86_SYNTAX_REG_VVVV_RM_IMM] = "rvmi",
        [X86_SYNTAX_REG_RM_VVVV] = "rmv",
        [X86_SYNTAX_VVVV_RM] = "vm",
        [X86_SYNTAX_VVVV_RM_IMM] = "vmi",
        [X86_SYNTAX_ST_RM] = "sm",
        [X86_SYNTAX_RM_ST] = "ms",
        [X86_SYNTAX_ACC] = "a",
        [X86_SYNTAX_RM_ACC] = "ma",
        [X86_SYNTAX_RM_REG_VVVV] = "mrv",
        [X86_SYNTAX_REG_RM_XMM0] = "rmx",
        [X86_SYNTAX_RM_IMM8_IMM8] = "mjk",
        [X86_SYNTAX_REG_RM_IMM8_IMM8] = "rmjk",
        [X86_SYNTAX_RM_VVVV_REG] = "mvr",
        [X86_SYNTAX_REG_VVVV_RM_IS4] = "rvm4",
        [X86_SYNTAX_REG_VVVV_IS4_RM] = "rv4m",
        [X86_SYNTAX_REG_VVVV_RM_IS4_IMM] = "rvm4z",
        [X86_SYNTAX_REG_VVVV_IS4_RM_IMM] = "rv4mz",
        [X86_SYNTAX_ACC_DX] = "ad",
        [X86_SYNTAX_DX_ACC] = "da",
        [X86_SYNTAX_IMM_ACC] = "ia",
        [X86_SYNTAX_IMM_RM] = "im",
        [X86_SYNTAX_REG] = "r",
        [X86_SYNTAX_MEMORY_RCX_ACC] = "qa",
    };
    const X86Instruction* instruction = &decoded->instruction;
    const X86FormDescription* form = x86_form_description(instruction->form);
    X86DisassemblyText text = {.data = output, .capacity = capacity, .full = capacity == 0 || output == 0};
    const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    const bool address_register = row == X86_FORM_MOVDIR64B_R64_M512 || row == X86_FORM_ENQCMD_R64_M512 || row == X86_FORM_ENQCMDS_R64_M512 || row == X86_FORM_UMONITOR_R64;
    const uint32_t register_operand = decoded->address32 && address_register ? BUSTER_X86_CLASS_GPR32 : form->reg;
    const uint32_t rm_operand = decoded->address32 && address_register ? BUSTER_X86_CLASS_GPR32 : form->rm;
    const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
    const char* operands = form->syntax < BUSTER_ARRAY_LENGTH(syntax) ? syntax[form->syntax] : 0;
    if (operands == 0)
    {
        text.full = true;
    }
    else
    {
        if (form->family == BUSTER_X86_FAMILY_EVEX)
        {
            x86_disassembly_append(&text, "{evex} ");
        }
        else if (form->family == BUSTER_X86_FAMILY_VEX && x86_opcode_map(form) < 8)
        {
            x86_disassembly_append(&text, "{vex} ");
        }
        if (!memory && form->reg != BUSTER_X86_OPERAND_NONE && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_VVVV_RM))
        {
            const bool swapped = form->w && form->syntax == X86_SYNTAX_REG_VVVV_RM && x86_opcode_map(form) >= 8;
            x86_disassembly_append(&text, form->syntax == X86_SYNTAX_RM_REG || swapped ? "{store} " : "{load} ");
        }
        if (form->relative_bytes >= 2 && form->opcode != 0xe8)
        {
            x86_disassembly_append(&text, "{disp32} ");
        }
        const bool hidden_index = (form->rm & BUSTER_X86_OPERAND_SIB) && instruction->index == 4;
        const bool general_index = (instruction->mode & BUSTER_X86_MODE_INDEX) && !hidden_index && !(form->rm & BUSTER_X86_OPERAND_VSIB);
        const bool shown_address = address_register || (instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_RIP)) || general_index;
        if (decoded->address32 && !shown_address)
        {
            x86_disassembly_append(&text, "addr32 ");
        }
        x86_disassembly_append(&text, form->mnemonic);
        bool rounding_printed = false;
        static const char* const rounding_names[4] = {"{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}"};
        const char* rounding_name = (form->evex & BUSTER_X86_EVEX_ROUNDING) ? rounding_names[(instruction->decorations >> 5) & 3] : "{sae}";
        const uint64_t immediate = (uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32);
        const uint32_t accumulator = form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 2 ? BUSTER_X86_CLASS_GPR16 : form->operand_bytes == 4 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
        for (uint32_t i = 0; operands[i] != 0; i += 1)
        {
            x86_disassembly_append(&text, i == 0 ? " " : ", ");
            if (operands[i] == 'i' && rounding)
            {
                x86_disassembly_append(&text, rounding_name);
                x86_disassembly_append(&text, ", ");
                rounding_printed = true;
            }
            switch (operands[i])
            {
                case 'r': x86_disassembly_register(&text, instruction->reg, register_operand); break;
                case 'v': x86_disassembly_register(&text, instruction->vvvv, form->vvvv); break;
                case 'm':
                    if (memory)
                    {
                        x86_disassembly_memory(&text, decoded, form);
                    }
                    else
                    {
                        x86_disassembly_register(&text, instruction->rm, rm_operand);
                    }
                    break;
                case 'i':
                {
                    const uint8_t opcode = (uint8_t)form->opcode;
                    const int32_t value = form->immediate_bytes == 1 && (opcode == 0x83 || opcode == 0x6b || opcode == 0x6a) ? (int8_t)instruction->immediate : instruction->immediate;
                    if (form->immediate_bytes == 8)
                    {
                        x86_disassembly_append(&text, "0x");
                        x86_disassembly_unsigned(&text, immediate, 16);
                    }
                    else if (form->family != BUSTER_X86_FAMILY_LEGACY && x86_opcode_map(form) == 7)
                    {
                        x86_disassembly_unsigned(&text, (uint32_t)instruction->immediate, 10);
                    }
                    else
                    {
                        x86_disassembly_signed(&text, value, false);
                    }
                    break;
                }
                case 't':
                    x86_disassembly_append(&text, ".");
                    x86_disassembly_signed(&text, (int64_t)instruction->displacement + decoded->length, true);
                    break;
                case 'a': x86_disassembly_register(&text, 0, accumulator); break;
                case 'o':
                    x86_disassembly_append(&text, "ds:0x");
                    x86_disassembly_unsigned(&text, immediate, 16);
                    break;
                case 'l': x86_disassembly_unsigned(&text, (uint32_t)instruction->immediate & 65535, 10); break;
                case 'h': x86_disassembly_unsigned(&text, (uint32_t)instruction->immediate >> 16, 10); break;
                case 'j': x86_disassembly_unsigned(&text, (uint32_t)instruction->immediate & 255, 10); break;
                case 'k': x86_disassembly_unsigned(&text, ((uint32_t)instruction->immediate >> 8) & 255, 10); break;
                case '4': x86_disassembly_register(&text, (uint32_t)instruction->immediate >> 4, form->reg); break;
                case 'z': x86_disassembly_unsigned(&text, (uint32_t)instruction->immediate & 15, 10); break;
                case 'c': x86_disassembly_append(&text, "cl"); break;
                case 'd': x86_disassembly_append(&text, "dx"); break;
                case 's': x86_disassembly_append(&text, "st(0)"); break;
                case 'x': x86_disassembly_append(&text, "xmm0"); break;
                case 'q': x86_disassembly_append(&text, "[rcx]"); break;
                default: x86_disassembly_append(&text, "1"); break;
            }
            if (i == 0 && (instruction->decorations & BUSTER_X86_DECORATION_MASK))
            {
                x86_disassembly_append(&text, "{k");
                x86_disassembly_unsigned(&text, instruction->decorations & BUSTER_X86_DECORATION_MASK, 10);
                x86_disassembly_append(&text, "}");
            }
            if (i == 0 && (instruction->decorations & BUSTER_X86_DECORATION_ZEROING))
            {
                x86_disassembly_append(&text, "{z}");
            }
        }
        if (rounding && !rounding_printed)
        {
            x86_disassembly_append(&text, ", ");
            x86_disassembly_append(&text, rounding_name);
        }
    }
    return text.full ? -1 : (int64_t)text.length;
}

#include "buster/compiler/x86_64_disassembly.c"
