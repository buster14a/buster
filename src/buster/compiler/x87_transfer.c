// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded transport between compiler-private x87 homes (#1387). Each adapter certifies a private store
// and its one consuming reload. FST ST(0) keeps the full-width store at the original wait, including
// its documented SNaN/unsupported-format effects. FXCH at the reload takes the saved operand in its
// original order. Neither instruction narrows the value. Intel SDM vol. 2 and AMD APM vol. 5 describe
// conversion for narrower destinations and no denormal exception for an extended FLD. The physical
// stack is replayed before publication. Calls and control/environment observations cut regions.

#ifndef BUSTER_X87_TRANSFER_INCLUDED
#define BUSTER_X87_TRANSFER_INCLUDED

// The exact binary32 or binary64 encoding of a finite normal (and signed zero), or the original
// extended significand. A narrower subnormal would introduce a denormal-operand exception on FLD.
static uint16_t x87_literal_bits(uint64_t low, uint16_t high, uint64_t* bits)
{
    const uint32_t exponent = high & 0x7fffu;
    const uint64_t sign = high >> 15;
    const bool normal = (low >> 63) != 0;
    uint16_t form = X86_FORM_FLD_M80;
    *bits = low;
    if (exponent == 0 && low == 0)
    {
        *bits = sign << 31;
        form = X86_FORM_FLD_M32;
    }
    else if (normal && exponent >= 16383 - 126 && exponent <= 16383 + 127 && (low & ((UINT64_C(1) << 40) - 1)) == 0)
    {
        const uint64_t fraction = (low >> 40) & 0x7fffffu;
        *bits = (sign << 31) | ((uint64_t)(exponent - (16383 - 127)) << 23) | fraction;
        form = X86_FORM_FLD_M32;
    }
    else if (normal && exponent >= 16383 - 1022 && exponent <= 16383 + 1023 && (low & 0x7ffu) == 0)
    {
        const uint64_t fraction = (low >> 11) & ((UINT64_C(1) << 52) - 1);
        *bits = (sign << 63) | ((uint64_t)(exponent - (16383 - 1023)) << 52) | fraction;
        form = X86_FORM_FLD_M64;
    }
    if (form == X86_FORM_FLD_M32 && (*bits == 0 || *bits == UINT32_C(0x3f800000)))
    {
        form = *bits == 0 ? X86_FORM_FLDZ : X86_FORM_FLD1;
    }
    return form;
}

#define BUSTER_X87_TRANSFER_NONE UINT32_MAX
#define BUSTER_X87_TRANSFER_STORE 1u
#define BUSTER_X87_TRANSFER_RELOAD 2u
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

// Plain integer loads and spills used to stage a signed int for FILD. Preserve the access
// itself and reject indexed frame addresses, which cannot be checked for private-home overlap.
static bool x87_transfer_integer_memory(const X86Instruction* record)
{
    const bool load = record->form == X86_FORM_MOV_R32_RM32;
    const bool store = record->form == X86_FORM_MOV_RM32_R32;
    const uint32_t protected = (1u << X86_REGISTER_RSP) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R15);
    const bool indexed = (record->mode & BUSTER_X86_MODE_INDEX) != 0 && record->index < 16 && record->rm < 16 &&
                         ((protected >> record->index) & 1) == 0 && ((protected >> record->rm) & 1) == 0;
    const bool address = record->mode == BUSTER_X86_MEMORY_BASE || (indexed && (record->mode & ~3u) == BUSTER_X86_MEMORY_BASE_INDEX(0));
    return (load || store) && address && record->decorations == 0 && (!load || (record->reg < 16 && ((protected >> record->reg) & 1) == 0));
}

// Plain integer operations and the recognized memory transfers may lie between the two waiting points.
// Calls, control, SIMD and environment operations end the region. Its possible frame bases must stay stable.
static X87TransferKind x87_transfer_kind(const X86Instruction* record)
{
    const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
    X87TransferKind kind = X87_TRANSFER_STOP;
    if (form == X86_FORM_NONE || x87_transfer_integer_memory(record))
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
    const bool four = form == X86_FORM_FLD_M32 || form == X86_FORM_FILD_M32 || form == X86_FORM_MOV_R32_RM32 || form == X86_FORM_MOV_RM32_R32;
    const uint32_t bytes = form == X86_FORM_FLD_M80 || form == X86_FORM_FSTP_M80 ? 10 : four ? 4 : form == X86_FORM_FLD_M64 || form == X86_FORM_FILD_M64 ? 8 : 0;
    const int64_t delta = (int64_t)record->displacement - home->displacement;
    const uint32_t bases = (1u << X86_REGISTER_RSP) | (1u << X86_REGISTER_RBP) | (1u << X86_REGISTER_R15);
    const bool frame = record->rm < 16 && ((bases >> record->rm) & 1) != 0;
    const bool indexed = (record->mode & BUSTER_X86_MODE_INDEX) != 0;
    const bool frame_index = indexed && record->index < 16 && ((bases >> record->index) & 1) != 0;
    // A compiler-private slot cannot escape. A second possible frame base or an indexed frame access
    // has no provable displacement from the slot, so conservatively treat it as overlapping.
    return bytes != 0 && (frame_index || (indexed && frame) || (record->mode == BUSTER_X86_MEMORY_BASE && frame &&
                         (record->rm != home->rm || (delta < 10 && delta + bytes > 0))));
}

// A span begins and ends at an adapter's known-empty boundary. Its internal peak is checked independently below.
static bool x87_transfer_span(const X86Instruction* records, uint32_t first, uint32_t end, uint8_t* kinds)
{
    uint32_t depth = 0;
    bool valid = end - first <= BUSTER_X87_TRANSFER_RECORDS;
    for (uint32_t r = first; valid && r < end; r += 1)
    {
        const X87TransferKind kind = x87_transfer_kind(&records[r]);
        kinds[r % BUSTER_X87_TRANSFER_RECORDS] = (uint8_t)kind;
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

// Both emitters certify the private store while assigning its unique reload. Read up to one region of
// certificates in a single masked load. A malformed certificate still has to pass the store and token
// check below, and every proposed transformation is independently replayed before publication.
static __m512i x87_transfer_annotations(const uint8_t* annotations, uint32_t first, uint32_t count)
{
    return _mm512_maskz_loadu_epi8(_bzhi_u64(UINT64_MAX, count), annotations + first);
}

// Independently replay both stacks. Values name original loads and arithmetic results. A resident
// full-width store has its own identity: its destination can reflect the architecture's SNaN handling.
static bool x87_transfer_verify(const X86Instruction* original, const X86Instruction* proposed, const uint32_t* tokens, const uint8_t* annotations, uint32_t first, uint32_t count, uint64_t edits)
{
    uint64_t before[8], after[8], homes[BUSTER_X87_TRANSFER_RECORDS] = { 0 };
    uint32_t old_depth = 0, new_depth = 0;
    uint64_t live = 0, transported = 0;
    bool valid = count <= BUSTER_X87_TRANSFER_RECORDS;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const uint32_t token = tokens[first + r];
        const uint32_t home = token - first;
        const X87TransferKind kind = x87_transfer_kind(&original[r]);
        const bool same = ((edits >> r) & 1) == 0 || memcmp(&original[r], &proposed[r], sizeof(X86Instruction)) == 0;
        const uint64_t identity = (uint64_t)first + r + 1;
        for (uint64_t held = live; valid && held != 0; held &= held - 1)
        {
            const uint32_t h = (uint32_t)__builtin_ctzll(held);
            valid = !x87_transfer_overlap(&original[h], &original[r]) || (!same && kind == X87_TRANSFER_LOAD && home == h);
        }
        if (valid && kind == X87_TRANSFER_LOAD)
        {
            const uint64_t value = home < count && homes[home] != 0 ? homes[home] : identity;
            valid = old_depth < 8;
            if (valid && same)
            {
                valid = new_depth < 8 && (home >= count || ((transported >> home) & 1) == 0);
                if (valid)
                {
                    after[new_depth++] = value;
                }
            }
            else if (valid)
            {
                const uint32_t index = proposed[r].rm;
                const X86Instruction exchange = { .form = X86_FORM_FXCH_ST, .rm = (uint8_t)index, .mode = BUSTER_X86_REGISTER_DIRECT };
                valid = home < count && ((live >> home) & 1) != 0 && x87_transfer_home(&original[home], &original[r]) &&
                        memcmp(&proposed[r], &exchange, sizeof(exchange)) == 0 && index < new_depth && after[new_depth - 1 - index] == value;
                if (valid)
                {
                    after[new_depth - 1 - index] = after[new_depth - 1];
                    after[new_depth - 1] = value;
                    live &= ~(UINT64_C(1) << home);
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
            if (valid && home == r)
            {
                homes[r] = identity;
            }
            if (valid && same)
            {
                new_depth -= 1;
            }
            else if (valid)
            {
                const X86Instruction store = { .form = X86_FORM_FST_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
                valid = home == r && (annotations[token] & BUSTER_X87_TRANSFER_STORE) != 0 && memcmp(&store, &proposed[r], sizeof(store)) == 0;
                if (valid)
                {
                    after[new_depth - 1] = identity;
                    live |= UINT64_C(1) << r;
                    transported |= UINT64_C(1) << r;
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
    return valid && old_depth == 0 && new_depth == 0 && live == 0;
}

// The small physical stack contains a store's local record number while its value is parked there,
// and NONE for an ordinary operand. A reload exchanges its saved value with st0 at its original wait.
// The independent replay checks the complete operand order, including other values that exchange moved.
static bool x87_transfer_plan(const X86Instruction* records, X86Instruction* proposed, const uint32_t* tokens, const uint8_t* annotations, uint32_t first, uint32_t count, uint64_t candidates, uint64_t* edits, const uint8_t* kinds)
{
    uint32_t stack[8];
    uint32_t depth = 0;
    bool valid = true;
    uint64_t changed = 0;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        const X87TransferKind kind = (X87TransferKind)kinds[(first + r) % BUSTER_X87_TRANSFER_RECORDS];
        const uint32_t home = tokens[first + r] - first;
        if (kind == X87_TRANSFER_STORE && ((candidates >> r) & 1) != 0)
        {
            valid = depth != 0 && stack[depth - 1] == BUSTER_X87_TRANSFER_NONE;
            if (valid)
            {
                changed |= UINT64_C(1) << r;
                proposed[r] = (X86Instruction){ .form = X86_FORM_FST_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
                stack[depth - 1] = r;
            }
        }
        else if (kind == X87_TRANSFER_LOAD && home < count && ((candidates >> home) & 1) != 0)
        {
            uint32_t index = 0;
            while (index < depth && stack[depth - 1 - index] != home)
            {
                index += 1;
            }
            valid = index < depth;
            if (valid)
            {
                changed |= UINT64_C(1) << r;
                proposed[r] = (X86Instruction){ .form = X86_FORM_FXCH_ST, .rm = (uint8_t)index, .mode = BUSTER_X86_REGISTER_DIRECT };
                stack[depth - 1 - index] = stack[depth - 1];
                stack[depth - 1] = BUSTER_X87_TRANSFER_NONE;
            }
        }
        else if (kind == X87_TRANSFER_LOAD)
        {
            valid = depth < 8;
            if (valid)
            {
                stack[depth++] = BUSTER_X87_TRANSFER_NONE;
            }
        }
        else if (kind == X87_TRANSFER_STORE || kind == X87_TRANSFER_BINARY)
        {
            const uint32_t operands = kind == X87_TRANSFER_BINARY ? 2 : 1;
            valid = depth >= operands && stack[depth - 1] == BUSTER_X87_TRANSFER_NONE && stack[depth - operands] == BUSTER_X87_TRANSFER_NONE;
            depth -= valid;
        }
    }
    // A row outside the edit mask still denotes the original instruction, both in replay and publication.
    *edits = changed;
    return valid && depth == 0 && x87_transfer_verify(records, proposed, tokens, annotations, first, count, changed);
}

static uint32_t x87_transfer_region(X86Instruction* records, const uint32_t* tokens, const uint8_t* annotations, uint32_t first, uint32_t end, const uint8_t* kinds)
{
    const uint32_t count = end - first;
    X86Instruction proposed[BUSTER_X87_TRANSFER_RECORDS];
    uint64_t edits = 0;
    const __m512i tags = x87_transfer_annotations(annotations, first, count);
    uint64_t candidates = _mm512_test_epi8_mask(tags, _mm512_set1_epi8(BUSTER_X87_TRANSFER_STORE));
    uint64_t loads = _mm512_test_epi8_mask(tags, _mm512_set1_epi8(BUSTER_X87_TRANSFER_RELOAD));
    uint64_t reads = 0, repeated = 0;
    while (loads != 0 && candidates != 0)
    {
        const uint32_t r = (uint32_t)__builtin_ctzll(loads);
        loads &= loads - 1;
        const uint32_t owner = tokens[first + r];
        const uint32_t home = owner - first;
        if (home < r && records[first + r].form == X86_FORM_FLD_M80 &&
            records[owner].form == X86_FORM_FSTP_M80 && tokens[owner] == owner)
        {
            const uint64_t bit = UINT64_C(1) << home;
            repeated |= reads & bit;
            reads |= bit;
        }
    }
    candidates &= reads & ~repeated;
    uint32_t pairs = 0;
    if (candidates != 0)
    {
        uint64_t selected = candidates;
        if (!x87_transfer_plan(records + first, proposed, tokens, annotations, first, count, selected, &edits, kinds))
        {
            // Under pressure or an unrepresentable permutation, retain only independently proven pairs.
            selected = 0;
            for (uint64_t remaining = candidates; remaining != 0; remaining &= remaining - 1)
            {
                const uint64_t trial = selected | (remaining & -remaining);
                if (x87_transfer_plan(records + first, proposed, tokens, annotations, first, count, trial, &edits, kinds))
                {
                    selected = trial;
                }
            }
            if (selected != 0 && !x87_transfer_plan(records + first, proposed, tokens, annotations, first, count, selected, &edits, kinds))
            {
                selected = 0;
            }
        }
        if (selected != 0)
        {
            while (edits != 0)
            {
                const uint32_t row = (uint32_t)__builtin_ctzll(edits);
                records[first + row] = proposed[row];
                edits &= edits - 1;
            }
            pairs = (uint32_t)__builtin_popcountll(selected);
        }
    }
    return pairs;
}

// A token names its private FSTP definition, or NONE. A store tag certifies a single consumer, and a reload
// tag accompanies each load token. The count + 1 boundary bytes mark empty-stack entries and mandatory cuts.
// Missing annotations, unsupported spans, a full stack and rejected proofs leave the original records unchanged.
static X87TransferStats x87_transfer_records(X86Instruction* records, uint32_t count, const uint32_t* tokens, const uint8_t* annotations, const uint8_t* boundaries)
{
    X87TransferStats stats = { 0 };
    // Cache the original operation kind while checking each span. A live region and the next
    // span fit in one circular window after the known cuts. The verifier decodes the original
    // records independently when it replays a proposal.
    uint8_t kinds[BUSTER_X87_TRANSFER_RECORDS];
    uint32_t region_first = 0;
    uint32_t region_end = 0;
    uint32_t first = 0;
    for (uint64_t start = 1; start <= count; start += 64)
    {
        const uint32_t left = (uint32_t)(count - start + 1);
        const __mmask64 lanes = left >= 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, left);
        const __m512i bytes = _mm512_maskz_loadu_epi8(lanes, boundaries + start);
        uint64_t ends = _mm512_test_epi8_mask(bytes, bytes);
        if (left <= 64)
        {
            ends |= UINT64_C(1) << (left - 1);
        }
        while (ends != 0)
        {
            const uint32_t end = (uint32_t)start + (uint32_t)__builtin_ctzll(ends);
            ends &= ends - 1;
            const bool cut = (boundaries[first] & BUSTER_X87_TRANSFER_CUT) != 0 || end - region_first > BUSTER_X87_TRANSFER_RECORDS;
            if (cut && region_end > region_first)
            {
                const uint32_t pairs = x87_transfer_region(records, tokens, annotations, region_first, region_end, kinds);
                stats.pairs += pairs;
                stats.regions += pairs != 0;
                region_first = region_end;
            }
            const bool safe = (boundaries[first] & BUSTER_X87_TRANSFER_BOUNDARY) != 0 && (boundaries[end] & BUSTER_X87_TRANSFER_BOUNDARY) != 0 &&
                              x87_transfer_span(records, first, end, kinds);
            if (!safe && region_end > region_first)
            {
                const uint32_t pairs = x87_transfer_region(records, tokens, annotations, region_first, region_end, kinds);
                stats.pairs += pairs;
                stats.regions += pairs != 0;
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
        const uint32_t pairs = x87_transfer_region(records, tokens, annotations, region_first, region_end, kinds);
        stats.pairs += pairs;
        stats.regions += pairs != 0;
    }
    return stats;
}

#endif
