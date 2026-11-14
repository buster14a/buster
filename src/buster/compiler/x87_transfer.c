// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded, exact transport of canonical x87 results between compiler-private homes (#1387). Include after the x86
// encoder and semantics. The adapters identify each private definition by its FSTP record and certify a single use
// of that definition. Raw F80 loads are deliberately not canonical: AMD's documented FSTP invalid-operation effects
// do not justify removing their first memory transfer. Arithmetic results and conversions from scalar values are.
//
// Keep the result twice instead of storing it: FSTP m80 -> FLD ST(0), then its sole reload -> FSTP ST(0). Both original
// waiting points remain, neither instruction converts the canonical value, and every other instruction stays in
// place. The reload must begin with the baseline x87 stack empty. Only one such pair is live at a time; all intervening
// x87 sequences run above its two copies. This costs no unmeasured form and changes no record index or metadata.
//
// Candidate stores are classified sixteen at a time, with an exact scalar twin. The ordered proof is bounded by a
// 64-record region and eight physical registers. It independently replays the proposed records before publication.

#ifndef BUSTER_X87_TRANSFER_INCLUDED
#define BUSTER_X87_TRANSFER_INCLUDED

#define BUSTER_X87_TRANSFER_NONE UINT32_MAX
#define BUSTER_X87_TRANSFER_BOUNDARY 1u
#define BUSTER_X87_TRANSFER_CUT 2u
#define BUSTER_X87_TRANSFER_RECORDS 64u

typedef struct X87TransferStats X87TransferStats;
struct X87TransferStats
{
    uint32_t pairs;
    uint32_t regions;
};

typedef enum X87TransferKind
{
    X87_TRANSFER_STOP,
    X87_TRANSFER_INTEGER,
    X87_TRANSFER_LOAD,
    X87_TRANSFER_STORE,
    X87_TRANSFER_BINARY,
    X87_TRANSFER_UNARY,
} X87TransferKind;

// Plain register arithmetic and addresses may lie between the two waiting points. Keep all memory, calls, control,
// SIMD, environment and unrecognized instructions outside a region, and never change a possible frame base there.
static X87TransferKind x87_transfer_kind(const X86Instruction* record)
{
    const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
    X87TransferKind kind = X87_TRANSFER_STOP;
    if (form == X86_FORM_NONE)
    {
        kind = X87_TRANSFER_INTEGER;
    }
    else if (form == X86_FORM_FLD_M80 || form == X86_FORM_FLD_M32 || form == X86_FORM_FLD_M64 || form == X86_FORM_FILD_M32 || form == X86_FORM_FILD_M64 || form == X86_FORM_FLDZ || form == X86_FORM_FLD1)
    {
        kind = X87_TRANSFER_LOAD;
    }
    else if (form == X86_FORM_FSTP_M80)
    {
        kind = X87_TRANSFER_STORE;
    }
    else if ((form == X86_FORM_FADDP_ST_ST0 || form == X86_FORM_FSUBP_ST_ST0 || form == X86_FORM_FMULP_ST_ST0 || form == X86_FORM_FDIVP_ST_ST0) && record->rm == 1 && record->mode == BUSTER_X86_REGISTER_DIRECT)
    {
        kind = X87_TRANSFER_BINARY;
    }
    else if (form == X86_FORM_FCHS || form == X86_FORM_FABS)
    {
        kind = X87_TRANSFER_UNARY;
    }
    else
    {
        const X86Effects* effects = x86_effects(record->form);
        const uint32_t operation = effects->operation;
        const bool general = (operation >= X86_OPERATION_ADD && operation <= X86_OPERATION_LEA) ||
                             (operation >= X86_OPERATION_NOT && operation <= X86_OPERATION_IMUL) ||
                             (operation >= X86_OPERATION_ROL && operation <= X86_OPERATION_SAR) ||
                             operation == X86_OPERATION_SETCC || operation == X86_OPERATION_CMOVCC || operation == X86_OPERATION_SIGN_EXTEND || operation == X86_OPERATION_SIGN_SPLIT ||
                             operation == X86_OPERATION_NOP || operation == X86_OPERATION_BSWAP;
        const uint32_t protected = (1u << X86_REGISTER_RSP) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R15);
        const bool writes_reg = (x86_effects_role(effects, BUSTER_X86_ROLE_REG) & BUSTER_X86_ROLE_WRITE) != 0;
        const bool writes_rm = record->mode == BUSTER_X86_REGISTER_DIRECT && (x86_effects_role(effects, BUSTER_X86_ROLE_RM) & BUSTER_X86_ROLE_WRITE) != 0;
        const bool stable = (effects->implicit_writes & protected) == 0 &&
                            !(writes_reg && record->reg < 16 && ((protected >> record->reg) & 1) != 0) &&
                            !(writes_rm && record->rm < 16 && ((protected >> record->rm) & 1) != 0);
        const bool plain = record->mode == BUSTER_X86_REGISTER_DIRECT || operation == X86_OPERATION_LEA;
        const uint32_t forbidden = BUSTER_X86_EFFECT_FAULT | BUSTER_X86_EFFECT_X87 | BUSTER_X86_EFFECT_X87_CONTROL | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_RARE;
        if (general && plain && stable && (effects->properties & forbidden) == 0 && (effects->kinds >> 4) == BUSTER_X86_CONTROL_NONE &&
            (effects->memory & ~BUSTER_X86_MEMORY_EFFECT_ADDRESS) == 0 && record->decorations == 0)
        {
            kind = X87_TRANSFER_INTEGER;
        }
    }
    return kind;
}

static bool x87_transfer_home(const X86Instruction* store, const X86Instruction* load)
{
    const uint32_t base = store->rm;
    return store->form == X86_FORM_FSTP_M80 && load->form == X86_FORM_FLD_M80 && store->mode == BUSTER_X86_MEMORY_BASE &&
           load->mode == BUSTER_X86_MEMORY_BASE && (base == X86_REGISTER_RSP || base == X86_REGISTER_RBP || base == X86_REGISTER_R15) &&
           load->rm == base && store->displacement == load->displacement && store->decorations == 0 && load->decorations == 0;
}

static bool x87_transfer_overlap(const X86Instruction* home, const X86Instruction* record)
{
    const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
    const uint32_t bytes = form == X86_FORM_FLD_M80 || form == X86_FORM_FSTP_M80 ? 10 : form == X86_FORM_FLD_M32 || form == X86_FORM_FILD_M32 ? 4 :
                           form == X86_FORM_FLD_M64 || form == X86_FORM_FILD_M64 ? 8 : 0;
    const int64_t delta = (int64_t)record->displacement - home->displacement;
    return bytes != 0 && record->mode == BUSTER_X86_MEMORY_BASE && record->rm == home->rm && delta < 10 && delta + bytes > 0;
}

// A span begins and ends at an adapter's known-empty boundary. Its internal peak is checked independently below.
static bool x87_transfer_span(const X86Instruction* records, uint32_t first, uint32_t end)
{
    uint32_t depth = 0;
    bool valid = end - first <= BUSTER_X87_TRANSFER_RECORDS;
    for (uint32_t r = first; valid && r < end; r += 1)
    {
        const X87TransferKind kind = x87_transfer_kind(&records[r]);
        valid = kind != X87_TRANSFER_STOP && (kind != X87_TRANSFER_STORE || depth >= 1) && (kind != X87_TRANSFER_BINARY || depth >= 2) && (kind != X87_TRANSFER_UNARY || depth >= 1);
        if (valid)
        {
            depth += kind == X87_TRANSFER_LOAD;
            depth -= kind == X87_TRANSFER_STORE || kind == X87_TRANSFER_BINARY;
            valid = depth <= 8;
        }
    }
    return valid && depth == 0;
}

static uint64_t x87_transfer_candidates(const X86Instruction* records, const uint32_t* tokens, const uint8_t* eligible, uint32_t first, uint32_t count, bool vector)
{
    uint64_t candidates = 0;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i indices = _mm512_setr_epi32(0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240);
        const __m512i lanes_ids = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (uint32_t r = 0; r < count; r += 16)
        {
            const uint32_t left = count - r;
            const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
            const __m512i words = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, indices, &records[first + r].form, 1);
            const __m512i forms = _mm512_and_si512(words, _mm512_set1_epi32(0xffff));
            const __m512i ids = _mm512_add_epi32(lanes_ids, _mm512_set1_epi32((int32_t)(first + r)));
            const __m512i names = _mm512_maskz_loadu_epi32(lanes, tokens + first + r);
            const __mmask16 stores = lanes & _mm512_cmpeq_epi32_mask(forms, _mm512_set1_epi32(X86_FORM_FSTP_M80)) & _mm512_cmpeq_epi32_mask(names, ids);
            candidates |= (uint64_t)stores << r;
        }
    }
    else
    {
        for (uint32_t r = 0; r < count; r += 1)
        {
            candidates |= (uint64_t)(records[first + r].form == X86_FORM_FSTP_M80 && tokens[first + r] == first + r) << r;
        }
    }
    uint64_t enabled = candidates;
    while (enabled != 0)
    {
        const uint32_t r = (uint32_t)__builtin_ctzll(enabled);
        candidates &= eligible[first + r] != 0 ? UINT64_MAX : ~(UINT64_C(1) << r);
        enabled &= enabled - 1;
    }
    return candidates;
}

// Replay original operations and proposed records independently. Identities name source snapshots and arithmetic
// results, never a numeric value: every operand, operation and observable access must match in its original order.
static bool x87_transfer_verify(const X86Instruction* original, const X86Instruction* proposed, const uint32_t* tokens, const uint8_t* eligible, uint32_t first, uint32_t count)
{
    uint64_t before[8] = { 0 };
    uint64_t after[8] = { 0 };
    uint64_t homes[BUSTER_X87_TRANSFER_RECORDS] = { 0 };
    uint32_t old_depth = 0;
    uint32_t new_depth = 0;
    uint32_t live = BUSTER_X87_TRANSFER_NONE;
    bool valid = count <= BUSTER_X87_TRANSFER_RECORDS;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const uint32_t token = tokens[first + r];
        const bool local = token >= first && token - first < count;
        const X87TransferKind kind = x87_transfer_kind(&original[r]);
        const bool same = memcmp(&original[r], &proposed[r], sizeof(X86Instruction)) == 0;
        const uint64_t identity = (uint64_t)first + r + 1;
        // No unannotated reader or overlapping definition may observe an omitted store. Token identity alone is not
        // a proof of private storage: check the actual memory ranges independently of the adapter's use certificate.
        valid = live == BUSTER_X87_TRANSFER_NONE || !x87_transfer_overlap(&original[live - first], &original[r]) ||
                (kind == X87_TRANSFER_LOAD && token == live && !same);
        if (valid && kind == X87_TRANSFER_LOAD)
        {
            const uint64_t value = local && homes[token - first] != 0 ? homes[token - first] : identity;
            valid = old_depth < 8;
            if (valid && same)
            {
                valid = new_depth < 8;
                if (valid)
                {
                    after[new_depth++] = value;
                }
            }
            else if (valid)
            {
                const X86Instruction pop = { .form = X86_FORM_FSTP_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
                valid = old_depth == 0 && new_depth == 2 && live == token && local && eligible[token] != 0 &&
                        x87_transfer_home(&original[token - first], &original[r]) && memcmp(&proposed[r], &pop, sizeof(pop)) == 0 &&
                        after[0] == value && after[1] == value;
                if (valid)
                {
                    new_depth -= 1;
                    live = BUSTER_X87_TRANSFER_NONE;
                }
            }
            if (valid)
            {
                before[old_depth++] = value;
            }
        }
        else if (valid && kind == X87_TRANSFER_STORE)
        {
            valid = old_depth != 0 && new_depth != 0 && before[old_depth - 1] == after[new_depth - 1];
            if (valid && local && token == first + r)
            {
                homes[r] = before[old_depth - 1];
            }
            if (valid && same)
            {
                new_depth -= 1;
            }
            else if (valid)
            {
                const X86Instruction duplicate = { .form = X86_FORM_FLD_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
                valid = old_depth == 1 && new_depth == 1 && live == BUSTER_X87_TRANSFER_NONE && token == first + r && eligible[token] != 0 &&
                        memcmp(&proposed[r], &duplicate, sizeof(duplicate)) == 0;
                if (valid)
                {
                    after[new_depth] = after[new_depth - 1];
                    new_depth += 1;
                    live = token;
                }
            }
            if (valid)
            {
                old_depth -= 1;
            }
        }
        else if (valid && (kind == X87_TRANSFER_BINARY || kind == X87_TRANSFER_UNARY))
        {
            const uint32_t operands = kind == X87_TRANSFER_BINARY ? 2 : 1;
            valid = same && old_depth >= operands && new_depth >= operands;
            for (uint32_t k = 0; valid && k < operands; k += 1)
            {
                valid = before[old_depth - 1 - k] == after[new_depth - 1 - k];
            }
            if (valid)
            {
                old_depth -= operands - 1;
                new_depth -= operands - 1;
                before[old_depth - 1] = identity;
                after[new_depth - 1] = identity;
            }
        }
        else if (valid)
        {
            valid = kind == X87_TRANSFER_INTEGER && same;
        }
    }
    return valid && old_depth == 0 && new_depth == 0 && live == BUSTER_X87_TRANSFER_NONE;
}

static uint32_t x87_transfer_region(X86Instruction* records, const uint32_t* tokens, const uint8_t* eligible, uint32_t first, uint32_t end, bool vector)
{
    const uint32_t count = end - first;
    X86Instruction proposed[BUSTER_X87_TRANSFER_RECORDS];
    uint8_t depths[BUSTER_X87_TRANSFER_RECORDS + 1] = { 0 };
    memcpy(proposed, records + first, (uint64_t)count * sizeof(X86Instruction));
    for (uint32_t r = 0; r < count; r += 1)
    {
        const X87TransferKind kind = x87_transfer_kind(&records[first + r]);
        depths[r + 1] = (uint8_t)(depths[r] + (kind == X87_TRANSFER_LOAD) - (kind == X87_TRANSFER_STORE || kind == X87_TRANSFER_BINARY));
    }
    uint64_t candidates = x87_transfer_candidates(records, tokens, eligible, first, count, vector);
    uint32_t pairs = 0;
    uint32_t available = 0;
    while (candidates != 0)
    {
        const uint32_t store = (uint32_t)__builtin_ctzll(candidates);
        candidates &= candidates - 1;
        uint32_t load = count;
        uint32_t reads = 0;
        uint32_t peak = 0;
        for (uint32_t r = store + 1; r < count; r += 1)
        {
            const bool named = tokens[first + r] == first + store;
            load = named && reads == 0 ? r : load;
            reads += named;
        }
        for (uint32_t r = store + 1; r <= load && r < count; r += 1)
        {
            peak = depths[r] > peak ? depths[r] : peak;
        }
        const bool keep = store >= available && reads == 1 && load < count && depths[store] == 1 && depths[load] == 0 && peak <= 6 &&
                          x87_transfer_home(&records[first + store], &records[first + load]);
        if (keep)
        {
            proposed[store] = (X86Instruction){ .form = X86_FORM_FLD_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
            proposed[load] = (X86Instruction){ .form = X86_FORM_FSTP_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
            available = load + 1;
            pairs += 1;
        }
    }
    const bool verified = pairs != 0 && x87_transfer_verify(records + first, proposed, tokens, eligible, first, count);
    if (verified)
    {
        memcpy(records + first, proposed, (uint64_t)count * sizeof(X86Instruction));
    }
    return verified ? pairs : 0;
}

// `tokens[r]` is the private FSTP definition's record, or NONE. `eligible[definition]` certifies a canonical value
// whose one read consumes it. `boundaries` has count + 1 entries: empty-stack entry boundaries, plus mandatory cuts.
// Missing annotations, unsupported spans, a full stack and rejected proofs leave the original records unchanged.
static X87TransferStats x87_transfer_records(X86Instruction* records, uint32_t count, const uint32_t* tokens, const uint8_t* eligible, const uint8_t* boundaries, bool vector)
{
    X87TransferStats stats = { 0 };
    uint32_t region_first = 0;
    uint32_t region_end = 0;
    uint32_t first = 0;
    for (uint32_t end = 1; end <= count; end += 1)
    {
        if (boundaries[end] != 0 || end == count)
        {
            const bool safe = (boundaries[first] & BUSTER_X87_TRANSFER_BOUNDARY) != 0 && (boundaries[end] & BUSTER_X87_TRANSFER_BOUNDARY) != 0 &&
                              x87_transfer_span(records, first, end);
            const bool cut = !safe || (boundaries[first] & BUSTER_X87_TRANSFER_CUT) != 0 || end - region_first > BUSTER_X87_TRANSFER_RECORDS;
            if (cut && region_end > region_first)
            {
                const uint32_t pairs = x87_transfer_region(records, tokens, eligible, region_first, region_end, vector);
                stats.pairs += pairs;
                stats.regions += pairs != 0;
                region_first = region_end;
            }
            if (safe)
            {
                region_first = region_end == region_first ? first : region_first;
                region_end = end;
            }
            else
            {
                region_first = end;
                region_end = end;
            }
            first = end;
        }
    }
    if (region_end > region_first)
    {
        const uint32_t pairs = x87_transfer_region(records, tokens, eligible, region_first, region_end, vector);
        stats.pairs += pairs;
        stats.regions += pairs != 0;
    }
    return stats;
}

#endif
