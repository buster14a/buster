// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded decoding of the encoder catalogue's 64-bit encodings, including 32-bit addresses. Initialize
// once before sharing the read-only tables, which main.c's units share and the bake builds into the executable
// (buster_baked, baked.c); neither initialization nor decoding retains an allocation. Source and output storage must not overlap; no source padding is required.
// The decoder publishes complete records
// before the first error; consumed is that error's byte offset. Unsupported can include valid instructions outside
// this catalogue. Actual input lengths are retained, including non-shortest displacements and VEX prefixes.
// AVX-512 classifies 64 source bytes with nibble tables, derives ordinary legacy lengths and constructs up to
// sixteen records in lanes; other forms use the matcher, which reads a VEX or EVEX bucket's PEXT index and matches
// eight form signatures at once. Ordinary operand classes take compact checks, the rest the full validator; the
// compact checks rely only on invariants established by raw parsing.
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
// The tables' bounds, from the catalogue as it is (13,400 padded entries, 87,200 cells, lists of ~1,100 entries) with
// room to grow; initialization fails, and test-x86-disassembler with it, when one is short.
#define BUSTER_X86_DECODE_ENTRIES 16384
#define BUSTER_X86_DECODE_CELLS 98304
#define BUSTER_X86_DECODE_LIST_ENTRIES 4096

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

typedef struct X86DecodeCheck X86DecodeCheck;
struct X86DecodeCheck
{
    uint32_t registers;
    uint32_t memory;
    uint16_t flags;
};

// The decoder's and the formatter's tables: main.c's units share them, as they share the encoder's templates
// (x86_64_encoder.c's BUSTER_X86_SHARED), and the bake builds them (baked.c), so that a command starts with them; a
// benchmark's unit has its own and builds them, which its initialization time measures.
BUSTER_X86_SHARED BUSTER_BAKED X86DecodeBucket x86_decode_buckets[BUSTER_X86_DECODE_BUCKETS];
// Each bucket's entries' keys, masks and forms, padded to eight entries a bucket.
BUSTER_X86_SHARED BUSTER_BAKED uint64_t x86_decode_keys[BUSTER_X86_DECODE_ENTRIES] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint64_t x86_decode_masks[BUSTER_X86_DECODE_ENTRIES] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint16_t x86_decode_forms[BUSTER_X86_DECODE_ENTRIES] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint64_t x86_decode_storage_bytes;
BUSTER_X86_SHARED BUSTER_BAKED X86DecodeCheck x86_decode_checks[X86_FORM_COUNT];
// The forms (with their segment overrides, below BUSTER_X86_FORM_TEMPLATES) whose address32 template has no room for a
// memory operand: the full validator's x86_form_lacks_room test with a 32-bit address, which reads the address32
// encoder's templates, from a table the disassembler doesn't keep (x86_decode_checks_initialize).
BUSTER_X86_SHARED BUSTER_BAKED uint64_t x86_decode_address32_cramped[BUSTER_X86_FORM_TEMPLATES / 64];
// Each bucket's tail after its opcode (x86_decode_tails_initialize): bit 31 known, bit 24 a byte where ModRM sits
// (ModRM, or a fixed opcode byte, whose mod is 3), bits 16-23 the ModRM digits whose trailing bytes (immediates,
// relatives, 3DNow's operation byte) are bits 4-7 (8-11 with 66, 12-15 with W); the other digits' are bits 0-3.
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_tails[BUSTER_X86_DECODE_BUCKETS];
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_tails_unknown;
// Every bucket's index over its forms' compact keys (x86_decode_compact): the bits all its signatures share, the
// varying bits a cell's number packs (PEXT), and its cells' first entry. A cell holds 0 for no form, a row, or
// BUSTER_X86_DECODE_LIST | n for the candidates at x86_decode_lists[n], in catalogue order, ended by 0.
typedef struct X86DecodeLaneIndex X86DecodeLaneIndex;
struct X86DecodeLaneIndex
{
    uint32_t common_mask;
    uint32_t common_key;
    uint32_t varying;
    uint32_t first;
};
#define BUSTER_X86_DECODE_LIST 0x8000u
// What the lane decoder needs of a row (x86_decode_lane_forms_initialize): bits 0-3 its immediate bytes, 4-7 its
// relative bytes, 8-15 its disp8 scale, then BUSTER_X86_LANE_*.
#define BUSTER_X86_LANE_MODRM (1u << 16)
#define BUSTER_X86_LANE_REG (1u << 17)
#define BUSTER_X86_LANE_PLUS (1u << 18)
#define BUSTER_X86_LANE_FIXED (1u << 19)
#define BUSTER_X86_LANE_FIXED_PLUS (1u << 20)
#define BUSTER_X86_LANE_REG8 (1u << 21)
#define BUSTER_X86_LANE_RM8 (1u << 22)
#define BUSTER_X86_LANE_VSIB (1u << 23)
#define BUSTER_X86_LANE_SIB (1u << 24)
#define BUSTER_X86_LANE_ROUNDING (1u << 25)
#define BUSTER_X86_LANE_ADDRESS (1u << 26)
#define BUSTER_X86_LANE_NOP (1u << 27)
// Bits 28-31: the segments (none, FS, GS, SS) the form allows.
#define BUSTER_X86_LANE_SEGMENTS 28
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_lane_forms[X86_FORM_COUNT];
// A row's lane form and its compact checks' words (X86DecodeCheck's registers, memory, and flags with the row in the
// high half), 16 bytes a row, which the lane decoder loads a lane at a time (x86_decode_lane_rows_load).
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_lane_rows[X86_FORM_COUNT][4] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED X86DecodeLaneIndex x86_decode_lane_indices[BUSTER_X86_DECODE_BUCKETS] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint16_t x86_decode_cells[BUSTER_X86_DECODE_CELLS];
BUSTER_X86_SHARED BUSTER_BAKED uint16_t x86_decode_lists[BUSTER_X86_DECODE_LIST_ENTRIES];
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_cell_count;
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_list_count;
// The length pass's tables, from x86_decode_tails (x86_decode_length_tables_initialize). A legacy opcode's byte in
// maps 0 and 1: bits 0-3 its trailing bytes (15: no hint), bit 4 none but on digits 0 and 1, bit 5 four become eight
// with REX.W, bit 6 four become two with 66, bit 7 a byte where ModRM sits. A vector opcode's byte in map 1: bits 0-1
// VEX's (ModRM, an immediate byte), bits 2-3 EVEX's, bit 4 VEX's has no hint, bit 5 EVEX's. Every other map's
// (family, map) is uniform: its byte is the legacy layout's, 0x0f for no hint, 0xff for map 1's tables.
BUSTER_X86_SHARED BUSTER_BAKED uint8_t x86_decode_legacy_tails[2 * 256] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint8_t x86_decode_vector_tails[256] __attribute__((aligned(64)));
BUSTER_X86_SHARED BUSTER_BAKED uint8_t x86_decode_map_tails[3 * BUSTER_X86_DECODE_MAPS];
BUSTER_X86_SHARED BUSTER_BAKED bool x86_decode_initialized;
BUSTER_X86_SHARED BUSTER_BAKED uint32_t x86_decode_entry_count;

static void x86_disassembly_initialize(void);

// A signature key's (x86_decode_signature) compact form for the lane index: each of the first three prefix bytes as a
// code (bits 0-8: none 0, 66 1, F2 2, F3 3, F0 4, 2E 5, 3E 6, 9B 7: the bytes forms' keys hold), W, L, b and pp (bits
// 9-14), the byte where ModRM sits (bits 15-22) and the memory bit (23). A mask's prefix byte compacts to its code's
// three bits. An input lane with any other prefix byte, or a fourth, matches no form, and is refused before.
static uint32_t x86_decode_compact(uint64_t key, bool mask)
{
    uint32_t compact = 0;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        const uint32_t byte = (uint32_t)(key >> (8 * k)) & 0xff;
        const uint32_t code = byte == 0 ? 0 : byte == 0x66 ? 1 : byte == 0xf2 ? 2 : byte == 0xf3 ? 3 : byte == 0xf0 ? 4 : byte == 0x2e ? 5 : byte == 0x3e ? 6 : 7;
        // (Init checks every form's prefix bytes are among these: x86_decode_lane_indices_initialize.)
        compact |= (mask ? (byte != 0 ? 7u : 0u) : code) << (3 * k);
    }
    compact |= (uint32_t)((key >> 32) & 63) << 9;
    compact |= (uint32_t)((key >> 40) & 0xff) << 15;
    compact |= (uint32_t)((key >> 56) & 1) << 23;
    return compact;
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

// A form's tail after its map's opcode byte: whether a byte sits where ModRM does (ModRM, or one fixed opcode byte of
// mod 3), and its trailing bytes (immediates and relatives). False for a tail the descriptor can't hold (two fixed bytes, a fixed byte below C0).
static bool x86_decode_form_tail(const X86FormDescription* form, bool* slot, uint32_t* trailing)
{
    uint32_t index;
    x86_decode_form_map(form, &index);
    const bool operation = x86_form_has_operation_byte(form);
    const uint32_t extra = form->opcode_bytes - (operation ? 1u : 0u) - index - 1;
    const bool modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT);
    *slot = modrm || extra == 1;
    // 3DNow's operation byte is its immediate.
    *trailing = form->immediate_bytes + form->relative_bytes;
    return extra <= 1 && !(modrm && extra != 0) && (extra == 0 || x86_opcode_byte(form, index + 1) >= 0xc0) && *trailing <= 15;
}

// Fits each bucket's tail descriptor (x86_decode_tails) to its forms and checks it predicts every form's tail; a bucket
// it can't hold is left unknown, and its lanes' lengths come from their forms.
static void x86_decode_tails_initialize(void)
{
    x86_decode_tails_unknown = 0;
    // A bucket at a time, from its entries (a PR form's in each of its eight buckets).
    for (uint32_t bucket = 0; bucket < BUSTER_X86_DECODE_BUCKETS; bucket += 1)
    {
        const X86DecodeBucket entries = x86_decode_buckets[bucket];
        int8_t tails[2][2][8];
        int8_t slot_seen = -1;
        bool broken = false;
        memset(tails, -1, sizeof(tails));
        x86_decode_tails[bucket] = 0;
        for (uint32_t pass = 0; entries.count != 0 && pass < 2; pass += 1)
        {
            for (uint32_t j = 0; j < entries.count; j += 1)
            {
                const X86FormDescription* form = &x86_form_descriptions[x86_decode_forms[entries.first + j]];
                bool slot;
                uint32_t trailing;
                const bool fits = x86_decode_form_tail(form, &slot, &trailing);
                // REX.W decides the operand size over 66, as the length pass reads them.
                const uint32_t wide = form->family == BUSTER_X86_FAMILY_LEGACY && form->w;
                const uint32_t word = form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_has_prefix(form, 0x66) && !wide;
                const bool digit = (form->encoding & BUSTER_X86_ENCODING_DIGIT) != 0;
                for (uint32_t d = 0; d < 8; d += 1)
                {
                    if (!digit || d == (form->encoding & 7u))
                    {
                        if (pass == 0)
                        {
                            int8_t* entry = &tails[word][wide][d];
                            broken = broken || !fits || (*entry != -1 && *entry != (int8_t)trailing) || (slot_seen != -1 && slot_seen != (int8_t)slot);
                            *entry = (int8_t)trailing;
                            slot_seen = (int8_t)slot;
                        }
                        else
                        {
                            const uint32_t tail = x86_decode_tails[bucket];
                            const uint32_t predicted = ((tail >> (16 + d)) & 1) == 0 ? tail & 15 : wide ? (tail >> 12) & 15 : word ? (tail >> 8) & 15 : (tail >> 4) & 15;
                            broken = broken || predicted != trailing || ((tail >> 24) & 1) != (uint32_t)slot;
                        }
                    }
                }
            }
            if (pass == 0)
            {
                // A digit whose defined trailing bytes don't depend on 66 or W, and agree with the last such digit's (from
                // 7 down: F6's and F7's immediates are on 0 and 1), is outside the mask; the others are inside it, and must
                // agree with one another.
                int32_t outside = -1;
                uint32_t mask = 0;
                for (uint32_t k = 0; k < 8; k += 1)
                {
                    const uint32_t d = 7 - k;
                    const int8_t plain = tails[0][0][d];
                    const int8_t word = tails[1][0][d];
                    const int8_t wide = tails[0][1][d];
                    const int8_t value = plain != -1 ? plain : word != -1 ? word : wide;
                    const bool constant = value != -1 && (plain == -1 || plain == value) && (word == -1 || word == value) && (wide == -1 || wide == value);
                    if (constant && (outside == -1 || outside == value))
                    {
                        outside = value;
                    }
                    else if (value != -1)
                    {
                        mask |= 1u << d;
                    }
                }
                int32_t inside[3] = { -1, -1, -1 };
                for (uint32_t d = 0; d < 8; d += 1)
                {
                    const int8_t values[3] = { tails[0][0][d], tails[1][0][d], tails[0][1][d] };
                    for (uint32_t k = 0; ((mask >> d) & 1) != 0 && k < 3; k += 1)
                    {
                        broken = broken || (values[k] != -1 && inside[k] != -1 && inside[k] != values[k]);
                        inside[k] = values[k] != -1 ? values[k] : inside[k];
                    }
                }
                // A combination no form has (an unprefixed one where every form has 66) takes a defined one's value.
                inside[0] = inside[0] != -1 ? inside[0] : inside[1] != -1 ? inside[1] : inside[2] != -1 ? inside[2] : 0;
                inside[1] = inside[1] != -1 ? inside[1] : inside[0];
                inside[2] = inside[2] != -1 ? inside[2] : inside[0];
                outside = outside != -1 ? outside : 0;
                const bool used = slot_seen != -1;
                x86_decode_tails[bucket] = used && !broken ? (1u << 31) | ((uint32_t)slot_seen << 24) | (mask << 16) | ((uint32_t)inside[2] << 12) | ((uint32_t)inside[1] << 8) |
                                                                      ((uint32_t)inside[0] << 4) | (uint32_t)outside
                                                                : 0;
            }
        }
        x86_decode_tails[bucket] = broken ? 0 : x86_decode_tails[bucket];
        x86_decode_tails_unknown += slot_seen != -1 && x86_decode_tails[bucket] == 0;
    }
}

// A bucket's legacy-layout byte (x86_decode_legacy_tails), 0x0f where its descriptor doesn't fit the layout.
static uint8_t x86_decode_legacy_tail(uint32_t tail)
{
    const uint32_t mask = (tail >> 16) & 0xff;
    const uint32_t outside = tail & 15;
    const uint32_t inside = (tail >> 4) & 15;
    const uint32_t word = (tail >> 8) & 15;
    const uint32_t wide = (tail >> 12) & 15;
    const bool known = (tail >> 31) != 0;
    const bool digits = mask != 0 && (mask & ~3u) == 0 && outside == 0;
    const bool every = mask == 0xff || digits;
    const uint32_t trailing = mask == 0 ? outside : inside;
    const bool shrinks = every && inside == 4 && word == 2;
    const bool widens = every && inside == 4 && wide == 8;
    const bool fits = known && (mask == 0 || every) && (mask == 0 || ((word == inside || shrinks) && (wide == inside || widens))) && trailing < 15;
    return fits ? (uint8_t)(((tail >> 24) & 1) << 7 | (uint32_t)shrinks << 6 | (uint32_t)widens << 5 | (uint32_t)digits << 4 | trailing) : 0x0f;
}

// The length pass's tables (x86_decode_legacy_tails, x86_decode_vector_tails, x86_decode_map_tails) from the buckets'
// descriptors; a byte with no hint where they don't fit.
static void x86_decode_length_tables_initialize(void)
{
    memset(x86_decode_map_tails, 0xfe, sizeof(x86_decode_map_tails));
    memset(x86_decode_vector_tails, 0, sizeof(x86_decode_vector_tails));
    for (uint32_t family = 0; family < 3; family += 1)
    {
        for (uint32_t map = 0; map < BUSTER_X86_DECODE_MAPS; map += 1)
        {
            const bool tabled = map == 1 || (family == BUSTER_X86_FAMILY_LEGACY && map == 0);
            uint8_t* uniform = &x86_decode_map_tails[family * BUSTER_X86_DECODE_MAPS + map];
            *uniform = tabled ? 0xff : 0xfe;
            for (uint32_t opcode = 0; opcode < 256; opcode += 1)
            {
                const uint32_t tail = x86_decode_tails[x86_decode_bucket(family, map, opcode)];
                const bool used = x86_decode_buckets[x86_decode_bucket(family, map, opcode)].count != 0;
                const uint8_t legacy = x86_decode_legacy_tail(tail);
                if (family == BUSTER_X86_FAMILY_LEGACY && tabled)
                {
                    x86_decode_legacy_tails[map * 256 + opcode] = used ? legacy : 0x0f;
                }
                else if (tabled)
                {
                    // A vector form has one trailing immediate byte at most, and the same on every digit.
                    const bool fits = (tail >> 31) != 0 && (tail & 15) <= 1 && ((tail >> 16) & 0xff) == 0;
                    const uint32_t shift = family == BUSTER_X86_FAMILY_VEX ? 0 : 2;
                    const uint8_t bits = (uint8_t)((((tail >> 24) & 1) | ((tail & 1) << 1)) << shift);
                    x86_decode_vector_tails[opcode] |= used ? (fits ? bits : (uint8_t)(1u << (4 + shift / 2))) : 0;
                }
                else if (used)
                {
                    *uniform = *uniform == 0xfe || *uniform == legacy ? legacy : 0x0f;
                }
            }
            *uniform = *uniform == 0xfe ? 0x0f : *uniform;
        }
    }
}

// Initialization's scratch memory: an anonymous mapping rather than the heap, since the bake (baked.c) runs the
// initializer and refuses writes to the image's memory outside buster_baked, which the allocator's own state is.
static void* x86_decode_scratch(uint64_t bytes)
{
    void* pointer = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return pointer == MAP_FAILED ? NULL : pointer;
}

// Whether the form's template in `templates` (a mode's table, past its padding) has no room for its memory operand
// (x86_form_lacks_room's test, on a table given rather than the mode's shared one).
static bool x86_decode_lacks_room(const X86TemplateEntry* templates, uint32_t form, X86EncoderMode encoder)
{
    uint32_t flags;
    memcpy(&flags, &templates[form & BUSTER_X86_FORM_NUMBER].template.bytes[BUSTER_X86_TEMPLATE_FLAGS_OFFSET], sizeof(flags));
    const uint32_t row = (uint32_t)(x86_form_description(form) - x86_form_descriptions);
    return (x86_form_description(form)->rm & BUSTER_X86_OPERAND_MEMORY) && (flags & BUSTER_X86_TEMPLATE_NO_MEMORY) && x86_form_exists_in_mode(row, encoder);
}

// The checks' 32-bit-address room flags read the address32 encoder's templates: its shared table where a command built
// it, else a table built in scratch memory and dropped after (the bake builds the checks without the 4 MB table).
static bool x86_decode_checks_initialize(void)
{
    const uint64_t table_bytes = (uint64_t)BUSTER_X86_TEMPLATE_TABLE * sizeof(X86TemplateEntry);
    X86TemplateEntry* scratch = x86_encoder_initialized_address32 ? NULL : x86_decode_scratch(table_bytes);
    const X86TemplateEntry* address32 = x86_encoder_initialized_address32 ? x86_mode_templates(X86_ENCODER_MODE_ADDRESS32) : scratch != NULL ? scratch + BUSTER_X86_TEMPLATE_PADDING : NULL;
    const bool valid = address32 != NULL && (scratch == NULL || build_mode_templates(X86_ENCODER_MODE_ADDRESS32, scratch + BUSTER_X86_TEMPLATE_PADDING));
    for (uint32_t form = 0; form < BUSTER_X86_FORM_TEMPLATES; form += 64)
    {
        uint64_t cramped = 0;
        for (uint32_t k = 0; valid && k < 64; k += 1)
        {
            cramped |= (uint64_t)x86_decode_lacks_room(address32, form + k, X86_ENCODER_MODE_ADDRESS32) << k;
        }
        x86_decode_address32_cramped[form / 64] = cramped;
    }
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
                for (uint32_t mode32 = 0; valid && mode32 < 2; mode32 += 1)
                {
                    for (uint32_t segment = 0; segment <= BUSTER_X86_SEGMENT_SS; segment += 1)
                    {
                        const uint32_t number = row + segment * X86_FORM_COUNT;
                        const bool lacks = mode32 ? x86_decode_lacks_room(address32, number, X86_ENCODER_MODE_ADDRESS32) : x86_form_lacks_room(number, X86_ENCODER_MODE_64);
                        if (x86_form_has_segment(row, segment) && !lacks)
                        {
                            check->flags |= 1u << (2 + mode32 * 4 + segment);
                        }
                    }
                }
            }
            check->flags |= (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM) ? 1u << 10 : 0;
        }
    }
    if (scratch != NULL)
    {
        munmap(scratch, table_bytes);
    }
    return valid;
}

// Each row's lane descriptor (x86_decode_lane_forms), as x86_decode_candidate reads its form.
static void x86_decode_lane_forms_initialize(void)
{
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[row];
        uint32_t lane = 0;
        if (x86_decode_form_supported(row))
        {
            uint32_t index;
            x86_decode_form_map(form, &index);
            const uint32_t opcode_count = form->opcode_bytes - (x86_form_has_operation_byte(form) ? 1u : 0u);
            const bool fixed = index + 1 < opcode_count;
            const bool plus = form->encoding == BUSTER_X86_ENCODING_PR;
            lane = form->immediate_bytes | (uint32_t)form->relative_bytes << 4 | (uint32_t)form->disp8_scale << 8;
            lane |= form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT) ? BUSTER_X86_LANE_MODRM : 0;
            lane |= form->encoding == BUSTER_X86_ENCODING_R ? BUSTER_X86_LANE_REG : 0;
            lane |= plus && !fixed ? BUSTER_X86_LANE_PLUS : 0;
            lane |= fixed ? BUSTER_X86_LANE_FIXED : 0;
            lane |= plus && fixed ? BUSTER_X86_LANE_FIXED_PLUS : 0;
            lane |= (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_LANE_REG8 : 0;
            lane |= (form->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 ? BUSTER_X86_LANE_RM8 : 0;
            lane |= (form->rm & BUSTER_X86_OPERAND_VSIB) ? BUSTER_X86_LANE_VSIB : 0;
            lane |= (form->rm & BUSTER_X86_OPERAND_SIB) ? BUSTER_X86_LANE_SIB : 0;
            lane |= (form->evex & BUSTER_X86_EVEX_ROUNDING) ? BUSTER_X86_LANE_ROUNDING : 0;
            lane |= x86_form_has_prefix(form, 0x67) ? BUSTER_X86_LANE_ADDRESS : 0;
            lane |= row == X86_FORM_NOP ? BUSTER_X86_LANE_NOP : 0;
            for (uint32_t segment = 0; segment <= BUSTER_X86_SEGMENT_SS; segment += 1)
            {
                lane |= x86_form_has_segment(row, segment) ? 1u << (BUSTER_X86_LANE_SEGMENTS + segment) : 0;
            }
        }
        x86_decode_lane_forms[row] = lane;
    }
}

// The lane index (x86_decode_lane_indices) over every bucket's entries' compact keys. False where a form's prefix byte
// has no code, a bucket varies in more than 16 bits, the cells or the lists outgrow their bounds, or an allocation fails.
static bool x86_decode_lane_indices_initialize(void)
{
    bool valid = true;
    uint32_t cells = 0;
    for (uint32_t b = 0; b < BUSTER_X86_DECODE_BUCKETS; b += 1)
    {
        const X86DecodeBucket bucket = x86_decode_buckets[b];
        X86DecodeLaneIndex* index = &x86_decode_lane_indices[b];
        // An empty bucket's key is one no lane's has (bit 31).
        *index = (X86DecodeLaneIndex){ .common_mask = UINT32_MAX, .common_key = 1u << 31 };
        if (bucket.count != 0)
        {
            uint32_t common = UINT32_MAX;
            uint32_t all = 0;
            uint32_t different = 0;
            const uint32_t key = x86_decode_compact(x86_decode_keys[bucket.first], false);
            for (uint32_t j = 0; j < bucket.count; j += 1)
            {
                const uint64_t raw = x86_decode_keys[bucket.first + j];
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    const uint32_t byte = (uint32_t)(raw >> (8 * k)) & 0xff;
                    valid = valid && (k < 3 || byte == 0) && (byte == 0 || byte == 0x66 || byte == 0xf2 || byte == 0xf3 || byte == 0xf0 || byte == 0x2e || byte == 0x3e || byte == 0x9b);
                }
                const uint32_t mask = x86_decode_compact(x86_decode_masks[bucket.first + j], true);
                common &= mask;
                all |= mask;
                different |= key ^ x86_decode_compact(raw, false);
            }
            common &= ~different;
            index->common_mask = common;
            index->common_key = key & common;
            index->varying = all & ~common;
            index->first = cells;
            valid = valid && __builtin_popcount(index->varying) <= 16;
            cells += valid ? 1u << __builtin_popcount(index->varying) : 0;
        }
    }
    // Each cell's matching entries, in catalogue order. An entry matches the cells whose varying bits inside its mask
    // are its key's, so it walks just those (the subsets of its free bits): once to count each cell's matches and keep
    // its first, and again, for the cells with more than one, to fill their lists (each ended by 0; list 0 is no one's).
    valid = valid && cells <= BUSTER_X86_DECODE_CELLS;
    memset(x86_decode_cells, 0, sizeof(x86_decode_cells));
    memset(x86_decode_lists, 0, sizeof(x86_decode_lists));
    uint32_t* counts = valid ? x86_decode_scratch((uint64_t)cells * sizeof(uint32_t)) : NULL;
    valid = valid && counts != NULL;
    uint32_t used = 1;
    // The buckets with an ambiguous cell, the only ones the second pass walks.
    uint64_t ambiguous_buckets[(BUSTER_X86_DECODE_BUCKETS + 63) / 64] = { 0 };
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        for (uint32_t b = 0; b < BUSTER_X86_DECODE_BUCKETS; b += 1)
        {
            const X86DecodeBucket bucket = pass == 0 || ((ambiguous_buckets[b / 64] >> (b % 64)) & 1) != 0 ? x86_decode_buckets[b] : (X86DecodeBucket){ 0 };
            const X86DecodeLaneIndex* index = &x86_decode_lane_indices[b];
            for (uint32_t j = 0; j < bucket.count; j += 1)
            {
                const uint32_t entry = bucket.first + j;
                const uint32_t mask = x86_decode_compact(x86_decode_masks[entry], true);
                const uint32_t base = _pext_u32(x86_decode_compact(x86_decode_keys[entry], false) & mask, index->varying);
                const uint32_t free_cells = _pext_u32(~mask, index->varying);
                const uint16_t row = x86_decode_forms[entry];
                uint32_t subset = 0;
                bool walking = true;
                while (walking)
                {
                    const uint32_t cell = index->first + (base | subset);
                    if (pass == 0)
                    {
                        x86_decode_cells[cell] = counts[cell] == 0 ? row : x86_decode_cells[cell];
                        ambiguous_buckets[b / 64] |= (uint64_t)(counts[cell] != 0) << (b % 64);
                        counts[cell] += 1;
                    }
                    else if ((x86_decode_cells[cell] & BUSTER_X86_DECODE_LIST) != 0)
                    {
                        x86_decode_lists[counts[cell]] = row;
                        counts[cell] += 1;
                    }
                    subset = (subset - free_cells) & free_cells;
                    walking = subset != 0;
                }
            }
        }
        // Between the passes: each ambiguous cell's list starts where the previous one's end left off, and its count
        // becomes its write position.
        for (uint32_t cell = 0; pass == 0 && cell < cells; cell += 1)
        {
            const uint32_t list = used;
            const bool ambiguous = counts[cell] > 1;
            x86_decode_cells[cell] = ambiguous ? (uint16_t)(BUSTER_X86_DECODE_LIST | list) : x86_decode_cells[cell];
            used += ambiguous ? counts[cell] + 1 : 0;
            counts[cell] = list;
            valid = valid && list < BUSTER_X86_DECODE_LIST;
        }
        valid = valid && used <= BUSTER_X86_DECODE_LIST_ENTRIES;
    }
    if (counts != NULL)
    {
        munmap(counts, (uint64_t)cells * sizeof(uint32_t));
    }
    x86_decode_cell_count = cells;
    x86_decode_list_count = used;
    return valid;
}

static bool x86_disassembler_initialize(void)
{
    // A baked executable starts with the tables built; the checks build the address32 templates they read themselves.
    bool result = x86_decode_initialized || x86_encoder_initialized || x86_encoder_initialize();
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
            // The entries' bytes in use (the benchmark's table size).
            x86_decode_storage_bytes = (uint64_t)total * 18;
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
            result = x86_decode_lane_indices_initialize() && x86_decode_checks_initialize();
            x86_decode_lane_forms_initialize();
            if (result)
            {
                x86_decode_tails_initialize();
                x86_decode_length_tables_initialize();
                for (uint32_t row = 0; row < X86_FORM_COUNT; row += 1)
                {
                    x86_decode_lane_rows[row][0] = x86_decode_lane_forms[row];
                    x86_decode_lane_rows[row][1] = x86_decode_checks[row].registers;
                    x86_decode_lane_rows[row][2] = x86_decode_checks[row].memory;
                    x86_decode_lane_rows[row][3] = x86_decode_checks[row].flags | row << 16;
                }
                x86_disassembly_initialize();
                x86_decode_entry_count = total;
                x86_decode_initialized = true;
            }
        }
    }
    return result;
}

static __m512i x86_decode_window(const uint8_t* source, uint64_t size, uint32_t offset)
{
    const uint64_t available = size > offset ? size - offset : 0;
    const uint64_t active = available >= 64 ? UINT64_MAX : (1ull << available) - 1;
    return _mm512_maskz_loadu_epi8(active, source + (available != 0 ? offset : 0));
}

// The length pass's per-position fields, which the lane decoder reads at its lanes' starts: the window's 128 bytes,
// each position's prefix run, its opcode's position and map, and its kind (bit 0 VEX C5, bit 1 VEX C4 or XOP, bit 2
// EVEX, bit 4 FWAIT alone).
typedef struct X86DecodeWindow X86DecodeWindow;
struct X86DecodeWindow
{
    __m512i low;
    __m512i high;
    __m512i run;
    __m512i opcode_at;
    __m512i map;
    __m512i kind;
};

// A window's bytes that are legacy or REX prefixes: two nibble-table shuffles and their intersection.
static uint64_t x86_decode_prefix_bytes(__m512i bytes)
{
    const __m128i low = _mm_setr_epi8(5, 1, 5, 5, 3, 3, 11, 3, 1, 1, 1, 17, 1, 1, 9, 1);
    const __m128i high = _mm_setr_epi8(0, 0, 8, 8, 1, 0, 2, 0, 0, 16, 0, 0, 0, 0, 0, 4);
    const __m512i lo = _mm512_shuffle_epi8(_mm512_broadcast_i32x4(low), _mm512_and_si512(bytes, _mm512_set1_epi8(15)));
    const __m512i hi = _mm512_shuffle_epi8(_mm512_broadcast_i32x4(high), _mm512_and_si512(_mm512_srli_epi16(bytes, 4), _mm512_set1_epi8(15)));
    return _mm512_test_epi8_mask(lo, hi);
}

// The legacy and REX prefix bytes among `length` (at most 64) from `source`, a bit each (the tests' and the benchmark's
// view of the length pass's classification).
static __attribute__((unused)) uint64_t x86_decode_classify(const uint8_t* source, uint32_t length)
{
    const uint64_t active = length >= 64 ? UINT64_MAX : (1ull << length) - 1;
    return x86_decode_prefix_bytes(_mm512_maskz_loadu_epi8(active, source)) & active;
}

// A 256-byte table's entries for 64 byte indices: two two-source permutes, chosen by the index's top bit.
static __m512i x86_decode_lookup(const uint8_t* table, __m512i indices)
{
    const __m512i low = _mm512_permutex2var_epi8(_mm512_load_si512(table), indices, _mm512_load_si512(table + 64));
    const __m512i high = _mm512_permutex2var_epi8(_mm512_load_si512(table + 128), indices, _mm512_load_si512(table + 192));
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(indices), low, high);
}

// Every position's instruction length in the window `source[0, size)`, as if an instruction started there: 0 where it
// has no hint (a bucket x86_decode_length_tables_initialize couldn't describe, a vector prefix after a plain one or REX, 15
// prefix bytes, an end past `size` or past 15 bytes). Reads the 128 bytes from `source` that lie inside `size`.
static void x86_decode_lengths(const uint8_t* source, uint64_t size, uint8_t* lengths, X86DecodeWindow* window)
{
    const __m512i low = x86_decode_window(source, size, 0);
    const __m512i high = x86_decode_window(source, size, 64);
    const uint64_t active_low = size >= 64 ? UINT64_MAX : (1ull << size) - 1;
    const uint64_t active_high = size >= 128 ? UINT64_MAX : size > 64 ? (1ull << (size - 64)) - 1 : 0;
    const __m512i one = _mm512_set1_epi8(1);
    const __m512i positions = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
                                              31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __uint128_t prefix = (__uint128_t)(x86_decode_prefix_bytes(low) & active_low) | (__uint128_t)(x86_decode_prefix_bytes(high) & active_high) << 64;
    const __m512i rex_nibble = _mm512_set1_epi8(0x40);
    const __m512i high_nibble = _mm512_set1_epi8((char)0xf0);
    const __uint128_t rex = (__uint128_t)_mm512_cmpeq_epi8_mask(_mm512_and_si512(low, high_nibble), rex_nibble) |
                            (__uint128_t)_mm512_cmpeq_epi8_mask(_mm512_and_si512(high, high_nibble), rex_nibble) << 64;
    const __uint128_t rex_w = rex & ((__uint128_t)_mm512_test_epi8_mask(low, _mm512_set1_epi8(8)) | (__uint128_t)_mm512_test_epi8_mask(high, _mm512_set1_epi8(8)) << 64);
    const __uint128_t operand = (__uint128_t)_mm512_cmpeq_epi8_mask(low, _mm512_set1_epi8(0x66)) | (__uint128_t)_mm512_cmpeq_epi8_mask(high, _mm512_set1_epi8(0x66)) << 64;
    // FS, GS and SS's overrides and 67, the prefixes a VEX or EVEX prefix may follow.
    const __m512i lows[4] = { _mm512_set1_epi8(0x64), _mm512_set1_epi8(0x65), _mm512_set1_epi8(0x36), _mm512_set1_epi8(0x67) };
    __uint128_t allowed = 0;
    for (uint32_t k = 0; k < 4; k += 1)
    {
        allowed |= (__uint128_t)_mm512_cmpeq_epi8_mask(low, lows[k]) | (__uint128_t)_mm512_cmpeq_epi8_mask(high, lows[k]) << 64;
    }
    // Each position's run of prefixes, which a REX ends: the run's bytes in step k are those k bytes on.
    __m512i run = _mm512_setzero_si512();
    uint64_t word = 0;
    uint64_t wide = 0;
    uint64_t blocked = 0;
    uint64_t in = (uint64_t)prefix;
    for (uint32_t k = 0; in != 0 && k < 15; k += 1)
    {
        run = _mm512_mask_add_epi8(run, in, run, one);
        word |= in & (uint64_t)(operand >> k);
        wide |= in & (uint64_t)(rex_w >> k);
        blocked |= in & ~(uint64_t)(allowed >> k);
        in &= ~(uint64_t)(rex >> k) & (uint64_t)(prefix >> (k + 1));
    }
    uint64_t overlong = in;
    __m512i lead_at = _mm512_add_epi8(positions, run);
    __m512i lead = _mm512_permutex2var_epi8(low, lead_at, high);
    // FWAIT (9B) prefixes a waiting x87 form only, one whose prefix run leads to D9, DB, DD or DF; an instruction
    // starting with it otherwise is FWAIT alone, one byte (x86_decode_header's caller retries it so).
    const uint64_t waits = _mm512_cmpeq_epi8_mask(low, _mm512_set1_epi8((char)0x9b)) &
                           ~_mm512_cmpeq_epi8_mask(_mm512_and_si512(_mm512_sub_epi8(lead, _mm512_set1_epi8((char)0xd9)), _mm512_set1_epi8((char)0xf9)), _mm512_setzero_si512());
    run = _mm512_mask_mov_epi8(run, waits, _mm512_setzero_si512());
    word &= ~waits;
    wide &= ~waits;
    overlong &= ~waits;
    blocked &= ~waits;
    lead_at = _mm512_mask_mov_epi8(lead_at, waits, positions);
    lead = _mm512_mask_mov_epi8(lead, waits, low);
    const __m512i second = _mm512_permutex2var_epi8(low, _mm512_add_epi8(lead_at, one), high);
    const uint64_t vex2 = _mm512_cmpeq_epi8_mask(lead, _mm512_set1_epi8((char)0xc5));
    const uint64_t vex3 = _mm512_cmpeq_epi8_mask(lead, _mm512_set1_epi8((char)0xc4)) |
                          (_mm512_cmpeq_epi8_mask(lead, _mm512_set1_epi8((char)0x8f)) & _mm512_cmpge_epu8_mask(_mm512_and_si512(second, _mm512_set1_epi8(31)), _mm512_set1_epi8(8)));
    const uint64_t evex = _mm512_cmpeq_epi8_mask(lead, _mm512_set1_epi8(0x62));
    const uint64_t vector = vex2 | vex3 | evex;
    const uint64_t escape = _mm512_cmpeq_epi8_mask(lead, _mm512_set1_epi8(0x0f)) & ~vector;
    const uint64_t map2 = escape & _mm512_cmpeq_epi8_mask(second, _mm512_set1_epi8(0x38));
    const uint64_t map3 = escape & _mm512_cmpeq_epi8_mask(second, _mm512_set1_epi8(0x3a));
    // The opcode's offset from the lead byte, and its map: 0F, 0F 38 or 0F 3A's, C5's 1, C4's and XOP's mmmmm, EVEX's mmm.
    __m512i offset = _mm512_maskz_mov_epi8(escape, one);
    offset = _mm512_mask_add_epi8(offset, map2 | map3, offset, one);
    offset = _mm512_mask_mov_epi8(offset, vex2, _mm512_set1_epi8(2));
    offset = _mm512_mask_mov_epi8(offset, vex3, _mm512_set1_epi8(3));
    offset = _mm512_mask_mov_epi8(offset, evex, _mm512_set1_epi8(4));
    __m512i map = _mm512_maskz_mov_epi8(escape, one);
    map = _mm512_mask_add_epi8(map, map2, map, one);
    map = _mm512_mask_add_epi8(map, map3, map, _mm512_set1_epi8(2));
    map = _mm512_mask_mov_epi8(map, vex2, one);
    map = _mm512_mask_mov_epi8(map, vex3, _mm512_and_si512(second, _mm512_set1_epi8(31)));
    map = _mm512_mask_mov_epi8(map, evex, _mm512_and_si512(second, _mm512_set1_epi8(7)));
    const __m512i opcode_at = _mm512_add_epi8(lead_at, offset);
    const __m512i opcode = _mm512_permutex2var_epi8(low, opcode_at, high);
    const __m512i modrm = _mm512_permutex2var_epi8(low, _mm512_add_epi8(opcode_at, one), high);
    const __m512i sib = _mm512_permutex2var_epi8(low, _mm512_add_epi8(opcode_at, _mm512_set1_epi8(2)), high);
    // The tail's byte in the legacy layout (x86_decode_legacy_tails): the (family, map)'s, or its opcode's in map 1's
    // and legacy map 0's tables.
    const uint64_t mapped = _mm512_cmplt_epu8_mask(map, _mm512_set1_epi8(BUSTER_X86_DECODE_MAPS));
    __m512i family_map = _mm512_mask_add_epi8(map, vex2 | vex3, map, _mm512_set1_epi8(BUSTER_X86_DECODE_MAPS));
    family_map = _mm512_mask_add_epi8(family_map, evex, map, _mm512_set1_epi8(2 * BUSTER_X86_DECODE_MAPS));
    uint8_t map_table[64] __attribute__((aligned(64))) = { 0 };
    memcpy(map_table, x86_decode_map_tails, sizeof(x86_decode_map_tails));
    __m512i tail = _mm512_permutexvar_epi8(family_map, _mm512_load_si512(map_table));
    const uint64_t tabled = _mm512_cmpeq_epi8_mask(tail, _mm512_set1_epi8((char)0xff));
    const uint64_t legacy0 = tabled & ~vector & ~escape;
    const uint64_t legacy1 = tabled & escape;
    tail = _mm512_mask_mov_epi8(tail, legacy0, x86_decode_lookup(x86_decode_legacy_tails, opcode));
    tail = _mm512_mask_mov_epi8(tail, legacy1, x86_decode_lookup(x86_decode_legacy_tails + 256, opcode));
    // VEX's bits 0, 1 and 4, EVEX's 2, 3 and 5: ModRM, an immediate byte, and no hint (15) in the legacy layout.
    const __m512i bits = x86_decode_lookup(x86_decode_vector_tails, opcode);
    const uint64_t vector_slot = _mm512_test_epi8_mask(bits, _mm512_mask_blend_epi8(evex, one, _mm512_set1_epi8(4)));
    const uint64_t vector_immediate = _mm512_test_epi8_mask(bits, _mm512_mask_blend_epi8(evex, _mm512_set1_epi8(2), _mm512_set1_epi8(8)));
    const uint64_t vector_unknown = _mm512_test_epi8_mask(bits, _mm512_mask_blend_epi8(evex, _mm512_set1_epi8(0x10), _mm512_set1_epi8(0x20)));
    __m512i vector_tail = _mm512_maskz_mov_epi8(vector_immediate, one);
    vector_tail = _mm512_mask_mov_epi8(vector_tail, vector_slot, _mm512_or_si512(vector_tail, _mm512_set1_epi8((char)0x80)));
    vector_tail = _mm512_mask_mov_epi8(vector_tail, vector_unknown, _mm512_set1_epi8(0x0f));
    tail = _mm512_mask_mov_epi8(tail, tabled & vector, vector_tail);
    const uint64_t hinted = mapped & ~_mm512_cmpeq_epi8_mask(_mm512_and_si512(tail, _mm512_set1_epi8(15)), _mm512_set1_epi8(15));
    const uint64_t slot = _mm512_test_epi8_mask(tail, _mm512_set1_epi8((char)0x80));
    const uint64_t shrinks = _mm512_test_epi8_mask(tail, _mm512_set1_epi8(0x40)) & word & ~wide;
    const uint64_t widens = _mm512_test_epi8_mask(tail, _mm512_set1_epi8(0x20)) & wide;
    const uint64_t digits = _mm512_test_epi8_mask(tail, _mm512_set1_epi8(0x10)) &
                            _mm512_cmpge_epu8_mask(_mm512_and_si512(_mm512_srli_epi16(modrm, 3), _mm512_set1_epi8(7)), _mm512_set1_epi8(2));
    __m512i trailing = _mm512_and_si512(tail, _mm512_set1_epi8(15));
    trailing = _mm512_mask_mov_epi8(trailing, shrinks, _mm512_set1_epi8(2));
    trailing = _mm512_mask_mov_epi8(trailing, widens, _mm512_set1_epi8(8));
    trailing = _mm512_mask_mov_epi8(trailing, digits & slot, _mm512_setzero_si512());
    const __m512i mod = _mm512_and_si512(_mm512_srli_epi16(modrm, 6), _mm512_set1_epi8(3));
    const __m512i rm = _mm512_and_si512(modrm, _mm512_set1_epi8(7));
    const uint64_t memory = slot & _mm512_cmpneq_epi8_mask(mod, _mm512_set1_epi8(3));
    const uint64_t has_sib = memory & _mm512_cmpeq_epi8_mask(rm, _mm512_set1_epi8(4));
    const uint64_t mod0 = memory & _mm512_cmpeq_epi8_mask(mod, _mm512_setzero_si512());
    const uint64_t disp1 = memory & _mm512_cmpeq_epi8_mask(mod, one);
    const uint64_t base5 = _mm512_cmpeq_epi8_mask(_mm512_and_si512(sib, _mm512_set1_epi8(7)), _mm512_set1_epi8(5));
    const uint64_t disp4 = (memory & _mm512_cmpeq_epi8_mask(mod, _mm512_set1_epi8(2))) | (mod0 & (_mm512_cmpeq_epi8_mask(rm, _mm512_set1_epi8(5)) | (has_sib & base5)));
    __m512i length = _mm512_add_epi8(_mm512_add_epi8(run, offset), _mm512_add_epi8(one, trailing));
    length = _mm512_mask_add_epi8(length, slot, length, one);
    length = _mm512_mask_add_epi8(length, has_sib, length, one);
    length = _mm512_mask_add_epi8(length, disp1, length, one);
    length = _mm512_mask_add_epi8(length, disp4, length, _mm512_set1_epi8(4));
    const __m512i ends = _mm512_add_epi8(length, positions);
    uint64_t known = hinted & ~overlong & ~(vector & blocked) & _mm512_cmple_epu8_mask(length, _mm512_set1_epi8(15));
    known &= _mm512_cmple_epu8_mask(ends, _mm512_set1_epi8((char)(size < 127 ? size : 127)));
    _mm512_storeu_si512(lengths, _mm512_maskz_mov_epi8(known, length));
    window->low = low;
    window->high = high;
    window->run = run;
    window->opcode_at = opcode_at;
    window->map = map;
    __m512i kind = _mm512_maskz_mov_epi8(vex2, one);
    kind = _mm512_mask_mov_epi8(kind, vex3, _mm512_set1_epi8(2));
    kind = _mm512_mask_mov_epi8(kind, evex, _mm512_set1_epi8(4));
    window->kind = _mm512_mask_mov_epi8(kind, waits, _mm512_set1_epi8(16));
}

// The lane decoder's view of a window: 4 bytes from each lane's dword position (x86_decode_extract on 128 bytes).
static __m512i x86_decode_lane_bytes(const X86DecodeWindow* window, __m512i positions)
{
    // Each dword's low byte (a position below 128) in its four bytes, plus 0, 1, 2, 3.
    const __m512i broadcast = _mm512_broadcast_i32x4(_mm_setr_epi8(0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12));
    const __m512i indices = _mm512_add_epi8(_mm512_shuffle_epi8(positions, broadcast), _mm512_set1_epi32(0x03020100));
    return _mm512_permutex2var_epi8(window->low, indices, window->high);
}

// The lanes' rows' entries (x86_decode_lane_rows), a 16-byte load a lane, transposed into four vectors of a field each:
// gathers are microcoded on Zen 5, and a vector load of scalar stores waits for them all.
static void x86_decode_lane_rows_load(const uint32_t* rows, __m512i* fields)
{
    __m512i blocks[4];
    for (uint32_t k = 0; k < 4; k += 1)
    {
        // Block j holds lane 4j + k's entry.
        __m512i block = _mm512_castsi128_si512(_mm_load_si128((const __m128i*)x86_decode_lane_rows[rows[k]]));
        block = _mm512_inserti32x4(block, _mm_load_si128((const __m128i*)x86_decode_lane_rows[rows[4 + k]]), 1);
        block = _mm512_inserti32x4(block, _mm_load_si128((const __m128i*)x86_decode_lane_rows[rows[8 + k]]), 2);
        blocks[k] = _mm512_inserti32x4(block, _mm_load_si128((const __m128i*)x86_decode_lane_rows[rows[12 + k]]), 3);
    }
    const __m512i low01 = _mm512_unpacklo_epi32(blocks[0], blocks[1]);
    const __m512i high01 = _mm512_unpackhi_epi32(blocks[0], blocks[1]);
    const __m512i low23 = _mm512_unpacklo_epi32(blocks[2], blocks[3]);
    const __m512i high23 = _mm512_unpackhi_epi32(blocks[2], blocks[3]);
    fields[0] = _mm512_unpacklo_epi64(low01, low23);
    fields[1] = _mm512_unpackhi_epi64(low01, low23);
    fields[2] = _mm512_unpacklo_epi64(high01, high23);
    fields[3] = _mm512_unpackhi_epi64(high01, high23);
}

// A per-position field of the length pass (X86DecodeWindow) at each lane's start.
static __m512i x86_decode_lane_field(__m512i starts, __m512i field)
{
    return _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(starts, field)));
}

// The window's `count` instructions at `offsets` (from the window's start, `remaining` bytes of input from there), in
// one pass of 16 lanes: their prefixes, VEX, XOP or EVEX header, compact key, form (the lane index; an ambiguous cell's
// candidates in turn, in catalogue order), operands, validity and length. Writes the instructions before the first
// that doesn't decode, and stops after one whose length isn't its hint (the caller's next window starts after it).
// Returns how many it wrote; `status` is the first failure's (UNSUPPORTED or TRUNCATED), else OK.
static uint32_t x86_decode_lanes(const X86DecodeWindow* window, uint64_t remaining, const uint8_t* offsets, const uint8_t* lengths, uint32_t count, uint64_t start,
                                 X86Decoded* output, X86DecodeStatus* status, uint32_t* consumed)
{
    const uint16_t live = (uint16_t)((1u << count) - 1);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i byte_mask = _mm512_set1_epi32(0xff);
    const __m512i lane_starts = _mm512_maskz_loadu_epi8(live, offsets);
    const __m512i starts = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(lane_starts));
    const __m512i run = x86_decode_lane_field(lane_starts, window->run);
    const __m512i opcode_at = x86_decode_lane_field(lane_starts, window->opcode_at);
    const __m512i map = x86_decode_lane_field(lane_starts, window->map);
    const __m512i kind = x86_decode_lane_field(lane_starts, window->kind);
    // Each lane's bytes, at most 15 (the input's end may come first).
    const __m512i available = _mm512_min_epu32(_mm512_set1_epi32(15), _mm512_sub_epi32(_mm512_set1_epi32((int)(remaining < 127 ? remaining : 127)), starts));
    const __m512i end = _mm512_add_epi32(starts, available);
    // The prefixes, as x86_decode_header reads them: a REX ends them; FS, GS and SS's overrides and 67 once each; the
    // others as codes (x86_decode_compact), three at most for any form.
    __m512i codes = zero;
    __m512i plain = zero;
    __m512i segment = zero;
    __m512i rex = zero;
    uint16_t address32 = 0;
    // A header error (a segment or 67 twice, a fifth plain prefix), and a prefix no form has (a byte without a code, a
    // fourth plain one), which only the match refuses.
    uint16_t malformed = 0;
    uint16_t unmatched = 0;
    uint16_t operand = 0;
    uint16_t padding = live;
    const uint32_t longest = _mm512_mask_reduce_max_epu32(live, run);
    for (uint32_t k = 0; k < longest; k += 1)
    {
        const uint16_t active = live & _mm512_cmpgt_epu32_mask(run, _mm512_set1_epi32((int)k));
        const __m512i byte = _mm512_and_si512(x86_decode_lane_bytes(window, _mm512_add_epi32(starts, _mm512_set1_epi32((int)k))), byte_mask);
        const uint16_t is_rex = _mm512_cmpeq_epi32_mask(_mm512_and_si512(byte, _mm512_set1_epi32(0xf0)), _mm512_set1_epi32(0x40));
        const uint16_t fs = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x64));
        const uint16_t gs = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x65));
        const uint16_t ss = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x36));
        const uint16_t is_address = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x67));
        const uint16_t is_segment = fs | gs | ss;
        const uint16_t is_plain = active & ~is_rex & ~is_segment & ~is_address;
        const uint16_t word = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x66));
        const uint16_t cs = _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x2e));
        __m512i code = _mm512_maskz_mov_epi32(word, one);
        code = _mm512_mask_mov_epi32(code, _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0xf2)), _mm512_set1_epi32(2));
        code = _mm512_mask_mov_epi32(code, _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0xf3)), _mm512_set1_epi32(3));
        code = _mm512_mask_mov_epi32(code, _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0xf0)), _mm512_set1_epi32(4));
        code = _mm512_mask_mov_epi32(code, cs, _mm512_set1_epi32(5));
        code = _mm512_mask_mov_epi32(code, _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x3e)), _mm512_set1_epi32(6));
        code = _mm512_mask_mov_epi32(code, _mm512_cmpeq_epi32_mask(byte, _mm512_set1_epi32(0x9b)), _mm512_set1_epi32(7));
        unmatched |= is_plain & (_mm512_cmpeq_epi32_mask(code, zero) | _mm512_cmpge_epu32_mask(plain, _mm512_set1_epi32(3)));
        malformed |= is_plain & _mm512_cmpge_epu32_mask(plain, _mm512_set1_epi32(4));
        codes = _mm512_mask_or_epi32(codes, is_plain, codes, _mm512_sllv_epi32(code, _mm512_add_epi32(_mm512_add_epi32(plain, plain), plain)));
        plain = _mm512_mask_add_epi32(plain, is_plain, plain, one);
        malformed |= active & is_segment & _mm512_cmpneq_epi32_mask(segment, zero);
        segment = _mm512_mask_mov_epi32(segment, active & fs, _mm512_set1_epi32(BUSTER_X86_SEGMENT_FS));
        segment = _mm512_mask_mov_epi32(segment, active & gs, _mm512_set1_epi32(BUSTER_X86_SEGMENT_GS));
        segment = _mm512_mask_mov_epi32(segment, active & ss, _mm512_set1_epi32(BUSTER_X86_SEGMENT_SS));
        malformed |= active & is_address & address32;
        address32 |= active & is_address;
        rex = _mm512_mask_mov_epi32(rex, active & is_rex, byte);
        operand |= active & word;
        padding &= ~active | word | cs;
    }
    const __m512i lead_at = _mm512_add_epi32(starts, run);
    const __m512i lead4 = x86_decode_lane_bytes(window, lead_at);
    const __m512i lead = _mm512_and_si512(lead4, byte_mask);
    const __m512i p0 = _mm512_and_si512(_mm512_srli_epi32(lead4, 8), byte_mask);
    const __m512i middle = _mm512_and_si512(_mm512_srli_epi32(lead4, 16), byte_mask);
    const __m512i p1 = _mm512_srli_epi32(lead4, 24);
    const uint16_t vex2 = _mm512_test_epi32_mask(kind, one);
    const uint16_t vex3 = _mm512_test_epi32_mask(kind, _mm512_set1_epi32(2));
    const uint16_t evex = _mm512_test_epi32_mask(kind, _mm512_set1_epi32(4));
    const uint16_t vector = vex2 | vex3 | evex;
    const __m512i opcode4 = x86_decode_lane_bytes(window, opcode_at);
    const __m512i opcode = _mm512_and_si512(opcode4, byte_mask);
    const __m512i next = _mm512_and_si512(_mm512_srli_epi32(opcode4, 8), byte_mask);
    // Clang's long NOPs: 66 and CS bytes before 0F 1F are padding, one 66 kept.
    padding &= _mm512_cmpgt_epu32_mask(run, zero) & ~vector & _mm512_cmpeq_epi32_mask(map, one) & _mm512_cmpeq_epi32_mask(opcode, _mm512_set1_epi32(0x1f)) &
               _mm512_cmpeq_epi32_mask(lead, _mm512_set1_epi32(0x0f));
    codes = _mm512_mask_mov_epi32(codes, padding, _mm512_maskz_mov_epi32(operand, one));
    plain = _mm512_mask_mov_epi32(plain, padding, _mm512_maskz_mov_epi32(operand, one));
    segment = _mm512_mask_mov_epi32(segment, padding, zero);
    malformed &= ~padding;
    unmatched &= ~padding;
    address32 &= ~padding;
    // The header's truncations and refusals, in x86_decode_header's order: the first that applies decides.
    const __m512i vector_bytes = _mm512_mask_mov_epi32(_mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(vex2, _mm512_set1_epi32(2)), vex3, _mm512_set1_epi32(3)), evex, _mm512_set1_epi32(4));
    const uint16_t short_input = _mm512_cmplt_epu32_mask(available, _mm512_set1_epi32(15));
    const uint16_t lead_missing = live & _mm512_cmpge_epu32_mask(lead_at, end);
    uint16_t truncated = lead_missing & short_input;
    uint16_t refused = lead_missing & ~short_input;
    uint16_t open = live & ~lead_missing;
    refused |= open & malformed;
    open &= ~malformed;
    const uint16_t vector_cut = open & vector & _mm512_cmpge_epu32_mask(_mm512_add_epi32(lead_at, vector_bytes), end);
    truncated |= vector_cut;
    open &= ~vector_cut;
    const uint16_t vector_refused = open & ((vector & (_mm512_cmpneq_epi32_mask(plain, zero) | _mm512_cmpneq_epi32_mask(rex, zero))) |
                                            (evex & (_mm512_test_epi32_mask(p0, _mm512_set1_epi32(8)) | _mm512_testn_epi32_mask(middle, _mm512_set1_epi32(4)) |
                                                     _mm512_cmpeq_epi32_mask(map, _mm512_set1_epi32(4)) | _mm512_cmpeq_epi32_mask(map, _mm512_set1_epi32(7)))));
    refused |= vector_refused;
    open &= ~vector_refused;
    const uint16_t opcode_cut = open & _mm512_cmpge_epu32_mask(opcode_at, end);
    truncated |= opcode_cut;
    open &= ~opcode_cut;
    const uint16_t unmapped = open & _mm512_cmpge_epu32_mask(map, _mm512_set1_epi32(BUSTER_X86_DECODE_MAPS));
    refused |= unmapped;
    open &= ~unmapped;
    // A prefix no form has matches nothing.
    refused |= open & unmatched;
    // The registers' high bits, vvvv, the decorations, W, L, pp and EVEX.b, as x86_decode_header has them.
    const __m512i not_p0 = _mm512_xor_si512(p0, byte_mask);
    __m512i r = _mm512_slli_epi32(_mm512_and_si512(rex, _mm512_set1_epi32(4)), 1);
    __m512i x = _mm512_slli_epi32(_mm512_and_si512(rex, _mm512_set1_epi32(2)), 2);
    __m512i b = _mm512_slli_epi32(_mm512_and_si512(rex, one), 3);
    __m512i w = _mm512_and_si512(_mm512_srli_epi32(rex, 3), one);
    r = _mm512_mask_mov_epi32(r, vector, _mm512_srli_epi32(_mm512_and_si512(not_p0, _mm512_set1_epi32(0x80)), 4));
    r = _mm512_mask_or_epi32(r, evex, r, _mm512_and_si512(not_p0, _mm512_set1_epi32(16)));
    x = _mm512_mask_mov_epi32(x, vex3 | evex, _mm512_srli_epi32(_mm512_and_si512(not_p0, _mm512_set1_epi32(0x40)), 3));
    b = _mm512_mask_mov_epi32(b, vex3 | evex, _mm512_srli_epi32(_mm512_and_si512(not_p0, _mm512_set1_epi32(0x20)), 2));
    x = _mm512_mask_mov_epi32(x, vex2, zero);
    b = _mm512_mask_mov_epi32(b, vex2, zero);
    w = _mm512_mask_mov_epi32(w, vex2, zero);
    w = _mm512_mask_mov_epi32(w, vex3 | evex, _mm512_srli_epi32(middle, 7));
    const __m512i high_rm = _mm512_maskz_mov_epi32(evex, _mm512_srli_epi32(_mm512_and_si512(not_p0, _mm512_set1_epi32(0x40)), 2));
    const __m512i high_index = _mm512_maskz_mov_epi32(evex, _mm512_slli_epi32(_mm512_andnot_si512(p1, _mm512_set1_epi32(8)), 1));
    __m512i vvvv = _mm512_maskz_mov_epi32(vex2, _mm512_and_si512(_mm512_srli_epi32(not_p0, 3), _mm512_set1_epi32(15)));
    vvvv = _mm512_mask_mov_epi32(vvvv, vex3 | evex, _mm512_and_si512(_mm512_srli_epi32(_mm512_xor_si512(middle, byte_mask), 3), _mm512_set1_epi32(15)));
    vvvv = _mm512_or_si512(vvvv, high_index);
    const __m512i decorations = _mm512_maskz_mov_epi32(evex, _mm512_and_si512(p1, _mm512_set1_epi32(0xe7)));
    __m512i length = _mm512_maskz_mov_epi32(vex2, _mm512_and_si512(_mm512_srli_epi32(p0, 2), one));
    length = _mm512_mask_mov_epi32(length, vex3, _mm512_and_si512(_mm512_srli_epi32(middle, 2), one));
    length = _mm512_mask_mov_epi32(length, evex, _mm512_and_si512(_mm512_srli_epi32(p1, 5), _mm512_set1_epi32(3)));
    __m512i pp = _mm512_maskz_mov_epi32(vex2, _mm512_and_si512(p0, _mm512_set1_epi32(3)));
    pp = _mm512_mask_mov_epi32(pp, vex3 | evex, _mm512_and_si512(middle, _mm512_set1_epi32(3)));
    const __m512i broadcast = _mm512_maskz_mov_epi32(evex, _mm512_and_si512(_mm512_srli_epi32(p1, 4), one));
    // The compact key (x86_decode_compact); a lane whose opcode is its last byte has no ModRM byte or memory bit, and
    // its signatures match on their other bits (x86_decode_one's `header.end == size`).
    const uint16_t has_next = _mm512_cmplt_epu32_mask(_mm512_add_epi32(opcode_at, one), end);
    __m512i key = _mm512_or_si512(codes, _mm512_slli_epi32(w, 9));
    key = _mm512_or_si512(key, _mm512_slli_epi32(length, 10));
    key = _mm512_or_si512(key, _mm512_slli_epi32(broadcast, 12));
    key = _mm512_or_si512(key, _mm512_slli_epi32(pp, 13));
    key = _mm512_mask_or_epi32(key, has_next, key, _mm512_slli_epi32(next, 15));
    key = _mm512_mask_or_epi32(key, has_next & _mm512_cmpneq_epi32_mask(_mm512_srli_epi32(next, 6), _mm512_set1_epi32(3)), key, _mm512_set1_epi32(1 << 23));
    const __m512i family = _mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(vex2 | vex3, one), evex, _mm512_set1_epi32(2));
    const __m512i bucket = _mm512_add_epi32(_mm512_slli_epi32(_mm512_add_epi32(_mm512_mullo_epi32(family, _mm512_set1_epi32(BUSTER_X86_DECODE_MAPS)), map), 8), opcode);
    // Each lane's row (the lane index): the bucket's common bits, then its cell by PEXT of its varying bits, a lane at a
    // time. A cell's list, and a lane without a ModRM byte (it matches its bucket's entries on the bits it has, in
    // order, into a list of its own), give the lane its rows in turn.
    const uint16_t open_lanes = live & ~(refused | truncated);
    uint32_t keys[16];
    uint32_t buckets[16];
    _mm512_storeu_si512(keys, key);
    _mm512_storeu_si512(buckets, _mm512_min_epu32(bucket, _mm512_set1_epi32(BUSTER_X86_DECODE_BUCKETS - 1)));
    const uint16_t* lists[16];
    uint16_t partial[16][65];
    uint32_t rows[16] = { 0 };
    uint16_t listed = 0;
    uint16_t pending = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const X86DecodeLaneIndex* index = &x86_decode_lane_indices[buckets[i]];
        const bool open_lane = ((open_lanes >> i) & 1) != 0;
        const bool next_byte = ((has_next >> i) & 1) != 0;
        const bool matched = open_lane && next_byte && (keys[i] & index->common_mask) == index->common_key;
        const uint32_t cell = matched ? x86_decode_cells[index->first + _pext_u32(keys[i], index->varying)] : 0;
        const bool list = (cell & BUSTER_X86_DECODE_LIST) != 0;
        // (A row's cell points at list 0, never read: a row past the lists' end is no pointer into them.)
        lists[i] = x86_decode_lists + (list ? cell & ~BUSTER_X86_DECODE_LIST : 0);
        rows[i] = list ? lists[i][0] : cell;
        listed |= (uint16_t)((uint32_t)list << i);
        pending |= (uint16_t)((uint32_t)(matched || (open_lane && !next_byte)) << i);
    }
    const uint16_t unindexed = open_lanes & ~has_next;
    for (uint16_t lanes = unindexed; lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t i = (uint32_t)__builtin_ctz(lanes);
        const X86DecodeBucket entries = x86_decode_buckets[buckets[i]];
        uint32_t found = 0;
        for (uint32_t j = 0; j < entries.count; j += 1)
        {
            const uint32_t entry = entries.first + j;
            const uint32_t mask = x86_decode_compact(x86_decode_masks[entry], true) & ((1u << 15) - 1);
            const uint32_t wanted = x86_decode_compact(x86_decode_keys[entry], false) & mask;
            partial[i][found] = x86_decode_forms[entry];
            found += (keys[i] & mask) == wanted;
        }
        partial[i][found] = 0;
        lists[i] = partial[i];
        rows[i] = partial[i][0];
    }
    // The candidates in rounds: a lane whose candidate is UNSUPPORTED takes its next one (x86_decode_one's order).
    uint16_t decoded = 0;
    uint32_t round = 0;
    __m512i done_immediate = zero;
    __m512i done_displacement = zero;
    __m512i done_registers = zero;
    __m512i done_tail = zero;
    __m512i done_length = zero;
    uint16_t done_address32 = 0;
    while (pending != 0)
    {
        // A later round's candidates: the next on each pending lane's list (a lane with a list has one); a lane done
        // reads row 0's entry.
        for (uint32_t i = 0; i < count; i += 1)
        {
            rows[i] = ((pending >> i) & 1) == 0 ? 0 : round == 0 ? rows[i] : ((listed | unindexed) >> i) & 1 ? lists[i][round] : 0;
        }
        __m512i entries[4];
        x86_decode_lane_rows_load(rows, entries);
        const __m512i row = _mm512_srli_epi32(entries[3], 16);
        // A lane out of candidates is UNSUPPORTED.
        const uint16_t candidates = pending & _mm512_cmpneq_epi32_mask(row, zero);
        pending = candidates;
        const __m512i form = entries[0];
        const __m512i check_registers = entries[1];
        const __m512i check_memory = entries[2];
        const __m512i flags = _mm512_and_si512(entries[3], _mm512_set1_epi32(0xffff));
        const uint16_t has_modrm = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_MODRM));
        const uint16_t has_reg = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_REG));
        const uint16_t plus = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_PLUS));
        const uint16_t fixed = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_FIXED));
        const uint16_t fixed_plus = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_FIXED_PLUS));
        const uint16_t form_address = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_ADDRESS));
        const uint16_t allowed_segment = _mm512_test_epi32_mask(_mm512_srlv_epi32(form, _mm512_add_epi32(segment, _mm512_set1_epi32(BUSTER_X86_LANE_SEGMENTS))), one);
        const uint16_t nop = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_NOP));
        const uint16_t lane_address32 = address32 & ~form_address;
        uint16_t valid = pending & (address32 | ~form_address) & allowed_segment & ~(nop & _mm512_test_epi32_mask(b, b));
        __m512i at = _mm512_add_epi32(opcode_at, one);
        const __m512i fixed_byte = _mm512_and_si512(x86_decode_lane_bytes(window, at), byte_mask);
        at = _mm512_mask_add_epi32(at, fixed, at, one);
        __m512i rm = _mm512_maskz_mov_epi32(plus, _mm512_or_si512(_mm512_and_si512(opcode, _mm512_set1_epi32(7)), b));
        rm = _mm512_mask_mov_epi32(rm, fixed_plus, _mm512_or_si512(_mm512_and_si512(fixed_byte, _mm512_set1_epi32(7)), b));
        const __m512i modrm4 = x86_decode_lane_bytes(window, at);
        const __m512i modrm = _mm512_and_si512(modrm4, byte_mask);
        const __m512i sib = _mm512_and_si512(_mm512_srli_epi32(modrm4, 8), byte_mask);
        at = _mm512_mask_add_epi32(at, has_modrm, at, one);
        const __m512i mod = _mm512_srli_epi32(modrm, 6);
        const __m512i modrm_rm = _mm512_and_si512(modrm, _mm512_set1_epi32(7));
        const __m512i reg = _mm512_maskz_mov_epi32(has_reg, _mm512_or_si512(_mm512_and_si512(_mm512_srli_epi32(modrm, 3), _mm512_set1_epi32(7)), r));
        const uint16_t direct = has_modrm & _mm512_cmpeq_epi32_mask(mod, _mm512_set1_epi32(3));
        const uint16_t memory = has_modrm & ~direct;
        rm = _mm512_mask_mov_epi32(rm, direct, _mm512_or_si512(_mm512_or_si512(modrm_rm, b), high_rm));
        const uint16_t has_sib = memory & _mm512_cmpeq_epi32_mask(modrm_rm, _mm512_set1_epi32(4));
        const uint16_t mod0 = _mm512_cmpeq_epi32_mask(mod, zero);
        const uint16_t vsib = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_VSIB));
        const uint16_t sib_operand = _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_SIB));
        const __m512i index = _mm512_or_si512(_mm512_or_si512(_mm512_and_si512(_mm512_srli_epi32(sib, 3), _mm512_set1_epi32(7)), x), _mm512_maskz_mov_epi32(vsib, high_index));
        const uint16_t has_index = has_sib & (_mm512_cmpneq_epi32_mask(index, _mm512_set1_epi32(4)) | vsib | sib_operand);
        const uint16_t sib_base5 = _mm512_cmpeq_epi32_mask(_mm512_and_si512(sib, _mm512_set1_epi32(7)), _mm512_set1_epi32(5));
        const uint16_t sib_base = has_sib & ~(mod0 & sib_base5);
        const uint16_t rip = memory & ~has_sib & mod0 & _mm512_cmpeq_epi32_mask(modrm_rm, _mm512_set1_epi32(5));
        const uint16_t plain_base = memory & ~has_sib & ~rip;
        rm = _mm512_mask_mov_epi32(rm, sib_base, _mm512_or_si512(_mm512_and_si512(sib, _mm512_set1_epi32(7)), b));
        rm = _mm512_mask_mov_epi32(rm, plain_base, _mm512_or_si512(modrm_rm, b));
        __m512i mode = _mm512_maskz_mov_epi32(memory, _mm512_set1_epi32(BUSTER_X86_MODE_MEMORY));
        mode = _mm512_mask_or_epi32(mode, has_index, mode, _mm512_or_si512(_mm512_set1_epi32(BUSTER_X86_MODE_INDEX), _mm512_srli_epi32(sib, 6)));
        mode = _mm512_mask_or_epi32(mode, sib_base | plain_base, mode, _mm512_set1_epi32(BUSTER_X86_MODE_BASE));
        mode = _mm512_mask_or_epi32(mode, rip, mode, _mm512_set1_epi32(BUSTER_X86_MODE_RIP));
        at = _mm512_mask_add_epi32(at, has_sib, at, one);
        const uint16_t disp1 = memory & _mm512_cmpeq_epi32_mask(mod, one);
        const uint16_t disp4 = (memory & _mm512_cmpeq_epi32_mask(mod, _mm512_set1_epi32(2))) | rip | (has_sib & mod0 & sib_base5);
        const __m512i displacement_raw = x86_decode_lane_bytes(window, at);
        __m512i displacement = _mm512_maskz_mov_epi32(disp4, displacement_raw);
        const __m512i scale = _mm512_and_si512(_mm512_srli_epi32(form, 8), byte_mask);
        displacement = _mm512_mask_mov_epi32(displacement, disp1, _mm512_mullo_epi32(_mm512_srai_epi32(_mm512_slli_epi32(displacement_raw, 24), 24), scale));
        at = _mm512_mask_add_epi32(at, disp1, at, one);
        at = _mm512_mask_add_epi32(at, disp4, at, _mm512_set1_epi32(4));
        const __m512i relative_width = _mm512_and_si512(_mm512_srli_epi32(form, 4), _mm512_set1_epi32(15));
        const __m512i relative_raw = x86_decode_lane_bytes(window, at);
        const __m512i relative_shift = _mm512_sub_epi32(_mm512_set1_epi32(32), _mm512_slli_epi32(relative_width, 3));
        displacement = _mm512_mask_mov_epi32(displacement, _mm512_cmpneq_epi32_mask(relative_width, zero), _mm512_srav_epi32(_mm512_sllv_epi32(relative_raw, relative_shift), relative_shift));
        at = _mm512_add_epi32(at, relative_width);
        const __m512i immediate_width = _mm512_and_si512(form, _mm512_set1_epi32(15));
        const __m512i immediate_raw = x86_decode_lane_bytes(window, at);
        const __m512i immediate_shift = _mm512_sub_epi32(_mm512_set1_epi32(32), _mm512_slli_epi32(_mm512_min_epu32(immediate_width, _mm512_set1_epi32(4)), 3));
        const __m512i immediate = _mm512_and_si512(immediate_raw, _mm512_srlv_epi32(_mm512_set1_epi32(-1), immediate_shift));
        const uint16_t immediate64 = _mm512_cmpeq_epi32_mask(immediate_width, _mm512_set1_epi32(8));
        displacement = _mm512_mask_mov_epi32(displacement, immediate64, x86_decode_lane_bytes(window, _mm512_add_epi32(at, _mm512_set1_epi32(4))));
        at = _mm512_add_epi32(at, immediate_width);
        // VEX's and EVEX's vvvv and decorations; without REX, a byte register 4-7 is AH, CH, DH or BH.
        __m512i lane_vvvv = _mm512_maskz_mov_epi32(vector, vvvv);
        lane_vvvv = _mm512_mask_and_epi32(lane_vvvv, evex & vsib, lane_vvvv, _mm512_set1_epi32(15));
        const __m512i decoration_mask = _mm512_mask_blend_epi32(_mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_ROUNDING)), _mm512_set1_epi32(0x87), _mm512_set1_epi32(0xe7));
        const __m512i lane_decorations = _mm512_maskz_and_epi32(vector, decorations, decoration_mask);
        const uint16_t no_rex = ~vector & _mm512_cmpeq_epi32_mask(rex, zero);
        const __m512i four = _mm512_set1_epi32(4);
        const uint16_t high_reg = no_rex & _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_REG8)) & _mm512_cmpge_epu32_mask(reg, four);
        const uint16_t high_rm8 = no_rex & ~memory & _mm512_test_epi32_mask(form, _mm512_set1_epi32(BUSTER_X86_LANE_RM8)) & _mm512_cmpge_epu32_mask(rm, four);
        const __m512i lane_reg = _mm512_mask_or_epi32(reg, high_reg, reg, _mm512_set1_epi32(BUSTER_X86_REGISTER_HIGH_BYTE));
        const __m512i lane_rm = _mm512_mask_or_epi32(rm, high_rm8, rm, _mm512_set1_epi32(BUSTER_X86_REGISTER_HIGH_BYTE));
        const __m512i lane_index = _mm512_maskz_mov_epi32(has_index, index);
        // Its length, complete when the input has it, and too long past 15 bytes.
        const __m512i instruction_length = _mm512_sub_epi32(at, starts);
        const uint16_t overlong = _mm512_cmpgt_epu32_mask(instruction_length, _mm512_set1_epi32(15));
        const uint16_t complete = _mm512_cmple_epu32_mask(at, end) & ~overlong;
        // The compact checks (x86_decode_valid); the other rows take the full validator.
        const __m512i fields = _mm512_or_si512(_mm512_or_si512(lane_reg, _mm512_slli_epi32(lane_rm, 8)), _mm512_or_si512(_mm512_slli_epi32(lane_vvvv, 16), _mm512_slli_epi32(lane_decorations, 24)));
        const uint16_t compact = _mm512_test_epi32_mask(flags, one);
        const uint16_t memory_mode = _mm512_test_epi32_mask(mode, _mm512_set1_epi32(BUSTER_X86_MODE_MEMORY));
        const __m512i allowed_shift = _mm512_add_epi32(_mm512_add_epi32(_mm512_set1_epi32(2), _mm512_maskz_mov_epi32(lane_address32, four)), segment);
        __m512i allowed = _mm512_maskz_mov_epi32(memory_mode, _mm512_sllv_epi32(one, allowed_shift));
        allowed = _mm512_mask_mov_epi32(allowed, ~memory_mode & _mm512_cmpeq_epi32_mask(segment, zero), _mm512_set1_epi32(2));
        const __m512i check = _mm512_mask_blend_epi32(memory_mode, check_registers, check_memory);
        const uint16_t zeroing = _mm512_test_epi32_mask(lane_decorations, _mm512_set1_epi32(BUSTER_X86_DECORATION_ZEROING));
        const uint16_t unmasked = _mm512_testn_epi32_mask(lane_decorations, _mm512_set1_epi32(BUSTER_X86_DECORATION_MASK));
        const uint16_t memory_zeroing = memory_mode & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(1 << 10));
        const uint16_t checked = _mm512_test_epi32_mask(flags, allowed) & _mm512_testn_epi32_mask(fields, check) & ~(zeroing & (unmasked | memory_zeroing));
        const __m512i record_registers = _mm512_or_si512(_mm512_or_si512(lane_reg, _mm512_slli_epi32(lane_rm, 8)), _mm512_or_si512(_mm512_slli_epi32(lane_index, 16), _mm512_slli_epi32(mode, 24)));
        const __m512i record_tail = _mm512_or_si512(_mm512_add_epi32(row, _mm512_mullo_epi32(segment, _mm512_set1_epi32(X86_FORM_COUNT))),
                                                    _mm512_or_si512(_mm512_slli_epi32(lane_vvvv, 16), _mm512_slli_epi32(lane_decorations, 24)));
        uint16_t accepted = valid & complete & compact & checked;
        uint16_t full = valid & complete & ~compact;
        if (full != 0)
        {
            uint32_t words[4][16];
            _mm512_storeu_si512(words[0], immediate);
            _mm512_storeu_si512(words[1], displacement);
            _mm512_storeu_si512(words[2], record_registers);
            _mm512_storeu_si512(words[3], record_tail);
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (((full >> i) & 1) != 0)
                {
                    X86Instruction instruction;
                    memcpy(&instruction.immediate, &words[0][i], 4);
                    memcpy(&instruction.displacement, &words[1][i], 4);
                    memcpy(&instruction.reg, &words[2][i], 4);
                    memcpy(&instruction.form, &words[3][i], 4);
                    // With a 32-bit address, the validator's room test reads templates the disassembler doesn't build; the
                    // cramped forms' table answers it for a memory operand.
                    const uint32_t number = instruction.form & BUSTER_X86_FORM_NUMBER;
                    const bool cramped = (instruction.mode & BUSTER_X86_MODE_MEMORY) != 0 && ((x86_decode_address32_cramped[number / 64] >> (number % 64)) & 1) != 0;
                    const bool lane_valid = ((lane_address32 >> i) & 1) != 0 ? !cramped && x86_instruction_is_valid_address32(&instruction) : x86_instruction_is_valid(&instruction);
                    accepted |= (uint16_t)((uint32_t)lane_valid << i);
                }
            }
        }
        const uint16_t cut = valid & ~complete & ~overlong;
        done_immediate = _mm512_mask_mov_epi32(done_immediate, accepted, immediate);
        done_displacement = _mm512_mask_mov_epi32(done_displacement, accepted, displacement);
        done_registers = _mm512_mask_mov_epi32(done_registers, accepted, record_registers);
        done_tail = _mm512_mask_mov_epi32(done_tail, accepted, record_tail);
        done_length = _mm512_mask_mov_epi32(done_length, accepted, instruction_length);
        done_address32 |= accepted & lane_address32;
        decoded |= accepted;
        truncated |= cut;
        pending &= ~accepted & ~cut;
        round += 1;
    }
    // A lane starting with FWAIT whose waiting form didn't decode is FWAIT alone, one byte.
    const uint16_t wait = live & ~decoded & _mm512_cmpeq_epi32_mask(_mm512_and_si512(x86_decode_lane_bytes(window, starts), byte_mask), _mm512_set1_epi32(0x9b));
    done_tail = _mm512_mask_mov_epi32(done_tail, wait, _mm512_set1_epi32(X86_FORM_FWAIT));
    done_length = _mm512_mask_mov_epi32(done_length, wait, one);
    done_immediate = _mm512_mask_mov_epi32(done_immediate, wait, zero);
    done_displacement = _mm512_mask_mov_epi32(done_displacement, wait, zero);
    done_registers = _mm512_mask_mov_epi32(done_registers, wait, zero);
    done_address32 &= ~wait;
    decoded |= wait;
    truncated &= ~wait;
    // In order: the lanes before the first failure, and none after a lane whose length isn't its hint.
    const __m512i hint = x86_decode_lane_field(lane_starts, _mm512_loadu_si512(lengths));
    const uint32_t failed = (uint32_t)(live & ~decoded) | (1u << count);
    const uint32_t mismatched = (uint32_t)(decoded & _mm512_cmpneq_epi32_mask(hint, done_length)) | (1u << count);
    const uint32_t first_failure = (uint32_t)__builtin_ctz(failed);
    const uint32_t last = (uint32_t)__builtin_ctz(mismatched) + 1;
    const uint32_t written = first_failure < last ? first_failure : last < count ? last : count;
    *status = first_failure < count && first_failure < last ? (((truncated >> first_failure) & 1) != 0 ? X86_DECODE_TRUNCATED : X86_DECODE_UNSUPPORTED) : X86_DECODE_OK;
    // Where the last written instruction ends, from the lanes (not read back from the records just stored).
    const __m512i last_lane = _mm512_set1_epi32((int)(written != 0 ? written - 1 : 0));
    *consumed = written != 0 ? (uint32_t)_mm512_cvtsi512_si32(_mm512_permutexvar_epi32(last_lane, _mm512_add_epi32(starts, done_length))) : 0;
    // The records' six dwords (X86Decoded) a lane, transposed into 24-byte records in lane order.
    const __m512i where = _mm512_add_epi32(starts, _mm512_set1_epi32((int)start));
    const __m512i sizes = _mm512_mask_or_epi32(done_length, done_address32, done_length, _mm512_set1_epi32(1 << 8));
    const __m512i pairs[3][2] = {
        { _mm512_unpacklo_epi32(done_immediate, done_displacement), _mm512_unpackhi_epi32(done_immediate, done_displacement) },
        { _mm512_unpacklo_epi32(done_registers, done_tail), _mm512_unpackhi_epi32(done_registers, done_tail) },
        { _mm512_unpacklo_epi32(where, sizes), _mm512_unpackhi_epi32(where, sizes) },
    };
    // unpacklo has lanes 0, 1, 4, 5, 8, 9, 12, 13 and unpackhi 2, 3, 6, 7, ...: back in order, 8 lanes a register.
    const __m512i order_low = _mm512_set_epi64(11, 10, 3, 2, 9, 8, 1, 0);
    const __m512i order_high = _mm512_set_epi64(15, 14, 7, 6, 13, 12, 5, 4);
    __m512i words[3][2];
    for (uint32_t f = 0; f < 3; f += 1)
    {
        words[f][0] = _mm512_permutex2var_epi64(pairs[f][0], order_low, pairs[f][1]);
        words[f][1] = _mm512_permutex2var_epi64(pairs[f][0], order_high, pairs[f][1]);
    }
    // Eight lanes' 24 qwords, three registers: record r's qwords are A[r], B[r], C[r].
    // (A from 0-7, B from 8-15; C's elements, by mask, from C.)
    const __m512i ab[3] = { _mm512_set_epi64(10, 2, 0, 9, 1, 0, 8, 0), _mm512_set_epi64(5, 0, 12, 4, 0, 11, 3, 0), _mm512_set_epi64(0, 15, 7, 0, 14, 6, 0, 13) };
    const __m512i c[3] = { _mm512_set_epi64(0, 0, 1, 0, 0, 0, 0, 0), _mm512_set_epi64(0, 4, 0, 0, 3, 0, 0, 2), _mm512_set_epi64(7, 0, 0, 6, 0, 0, 5, 0) };
    const __mmask8 from_c[3] = { 0x24, 0x49, 0x92 };
    uint8_t* bytes = (uint8_t*)output;
    for (uint32_t half = 0; half < 2; half += 1)
    {
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const uint32_t qword = half * 24 + k * 8;
            const uint32_t wanted = written * 3;
            const __mmask8 store = (__mmask8)(qword >= wanted ? 0 : wanted - qword >= 8 ? 0xff : (1u << (wanted - qword)) - 1);
            const __m512i merged = _mm512_permutex2var_epi64(words[0][half], ab[k], words[1][half]);
            const __m512i record = _mm512_mask_permutexvar_epi64(merged, from_c[k], c[k], words[2][half]);
            _mm512_mask_storeu_epi64(bytes + (uint64_t)qword * 8, store, record);
        }
    }
    return written;
}

// Decodes `source[0, size)` into at most `capacity` records, a window at a time: the length pass (x86_decode_lengths)
// chains up to 16 instruction starts through the hints, and the lane decoder (x86_decode_lanes) decodes them in one
// pass; the next window starts after the last instruction decoded.
static X86DecodeResult x86_disassemble(const uint8_t* source, uint64_t size, X86Decoded* output, uint64_t capacity)
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
            uint8_t lengths[64];
            X86DecodeWindow window;
            x86_decode_lengths(source + start, remaining, lengths, &window);
            // The starts (each lane's 15 bytes lie inside the window's 128): lane i is the i-th instruction from 0
            // through the hints, by pointer jumping. A position without a hint leads past the window (127), so the
            // chain stops after it.
            const __m512i positions = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35,
                                                      34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6,
                                                      5, 4, 3, 2, 1, 0);
            const __m512i hints = _mm512_loadu_si512(lengths);
            const __m512i past = _mm512_set1_epi8(127);
            __m512i jump = _mm512_mask_add_epi8(past, _mm512_test_epi8_mask(hints, hints), positions, hints);
            const __m512i lanes = _mm512_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
            __m512i at = _mm512_setzero_si512();
            for (uint32_t k = 0; k < 4; k += 1)
            {
                at = _mm512_mask_mov_epi8(at, _mm512_test_epi8_mask(lanes, _mm512_set1_epi8((char)(1 << k))), _mm512_permutex2var_epi8(jump, at, past));
                jump = _mm512_permutex2var_epi8(jump, jump, past);
            }
            const uint64_t limit = remaining < 64 ? remaining : 64;
            const uint64_t room = capacity - result.count < 16 ? capacity - result.count : 16;
            const uint32_t inside = (uint32_t)(_mm512_cmplt_epu8_mask(at, _mm512_set1_epi8((char)limit)) & 0xffff);
            const uint32_t reached = (uint32_t)__builtin_ctz(~inside);
            const uint32_t count = reached < room ? reached : (uint32_t)room;
            uint8_t offsets[16];
            _mm_storeu_si128((__m128i*)offsets, _mm512_castsi512_si128(at));
            // The window's end by the hints alone, which the next window's length pass can start from while this
            // one's lanes decode (a branch, not a data dependency); their end replaces it only when a lane failed or
            // wasn't its hint's length.
            const uint32_t hinted_end = offsets[count - 1] + lengths[offsets[count - 1]];
            X86DecodeStatus status = X86_DECODE_OK;
            uint32_t consumed = 0;
            const uint32_t written = x86_decode_lanes(&window, remaining, offsets, lengths, count, start, output + result.count, &status, &consumed);
            result.count += written;
            result.consumed = start + hinted_end;
            if (__builtin_expect(status != X86_DECODE_OK || consumed != hinted_end, 0))
            {
                result.consumed = start + consumed;
            }
            result.status = status;
        }
    }
    return result;
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
        x86_disassembly_append(text, segment == BUSTER_X86_SEGMENT_FS ? "fs:" : segment == BUSTER_X86_SEGMENT_GS ? "gs:" : "ss:");
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
