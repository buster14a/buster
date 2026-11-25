// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fused one-pass C17 compiler directly from preprocessed SoA tokens to x86-64 machine code and ELF64 (#2850).
//
// Call-graph discipline (strictly acyclic DAG, ZERO direct or mutual recursion):
//   Level  9: AVX-512 body prescan, inline memcpy/memzero, .rodata consts, ELF sym helpers, FastVal primitives
//   Level 10: Iterative static constant evaluator & static/global initializer (calls <= Level 9)
//   Level 11: Iterative Pratt/shunting-yard expression compiler + SysV AMD64 call lowering (calls <= Level 9)
//   Level 12: Iterative local variable initializer (calls <= Level 11)
//   Level 13: Iterative statement & function body compiler + jump tables (calls <= Level 12)
//   Level 14: Translation unit driver & deferred static-inline fixed-point pass (calls <= Level 13)

#ifndef BUSTER_C_DIRECT_CC_INCLUDED
#define BUSTER_C_DIRECT_CC_INCLUDED

#include "buster/compiler/c_direct_types.c"

// ---------------------------------------------------------------------------------------------------------------------
// Level 9a: AVX-512 body pre-scanner, stack allocation, .rodata constants, inline copy/zero, ELF symbol binding

// A hosted program's main must return int without GNU's extensions (#3592): clang refuses `void main(void)` in
// -std=c99, c11, c17 and c23, at file scope or in a block, and only warns in GNU's modes (c_check_input.c's
// check_input_main). The direct path leaves such a declaration to the full pipeline, freestanding or not.
static void fast_cc_check_main(FastCCState* cc, uint32_t name_id, FastType type)
{
    if (cc->pp->strict_iso && name_id != 0 && name_id == cc->id_main && ftype_is_func(type) &&
        cc->ctypes[type & BUSTER_FTYPE_CIDX_MASK].inner != (FastType)FTYPE_I32)
    {
        fast_cc_bail(cc, "main returns other than int without GNU's extensions");
    }
}

// A declaration's declarators name what they declare, and the comma after one is followed by another: `int , x;`,
// `int x, ;`, `int (*)(void);` and the `, ,` that `__VA_ARGS__ ## __VA_ARGS__` makes of a comma are clang's errors, which
// the direct path leaves to the full pipeline (#3592). Returns whether the declarator has a name.
static bool fast_cc_declarator_named(FastCCState* cc, uint32_t name_id)
{
    if (name_id == 0)
    {
        fast_cc_bail(cc, "declarator without a name");
    }
    return name_id != 0;
}

// The comma between two declarators, and the declarator it promises.
static void fast_cc_declarator_comma(FastCCState* cc)
{
    cc->pos += 1u;
    if (fast_cc_peek(cc) == FTOK_SEMI)
    {
        fast_cc_bail(cc, "comma before the end of a declaration");
    }
}

typedef struct FastBodyScan FastBodyScan;
struct FastBodyScan
{
    uint32_t end_pos;       // token index of matching '}' (UINT32_MAX if unmatched)
    bool has_unsupported;   // saw _Atomic, _Complex, __int128, _Float128, _Float16, float80, _Thread_local, typeof
    bool has_switch;
    bool has_goto;
};

// Scan a `{ ... }` token slice at 64 tokens per AVX-512 iteration to locate the matching `}` and check for
// unsupported tokens in one pass!
static FastBodyScan fast_cc_prescan_body(const FastCCState* cc, uint32_t start_pos)
{
    FastBodyScan res = { .end_pos = UINT32_MAX };
    const uint8_t* const tk = cc->tk;
    const uint32_t n = cc->tcount;
    if (start_pos >= n || tk[start_pos] != FTOK_LBRACE)
    {
        return res;
    }

    const __m512i v_lb = _mm512_set1_epi8((char)FTOK_LBRACE);
    const __m512i v_rb = _mm512_set1_epi8((char)FTOK_RBRACE);
    const __m512i v_atm = _mm512_set1_epi8((char)FTOK_KW_ATOMIC);
    const __m512i v_cpx = _mm512_set1_epi8((char)FTOK_KW_COMPLEX);
    const __m512i v_i128 = _mm512_set1_epi8((char)FTOK_KW_INT128);
    const __m512i v_f128 = _mm512_set1_epi8((char)FTOK_KW_FLOAT128);
    const __m512i v_f16 = _mm512_set1_epi8((char)FTOK_KW_FLOAT16);
    const __m512i v_f80 = _mm512_set1_epi8((char)FTOK_FLOAT80);
    const __m512i v_tls = _mm512_set1_epi8((char)FTOK_KW_THREAD_LOCAL);
    const __m512i v_tof = _mm512_set1_epi8((char)FTOK_KW_TYPEOF);
    const __m512i v_sw = _mm512_set1_epi8((char)FTOK_KW_SWITCH);
    const __m512i v_gt = _mm512_set1_epi8((char)FTOK_KW_GOTO);

    int32_t depth = 1;
    uint32_t i = start_pos + 1u;
    uint64_t unsup_acc = 0;
    uint64_t sw_acc = 0;
    uint64_t gt_acc = 0;

    while (i < n && depth > 0)
    {
        const uint32_t rem = n - i;
        const uint32_t chunk = rem >= 64u ? 64u : rem;
        const __mmask64 valid = chunk == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << chunk) - 1u);
        const __m512i v = chunk == 64u ? _mm512_loadu_si512((const void*)(tk + i)) :
                                         _mm512_maskz_loadu_epi8(valid, tk + i);
        const uint64_t m_lb = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_lb);
        const uint64_t m_rb = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_rb);
        uint64_t m_unsup = (uint64_t)(
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_atm) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_cpx) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_f128) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_f16) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_f80) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_tls) |
            _mm512_mask_cmpeq_epi8_mask(valid, v, v_tof));
        (void)v_i128;
        uint64_t m_sw = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_sw);
        uint64_t m_gt = (uint64_t)_mm512_mask_cmpeq_epi8_mask(valid, v, v_gt);

        const int32_t closes = __builtin_popcountll(m_rb);
        if (closes < depth)
        {
            // Fast path: cannot hit depth 0 anywhere in these 64 tokens!
            depth += __builtin_popcountll(m_lb) - closes;
            unsup_acc |= m_unsup;
            sw_acc |= m_sw;
            gt_acc |= m_gt;
            i += chunk;
            continue;
        }

        // Walk only the brace bit positions in this 64-token chunk!
        uint64_t braces = m_lb | m_rb;
        uint32_t cut = chunk;
        while (braces != 0)
        {
            const uint32_t bit = (uint32_t)__builtin_ctzll(braces);
            braces &= braces - 1u;
            if ((m_lb & (UINT64_C(1) << bit)) != 0) { depth += 1; }
            else
            {
                depth -= 1;
                if (depth == 0)
                {
                    res.end_pos = i + bit;
                    cut = bit;
                    break;
                }
            }
        }
        const uint64_t keep = cut == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << cut) - 1u);
        unsup_acc |= (m_unsup & keep);
        sw_acc |= (m_sw & keep);
        gt_acc |= (m_gt & keep);
        i += chunk;
    }

    res.has_unsupported = unsup_acc != 0;
    res.has_switch = sw_acc != 0;
    res.has_goto = gt_acc != 0;
    return res;
}

#define BUSTER_FDIRECT_MAX_FRAME UINT64_C(0x10000000)

static inline int32_t fast_cc_alloc_local(FastCCState* cc, uint32_t size, uint32_t align)
{
    int32_t result = 0;
    if (size == 0) { size = 1u; }
    if (align < 1u) { align = 1u; }
    const uint64_t raw = (uint64_t)(uint32_t)cc->cur_frame_size + (uint64_t)size + (uint64_t)(align - 1u);
    const uint64_t aligned = raw & ~(uint64_t)(align - 1u);
    if (aligned > BUSTER_FDIRECT_MAX_FRAME)
    {
        fast_cc_bail(cc, "stack frame too large");
    }
    else
    {
        cc->cur_frame_size = (int32_t)aligned;
        if (cc->cur_frame_size > cc->cur_tmp_off) { cc->cur_tmp_off = cc->cur_frame_size; }
        if (cc->cur_tmp_off > cc->cur_max_frame) { cc->cur_max_frame = cc->cur_tmp_off; }
        result = -cc->cur_frame_size;
    }
    return result;
}

static inline int32_t fast_cc_alloc_tmp(FastCCState* cc, uint32_t size, uint32_t align)
{
    int32_t result = 0;
    if (size == 0) { size = 8u; }
    if (align < 8u) { align = 8u; }
    const uint64_t raw = (uint64_t)(uint32_t)cc->cur_tmp_off + (uint64_t)size + (uint64_t)(align - 1u);
    const uint64_t aligned = raw & ~(uint64_t)(align - 1u);
    if (aligned > BUSTER_FDIRECT_MAX_FRAME)
    {
        fast_cc_bail(cc, "stack frame too large");
    }
    else
    {
        cc->cur_tmp_off = (int32_t)aligned;
        if (cc->cur_tmp_off > cc->cur_max_frame) { cc->cur_max_frame = cc->cur_tmp_off; }
        result = -cc->cur_tmp_off;
    }
    return result;
}

static inline void fast_cc_reset_tmps(FastCCState* cc)
{
    cc->cur_tmp_off = cc->cur_frame_size;
}

static uint32_t fast_cc_emit_rodata_u32(FastCCState* cc, uint32_t bits)
{
    uint8_t* ptr = NULL;
    const uint32_t off = fast_obj_alloc_section(cc->obj, FSEC_RODATA, 4u, 4u, &ptr);
    if (ptr != NULL) { memcpy(ptr, &bits, 4); }
    return off;
}

static uint32_t fast_cc_emit_rodata_u64(FastCCState* cc, uint64_t bits)
{
    uint8_t* ptr = NULL;
    const uint32_t off = fast_obj_alloc_section(cc->obj, FSEC_RODATA, 8u, 8u, &ptr);
    if (ptr != NULL) { memcpy(ptr, &bits, 8); }
    return off;
}

static uint32_t fast_cc_emit_rodata_u128(FastCCState* cc, uint64_t lo, uint64_t hi)
{
    uint8_t* ptr = NULL;
    const uint32_t off = fast_obj_alloc_section(cc->obj, FSEC_RODATA, 16u, 16u, &ptr);
    if (ptr != NULL)
    {
        memcpy(ptr, &lo, 8);
        memcpy(ptr + 8, &hi, 8);
    }
    return off;
}

static inline uint32_t fast_cc_get_sign_f32(FastCCState* cc)
{
    if (cc->ro_sign_f32_off == UINT32_MAX) { cc->ro_sign_f32_off = fast_cc_emit_rodata_u128(cc, UINT64_C(0x80000000), 0); }
    return cc->ro_sign_f32_off;
}

static inline uint32_t fast_cc_get_sign_f64(FastCCState* cc)
{
    if (cc->ro_sign_f64_off == UINT32_MAX) { cc->ro_sign_f64_off = fast_cc_emit_rodata_u128(cc, UINT64_C(0x8000000000000000), 0); }
    return cc->ro_sign_f64_off;
}

static inline uint32_t fast_cc_get_f64_2p63(FastCCState* cc)
{
    if (cc->ro_f64_to_u64_off == UINT32_MAX) { cc->ro_f64_to_u64_off = fast_cc_emit_rodata_u64(cc, UINT64_C(0x43e0000000000000)); }
    return cc->ro_f64_to_u64_off;
}

static inline uint32_t fast_cc_get_f32_2p63(FastCCState* cc)
{
    if (cc->ro_f32_to_u64_off == UINT32_MAX) { cc->ro_f32_to_u64_off = fast_cc_emit_rodata_u32(cc, UINT32_C(0x5f000000)); }
    return cc->ro_f32_to_u64_off;
}

static inline uint32_t fast_cc_get_one_f32(FastCCState* cc)
{
    if (cc->ro_one_f32_off == UINT32_MAX) { cc->ro_one_f32_off = fast_cc_emit_rodata_u32(cc, UINT32_C(0x3f800000)); }
    return cc->ro_one_f32_off;
}

static inline uint32_t fast_cc_get_one_f64(FastCCState* cc)
{
    if (cc->ro_one_f64_off == UINT32_MAX) { cc->ro_one_f64_off = fast_cc_emit_rodata_u64(cc, UINT64_C(0x3ff0000000000000)); }
    return cc->ro_one_f64_off;
}

// Parse one or more adjacent FTOK_STR_LIT tokens into a contiguous byte slice (zero-copy for single string!).
static __attribute__((noinline)) const char* fast_cc_parse_string_lit(FastCCState* cc, uint32_t* out_len)
{
    const uint32_t first_idx = fast_cc_data(cc);
    const FastLitSlice first = cc->pp->lit_slices[first_idx];
    cc->pos += 1u;
    if (fast_cc_peek(cc) != FTOK_STR_LIT)
    {
        *out_len = first.length;
        return cc->pp->lit_bytes + first.offset;
    }
    // Concatenate multiple adjacent string literals in scratch arena
    uint32_t total = first.length;
    uint32_t scan = cc->pos;
    while (scan < cc->tcount && cc->tk[scan] == FTOK_STR_LIT)
    {
        total += cc->pp->lit_slices[cc->td[scan]].length;
        scan += 1u;
    }
    char* dst = (char*)fast_arena_alloc(cc->scratch, (uint64_t)total + 1u, 1);
    if (dst == NULL)
    {
        fast_cc_bail(cc, "scratch full for string concat");
        *out_len = 0;
        return "";
    }
    memcpy(dst, cc->pp->lit_bytes + first.offset, first.length);
    uint32_t cur = first.length;
    while (cc->pos < scan)
    {
        const FastLitSlice sl = cc->pp->lit_slices[fast_cc_data(cc)];
        memcpy(dst + cur, cc->pp->lit_bytes + sl.offset, sl.length);
        cur += sl.length;
        cc->pos += 1u;
    }
    dst[cur] = '\0';
    *out_len = cur;
    return dst;
}

// Each ready bit is a deferred body in declaration order. A symbol can have been referenced before
// its definition arrives; that definition sets its own bit when it joins the table.
static inline void fast_cc_ready_task(FastCCState* cc, uint32_t task)
{
    const uint32_t group = task >> 6;
    cc->ready_tasks[group] |= UINT64_C(1) << (task & 63u);
    cc->ready_groups |= UINT64_C(1) << group;
}

static inline void fast_cc_reference(FastCCState* cc, FastSym* sym)
{
    // A pending inline definition (C99's or GNU's) is decided at the unit's end, not by a reference (#3595).
    if (sym->referenced != 1u && (sym->inline_rules & BUSTER_FINLINE_PENDING) == 0)
    {
        // #3577: a use in an inline body compiled to be checked alone (fast_cc_check_body) is a use for the unit's end
        // (-Wundefined-internal), but readies no body to emit.
        if (cc->checking != 0)
        {
            sym->referenced = 2u;
        }
        else
        {
            sym->referenced = 1u;
            for (uint32_t link = sym->deferred_task; link != 0; link = cc->deferred[link - 1u].previous_task)
            {
                fast_cc_ready_task(cc, link - 1u);
            }
        }
    }
}

// #3577: a use without code where evaluated, in #3592's dead branch (or an expression compiled again for its log): the
// unit's end counts it (-Wundefined-internal), as fast_cc_reference does a body's checked alone, and it readies no body
// to emit, as no code refers to one. sizeof's operand is no use.
static inline void fast_cc_reference_dead(FastSym* sym)
{
    if (sym->referenced == 0u && (sym->inline_rules & BUSTER_FINLINE_PENDING) == 0)
    {
        sym->referenced = 2u;
    }
}

// Ensure a FastSym (GLOBAL or FUNC) has an ELF symbol in cc->obj (lazy creation avoids polluting .symtab with unused decls!)
static uint32_t fast_cc_ensure_obj_sym(FastCCState* cc, uint32_t sym_idx)
{
    FastSym* s = cc->syms + sym_idx;
    if (s->obj_sym != 0)
    {
        return s->obj_sym;
    }
    const char* name = NULL;
    uint32_t len = 0;
    if (s->asm_name != NULL && s->asm_name_len > 0)
    {
        name = s->asm_name;
        len = s->asm_name_len;
    }
    else
    {
        name = fast_cc_ident_str(cc, s->ident_id, &len);
    }
    const uint8_t bind = (s->storage == 1u || s->storage == 4u) ? STB_LOCAL : (s->is_weak ? STB_WEAK : STB_GLOBAL);
    const uint8_t stype = s->kind == FSYM_FUNC ? STT_FUNC : STT_OBJECT;
    const uint32_t obj_idx = fast_obj_add_sym(cc->obj, name, len, bind, stype);
    if (obj_idx != 0)
    {
        cc->obj->syms[obj_idx].visibility = s->visibility;
    }
    s->obj_sym = obj_idx;
    // #3577: a checked inline body's object symbols go with it (fast_cc_check_body).
    if (cc->checking != 0)
    {
        if (cc->checked_obj_sym_count < BUSTER_FCHECKED_OBJ_SYMS)
        {
            cc->checked_obj_syms[cc->checked_obj_sym_count++] = sym_idx;
        }
        else
        {
            fast_cc_bail(cc, "too many symbols in a checked inline body");
        }
    }
    return obj_idx;
}

// Inline byte-copy `[dst_reg + dst_off] <- [src_reg + src_off]` of `size` bytes.
// Clobbers tmp_reg (and rdi/rsi/rcx when size > 64 bytes -> callers spill before large copy!).
static void x86_emit_memcpy_inline(FastObj* obj, uint32_t dst_reg, int32_t dst_off,
                                   uint32_t src_reg, int32_t src_off, uint32_t size, uint32_t tmp_reg)
{
    if (size == 0) { return; }
    if (size <= 64u)
    {
        uint32_t off = 0;
        while (off + 8u <= size)
        {
            x86_load(obj, tmp_reg, src_reg, src_off + (int32_t)off, 8u, false);
            x86_store(obj, dst_reg, dst_off + (int32_t)off, tmp_reg, 8u);
            off += 8u;
        }
        if (off + 4u <= size)
        {
            x86_load(obj, tmp_reg, src_reg, src_off + (int32_t)off, 4u, false);
            x86_store(obj, dst_reg, dst_off + (int32_t)off, tmp_reg, 4u);
            off += 4u;
        }
        if (off + 2u <= size)
        {
            x86_load(obj, tmp_reg, src_reg, src_off + (int32_t)off, 2u, false);
            x86_store(obj, dst_reg, dst_off + (int32_t)off, tmp_reg, 2u);
            off += 2u;
        }
        if (off < size)
        {
            x86_load(obj, tmp_reg, src_reg, src_off + (int32_t)off, 1u, false);
            x86_store(obj, dst_reg, dst_off + (int32_t)off, tmp_reg, 1u);
        }
        return;
    }
    // Large copy via ERMS `rep movsb`: safely compute dst -> rdi, src -> rsi without clobbering!
    if (dst_reg == FREG_RSI && src_reg == FREG_RDI)
    {
        x86_mov_rr(obj, true, FREG_RAX, FREG_RSI);
        x86_lea(obj, FREG_RSI, FREG_RDI, src_off);
        x86_lea(obj, FREG_RDI, FREG_RAX, dst_off);
    }
    else if (dst_reg == FREG_RSI)
    {
        x86_lea(obj, FREG_RDI, dst_reg, dst_off);
        x86_lea(obj, FREG_RSI, src_reg, src_off);
    }
    else
    {
        x86_lea(obj, FREG_RSI, src_reg, src_off);
        x86_lea(obj, FREG_RDI, dst_reg, dst_off);
    }
    x86_mov_ri(obj, false, FREG_RCX, size);
    x86_rep_movsb(obj);
}

// Inline zero-fill `[base_reg + base_off]` of `size` bytes (clobbers rax, and rdi/rcx when size > 64).
static void x86_emit_memzero_inline(FastObj* obj, uint32_t base_reg, int32_t base_off, uint32_t size)
{
    if (size == 0) { return; }
    x86_mov_ri(obj, false, FREG_RAX, 0);
    if (size <= 64u)
    {
        uint32_t off = 0;
        while (off + 8u <= size) { x86_store(obj, base_reg, base_off + (int32_t)off, FREG_RAX, 8u); off += 8u; }
        if (off + 4u <= size)    { x86_store(obj, base_reg, base_off + (int32_t)off, FREG_RAX, 4u); off += 4u; }
        if (off + 2u <= size)    { x86_store(obj, base_reg, base_off + (int32_t)off, FREG_RAX, 2u); off += 2u; }
        if (off < size)          { x86_store(obj, base_reg, base_off + (int32_t)off, FREG_RAX, 1u); }
        return;
    }
    x86_lea(obj, FREG_RDI, base_reg, base_off);
    x86_mov_ri(obj, false, FREG_RCX, size);
    x86_rep_stosb(obj);
}

static bool ftype_is_object_const(const FastCCState* cc, FastType t)
{
    while (ftype_is_array(t))
    {
        t = cc->ctypes[t & BUSTER_FTYPE_CIDX_MASK].inner;
    }
    return ftype_is_const(t);
}

// Check iteratively whether a type contains any pointer (to place const relocated globals in .data.rel.ro under PIC)
static bool ftype_contains_ptr(const FastCCState* cc, FastType t)
{
    FastType stack[64];
    uint32_t sp = 0;
    stack[sp++] = t;
    while (sp > 0)
    {
        const FastType cur = stack[--sp];
        if (ftype_is_ptr(cur)) { return true; }
        if (ftype_is_array(cur))
        {
            if (sp < 64u) { stack[sp++] = cc->ctypes[cur & BUSTER_FTYPE_CIDX_MASK].inner; }
        }
        else if (ftype_is_record(cur))
        {
            const FastComplexType* ct = cc->ctypes + (cur & BUSTER_FTYPE_CIDX_MASK);
            for (uint32_t i = 0; i < ct->count; i += 1)
            {
                const FastType mt = cc->member_info[ct->start + i].type;
                if (ftype_is_ptr(mt)) { return true; }
                if ((ftype_is_array(mt) || ftype_is_record(mt)) && sp < 64u)
                {
                    stack[sp++] = mt;
                }
            }
        }
    }
    return false;
}

// Check if type is char / signed char / unsigned char (valid target for string literal array initializer!)
static inline bool ftype_is_char_type(FastType t)
{
    if (ftype_is_complex(t) || ftype_ptr_depth(t) > 0) { return false; }
    const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
    return sk == FTYPE_I8 || sk == FTYPE_U8;
}

// C17 _Generic type matching (handles const T* vs T*, void* vs const void*, char/int/long/etc., structs)
static bool ftype_generic_match(const FastCCState* cc, FastType ctrl, FastType assoc)
{
    // Strip top-level qualifiers (controlling expression lvalue-converted)
    ctrl = ftype_unqual(ctrl);
    assoc = ftype_unqual(assoc);
    if (ctrl == assoc) { return true; }
    // Compare pointer depths and pointee constness
    const uint32_t cd = ftype_ptr_depth(ctrl);
    const uint32_t ad = ftype_ptr_depth(assoc);
    if (cd != ad) { return false; }
    if (cd > 0)
    {
        const bool c_pc = (ctrl & BUSTER_FTYPE_PCONST) != 0;
        const bool a_pc = (assoc & BUSTER_FTYPE_PCONST) != 0;
        if (c_pc != a_pc) { return false; }
    }
    if (ftype_is_complex(ctrl) != ftype_is_complex(assoc)) { return false; }
    if (ftype_is_complex(ctrl))
    {
        return (ctrl & BUSTER_FTYPE_CIDX_MASK) == (assoc & BUSTER_FTYPE_CIDX_MASK);
    }
    // Scalar match: compare base kind + plain-char distinction + long distinction
    const uint32_t mask = BUSTER_FTYPE_SCALAR_MASK | BUSTER_FTYPE_SCALAR_CHAR | BUSTER_FTYPE_SCALAR_LONG;
    if ((ctrl & mask) == (assoc & mask)) { return true; }
    // Allow plain char to match signed/unsigned char only when neither differs in signedness? No, in C char != signed char != unsigned char.
    (void)cc;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 10: Iterative static constant evaluator & static/global initializer (ZERO recursion!)

typedef struct FastStaticConst FastStaticConst;
struct FastStaticConst
{
    int64_t val;          // integer value, IEEE bits (if is_float), or byte addend (if sym_idx != 0)
    uint32_t sym_idx;     // 0 = pure scalar; >0 = ELF symbol index
    uint8_t is_float;     // 0 = int/ptr, 1 = f32, 2 = f64
    uint8_t is_unsigned;
    uint8_t is_string;    // 1 = raw string literal
    uint8_t folded;       // 1 = not an integer literal (a fold, a cast, a character, an enumerator): no null pointer (#3577)
    const char* str_ptr;
    uint32_t str_len;
    FastType type;
    uint32_t enum_cidx;   // an enumerator's enum, for -Wimplicit-enum-enum-cast (#3577)
    uint8_t op;           // the binary operator or `!` that made it, outside parentheses (#3577: the precedence checks)
};

static double fsc_as_double(FastStaticConst v)
{
    if (v.is_float == 2u) { double d = 0.0; memcpy(&d, &v.val, 8); return d; }
    if (v.is_float == 1u) { float f = 0.0f; uint32_t b = (uint32_t)v.val; memcpy(&f, &b, 4); return (double)f; }
    return v.is_unsigned ? (double)(uint64_t)v.val : (double)v.val;
}

static FastStaticConst fsc_from_double(double d, bool is_f32)
{
    FastStaticConst r = { 0 };
    if (is_f32)
    {
        const float f = (float)d;
        uint32_t b = 0;
        memcpy(&b, &f, 4);
        r.val = (int64_t)(uint64_t)b;
        r.is_float = 1u;
        r.type = FTYPE_F32;
    }
    else
    {
        uint64_t b = 0;
        memcpy(&b, &d, 8);
        r.val = (int64_t)b;
        r.is_float = 2u;
        r.type = FTYPE_F64;
    }
    return r;
}

static void fsc_cast_to(FastCCState* cc, FastStaticConst* v, FastType target)
{
    if (v->sym_idx != 0)
    {
        // #3577: an address converts to an integer or a pointer, and to _Bool as true but a weak symbol's (which may be
        // null); to a floating type it is no constant, the full pipeline's error.
        if (ftype_is_float(target) || (ftype_is_bool(target) && cc->obj->syms[v->sym_idx].binding == STB_WEAK))
        {
            fast_cc_bail(cc, "address converted in a static initializer");
            return;
        }
        if (ftype_is_bool(target))
        {
            v->sym_idx = 0;
            v->val = 1;
            v->is_float = 0;
            v->type = FTYPE_BOOL;
            return;
        }
        v->type = target;
        return;
    }
    if (ftype_is_float(target))
    {
        *v = fsc_from_double(fsc_as_double(*v), !ftype_is_f64(target));
        v->folded = 1u;
        return;
    }
    uint64_t x = 0;
    if (v->is_float != 0)
    {
        // #3577: a value the integer type can't hold (NaN, or out of its range) is undefined, which the full pipeline
        // reports or refuses; the conversion below is defined for the others alone.
        const double d = fsc_as_double(*v);
        const bool sgn_target = ftype_is_signed(cc, target);
        const bool in_range = d == d && (sgn_target ? (d >= -9223372036854775808.0 && d < 9223372036854775808.0) :
                                                      (d > -1.0 && d < 18446744073709551616.0));
        if (ftype_is_bool(target)) { x = d != 0.0 ? 1u : 0u; }
        else if (!in_range) { fast_cc_bail(cc, "floating constant out of the integer's range"); }
        else if (sgn_target) { x = (uint64_t)(int64_t)d; }
        else { x = (uint64_t)d; }
    }
    else
    {
        x = (uint64_t)v->val;
    }
    const uint32_t sz = ftype_size(cc, target);
    const bool sgn = ftype_is_signed(cc, target);
    if (ftype_is_bool(target)) { x = x != 0 ? 1u : 0u; }
    else if (sz == 1u) { x = sgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
    else if (sz == 2u) { x = sgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
    else if (sz == 4u) { x = sgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
    v->val = (int64_t)x;
    v->is_float = 0;
    v->is_unsigned = !sgn ? 1u : 0u;
    v->type = target;
    v->folded = 1u;
}

// The value an operator just made (#3577): a fold (no null pointer constant), and its operator for the precedence
// checks: a binary one's token, `!`'s FTOK_BANG, another unary one's none.
static inline void fsc_made_by(FastStaticConst* vals, uint32_t vsp, uint32_t op)
{
    FastStaticConst* v = vals + (vsp - (uint32_t)(vsp != 0));
    v->folded = 1u;
    v->op = (uint8_t)((op & (UINT32_C(0) - (uint32_t)(op < 64u))) | ((uint32_t)FTOK_BANG * (uint32_t)(op == BUSTER_FPP_OP_UNARY_BANG)));
}

static void fsc_apply_op(FastCCState* cc, uint32_t op, FastStaticConst* vals, uint32_t* vsp_ptr)
{
    uint32_t vsp = *vsp_ptr;
    // Cast op: high bit 0x80000000 carries target FastType in low 31 bits? Better: store cast types in parallel array.
    if (op == BUSTER_FPP_OP_UNARY_PLUS) { return; }
    if (op == BUSTER_FPP_OP_UNARY_MINUS)
    {
        if (vsp < 1u || vals[vsp - 1u].sym_idx != 0) { fast_cc_bail(cc, "unary minus static const"); return; }
        if (vals[vsp - 1u].is_float != 0)
        {
            vals[vsp - 1u] = fsc_from_double(-fsc_as_double(vals[vsp - 1u]), vals[vsp - 1u].is_float == 1u);
        }
        else { vals[vsp - 1u].val = -(int64_t)vals[vsp - 1u].val; }
        return;
    }
    if (op == BUSTER_FPP_OP_UNARY_TILDE)
    {
        if (vsp < 1u || vals[vsp - 1u].sym_idx != 0 || vals[vsp - 1u].is_float != 0) { fast_cc_bail(cc, "~ static const"); return; }
        vals[vsp - 1u].val = ~vals[vsp - 1u].val;
        return;
    }
    if (op == BUSTER_FPP_OP_UNARY_BANG)
    {
        if (vsp < 1u) { fast_cc_bail(cc, "! static const"); return; }
        const FastStaticConst a = vals[vsp - 1u];
        const int64_t z = a.sym_idx != 0 ? 0 : (a.is_float != 0 ? (fsc_as_double(a) == 0.0 ? 1 : 0) : (a.val == 0 ? 1 : 0));
        vals[vsp - 1u] = (FastStaticConst){ .val = z, .type = FTYPE_I32 };
        return;
    }
    if (op == BUSTER_FPP_OP_TERNARY)
    {
        if (vsp < 3u) { fast_cc_bail(cc, "ternary static const"); return; }
        const FastStaticConst c = vals[vsp - 3u];
        const bool truth = c.sym_idx != 0 ? true : (c.is_float != 0 ? fsc_as_double(c) != 0.0 : c.val != 0);
        vals[vsp - 3u] = truth ? vals[vsp - 2u] : vals[vsp - 1u];
        *vsp_ptr = vsp - 2u;
        return;
    }
    if (vsp < 2u) { fast_cc_bail(cc, "binary static const stack"); return; }
    const FastStaticConst a = vals[vsp - 2u];
    const FastStaticConst b = vals[vsp - 1u];
    *vsp_ptr = vsp - 1u;
    // #3577: the runtime expressions' checks, in an initializer's constant too: an operand whose own operator the
    // binary one's precedence questions (-Wparentheses' family), and a right operand of && or || that is a constant
    // other than 0 or 1 (-Wconstant-logical-operand).
    {
        const uint32_t lclass = fast_expr_operand_class[a.op & 127u];
        const uint32_t rclass = fast_expr_operand_class[b.op & 127u];
        const uint32_t conflict = (((lclass | rclass) & fast_expr_conflicts_any[op & 127u]) | (lclass & fast_expr_conflicts_left[op & 127u])) != 0;
        const uint32_t logical_constant = ((uint32_t)(op == FTOK_LAND) | (uint32_t)(op == FTOK_LOR)) & (uint32_t)(b.sym_idx == 0) &
                                          (uint32_t)(b.is_float == 0) & (uint32_t)((uint64_t)b.val > 1u);
        if ((conflict | logical_constant) != 0)
        {
            fast_cc_bail(cc, "constant expression the full pipeline questions");
            return;
        }
    }

    // Address constant + / - integer offset (e.g. `&arr[0] + 4` or `(char*)&g + 8`)
    if (a.sym_idx != 0 || b.sym_idx != 0)
    {
        if ((op == FTOK_PLUS || op == FTOK_MINUS) && a.sym_idx != 0 && b.sym_idx == 0 && b.is_float == 0)
        {
            const uint32_t scale = ftype_is_ptr(a.type) ? ftype_size(cc, ftype_deref(cc, a.type)) : 1u;
            const int64_t delta = b.val * (int64_t)(scale != 0 ? scale : 1u);
            FastStaticConst r = a;
            r.val = op == FTOK_PLUS ? (a.val + delta) : (a.val - delta);
            vals[vsp - 2u] = r;
            return;
        }
        if (op == FTOK_PLUS && b.sym_idx != 0 && a.sym_idx == 0 && a.is_float == 0)
        {
            const uint32_t scale = ftype_is_ptr(b.type) ? ftype_size(cc, ftype_deref(cc, b.type)) : 1u;
            FastStaticConst r = b;
            r.val = b.val + a.val * (int64_t)(scale != 0 ? scale : 1u);
            vals[vsp - 2u] = r;
            return;
        }
        fast_cc_bail(cc, "unsupported reloc binary op in static init");
        return;
    }

    // Float arithmetic in static initializer
    if (a.is_float != 0 || b.is_float != 0)
    {
        const double da = fsc_as_double(a);
        const double db = fsc_as_double(b);
        const bool both_f32 = a.is_float == 1u && b.is_float == 1u;
        switch (op)
        {
            case FTOK_PLUS:    vals[vsp - 2u] = fsc_from_double(da + db, both_f32); return;
            case FTOK_MINUS:   vals[vsp - 2u] = fsc_from_double(da - db, both_f32); return;
            case FTOK_STAR:    vals[vsp - 2u] = fsc_from_double(da * db, both_f32); return;
            case FTOK_SLASH:   vals[vsp - 2u] = fsc_from_double(da / db, both_f32); return;
            case FTOK_EQ:      vals[vsp - 2u] = (FastStaticConst){ .val = da == db ? 1 : 0, .type = FTYPE_I32 }; return;
            case FTOK_NE:      vals[vsp - 2u] = (FastStaticConst){ .val = da != db ? 1 : 0, .type = FTYPE_I32 }; return;
            case FTOK_LT:      vals[vsp - 2u] = (FastStaticConst){ .val = da < db ? 1 : 0, .type = FTYPE_I32 }; return;
            case FTOK_LE:      vals[vsp - 2u] = (FastStaticConst){ .val = da <= db ? 1 : 0, .type = FTYPE_I32 }; return;
            case FTOK_GT:      vals[vsp - 2u] = (FastStaticConst){ .val = da > db ? 1 : 0, .type = FTYPE_I32 }; return;
            case FTOK_GE:      vals[vsp - 2u] = (FastStaticConst){ .val = da >= db ? 1 : 0, .type = FTYPE_I32 }; return;
            default:           fast_cc_bail(cc, "bad float op in static const"); return;
        }
    }

    // Integer binary op
    FastPPVal pv[2] = { { (uint64_t)a.val, a.is_unsigned != 0 }, { (uint64_t)b.val, b.is_unsigned != 0 } };
    uint32_t pvsp = 2u;
    fast_pp_apply_op(cc->pp, (uint16_t)op, pv, &pvsp);
    if (cc->pp->bailout)
    {
        fast_cc_bail(cc, cc->pp->bailout_reason);
        return;
    }
    vals[vsp - 2u] = (FastStaticConst){
        .val = (int64_t)pv[0].val,
        .is_unsigned = pv[0].is_unsigned ? 1u : 0u,
        .type = pv[0].is_unsigned ? FTYPE_U64 : FTYPE_I64,
    };
}

// Parse a static constant initializer expression (scalars, floats, strings, `&global.field[i]`, function ptrs, casts, arithmetic).
static __attribute__((noinline)) FastStaticConst fast_cc_eval_static_const(FastCCState* cc)
{
    FastStaticConst vals[64];
    uint16_t ops[64];
    FastType cast_types[64];
    uint32_t vsp = 0;
    uint32_t osp = 0;
    uint32_t pdepth = 0;
    bool expect = true;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (expect)
        {
            if (k == FTOK_LPAREN)
            {
                if (fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    cc->pos += 1u;
                    bool tok_ok = false;
                    const FastType ct = fast_cc_parse_typename(cc, &tok_ok);
                    if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN)
                    {
                        fast_cc_bail(cc, "complex cast in static init");
                        break;
                    }
                    cc->pos += 1u;
                    if (fast_cc_peek(cc) == FTOK_LBRACE)
                    {
                        fast_cc_bail(cc, "compound literal in static init");
                        break;
                    }
                    if (osp < 64u)
                    {
                        cast_types[osp] = ct;
                        ops[osp++] = 0x2000u; // cast marker
                    }
                    continue;
                }
                if (osp < 64u) { ops[osp++] = BUSTER_FPP_OP_LPAREN; }
                pdepth += 1u;
                cc->pos += 1u;
            }
            else if (k == FTOK_PLUS)  { if (osp < 64u) { ops[osp++] = BUSTER_FPP_OP_UNARY_PLUS; } cc->pos += 1u; }
            else if (k == FTOK_MINUS) { if (osp < 64u) { ops[osp++] = BUSTER_FPP_OP_UNARY_MINUS; } cc->pos += 1u; }
            else if (k == FTOK_TILDE) { if (osp < 64u) { ops[osp++] = BUSTER_FPP_OP_UNARY_TILDE; } cc->pos += 1u; }
            else if (k == FTOK_BANG)  { if (osp < 64u) { ops[osp++] = BUSTER_FPP_OP_UNARY_BANG; } cc->pos += 1u; }
            else if (k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
            else if (k == FTOK_INT_LIT)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)(int32_t)fast_cc_data(cc), .type = FTYPE_I32 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_INT_U32)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)(uint64_t)fast_cc_data(cc), .is_unsigned = 1u, .type = FTYPE_U32 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_INT_I64)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)cc->pp->num_pool[fast_cc_data(cc)], .type = FTYPE_I64 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_INT_U64)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)cc->pp->num_pool[fast_cc_data(cc)], .is_unsigned = 1u, .type = FTYPE_U64 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_CHAR_LIT)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)(int32_t)fast_cc_data(cc), .folded = 1u, .type = FTYPE_I32 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_FLOAT_LIT)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)cc->pp->num_pool[fast_cc_data(cc)], .is_float = 2u, .type = FTYPE_F64 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_FLOAT32)
            {
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)(uint64_t)fast_cc_data(cc), .is_float = 1u, .type = FTYPE_F32 }; }
                cc->pos += 1u; expect = false;
            }
            else if (k == FTOK_STR_LIT)
            {
                uint32_t slen = 0;
                const char* sp = fast_cc_parse_string_lit(cc, &slen);
                // If followed by `[idx]` or `+`, we still emit to rodata_str on demand; keep raw string for char[] init!
                if (vsp < 64u)
                {
                    vals[vsp++] = (FastStaticConst){
                        .is_string = 1u,
                        .str_ptr = sp,
                        .str_len = slen,
                        .type = ftype_make_ptr(cc, ftype_with_const(FTYPE_I8)),
                    };
                }
                expect = false;
            }
            else if (k == FTOK_AMP || k == FTOK_IDENT)
            {
                const bool took_addr = k == FTOK_AMP;
                if (took_addr) { cc->pos += 1u; }
                // Handle `&((T*)0)->field` or `&(sym.field[i])`
                uint32_t inner_parens = 0;
                while (fast_cc_peek(cc) == FTOK_LPAREN && !fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    inner_parens += 1u;
                    cc->pos += 1u;
                }
                if (fast_cc_peek(cc) != FTOK_IDENT)
                {
                    fast_cc_bail(cc, "complex &expr in static init");
                    break;
                }
                const uint32_t id = fast_cc_data(cc);
                // Check float builtins `__builtin_inff()` / `__builtin_inf()` / `__builtin_nanf("")` / `__builtin_nan("")`, and
                // HUGE_VAL's `__builtin_huge_val()` and `__builtin_huge_valf()`, which are infinities too
                if (!took_addr && (id == cc->id_builtin_inff || id == cc->id_builtin_inf || id == cc->id_builtin_huge_valf ||
                                   id == cc->id_builtin_huge_val || id == cc->id_builtin_nanf || id == cc->id_builtin_nan) &&
                    fast_cc_peek_at(cc, 1u) == FTOK_LPAREN)
                {
                    cc->pos += 2u;
                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN) { cc->pos += 1u; }
                    if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                    FastStaticConst fc = { 0 };
                    if (id == cc->id_builtin_inff || id == cc->id_builtin_huge_valf) { fc.val = (int64_t)UINT32_C(0x7f800000); fc.is_float = 1u; fc.type = FTYPE_F32; }
                    else if (id == cc->id_builtin_inf || id == cc->id_builtin_huge_val) { fc.val = (int64_t)UINT64_C(0x7ff0000000000000); fc.is_float = 2u; fc.type = FTYPE_F64; }
                    else if (id == cc->id_builtin_nanf) { fc.val = (int64_t)UINT32_C(0x7fc00000); fc.is_float = 1u; fc.type = FTYPE_F32; }
                    else                                { fc.val = (int64_t)UINT64_C(0x7ff8000000000000); fc.is_float = 2u; fc.type = FTYPE_F64; }
                    if (vsp < 64u) { vals[vsp++] = fc; }
                    expect = false;
                    continue;
                }
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                if (si == 0)
                {
                    fast_cc_bail(cc, "unknown ident in static init");
                    break;
                }
                cc->pos += 1u;
                FastSym* sym = cc->syms + si;
                if (sym->kind == FSYM_ENUM_CONST && !took_addr)
                {
                    while (inner_parens > 0 && fast_cc_peek(cc) == FTOK_RPAREN) { inner_parens -= 1u; cc->pos += 1u; }
                    if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = sym->const_val, .folded = 1u, .type = sym->type, .enum_cidx = sym->sec_off }; }
                    expect = false;
                    continue;
                }
                if (sym->kind == FSYM_FUNC)
                {
                    if ((sym->use_flags & (BUSTER_FSYM_UNSUPPORTED | BUSTER_FSYM_DEPRECATED)) != 0) { fast_cc_bail(cc, "unsupported or deprecated func ref in static init"); break; }
                    fast_cc_reference(cc, sym);
                    const uint32_t obj_sym = fast_cc_ensure_obj_sym(cc, si);
                    while (inner_parens > 0 && fast_cc_peek(cc) == FTOK_RPAREN) { inner_parens -= 1u; cc->pos += 1u; }
                    if (vsp < 64u)
                    {
                        vals[vsp++] = (FastStaticConst){
                            .val = 0,
                            .sym_idx = obj_sym,
                            .type = ftype_make_ptr(cc, sym->type),
                        };
                    }
                    expect = false;
                    continue;
                }
                if (sym->kind == FSYM_GLOBAL)
                {
                    fast_cc_reference(cc, sym);
                    FastType cur_t = sym->type;
                    int64_t byte_off = 0;
                    // Optional postfix `.field` and `[idx]` on `&g.field[i]` or `g.arr`
                    while (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
                    {
                        if (fast_cc_peek(cc) == FTOK_DOT)
                        {
                            cc->pos += 1u;
                            if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT)
                            {
                                fast_cc_bail(cc, "bad static .field");
                                break;
                            }
                            const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
                            if (mi == NULL || mi->bit_width != 0) { fast_cc_bail(cc, "static field lookup"); break; }
                            byte_off += (int64_t)mi->byte_offset;
                            cur_t = mi->type;
                            cc->pos += 1u;
                        }
                        else
                        {
                            cc->pos += 1u;
                            const int64_t idx = fast_cc_const_int_expr(cc, false);
                            if (fast_cc_peek(cc) != FTOK_RBRACKET) { fast_cc_bail(cc, "static [idx]"); break; }
                            cc->pos += 1u;
                            if (!ftype_is_array(cur_t)) { fast_cc_bail(cc, "static subscript non-array"); break; }
                            cur_t = cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner;
                            byte_off += idx * (int64_t)ftype_size(cc, cur_t);
                        }
                    }
                    while (inner_parens > 0 && fast_cc_peek(cc) == FTOK_RPAREN) { inner_parens -= 1u; cc->pos += 1u; }
                    if (!took_addr && !ftype_is_array(cur_t))
                    {
                        fast_cc_bail(cc, "non-const global value in static init");
                        break;
                    }
                    const uint32_t obj_sym = fast_cc_ensure_obj_sym(cc, si);
                    const FastType ptr_t = ftype_is_array(cur_t) ?
                                           ftype_make_ptr(cc, cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner) :
                                           ftype_make_ptr(cc, cur_t);
                    if (vsp < 64u)
                    {
                        vals[vsp++] = (FastStaticConst){ .val = byte_off, .sym_idx = obj_sym, .type = ptr_t };
                    }
                    expect = false;
                    continue;
                }
                fast_cc_bail(cc, "unsupported symbol in static init");
                break;
            }
            else if (k == FTOK_KW_SIZEOF || k == FTOK_KW_ALIGNOF || k == FTOK_KW_BUILTIN_OFFSETOF)
            {
                // Delegate directly to leaf constant evaluator (returns integer value!)
                const int64_t v = fast_cc_const_int_expr(cc, true);
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = v, .is_unsigned = 1u, .folded = 1u, .type = FTYPE_U64 }; }
                expect = false;
            }
            else
            {
                break;
            }
        }
        else
        {
            if (k == FTOK_RPAREN)
            {
                if (pdepth == 0) { break; }
                while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
                {
                    const uint16_t op = ops[--osp];
                    if (op == 0x2000u)
                    {
                        if (vsp > 0)
                        {
                            if (vals[vsp - 1u].is_string)
                            {
                                const uint32_t so = fast_obj_add_string_lit(cc->obj, vals[vsp - 1u].str_ptr, vals[vsp - 1u].str_len);
                                vals[vsp - 1u].is_string = 0;
                                vals[vsp - 1u].sym_idx = cc->obj->sec_sym_rodata_str;
                                vals[vsp - 1u].val = (int64_t)so;
                            }
                            fsc_cast_to(cc, &vals[vsp - 1u], cast_types[osp]);
                        }
                    }
                    else { fsc_apply_op(cc, op, vals, &vsp); fsc_made_by(vals, vsp, op); }
                }
                if (osp > 0) { osp -= 1u; }
                pdepth -= 1u;
                cc->pos += 1u;
                // A parenthesized value's operator is no operand's to question (#3577).
                if (vsp > 0) { vals[vsp - 1u].op = 0; }
                continue;
            }
            if (k == FTOK_COLON)
            {
                bool has_q = false;
                for (uint32_t i = osp; i > 0; i -= 1)
                {
                    if (ops[i - 1u] == BUSTER_FPP_OP_LPAREN) { break; }
                    if (ops[i - 1u] == FTOK_QUESTION) { has_q = true; break; }
                }
                if (!has_q) { break; }
                while (osp > 0 && ops[osp - 1u] != FTOK_QUESTION)
                {
                    const uint16_t op = ops[--osp];
                    if (op == 0x2000u) { if (vsp > 0) { fsc_cast_to(cc, &vals[vsp - 1u], cast_types[osp]); } }
                    else { fsc_apply_op(cc, op, vals, &vsp); fsc_made_by(vals, vsp, op); }
                }
                if (osp > 0) { ops[osp - 1u] = BUSTER_FPP_OP_TERNARY; }
                cc->pos += 1u;
                expect = true;
                continue;
            }
            if (k == FTOK_COMMA && pdepth == 0) { break; }
            const uint8_t prec = fast_pp_prec(k);
            if (prec == 0) { break; }
            // Materialize raw string literal into .rodata.str1.1 if used in binary op (e.g. `"abc" + 1`)
            if (vsp > 0 && vals[vsp - 1u].is_string)
            {
                const uint32_t so = fast_obj_add_string_lit(cc->obj, vals[vsp - 1u].str_ptr, vals[vsp - 1u].str_len);
                vals[vsp - 1u].is_string = 0;
                vals[vsp - 1u].sym_idx = cc->obj->sec_sym_rodata_str;
                vals[vsp - 1u].val = (int64_t)so;
            }
            while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
            {
                const uint16_t top = ops[osp - 1u];
                const uint8_t tprec = (top == 0x2000u || (top >= BUSTER_FPP_OP_UNARY_PLUS && top <= BUSTER_FPP_OP_UNARY_BANG)) ? 12u :
                                      top == BUSTER_FPP_OP_TERNARY ? 1u : fast_pp_prec((uint8_t)top);
                if (tprec > prec || (tprec == prec && k != FTOK_QUESTION))
                {
                    osp -= 1u;
                    if (top == 0x2000u) { if (vsp > 0) { fsc_cast_to(cc, &vals[vsp - 1u], cast_types[osp]); } }
                    else { fsc_apply_op(cc, top, vals, &vsp); fsc_made_by(vals, vsp, top); }
                }
                else { break; }
            }
            if (osp < 64u) { ops[osp++] = k; }
            cc->pos += 1u;
            expect = true;
        }
    }

    while (osp > 0 && !cc->bailout)
    {
        const uint16_t top = ops[--osp];
        if (top == BUSTER_FPP_OP_LPAREN || top == FTOK_QUESTION) { fast_cc_bail(cc, "unclosed static expr"); break; }
        if (top == 0x2000u)
        {
            if (vsp > 0)
            {
                if (vals[vsp - 1u].is_string)
                {
                    const uint32_t so = fast_obj_add_string_lit(cc->obj, vals[vsp - 1u].str_ptr, vals[vsp - 1u].str_len);
                    vals[vsp - 1u].is_string = 0;
                    vals[vsp - 1u].sym_idx = cc->obj->sec_sym_rodata_str;
                    vals[vsp - 1u].val = (int64_t)so;
                }
                fsc_cast_to(cc, &vals[vsp - 1u], cast_types[osp]);
            }
        }
        else { fsc_apply_op(cc, top, vals, &vsp); fsc_made_by(vals, vsp, top); }
    }
    if (vsp != 1u && !cc->bailout) { fast_cc_bail(cc, "bad static const expr"); }
    return (!cc->bailout && vsp == 1u) ? vals[0] : (FastStaticConst){ 0 };
}

static void fast_cc_check_static_initializer(FastCCState* cc, FastType target, const FastMemberInfo* bf, const FastStaticConst* v);

// Write one scalar static constant into `sec_buf + byte_off` (adding `R_X86_64_64` relocation if sym_idx != 0).
static void fast_cc_write_static_scalar(FastCCState* cc, FastSecId sec, uint32_t byte_off, FastType target,
                                        const FastMemberInfo* bf, FastStaticConst v)
{
    FastByteBuf* buf = sec == FSEC_DATA ? &cc->obj->data :
                       sec == FSEC_RODATA ? &cc->obj->rodata : &cc->obj->data_rel_ro;
    FastRelaBuf* rela = sec == FSEC_DATA ? &cc->obj->rela_data :
                        sec == FSEC_RODATA ? &cc->obj->rela_rodata : &cc->obj->rela_data_rel_ro;
    // #3577: the initializer's implicit conversion, checked as an assignment's (fast_cc_check_static_initializer), but
    // for an integer constant into a scalar integer field (no bit-field) that settles it in place, as the tables do.
    const uint32_t tkind = target & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL |
                                     BUSTER_FTYPE_SCALAR_MASK);
    const uint32_t vkind = v.type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK);
    // (FTYPE_U8 is FTYPE_I8 + 1: the plain char's kind without a branch.)
    const uint32_t plain_char_ptr = ((FTYPE_I8 + (uint32_t)cc->unsigned_char) | BUSTER_FTYPE_SCALAR_CHAR) + (UINT32_C(1) << BUSTER_FTYPE_PTR_SH);
    const uint32_t settled = ((uint32_t)(v.sym_idx == 0) & (uint32_t)(v.is_string == 0) & (uint32_t)(v.is_float == 0) & (uint32_t)(bf == NULL) &
                              (uint32_t)((tkind & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) &
                              BUSTER_FAST_INT_SETTLED(v.val, vkind, tkind & BUSTER_FTYPE_SCALAR_MASK)) |
                             // a string literal into a (const) plain char pointer
                             ((uint32_t)(v.is_string != 0) & (uint32_t)(bf == NULL) &
                              (uint32_t)((target & ~(BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL | BUSTER_FTYPE_PCONST)) == plain_char_ptr));
    if (settled == 0)
    {
        fast_cc_check_static_initializer(cc, target, bf, &v);
        if (cc->bailout) { return; }
    }
    if (v.is_string)
    {
        const uint32_t so = fast_obj_add_string_lit(cc->obj, v.str_ptr, v.str_len);
        v.is_string = 0;
        v.sym_idx = cc->obj->sec_sym_rodata_str;
        v.val = (int64_t)so;
    }
    if (bf != NULL && bf->bit_width != 0)
    {
        if (v.sym_idx != 0) { fast_cc_bail(cc, "reloc in bitfield init"); return; }
        fsc_cast_to(cc, &v, target);
        const uint64_t mask = bf->bit_width == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << bf->bit_width) - 1u);
        uint64_t cur = 0;
        if (byte_off + bf->bit_unit_size <= buf->cap)
        {
            memcpy(&cur, buf->data + byte_off, bf->bit_unit_size);
            cur = (cur & ~(mask << bf->bit_offset)) | (((uint64_t)v.val & mask) << bf->bit_offset);
            memcpy(buf->data + byte_off, &cur, bf->bit_unit_size);
        }
        return;
    }
    fsc_cast_to(cc, &v, target);
    const uint32_t sz = ftype_size(cc, target);
    if (byte_off + sz > buf->cap) { cc->obj->overflow = true; return; }
    if (v.sym_idx != 0)
    {
        if (sz != 8u) { fast_cc_bail(cc, "non-8B reloc in static init"); return; }
        memset(buf->data + byte_off, 0, 8u);
        fast_obj_add_rela(cc->obj, rela, byte_off, v.sym_idx, R_X86_64_64, v.val);
    }
    else
    {
        memcpy(buf->data + byte_off, &v.val, sz);
    }
}

// #3577: the bit of position IDX (a member's index or an element's) in an initializer frame's touched mask, 0 past 64. A
// positional element whose bit a designator set before it overrides that initialization, the full pipeline's
// -Winitializer-overrides: each initializer collects them, branch-free, and is refused at its end.
#define BUSTER_FAST_INIT_BIT(idx) ((UINT64_C(1) << ((idx) & 63u)) & (UINT64_C(0) - (uint64_t)((idx) < 64u)))

// #3577: where an initializer's frame has reached, in bits from its object's start: the next positional element's start
// (an array's index times its element's size; a record's member, a bit-field's bit, past a flattened anonymous union's
// siblings, or its end past the last one, or past a union's first). A designator whose target starts before it may
// override what the frame initialized, the full pipeline's -Winitializer-overrides: refused, as an element a designator
// skipped would be (out of order).
static __attribute__((cold, noinline)) int64_t fast_cc_init_frame_reached(const FastCCState* cc, FastType type, uint32_t idx)
{
    int64_t reached = 0;
    if (idx != 0 && ftype_is_array(type))
    {
        reached = (int64_t)idx * (int64_t)ftype_size(cc, cc->ctypes[type & BUSTER_FTYPE_CIDX_MASK].inner) * 8;
    }
    else if (idx != 0 && ftype_is_record(type))
    {
        const FastComplexType* rec = cc->ctypes + (type & BUSTER_FTYPE_CIDX_MASK);
        // Past the siblings of a flattened anonymous union, which share its offset.
        const FastMemberInfo* last = cc->member_info + rec->start + (idx - 1u);
        while (idx < rec->count && last->bit_width == 0 && cc->member_info[rec->start + idx].bit_width == 0 &&
               cc->member_info[rec->start + idx].byte_offset == last->byte_offset)
        {
            idx += 1u;
        }
        if (idx < rec->count && rec->kind == FCTYPE_STRUCT)
        {
            const FastMemberInfo* mi = cc->member_info + rec->start + idx;
            reached = (int64_t)mi->byte_offset * 8 + (mi->bit_width != 0 ? mi->bit_offset : 0);
        }
        else
        {
            reached = (int64_t)rec->size * 8;
        }
    }
    return reached;
}

// #3577: whether a designator whose first component is POSITION of an initializer frame of TYPE (a member's index or an
// element's) overrides what the frame initialized, the full pipeline's -Winitializer-overrides (or "partially
// overrides", where the position's subobject is a member's member): the frame's TOUCHED positions (a union's any, a
// flattened anonymous union's siblings with its member), which it then marks; past the 64 positions a mask holds, a
// target starting before what the frame reached (fast_cc_init_frame_reached at IDX, the frame's next positional element
// before the designator) is refused, as out of order.
static __attribute__((cold, noinline)) bool fast_cc_init_overrides(const FastCCState* cc, FastType type, int64_t base_off, uint32_t idx,
                                                                    uint64_t* touched, uint32_t position, int64_t target)
{
    bool overrides = false;
    if (position < 64u)
    {
        uint64_t bits = UINT64_C(1) << position;
        bool shared = false;
        if (ftype_is_record(type))
        {
            const FastComplexType* rec = cc->ctypes + (type & BUSTER_FTYPE_CIDX_MASK);
            shared = rec->kind == FCTYPE_UNION;
            const FastMemberInfo* mi = position < rec->count ? cc->member_info + rec->start + position : NULL;
            for (uint32_t q = 0; mi != NULL && mi->bit_width == 0 && q < rec->count && q < 64u; q += 1)
            {
                const FastMemberInfo* other = cc->member_info + rec->start + q;
                bits |= (uint64_t)(other->bit_width == 0 && other->byte_offset == mi->byte_offset) << q;
            }
        }
        overrides = (*touched & bits) != 0 || (shared && *touched != 0);
        *touched |= bits;
    }
    else
    {
        overrides = target < base_off * 8 + fast_cc_init_frame_reached(cc, type, idx);
    }
    return overrides;
}

// A literal followed by a comma is a complete initializer. Stop before expressions, designators,
// the end of a brace-elided row, or the end of the output section.
static uint32_t fast_cc_init_int_run(FastCCState* cc, FastType type, uint32_t size, FastByteBuf* buf, uint64_t offset, uint32_t remaining)
{
    const uint32_t scalar = type & BUSTER_FTYPE_SCALAR_MASK;
    uint32_t count = 0;
    if (!ftype_is_complex(type) && ftype_ptr_depth(type) == 0 && scalar >= FTYPE_I8 && scalar <= FTYPE_U64 && offset <= buf->len && offset <= buf->cap)
    {
        const uint32_t room = (buf->cap - (uint32_t)offset) / size;
        const uint32_t pairs = (cc->tcount - cc->pos) / 2u;
        uint32_t limit = remaining < room ? remaining : room;
        if (pairs < limit)
        {
            limit = pairs;
        }
        const __m256i signed_pair = _mm256_set1_epi16(FTOK_INT_LIT | (FTOK_COMMA << 8));
        const __m256i unsigned_pair = _mm256_set1_epi16(FTOK_INT_U32 | (FTOK_COMMA << 8));
        const __m512i evens = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
        while (limit - count >= 16u)
        {
            const uint32_t pos = cc->pos + 2u * count;
            const __m256i kinds = _mm256_loadu_si256((const __m256i*)(cc->tk + pos));
            const __mmask16 signed_lanes = _mm256_cmpeq_epi16_mask(kinds, signed_pair);
            if ((signed_lanes | _mm256_cmpeq_epi16_mask(kinds, unsigned_pair)) != UINT16_MAX)
            {
                break;
            }
            const __m512i lo = _mm512_loadu_si512(cc->td + pos);
            const __m512i hi = _mm512_loadu_si512(cc->td + pos + 16u);
            const __m512i values = _mm512_permutex2var_epi32(lo, evens, hi);
            uint8_t* const dst = buf->data + offset + count * size;
            if (size == 1u)
            {
                _mm_storeu_si128((__m128i*)dst, _mm512_cvtepi32_epi8(values));
            }
            else if (size == 2u)
            {
                _mm256_storeu_si256((__m256i*)dst, _mm512_cvtepi32_epi16(values));
            }
            else if (size == 4u)
            {
                _mm512_storeu_si512(dst, values);
            }
            else
            {
                const __m256i low = _mm512_castsi512_si256(values);
                const __m256i high = _mm512_extracti64x4_epi64(values, 1);
                const __m512i low64 = _mm512_mask_cvtepi32_epi64(_mm512_cvtepu32_epi64(low), (__mmask8)signed_lanes, low);
                const __m512i high64 = _mm512_mask_cvtepi32_epi64(_mm512_cvtepu32_epi64(high), (__mmask8)(signed_lanes >> 8), high);
                _mm512_storeu_si512(dst, low64);
                _mm512_storeu_si512(dst + 64, high64);
            }
            count += 16u;
        }
        cc->pos += count * 2u;
        const uint32_t end = (uint32_t)offset + count * size;
        if (end > buf->len)
        {
            buf->len = end;
        }
    }
    return count;
}

// Iterative static/global initializer: allocates storage in `.rodata` / `.data.rel.ro` / `.data`,
// sizes incomplete arrays (`int a[] = {...}` or `char s[] = "..."`), and emits bytes + relocations!
static __attribute__((noinline)) uint32_t fast_cc_init_static(FastCCState* cc, FastType* io_type, FastSecId sec, uint32_t align)
{
    typedef struct SInitFrame SInitFrame;
    struct SInitFrame
    {
        FastType type;
        uint32_t base_off;
        uint32_t idx;
        bool braced;
        bool is_incomplete_arr;
        bool deep;          // the last designator reached into a member's member (#3577)
        uint32_t elem_size;
        uint32_t max_elem;
        uint64_t touched;   // the positions (members, elements) under 64 initialized (#3577: fast_cc_init_overrides)
    };

    FastType root_t = *io_type;
    if (ftype_is_unsupported(root_t)) { fast_cc_bail(cc, "unsupported type static init"); return 0; }

    FastByteBuf* buf = sec == FSEC_DATA ? &cc->obj->data :
                       sec == FSEC_RODATA ? &cc->obj->rodata : &cc->obj->data_rel_ro;
    const uint32_t type_align = ftype_align(cc, root_t);
    if (align < type_align) { align = type_align; }
    if (sec == FSEC_DATA && align > cc->obj->data_align) { cc->obj->data_align = align; }
    if (sec == FSEC_RODATA && align > cc->obj->rodata_align) { cc->obj->rodata_align = align; }
    if (sec == FSEC_DATA_REL_RO && align > cc->obj->data_rel_ro_align) { cc->obj->data_rel_ro_align = align; }

    // Fast path 1: `char s[] = "hello"` or `char s[N] = "hello"` (optionally braced `{"hello"}`)
    const bool is_arr = ftype_is_array(root_t);
    if (is_arr && ftype_is_char_type(cc->ctypes[root_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
        (fast_cc_peek(cc) == FTOK_STR_LIT ||
         (fast_cc_peek(cc) == FTOK_LBRACE && fast_cc_peek_at(cc, 1u) == FTOK_STR_LIT)))
    {
        const bool br = fast_cc_peek(cc) == FTOK_LBRACE;
        if (br) { cc->pos += 1u; }
        uint32_t slen = 0;
        const char* sp = fast_cc_parse_string_lit(cc, &slen);
        if (br)
        {
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
        }
        FastComplexType* ar = cc->ctypes + (root_t & BUSTER_FTYPE_CIDX_MASK);
        if (ar->count == UINT32_MAX)
        {
            ar->count = slen + 1u;
            ar->size = slen + 1u;
            ar->complete = 1u;
        }
        uint8_t* dst = NULL;
        const uint32_t off = fast_obj_alloc_section(cc->obj, sec, ar->size, align, &dst);
        if (dst != NULL)
        {
            const uint32_t cpy = slen < ar->size ? slen : ar->size;
            memcpy(dst, sp, cpy);
        }
        return off;
    }

    // Fast path 2: scalar global `T x = expr;`
    if (ftype_is_scalar(root_t))
    {
        const bool br = fast_cc_peek(cc) == FTOK_LBRACE;
        if (br) { cc->pos += 1u; }
        const FastStaticConst v = fast_cc_eval_static_const(cc);
        if (br)
        {
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
        }
        const uint32_t off = fast_obj_alloc_section(cc->obj, sec, ftype_size(cc, root_t), align, NULL);
        if (!cc->bailout) { fast_cc_write_static_scalar(cc, sec, off, root_t, NULL, v); }
        return off;
    }

    // Aggregate static initializer (struct / union / array, complete or incomplete `T a[] = {...}`)
    if (fast_cc_peek(cc) != FTOK_LBRACE)
    {
        fast_cc_bail(cc, "unbraced aggregate static init");
        return 0;
    }

    const bool inc_arr = is_arr && cc->ctypes[root_t & BUSTER_FTYPE_CIDX_MASK].count == UINT32_MAX;
    uint32_t root_off = 0;
    if (inc_arr)
    {
        const uint32_t mask = align - 1u;
        root_off = (buf->len + mask) & ~mask;
        if (root_off > buf->len && root_off <= buf->cap)
        {
            memset(buf->data + buf->len, 0, root_off - buf->len);
            buf->len = root_off;
        }
    }
    else
    {
        root_off = fast_obj_alloc_section(cc->obj, sec, ftype_size(cc, root_t), align, NULL);
    }

    SInitFrame st[32];
    uint32_t sp = 0;
    uint64_t overridden = 0;
    cc->pos += 1u; // '{'
    st[sp++] = (SInitFrame){
        .type = root_t,
        .base_off = root_off,
        .idx = 0,
        .braced = true,
        .is_incomplete_arr = inc_arr,
        .elem_size = is_arr ? ftype_size(cc, cc->ctypes[root_t & BUSTER_FTYPE_CIDX_MASK].inner) : 0u,
        .max_elem = 0,
    };

    while (sp > 0 && cc->pos < cc->tcount && !cc->bailout)
    {
        SInitFrame* f = st + (sp - 1u);
        if (fast_cc_peek(cc) == FTOK_RBRACE)
        {
            if (f->braced) { cc->pos += 1u; }
            sp -= 1u;
            if (sp > 0 && f->braced && fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            continue;
        }

        // Handle C99 designated initializers (`.field =` or `[idx] =` or chained `.a[2].b =`)!
        if (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
        {
            // Pop any unbraced intermediate frames before applying designator to innermost braced frame
            while (sp > 1u && !st[sp - 1u].braced) { sp -= 1u; }
            f = st + (sp - 1u);
            FastType cur_t = f->type;
            uint32_t cur_off = f->base_off;
            const FastMemberInfo* cur_bf = NULL;
            const uint32_t idx_before = f->idx;
            uint32_t position = UINT32_MAX;
            uint32_t components = 0;
            while (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                components += 1u;
                if (fast_cc_peek(cc) == FTOK_DOT)
                {
                    cc->pos += 1u;
                    if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "static .designator"); break; }
                    const uint32_t fid = fast_cc_data(cc);
                    cc->pos += 1u;
                    const FastComplexType* rec = cc->ctypes + (cur_t & BUSTER_FTYPE_CIDX_MASK);
                    const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fid);
                    if (mi == NULL) { fast_cc_bail(cc, "unknown static .designator"); break; }
                    if (cur_t == f->type)
                    {
                        position = (uint32_t)(mi - (cc->member_info + rec->start));
                        f->idx = position + 1u;
                    }
                    cur_off += mi->byte_offset;
                    cur_t = mi->type;
                    cur_bf = mi->bit_width != 0 ? mi : NULL;
                }
                else
                {
                    cc->pos += 1u;
                    const int64_t didx = fast_cc_const_int_expr(cc, false);
                    if (fast_cc_peek(cc) != FTOK_RBRACKET || didx < 0) { fast_cc_bail(cc, "static [designator]"); break; }
                    cc->pos += 1u;
                    if (!ftype_is_array(cur_t)) { fast_cc_bail(cc, "static [designator] non-array"); break; }
                    const FastComplexType* ar = cc->ctypes + (cur_t & BUSTER_FTYPE_CIDX_MASK);
                    const uint32_t esz = ftype_size(cc, ar->inner);
                    if (cur_t == f->type)
                    {
                        position = (uint32_t)didx;
                        f->idx = (uint32_t)didx + 1u;
                        if (f->is_incomplete_arr && (uint32_t)didx + 1u > f->max_elem)
                        {
                            const uint32_t need_end = f->base_off + ((uint32_t)didx + 1u) * esz;
                            if (need_end > buf->cap) { cc->obj->overflow = true; break; }
                            if (need_end > buf->len) { memset(buf->data + buf->len, 0, need_end - buf->len); buf->len = need_end; }
                            f->max_elem = (uint32_t)didx + 1u;
                        }
                    }
                    cur_off += (uint32_t)didx * esz;
                    cur_t = ar->inner;
                    cur_bf = NULL;
                }
            }
            if (fast_cc_peek(cc) == FTOK_ASSIGN) { cc->pos += 1u; }
            if (cc->bailout) { break; }
            // #3577: a target the frame initialized (-Winitializer-overrides); and the positional elements after a designator
            // into a member's member continue there, which this path doesn't follow.
            if (fast_cc_init_overrides(cc, f->type, f->base_off, idx_before, &f->touched, position, (int64_t)cur_off * 8 + (cur_bf != NULL ? cur_bf->bit_offset : 0)))
            {
                fast_cc_bail(cc, "designator overriding what the initializer initialized");
                break;
            }
            f->deep = components >= 2u;
            // Initialize target `cur_t` at `cur_off`
            if (ftype_is_scalar(cur_t))
            {
                const bool b2 = fast_cc_peek(cc) == FTOK_LBRACE;
                if (b2) { cc->pos += 1u; }
                const FastStaticConst v = fast_cc_eval_static_const(cc);
                if (b2 && fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
                fast_cc_write_static_scalar(cc, sec, cur_off, cur_t, cur_bf, v);
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            }
            else if (ftype_is_array(cur_t) && ftype_is_char_type(cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
                     fast_cc_peek(cc) == FTOK_STR_LIT)
            {
                uint32_t slen = 0;
                const char* sp_s = fast_cc_parse_string_lit(cc, &slen);
                const uint32_t asz = ftype_size(cc, cur_t);
                const uint32_t cpy = slen < asz ? slen : asz;
                if (cur_off + cpy <= buf->cap) { memcpy(buf->data + cur_off, sp_s, cpy); }
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            }
            else if (fast_cc_peek(cc) == FTOK_LBRACE)
            {
                cc->pos += 1u;
                if (sp >= 32u) { fast_cc_bail(cc, "static init depth"); break; }
                st[sp++] = (SInitFrame){ .type = cur_t, .base_off = cur_off, .idx = 0, .braced = true };
            }
            else
            {
                if (sp >= 32u) { fast_cc_bail(cc, "static init depth"); break; }
                st[sp++] = (SInitFrame){ .type = cur_t, .base_off = cur_off, .idx = 0, .braced = false };
            }
            continue;
        }

        // Positional element of current frame `f`
        if (f->deep)
        {
            fast_cc_bail(cc, "positional element after a designator into a member's member");
            break;
        }
        FastType elem_t = FTYPE_VOID;
        uint32_t elem_off = f->base_off;
        const FastMemberInfo* elem_bf = NULL;
        bool frame_done = false;

        if (ftype_is_array(f->type))
        {
            const FastComplexType* ar = cc->ctypes + (f->type & BUSTER_FTYPE_CIDX_MASK);
            if (!f->is_incomplete_arr && f->idx >= ar->count)
            {
                frame_done = true;
            }
            else
            {
                const uint32_t esz = ftype_size(cc, ar->inner);
                if (ar->count - f->idx >= 16u && (fast_cc_peek(cc) == FTOK_INT_LIT || fast_cc_peek(cc) == FTOK_INT_U32) &&
                    fast_cc_peek_at(cc, 1u) == FTOK_COMMA)
                {
                    const uint64_t offset = f->base_off + (uint64_t)f->idx * esz;
                    const uint32_t count = fast_cc_init_int_run(cc, ar->inner, esz, buf, offset, ar->count - f->idx);
                    if (count != 0)
                    {
                        // #3577: the run's elements are positional ones too, below 64 (BUSTER_FAST_INIT_BIT).
                        const uint32_t run_end = f->idx + count;
                        const uint32_t run_low = f->idx ^ ((f->idx ^ 64u) & (UINT32_C(0) - (uint32_t)(f->idx > 64u)));
                        const uint32_t run_high = run_end ^ ((run_end ^ 64u) & (UINT32_C(0) - (uint32_t)(run_end > 64u)));
                        const uint64_t run_bits = _bzhi_u64(UINT64_MAX, run_high) & ~_bzhi_u64(UINT64_MAX, run_low);
                        overridden |= f->touched & run_bits;
                        f->touched |= run_bits;
                        f->idx += count;
                        if (f->is_incomplete_arr && f->idx > f->max_elem)
                        {
                            f->max_elem = f->idx;
                        }
                        continue;
                    }
                }
                if (f->is_incomplete_arr && f->idx + 1u > f->max_elem)
                {
                    const uint32_t need_end = f->base_off + (f->idx + 1u) * esz;
                    if (need_end > buf->cap) { cc->obj->overflow = true; break; }
                    if (need_end > buf->len) { memset(buf->data + buf->len, 0, need_end - buf->len); buf->len = need_end; }
                    f->max_elem = f->idx + 1u;
                }
                elem_t = ar->inner;
                elem_off = f->base_off + f->idx * esz;
                overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                f->idx += 1u;
            }
        }
        else if (ftype_is_record(f->type))
        {
            const FastComplexType* rec = cc->ctypes + (f->type & BUSTER_FTYPE_CIDX_MASK);
            if (f->idx >= rec->count || (rec->kind == FCTYPE_UNION && f->idx >= 1u))
            {
                frame_done = true;
            }
            else
            {
                const FastMemberInfo* mi = cc->member_info + rec->start + f->idx;
                elem_t = mi->type;
                elem_off = f->base_off + mi->byte_offset;
                elem_bf = mi->bit_width != 0 ? mi : NULL;
                overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                f->idx += 1u;
                // Skip flattened anonymous-union siblings sharing the same byte_offset in a struct!
                if (rec->kind == FCTYPE_STRUCT && mi->bit_width == 0)
                {
                    while (f->idx < rec->count && cc->member_info[rec->start + f->idx].byte_offset == mi->byte_offset &&
                           cc->member_info[rec->start + f->idx].bit_width == 0)
                    {
                        overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                        f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                        f->idx += 1u;
                    }
                }
            }
        }
        else
        {
            frame_done = true;
        }

        if (frame_done)
        {
            if (!f->braced) { sp -= 1u; continue; }
            fast_cc_bail(cc, "excess elements in static init");
            break;
        }

        // String literal initializing nested `char[N]` field or array element!
        if (ftype_is_array(elem_t) && ftype_is_char_type(cc->ctypes[elem_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
            fast_cc_peek(cc) == FTOK_STR_LIT)
        {
            uint32_t slen = 0;
            const char* sp_s = fast_cc_parse_string_lit(cc, &slen);
            const uint32_t asz = ftype_size(cc, elem_t);
            const uint32_t cpy = slen < asz ? slen : asz;
            if (elem_off + cpy <= buf->cap) { memcpy(buf->data + elem_off, sp_s, cpy); }
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            continue;
        }

        if (ftype_is_scalar(elem_t))
        {
            const bool b2 = fast_cc_peek(cc) == FTOK_LBRACE;
            if (b2) { cc->pos += 1u; }
            const FastStaticConst v = fast_cc_eval_static_const(cc);
            if (b2)
            {
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
                else { fast_cc_bail(cc, "expected } after braced scalar static init"); break; }
            }
            fast_cc_write_static_scalar(cc, sec, elem_off, elem_t, elem_bf, v);
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
        }
        else if (fast_cc_peek(cc) == FTOK_LBRACE)
        {
            cc->pos += 1u;
            if (sp >= 32u) { fast_cc_bail(cc, "static init depth"); break; }
            st[sp++] = (SInitFrame){ .type = elem_t, .base_off = elem_off, .idx = 0, .braced = true };
        }
        else
        {
            // Brace elision: descend into nested aggregate without consuming '{'!
            if (sp >= 32u) { fast_cc_bail(cc, "static init depth"); break; }
            st[sp++] = (SInitFrame){ .type = elem_t, .base_off = elem_off, .idx = 0, .braced = false };
        }
    }

    // #3577: a positional element over one a designator initialized (BUSTER_FAST_INIT_BIT).
    if (overridden != 0)
    {
        fast_cc_bail(cc, "initializer overrides prior initialization");
    }
    if (inc_arr && !cc->bailout)
    {
        FastComplexType* ar = cc->ctypes + (root_t & BUSTER_FTYPE_CIDX_MASK);
        ar->count = st[0].max_elem;
        ar->size = st[0].max_elem * st[0].elem_size;
        ar->complete = 1u;
    }
    return root_off;
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 9b: Lazy FastVal register-stack engine & x86-64 codegen primitives

typedef enum FastValKind
{
    FVAL_VOID = 0,
    FVAL_IMM = 1,          // integer constant in `imm`
    FVAL_FIMM = 2,         // float/double bits in `imm`
    FVAL_REG = 3,          // rvalue in GP register `reg`
    FVAL_XMM = 4,          // rvalue in XMM register `reg` (0..7)
    FVAL_LOCAL = 5,        // lvalue at [rbp + off]
    FVAL_GLOBAL = 6,       // lvalue at ELF symbol `sym_idx` + `imm`
    FVAL_INDIRECT = 7,     // lvalue at [reg + off] (holds GP `reg`)
    FVAL_SPILL = 8,        // spilled rvalue at [rbp + off]
    FVAL_SPILL_ADDR = 9,   // spilled lvalue: address stored at [rbp + off], plus `addr_disp`
    FVAL_FLAGS = 10,       // boolean in CPU EFLAGS (`cc_code`)
    FVAL_BITFIELD = 11,    // bitfield lvalue (base in `bf_base`: LOCAL/GLOBAL/INDIRECT/SPILL_ADDR)
    FVAL_BUILTIN = 12,     // recognized __builtin_* callee (IdentId in `sym_idx`)
    FVAL_LOCAL_ADDR = 13,  // rvalue pointer = rbp + off (0 regs, 0 spills!)
    FVAL_GLOBAL_ADDR = 14  // rvalue pointer = sym_idx + imm (0 regs, 0 spills!)
} FastValKind;

typedef struct FastVal FastVal;
struct FastVal
{
    FastType type;
    uint8_t kind;          // FastValKind
    uint8_t reg;           // GP (0..15) or XMM (0..7)
    uint8_t is_lvalue;
    union
    {
        uint8_t cc_code;       // FVAL_FLAGS
        uint8_t bf_base;       // address kind for FVAL_BITFIELD
        uint8_t is_func_sym;   // FVAL_GLOBAL is a direct call target
    };
    union
    {
        int64_t imm;           // integer, float bits or global addend
        struct
        {
            int32_t off;       // frame offset or indirect displacement
            int32_t addr_disp; // displacement after reloading a spilled address
        };
    };
    uint32_t sym_idx;          // ELF symbol or builtin IdentId
    uint8_t bf_off;
    uint8_t bf_width;
    uint8_t bf_unit;
    uint8_t bf_signed;
    uint8_t marks;         // BUSTER_FVAL_MARK_*: what an operator on the value must refuse (#3577)
    uint8_t op;            // the binary operator (FTOK_*) that made it, FTOK_BANG for `!`, or FTOK_STR_LIT or FTOK_CHAR_LIT for
                           // a literal, outside parentheses (#3577)
    uint8_t lasting;       // BUSTER_FVAL_LASTING_*: what a value keeps in any parentheses (#3577)
    uint8_t seq_start;     // a variable by name (or in parentheses): its -Wunsequenced log entry + 1 (#3577); else 0
    uint32_t seq_ident;    // ... and its name's identifier, the variable's identity in the log (where seq_start isn't 0)
};

// The expression value with #3577's eight bytes (marks, op, lasting, seq_start, seq_ident) past the 24 of #3594.
_Static_assert(sizeof(FastVal) == 32u, "expression stack entry size");

// A value's lasting marks (#3577), kept in parentheses: a cast of a float to a wider floating type (-Wliteral-range),
// and a string literal, in casts too (-Wstring-compare, whose literal the full pipeline finds through them).
#define BUSTER_FVAL_LASTING_FLOAT_CAST 0x01u
#define BUSTER_FVAL_LASTING_STRING     0x02u

// A value's marks (#3577), lost where an operator makes a new value: a register variable, whose address is an error; a
// function whose call's null constant arguments are -Wnonnull; the result of a call whose discarding is -Wunused-result;
// a value with an effect (a call, an assignment, ++ or --, a cast to void), which a statement may discard quietly
// (-Wunused-value); and a const or pure function, whose call has none.
#define BUSTER_FVAL_MARK_REGISTER      0x01u
#define BUSTER_FVAL_MARK_NONNULL       0x02u
#define BUSTER_FVAL_MARK_UNUSED_RESULT 0x04u
#define BUSTER_FVAL_MARK_EFFECT        0x08u
#define BUSTER_FVAL_MARK_PURE          0x10u
// A constant that isn't an integer literal (folded, a character, an enumerator, a cast): only the literal 0 is a null
// pointer constant the full pipeline converts quietly (-Wnon-literal-null-conversion).
#define BUSTER_FVAL_MARK_NONLITERAL    0x20u
// A conditional with constant arms, which the implicit conversion of the value checks one by one
// (fast_cc_constant_conversion_quiet): sym_idx says how imm holds them (FastArmsKind).
#define BUSTER_FVAL_MARK_CONST_ARMS    0x40u
// A constant `<<` result, which a boolean context finds always true (-Wtautological-constant-compare).
#define BUSTER_FVAL_MARK_SHIFTED       0x80u
// FARMS_BITS's flags above the bits: an arm is 0, an arm isn't, an arm isn't constant (a boolean context of a
// conditional whose arms are all constant and all 0 or all not is -Wtautological-constant-compare's).
#define BUSTER_FARMS_ZERO              0x10000u
#define BUSTER_FARMS_NONZERO           0x20000u
#define BUSTER_FARMS_VARIABLE          0x40000u
// A FARMS_FLOAT arm's summary (fast_cc_float_summary): the double is 0 or 1 (a _Bool takes it quietly), and bit
// BUSTER_FARMS_FLOAT_INT(bytes log2, signed) where it is integral and within that integer type's range.
#define BUSTER_FARMS_FLOAT_BOOL        0x1u
#define BUSTER_FARMS_FLOAT_INT(log2, sign) (UINT32_C(2) << ((log2) * 2u + (sign)))
// A conditional's arms in its value's sym_idx (#3577): the kind (a FastArmsKind) in the low 2 bits, the description
// (FastArm's value: FARMS_BITS' and FARMS_FLOATING's bits and flags, FARMS_FLOAT's summary, all under 30 bits) above,
// where a spill, which writes the slot over imm's low half, leaves it.
#define BUSTER_FARMS_PACK(kind, value) ((uint32_t)(kind) | ((uint32_t)(value) << 2))
#define BUSTER_FARMS_KIND(v) ((v)->sym_idx & 3u)
#define BUSTER_FARMS_VALUE(v) ((v)->sym_idx >> 2)

// How a conditional's value (FVAL_REG or FVAL_XMM, whose sym_idx and imm are otherwise unused) holds its constant arms.
typedef enum FastArmsKind
{
    FARMS_NONE = 0,
    FARMS_BITS = 1,        // integer arms: imm's low byte the unsigned bits of the largest non-negative one, the next
                           // byte the signed bits of the most negative one (fast_cc_int_bits_quiet)
    FARMS_FLOAT = 2,       // one floating arm, its summary (BUSTER_FARMS_FLOAT_*: what its conversions keep)
    FARMS_FLOATING = 3,    // floating arms, or a floating and integer ones (imm the integer ones' bits, as FARMS_BITS)
} FastArmsKind;

// One arm, for the conditional's completion: its kind (a FastArmsKind, FARMS_NONE for an arm that isn't constant) and
// value, and whether an integer arm is other than the literal 0 (which a pointer conditional's arm may be alone).
typedef struct FastArm FastArm;
struct FastArm
{
    int64_t value;
    uint8_t kind;
    uint8_t not_null_literal;
};

// An integer constant's bits as FARMS_BITS holds them: the unsigned bits of a non-negative value (0 for 0), the signed
// bits of a negative one (its sign included) in the next byte.
static uint32_t fast_cc_value_bits(int64_t value, bool unsigned_value)
{
    uint32_t bits = 0;
    if (value < 0 && !unsigned_value)
    {
        bits = (64u - (uint32_t)__builtin_clrsbll(value)) << 8;
    }
    else
    {
        bits = value == 0 ? 0u : 64u - (uint32_t)__builtin_clzll((uint64_t)value);
    }
    return bits;
}

// Whether integer constants of those bits (fast_cc_value_bits, merged by maximum) convert quietly to an integer of
// `target` bits (a type's, or a bit-field's width), `dst_signed` or not, from a source type `src_signed` or not, as the
// full pipeline's
// -Wconstant-conversion decides: a non-negative value that needs fewer bits than the target, or as many where target
// or source is unsigned (`unsigned char c = 255`, `int i = 4294967295u`); a negative one whose signed bits the target
// holds (`unsigned char c = -1` too). Where the full pipeline is quieter still (a hex literal into a signed char),
// this is stricter.
static bool fast_cc_int_bits_quiet(uint32_t bits, uint32_t target, bool dst_signed, bool src_signed)
{
    const uint32_t positive = bits & 0xffu;
    const uint32_t negative = (bits >> 8) & 0xffu;
    return negative <= target && (positive < target || (positive == target && (!dst_signed || !src_signed)));
}

// #3577: an arm of a conditional, as fast_cc_constant_conversion_quiet will check it: an integer constant (a literal or
// a fold) by its bits, a floating constant by its value, and a nested conditional by its own arms. A pointer constant
// (`(char *)0`) is no integer arm.
// #3577: a floating arm's summary (BUSTER_FARMS_FLOAT_*): what the constant conversion checks ask of its double.
static uint32_t fast_cc_float_summary(double d)
{
    uint32_t summary = (uint32_t)((d == 0.0) | (d == 1.0)) * BUSTER_FARMS_FLOAT_BOOL;
    for (uint32_t log2 = 0; log2 < 4u; log2 += 1)
    {
        const double half = (double)(UINT64_C(1) << ((8u << log2) - 1u));
        for (uint32_t sign = 0; sign < 2u; sign += 1)
        {
            const double top = sign != 0 ? half : 2.0 * half;
            const double bottom = sign != 0 ? -half : 0.0;
            // The range first, then the integrality test's conversion, which the range makes defined.
            const bool kept = d == d && d >= bottom && d < top && (d >= 0.0 ? (double)(uint64_t)d == d : (double)(int64_t)d == d);
            summary |= (uint32_t)kept * BUSTER_FARMS_FLOAT_INT(log2, sign);
        }
    }
    return summary;
}

static FastArm fast_cc_arm(const FastCCState* cc, const FastVal* v)
{
    FastArm arm = { 0 };
    if (v->kind == FVAL_IMM && !ftype_is_ptr(v->type))
    {
        const bool unsigned64 = ftype_size(cc, v->type) == 8u && !ftype_is_signed(cc, v->type);
        arm.kind = FARMS_BITS;
        arm.value = (int64_t)(fast_cc_value_bits(v->imm, unsigned64) | (v->imm == 0 ? BUSTER_FARMS_ZERO : BUSTER_FARMS_NONZERO));
        arm.not_null_literal = (uint8_t)(v->imm != 0 || (v->marks & BUSTER_FVAL_MARK_NONLITERAL) != 0);
    }
    else if (v->kind == FVAL_FIMM)
    {
        double d = 0.0;
        if (ftype_is_f64(v->type)) { memcpy(&d, &v->imm, 8); }
        else { float f = 0.0f; const uint32_t b = (uint32_t)v->imm; memcpy(&f, &b, 4); d = (double)f; }
        arm.kind = FARMS_FLOAT;
        arm.value = (int64_t)fast_cc_float_summary(d);
        arm.not_null_literal = 1u;
    }
    else if ((v->marks & BUSTER_FVAL_MARK_CONST_ARMS) != 0)
    {
        arm.kind = (uint8_t)BUSTER_FARMS_KIND(v);
        arm.value = (int64_t)BUSTER_FARMS_VALUE(v);
        arm.not_null_literal = 1u;
    }
    return arm;
}

// Two arms' constants as one description: integer arms' bits by maximum, one arm's where the other isn't constant,
// and floating where either is floating (with the integer arms' bits).
static FastArm fast_cc_arms_merge(FastArm a, FastArm b)
{
    FastArm m = a.kind == FARMS_NONE ? b : a;
    if (a.kind != FARMS_NONE && b.kind != FARMS_NONE)
    {
        const uint32_t abits = a.kind == FARMS_FLOAT ? 0u : (uint32_t)a.value;
        const uint32_t bbits = b.kind == FARMS_FLOAT ? 0u : (uint32_t)b.value;
        const uint32_t positive = (abits & 0xffu) > (bbits & 0xffu) ? (abits & 0xffu) : (bbits & 0xffu);
        const uint32_t negative = (abits & 0xff00u) > (bbits & 0xff00u) ? (abits & 0xff00u) : (bbits & 0xff00u);
        m.kind = a.kind == FARMS_BITS && b.kind == FARMS_BITS ? FARMS_BITS : FARMS_FLOATING;
        m.value = (int64_t)(positive | negative | ((abits | bbits) & (BUSTER_FARMS_ZERO | BUSTER_FARMS_NONZERO | BUSTER_FARMS_VARIABLE)));
        m.not_null_literal = (uint8_t)(a.not_null_literal | b.not_null_literal);
    }
    else if (m.kind != FARMS_NONE && m.kind != FARMS_FLOAT)
    {
        m.value |= (int64_t)BUSTER_FARMS_VARIABLE;
    }
    return m;
}

// Whether a value is a condition as the full pipeline takes one quietly (#3577): a scalar (anything else is its error),
// and neither a constant `<<` nor a conditional of integer constant arms all non-zero or all zero, which it finds always
// true or false (-Wtautological-constant-compare). For a condition, the operands of ! && || and conversion to _Bool.
// And not an array, a function or an address, which is never null (-Wpointer-bool-conversion).
// (A macro, not a function: every condition takes it.) 1 when quiet, else 0.
// The conditions BUSTER_FAST_CONDITION_QUIET may question, by the value's kind, type and marks alone: a comparison's
// flags never; a record, enum, array or function by value, void or a type this path doesn't support, an address, a
// function, or a constant shift's or a conditional's constant arms' value may (#3577).
#define BUSTER_FAST_CONDITION_SUSPECT(v) \
    ((uint32_t)((v)->kind != FVAL_FLAGS) & \
     ((uint32_t)(((v)->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == BUSTER_FTYPE_COMPLEX_BIT) | \
      ((uint32_t)(((v)->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) & \
       (uint32_t)((BUSTER_FAST_OTHER_SCALARS >> ((v)->type & BUSTER_FTYPE_SCALAR_MASK)) & 1u)) | \
      (uint32_t)((v)->kind == FVAL_LOCAL_ADDR) | (uint32_t)((v)->kind == FVAL_GLOBAL_ADDR) | (uint32_t)((v)->kind == FVAL_GLOBAL) | \
      (uint32_t)(((v)->marks & (BUSTER_FVAL_MARK_SHIFTED | BUSTER_FVAL_MARK_CONST_ARMS)) != 0)))
// The scalar kinds of no operand class (void, the unsupported ones), a bit each.
#define BUSTER_FAST_OTHER_SCALARS ((1u << FTYPE_VOID) | (1u << FTYPE_UNSUP16) | (1u << FTYPE_UNSUP2))
#define BUSTER_FAST_CONDITION_QUIET(cc, v) (BUSTER_FAST_CONDITION_SUSPECT(v) == 0 || BUSTER_FAST_CONDITION_EXACT(cc, v))
#define BUSTER_FAST_CONDITION_EXACT(cc, v) \
    ((((uint32_t)((v)->kind != FVAL_FLAGS) & (uint32_t)(BUSTER_FAST_OPERAND_CLASS(cc, (v)->type) == BUSTER_FOPERAND_OTHER)) | \
      ((uint32_t)((v)->type >> 31) & (uint32_t)(((v)->type & BUSTER_FTYPE_PTR_MASK) == 0) & \
       ((uint32_t)((cc)->ctypes[(v)->type & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL((v)->type)].kind == FCTYPE_ARRAY) | \
        (uint32_t)((cc)->ctypes[(v)->type & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL((v)->type)].kind == FCTYPE_FUNC))) | \
      (uint32_t)((v)->kind == FVAL_LOCAL_ADDR) | (uint32_t)((v)->kind == FVAL_GLOBAL_ADDR) | \
      ((uint32_t)((v)->kind == FVAL_GLOBAL) & (uint32_t)((v)->is_func_sym != 0)) | \
      ((uint32_t)((v)->kind == FVAL_IMM) & (uint32_t)(((v)->marks & BUSTER_FVAL_MARK_SHIFTED) != 0)) | \
      ((uint32_t)(((v)->marks & BUSTER_FVAL_MARK_CONST_ARMS) != 0) & (uint32_t)(BUSTER_FARMS_KIND(v) == FARMS_BITS) & \
       (uint32_t)((BUSTER_FARMS_VALUE(v) & BUSTER_FARMS_VARIABLE) == 0) & \
       (uint32_t)((BUSTER_FARMS_VALUE(v) & (BUSTER_FARMS_ZERO | BUSTER_FARMS_NONZERO)) != (BUSTER_FARMS_ZERO | BUSTER_FARMS_NONZERO)))) == 0)

// Whether a function symbol is one of the library's builtins that are const or pure (#3577: `abs`, `isalpha`), whose
// discarded call the full pipeline warns of as of an attributed one: asked of the driver at the symbol's first use by
// name (its push_marks are BUSTER_FSYM_PUSH_UNASKED until then), and only of a function with external linkage. Its
// push marks, then, PURE or none.
static __attribute__((cold, noinline)) uint8_t fast_cc_library_pure_query(FastCCState* cc, FastSym* sym)
{
    const FastIdentEntry* e = cc->pp->idents.entries + sym->ident_id;
    const bool external = sym->storage != 1u && sym->storage != 4u;
    const bool pure = external && cc->pp->feature_query != NULL &&
                      cc->pp->feature_query(cc->pp->feature_context, C_DIRECT_QUERY_PURE_LIBRARY_FUNCTION,
                                            (const uint8_t*)(cc->pp->idents.strings + e->str_offset), e->length) != 0;
    sym->push_marks = pure ? (uint8_t)BUSTER_FSYM_PUSH_PURE : 0u;
    return sym->push_marks;
}
_Static_assert(BUSTER_FSYM_PUSH_PURE == BUSTER_FVAL_MARK_PURE && BUSTER_FSYM_PUSH_NONLITERAL == BUSTER_FVAL_MARK_NONLITERAL,
               "a symbol's push marks are its value's");

// Scratch register allocation order (RAX, R10, R11 first so ABI arg regs stay free longer!)
static const uint8_t fast_gp_pool[9] = {
    FREG_RAX, FREG_R10, FREG_R11, FREG_RCX, FREG_RDX, FREG_RSI, FREG_RDI, FREG_R8, FREG_R9
};

typedef struct FastExprCtx FastExprCtx;
struct FastExprCtx
{
    FastCCState* cc;
    // The scalars first, apart from the stacks (#3577): a 32-byte value makes vstack 4096 bytes, so a scalar right past
    // it would share its 4 KiB offset with vstack[0], the entry every expression writes and reads (4K aliasing).
    uint32_t vsp;
    uint32_t arg_sp;
    uint16_t gp_used;      // bitmask of live GP scratch regs in vstack/call_args
    uint8_t xmm_used;      // bitmask of live XMM scratch regs (0..7)
    uint8_t eval_only;     // >0 inside sizeof(expr) / _Alignof(expr) / _Generic ctrl, a dead branch (#3592) or a compile
                           // again for the log (#3577): emit ZERO machine code!
    uint8_t unevaluated;   // #3577: >0 inside sizeof(expr) / _Alignof(expr) / _Generic ctrl alone, C's unevaluated operands
    uint8_t seq_logging;   // #3577: 1 where the -Wunsequenced log below is kept (a compile again); 0 where seq_mod_* count
    // #3577: without the log, the variables the expression's ++, -- and assignments modify (each lvalue's seq_ident; the
    // first four), how many, and the expression's first token: the end decides from them, and compiles again for the log
    // where they leave it open (fast_cc_sequenced_lazy).
    uint32_t seq_mod_count;
    uint32_t seq_mod_ids[4];
    uint32_t seq_start_pos;
    uint32_t cmp_pos;      // #3577: the token of the binary operator fval_emit_binary emits (fast_cc_comparison_reported)
    // #3577: for -Wunsequenced, a log of the variables the expression names where evaluated and the lvalues its ++, --
    // and assignments modify, in order, by identity (BUSTER_FSEQ_*; 0 for an lvalue that isn't a variable by name): bit i
    // of seq_mods marks entry i a modification, seq_own its own operands' first entry + 1 (0 unknown). A variable modified
    // and then named or modified again, or named earlier than its modification's operands, sets seq_hit (by the modified
    // variables' hash bits, seq_bloom), and only then does the end of the expression look at the log, with the regions
    // that sequence points order (`&&`, `||`, `,`, `?:`: entries [lo, mid) before [mid, hi), each + 1). The arrays sit
    // between the two stacks, which they keep from sharing offsets (call_args[0]'s is vstack[21]'s).
    uint32_t seq_count;
    uint32_t seq_hit;
    uint32_t seq_regions;
    uint64_t seq_mods;
    uint64_t seq_bloom;
    FastVal vstack[128];
    uint64_t seq_log[64];
    uint8_t seq_own[64];
    uint8_t seq_region[32][3];
    FastVal call_args[128];
};

// #3577: a variable's identity for -Wunsequenced is its name's identifier (never 0): an expression declares nothing, so
// a name is one variable throughout it. An lvalue has one where it is a local or global variable by name (seq_start
// isn't 0, and its seq_ident is the name's); any other lvalue has none (0).
#define BUSTER_FSEQ_LVALUE(v) \
    ((uint64_t)(v)->seq_ident & \
     (UINT64_C(0) - (uint64_t)((uint32_t)((v)->seq_start != 0) & ((uint32_t)((v)->kind == FVAL_LOCAL) | (uint32_t)((v)->kind == FVAL_GLOBAL)))))
// An identity's hash bit in seq_bloom.
#define BUSTER_FSEQ_BIT(identity) (UINT64_C(1) << ((uint64_t)(identity) * UINT64_C(0x9E3779B97F4A7C15) >> 58))
// Log a naming where LOGGED (0 or 1: a use of a variable by name where evaluated); a modified variable's sets seq_hit.
// Every name takes one: the hash is taken only once something was modified (seq_bloom), which few names see, behind a
// branch.
#define BUSTER_FSEQ_NAME(ex, identity, logged) \
    if (__builtin_expect((ex)->seq_logging != 0, 0)) \
    { \
        fast_cc_seq_name(ex, identity, logged); \
    }
// A region's bounds, which the end's check reads when unknown ones (0) aren't counted.
#define BUSTER_FSEQ_REGION(ex, lo, mid, hi) \
    if (__builtin_expect((ex)->seq_logging != 0, 0)) \
    { \
        fast_cc_seq_region(ex, lo, mid, hi); \
    }
// The log's count + 1 as a seq_start or an operator's seq_mid, in the byte it fits: past 253 entries the positions
// wrap, and the expression's end refuses a log that long once anything in it was modified (seq_bloom), as the log's
// 64 entries would anyway. Every use of a variable and every operator takes one.
#define BUSTER_FSEQ_MARK(ex) ((uint8_t)((ex)->seq_count + 1u))
// Where an operator about to be pushed on OPS (OSP of them) has its left operand begin: the operator below's seq_mid, or
// the expression's first entry; 0 where that operator doesn't keep it.
// Both where the expression keeps the log (LOGGING, a constant of fast_cc_expr_compile); 0 where it doesn't.
#define BUSTER_FSEQ_MARK_IF(ex, logging) ((uint8_t)((logging) ? BUSTER_FSEQ_MARK(ex) : 0u))
#define BUSTER_FSEQ_BELOW_IF(ops, osp, logging) ((uint8_t)((logging) ? BUSTER_FSEQ_BELOW(ops, osp) : 0u))
#define BUSTER_FSEQ_BELOW(ops, osp) \
    ((uint8_t)(((uint32_t)(ops)[(osp) - (uint32_t)((osp) != 0)].seq_mid & (UINT32_C(0) - (uint32_t)((osp) != 0))) | (uint32_t)((osp) == 0)))

// #3577: the seq_ident of the value a modification of a variable by name makes (no name's identifier): where it is the
// expression's value, the modification is its last operator (BUSTER_FSEQ_RESULT of the lvalue, taken before).
#define BUSTER_FSEQ_MODIFIED UINT32_C(0xffffffff)
#define BUSTER_FSEQ_RESULT(v) (BUSTER_FSEQ_MODIFIED & (UINT32_C(0) - (uint32_t)((v)->seq_ident != 0)))

// #3577: the -Wunsequenced log's work where it is kept (a compile again, seq_logging), out of the hot functions that
// test for it: a name's entry (BUSTER_FSEQ_NAME), a sequence point's region (BUSTER_FSEQ_REGION), a modification
// (BUSTER_FSEQ_MODIFY).
static __attribute__((cold, noinline)) void fast_cc_seq_name(FastExprCtx* ex, uint32_t identity, uint32_t logged)
{
    ex->seq_log[ex->seq_count & 63u] = identity;
    if (ex->seq_bloom != 0)
    {
        ex->seq_hit |= (uint32_t)((ex->seq_bloom & BUSTER_FSEQ_BIT(identity)) != 0) & logged;
    }
    ex->seq_count += logged;
}

static __attribute__((cold, noinline)) void fast_cc_seq_region(FastExprCtx* ex, uint32_t lo, uint32_t mid, uint32_t hi)
{
    ex->seq_region[ex->seq_regions & 31u][0] = (uint8_t)lo;
    ex->seq_region[ex->seq_regions & 31u][1] = (uint8_t)mid;
    ex->seq_region[ex->seq_regions & 31u][2] = (uint8_t)hi;
    ex->seq_regions += (uint32_t)(lo != 0) & (uint32_t)(mid != 0);
}

// A use by name pushed (PUSHED) on the stack: a variable's (VARIABLE) entry + 1 as its seq_start, and its entry where
// evaluated.
static __attribute__((cold, noinline)) void fast_cc_seq_push(FastExprCtx* ex, uint32_t identity, uint32_t variable, uint32_t pushed)
{
    if (pushed != 0)
    {
        ex->vstack[ex->vsp - 1u].seq_start = (uint8_t)(BUSTER_FSEQ_MARK(ex) & (UINT32_C(0) - variable));
    }
    fast_cc_seq_name(ex, identity, variable & (uint32_t)(ex->unevaluated == 0));
}

// A modification of lvalue V, whose own operands' entries start at V's seq_start: a variable modified before (its hash
// bit), named before its operands (the vector compare looks at the log's first 16; more of them set it too) sets
// seq_hit. A variable by name alone (an lvalue an operator made, an element or a member, has no known start), as the
// full pipeline follows. The search runs where there is something to find.
static __attribute__((cold, noinline)) void fast_cc_seq_modify(FastExprCtx* ex, const FastVal* v)
{
    const uint64_t seq_identity = BUSTER_FSEQ_LVALUE(v);
    const uint32_t seq_tracked = (uint32_t)(seq_identity != 0);
    const uint32_t seq_count = ex->seq_count;
    const uint32_t seq_own = v->seq_start;
    // Earlier entries outside the operands: [0, own - 1), or all of them where the start isn't known.
    const uint32_t seq_earlier = seq_own - 1u + (uint32_t)(seq_own == 0) * (seq_count + 1u);
    // Only where there is something to find: entries before the operands, or an earlier modification (an assignment's
    // lvalue is most often the expression's first name, and its first modification).
    if (seq_tracked != 0)
    {
        if (((uint32_t)(seq_earlier != 0) | (uint32_t)(ex->seq_bloom != 0)) != 0)
        {
            const uint32_t seq_looked = seq_count ^ ((seq_earlier ^ seq_count) & (UINT32_C(0) - (uint32_t)(seq_earlier < seq_count)));
            const uint32_t seq_within = seq_looked ^ ((seq_looked ^ 16u) & (UINT32_C(0) - (uint32_t)(seq_looked > 16u)));
            const __mmask16 seq_valid = (__mmask16)_bzhi_u32(0xffffu, seq_within);
            const __m512i seq_wanted = _mm512_set1_epi64((long long)seq_identity);
            const __mmask8 seq_low = _mm512_mask_cmpeq_epi64_mask((__mmask8)seq_valid, _mm512_maskz_loadu_epi64((__mmask8)seq_valid, ex->seq_log), seq_wanted);
            const __mmask8 seq_high = _mm512_mask_cmpeq_epi64_mask((__mmask8)(seq_valid >> 8),
                                                                   _mm512_maskz_loadu_epi64((__mmask8)(seq_valid >> 8), ex->seq_log + 8), seq_wanted);
            const uint32_t seq_named_before = (uint32_t)((seq_low | seq_high) != 0);
            const uint32_t seq_modified_before = (uint32_t)((ex->seq_bloom & BUSTER_FSEQ_BIT(seq_identity)) != 0);
            ex->seq_hit |= seq_named_before | seq_modified_before | (uint32_t)(seq_looked > 16u);
        }
        ex->seq_bloom |= BUSTER_FSEQ_BIT(seq_identity);
    }
    ex->seq_log[seq_count & 63u] = seq_identity;
    ex->seq_own[seq_count & 63u] = (uint8_t)seq_own;
    ex->seq_mods |= (uint64_t)seq_tracked << (seq_count & 63u);
    ex->seq_count = seq_count + 1u;
}

// A modification of lvalue V: without the log, V's seq_ident counted where it isn't 0 (a variable by name's identifier;
// a member and the comma's value clear it, as they clear seq_start, and any other lvalue with one only makes the end
// look closer); with it, fast_cc_seq_modify. A block, not a function, whose counter would tick at each.
#define BUSTER_FSEQ_MODIFY(ex, v) \
    if (__builtin_expect((ex)->seq_logging == 0, 1)) \
    { \
        const uint32_t seq_plain = (v)->seq_ident; \
        (ex)->seq_mod_ids[(ex)->seq_mod_count & 3u] = seq_plain; \
        (ex)->seq_mod_count += (uint32_t)(seq_plain != 0); \
    } \
    else \
    { \
        fast_cc_seq_modify(ex, v); \
    }

static inline bool fval_holds_gp(const FastVal* v)
{
    return v->kind == FVAL_REG || v->kind == FVAL_INDIRECT ||
           (v->kind == FVAL_BITFIELD && v->bf_base == FVAL_INDIRECT);
}

static inline bool fval_holds_xmm(const FastVal* v)
{
    return v->kind == FVAL_XMM;
}

static inline void fval_release(FastExprCtx* ex, const FastVal* v)
{
    // Without code no value holds a register (#3616): the placeholders of a sizeof, _Alignof or _Generic operand, or of
    // a dead arm (#3592), say rax, and releasing one would free a live value's register.
    const uint32_t live = 0u - (uint32_t)(ex->eval_only == 0);
    if (fval_holds_gp(v)) { ex->gp_used &= (uint16_t)~((1u << v->reg) & live); }
    else if (fval_holds_xmm(v)) { ex->xmm_used &= (uint8_t)~((1u << v->reg) & live); }
}

// Spill a single live FastVal in-place to a temporary stack slot, releasing its register/flags!
static void fval_spill_one(FastExprCtx* ex, FastVal* v)
{
    if (ex->eval_only > 0) { return; }
    FastObj* obj = ex->cc->obj;
    if (v->kind == FVAL_REG)
    {
        const int32_t slot = fast_cc_alloc_tmp(ex->cc, 8u, 8u);
        x86_store(obj, FREG_RBP, slot, v->reg, 8u);
        ex->gp_used &= (uint16_t)~(1u << v->reg);
        v->kind = FVAL_SPILL;
        v->off = slot;
    }
    else if (v->kind == FVAL_XMM)
    {
        const int32_t slot = fast_cc_alloc_tmp(ex->cc, 8u, 8u);
        x86_sse_store(obj, ftype_is_f64(v->type), FREG_RBP, slot, v->reg);
        ex->xmm_used &= (uint8_t)~(1u << v->reg);
        v->kind = FVAL_SPILL;
        v->off = slot;
    }
    else if (v->kind == FVAL_INDIRECT)
    {
        const int32_t slot = fast_cc_alloc_tmp(ex->cc, 8u, 8u);
        x86_store(obj, FREG_RBP, slot, v->reg, 8u);
        ex->gp_used &= (uint16_t)~(1u << v->reg);
        v->addr_disp = v->off;
        v->off = slot;
        v->kind = FVAL_SPILL_ADDR;
    }
    else if (v->kind == FVAL_BITFIELD && v->bf_base == FVAL_INDIRECT)
    {
        const int32_t slot = fast_cc_alloc_tmp(ex->cc, 8u, 8u);
        x86_store(obj, FREG_RBP, slot, v->reg, 8u);
        ex->gp_used &= (uint16_t)~(1u << v->reg);
        v->addr_disp = v->off;
        v->off = slot;
        v->bf_base = FVAL_SPILL_ADDR;
    }
    else if (v->kind == FVAL_FLAGS)
    {
        // Materialize EFLAGS into a temp stack slot using R11 (temporarily saved if needed? No, pick any free GP or spill first)
        uint32_t tmp = FREG_RAX;
        for (uint32_t i = 0; i < 9u; i += 1)
        {
            if ((ex->gp_used & (1u << fast_gp_pool[i])) == 0) { tmp = fast_gp_pool[i]; break; }
        }
        const int32_t slot = fast_cc_alloc_tmp(ex->cc, 8u, 8u);
        x86_setcc_zext(obj, v->cc_code, tmp);
        x86_store(obj, FREG_RBP, slot, tmp, 4u);
        v->kind = FVAL_SPILL;
        v->off = slot;
        v->type = FTYPE_I32;
    }
}

// Materialize any FVAL_FLAGS currently on vstack/call_args before emitting an instruction that modifies EFLAGS!
static void fval_flush_flags(FastExprCtx* ex)
{
    if (ex->eval_only > 0) { return; }
    for (uint32_t i = 0; i < ex->vsp; i += 1)
    {
        if (ex->vstack[i].kind == FVAL_FLAGS)
        {
            // Find a free GP reg or spill
            uint32_t r = UINT32_MAX;
            for (uint32_t k = 0; k < 9u; k += 1)
            {
                if ((ex->gp_used & (1u << fast_gp_pool[k])) == 0) { r = fast_gp_pool[k]; break; }
            }
            if (r == UINT32_MAX) { fval_spill_one(ex, &ex->vstack[i]); }
            else
            {
                x86_setcc_zext(ex->cc->obj, ex->vstack[i].cc_code, r);
                ex->gp_used |= (uint16_t)(1u << r);
                ex->vstack[i].kind = FVAL_REG;
                ex->vstack[i].reg = (uint8_t)r;
                ex->vstack[i].type = FTYPE_I32;
            }
        }
    }
    for (uint32_t i = 0; i < ex->arg_sp; i += 1)
    {
        if (ex->call_args[i].kind == FVAL_FLAGS) { fval_spill_one(ex, &ex->call_args[i]); }
    }
}

// Materialize a single popped FVAL_FLAGS value into a GP register BEFORE loading another operand (which may clobber EFLAGS).
static inline void fval_materialize_flags(FastExprCtx* ex, FastVal* v)
{
    if (ex->eval_only == 0 && v->kind == FVAL_FLAGS)
    {
        uint32_t r = UINT32_MAX;
        for (uint32_t k = 0; k < 9u; k += 1)
        {
            if ((ex->gp_used & (1u << fast_gp_pool[k])) == 0) { r = fast_gp_pool[k]; break; }
        }
        if (r == UINT32_MAX)
        {
            fval_spill_one(ex, v);
        }
        else
        {
            x86_setcc_zext(ex->cc->obj, v->cc_code, r);
            ex->gp_used |= (uint16_t)(1u << r);
            v->kind = FVAL_REG;
            v->reg = (uint8_t)r;
            v->type = FTYPE_I32;
        }
    }
}

// Spill all live registers in vstack[0..v_limit) and call_args[0..a_limit)
static void fval_spill_all(FastExprCtx* ex, uint32_t v_limit, uint32_t a_limit)
{
    if (ex->eval_only > 0) { return; }
    // Flush flags first while EFLAGS is still valid!
    for (uint32_t i = 0; i < v_limit; i += 1) { if (ex->vstack[i].kind == FVAL_FLAGS) { fval_spill_one(ex, &ex->vstack[i]); } }
    for (uint32_t i = 0; i < a_limit; i += 1) { if (ex->call_args[i].kind == FVAL_FLAGS) { fval_spill_one(ex, &ex->call_args[i]); } }
    for (uint32_t i = 0; i < v_limit; i += 1) { fval_spill_one(ex, &ex->vstack[i]); }
    for (uint32_t i = 0; i < a_limit; i += 1) { fval_spill_one(ex, &ex->call_args[i]); }
}

// Allocate a free GP scratch register (spilling oldest live GP if all 9 are occupied).
// `avoid_mask` allows reserving specific regs (e.g. RAX/RDX/RCX for div/shift).
static uint32_t fval_alloc_gp_avoid(FastExprCtx* ex, uint16_t avoid_mask)
{
    if (ex->eval_only > 0) { return FREG_RAX; }
    const uint16_t busy = ex->gp_used | avoid_mask;
    for (uint32_t i = 0; i < 9u; i += 1)
    {
        const uint32_t r = fast_gp_pool[i];
        if ((busy & (1u << r)) == 0)
        {
            ex->gp_used |= (uint16_t)(1u << r);
            return r;
        }
    }
    // Spill oldest GP holder not in avoid_mask from call_args or vstack
    for (uint32_t i = 0; i < ex->arg_sp; i += 1)
    {
        if (fval_holds_gp(&ex->call_args[i]) && (avoid_mask & (1u << ex->call_args[i].reg)) == 0)
        {
            const uint32_t r = ex->call_args[i].reg;
            fval_spill_one(ex, &ex->call_args[i]);
            ex->gp_used |= (uint16_t)(1u << r);
            return r;
        }
    }
    for (uint32_t i = 0; i < ex->vsp; i += 1)
    {
        if (fval_holds_gp(&ex->vstack[i]) && (avoid_mask & (1u << ex->vstack[i].reg)) == 0)
        {
            const uint32_t r = ex->vstack[i].reg;
            fval_spill_one(ex, &ex->vstack[i]);
            ex->gp_used |= (uint16_t)(1u << r);
            return r;
        }
    }
    // Fallback: spill all and pick first non-avoided reg
    fval_spill_all(ex, ex->vsp, ex->arg_sp);
    for (uint32_t i = 0; i < 9u; i += 1)
    {
        const uint32_t r = fast_gp_pool[i];
        if ((avoid_mask & (1u << r)) == 0)
        {
            ex->gp_used |= (uint16_t)(1u << r);
            return r;
        }
    }
    return FREG_R10;
}

static inline uint32_t fval_alloc_gp(FastExprCtx* ex) { return fval_alloc_gp_avoid(ex, 0); }

static uint32_t fval_alloc_xmm(FastExprCtx* ex)
{
    if (ex->eval_only > 0) { return 0u; }
    for (uint32_t r = 0; r < 8u; r += 1)
    {
        if ((ex->xmm_used & (1u << r)) == 0)
        {
            ex->xmm_used |= (uint8_t)(1u << r);
            return r;
        }
    }
    for (uint32_t i = 0; i < ex->arg_sp; i += 1)
    {
        if (ex->call_args[i].kind == FVAL_XMM)
        {
            const uint32_t r = ex->call_args[i].reg;
            fval_spill_one(ex, &ex->call_args[i]);
            ex->xmm_used |= (uint8_t)(1u << r);
            return r;
        }
    }
    for (uint32_t i = 0; i < ex->vsp; i += 1)
    {
        if (ex->vstack[i].kind == FVAL_XMM)
        {
            const uint32_t r = ex->vstack[i].reg;
            fval_spill_one(ex, &ex->vstack[i]);
            ex->xmm_used |= (uint8_t)(1u << r);
            return r;
        }
    }
    return 0u;
}

// Compute address of an lvalue (`LOCAL`/`GLOBAL`/`INDIRECT`/`SPILL_ADDR`) into a GP register!
// If `consume` is true, frees/reuses any GP register held by `*v`.
static uint32_t fval_addr_to_gp(FastExprCtx* ex, FastVal* v, bool consume)
{
    if (ex->eval_only > 0) { return FREG_RAX; }
    FastObj* obj = ex->cc->obj;
    if (v->kind == FVAL_INDIRECT)
    {
        if (consume)
        {
            const uint32_t r = v->reg;
            if (v->off != 0) { x86_lea(obj, r, r, v->off); }
            v->kind = FVAL_VOID;
            return r;
        }
        const uint32_t r = fval_alloc_gp(ex);
        if (v->off != 0) { x86_lea(obj, r, v->reg, v->off); }
        else { x86_mov_rr(obj, true, r, v->reg); }
        return r;
    }
    if (v->kind == FVAL_LOCAL)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_lea(obj, r, FREG_RBP, v->off);
        return r;
    }
    if (v->kind == FVAL_GLOBAL)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_lea_sym(obj, r, v->sym_idx, v->imm);
        return r;
    }
    if (v->kind == FVAL_SPILL_ADDR)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_load(obj, r, FREG_RBP, v->off, 8u, false);
        if (v->addr_disp != 0) { x86_lea(obj, r, r, v->addr_disp); }
        return r;
    }
    fast_cc_bail(ex->cc, "address of non-lvalue");
    return fval_alloc_gp(ex);
}

// Load any integer/pointer/bitfield/array/func FastVal into a GP register (sign-extending 32-bit signed values
// ONLY when needed — actually loads 1/2/4/8B properly and decays arrays/funcs to addresses!).
// If `consume` is true, releases `*v`'s existing register (reusing it in-place when possible!).
static BUSTER_DIRECT_HOT uint32_t fval_to_gp(FastExprCtx* ex, FastVal* v, bool consume)
{
    if (ex->eval_only > 0) { return FREG_RAX; }
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;

    // Already-loaded register (including dereferenced function pointer `(*fp)`) returns its register directly!
    if (v->kind == FVAL_REG)
    {
        if (consume)
        {
            const uint32_t r = v->reg;
            v->kind = FVAL_VOID;
            return r;
        }
        const uint32_t r = fval_alloc_gp(ex);
        x86_mov_rr(obj, true, r, v->reg);
        return r;
    }

    if (v->kind == FVAL_SPILL && ftype_is_func(v->type))
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_load(obj, r, FREG_RBP, v->off, 8u, false);
        return r;
    }

    // Arrays and lvalue functions decay to their address!
    if (ftype_is_array(v->type) || ftype_is_func(v->type))
    {
        return fval_addr_to_gp(ex, v, consume);
    }

    const uint32_t sz = ftype_size(cc, v->type);
    const bool sgn = ftype_is_signed(cc, v->type);
    if (v->kind == FVAL_IMM)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_mov_ri(obj, sz == 8u, r, (uint64_t)v->imm);
        return r;
    }
    if (v->kind == FVAL_LOCAL_ADDR)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_lea(obj, r, FREG_RBP, v->off);
        return r;
    }
    if (v->kind == FVAL_GLOBAL_ADDR)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_lea_sym(obj, r, v->sym_idx, v->imm);
        return r;
    }
    if (v->kind == FVAL_LOCAL || v->kind == FVAL_SPILL)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_load(obj, r, FREG_RBP, v->off, sz, sgn);
        return r;
    }
    if (v->kind == FVAL_GLOBAL)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_lea_sym(obj, r, v->sym_idx, v->imm);
        x86_load(obj, r, r, 0, sz, sgn);
        return r;
    }
    if (v->kind == FVAL_INDIRECT)
    {
        uint32_t r = 0;
        if (consume)
        {
            r = v->reg;
            v->kind = FVAL_VOID;
        }
        else
        {
            r = fval_alloc_gp(ex);
        }
        x86_load(obj, r, consume ? r : v->reg, v->off, sz, sgn);
        return r;
    }
    if (v->kind == FVAL_SPILL_ADDR)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_load(obj, r, FREG_RBP, v->off, 8u, false);
        x86_load(obj, r, r, v->addr_disp, sz, sgn);
        return r;
    }
    if (v->kind == FVAL_FLAGS)
    {
        const uint32_t r = fval_alloc_gp(ex);
        x86_setcc_zext(obj, v->cc_code, r);
        if (consume) { v->kind = FVAL_VOID; }
        return r;
    }
    if (v->kind == FVAL_BITFIELD)
    {
        uint32_t r = 0;
        if (v->bf_base == FVAL_LOCAL)
        {
            r = fval_alloc_gp(ex);
            x86_load(obj, r, FREG_RBP, v->off, v->bf_unit, false);
        }
        else if (v->bf_base == FVAL_GLOBAL)
        {
            r = fval_alloc_gp(ex);
            x86_lea_sym(obj, r, v->sym_idx, v->imm);
            x86_load(obj, r, r, 0, v->bf_unit, false);
        }
        else if (v->bf_base == FVAL_INDIRECT)
        {
            r = consume ? v->reg : fval_alloc_gp(ex);
            x86_load(obj, r, v->reg, v->off, v->bf_unit, false);
            if (consume) { v->kind = FVAL_VOID; }
        }
        else // FVAL_SPILL_ADDR
        {
            r = fval_alloc_gp(ex);
            x86_load(obj, r, FREG_RBP, v->off, 8u, false);
            x86_load(obj, r, r, v->addr_disp, v->bf_unit, false);
        }
        // Extract [bf_off .. bf_off + bf_width - 1] with exact sign/zero extension!
        const bool w64 = v->bf_unit == 8u || v->bf_width > 32u || (v->bf_off + v->bf_width) > 32u;
        const uint32_t total_bits = w64 ? 64u : 32u;
        const uint32_t shl = total_bits - (v->bf_off + v->bf_width);
        const uint32_t shr = total_bits - v->bf_width;
        if (shl > 0) { x86_shift_ri(obj, w64, 4u, r, (uint8_t)shl); }
        if (shr > 0) { x86_shift_ri(obj, w64, v->bf_signed ? 7u : 5u, r, (uint8_t)shr); }
        return r;
    }
    fast_cc_bail(cc, "unexpected value kind in fval_to_gp");
    return fval_alloc_gp(ex);
}

// Load any float/double FastVal into an XMM register (0..7).
static uint32_t fval_to_xmm(FastExprCtx* ex, FastVal* v, bool consume)
{
    if (ex->eval_only > 0) { return 0u; }
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    const bool is_f64 = ftype_is_f64(v->type);

    if (v->kind == FVAL_XMM)
    {
        if (consume)
        {
            const uint32_t x = v->reg;
            v->kind = FVAL_VOID;
            return x;
        }
        const uint32_t x = fval_alloc_xmm(ex);
        x86_sse_rr(obj, is_f64, 0x10u, x, v->reg);
        return x;
    }
    if (v->kind == FVAL_FIMM)
    {
        const uint32_t x = fval_alloc_xmm(ex);
        if (v->imm == 0)
        {
            x86_sse_zero(obj, x);
        }
        else if (is_f64)
        {
            const uint32_t off = fast_cc_emit_rodata_u64(cc, (uint64_t)v->imm);
            x86_sse_load_rip(obj, true, x, obj->sec_sym_rodata, (int64_t)off);
        }
        else
        {
            const uint32_t off = fast_cc_emit_rodata_u32(cc, (uint32_t)v->imm);
            x86_sse_load_rip(obj, false, x, obj->sec_sym_rodata, (int64_t)off);
        }
        return x;
    }
    if (v->kind == FVAL_LOCAL || v->kind == FVAL_SPILL)
    {
        const uint32_t x = fval_alloc_xmm(ex);
        x86_sse_load(obj, is_f64, x, FREG_RBP, v->off);
        return x;
    }
    if (v->kind == FVAL_GLOBAL)
    {
        const uint32_t addr = fval_alloc_gp(ex);
        const uint32_t x = fval_alloc_xmm(ex);
        x86_lea_sym(obj, addr, v->sym_idx, v->imm);
        x86_sse_load(obj, is_f64, x, addr, 0);
        ex->gp_used &= (uint16_t)~(1u << addr);
        return x;
    }
    if (v->kind == FVAL_INDIRECT)
    {
        const uint32_t x = fval_alloc_xmm(ex);
        x86_sse_load(obj, is_f64, x, v->reg, v->off);
        if (consume)
        {
            ex->gp_used &= (uint16_t)~(1u << v->reg);
            v->kind = FVAL_VOID;
        }
        return x;
    }
    if (v->kind == FVAL_SPILL_ADDR)
    {
        const uint32_t addr = fval_alloc_gp(ex);
        const uint32_t x = fval_alloc_xmm(ex);
        x86_load(obj, addr, FREG_RBP, v->off, 8u, false);
        x86_sse_load(obj, is_f64, x, addr, v->addr_disp);
        ex->gp_used &= (uint16_t)~(1u << addr);
        return x;
    }
    fast_cc_bail(cc, "unexpected value kind in fval_to_xmm");
    return fval_alloc_xmm(ex);
}

// Convert FastVal `*v` in-place to `dst_type` (all scalar/pointer/float/bool C17 conversions, including exact u64<->fp!).
static BUSTER_DIRECT_HOT void fval_convert(FastExprCtx* ex, FastVal* v, FastType dst_type)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    if (ftype_is_unsupported(dst_type) || ftype_is_unsupported(v->type))
    {
        fast_cc_bail(cc, "unsupported scalar conversion");
        return;
    }
    if (ftype_is_void(dst_type))
    {
        fval_release(ex, v);
        *v = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
        return;
    }
    const FastType src_t = ftype_decay(cc, v->type);
    const FastType dst_u = ftype_unqual(dst_type);
    const FastType src_u = ftype_unqual(src_t);
    if (BUSTER_FTYPE_CODEGEN(src_u) == BUSTER_FTYPE_CODEGEN(dst_u) && !ftype_is_array(v->type) && !ftype_is_func(v->type))
    {
        v->type = dst_u;
        return;
    }
    if (ex->eval_only > 0)
    {
        v->type = dst_u;
        v->is_lvalue = 0;
        return;
    }

    const bool src_fp = ftype_is_float(src_u);
    const bool dst_fp = ftype_is_float(dst_u);
    const bool dst_bool = ftype_is_bool(dst_u);
    const bool src_128 = ftype_is_i128(src_u);
    const bool dst_128 = ftype_is_i128(dst_u);

    // 128-bit integer conversions (__int128 / __uint128_t, stored in 16B stack slots!)
    if (src_128 || dst_128)
    {
        if (src_128 && dst_128) { v->type = dst_u; return; }
        if (dst_128 && !src_fp)
        {
            const int32_t slot = fast_cc_alloc_tmp(cc, 16u, 16u);
            const uint32_t ssz = ftype_size(cc, src_u);
            const bool ssgn = ftype_is_signed(cc, src_u);
            const uint32_t lo = fval_to_gp(ex, v, true);
            if (ssz < 4u)
            {
                x86_extend(obj, lo, lo, ssz, ssgn);
                if (ssgn) { x86_extend(obj, lo, lo, 4u, true); }
            }
            else if (ssz == 4u)
            {
                x86_extend(obj, lo, lo, 4u, ssgn);
            }
            const uint32_t hi = fval_alloc_gp(ex);
            x86_store(obj, FREG_RBP, slot, lo, 8u);
            if (ssgn)
            {
                x86_mov_rr(obj, true, hi, lo);
                x86_shift_ri(obj, true, 7u, hi, 63u); // sign-extend into hi
            }
            else
            {
                x86_mov_ri(obj, false, hi, 0u);
            }
            x86_store(obj, FREG_RBP, slot + 8, hi, 8u);
            ex->gp_used &= (uint16_t)~((1u << lo) | (1u << hi));
            *v = (FastVal){ .type = dst_u, .kind = FVAL_LOCAL, .off = slot };
            return;
        }
        if (src_128 && !dst_fp)
        {
            const uint32_t a = fval_addr_to_gp(ex, v, true);
            if (dst_bool)
            {
                const uint32_t hi = fval_alloc_gp(ex);
                x86_load(obj, hi, a, 8, 8u, false);
                x86_load(obj, a, a, 0, 8u, false);
                x86_alu_rr(obj, true, 0x09u, a, hi);
                x86_setcc_zext(obj, FCC_NE, a);
                ex->gp_used &= (uint16_t)~(1u << hi);
                *v = (FastVal){ .type = FTYPE_BOOL, .kind = FVAL_REG, .reg = (uint8_t)a };
                return;
            }
            const uint32_t dsz = ftype_size(cc, dst_u);
            const bool dsgn = ftype_is_signed(cc, dst_u);
            x86_load(obj, a, a, 0, dsz, dsgn);
            *v = (FastVal){ .type = dst_u, .kind = FVAL_REG, .reg = (uint8_t)a };
            return;
        }
        fast_cc_bail(cc, "128-bit float conversion");
        return;
    }

    // Compile-time constant fold for FVAL_IMM / FVAL_FIMM!
    if (v->kind == FVAL_IMM && !dst_fp)
    {
        uint64_t x = (uint64_t)v->imm;
        const uint32_t dsz = ftype_size(cc, dst_u);
        const bool dsgn = ftype_is_signed(cc, dst_u);
        if (dst_bool) { x = x != 0 ? 1u : 0u; }
        else if (dsz == 1u) { x = dsgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
        else if (dsz == 2u) { x = dsgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
        else if (dsz == 4u) { x = dsgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
        v->imm = (int64_t)x;
        v->type = dst_u;
        return;
    }
    if (v->kind == FVAL_IMM && dst_fp)
    {
        const double d = ftype_is_signed(cc, src_u) ? (double)v->imm : (double)(uint64_t)v->imm;
        if (ftype_is_f64(dst_u))
        {
            uint64_t b = 0; memcpy(&b, &d, 8);
            v->imm = (int64_t)b;
        }
        else
        {
            const float f = (float)d;
            uint32_t b = 0; memcpy(&b, &f, 4);
            v->imm = (int64_t)(uint64_t)b;
        }
        v->kind = FVAL_FIMM;
        v->type = dst_u;
        return;
    }
    if (v->kind == FVAL_FIMM)
    {
        double d = 0.0;
        if (ftype_is_f64(src_u)) { memcpy(&d, &v->imm, 8); }
        else { float f = 0.0f; uint32_t b = (uint32_t)v->imm; memcpy(&f, &b, 4); d = (double)f; }
        if (dst_fp)
        {
            if (ftype_is_f64(dst_u)) { uint64_t b = 0; memcpy(&b, &d, 8); v->imm = (int64_t)b; }
            else { float f = (float)d; uint32_t b = 0; memcpy(&b, &f, 4); v->imm = (int64_t)(uint64_t)b; }
            v->type = dst_u;
            return;
        }
        uint64_t x = dst_bool ? (d != 0.0 ? 1u : 0u) :
                     ftype_is_signed(cc, dst_u) ? (uint64_t)(int64_t)d : (uint64_t)d;
        const uint32_t dsz = ftype_size(cc, dst_u);
        const bool dsgn = ftype_is_signed(cc, dst_u);
        if (!dst_bool && dsz == 1u) { x = dsgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
        else if (!dst_bool && dsz == 2u) { x = dsgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
        else if (!dst_bool && dsz == 4u) { x = dsgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
        v->kind = FVAL_IMM;
        v->imm = (int64_t)x;
        v->type = dst_u;
        return;
    }

    // Float <-> Float
    if (src_fp && dst_fp)
    {
        const uint32_t x = fval_to_xmm(ex, v, true);
        if (ftype_is_f64(src_u) && !ftype_is_f64(dst_u)) { x86_cvtsd2ss(obj, x, x); }
        else if (!ftype_is_f64(src_u) && ftype_is_f64(dst_u)) { x86_cvtss2sd(obj, x, x); }
        *v = (FastVal){ .type = dst_u, .kind = FVAL_XMM, .reg = (uint8_t)x };
        return;
    }

    // Int/Ptr -> Float/Double
    if (!src_fp && dst_fp)
    {
        const uint32_t g = fval_to_gp(ex, v, true);
        const uint32_t ssz = ftype_size(cc, src_u);
        const bool ssgn = ftype_is_signed(cc, src_u);
        const bool is_f64 = ftype_is_f64(dst_u);
        const uint32_t x = fval_alloc_xmm(ex);
        if (ssz < 4u) { x86_extend(obj, g, g, ssz, ssgn); }
        if (ssz == 4u && !ssgn)
        {
            // Zero-extend u32 -> u64 then signed 64-bit cvtsi2sd/ss!
            x86_extend(obj, g, g, 4u, false);
            x86_cvtsi2fp(obj, is_f64, true, x, g);
        }
        else if (ssz == 8u && !ssgn)
        {
            // Exact IEEE 754 u64 -> double/float!
            const uint32_t lbl_big = fast_label_new(obj);
            const uint32_t lbl_end = fast_label_new(obj);
            const uint32_t tmp = fval_alloc_gp(ex);
            x86_alu_rr(obj, true, 0x85u, g, g); // test g, g
            x86_jcc_label(obj, FCC_S, lbl_big);
            x86_cvtsi2fp(obj, is_f64, true, x, g);
            x86_jmp_label(obj, lbl_end);
            fast_label_bind(obj, lbl_big);
            x86_mov_rr(obj, true, tmp, g);
            x86_shift_ri(obj, true, 5u, g, 1u); // shr g, 1
            x86_alu_ri(obj, true, 4u, tmp, 1);  // and tmp, 1
            x86_alu_rr(obj, true, 0x09u, g, tmp); // or g, tmp
            x86_cvtsi2fp(obj, is_f64, true, x, g);
            x86_sse_rr(obj, is_f64, 0x58u, x, x); // addsd/ss x, x
            fast_label_bind(obj, lbl_end);
            ex->gp_used &= (uint16_t)~(1u << tmp);
        }
        else
        {
            x86_cvtsi2fp(obj, is_f64, ssz == 8u, x, g);
        }
        ex->gp_used &= (uint16_t)~(1u << g);
        *v = (FastVal){ .type = dst_u, .kind = FVAL_XMM, .reg = (uint8_t)x };
        return;
    }

    // Float/Double -> Int/Bool
    if (src_fp && !dst_fp)
    {
        const uint32_t x = fval_to_xmm(ex, v, true);
        const bool is_f64 = ftype_is_f64(src_u);
        const uint32_t g = fval_alloc_gp(ex);
        if (dst_bool)
        {
            // NaN != 0.0 -> TRUE! ucomisd x, 0.0; setne g; setp tmp; or g, tmp!
            const uint32_t xzero = fval_alloc_xmm(ex);
            const uint32_t tmp = fval_alloc_gp(ex);
            x86_sse_zero(obj, xzero);
            x86_ucomis(obj, is_f64, x, xzero);
            x86_setcc_zext(obj, FCC_NE, g);
            x86_setcc_zext(obj, FCC_P, tmp);
            x86_alu_rr(obj, false, 0x09u, g, tmp);
            ex->gp_used &= (uint16_t)~(1u << tmp);
            ex->xmm_used &= (uint8_t)~(1u << xzero);
        }
        else
        {
            const uint32_t dsz = ftype_size(cc, dst_u);
            const bool dsgn = ftype_is_signed(cc, dst_u);
            if (dsz == 8u && !dsgn)
            {
                // Exact IEEE 754 double/float -> u64!
                const uint32_t lbl_big = fast_label_new(obj);
                const uint32_t lbl_end = fast_label_new(obj);
                const uint32_t x2p63 = fval_alloc_xmm(ex);
                const uint32_t tmp = fval_alloc_gp(ex);
                const uint32_t ro = is_f64 ? fast_cc_get_f64_2p63(cc) : fast_cc_get_f32_2p63(cc);
                x86_sse_load_rip(obj, is_f64, x2p63, obj->sec_sym_rodata, (int64_t)ro);
                x86_ucomis(obj, is_f64, x, x2p63);
                x86_jcc_label(obj, FCC_AE, lbl_big);
                x86_cvttfp2si(obj, is_f64, true, g, x);
                x86_jmp_label(obj, lbl_end);
                fast_label_bind(obj, lbl_big);
                x86_sse_rr(obj, is_f64, 0x5cu, x, x2p63); // subsd/ss x, 2^63
                x86_cvttfp2si(obj, is_f64, true, g, x);
                x86_mov_ri(obj, true, tmp, UINT64_C(0x8000000000000000));
                x86_alu_rr(obj, true, 0x31u, g, tmp); // xor g, 2^63
                fast_label_bind(obj, lbl_end);
                ex->gp_used &= (uint16_t)~(1u << tmp);
                ex->xmm_used &= (uint8_t)~(1u << x2p63);
            }
            else if (dsz == 4u && !dsgn)
            {
                // float/double -> u32: convert to 64-bit signed int, then truncate to 32-bit!
                x86_cvttfp2si(obj, is_f64, true, g, x);
                x86_extend(obj, g, g, 4u, false);
            }
            else
            {
                x86_cvttfp2si(obj, is_f64, dsz == 8u, g, x);
                if (dsz < 4u) { x86_extend(obj, g, g, dsz, dsgn); }
            }
        }
        ex->xmm_used &= (uint8_t)~(1u << x);
        *v = (FastVal){ .type = dst_u, .kind = FVAL_REG, .reg = (uint8_t)g };
        return;
    }

    // Int/Ptr <-> Int/Ptr/Bool
    const uint32_t ssz = ftype_size(cc, src_u);
    const uint32_t dsz = ftype_size(cc, dst_u);
    const bool ssgn = ftype_is_signed(cc, src_u);
    const bool dsgn = ftype_is_signed(cc, dst_u);

    if (dst_bool)
    {
        if (v->kind == FVAL_FLAGS)
        {
            v->type = FTYPE_BOOL;
            return;
        }
        const uint32_t g = fval_to_gp(ex, v, true);
        if (ssz < 4u) { x86_extend(obj, g, g, ssz, false); }
        x86_alu_rr(obj, ssz == 8u, 0x85u, g, g); // test g, g
        x86_setcc_zext(obj, FCC_NE, g);
        *v = (FastVal){ .type = FTYPE_BOOL, .kind = FVAL_REG, .reg = (uint8_t)g };
        return;
    }

    // Same-width signed<->unsigned or ptr<->i64/u64 is a pure type retag if already in register, address, or memory!
    if (ssz == dsz && (v->kind == FVAL_REG || v->kind == FVAL_SPILL || v->kind == FVAL_LOCAL_ADDR || v->kind == FVAL_GLOBAL_ADDR))
    {
        if (ssz < 4u && ssgn != dsgn)
        {
            const uint32_t g = fval_to_gp(ex, v, true);
            x86_extend(obj, g, g, dsz, dsgn);
            *v = (FastVal){ .type = dst_u, .kind = FVAL_REG, .reg = (uint8_t)g };
            return;
        }
        v->type = dst_u;
        return;
    }
    // Zero-cost decay of FVAL_LOCAL / FVAL_GLOBAL array or function to pointer (0 instructions, 0 spills!)
    if ((ftype_is_array(v->type) || ftype_is_func(v->type)) && dsz == 8u)
    {
        if (v->kind == FVAL_LOCAL) { v->kind = FVAL_LOCAL_ADDR; v->type = dst_u; v->is_lvalue = 0; return; }
        if (v->kind == FVAL_GLOBAL) { v->kind = FVAL_GLOBAL_ADDR; v->type = dst_u; v->is_lvalue = 0; return; }
    }

    const uint32_t g = fval_to_gp(ex, v, true);
    if (dsz > ssz)
    {
        // Widen using source signedness (note: 1B/2B/4B loads already extended to 32-bit, but 4B->8B needs movsxd or zext!)
        if (ssz == 4u && dsz == 8u) { x86_extend(obj, g, g, 4u, ssgn); }
        else if (ssz < 4u && dsz == 8u)
        {
            x86_extend(obj, g, g, ssz, ssgn);
            if (ssgn) { x86_extend(obj, g, g, 4u, true); }
        }
    }
    else if (dsz < ssz)
    {
        // Truncate to destination width & sign/zero extend into 32-bit register
        x86_extend(obj, g, g, dsz, dsgn);
    }
    else if (dsz < 4u && ssgn != dsgn)
    {
        x86_extend(obj, g, g, dsz, dsgn);
    }
    *v = (FastVal){ .type = dst_u, .kind = FVAL_REG, .reg = (uint8_t)g };
}

// Whether an implicit conversion of a constant, or of a conditional with constant arms, to dst_t keeps the value as the
// full pipeline sees it (#3577): -Wconstant-conversion, -Wliteral-conversion (a floating constant made integral or
// out of range, made bool other than from 0 or 1), -Wimplicit-const-int-float-conversion (an integer the floating
// type doesn't hold exactly), -Wnon-literal-null-conversion (a null pointer from anything but the literal 0) and
// -Wimplicit-enum-enum-cast (an enumerator of another enum). Cold: constants alone come here.
static __attribute__((cold, noinline)) bool fast_cc_constant_conversion_quiet(FastCCState* cc, FastType dst_t, const FastVal* src)
{
    const FastType dt = ftype_unqual(dst_t);
    const FastType st = ftype_decay(cc, src->type);
    // The constant as integer bits (an integer constant, or a conditional's integer arms), or a floating value (a
    // floating constant, or a conditional's one floating arm), or floating arms with integer bits (FARMS_FLOATING).
    const FastArmsKind arms = src->kind == FVAL_IMM || src->kind == FVAL_FIMM ? FARMS_NONE : (FastArmsKind)BUSTER_FARMS_KIND(src);
    const bool unsigned64 = src->kind == FVAL_IMM && ftype_size(cc, st) == 8u && !ftype_is_signed(cc, st);
    const bool is_int = src->kind == FVAL_IMM || arms == FARMS_BITS;
    const bool is_float = src->kind == FVAL_FIMM || arms == FARMS_FLOAT;
    const uint32_t bits = src->kind == FVAL_IMM ? fast_cc_value_bits(src->imm, unsigned64) : BUSTER_FARMS_VALUE(src);
    // A floating constant's summary, or a floating arm's (BUSTER_FARMS_FLOAT_*).
    uint32_t fsummary = 0;
    if (src->kind == FVAL_FIMM)
    {
        double fvalue = 0.0;
        if (ftype_is_f64(st)) { memcpy(&fvalue, &src->imm, 8); }
        else { float f = 0.0f; const uint32_t b = (uint32_t)src->imm; memcpy(&f, &b, 4); fvalue = (double)f; }
        fsummary = fast_cc_float_summary(fvalue);
    }
    else if (arms == FARMS_FLOAT)
    {
        fsummary = BUSTER_FARMS_VALUE(src);
    }
    bool quiet = false;
    if (!is_int && !is_float && arms != FARMS_FLOATING)
    {
        quiet = false;  // a value with no constant description: nothing this check knows
    }
    else if (ftype_is_ptr(dt))
    {
        // A null pointer constant from an integer: the literal 0 alone (`1-1`, '\0', an enumerator, a cast warn).
        quiet = src->kind != FVAL_IMM || !ftype_is_int(st) || src->imm != 0 || (src->marks & BUSTER_FVAL_MARK_NONLITERAL) == 0;
        quiet = quiet && arms == FARMS_NONE;
    }
    else if (ftype_is_complex(dt))
    {
        // An enum: an enumerator of another enum is -Wimplicit-enum-enum-cast; a conditional's arms aren't tracked.
        const uint32_t cidx = dt & BUSTER_FTYPE_CIDX_MASK;
        quiet = arms == FARMS_NONE && cc->ctypes[cidx].kind == FCTYPE_ENUM && ftype_ptr_depth(dt) == 0 &&
                (src->kind != FVAL_IMM || src->sym_idx == 0 || src->sym_idx == cidx);
    }
    else if (ftype_is_bool(dt))
    {
        // An integer is quiet; a floating constant other than 0 or 1, or floating arms, are not.
        quiet = is_int || (is_float && (fsummary & BUSTER_FARMS_FLOAT_BOOL) != 0);
    }
    else if (ftype_is_float(dt))
    {
        // An integer the floating type doesn't hold exactly (its bits past the significand's); a floating constant
        // converts quietly, even out of range.
        const uint32_t significand = ftype_is_f64(dt) ? 53u : 24u;
        quiet = is_float || ((bits & 0xffu) <= significand && ((bits >> 8) & 0xffu) <= significand + 1u);
    }
    else if (ftype_is_int(dt) && !ftype_is_i128(dt) && !ftype_is_i128(st))
    {
        const uint32_t width = ftype_size(cc, dt);
        const bool dst_signed = ftype_is_signed(cc, dt);
        if (is_int)
        {
            // The source's signedness is the constant's type's, or the conditional's (to which its arms convert).
            quiet = fast_cc_int_bits_quiet(bits, width * 8u, dst_signed, ftype_is_signed(cc, st));
        }
        else if (is_float)
        {
            // A floating constant: integral and within the target's range (C's, without the unsigned leeway).
            const uint32_t log2 = (uint32_t)__builtin_ctz(width);
            quiet = (fsummary & BUSTER_FARMS_FLOAT_INT(log2, (uint32_t)dst_signed)) != 0;
        }
    }
    return quiet;
}

// Whether a constant (or a conditional's integer arms) stored into a bit-field keeps its value as the full pipeline
// sees it (#3577): fast_cc_int_bits_quiet with the field's width and signedness.
static __attribute__((cold, noinline)) bool fast_cc_bitfield_constant_quiet(FastCCState* cc, const FastVal* lhs, const FastVal* rhs)
{
    const FastType st = ftype_decay(cc, rhs->type);
    const bool unsigned64 = rhs->kind == FVAL_IMM && ftype_size(cc, st) == 8u && !ftype_is_signed(cc, st);
    bool quiet = rhs->kind == FVAL_IMM || BUSTER_FARMS_KIND(rhs) == FARMS_BITS;
    if (quiet)
    {
        const uint32_t bits = rhs->kind == FVAL_IMM ? fast_cc_value_bits(rhs->imm, unsigned64) : BUSTER_FARMS_VALUE(rhs);
        quiet = fast_cc_int_bits_quiet(bits, lhs->bf_width, lhs->bf_signed != 0, ftype_is_signed(cc, st));
    }
    return quiet;
}

static bool fast_cc_types_redeclarable(const FastCCState* cc, FastType a, FastType b);

// #3577: a conversion of SRC to its own type DST_T, a scalar's or a pointer's (whose decay is itself): the first return of
// fast_cc_check_implicit_conv below, ahead of its call, which most conversions take.
#define BUSTER_FAST_CONV_OWN(dst_t, src) \
    ((uint32_t)(BUSTER_FTYPE_UNQUAL(dst_t) == BUSTER_FTYPE_UNQUAL((src)->type)) & \
     (uint32_t)(((src)->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) != BUSTER_FTYPE_COMPLEX_BIT))

// Soundness check for implicit conversions (assignment, initialization, prototype args, return).
// Bails out cleanly to the full pipeline on C semantic errors (int<->ptr, incompatible pointers) or default warnings!
static __attribute__((noinline)) void fast_cc_check_implicit_conv(FastCCState* cc, FastType dst_t, const FastVal* src)
{
    const FastType dt = ftype_unqual(dst_t);
    const FastType st = ftype_decay(cc, src->type);
    // #3577: a conversion to the source's own type questions nothing (no value changes, no qualifier is dropped, no enum
    // or record is another), and most conversions are one: only the others take the checks below.
    if (dt == BUSTER_FTYPE_UNQUAL(st))
    {
        return;
    }
    // #3577: a constant's (or a conditional's constant arms') conversion the full pipeline questions; and one enum's
    // value into another's (-Wimplicit-enum-enum-cast). An integer constant (not an unsigned 64-bit one's bits) from
    // -128 to 127 into any integer, _Bool or floating type, and one in the int32 range into a 32- or 64-bit integer,
    // _Bool or double, converts quietly whatever the target: that test is a few instructions with no branch, and the
    // full check runs for the other constants alone.
    const uint32_t constant = (uint32_t)(src->kind == FVAL_IMM) | (uint32_t)(src->kind == FVAL_FIMM) |
                              (uint32_t)((src->marks & BUSTER_FVAL_MARK_CONST_ARMS) != 0);
    const uint32_t dkind = dt & BUSTER_FTYPE_SCALAR_MASK;
    const uint32_t dst_scalar = (uint32_t)((dt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0);
    if (constant != 0)
    {
        const uint32_t skind = src->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK);
        const uint32_t small = (uint32_t)(src->kind == FVAL_IMM) & dst_scalar & BUSTER_FAST_INT_SETTLED(src->imm, skind, dkind);
        // A pointer target from a pointer constant, or from the literal 0; a floating constant into a floating type.
        const uint32_t dst_pointer = (uint32_t)((dt & BUSTER_FTYPE_PTR_MASK) != 0);
        const uint32_t src_pointer = (uint32_t)((src->type & BUSTER_FTYPE_PTR_MASK) != 0);
        const uint32_t null_literal = (uint32_t)(src->kind == FVAL_IMM) & dst_pointer &
                                      (src_pointer | ((uint32_t)(src->imm == 0) & (uint32_t)((src->marks & BUSTER_FVAL_MARK_NONLITERAL) == 0)));
        const uint32_t floating = (uint32_t)(src->kind == FVAL_FIMM) & dst_scalar & ((uint32_t)(dkind == FTYPE_F32) | (uint32_t)(dkind == FTYPE_F64));
        if ((small | null_literal | floating) == 0 && !fast_cc_constant_conversion_quiet(cc, dst_t, src))
        {
            fast_cc_bail(cc, "constant conversion the full pipeline questions");
            return;
        }
    }
    if (((uint32_t)((dt & st & BUSTER_FTYPE_COMPLEX_BIT) != 0) & (uint32_t)(((dt | st) & BUSTER_FTYPE_PTR_MASK) == 0) & (uint32_t)(dt != st)) != 0)
    {
        if (cc->ctypes[dt & BUSTER_FTYPE_CIDX_MASK].kind == FCTYPE_ENUM || cc->ctypes[st & BUSTER_FTYPE_CIDX_MASK].kind == FCTYPE_ENUM)
        {
            fast_cc_bail(cc, "conversion between enum types");
            return;
        }
    }
    // #3577: a conversion to _Bool is a condition's (a constant `<<`, constant arms).
    if (dkind == FTYPE_BOOL && dst_scalar != 0 && !BUSTER_FAST_CONDITION_QUIET(cc, src))
    {
        fast_cc_bail(cc, "conversion to _Bool the full pipeline questions");
        return;
    }
    // #3577: a struct or union converts to its own type alone, and nothing else (void included) converts to one. (A
    // scalar this path doesn't support, a long double, converts as the full pipeline converts it; code for one isn't made.)
    const uint32_t record_or_void = (uint32_t)((dt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == BUSTER_FTYPE_COMPLEX_BIT) |
                                    (uint32_t)((st & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == BUSTER_FTYPE_COMPLEX_BIT) |
                                    (uint32_t)((dt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID) |
                                    (uint32_t)((st & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
    if (record_or_void != 0 &&
        ((BUSTER_FAST_RECORD_OR_VOID(cc, dt) | BUSTER_FAST_RECORD_OR_VOID(cc, st)) & (uint32_t)(BUSTER_FTYPE_CODEGEN(dt) != BUSTER_FTYPE_CODEGEN(BUSTER_FTYPE_UNQUAL(st)))) != 0)
    {
        fast_cc_bail(cc, "conversion to or from a struct, union or void");
        return;
    }
    const bool dp = ftype_is_ptr(dt);
    const bool sp = ftype_is_ptr(st);
    if (dp && !sp)
    {
        // Only null pointer constant (integer literal 0) converts implicitly to a pointer!
        if (!(ftype_is_int(st) && src->kind == FVAL_IMM && src->imm == 0))
        {
            fast_cc_bail(cc, "implicit int-to-ptr conversion");
        }
        return;
    }
    if (!dp && sp)
    {
        if (!ftype_is_bool(dt))
        {
            fast_cc_bail(cc, "implicit ptr-to-int conversion");
        }
        return;
    }
    if (dp && sp)
    {
        const FastType d0 = ftype_deref(cc, dt);
        const FastType s0 = ftype_deref(cc, st);
        // #3577: a conversion that drops the pointee's const or volatile (-Wincompatible-pointer-types-discards-
        // qualifiers), or that changes a deeper level's (in nested pointer types: there they must match). With masks
        // rather than branches: every pointer assignment and argument comes here.
        const uint32_t s_complex = UINT32_C(0) - (s0 >> 31);
        const uint32_t d_complex = UINT32_C(0) - (d0 >> 31);
        const uint32_t s_const = (uint32_t)((s0 & BUSTER_FTYPE_CONST) != 0);
        const uint32_t s_vol = (uint32_t)((s0 & BUSTER_FTYPE_VOL) != 0);
        const uint32_t d_const = (uint32_t)((d0 & BUSTER_FTYPE_CONST) != 0);
        const uint32_t d_vol = (uint32_t)((d0 & BUSTER_FTYPE_VOL) != 0);
        const uint32_t deep = BUSTER_FTYPE_PCONST | BUSTER_FTYPE_PVOL | BUSTER_FTYPE_QDEEP;
        // (To or from void, the deeper levels are no one's: `const char **` converts to `const void *` quietly. Of two
        // kinds, the types differ anyway, which the checks below judge.)
        const bool void_pointee = ftype_is_void(d0) || ftype_is_void(s0);
        const uint32_t nested = (uint32_t)!void_pointee & (uint32_t)(s_complex == d_complex) & (uint32_t)(((s0 ^ d0) & deep) != 0);
        if (((s_const & (d_const ^ 1u)) | (s_vol & (d_vol ^ 1u)) | nested) != 0)
        {
            fast_cc_bail(cc, "pointer conversion that changes qualifiers");
            return;
        }
        if (void_pointee)
        {
            return;
        }
        const FastType du = ftype_unqual(d0);
        const FastType su = ftype_unqual(s0);
        if (du == su)
        {
            return;
        }
        // Check scalar vs scalar pointer compatibility (must match size, signedness, floatness, and char-ness!)
        const bool dc = (du & BUSTER_FTYPE_COMPLEX_BIT) != 0;
        const bool sc = (su & BUSTER_FTYPE_COMPLEX_BIT) != 0;
        if (!dc && !sc)
        {
            const uint32_t db = du & BUSTER_FTYPE_SCALAR_MASK;
            const uint32_t sb = su & BUSTER_FTYPE_SCALAR_MASK;
            const uint32_t ddepth = ftype_ptr_depth(du);
            const uint32_t sdepth = ftype_ptr_depth(su);
            if (ddepth != sdepth || db != sb ||
                ((du & BUSTER_FTYPE_SCALAR_CHAR) != (su & BUSTER_FTYPE_SCALAR_CHAR)))
            {
                fast_cc_bail(cc, "incompatible pointer conversion");
            }
            return;
        }
        if (dc && sc)
        {
            const FastComplexType* dct = cc->ctypes + (du & BUSTER_FTYPE_CIDX_MASK);
            const FastComplexType* sct = cc->ctypes + (su & BUSTER_FTYPE_CIDX_MASK);
            if (dct->kind != sct->kind)
            {
                fast_cc_bail(cc, "incompatible complex pointer conversion");
                return;
            }
            if ((dct->kind == FCTYPE_STRUCT || dct->kind == FCTYPE_UNION) && du != su)
            {
                fast_cc_bail(cc, "incompatible struct pointer conversion");
                return;
            }
            // #3577: an enum's, an array's, a function's or a deeper pointer's type must be the other's, part by part
            // (-Wincompatible-pointer-types, -Wincompatible-function-pointer-types).
            if (du != su && !fast_cc_types_redeclarable(cc, du, su))
            {
                fast_cc_bail(cc, "incompatible pointer target conversion");
                return;
            }
            return;
        }
        fast_cc_bail(cc, "incompatible pointer target conversion");
    }
}

// #3577: a static initializer's implicit conversion to the object's type, checked as an assignment's is
// (fast_cc_check_implicit_conv: a constant's value, a pointer's target and qualifiers, an enum's identity); a string
// literal is a plain char array, as in an expression. A 128-bit scalar is one this 64-bit evaluator can't hold, and a
// bit-field's width is checked as fval_store checks a constant's.
static void fast_cc_check_static_initializer(FastCCState* cc, FastType target, const FastMemberInfo* bf, const FastStaticConst* v)
{
    const FastType plain_char = (cc->unsigned_char ? FTYPE_U8 : FTYPE_I8) | BUSTER_FTYPE_SCALAR_CHAR;
    FastVal cv = { .type = v->is_string ? ftype_make_ptr(cc, plain_char) : v->type, .kind = FVAL_GLOBAL_ADDR };
    if (!v->is_string && v->sym_idx == 0)
    {
        cv.kind = v->is_float != 0 ? FVAL_FIMM : FVAL_IMM;
        cv.imm = v->val;
        cv.sym_idx = v->enum_cidx;
        cv.marks = (uint8_t)(BUSTER_FVAL_MARK_NONLITERAL * v->folded);
    }
    fast_cc_check_implicit_conv(cc, target, &cv);
    if (!cc->bailout && ftype_size(cc, target) > 8u) { fast_cc_bail(cc, "128-bit static scalar"); }
    if (!cc->bailout && bf != NULL && bf->bit_width != 0 && v->sym_idx == 0 && !v->is_string && v->is_float == 0 &&
        !fast_cc_int_bits_quiet(fast_cc_value_bits(v->val, v->is_unsigned != 0 && v->val < 0), bf->bit_width, ftype_is_signed(cc, target),
                                v->is_unsigned == 0))
    {
        fast_cc_bail(cc, "bit-field constant conversion");
    }
}

// Store rvalue `rhs` into lvalue `lhs` (scalar, float, bitfield, or struct copy).
// Releases `lhs` if `release_lhs`, leaves converted stored rvalue in `*out_res` (if non-NULL).
static BUSTER_DIRECT_HOT void fval_store(FastExprCtx* ex, FastVal* lhs, FastVal* rhs, bool release_lhs, FastVal* out_res)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    if (ftype_is_unsupported(lhs->type) || ftype_is_unsupported(rhs->type))
    {
        fast_cc_bail(cc, "unsupported type store");
        return;
    }
    if (BUSTER_FAST_CONV_OWN(lhs->type, rhs) == 0)
    {
        fast_cc_check_implicit_conv(cc, lhs->type, rhs);
    }
    // #3577: a constant into a bit-field narrower than its value (-Wbitfield-constant-conversion, a one-bit signed
    // field's 1 too).
    if (((uint32_t)(lhs->kind == FVAL_BITFIELD) & ((uint32_t)(rhs->kind == FVAL_IMM) | (uint32_t)((rhs->marks & BUSTER_FVAL_MARK_CONST_ARMS) != 0))) != 0 &&
        !fast_cc_bitfield_constant_quiet(cc, lhs, rhs))
    {
        fast_cc_bail(cc, "bit-field constant conversion");
        return;
    }
    if (cc->bailout) { return; }
    if (ex->eval_only > 0)
    {
        if (out_res != NULL) { *out_res = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_REG }; }
        return;
    }

    // Struct / union / 128-bit integer copy assignment
    if (ftype_is_record(lhs->type) || ftype_is_i128(lhs->type))
    {
        if (ftype_is_i128(lhs->type)) { fval_convert(ex, rhs, ftype_unqual(lhs->type)); }
        const uint32_t sz = ftype_size(cc, lhs->type);
        if (sz > 64u)
        {
            // Spill outer regs because `rep movsb` uses rdi/rsi/rcx
            fval_spill_all(ex, ex->vsp, ex->arg_sp);
        }
        const uint32_t dst_a = fval_addr_to_gp(ex, lhs, release_lhs);
        const uint32_t src_a = fval_addr_to_gp(ex, rhs, true);
        const uint32_t tmp = fval_alloc_gp(ex);
        x86_emit_memcpy_inline(obj, dst_a, 0, src_a, 0, sz, tmp);
        ex->gp_used &= (uint16_t)~((1u << src_a) | (1u << tmp));
        if (out_res != NULL)
        {
            *out_res = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_INDIRECT, .reg = (uint8_t)dst_a, .off = 0 };
        }
        else
        {
            ex->gp_used &= (uint16_t)~(1u << dst_a);
        }
        return;
    }

    // Materialize popped FVAL_FLAGS before convert/bitfield operations can clobber EFLAGS.
    fval_materialize_flags(ex, rhs);

    // Convert rhs to lhs type
    fval_convert(ex, rhs, ftype_unqual(lhs->type));

    // Bitfield store (read-modify-write!)
    if (lhs->kind == FVAL_BITFIELD)
    {
        const uint32_t rv = fval_to_gp(ex, rhs, true);
        const bool w64 = lhs->bf_unit == 8u || lhs->bf_width > 32u || (lhs->bf_off + lhs->bf_width) > 32u;
        const uint64_t mask = lhs->bf_width == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << lhs->bf_width) - 1u);
        const uint32_t cur = fval_alloc_gp(ex);
        const uint32_t mreg = fval_alloc_gp(ex);
        uint32_t base_r = FREG_RBP;
        int32_t base_d = lhs->off;
        bool free_base = false;

        if (lhs->bf_base == FVAL_GLOBAL)
        {
            base_r = fval_alloc_gp(ex);
            x86_lea_sym(obj, base_r, lhs->sym_idx, lhs->imm);
            base_d = 0;
            free_base = true;
        }
        else if (lhs->bf_base == FVAL_INDIRECT)
        {
            base_r = lhs->reg;
            base_d = lhs->off;
            free_base = release_lhs;
        }
        else if (lhs->bf_base == FVAL_SPILL_ADDR)
        {
            base_r = fval_alloc_gp(ex);
            x86_load(obj, base_r, FREG_RBP, lhs->off, 8u, false);
            base_d = lhs->addr_disp;
            free_base = true;
        }

        // Load existing storage unit
        x86_load(obj, cur, base_r, base_d, lhs->bf_unit, false);
        // Clear bitfield bits in `cur`
        x86_mov_ri(obj, w64, mreg, ~(mask << lhs->bf_off));
        x86_alu_rr(obj, w64, 0x21u, cur, mreg); // and cur, ~mask
        // Mask `rv`, keep masked copy if out_res wanted, shift and OR into `cur`
        const uint32_t ins = fval_alloc_gp(ex);
        x86_mov_rr(obj, w64, ins, rv);
        x86_mov_ri(obj, w64, mreg, mask);
        x86_alu_rr(obj, w64, 0x21u, ins, mreg); // and ins, mask
        if (lhs->bf_off > 0) { x86_shift_ri(obj, w64, 4u, ins, lhs->bf_off); }
        x86_alu_rr(obj, w64, 0x09u, cur, ins); // or cur, ins
        x86_store(obj, base_r, base_d, cur, lhs->bf_unit);

        ex->gp_used &= (uint16_t)~((1u << cur) | (1u << mreg) | (1u << ins));
        if (free_base) { ex->gp_used &= (uint16_t)~(1u << base_r); }
        if (out_res != NULL)
        {
            // Return truncated/sign-extended bitfield value in `rv`
            const uint32_t tbits = w64 ? 64u : 32u;
            const uint32_t sh = tbits - lhs->bf_width;
            if (sh > 0)
            {
                x86_shift_ri(obj, w64, 4u, rv, (uint8_t)sh);
                x86_shift_ri(obj, w64, lhs->bf_signed ? 7u : 5u, rv, (uint8_t)sh);
            }
            *out_res = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_REG, .reg = (uint8_t)rv };
        }
        else
        {
            ex->gp_used &= (uint16_t)~(1u << rv);
        }
        return;
    }

    // Float / double scalar store
    if (ftype_is_float(lhs->type))
    {
        const bool is_f64 = ftype_is_f64(lhs->type);
        const uint32_t x = fval_to_xmm(ex, rhs, true);
        if (lhs->kind == FVAL_LOCAL)
        {
            x86_sse_store(obj, is_f64, FREG_RBP, lhs->off, x);
        }
        else if (lhs->kind == FVAL_GLOBAL)
        {
            const uint32_t a = fval_alloc_gp(ex);
            x86_lea_sym(obj, a, lhs->sym_idx, lhs->imm);
            x86_sse_store(obj, is_f64, a, 0, x);
            ex->gp_used &= (uint16_t)~(1u << a);
        }
        else if (lhs->kind == FVAL_INDIRECT)
        {
            x86_sse_store(obj, is_f64, lhs->reg, lhs->off, x);
            if (release_lhs) { ex->gp_used &= (uint16_t)~(1u << lhs->reg); }
        }
        else if (lhs->kind == FVAL_SPILL_ADDR)
        {
            const uint32_t a = fval_alloc_gp(ex);
            x86_load(obj, a, FREG_RBP, lhs->off, 8u, false);
            x86_sse_store(obj, is_f64, a, lhs->addr_disp, x);
            ex->gp_used &= (uint16_t)~(1u << a);
        }
        if (out_res != NULL)
        {
            *out_res = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_XMM, .reg = (uint8_t)x };
        }
        else
        {
            ex->xmm_used &= (uint8_t)~(1u << x);
        }
        return;
    }

    // Integer / pointer scalar store (1/2/4/8 bytes)
    const uint32_t sz = ftype_size(cc, lhs->type);
    // Fast path: immediate store directly to local/indirect when out_res is NULL and fits imm32!
    if (out_res == NULL && rhs->kind == FVAL_IMM &&
        (sz <= 4u || (rhs->imm >= INT32_MIN && rhs->imm <= INT32_MAX)))
    {
        if (lhs->kind == FVAL_LOCAL)
        {
            x86_store_imm(obj, FREG_RBP, lhs->off, (int32_t)rhs->imm, sz);
            return;
        }
        if (lhs->kind == FVAL_INDIRECT)
        {
            x86_store_imm(obj, lhs->reg, lhs->off, (int32_t)rhs->imm, sz);
            if (release_lhs) { ex->gp_used &= (uint16_t)~(1u << lhs->reg); }
            return;
        }
    }

    const uint32_t rv = fval_to_gp(ex, rhs, true);
    if (lhs->kind == FVAL_LOCAL)
    {
        x86_store(obj, FREG_RBP, lhs->off, rv, sz);
    }
    else if (lhs->kind == FVAL_GLOBAL)
    {
        const uint32_t a = fval_alloc_gp(ex);
        x86_lea_sym(obj, a, lhs->sym_idx, lhs->imm);
        x86_store(obj, a, 0, rv, sz);
        ex->gp_used &= (uint16_t)~(1u << a);
    }
    else if (lhs->kind == FVAL_INDIRECT)
    {
        x86_store(obj, lhs->reg, lhs->off, rv, sz);
        if (release_lhs) { ex->gp_used &= (uint16_t)~(1u << lhs->reg); }
    }
    else if (lhs->kind == FVAL_SPILL_ADDR)
    {
        const uint32_t a = fval_alloc_gp(ex);
        x86_load(obj, a, FREG_RBP, lhs->off, 8u, false);
        x86_store(obj, a, lhs->addr_disp, rv, sz);
        ex->gp_used &= (uint16_t)~(1u << a);
    }
    if (out_res != NULL)
    {
        *out_res = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_REG, .reg = (uint8_t)rv };
    }
    else
    {
        ex->gp_used &= (uint16_t)~(1u << rv);
    }
}

// Emit conditional branch on `v` (`jump_if_true=true`: jump if nonzero; `false`: jump if zero). Consumes `v`!
static void fval_branch(FastExprCtx* ex, FastVal* v, bool jump_if_true, uint32_t target_label)
{
    // #3577: a condition (and && || ?: !'s operand) the full pipeline takes quietly.
    if (!BUSTER_FAST_CONDITION_QUIET(ex->cc, v))
    {
        fast_cc_bail(ex->cc, "condition the full pipeline refuses or questions");
        return;
    }
    if (ex->eval_only > 0) { fval_release(ex, v); return; }
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    const FastType t = ftype_decay(cc, v->type);

    if (v->kind == FVAL_FLAGS)
    {
        const uint32_t cond = jump_if_true ? v->cc_code : (v->cc_code ^ 1u);
        x86_jcc_label(obj, cond, target_label);
        v->kind = FVAL_VOID;
        return;
    }
    if (v->kind == FVAL_IMM)
    {
        const bool truth = v->imm != 0;
        if (truth == jump_if_true) { x86_jmp_label(obj, target_label); }
        return;
    }
    if (v->kind == FVAL_FIMM)
    {
        double d = 0.0;
        if (ftype_is_f64(t)) { memcpy(&d, &v->imm, 8); }
        else { float f = 0.0f; uint32_t b = (uint32_t)v->imm; memcpy(&f, &b, 4); d = (double)f; }
        const bool truth = d != 0.0; // NaN != 0.0 is true!
        if (truth == jump_if_true) { x86_jmp_label(obj, target_label); }
        return;
    }
    if (ftype_is_float(t))
    {
        const bool is_f64 = ftype_is_f64(t);
        const uint32_t x = fval_to_xmm(ex, v, true);
        const uint32_t xzero = fval_alloc_xmm(ex);
        x86_sse_zero(obj, xzero);
        x86_ucomis(obj, is_f64, x, xzero);
        if (jump_if_true)
        {
            x86_jcc_label(obj, FCC_NE, target_label);
            x86_jcc_label(obj, FCC_P, target_label); // NaN is truthy!
        }
        else
        {
            const uint32_t lbl_keep = fast_label_new(obj);
            x86_jcc_label(obj, FCC_P, lbl_keep);     // NaN is NOT false!
            x86_jcc_label(obj, FCC_E, target_label);
            fast_label_bind(obj, lbl_keep);
        }
        ex->xmm_used &= (uint8_t)~((1u << x) | (1u << xzero));
        return;
    }
    const uint32_t g = fval_to_gp(ex, v, true);
    const uint32_t sz = ftype_size(cc, t);
    if (sz < 4u) { x86_extend(obj, g, g, sz, false); }
    x86_alu_rr(obj, sz == 8u, 0x85u, g, g); // test g, g
    x86_jcc_label(obj, jump_if_true ? FCC_NE : FCC_E, target_label);
    ex->gp_used &= (uint16_t)~(1u << g);
}

// Whether a _Generic (its `(` at `open`) is one whose selected association the full pipeline's checks of all of them
// come to (#3577): glibc's qualifier-generic string functions, `_Generic (0 ? (S) : (void *) 1, const void *:
// (const char *) (CALL), default: CALL)`. Two associations, a default and one pointer type (its last token `*`: not
// qualified, and compatible with no other), whose expressions are the same tokens, one of them or both behind a cast.
// A cast between pointers says nothing; anything else in an unselected association could, and is refused.
static __attribute__((cold, noinline)) bool fast_cc_generic_shape_ok(const FastCCState* cc, uint32_t open)
{
    uint32_t bounds[3][4] = { { 0 } }; // per association: type begin, type end (its ':'), expression begin, end
    uint32_t count = 0;
    bool ok = open < cc->tcount && cc->tk[open] == FTOK_LPAREN;
    uint32_t depth = 0;
    uint32_t i = open + 1u;
    bool in_ctrl = true;
    bool done = false;
    while (ok && !done && i < cc->tcount)
    {
        const uint8_t k = cc->tk[i];
        const bool top = depth == 0;
        if (k == FTOK_LPAREN || k == FTOK_LBRACKET || k == FTOK_LBRACE) { depth += 1u; }
        else if ((k == FTOK_RPAREN || k == FTOK_RBRACKET || k == FTOK_RBRACE) && !top) { depth -= 1u; }
        else if (top && k == FTOK_COLON && !in_ctrl && count != 0 && bounds[count - 1u][1] == 0)
        {
            bounds[count - 1u][1] = i;
            bounds[count - 1u][2] = i + 1u;
        }
        if (top && (k == FTOK_COMMA || k == FTOK_RPAREN))
        {
            if (!in_ctrl)
            {
                ok = bounds[count - 1u][1] != 0;
                bounds[count - 1u][3] = i;
            }
            in_ctrl = false;
            done = k == FTOK_RPAREN;
            if (!done)
            {
                ok = ok && count < 3u;
                if (ok)
                {
                    bounds[count][0] = i + 1u;
                    count += 1u;
                }
            }
        }
        i += 1u;
    }
    ok = ok && done && count == 2u;
    // One default, and one pointer type.
    uint32_t defaults = 0;
    uint32_t pointers = 0;
    uint32_t core_begin[2] = { 0, 0 };
    uint32_t core_end[2] = { 0, 0 };
    for (uint32_t a = 0; ok && a < 2u; a += 1)
    {
        const uint32_t tb = bounds[a][0];
        const uint32_t te = bounds[a][1];
        const bool is_default = te == tb + 1u && cc->tk[tb] == FTOK_KW_DEFAULT;
        defaults += is_default ? 1u : 0u;
        pointers += !is_default && te > tb && cc->tk[te - 1u] == FTOK_STAR ? 1u : 0u;
        // The expression's tokens after a leading cast `(type)`.
        uint32_t cb = bounds[a][2];
        if (cb < bounds[a][3] && cc->tk[cb] == FTOK_LPAREN && fast_cc_is_type_start(cc, cb + 1u))
        {
            uint32_t d = 0;
            uint32_t j = cb;
            do
            {
                d += cc->tk[j] == FTOK_LPAREN ? 1u : 0u;
                d -= cc->tk[j] == FTOK_RPAREN ? 1u : 0u;
                j += 1u;
            } while (d != 0 && j < bounds[a][3]);
            cb = j;
        }
        // And without parentheses around all of it.
        uint32_t ce = bounds[a][3];
        bool wrapped = true;
        while (wrapped && ce > cb + 1u && cc->tk[cb] == FTOK_LPAREN && cc->tk[ce - 1u] == FTOK_RPAREN)
        {
            uint32_t d = 0;
            uint32_t j = cb;
            do
            {
                d += cc->tk[j] == FTOK_LPAREN ? 1u : 0u;
                d -= cc->tk[j] == FTOK_RPAREN ? 1u : 0u;
                j += 1u;
            } while (d != 0 && j < ce);
            wrapped = j == ce;
            cb += wrapped ? 1u : 0u;
            ce -= wrapped ? 1u : 0u;
        }
        core_begin[a] = cb;
        core_end[a] = ce;
    }
    ok = ok && defaults == 1u && pointers == 1u;
    const uint32_t n0 = ok ? core_end[0] - core_begin[0] : 0u;
    const uint32_t n1 = ok ? core_end[1] - core_begin[1] : 1u;
    ok = ok && n0 == n1 && n0 != 0;
    for (uint32_t t = 0; ok && t < n0; t += 1)
    {
        ok = cc->tk[core_begin[0] + t] == cc->tk[core_begin[1] + t] && cc->td[core_begin[0] + t] == cc->td[core_begin[1] + t];
    }
    return ok;
}

// The pairs of operand classes (left * 4 + right, BUSTER_FAST_OPERAND_CLASS) C refuses each binary operator, by its token
// (#3577): all but arithmetic (0x0033), integers (0x0001), a pointer and an integer (0x0100; an integer and a pointer
// 0x0004) or two pointers (0x0400), as the operator allows. An operator not listed (0) is checked elsewhere.
static const uint16_t fast_binary_refused[64] = {
    [FTOK_STAR] = 0xffccu, [FTOK_SLASH] = 0xffccu,
    [FTOK_PERCENT] = 0xfffeu, [FTOK_AMP] = 0xfffeu, [FTOK_PIPE] = 0xfffeu, [FTOK_CARET] = 0xfffeu,
    [FTOK_LSHIFT] = 0xfffeu, [FTOK_RSHIFT] = 0xfffeu,
    [FTOK_PLUS] = 0xfec8u, [FTOK_MINUS] = 0xfaccu,
    [FTOK_LT] = 0xfbccu, [FTOK_GT] = 0xfbccu, [FTOK_LE] = 0xfbccu, [FTOK_GE] = 0xfbccu,
    [FTOK_EQ] = 0xfbccu, [FTOK_NE] = 0xfbccu,
};

// Whether two pointers of different types may meet in `op` as the full pipeline lets them (#3577): to compatible types
// but for their qualifiers (a function's of one signature, an array's of one element), or for == and != one to void
// and the other to an object; anything else is its error or warning (distinct pointer types, a function pointer against
// void *).
static __attribute__((noinline)) bool fast_cc_pointer_pair_valid(FastCCState* cc, uint8_t op, FastType lt, FastType rt)
{
    const FastType le = BUSTER_FTYPE_CODEGEN(ftype_unqual(ftype_deref(cc, lt)));
    const FastType re = BUSTER_FTYPE_CODEGEN(ftype_unqual(ftype_deref(cc, rt)));
    const bool equality = op == FTOK_EQ || op == FTOK_NE;
    const bool l_void = le == FTYPE_VOID;
    const bool r_void = re == FTYPE_VOID;
    return le == re || (equality && ((l_void && !ftype_is_func(re)) || (r_void && !ftype_is_func(le)))) ||
           (!l_void && !r_void && fast_cc_types_redeclarable(cc, le, re));
}

// #3577: the end of a bracketed group whose opening `(`, `[` or `{` is at POS: the token after its closing one, or the
// token count.
static uint32_t fast_cc_group_end(const FastCCState* cc, uint32_t pos)
{
    uint32_t depth = 0;
    uint32_t p = pos;
    while (p < cc->tcount)
    {
        const uint8_t t = cc->tk[p];
        depth += (uint32_t)(t == FTOK_LPAREN) | (uint32_t)(t == FTOK_LBRACKET) | (uint32_t)(t == FTOK_LBRACE);
        depth -= (uint32_t)(t == FTOK_RPAREN) | (uint32_t)(t == FTOK_RBRACKET) | (uint32_t)(t == FTOK_RBRACE);
        p += 1u;
        if (depth == 0) { break; }
    }
    return p;
}

// #3577: what tokens [START, END) fold to through the variables they name (FastSym's pad_sym[1], BUSTER_FFOLD_*), as bits:
// 1 a variable foldable to a value that may be zero, or one known not zero among other tokens; 2 the tokens are a
// variable known not zero alone, in any parentheses; 4 a variable or function that doesn't fold. A member's name isn't
// a variable.
#define BUSTER_FFOLD_SCAN_ZERO     1u
#define BUSTER_FFOLD_SCAN_NONZERO  2u
#define BUSTER_FFOLD_SCAN_VARIABLE 4u
static uint32_t fast_cc_tokens_fold(const FastCCState* cc, uint32_t start, uint32_t end)
{
    uint32_t zero = 0;
    uint32_t nonzero = 0;
    uint32_t variable = 0;
    uint32_t tokens = 0;
    for (uint32_t p = start; p < end && p < cc->tcount; p += 1u)
    {
        const uint8_t t = cc->tk[p];
        tokens += (uint32_t)(t != FTOK_LPAREN) & (uint32_t)(t != FTOK_RPAREN);
        const bool member = p > start && (cc->tk[p - 1u] == FTOK_DOT || cc->tk[p - 1u] == FTOK_ARROW);
        if (t == FTOK_IDENT && !member)
        {
            const uint32_t id = cc->td[p];
            const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
            const FastSym* sym = cc->syms + si;
            const uint32_t object = (uint32_t)(si != 0) & ((uint32_t)(sym->kind == FSYM_LOCAL) | (uint32_t)(sym->kind == FSYM_GLOBAL));
            const uint32_t fold = object != 0 ? sym->pad_sym[1] : BUSTER_FFOLD_NONE;
            zero |= (uint32_t)(fold == BUSTER_FFOLD_ZERO);
            nonzero |= (uint32_t)(fold == BUSTER_FFOLD_NONZERO);
            variable |= ((object | (uint32_t)(si != 0 && sym->kind == FSYM_FUNC)) & (uint32_t)(fold == BUSTER_FFOLD_NONE));
        }
    }
    const uint32_t lone = nonzero & (zero ^ 1u) & (uint32_t)(tokens == 1u);
    return ((zero | (nonzero & (lone ^ 1u))) * BUSTER_FFOLD_SCAN_ZERO) | (lone * BUSTER_FFOLD_SCAN_NONZERO) | (variable * BUSTER_FFOLD_SCAN_VARIABLE);
}

// #3577: whether tokens [START, END) name a variable or a function outside a sizeof or _Alignof operand (a subscript
// that does isn't a constant address). A member's name isn't one.
static bool fast_cc_tokens_name_variable(const FastCCState* cc, uint32_t start, uint32_t end)
{
    bool result = false;
    uint32_t p = start;
    while (!result && p < end && p < cc->tcount)
    {
        const uint8_t t = cc->tk[p];
        if (t == FTOK_KW_SIZEOF || t == FTOK_KW_ALIGNOF)
        {
            p = p + 1u < cc->tcount && cc->tk[p + 1u] == FTOK_LPAREN ? fast_cc_group_end(cc, p + 1u) : p + 2u;
        }
        else
        {
            const bool member = p > start && (cc->tk[p - 1u] == FTOK_DOT || cc->tk[p - 1u] == FTOK_ARROW);
            if (t == FTOK_IDENT && !member)
            {
                const uint32_t id = cc->td[p];
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                const uint8_t kind = cc->syms[si].kind;
                result = si != 0 && (kind == FSYM_LOCAL || kind == FSYM_GLOBAL || kind == FSYM_FUNC);
            }
            p += 1u;
        }
    }
    return result;
}

// #3577: whether the divisor that starts at START (with WHOLE an assignment-expression, `/=`'s and `%=`'s; else a
// cast-expression) is one the full pipeline's -Wdivision-by-zero loads as zero from a const object
// (check_common_zero_divisor): past casts, `*`, `&` and grouping parentheses, an object whose fold class may be zero,
// with members and subscripts whose indices name no variable. An operator's value, a call or a step it doesn't load.
static bool fast_cc_divisor_may_fold_zero(const FastCCState* cc, uint32_t start, bool whole)
{
    bool shaped = true;
    bool operand = false;
    bool ended = false;
    uint32_t depth = 0;
    uint32_t root = 0;
    uint32_t p = start;
    while (shaped && !ended && p < cc->tcount)
    {
        const uint8_t t = cc->tk[p];
        if (!operand)
        {
            if (t == FTOK_LPAREN && fast_cc_is_type_start(cc, p + 1u))
            {
                p = fast_cc_group_end(cc, p);
            }
            else if (t == FTOK_LPAREN)
            {
                depth += 1u;
                p += 1u;
            }
            else if (t == FTOK_STAR || t == FTOK_AMP || t == FTOK_KW_EXTENSION)
            {
                p += 1u;
            }
            else if (t == FTOK_IDENT)
            {
                root = p;
                operand = true;
                p += 1u;
            }
            else
            {
                shaped = false;
            }
        }
        else if (t == FTOK_RPAREN && depth != 0)
        {
            depth -= 1u;
            p += 1u;
        }
        else if (t == FTOK_DOT || t == FTOK_ARROW)
        {
            p += 2u;
        }
        else if (t == FTOK_LBRACKET)
        {
            const uint32_t close = fast_cc_group_end(cc, p);
            shaped = !fast_cc_tokens_name_variable(cc, p + 1u, close - 1u);
            p = close;
        }
        else if (t == FTOK_LPAREN || t == FTOK_INC || t == FTOK_DEC || depth != 0)
        {
            // A call's or a step's value, or an operator's inside the parentheses.
            shaped = false;
        }
        else
        {
            // The divisor ends: a cast-expression at any operator, an assignment-expression where the expression does.
            shaped = !whole || t == FTOK_SEMI || t == FTOK_COMMA || t == FTOK_RPAREN || t == FTOK_RBRACKET || t == FTOK_RBRACE || t == FTOK_COLON;
            ended = true;
        }
    }
    bool result = false;
    if (shaped && operand && depth == 0)
    {
        const uint32_t id = cc->td[root];
        const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
        const FastSym* sym = cc->syms + si;
        result = si != 0 && (sym->kind == FSYM_LOCAL || sym->kind == FSYM_GLOBAL) && sym->pad_sym[1] == BUSTER_FFOLD_ZERO;
    }
    return result;
}

// #3577: the fold class (BUSTER_FFOLD_*) of an object of type T with an initializer, from its integer VALUE where KNOWN:
// none unless T, or its arrays' element, is const and not volatile; else nonzero for a known integer not zero, zero
// otherwise (a floating or pointer value, or an aggregate's, may be).
static uint8_t fast_cc_fold_class(const FastCCState* cc, FastType t, bool known, uint64_t value)
{
    FastType element = t;
    while (ftype_is_array(element))
    {
        element = cc->ctypes[element & BUSTER_FTYPE_CIDX_MASK].inner;
    }
    const uint32_t qualifiers = element & BUSTER_FTYPE_CONST;
    const uint32_t volatiles = element & BUSTER_FTYPE_VOL;
    uint8_t result = BUSTER_FFOLD_NONE;
    if (qualifiers != 0 && volatiles == 0)
    {
        const bool integer = BUSTER_FAST_OPERAND_CLASS(cc, t) == BUSTER_FOPERAND_INTEGER;
        result = integer && known && value != 0 ? BUSTER_FFOLD_NONZERO : BUSTER_FFOLD_ZERO;
    }
    return result;
}

// #3577: the fold class of a static object (a global or a static local) just initialized at SEC_OFF of SEC: an integer's
// value is the section's bytes.
static uint8_t fast_cc_static_fold_class(const FastCCState* cc, FastType t, FastSecId sec, uint32_t sec_off)
{
    const uint32_t size = ftype_size(cc, t);
    uint64_t value = 0;
    const bool known = sec == FSEC_RODATA && size <= 8u && (uint64_t)sec_off + size <= cc->obj->rodata.len;
    if (known)
    {
        memcpy(&value, cc->obj->rodata.data + sec_off, size);
    }
    return fast_cc_fold_class(cc, t, known, value);
}

// #3577: the fold class of a local just initialized from tokens [START, END): an integer constant's value where the
// initializer was one; else zero where its tokens name a foldable variable, or no variable at all.
static uint8_t fast_cc_local_fold_class(const FastCCState* cc, FastType t, uint32_t start, uint32_t end)
{
    const bool integer = BUSTER_FAST_OPERAND_CLASS(cc, t) == BUSTER_FOPERAND_INTEGER;
    const bool known = integer && cc->init_kind == FVAL_IMM;
    uint8_t result = fast_cc_fold_class(cc, t, known, (uint64_t)cc->init_value);
    if (result != BUSTER_FFOLD_NONE && integer && !known)
    {
        const uint32_t fold = fast_cc_tokens_fold(cc, start, end);
        result = (fold & BUSTER_FFOLD_SCAN_NONZERO) != 0 ? BUSTER_FFOLD_NONZERO :
                 (fold & (BUSTER_FFOLD_SCAN_ZERO | BUSTER_FFOLD_SCAN_VARIABLE)) == BUSTER_FFOLD_SCAN_VARIABLE ? BUSTER_FFOLD_NONE : BUSTER_FFOLD_ZERO;
    }
    return result;
}

// #3577: an automatic object's rules, which a block's declaration and a for clause's take alike. Blocks, not functions,
// whose counters would tick at every local.
//
// The fold class of local LSI of TYPE just initialized from tokens [START, cc->pos), where it is const (or an array,
// whose element may be): a divisor naming it reads it (fast_cc_local_fold_class).
#define BUSTER_FAST_LOCAL_FOLD(cc, lsi, type, start) \
    { \
        const uint32_t lf_complex = UINT32_C(0) - ((uint32_t)(type) >> 31); \
        const uint32_t lf_foldable = (uint32_t)(((type) & BUSTER_FTYPE_CONST) != 0) | \
                                     (uint32_t)((cc)->ctypes[(type) & BUSTER_FTYPE_CIDX_MASK & lf_complex].kind == FCTYPE_ARRAY); \
        if ((lf_foldable & (uint32_t)((lsi) != 0)) != 0) \
        { \
            (cc)->syms[lsi].pad_sym[1] = fast_cc_local_fold_class((cc), (type), (start), (cc)->pos); \
        } \
    }
// RESULT: 1 where an object of TYPE left uninitialized is the full pipeline's -Wdefault-const-init-var-unsafe or
// -field-unsafe (a const object, a struct or union with a const member, an array of const elements: a multidimensional
// array's element is found by ftype_is_object_const's loop), else 0.
#define BUSTER_FAST_UNINITIALIZED_CONST(cc, type, result) \
    if (((type) & (BUSTER_FTYPE_CONST | BUSTER_FTYPE_COMPLEX_BIT)) != 0) \
    { \
        const uint32_t uc_object = (uint32_t)((uint32_t)(type) >> 31) & (uint32_t)(((type) & BUSTER_FTYPE_PTR_MASK) == 0); \
        const FastComplexType* uc_ct = (cc)->ctypes + ((type) & BUSTER_FTYPE_CIDX_MASK & (UINT32_C(0) - uc_object)); \
        const uint32_t uc_record_const = uc_object & (uint32_t)uc_ct->has_const_mbr & \
                                         ((uint32_t)(uc_ct->kind == FCTYPE_STRUCT) | (uint32_t)(uc_ct->kind == FCTYPE_UNION)); \
        const uint32_t uc_array = uc_object & (uint32_t)(uc_ct->kind == FCTYPE_ARRAY); \
        const FastType uc_element = uc_ct->inner; \
        const uint32_t uc_element_complex = BUSTER_FTYPE_COMPLEX_ALL(uc_element); \
        const uint32_t uc_element_array = (uc_element_complex & 1u) & (uint32_t)((uc_element & BUSTER_FTYPE_PTR_MASK) == 0) & \
                                          (uint32_t)((cc)->ctypes[uc_element & BUSTER_FTYPE_CIDX_MASK & uc_element_complex].kind == FCTYPE_ARRAY); \
        (result) = (uint32_t)(((type) & BUSTER_FTYPE_CONST) != 0) | uc_record_const | (uc_array & (uint32_t)((uc_element & BUSTER_FTYPE_CONST) != 0)); \
        if ((uc_array & uc_element_array) != 0) \
        { \
            (result) |= (uint32_t)ftype_is_object_const((cc), (type)); \
        } \
    }

// #3577: whether a `+`'s operands are what the full pipeline's -Wstring-plus-int or -Wstring-plus-char reports (clang's
// diagnoseStringPlusInt and diagnoseStringPlusChar): an unparenthesized string literal (op FTOK_STR_LIT) and an integer
// (an enumeration or _Bool included), or an unparenthesized character literal (op FTOK_CHAR_LIT) and a pointer to, or an
// array of, a character type. Asked only where one operand is such a literal.
static uint32_t fast_cc_string_plus(const FastCCState* cc, const FastVal* lhs, const FastVal* rhs)
{
    uint32_t result = 0;
    for (uint32_t side = 0; side < 2u; side += 1u)
    {
        const FastVal* literal = side == 0 ? lhs : rhs;
        const FastVal* other = side == 0 ? rhs : lhs;
        const FastType t = other->type;
        FastType element = FTYPE_VOID | BUSTER_FTYPE_PTR_MASK;
        if (ftype_is_array(t))
        {
            element = cc->ctypes[t & BUSTER_FTYPE_CIDX_MASK].inner;
        }
        else if (ftype_is_ptr(t))
        {
            element = ftype_deref(cc, t);
        }
        const uint32_t string_plus_int = (uint32_t)(literal->op == FTOK_STR_LIT) & (uint32_t)(BUSTER_FAST_OPERAND_CLASS(cc, t) == BUSTER_FOPERAND_INTEGER);
        const uint32_t string_plus_char = (uint32_t)(literal->op == FTOK_CHAR_LIT) & (uint32_t)ftype_is_char_type(element);
        result |= string_plus_int | string_plus_char;
    }
    return result;
}

// Emit binary operator `op` on `lhs` and `rhs`, returning result in `*out`. Consumes `lhs` and `rhs`!
// The operators whose operands compare: < > <= >= == !=, a bit each (#3577).
#define BUSTER_FAST_COMPARISON_OPS ((UINT64_C(1) << FTOK_LT) | (UINT64_C(1) << FTOK_GT) | (UINT64_C(1) << FTOK_LE) | \
                                    (UINT64_C(1) << FTOK_GE) | (UINT64_C(1) << FTOK_EQ) | (UINT64_C(1) << FTOK_NE))
// The integer scalar kinds, _Bool to the 64-bit ones and __int128's, a bit each (#3577).
#define BUSTER_FAST_INTEGER_KINDS (((1u << (FTYPE_U64 + 1u)) - (1u << FTYPE_BOOL)) | (1u << FTYPE_I128) | (1u << FTYPE_U128))
// The scalar kinds a comparison with a constant may question (#3577): narrower than int (_Bool, the chars, the shorts),
// and float, a bit each.
#define BUSTER_FAST_QUESTIONED_KINDS ((1u << FTYPE_BOOL) | (1u << FTYPE_I8) | (1u << FTYPE_U8) | (1u << FTYPE_I16) | (1u << FTYPE_U16) | \
                                      (1u << FTYPE_F32))
// #3577: a superset, in registers, of the comparisons of two integers that fast_cc_binary_operands_quiet questions (KINDS
// is the operands' value kinds, a bit each): a constant against a flag value, an address, a narrow or float-cast operand
// or a function; two constants (enumerators carry their enum); or an enum's value (an enum type before its conversion).
#define BUSTER_FAST_INT_COMPARE_ASKED(lhs, rhs, lt, rt, kinds) \
    (((((kinds) >> FVAL_IMM) | ((kinds) >> FVAL_FIMM)) & 1u & \
      ((uint32_t)(((kinds) & ((UINT32_C(1) << FVAL_FLAGS) | (UINT32_C(1) << FVAL_LOCAL_ADDR) | (UINT32_C(1) << FVAL_GLOBAL_ADDR))) != 0) | \
       ((BUSTER_FAST_QUESTIONED_KINDS >> ((lt) & BUSTER_FTYPE_SCALAR_MASK)) & 1u) | \
       ((BUSTER_FAST_QUESTIONED_KINDS >> ((rt) & BUSTER_FTYPE_SCALAR_MASK)) & 1u) | \
       (uint32_t)(((lhs)->is_func_sym | (rhs)->is_func_sym | (lhs)->lasting | (rhs)->lasting) != 0) | \
       (uint32_t)((kinds) == (UINT32_C(1) << FVAL_IMM)))) | \
     (uint32_t)((((lhs)->type | (rhs)->type) & BUSTER_FTYPE_COMPLEX_BIT) != 0))

// #3577: whether a binary operator's operands, of types LT and RT, are ones the full pipeline questions, where
// fval_emit_binary's gate let them past: a comparison of a constant out of a narrower operand's range
// (-Wtautological-constant-out-of-range-compare), of two enums' values (-Wenum-compare), of an array, a function or an
// address with a null pointer constant (-Wtautological-pointer-compare), of a float with a double constant it may not
// hold (-Wliteral-range); and under any operator, a bit-field of a 64-bit type and 32 bits or fewer, which promotes to
// int where this path keeps its type.
static __attribute__((noinline)) bool fast_cc_binary_questioned(const FastCCState* cc, uint8_t op, const FastVal* lhs, const FastVal* rhs, FastType lt, FastType rt)
{
    const uint32_t equality = (uint32_t)(op == FTOK_EQ) | (uint32_t)(op == FTOK_NE);
    // A comparison of an integer constant with a value of a narrower range (_Bool, a char, a short, a boolean
    // expression) that the constant is out of: -Wtautological-constant-out-of-range-compare. Branch-free: the side
    // that isn't constant is picked by mask. A bit-field of a 64-bit type and 32 bits or fewer promotes to int,
    // where this path keeps its type: refused under any operator.
    static const int32_t range_lo[32] = { [FTYPE_BOOL] = 0, [FTYPE_I8] = -128, [FTYPE_I16] = -32768 };
    static const int32_t range_hi[32] = { [FTYPE_BOOL] = 1, [FTYPE_I8] = 127, [FTYPE_U8] = 255, [FTYPE_I16] = 32767, [FTYPE_U16] = 65535 };
    static const uint8_t range_narrow[32] = { [FTYPE_BOOL] = 1, [FTYPE_I8] = 1, [FTYPE_U8] = 1, [FTYPE_I16] = 1, [FTYPE_U16] = 1 };
    const uint32_t comparison = (uint32_t)(op == FTOK_LT) | (uint32_t)(op == FTOK_GT) | (uint32_t)(op == FTOK_LE) | (uint32_t)(op == FTOK_GE) | equality;
    const uint32_t l_const = (uint32_t)(lhs->kind == FVAL_IMM);
    const uint32_t r_const = (uint32_t)(rhs->kind == FVAL_IMM);
    const uint32_t pick_left = l_const & (r_const ^ 1u);            // the left side is the constant
    const uint64_t left_mask = UINT64_C(0) - (uint64_t)pick_left;
    const int64_t constant = (int64_t)(((uint64_t)rhs->imm & ~left_mask) | ((uint64_t)lhs->imm & left_mask));
    const FastType other = (FastType)((lt & ~(uint32_t)left_mask) | (rt & (uint32_t)left_mask));
    const uint32_t other_flags = (uint32_t)(((uint32_t)lhs->kind & ~(uint32_t)left_mask) | ((uint32_t)rhs->kind & (uint32_t)left_mask)) == FVAL_FLAGS;
    const uint32_t other_scalar = (uint32_t)((other & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0);
    const uint32_t kind = other & BUSTER_FTYPE_SCALAR_MASK;
    const uint32_t narrow = ((uint32_t)range_narrow[kind] & other_scalar) | other_flags;
    const uint64_t flags_mask = UINT64_C(0) - (uint64_t)other_flags;
    const int64_t lo = (int64_t)((uint64_t)(int64_t)range_lo[kind] & ~flags_mask);
    const int64_t hi = (int64_t)(((uint64_t)(int64_t)range_hi[kind] & ~flags_mask) | (UINT64_C(1) & flags_mask));
    const uint32_t out_of_range = comparison & (l_const ^ r_const) & narrow & ((uint32_t)(constant < lo) | (uint32_t)(constant > hi));
    const uint32_t l_long = (uint32_t)((lt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) & (uint32_t)((lt & BUSTER_FTYPE_SCALAR_MASK) - FTYPE_I64 <= 1u);
    const uint32_t r_long = (uint32_t)((rt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) & (uint32_t)((rt & BUSTER_FTYPE_SCALAR_MASK) - FTYPE_I64 <= 1u);
    const uint32_t narrow_field = ((uint32_t)(lhs->kind == FVAL_BITFIELD) & l_long & (uint32_t)(lhs->bf_width <= 32u)) |
                                  ((uint32_t)(rhs->kind == FVAL_BITFIELD) & r_long & (uint32_t)(rhs->bf_width <= 32u));
    // -Wenum-compare: a comparison of two enums' values (an enumerator, whose enum rides in sym_idx, or a value of the
    // enum's type) of different enums. (An unnamed enum's isn't warned of; refused the same.)
    const uint32_t l_cidx = lt & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(lt) & (UINT32_C(0) - (uint32_t)((lt & BUSTER_FTYPE_PTR_MASK) == 0));
    const uint32_t r_cidx = rt & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(rt) & (UINT32_C(0) - (uint32_t)((rt & BUSTER_FTYPE_PTR_MASK) == 0));
    const uint32_t l_enum = (l_cidx & (UINT32_C(0) - (uint32_t)(cc->ctypes[l_cidx].kind == FCTYPE_ENUM))) | (lhs->sym_idx & (UINT32_C(0) - (uint32_t)(lhs->kind == FVAL_IMM)));
    const uint32_t r_enum = (r_cidx & (UINT32_C(0) - (uint32_t)(cc->ctypes[r_cidx].kind == FCTYPE_ENUM))) | (rhs->sym_idx & (UINT32_C(0) - (uint32_t)(rhs->kind == FVAL_IMM)));
    const uint32_t mixed_enums = comparison & (uint32_t)(l_enum != 0) & (uint32_t)(r_enum != 0) & (uint32_t)(l_enum != r_enum);
    // -Wtautological-pointer-compare: an array's, a function's or an address's equality with a null pointer constant.
    // (A weak function's isn't warned of; refused the same.)
    const uint32_t l_kind = cc->ctypes[lhs->type & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(lhs->type)].kind;
    const uint32_t r_kind = cc->ctypes[rhs->type & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(rhs->type)].kind;
    const uint32_t l_aggregate = (uint32_t)(lhs->type >> 31) & (uint32_t)((lhs->type & BUSTER_FTYPE_PTR_MASK) == 0) &
                                 ((uint32_t)(l_kind == FCTYPE_ARRAY) | (uint32_t)(l_kind == FCTYPE_FUNC));
    const uint32_t r_aggregate = (uint32_t)(rhs->type >> 31) & (uint32_t)((rhs->type & BUSTER_FTYPE_PTR_MASK) == 0) &
                                 ((uint32_t)(r_kind == FCTYPE_ARRAY) | (uint32_t)(r_kind == FCTYPE_FUNC));
    // (is_func_sym shares its byte with other kinds' fields: a function designator's is FVAL_GLOBAL's.)
    const uint32_t l_nonnull = l_aggregate | (uint32_t)(lhs->kind == FVAL_LOCAL_ADDR) | (uint32_t)(lhs->kind == FVAL_GLOBAL_ADDR) |
                               ((uint32_t)(lhs->kind == FVAL_GLOBAL) & (uint32_t)(lhs->is_func_sym != 0));
    const uint32_t r_nonnull = r_aggregate | (uint32_t)(rhs->kind == FVAL_LOCAL_ADDR) | (uint32_t)(rhs->kind == FVAL_GLOBAL_ADDR) |
                               ((uint32_t)(rhs->kind == FVAL_GLOBAL) & (uint32_t)(rhs->is_func_sym != 0));
    const uint32_t l_zero = (uint32_t)(lhs->kind == FVAL_IMM) & (uint32_t)(lhs->imm == 0);
    const uint32_t r_zero = (uint32_t)(rhs->kind == FVAL_IMM) & (uint32_t)(rhs->imm == 0);
    const uint32_t tautological = equality & ((l_nonnull & r_zero) | (r_nonnull & l_zero));
    // -Wliteral-range: `==` or `!=` of a float (or a cast of one to double) and a double constant, which a float may not
    // hold exactly (clang's CheckFloatComparison): asked where one is.
    const uint32_t l_float = (uint32_t)((lt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F32) | (uint32_t)(lhs->lasting & BUSTER_FVAL_LASTING_FLOAT_CAST);
    const uint32_t r_float = (uint32_t)((rt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F32) | (uint32_t)(rhs->lasting & BUSTER_FVAL_LASTING_FLOAT_CAST);
    const uint32_t l_double = (uint32_t)(lhs->kind == FVAL_FIMM) & (uint32_t)((lt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F64);
    const uint32_t r_double = (uint32_t)(rhs->kind == FVAL_FIMM) & (uint32_t)((rt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F64);
    uint32_t float_literal = equality & ((l_float & r_double) | (r_float & l_double));
    if (float_literal != 0)
    {
        const uint64_t bits = (uint64_t)(r_double != 0 ? rhs->imm : lhs->imm);
        double value = 0.0;
        memcpy(&value, &bits, sizeof(value));
        // Out of float's range (or NaN) a float can't hold it; within, exactly where float's rounding gives it back.
        const bool in_range = value >= -(double)__FLT_MAX__ && value <= (double)__FLT_MAX__;
        float_literal = !in_range || (double)(float)value != value ? 1u : 0u;
    }
    return (out_of_range | narrow_field | mixed_enums | tautological | float_literal) != 0;
}

// #3577: two lvalues at one place, their types, kinds and places (an object's frame offset, an address's addend and
// symbol, an indirect member's displacement) alike: a comparison of them may be a self-comparison, which their tokens
// decide (fast_cc_same_paths). 1 or 0, field by field (a vector load would miss the fields' recent stores).
#define BUSTER_FAST_SAME_PLACE(lhs, rhs) \
    ((uint32_t)((lhs)->type == (rhs)->type) & (uint32_t)((lhs)->kind == (rhs)->kind) & (uint32_t)((lhs)->imm == (rhs)->imm) & \
     (uint32_t)((lhs)->sym_idx == (rhs)->sym_idx) & (uint32_t)(lhs)->is_lvalue & (uint32_t)(rhs)->is_lvalue)

// #3577: a token's class in an access path as the full pipeline's check_expression_same compares one (a name, its
// members by `.` and `->`, subscripts of an integer literal or a path, and parentheses): BUSTER_FPATH_OTHER for any
// other token. fast_path_follows[a] holds b's bit where a path's token of class a may come right before one of class b,
// BUSTER_FPATH_EDGE standing for the path's start (as a) and its end (as b).
#define BUSTER_FPATH_OTHER 0u
#define BUSTER_FPATH_NAME 1u
#define BUSTER_FPATH_NUMBER 2u
#define BUSTER_FPATH_MEMBER 3u
#define BUSTER_FPATH_OPEN_SUB 4u
#define BUSTER_FPATH_CLOSE_SUB 5u
#define BUSTER_FPATH_OPEN_PAREN 6u
#define BUSTER_FPATH_CLOSE_PAREN 7u
#define BUSTER_FPATH_EDGE 8u
#define BUSTER_FPATH_AFTER_CLOSE ((1u << BUSTER_FPATH_MEMBER) | (1u << BUSTER_FPATH_OPEN_SUB) | (1u << BUSTER_FPATH_CLOSE_SUB) | \
                                  (1u << BUSTER_FPATH_CLOSE_PAREN) | (1u << BUSTER_FPATH_EDGE))

static const uint16_t fast_path_follows[9] = {
    [BUSTER_FPATH_NAME] = BUSTER_FPATH_AFTER_CLOSE,
    [BUSTER_FPATH_NUMBER] = 1u << BUSTER_FPATH_CLOSE_SUB,
    [BUSTER_FPATH_MEMBER] = 1u << BUSTER_FPATH_NAME,
    [BUSTER_FPATH_OPEN_SUB] = (1u << BUSTER_FPATH_NAME) | (1u << BUSTER_FPATH_NUMBER) | (1u << BUSTER_FPATH_OPEN_PAREN),
    [BUSTER_FPATH_CLOSE_SUB] = BUSTER_FPATH_AFTER_CLOSE,
    [BUSTER_FPATH_OPEN_PAREN] = (1u << BUSTER_FPATH_NAME) | (1u << BUSTER_FPATH_OPEN_PAREN),
    [BUSTER_FPATH_CLOSE_PAREN] = BUSTER_FPATH_AFTER_CLOSE,
    [BUSTER_FPATH_EDGE] = (1u << BUSTER_FPATH_NAME) | (1u << BUSTER_FPATH_OPEN_PAREN),
};

static uint32_t fast_path_class(uint8_t k)
{
    uint32_t cls = BUSTER_FPATH_OTHER;
    if (k == FTOK_IDENT) { cls = BUSTER_FPATH_NAME; }
    else if (k == FTOK_INT_LIT) { cls = BUSTER_FPATH_NUMBER; }
    else if (k == FTOK_DOT || k == FTOK_ARROW) { cls = BUSTER_FPATH_MEMBER; }
    else if (k == FTOK_LBRACKET) { cls = BUSTER_FPATH_OPEN_SUB; }
    else if (k == FTOK_RBRACKET) { cls = BUSTER_FPATH_CLOSE_SUB; }
    else if (k == FTOK_LPAREN) { cls = BUSTER_FPATH_OPEN_PAREN; }
    else if (k == FTOK_RPAREN) { cls = BUSTER_FPATH_CLOSE_PAREN; }
    return cls;
}

// #3577: the end (one past its last token) of the access path that starts at POS, or POS where none does: its tokens in
// an order fast_path_follows allows and its brackets matched. It ends before a token that continues it otherwise, or a
// `)` or `]` it didn't open; one that leaves a bracket open is none.
static uint32_t fast_cc_path_end(const FastCCState* cc, uint32_t pos)
{
    uint64_t subscripts = 0; // a bit a level open: 1 for `[`, 0 for `(`
    uint32_t depth = 0;
    uint32_t before = BUSTER_FPATH_EDGE;
    uint32_t i = pos;
    bool more = true;
    while (more)
    {
        const uint32_t cls = i < cc->tcount ? fast_path_class(cc->tk[i]) : BUSTER_FPATH_OTHER;
        const bool opens = cls == BUSTER_FPATH_OPEN_SUB || cls == BUSTER_FPATH_OPEN_PAREN;
        const bool closes = cls == BUSTER_FPATH_CLOSE_SUB || cls == BUSTER_FPATH_CLOSE_PAREN;
        more = cls != BUSTER_FPATH_OTHER && !(closes && depth == 0) && ((fast_path_follows[before] >> cls) & 1u) != 0 &&
               depth < 64u && !(closes && (subscripts & 1u) != (uint64_t)(cls == BUSTER_FPATH_CLOSE_SUB));
        if (more)
        {
            subscripts = opens ? (subscripts << 1) | (uint64_t)(cls == BUSTER_FPATH_OPEN_SUB) : closes ? subscripts >> 1 : subscripts;
            depth = depth + (uint32_t)opens - (uint32_t)closes;
            before = cls;
            i += 1u;
        }
    }
    const bool ok = i > pos && depth == 0 && ((fast_path_follows[before] >> BUSTER_FPATH_EDGE) & 1u) != 0;
    return ok ? i : pos;
}

// #3577: the first token of the access path that ends just before END, or END where none does (fast_cc_path_end's,
// read backward).
static uint32_t fast_cc_path_begin(const FastCCState* cc, uint32_t end)
{
    uint64_t subscripts = 0; // a bit a level open: 1 for `]`, 0 for `)`
    uint32_t depth = 0;
    uint32_t after = BUSTER_FPATH_EDGE;
    uint32_t i = end;
    bool more = true;
    while (more)
    {
        const uint32_t cls = i > 0 && i <= cc->tcount ? fast_path_class(cc->tk[i - 1u]) : BUSTER_FPATH_OTHER;
        const bool opens = cls == BUSTER_FPATH_OPEN_SUB || cls == BUSTER_FPATH_OPEN_PAREN;
        const bool closes = cls == BUSTER_FPATH_CLOSE_SUB || cls == BUSTER_FPATH_CLOSE_PAREN;
        more = cls != BUSTER_FPATH_OTHER && !(opens && depth == 0) && ((fast_path_follows[cls] >> after) & 1u) != 0 &&
               depth < 64u && !(opens && (subscripts & 1u) != (uint64_t)(cls == BUSTER_FPATH_OPEN_SUB));
        if (more)
        {
            subscripts = closes ? (subscripts << 1) | (uint64_t)(cls == BUSTER_FPATH_CLOSE_SUB) : opens ? subscripts >> 1 : subscripts;
            depth = depth + (uint32_t)closes - (uint32_t)opens;
            after = cls;
            i -= 1u;
        }
    }
    const bool ok = i < end && depth == 0 && ((fast_path_follows[BUSTER_FPATH_EDGE] >> after) & 1u) != 0;
    return ok ? i : end;
}

// #3577: whether the token K that comes right before (BEFORE) or after an operand's path makes the comparison OP's
// operand more than the path: a prefix or a cast's `)` before it, a postfix or a call's `(` after it, a multiplicative,
// additive or shift operator on either side, a relational one around an equality's operand, and one of OP's own
// precedence before it (left to right). PREVIOUS is the token before K, which tells a binary `&` (looser) from a unary one.
static bool fast_cc_path_bound_tighter(uint8_t k, uint8_t previous, uint8_t op, bool before)
{
    const bool equality = op == FTOK_EQ || op == FTOK_NE;
    const bool relational_k = k == FTOK_LT || k == FTOK_GT || k == FTOK_LE || k == FTOK_GE;
    const bool binary = k == FTOK_STAR || k == FTOK_SLASH || k == FTOK_PERCENT || k == FTOK_PLUS || k == FTOK_MINUS ||
                        k == FTOK_LSHIFT || k == FTOK_RSHIFT || k == FTOK_INC || k == FTOK_DEC;
    const bool operand_end = previous == FTOK_IDENT || previous == FTOK_INT_LIT || previous == FTOK_FLOAT_LIT || previous == FTOK_CHAR_LIT ||
                             previous == FTOK_STR_LIT || previous == FTOK_RPAREN || previous == FTOK_RBRACKET;
    const bool prefix = k == FTOK_BANG || k == FTOK_TILDE || k == FTOK_KW_SIZEOF || k == FTOK_KW_ALIGNOF || k == FTOK_RPAREN ||
                        (k == FTOK_AMP && !operand_end);
    const bool tighter = binary || (relational_k && (equality || before)) || (before && (prefix || (equality && (k == FTOK_EQ || k == FTOK_NE)))) ||
                         (!before && k == FTOK_LPAREN);
    return tighter;
}

// #3577: whether the comparison at AT (its token OP there) has one access path on both sides, parentheses aside: the
// full pipeline's self-comparison (check_expression_same), asked where both operands are lvalues at one place. Where its
// token isn't the comparison, it says so (failing closed).
static __attribute__((cold, noinline)) bool fast_cc_same_paths(const FastCCState* cc, uint8_t op, uint32_t at)
{
    bool same = at >= cc->tcount || cc->tk[at] != op;
    if (!same)
    {
        const uint32_t r_end = fast_cc_path_end(cc, at + 1u);
        const uint32_t l_begin = fast_cc_path_begin(cc, at);
        const uint8_t follower = r_end < cc->tcount ? cc->tk[r_end] : FTOK_EOF;
        const uint8_t predecessor = l_begin > 0 ? cc->tk[l_begin - 1u] : FTOK_EOF;
        const uint8_t before_predecessor = l_begin > 1u ? cc->tk[l_begin - 2u] : FTOK_EOF;
        const bool paths = r_end > at + 1u && l_begin < at && !fast_cc_path_bound_tighter(follower, FTOK_EOF, op, false) &&
                           !fast_cc_path_bound_tighter(predecessor, before_predecessor, op, true);
        // The two token runs, parentheses skipped, alike in kind and data.
        uint32_t l = l_begin;
        uint32_t r = at + 1u;
        bool alike = paths;
        while (alike && (l < at || r < r_end))
        {
            while (l < at && (cc->tk[l] == FTOK_LPAREN || cc->tk[l] == FTOK_RPAREN)) { l += 1u; }
            while (r < r_end && (cc->tk[r] == FTOK_LPAREN || cc->tk[r] == FTOK_RPAREN)) { r += 1u; }
            const bool l_more = l < at;
            const bool r_more = r < r_end;
            alike = l_more == r_more && (!l_more || (cc->tk[l] == cc->tk[r] && cc->td[l] == cc->td[r]));
            l += (uint32_t)l_more;
            r += (uint32_t)r_more;
        }
        same = alike;
    }
    return same;
}

// #3577: -Wtautological-compare's pointer bounds (the full pipeline's check_expression_bounds): a sum `p + n` outside
// parentheses on the left of `<` or `>=` (SUM_LEFT), or on the right of `>` or `<=`, whose pointer is the other side's
// access path, for any offset (the full pipeline's is unsigned alone: this fails closed). True where the other side's
// path is one of the access paths in the sum's tokens, up to the operators looser than `+`; and where the comparison's
// token isn't at AT.
static __attribute__((cold, noinline)) bool fast_cc_sum_bounds(const FastCCState* cc, uint8_t op, uint32_t at, bool sum_left)
{
    bool found = at >= cc->tcount || cc->tk[at] != op;
    // The other side's path [b_begin, b_end).
    uint32_t b_begin = at;
    uint32_t b_end = at;
    if (sum_left)
    {
        b_begin = at + 1u;
        b_end = fast_cc_path_end(cc, b_begin);
        const uint8_t follower = b_end < cc->tcount ? cc->tk[b_end] : FTOK_EOF;
        b_end = fast_cc_path_bound_tighter(follower, FTOK_EOF, op, false) ? b_begin : b_end;
    }
    else
    {
        b_begin = fast_cc_path_begin(cc, at);
        const uint8_t predecessor = b_begin > 0 ? cc->tk[b_begin - 1u] : FTOK_EOF;
        const uint8_t before_predecessor = b_begin > 1u ? cc->tk[b_begin - 2u] : FTOK_EOF;
        b_begin = fast_cc_path_bound_tighter(predecessor, before_predecessor, op, true) ? at : b_begin;
    }
    // The sum's tokens [s_begin, s_end): from the comparison to an operator looser than `+` or a bracket it didn't open.
    uint32_t s_begin = sum_left ? at : at + 1u;
    uint32_t s_end = sum_left ? at : at + 1u;
    uint32_t depth = 0;
    bool more = !found && b_end > b_begin;
    while (more)
    {
        const uint32_t i = sum_left ? s_begin - 1u : s_end;
        const bool inside = sum_left ? s_begin > 0 : s_end < cc->tcount;
        const uint8_t k = inside ? cc->tk[i] : FTOK_EOF;
        const bool opens = sum_left ? (k == FTOK_RPAREN || k == FTOK_RBRACKET) : (k == FTOK_LPAREN || k == FTOK_LBRACKET);
        const bool closes = sum_left ? (k == FTOK_LPAREN || k == FTOK_LBRACKET) : (k == FTOK_RPAREN || k == FTOK_RBRACKET);
        const bool looser = k == FTOK_EOF || k == FTOK_SEMI || k == FTOK_COMMA || k == FTOK_QUESTION || k == FTOK_COLON || k == FTOK_LBRACE ||
                            k == FTOK_RBRACE || k == FTOK_ASSIGN || k == FTOK_LAND || k == FTOK_LOR || k == FTOK_PIPE || k == FTOK_CARET ||
                            k == FTOK_AMP || k == FTOK_EQ || k == FTOK_NE || k == FTOK_LT || k == FTOK_GT || k == FTOK_LE || k == FTOK_GE ||
                            (k >= FTOK_ADD_ASSIGN && k <= FTOK_SHR_ASSIGN);
        more = inside && !(depth == 0 && (looser || closes)) && depth < 1024u;
        if (more)
        {
            depth = depth + (uint32_t)opens - (uint32_t)closes;
            s_begin -= (uint32_t)sum_left;
            s_end += (uint32_t)!sum_left;
        }
    }
    const uint32_t b_tokens = b_end - b_begin;
    for (uint32_t j = s_begin; !found && j < s_end; j += 1u)
    {
        const uint32_t e = fast_cc_path_end(cc, j);
        bool alike = e > j && e <= s_end;
        uint32_t x = j;
        uint32_t y = b_begin;
        while (alike && (x < e || y < b_end))
        {
            while (x < e && (cc->tk[x] == FTOK_LPAREN || cc->tk[x] == FTOK_RPAREN)) { x += 1u; }
            while (y < b_end && (cc->tk[y] == FTOK_LPAREN || cc->tk[y] == FTOK_RPAREN)) { y += 1u; }
            const bool x_more = x < e;
            const bool y_more = y < b_end;
            alike = x_more == y_more && (!x_more || (cc->tk[x] == cc->tk[y] && cc->td[x] == cc->td[y]));
            x += (uint32_t)x_more;
            y += (uint32_t)y_more;
        }
        found = alike && b_tokens != 0;
    }
    return found;
}

// #3577: whether a comparison is one the full pipeline's -Wtautological-compare or -Wstring-compare reports where
// evaluated: of two arrays; of a string literal (in parentheses or casts) and anything but a null pointer constant; of
// one access path with itself, not floating (asked where the operands are lvalues at one place); or of a pointer sum and
// its pointer (fast_cc_sum_bounds). AT is the comparison's token.
static __attribute__((cold, noinline)) bool fast_cc_comparison_reported(const FastCCState* cc, uint8_t op, const FastVal* lhs, const FastVal* rhs,
                                                                         FastType lt, FastType rt, uint32_t at)
{
    const bool arrays = ftype_is_array(lhs->type) && ftype_is_array(rhs->type);
    const bool l_null = lhs->kind == FVAL_IMM && lhs->imm == 0;
    const bool r_null = rhs->kind == FVAL_IMM && rhs->imm == 0;
    const bool strings = ((lhs->lasting & BUSTER_FVAL_LASTING_STRING) != 0 && !r_null) || ((rhs->lasting & BUSTER_FVAL_LASTING_STRING) != 0 && !l_null);
    const bool self = !arrays && !strings && BUSTER_FAST_SAME_PLACE(lhs, rhs) != 0 && !ftype_is_float(lt) && fast_cc_same_paths(cc, op, at);
    const bool sum_left = lhs->op == FTOK_PLUS && ftype_is_ptr(lt) && (op == FTOK_LT || op == FTOK_GE);
    const bool sum_right = rhs->op == FTOK_PLUS && ftype_is_ptr(rt) && (op == FTOK_GT || op == FTOK_LE);
    const bool bounds = !arrays && !strings && !self && (sum_left || sum_right) && fast_cc_sum_bounds(cc, op, at, sum_left);
    return arrays || strings || self || bounds;
}

// #3577: a binary operator's operands, as fval_emit_binary's gate sends them (two integer scalars that aren't compared
// or a bit-field don't come): false, with the bailout made, where the full pipeline's error or warning is.
// (Out of line: its values would take fval_emit_binary's registers, whose every operator spilled them.)
static __attribute__((noinline)) bool fast_cc_binary_operands_quiet(FastCCState* cc, uint8_t op, const FastVal* lhs, const FastVal* rhs,
                                                                   FastType lt, FastType rt, uint32_t integers, uint32_t at, uint32_t evaluated)
{
    bool quiet = true;
    // #3577: the operands C allows each operator, or the full pipeline's error or warning: * / arithmetic; % & | ^ << >>
    // integers; + arithmetic, or a pointer and an integer; - those, or two pointers; < > <= >= arithmetic or two
    // pointers; == != those, or a pointer and a literal 0. A pair of classes (left * 4 + right) is a bit of the
    // operator's mask; two pointers must be to one type (fast_cc_pointer_pair_valid).
    // Two integer scalars (no enum, pointer or record) are every operator's operands: the classes are looked up for the
    // others alone.
    if (integers == 0)
    {
        const uint32_t lc = BUSTER_FAST_OPERAND_CLASS(cc, lt);
        const uint32_t rc = BUSTER_FAST_OPERAND_CLASS(cc, rt);
        const uint32_t pair = lc * 4u + rc;
        const uint32_t equality = (uint32_t)(op == FTOK_EQ) | (uint32_t)(op == FTOK_NE);
        const uint32_t lnull = (uint32_t)(lc == BUSTER_FOPERAND_INTEGER) & (uint32_t)(lhs->kind == FVAL_IMM) & (uint32_t)(lhs->imm == 0) &
                               (uint32_t)((lhs->marks & BUSTER_FVAL_MARK_NONLITERAL) == 0);
        const uint32_t rnull = (uint32_t)(rc == BUSTER_FOPERAND_INTEGER) & (uint32_t)(rhs->kind == FVAL_IMM) & (uint32_t)(rhs->imm == 0) &
                               (uint32_t)((rhs->marks & BUSTER_FVAL_MARK_NONLITERAL) == 0);
        const uint32_t null_pair = equality & (((uint32_t)(lc == BUSTER_FOPERAND_POINTER) & rnull) | ((uint32_t)(rc == BUSTER_FOPERAND_POINTER) & lnull));
        const uint32_t refused = ((uint32_t)fast_binary_refused[op & 63u] >> pair) & 1u;
        if ((refused & (null_pair ^ 1u)) != 0)
        {
            fast_cc_bail(cc, "operands of a binary operator");
            quiet = false;
        }
        else if (pair == BUSTER_FOPERAND_POINTER * 5u && lt != rt && !fast_cc_pointer_pair_valid(cc, op, lt, rt))
        {
            fast_cc_bail(cc, "operands of distinct pointer types");
            quiet = false;
        }
    }
    // #3577: the comparisons and operands the full pipeline questions (fast_cc_binary_questioned) have a constant against
    // a narrow, flag, enum, aggregate, address or float operand, two enum values, or a bit-field: a comparison or a
    // bit-field alone looks further, at the operands' kinds and types, in registers, and the exact check runs past that.
    const uint32_t comparison = (uint32_t)(((UINT64_C(1) << (op & 63u)) & BUSTER_FAST_COMPARISON_OPS) != 0);
    const uint32_t kinds = (UINT32_C(1) << lhs->kind) | (UINT32_C(1) << rhs->kind);
    if (quiet && ((comparison | (kinds >> FVAL_BITFIELD)) & 1u) != 0)
    {
        const uint32_t constant = (uint32_t)((kinds & ((UINT32_C(1) << FVAL_IMM) | (UINT32_C(1) << FVAL_FIMM))) != 0);
        // (A scalar kind's bit, below the pointer and complex bits; a pointer's or a record's scalar bits are 0, void's.)
        const uint32_t l_scalar = (uint32_t)((lt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0);
        const uint32_t r_scalar = (uint32_t)((rt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0);
        // An array, a function or an enum before decay (not a pointer to a record, which compares quietly).
        const uint32_t l_aggregate = (uint32_t)((lhs->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == BUSTER_FTYPE_COMPLEX_BIT);
        const uint32_t r_aggregate = (uint32_t)((rhs->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == BUSTER_FTYPE_COMPLEX_BIT);
        const uint32_t special = (uint32_t)((kinds & ((UINT32_C(1) << FVAL_FLAGS) | (UINT32_C(1) << FVAL_LOCAL_ADDR) | (UINT32_C(1) << FVAL_GLOBAL_ADDR))) != 0) |
                                 (l_scalar & (BUSTER_FAST_QUESTIONED_KINDS >> (lt & BUSTER_FTYPE_SCALAR_MASK))) |
                                 (r_scalar & (BUSTER_FAST_QUESTIONED_KINDS >> (rt & BUSTER_FTYPE_SCALAR_MASK))) | l_aggregate | r_aggregate |
                                 ((uint32_t)(lhs->kind == FVAL_GLOBAL) & (uint32_t)(lhs->is_func_sym != 0)) |
                                 ((uint32_t)(rhs->kind == FVAL_GLOBAL) & (uint32_t)(rhs->is_func_sym != 0)) |
                                 (uint32_t)((lhs->lasting & BUSTER_FVAL_LASTING_FLOAT_CAST) != 0) | (uint32_t)((rhs->lasting & BUSTER_FVAL_LASTING_FLOAT_CAST) != 0);
        const uint32_t l_enum = l_aggregate | ((uint32_t)(lhs->kind == FVAL_IMM) & (uint32_t)(lhs->sym_idx != 0));
        const uint32_t r_enum = r_aggregate | ((uint32_t)(rhs->kind == FVAL_IMM) & (uint32_t)(rhs->sym_idx != 0));
        const uint32_t questioned = (comparison & ((constant & special & 1u) | (l_enum & r_enum))) | ((kinds >> FVAL_BITFIELD) & 1u);
        if (questioned != 0 && fast_cc_binary_questioned(cc, op, lhs, rhs, lt, rt))
        {
            fast_cc_bail(cc, "comparison or operand the full pipeline questions");
            quiet = false;
        }
    }
    // #3577: a comparison -Wtautological-compare or -Wstring-compare reports, where evaluated (not sizeof's operand):
    // asked of two lvalues at one place, two non-pointer aggregates, a string literal or a relational pointer sum, the
    // rest by fast_cc_comparison_reported.
    const uint32_t relational = (uint32_t)(op == FTOK_LT) | (uint32_t)(op == FTOK_GT) | (uint32_t)(op == FTOK_LE) | (uint32_t)(op == FTOK_GE);
    const uint32_t reported_asked = comparison & evaluated &
                                    (BUSTER_FAST_SAME_PLACE(lhs, rhs) |
                                     ((uint32_t)((lhs->type & rhs->type & BUSTER_FTYPE_COMPLEX_BIT) != 0) &
                                      (uint32_t)(((lhs->type | rhs->type) & BUSTER_FTYPE_PTR_MASK) == 0)) |
                                     (uint32_t)(((lhs->lasting | rhs->lasting) & BUSTER_FVAL_LASTING_STRING) != 0) |
                                     (relational & ((uint32_t)(lhs->op == FTOK_PLUS) | (uint32_t)(rhs->op == FTOK_PLUS))));
    if (quiet && reported_asked != 0 && fast_cc_comparison_reported(cc, op, lhs, rhs, lt, rt, at))
    {
        fast_cc_bail(cc, "a comparison the full pipeline's -Wtautological-compare or -Wstring-compare reports");
        quiet = false;
    }
    return quiet;
}

static BUSTER_DIRECT_HOT void fval_emit_binary(FastExprCtx* ex, uint8_t op, FastVal* lhs, FastVal* rhs, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    const FastType lt = ftype_decay(cc, lhs->type);
    const FastType rt = ftype_decay(cc, rhs->type);
    if (ftype_is_unsupported(lt) || ftype_is_unsupported(rt))
    {
        fast_cc_bail(cc, "unsupported type binary op");
        return;
    }
    // #3577: the operands C allows each operator, and the comparisons and operands the full pipeline questions: two integer
    // scalars (no enum, pointer or record) that aren't a bit-field are every operator's operands, and are questioned only
    // compared, where a superset of what fast_cc_binary_operands_quiet questions is looked at first, in registers; any
    // other pair takes it.
    const uint32_t integers = (uint32_t)(((lt | rt) & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) &
                              (BUSTER_FAST_INTEGER_KINDS >> (lt & BUSTER_FTYPE_SCALAR_MASK)) & (BUSTER_FAST_INTEGER_KINDS >> (rt & BUSTER_FTYPE_SCALAR_MASK)) & 1u;
    const uint32_t compared = (uint32_t)(((UINT64_C(1) << (op & 63u)) & BUSTER_FAST_COMPARISON_OPS) != 0);
    const uint32_t kinds = (UINT32_C(1) << lhs->kind) | (UINT32_C(1) << rhs->kind);
    uint32_t asked = (integers ^ 1u) | ((kinds >> FVAL_BITFIELD) & 1u);
    if ((asked == 0) & (compared != 0))
    {
        asked = BUSTER_FAST_INT_COMPARE_ASKED(lhs, rhs, lt, rt, kinds) | BUSTER_FAST_SAME_PLACE(lhs, rhs);
    }
    if (asked != 0)
    {
        if (!fast_cc_binary_operands_quiet(cc, op, lhs, rhs, lt, rt, integers, ex->cmp_pos, (uint32_t)(ex->unevaluated == 0)))
        {
            return;
        }
    }

    // Materialize popped FVAL_FLAGS BEFORE loading the other operand (e.g. `0 == (a >= b)` emits `xor eax,eax` for 0!).
    fval_materialize_flags(ex, rhs);
    fval_materialize_flags(ex, lhs);

    const bool lp = ftype_is_ptr(lt);
    const bool rp = ftype_is_ptr(rt);

    // Pointer arithmetic: ptr + int, int + ptr, ptr - int, ptr - ptr
    if ((op == FTOK_PLUS || op == FTOK_MINUS) && (lp || rp))
    {
        if (op == FTOK_MINUS && lp && rp)
        {
            const FastType elem = ftype_deref(cc, lt);
            if (BUSTER_FAST_TYPE_INCOMPLETE(cc, elem) != 0) { fast_cc_bail(cc, "arithmetic on a pointer to an incomplete type"); return; }
            const uint32_t esz = ftype_size(cc, elem);
            if (ex->eval_only > 0) { *out = (FastVal){ .type = FTYPE_I64, .kind = FVAL_REG }; return; }
            fval_flush_flags(ex);
            const uint32_t rl = fval_to_gp(ex, lhs, true);
            const uint32_t rr = fval_to_gp(ex, rhs, true);
            x86_alu_rr(obj, true, 0x29u, rl, rr); // sub rl, rr
            ex->gp_used &= (uint16_t)~(1u << rr);
            if (esz > 1u)
            {
                if ((esz & (esz - 1u)) == 0)
                {
                    x86_shift_ri(obj, true, 7u, rl, (uint8_t)__builtin_ctz(esz)); // sar rl, log2(esz)
                }
                else
                {
                    // Signed 64-bit idiv by esz
                    fval_spill_all(ex, ex->vsp, ex->arg_sp);
                    if (rl != FREG_RAX)
                    {
                        x86_mov_rr(obj, true, FREG_RAX, rl);
                        ex->gp_used &= (uint16_t)~(1u << rl);
                        ex->gp_used |= (uint16_t)(1u << FREG_RAX);
                    }
                    const uint32_t divr = fval_alloc_gp_avoid(ex, (1u << FREG_RAX) | (1u << FREG_RDX));
                    x86_mov_ri(obj, true, divr, esz);
                    x86_cqo(obj);
                    x86_f7(obj, true, 7u, divr); // idiv divr
                    ex->gp_used &= (uint16_t)~(1u << divr);
                    *out = (FastVal){ .type = FTYPE_I64, .kind = FVAL_REG, .reg = FREG_RAX };
                    return;
                }
            }
            *out = (FastVal){ .type = FTYPE_I64, .kind = FVAL_REG, .reg = (uint8_t)rl };
            return;
        }
        // ptr +/- int or int + ptr
        FastVal* pv = lp ? lhs : rhs;
        FastVal* iv = lp ? rhs : lhs;
        const FastType pt = lp ? lt : rt;
        const FastType elem = ftype_deref(cc, pt);
        // #3577: a pointer to an incomplete type has no step, the full pipeline's error.
        if (BUSTER_FAST_TYPE_INCOMPLETE(cc, elem) != 0) { fast_cc_bail(cc, "arithmetic on a pointer to an incomplete type"); return; }
        const uint32_t esz = ftype_size(cc, elem) != 0 ? ftype_size(cc, elem) : 1u;
        if (ex->eval_only > 0) { *out = (FastVal){ .type = ftype_unqual(pt), .kind = FVAL_REG }; return; }
        fval_flush_flags(ex);
        if (iv->kind == FVAL_IMM)
        {
            const uint32_t pr = fval_to_gp(ex, pv, true);
            const int64_t delta = (op == FTOK_PLUS ? iv->imm : -iv->imm) * (int64_t)esz;
            if (delta != 0)
            {
                if (delta >= INT32_MIN && delta <= INT32_MAX) { x86_lea(obj, pr, pr, (int32_t)delta); }
                else
                {
                    const uint32_t tr = fval_alloc_gp(ex);
                    x86_mov_ri(obj, true, tr, (uint64_t)delta);
                    x86_alu_rr(obj, true, 0x01u, pr, tr);
                    ex->gp_used &= (uint16_t)~(1u << tr);
                }
            }
            *out = (FastVal){ .type = ftype_unqual(pt), .kind = FVAL_REG, .reg = (uint8_t)pr };
            return;
        }
        fval_convert(ex, iv, FTYPE_I64);
        const uint32_t pr = fval_to_gp(ex, pv, true);
        const uint32_t ir = fval_to_gp(ex, iv, true);
        if (op == FTOK_PLUS && (esz == 1u || esz == 2u || esz == 4u || esz == 8u))
        {
            x86_lea_sib(obj, pr, pr, ir, (uint32_t)__builtin_ctz(esz), 0);
        }
        else
        {
            if (esz > 1u)
            {
                if ((esz & (esz - 1u)) == 0) { x86_shift_ri(obj, true, 4u, ir, (uint8_t)__builtin_ctz(esz)); }
                else { x86_imul_rri(obj, true, ir, ir, (int32_t)esz); }
            }
            x86_alu_rr(obj, true, op == FTOK_PLUS ? 0x01u : 0x29u, pr, ir);
        }
        ex->gp_used &= (uint16_t)~(1u << ir);
        *out = (FastVal){ .type = ftype_unqual(pt), .kind = FVAL_REG, .reg = (uint8_t)pr };
        return;
    }

    // Comparisons
    const bool is_cmp = (op == FTOK_EQ || op == FTOK_NE || op == FTOK_LT || op == FTOK_LE || op == FTOK_GT || op == FTOK_GE);
    if (is_cmp)
    {
        if (ex->eval_only > 0) { *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG }; return; }
        fval_flush_flags(ex);
        FastType ct = (lp || rp) ? FTYPE_U64 : ftype_usual_arith(cc, lt, rt);
        if (ftype_is_float(ct))
        {
            fval_convert(ex, lhs, ct);
            fval_convert(ex, rhs, ct);
            const bool is_f64 = ftype_is_f64(ct);
            const uint32_t xl = fval_to_xmm(ex, lhs, true);
            const uint32_t xr = fval_to_xmm(ex, rhs, true);
            if (op == FTOK_GT || op == FTOK_GE)
            {
                x86_ucomis(obj, is_f64, xl, xr);
                ex->xmm_used &= (uint8_t)~((1u << xl) | (1u << xr));
                *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_FLAGS, .cc_code = op == FTOK_GT ? FCC_A : FCC_AE };
                return;
            }
            if (op == FTOK_LT || op == FTOK_LE)
            {
                // Swap operands so NaN (CF=1) yields FALSE in 1 condition code!
                x86_ucomis(obj, is_f64, xr, xl);
                ex->xmm_used &= (uint8_t)~((1u << xl) | (1u << xr));
                *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_FLAGS, .cc_code = op == FTOK_LT ? FCC_A : FCC_AE };
                return;
            }
            // == or != with exact IEEE NaN parity check!
            x86_ucomis(obj, is_f64, xl, xr);
            ex->xmm_used &= (uint8_t)~((1u << xl) | (1u << xr));
            const uint32_t r1 = fval_alloc_gp(ex);
            const uint32_t r2 = fval_alloc_gp(ex);
            if (op == FTOK_EQ)
            {
                x86_setcc_zext(obj, FCC_E, r1);
                x86_setcc_zext(obj, FCC_NP, r2);
                x86_alu_rr(obj, false, 0x21u, r1, r2); // and
            }
            else
            {
                x86_setcc_zext(obj, FCC_NE, r1);
                x86_setcc_zext(obj, FCC_P, r2);
                x86_alu_rr(obj, false, 0x09u, r1, r2); // or
            }
            ex->gp_used &= (uint16_t)~(1u << r2);
            *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = (uint8_t)r1 };
            return;
        }
        // Integer / pointer comparison
        fval_convert(ex, lhs, ct);
        fval_convert(ex, rhs, ct);
        const bool w64 = ftype_size(cc, ct) == 8u;
        const bool sgn = ftype_is_signed(cc, ct);
        if (lhs->kind == FVAL_IMM && rhs->kind == FVAL_IMM)
        {
            const uint64_t a = w64 ? (uint64_t)lhs->imm : (uint32_t)lhs->imm;
            const uint64_t b = w64 ? (uint64_t)rhs->imm : (uint32_t)rhs->imm;
            const int64_t sa = w64 ? lhs->imm : (int64_t)(int32_t)lhs->imm;
            const int64_t sb = w64 ? rhs->imm : (int64_t)(int32_t)rhs->imm;
            bool res = false;
            if (op == FTOK_EQ) res = (a == b);
            else if (op == FTOK_NE) res = (a != b);
            else if (op == FTOK_LT) res = sgn ? (sa < sb) : (a < b);
            else if (op == FTOK_LE) res = sgn ? (sa <= sb) : (a <= b);
            else if (op == FTOK_GT) res = sgn ? (sa > sb) : (a > b);
            else res = sgn ? (sa >= sb) : (a >= b);
            *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = res ? 1 : 0 };
            return;
        }
        const uint32_t rl = fval_to_gp(ex, lhs, true);
        if (rhs->kind == FVAL_IMM && rhs->imm >= INT32_MIN && rhs->imm <= INT32_MAX)
        {
            if (rhs->imm == 0 && (op == FTOK_EQ || op == FTOK_NE)) { x86_alu_rr(obj, w64, 0x85u, rl, rl); }
            else { x86_alu_ri(obj, w64, 7u, rl, (int32_t)rhs->imm); } // cmp rl, imm
        }
        else
        {
            const uint32_t rr = fval_to_gp(ex, rhs, true);
            x86_alu_rr(obj, w64, 0x39u, rl, rr); // cmp rl, rr
            ex->gp_used &= (uint16_t)~(1u << rr);
        }
        ex->gp_used &= (uint16_t)~(1u << rl);
        uint8_t cc_code = FCC_E;
        if (op == FTOK_EQ) cc_code = FCC_E;
        else if (op == FTOK_NE) cc_code = FCC_NE;
        else if (op == FTOK_LT) cc_code = sgn ? FCC_L : FCC_B;
        else if (op == FTOK_LE) cc_code = sgn ? FCC_LE : FCC_BE;
        else if (op == FTOK_GT) cc_code = sgn ? FCC_G : FCC_A;
        else cc_code = sgn ? FCC_GE : FCC_AE;
        *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_FLAGS, .cc_code = cc_code };
        return;
    }

    // Shifts `<<` and `>>` (result type is promoted LHS type, NOT usual arithmetic conversion!)
    if (op == FTOK_LSHIFT || op == FTOK_RSHIFT)
    {
        const FastType res_t = ftype_promote_int(cc, lt);
        // #3577: a constant count out of the promoted type's width, or negative, is the full pipeline's warning in an
        // unevaluated operand too (a 128-bit type's width is 128); a bit-field of a 64-bit type and 32 bits or fewer
        // promotes to int there, where this path keeps its type.
        {
            const uint32_t width = ftype_is_i128(res_t) ? 128u : ftype_size(cc, res_t) * 8u;
            const uint32_t count_bad = (uint32_t)(rhs->kind == FVAL_IMM) & ((uint32_t)(rhs->imm < 0) | (uint32_t)((uint64_t)rhs->imm >= width));
            if (count_bad != 0)
            {
                fast_cc_bail(cc, "shift count out of range");
                return;
            }
        }
        if (ex->eval_only > 0) { *out = (FastVal){ .type = res_t, .kind = FVAL_REG }; return; }
        fval_flush_flags(ex);
        if (ftype_is_i128(res_t))
        {
            if (rhs->kind != FVAL_IMM) { fast_cc_bail(cc, "non-const 128-bit shift"); return; }
            const uint32_t sh = (uint32_t)rhs->imm & 127u;
            const bool sgn = ftype_is_signed(cc, res_t);
            const int32_t slot = fast_cc_alloc_tmp(cc, 16u, 16u);
            const uint32_t a = fval_addr_to_gp(ex, lhs, true);
            const uint32_t lo = fval_alloc_gp(ex);
            const uint32_t hi = fval_alloc_gp(ex);
            x86_load(obj, lo, a, 0, 8u, false);
            x86_load(obj, hi, a, 8, 8u, false);
            ex->gp_used &= (uint16_t)~(1u << a);
            if (op == FTOK_RSHIFT)
            {
                if (sh >= 64u)
                {
                    x86_mov_rr(obj, true, lo, hi);
                    if (sh > 64u) { x86_shift_ri(obj, true, sgn ? 7u : 5u, lo, (uint8_t)(sh - 64u)); }
                    if (sgn) { x86_shift_ri(obj, true, 7u, hi, 63u); }
                    else { x86_mov_ri(obj, false, hi, 0u); }
                }
                else if (sh > 0u)
                {
                    x86_shrd_ri(obj, lo, hi, (uint8_t)sh);
                    x86_shift_ri(obj, true, sgn ? 7u : 5u, hi, (uint8_t)sh);
                }
            }
            else
            {
                if (sh >= 64u)
                {
                    x86_mov_rr(obj, true, hi, lo);
                    if (sh > 64u) { x86_shift_ri(obj, true, 4u, hi, (uint8_t)(sh - 64u)); }
                    x86_mov_ri(obj, false, lo, 0u);
                }
                else if (sh > 0u)
                {
                    x86_shld_ri(obj, hi, lo, (uint8_t)sh);
                    x86_shift_ri(obj, true, 4u, lo, (uint8_t)sh);
                }
            }
            x86_store(obj, FREG_RBP, slot, lo, 8u);
            x86_store(obj, FREG_RBP, slot + 8, hi, 8u);
            ex->gp_used &= (uint16_t)~((1u << lo) | (1u << hi));
            *out = (FastVal){ .type = res_t, .kind = FVAL_LOCAL, .off = slot };
            return;
        }
        fval_convert(ex, lhs, res_t);
        const bool w64 = ftype_size(cc, res_t) == 8u;
        const bool sgn = ftype_is_signed(cc, res_t);
        const uint8_t ext = op == FTOK_LSHIFT ? 4u : (sgn ? 7u : 5u);
        if (!cc->quiet && rhs->kind == FVAL_IMM && (rhs->imm < 0 || rhs->imm >= (w64 ? 64 : 32)))
        {
            fast_cc_bail(cc, "shift count out of range");
            return;
        }
        // #3577: a negative signed constant shifted left is -Wshift-negative-value, whatever the count.
        if (op == FTOK_LSHIFT && sgn && lhs->kind == FVAL_IMM && (w64 ? lhs->imm < 0 : (int32_t)lhs->imm < 0))
        {
            fast_cc_bail(cc, "negative value shifted left");
            return;
        }
        if (lhs->kind == FVAL_IMM && rhs->kind == FVAL_IMM)
        {
            const uint32_t sh = (uint32_t)rhs->imm & (w64 ? 63u : 31u);
            // #3577: a signed left shift whose bits leave the type is -Wshift-overflow (the sign bit alone is quiet).
            const uint32_t type_bits = w64 ? 64u : 32u;
            const uint64_t shifted_out = sh == 0 ? 0u : (w64 ? (uint64_t)lhs->imm : (uint32_t)lhs->imm) >> (type_bits - sh);
            if (op == FTOK_LSHIFT && sgn && shifted_out != 0)
            {
                fast_cc_bail(cc, "signed shift overflow");
                return;
            }
            uint64_t v = w64 ? (uint64_t)lhs->imm : (uint32_t)lhs->imm;
            if (op == FTOK_LSHIFT) { v = w64 ? (v << sh) : (uint32_t)(v << sh); }
            else if (sgn) { v = w64 ? (uint64_t)((int64_t)v >> sh) : (uint64_t)(int64_t)((int32_t)v >> sh); }
            else { v = w64 ? (v >> sh) : ((uint32_t)v >> sh); }
            *out = (FastVal){ .type = res_t, .kind = FVAL_IMM, .imm = (int64_t)v,
                              .marks = (uint8_t)(BUSTER_FVAL_MARK_NONLITERAL | (BUSTER_FVAL_MARK_SHIFTED * (uint32_t)(op == FTOK_LSHIFT))) };
            return;
        }
        if (rhs->kind == FVAL_IMM)
        {
            const uint32_t rl = fval_to_gp(ex, lhs, true);
            const uint8_t sh = (uint8_t)((uint32_t)rhs->imm & (w64 ? 63u : 31u));
            if (sh > 0) { x86_shift_ri(obj, w64, ext, rl, sh); }
            *out = (FastVal){ .type = res_t, .kind = FVAL_REG, .reg = (uint8_t)rl };
            return;
        }
        // Variable shift: rhs MUST be in CL (FREG_RCX), lhs MUST NOT be in RCX!
        fval_convert(ex, rhs, FTYPE_I32);
        // Make sure RCX is free if neither operand is in RCX, or handle safely via spill!
        const uint32_t rl = fval_to_gp(ex, lhs, true);
        uint32_t target = rl;
        if (target == FREG_RCX)
        {
            target = fval_alloc_gp_avoid(ex, (1u << FREG_RCX));
            x86_mov_rr(obj, true, target, FREG_RCX);
            ex->gp_used &= (uint16_t)~(1u << FREG_RCX);
        }
        // Now spill whoever else might hold RCX (except rhs!)
        if ((ex->gp_used & (1u << FREG_RCX)) != 0 && !(fval_holds_gp(rhs) && rhs->reg == FREG_RCX))
        {
            for (uint32_t i = 0; i < ex->vsp; i += 1) { if (fval_holds_gp(&ex->vstack[i]) && ex->vstack[i].reg == FREG_RCX) { fval_spill_one(ex, &ex->vstack[i]); } }
            for (uint32_t i = 0; i < ex->arg_sp; i += 1) { if (fval_holds_gp(&ex->call_args[i]) && ex->call_args[i].reg == FREG_RCX) { fval_spill_one(ex, &ex->call_args[i]); } }
        }
        const uint32_t rr = fval_to_gp(ex, rhs, true);
        if (rr != FREG_RCX) { x86_mov_rr(obj, false, FREG_RCX, rr); }
        ex->gp_used &= (uint16_t)~(1u << rr);
        x86_shift_cl(obj, w64, ext, target);
        *out = (FastVal){ .type = res_t, .kind = FVAL_REG, .reg = (uint8_t)target };
        return;
    }

    // Standard arithmetic / bitwise ops
    const FastType ct = ftype_usual_arith(cc, lt, rt);
    if (ex->eval_only > 0) { *out = (FastVal){ .type = ct, .kind = FVAL_REG }; return; }
    fval_flush_flags(ex);
    fval_convert(ex, lhs, ct);
    fval_convert(ex, rhs, ct);

    // 128-bit integer arithmetic (+, -, *, &, |, ^)!
    if (ftype_is_i128(ct))
    {
        fval_spill_all(ex, ex->vsp, ex->arg_sp);
        const int32_t slot = fast_cc_alloc_tmp(cc, 16u, 16u);
        const uint32_t la = fval_addr_to_gp(ex, lhs, true);
        if (la != FREG_R8) { x86_mov_rr(obj, true, FREG_R8, la); ex->gp_used &= (uint16_t)~(1u << la); ex->gp_used |= (1u << FREG_R8); }
        const uint32_t ra = fval_addr_to_gp(ex, rhs, true);
        if (ra != FREG_R9) { x86_mov_rr(obj, true, FREG_R9, ra); ex->gp_used &= (uint16_t)~(1u << ra); }
        ex->gp_used = 0;
        // RAX = lhs.lo, RDX = lhs.hi, RCX = rhs.lo, RSI = rhs.hi
        x86_load(obj, FREG_RAX, FREG_R8, 0, 8u, false);
        x86_load(obj, FREG_RDX, FREG_R8, 8, 8u, false);
        x86_load(obj, FREG_RCX, FREG_R9, 0, 8u, false);
        x86_load(obj, FREG_RSI, FREG_R9, 8, 8u, false);
        if (op == FTOK_PLUS)
        {
            x86_alu_rr(obj, true, 0x01u, FREG_RAX, FREG_RCX); // add rax, rcx
            x86_alu_rr(obj, true, 0x11u, FREG_RDX, FREG_RSI); // adc rdx, rsi
        }
        else if (op == FTOK_MINUS)
        {
            x86_alu_rr(obj, true, 0x29u, FREG_RAX, FREG_RCX); // sub rax, rcx
            x86_alu_rr(obj, true, 0x19u, FREG_RDX, FREG_RSI); // sbb rdx, rsi
        }
        else if (op == FTOK_STAR)
        {
            // Full 128x128->128 multiply:
            //   r10 = lhs.hi * rhs.lo; r11 = lhs.lo * rhs.hi;
            //   rdx:rax = mul(lhs.lo, rhs.lo); rdx += r10 + r11;
            x86_mov_rr(obj, true, FREG_R10, FREG_RDX);
            x86_imul_rr(obj, true, FREG_R10, FREG_RCX);
            x86_mov_rr(obj, true, FREG_R11, FREG_RAX);
            x86_imul_rr(obj, true, FREG_R11, FREG_RSI);
            x86_f7(obj, true, 4u, FREG_RCX); // mul rcx -> rdx:rax = rax * rcx!
            x86_alu_rr(obj, true, 0x01u, FREG_RDX, FREG_R10);
            x86_alu_rr(obj, true, 0x01u, FREG_RDX, FREG_R11);
        }
        else if (op == FTOK_AMP || op == FTOK_PIPE || op == FTOK_CARET)
        {
            const uint8_t aop = op == FTOK_AMP ? 0x21u : op == FTOK_PIPE ? 0x09u : 0x31u;
            x86_alu_rr(obj, true, aop, FREG_RAX, FREG_RCX);
            x86_alu_rr(obj, true, aop, FREG_RDX, FREG_RSI);
        }
        else
        {
            fast_cc_bail(cc, "128-bit div/mod unsupported");
            return;
        }
        x86_store(obj, FREG_RBP, slot, FREG_RAX, 8u);
        x86_store(obj, FREG_RBP, slot + 8, FREG_RDX, 8u);
        *out = (FastVal){ .type = ct, .kind = FVAL_LOCAL, .off = slot };
        return;
    }

    if (ftype_is_float(ct))
    {
        const bool is_f64 = ftype_is_f64(ct);
        const uint32_t xl = fval_to_xmm(ex, lhs, true);
        const uint32_t xr = fval_to_xmm(ex, rhs, true);
        const uint8_t sop = op == FTOK_PLUS ? 0x58u : op == FTOK_MINUS ? 0x5cu : op == FTOK_STAR ? 0x59u : 0x5eu;
        x86_sse_rr(obj, is_f64, sop, xl, xr);
        ex->xmm_used &= (uint8_t)~(1u << xr);
        *out = (FastVal){ .type = ct, .kind = FVAL_XMM, .reg = (uint8_t)xl };
        return;
    }

    const bool w64 = ftype_size(cc, ct) == 8u;
    const bool sgn = ftype_is_signed(cc, ct);

    // Constant folding for two integer immediates (skip div/mod by 0!)
    if (lhs->kind == FVAL_IMM && rhs->kind == FVAL_IMM &&
        !((op == FTOK_SLASH || op == FTOK_PERCENT) && rhs->imm == 0))
    {
        // #3577: a signed fold that overflows (INT_MAX + 1, INT_MIN / -1) is -Winteger-overflow: the full pipeline's.
        if (sgn)
        {
            bool overflow = false;
            if (w64)
            {
                int64_t t = 0;
                overflow = (op == FTOK_PLUS && __builtin_add_overflow(lhs->imm, rhs->imm, &t)) ||
                           (op == FTOK_MINUS && __builtin_sub_overflow(lhs->imm, rhs->imm, &t)) ||
                           (op == FTOK_STAR && __builtin_mul_overflow(lhs->imm, rhs->imm, &t)) ||
                           ((op == FTOK_SLASH || op == FTOK_PERCENT) && lhs->imm == INT64_MIN && rhs->imm == -1);
            }
            else
            {
                int32_t t = 0;
                const int32_t l32 = (int32_t)lhs->imm;
                const int32_t r32 = (int32_t)rhs->imm;
                overflow = (op == FTOK_PLUS && __builtin_add_overflow(l32, r32, &t)) ||
                           (op == FTOK_MINUS && __builtin_sub_overflow(l32, r32, &t)) ||
                           (op == FTOK_STAR && __builtin_mul_overflow(l32, r32, &t)) ||
                           ((op == FTOK_SLASH || op == FTOK_PERCENT) && l32 == INT32_MIN && r32 == -1);
            }
            if (overflow)
            {
                fast_cc_bail(cc, "signed overflow in a constant");
                return;
            }
        }
        uint64_t a = w64 ? (uint64_t)lhs->imm : (uint32_t)lhs->imm;
        uint64_t b = w64 ? (uint64_t)rhs->imm : (uint32_t)rhs->imm;
        uint64_t r = 0;
        if (op == FTOK_PLUS) r = a + b;
        else if (op == FTOK_MINUS) r = a - b;
        else if (op == FTOK_STAR) r = a * b;
        else if (op == FTOK_AMP) r = a & b;
        else if (op == FTOK_PIPE) r = a | b;
        else if (op == FTOK_CARET) r = a ^ b;
        else if (op == FTOK_SLASH)
        {
            if (sgn)
            {
                if (w64) r = (lhs->imm == INT64_MIN && rhs->imm == -1) ? (uint64_t)INT64_MIN : (uint64_t)(lhs->imm / rhs->imm);
                else r = ((int32_t)lhs->imm == INT32_MIN && (int32_t)rhs->imm == -1) ? (uint64_t)(uint32_t)INT32_MIN : (uint64_t)(int64_t)((int32_t)lhs->imm / (int32_t)rhs->imm);
            }
            else { r = a / b; }
        }
        else if (op == FTOK_PERCENT)
        {
            if (sgn)
            {
                if (w64) r = (lhs->imm == INT64_MIN && rhs->imm == -1) ? 0u : (uint64_t)(lhs->imm % rhs->imm);
                else r = ((int32_t)lhs->imm == INT32_MIN && (int32_t)rhs->imm == -1) ? 0u : (uint64_t)(int64_t)((int32_t)lhs->imm % (int32_t)rhs->imm);
            }
            else { r = a % b; }
        }
        if (!w64) { r = sgn ? (uint64_t)(int64_t)(int32_t)r : (uint32_t)r; }
        *out = (FastVal){ .type = ct, .kind = FVAL_IMM, .imm = (int64_t)r };
        return;
    }

    // Division & modulo (uses RAX + RDX!)
    if (op == FTOK_SLASH || op == FTOK_PERCENT)
    {
        if (!cc->quiet && rhs->kind == FVAL_IMM && rhs->imm == 0)
        {
            fast_cc_bail(cc, "division by zero");
            return;
        }
        // Save/spill RAX and RDX from outer vstack/call_args if occupied
        for (uint32_t i = 0; i < ex->vsp; i += 1)
        {
            if (fval_holds_gp(&ex->vstack[i]) && (ex->vstack[i].reg == FREG_RAX || ex->vstack[i].reg == FREG_RDX))
            {
                fval_spill_one(ex, &ex->vstack[i]);
            }
        }
        for (uint32_t i = 0; i < ex->arg_sp; i += 1)
        {
            if (fval_holds_gp(&ex->call_args[i]) && (ex->call_args[i].reg == FREG_RAX || ex->call_args[i].reg == FREG_RDX))
            {
                fval_spill_one(ex, &ex->call_args[i]);
            }
        }
        // Load divisor into a register that is neither RAX nor RDX!
        uint32_t rr = fval_to_gp(ex, rhs, true);
        if (rr == FREG_RAX || rr == FREG_RDX)
        {
            const uint32_t nr = fval_alloc_gp_avoid(ex, (1u << FREG_RAX) | (1u << FREG_RDX));
            x86_mov_rr(obj, true, nr, rr);
            ex->gp_used &= (uint16_t)~(1u << rr);
            rr = nr;
        }
        const uint32_t rl = fval_to_gp(ex, lhs, true);
        if (rl != FREG_RAX)
        {
            x86_mov_rr(obj, true, FREG_RAX, rl);
            ex->gp_used &= (uint16_t)~(1u << rl);
        }
        if (sgn)
        {
            if (w64) { x86_cqo(obj); } else { x86_cdq(obj); }
            x86_f7(obj, w64, 7u, rr); // idiv rr
        }
        else
        {
            x86_mov_ri(obj, false, FREG_RDX, 0);
            x86_f7(obj, w64, 6u, rr); // div rr
        }
        ex->gp_used &= (uint16_t)~(1u << rr);
        const uint32_t res_r = op == FTOK_SLASH ? FREG_RAX : FREG_RDX;
        ex->gp_used |= (uint16_t)(1u << res_r);
        *out = (FastVal){ .type = ct, .kind = FVAL_REG, .reg = (uint8_t)res_r };
        return;
    }

    // Multiply
    if (op == FTOK_STAR)
    {
        const uint32_t rl = fval_to_gp(ex, lhs, true);
        if (rhs->kind == FVAL_IMM && rhs->imm >= INT32_MIN && rhs->imm <= INT32_MAX)
        {
            x86_imul_rri(obj, w64, rl, rl, (int32_t)rhs->imm);
        }
        else
        {
            const uint32_t rr = fval_to_gp(ex, rhs, true);
            x86_imul_rr(obj, w64, rl, rr);
            ex->gp_used &= (uint16_t)~(1u << rr);
        }
        *out = (FastVal){ .type = ct, .kind = FVAL_REG, .reg = (uint8_t)rl };
        return;
    }

    // ADD / SUB / AND / OR / XOR
    const uint32_t rl = fval_to_gp(ex, lhs, true);
    if (rhs->kind == FVAL_IMM && rhs->imm >= INT32_MIN && rhs->imm <= INT32_MAX)
    {
        const uint8_t ext = op == FTOK_PLUS ? 0u : op == FTOK_PIPE ? 1u : op == FTOK_AMP ? 4u : op == FTOK_MINUS ? 5u : 6u;
        x86_alu_ri(obj, w64, ext, rl, (int32_t)rhs->imm);
    }
    else
    {
        const uint32_t rr = fval_to_gp(ex, rhs, true);
        const uint8_t aop = op == FTOK_PLUS ? 0x01u : op == FTOK_PIPE ? 0x09u : op == FTOK_AMP ? 0x21u : op == FTOK_MINUS ? 0x29u : 0x31u;
        x86_alu_rr(obj, w64, aop, rl, rr);
        ex->gp_used &= (uint16_t)~(1u << rr);
    }
    *out = (FastVal){ .type = ct, .kind = FVAL_REG, .reg = (uint8_t)rl };
}

// Emit pre/post `++` or `--` on lvalue `lv` (evaluates address ONCE!)
static void fval_emit_incdec(FastExprCtx* ex, FastVal* lv, bool is_inc, bool is_post, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    if (!lv->is_lvalue || ftype_is_const(lv->type))
    {
        fast_cc_bail(cc, "inc/dec of const or non-lvalue");
        return;
    }
    const FastType t = ftype_unqual(lv->type);
    // #3577: a scalar, and a pointer's to a complete type, or the full pipeline's error.
    {
        const uint32_t operand = BUSTER_FAST_OPERAND_CLASS(cc, t);
        uint32_t target_incomplete = 0;
        if (operand == BUSTER_FOPERAND_POINTER)
        {
            const FastType target = ftype_deref(cc, t);
            target_incomplete = BUSTER_FAST_TYPE_INCOMPLETE(cc, target);
        }
        if (((uint32_t)(operand == BUSTER_FOPERAND_OTHER) | target_incomplete) != 0)
        {
            fast_cc_bail(cc, "increment the full pipeline refuses");
            return;
        }
    }
    // #3577: a step in an unevaluated operand (sizeof's) is the full pipeline's -Wunevaluated-expression.
    if (ex->unevaluated > 0) { fast_cc_bail(cc, "side effect in an unevaluated operand"); return; }
    BUSTER_FSEQ_MODIFY(ex, lv)
    // A compile again for the log, or a dead branch's (#3592), makes no code.
    if (ex->eval_only > 0)
    {
        *out = (FastVal){ .type = t, .kind = FVAL_REG };
        return;
    }
    fval_flush_flags(ex);

    // Load old value WITHOUT releasing `lv`!
    if (ftype_is_float(t))
    {
        const bool is_f64 = ftype_is_f64(t);
        const uint32_t xold = fval_to_xmm(ex, lv, false);
        uint32_t xres = xold;
        uint32_t xnew = xold;
        if (is_post)
        {
            xnew = fval_alloc_xmm(ex);
            x86_sse_rr(obj, is_f64, 0x10u, xnew, xold);
        }
        const uint32_t x1 = fval_alloc_xmm(ex);
        const uint32_t ro = is_f64 ? fast_cc_get_one_f64(cc) : fast_cc_get_one_f32(cc);
        x86_sse_load_rip(obj, is_f64, x1, obj->sec_sym_rodata, (int64_t)ro);
        x86_sse_rr(obj, is_f64, is_inc ? 0x58u : 0x5cu, xnew, x1);
        ex->xmm_used &= (uint8_t)~(1u << x1);
        FastVal new_v = { .type = t, .kind = FVAL_XMM, .reg = (uint8_t)xnew };
        fval_store(ex, lv, &new_v, true, is_post ? NULL : out);
        if (is_post) { *out = (FastVal){ .type = t, .kind = FVAL_XMM, .reg = (uint8_t)xres }; }
        return;
    }

    const uint32_t old_r = fval_to_gp(ex, lv, false);
    uint32_t new_r = old_r;
    if (is_post)
    {
        new_r = fval_alloc_gp(ex);
        x86_mov_rr(obj, true, new_r, old_r);
    }
    const bool is_ptr = ftype_is_ptr(t);
    const int32_t step = is_ptr ? (int32_t)(ftype_size(cc, ftype_deref(cc, t)) != 0 ? ftype_size(cc, ftype_deref(cc, t)) : 1u) : 1;
    const bool w64 = ftype_size(cc, t) == 8u;
    if (ftype_is_bool(t))
    {
        if (is_inc) { x86_mov_ri(obj, false, new_r, 1u); }
        else { x86_alu_ri(obj, false, 6u, new_r, 1); } // xor 1
    }
    else
    {
        x86_alu_ri(obj, w64, is_inc ? 0u : 5u, new_r, step);
        const uint32_t sz = ftype_size(cc, t);
        if (sz < 4u) { x86_extend(obj, new_r, new_r, sz, ftype_is_signed(cc, t)); }
    }
    FastVal new_v = { .type = t, .kind = FVAL_REG, .reg = (uint8_t)new_r };
    fval_store(ex, lv, &new_v, true, is_post ? NULL : out);
    if (is_post) { *out = (FastVal){ .type = t, .kind = FVAL_REG, .reg = (uint8_t)old_r }; }
}

// Emit compound assignment `lhs op= rhs` (evaluates `lhs` address ONCE!)
static void fval_emit_compound_assign(FastExprCtx* ex, uint8_t assign_op, FastVal* lhs, FastVal* rhs, FastVal* out)
{
    if (!lhs->is_lvalue || ftype_is_const(lhs->type))
    {
        fast_cc_bail(ex->cc, "compound assign to const or non-lvalue");
        return;
    }
    static const uint8_t op_map[10] = {
        FTOK_PLUS, FTOK_MINUS, FTOK_STAR, FTOK_SLASH, FTOK_PERCENT,
        FTOK_AMP, FTOK_PIPE, FTOK_CARET, FTOK_LSHIFT, FTOK_RSHIFT
    };
    const uint8_t bin_op = op_map[assign_op - FTOK_ADD_ASSIGN];
    // #3577: an assignment in an unevaluated operand (sizeof's) is the full pipeline's -Wunevaluated-expression.
    if (ex->unevaluated > 0)
    {
        fast_cc_bail(ex->cc, "side effect in an unevaluated operand");
        return;
    }
    BUSTER_FSEQ_MODIFY(ex, lhs)
    // A compile again for the log, or a dead branch's (#3592), makes no code.
    if (ex->eval_only > 0)
    {
        *out = (FastVal){ .type = ftype_unqual(lhs->type), .kind = FVAL_REG };
        return;
    }
    fval_materialize_flags(ex, rhs);
    // Load LHS without releasing its address register, compute `old op rhs`, then store into `lhs`!
    if (ftype_is_i128(lhs->type))
    {
        FastVal old_val = *lhs;
        if (fval_holds_gp(lhs)) { fval_spill_one(ex, lhs); old_val = *lhs; }
        FastVal res = { 0 };
        fval_emit_binary(ex, bin_op, &old_val, rhs, &res);
        fval_store(ex, lhs, &res, true, out);
        return;
    }
    FastVal old_val = { .type = ftype_unqual(lhs->type) };
    if (ftype_is_float(lhs->type))
    {
        old_val.kind = FVAL_XMM;
        old_val.reg = (uint8_t)fval_to_xmm(ex, lhs, false);
    }
    else
    {
        old_val.kind = FVAL_REG;
        old_val.reg = (uint8_t)fval_to_gp(ex, lhs, false);
    }
    // Protect lhs's address register across `fval_emit_binary` (which may spill for div/shift!)
    if (fval_holds_gp(lhs))
    {
        // Spill `lhs`'s address to FVAL_SPILL_ADDR if bin_op is /, %, <<, >> so div/shift never clobbers it!
        if (bin_op == FTOK_SLASH || bin_op == FTOK_PERCENT || bin_op == FTOK_LSHIFT || bin_op == FTOK_RSHIFT)
        {
            fval_spill_one(ex, lhs);
        }
    }
    FastVal res = { 0 };
    fval_emit_binary(ex, bin_op, &old_val, rhs, &res);
    fval_store(ex, lhs, &res, true, out);
}

// Emit recognized __builtin_* call (clz, ctz, bswap, popcount, expect, constant_p, unreachable, inf/nan, memcpy/memset/...)
// Whether `t` is a va_list (#3577): __builtin_va_list, an array of one __va_list_tag, which decays to a pointer to it.
static bool fast_cc_is_va_list(FastCCState* cc, FastType t)
{
    const FastType d = BUSTER_FTYPE_CODEGEN(ftype_unqual(ftype_decay(cc, t)));
    return ftype_is_ptr(d) && BUSTER_FTYPE_CODEGEN(ftype_unqual(ftype_deref(cc, d))) == cc->va_list_tag_type;
}

// The argument count each builtin takes (#3577), UINT32_MAX for one this list doesn't hold; another count is the full
// pipeline's error.
static uint32_t fast_cc_builtin_arity(const FastCCState* cc, uint32_t id)
{
    uint32_t arity = UINT32_MAX;
    if (id == cc->id_builtin_expect) { arity = 2u; }
    else if (id == cc->id_builtin_constant_p || id == cc->id_builtin_clz || id == cc->id_builtin_clzll || id == cc->id_builtin_ctz ||
             id == cc->id_builtin_ctzll || id == cc->id_builtin_bswap16 || id == cc->id_builtin_bswap32 || id == cc->id_builtin_bswap64 ||
             id == cc->id_builtin_popcount || id == cc->id_builtin_popcountll || id == cc->id_builtin_nan || id == cc->id_builtin_nanf ||
             id == cc->id_builtin_strlen) { arity = 1u; }
    else if (id == cc->id_builtin_unreachable || id == cc->id_builtin_inf || id == cc->id_builtin_inff || id == cc->id_builtin_huge_val ||
             id == cc->id_builtin_huge_valf || id == cc->id_sync_synchronize) { arity = 0u; }
    else if (id == cc->id_builtin_memcpy || id == cc->id_builtin_memset || id == cc->id_builtin_memmove || id == cc->id_builtin_memcmp) { arity = 3u; }
    else if (id == cc->id_atomic_load_n) { arity = 2u; }
    else if (id == cc->id_atomic_store_n) { arity = 3u; }
    return arity;
}

static __attribute__((noinline)) void fval_emit_builtin_call(FastExprCtx* ex, uint32_t id, FastVal* args, uint32_t argc, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    if (id == cc->id_builtin_expect)
    {
        if (argc < 1u) { fast_cc_bail(cc, "builtin_expect argc"); return; }
        for (uint32_t i = 1u; i < argc; i += 1) { fval_release(ex, &args[i]); }
        *out = args[0];
        fval_convert(ex, out, FTYPE_I64);
        return;
    }
    if (id == cc->id_builtin_constant_p)
    {
        const bool is_c = argc >= 1u && (args[0].kind == FVAL_IMM || args[0].kind == FVAL_FIMM);
        for (uint32_t i = 0; i < argc; i += 1) { fval_release(ex, &args[i]); }
        *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = is_c ? 1 : 0 };
        return;
    }
    if (id == cc->id_builtin_unreachable)
    {
        if (ex->eval_only == 0) { x86_ud2(obj); }
        *out = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
        return;
    }
    if (id == cc->id_builtin_inff || id == cc->id_builtin_huge_valf) { *out = (FastVal){ .type = FTYPE_F32, .kind = FVAL_FIMM, .imm = (int64_t)UINT32_C(0x7f800000) }; return; }
    if (id == cc->id_builtin_inf || id == cc->id_builtin_huge_val)  { *out = (FastVal){ .type = FTYPE_F64, .kind = FVAL_FIMM, .imm = (int64_t)UINT64_C(0x7ff0000000000000) }; return; }
    if (id == cc->id_builtin_nanf) { for (uint32_t i = 0; i < argc; i += 1) fval_release(ex, &args[i]); *out = (FastVal){ .type = FTYPE_F32, .kind = FVAL_FIMM, .imm = (int64_t)UINT32_C(0x7fc00000) }; return; }
    if (id == cc->id_builtin_nan)  { for (uint32_t i = 0; i < argc; i += 1) fval_release(ex, &args[i]); *out = (FastVal){ .type = FTYPE_F64, .kind = FVAL_FIMM, .imm = (int64_t)UINT64_C(0x7ff8000000000000) }; return; }
    if (id == cc->id_builtin_clzll || id == cc->id_builtin_clz || id == cc->id_builtin_ctzll || id == cc->id_builtin_ctz)
    {
        if (argc < 1u) { fast_cc_bail(cc, "builtin clz/ctz argc"); return; }
        const bool is64 = (id == cc->id_builtin_clzll || id == cc->id_builtin_ctzll);
        const bool is_clz = (id == cc->id_builtin_clzll || id == cc->id_builtin_clz);
        if (ex->eval_only > 0) { *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG }; return; }
        fval_convert(ex, &args[0], is64 ? FTYPE_U64 : FTYPE_U32);
        const uint32_t r = fval_to_gp(ex, &args[0], true);
        if (is_clz)
        {
            x86_bsr(obj, is64, r, r);
            x86_alu_ri(obj, false, 6u, r, is64 ? 63 : 31); // xor r, 63 / 31
        }
        else
        {
            x86_bsf(obj, is64, r, r);
        }
        *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = (uint8_t)r };
        return;
    }
    if (id == cc->id_builtin_bswap16 || id == cc->id_builtin_bswap32 || id == cc->id_builtin_bswap64)
    {
        if (argc < 1u) { fast_cc_bail(cc, "builtin bswap argc"); return; }
        const FastType rt = id == cc->id_builtin_bswap16 ? FTYPE_U16 : id == cc->id_builtin_bswap32 ? FTYPE_U32 : FTYPE_U64;
        if (ex->eval_only > 0) { *out = (FastVal){ .type = rt, .kind = FVAL_REG }; return; }
        fval_convert(ex, &args[0], rt);
        const uint32_t r = fval_to_gp(ex, &args[0], true);
        if (id == cc->id_builtin_bswap16)
        {
            // rol r16, 8 -> 66 C1 C0+r 08
            x86_u8(obj, 0x66u);
            x86_rex(obj, false, 0, 0, r, false);
            x86_u8(obj, 0xc1u);
            x86_u8(obj, (uint8_t)(0xc0u | (r & 7u)));
            x86_u8(obj, 8u);
            x86_extend(obj, r, r, 2u, false);
        }
        else { x86_bswap(obj, id == cc->id_builtin_bswap64, r); }
        *out = (FastVal){ .type = rt, .kind = FVAL_REG, .reg = (uint8_t)r };
        return;
    }
    if (id == cc->id_builtin_popcount || id == cc->id_builtin_popcountll)
    {
        if (argc < 1u) { fast_cc_bail(cc, "builtin popcount argc"); return; }
        const bool is64 = id == cc->id_builtin_popcountll;
        if (ex->eval_only > 0) { *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG }; return; }
        fval_convert(ex, &args[0], is64 ? FTYPE_U64 : FTYPE_U32);
        const uint32_t r = fval_to_gp(ex, &args[0], true);
        x86_popcnt(obj, is64, r, r);
        *out = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = (uint8_t)r };
        return;
    }
    if (id == cc->id_sync_synchronize)
    {
        if (ex->eval_only == 0) { x86_u8(obj, 0x0fu); x86_u8(obj, 0xaeu); x86_u8(obj, 0xf0u); } // mfence
        *out = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
        return;
    }
    if (id == cc->id_atomic_load_n)
    {
        if (argc < 2u || !ftype_is_ptr(args[0].type)) { fast_cc_bail(cc, "atomic_load_n args"); return; }
        const FastType pt = ftype_deref(cc, args[0].type);
        const uint32_t sz = ftype_size(cc, pt);
        if (!ftype_is_int(pt) && !ftype_is_ptr(pt)) { fast_cc_bail(cc, "atomic_load_n type"); return; }
        if (sz != 1u && sz != 2u && sz != 4u && sz != 8u) { fast_cc_bail(cc, "atomic_load_n size"); return; }
        fval_release(ex, &args[1]);
        if (ex->eval_only > 0) { fval_release(ex, &args[0]); *out = (FastVal){ .type = pt, .kind = FVAL_REG }; return; }
        const uint32_t pr = fval_to_gp(ex, &args[0], true);
        x86_load(obj, pr, pr, 0, sz, ftype_is_signed(cc, pt));
        *out = (FastVal){ .type = pt, .kind = FVAL_REG, .reg = (uint8_t)pr };
        return;
    }
    if (id == cc->id_atomic_store_n)
    {
        if (argc < 3u || !ftype_is_ptr(args[0].type)) { fast_cc_bail(cc, "atomic_store_n args"); return; }
        const FastType pt = ftype_deref(cc, args[0].type);
        const uint32_t sz = ftype_size(cc, pt);
        if (!ftype_is_int(pt) && !ftype_is_ptr(pt)) { fast_cc_bail(cc, "atomic_store_n type"); return; }
        if (sz != 1u && sz != 2u && sz != 4u && sz != 8u) { fast_cc_bail(cc, "atomic_store_n size"); return; }
        const bool need_fence = args[2].kind != FVAL_IMM || args[2].imm >= 5;
        fval_release(ex, &args[2]);
        if (ex->eval_only > 0)
        {
            fval_release(ex, &args[1]);
            fval_release(ex, &args[0]);
            *out = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
            return;
        }
        fval_convert(ex, &args[1], pt);
        const uint32_t pr = fval_to_gp(ex, &args[0], true);
        const uint32_t vr = fval_to_gp(ex, &args[1], true);
        x86_store(obj, pr, 0, vr, sz);
        if (need_fence) { x86_u8(obj, 0x0fu); x86_u8(obj, 0xaeu); x86_u8(obj, 0xf0u); } // mfence
        ex->gp_used &= ~(1u << pr);
        ex->gp_used &= ~(1u << vr);
        *out = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
        return;
    }
    fast_cc_bail(cc, "unhandled builtin call");
}

// Emit full SysV AMD64 ABI function call (direct or indirect, 6 GP + 8 XMM + stack args, 1-16B & >16B structs, variadic!)
// Whether a nonnull function's call passes no null constant for a parameter the attribute names (#3577); the function
// type keeps their mask (FastComplexType's tag_ident), its declarations' together.
static __attribute__((cold, noinline)) bool fast_cc_nonnull_arguments_clean(const FastCCState* cc, const FastComplexType* fn,
                                                                           const FastVal* args, uint32_t argc)
{
    const uint32_t passed = argc < fn->count ? argc : fn->count;
    uint32_t named = fn->tag_ident & (passed >= 32u ? UINT32_MAX : (UINT32_C(1) << passed) - 1u);
    uint32_t nulls = 0;
    while (named != 0)
    {
        const uint32_t i = (uint32_t)__builtin_ctz(named);
        named &= named - 1u;
        nulls |= (uint32_t)(args[i].kind == FVAL_IMM) & (uint32_t)(args[i].imm == 0) &
                 (uint32_t)ftype_is_ptr(ftype_unqual(cc->param_types[fn->start + i]));
    }
    return nulls == 0;
}

static BUSTER_DIRECT_HOT void fval_emit_call(FastExprCtx* ex, FastVal* callee, FastVal* args, uint32_t argc, uint32_t outer_arg_sp, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;

    // Handle recognized compiler builtins directly!
    if (callee->kind == FVAL_BUILTIN)
    {
        const uint32_t bid = callee->sym_idx;
        // #3577: each with its own argument count, or the full pipeline's error.
        if (fast_cc_builtin_arity(cc, bid) != argc)
        {
            fast_cc_bail(cc, "builtin call's argument count");
            return;
        }
        // Forward __builtin_memcpy/memset/memmove/memcmp/strlen to standard libc symbol calls!
        const char* libc_name = NULL;
        FastType ret_t = FTYPE_VOID;
        if (bid == cc->id_builtin_memcpy)       { libc_name = "memcpy";  ret_t = ftype_make_ptr(cc, FTYPE_VOID); }
        else if (bid == cc->id_builtin_memset)  { libc_name = "memset";  ret_t = ftype_make_ptr(cc, FTYPE_VOID); }
        else if (bid == cc->id_builtin_memmove) { libc_name = "memmove"; ret_t = ftype_make_ptr(cc, FTYPE_VOID); }
        else if (bid == cc->id_builtin_memcmp)  { libc_name = "memcmp";  ret_t = FTYPE_I32; }
        else if (bid == cc->id_builtin_strlen)  { libc_name = "strlen";  ret_t = FTYPE_U64; }
        if (libc_name == NULL)
        {
            fval_emit_builtin_call(ex, bid, args, argc, out);
            // A void builtin (__builtin_unreachable, __sync_synchronize, ...) is an effect; a value's is not (#3577).
            out->marks = (uint8_t)(BUSTER_FVAL_MARK_EFFECT * (uint32_t)(out->kind == FVAL_VOID));
            return;
        }
        // #3577: these builtins' pointer arguments are nonnull to the full pipeline (-Wnonnull for a null constant).
        const uint32_t pointers = (bid == cc->id_builtin_memset || bid == cc->id_builtin_strlen) ? 1u : 2u;
        for (uint32_t i = 0; i < argc && i < pointers; i += 1)
        {
            if (args[i].kind == FVAL_IMM && args[i].imm == 0) { fast_cc_bail(cc, "null constant argument to a nonnull builtin"); return; }
        }
        // Synthesize direct call to libc function!
        const uint32_t lid = fast_ident_intern_cstr(&cc->pp->idents, libc_name);
        uint32_t si = lid < cc->env_cap ? cc->sym_cur[lid] : 0u;
        if (si == 0)
        {
            const uint32_t fc = fast_cc_alloc_ctype(cc, FCTYPE_FUNC);
            cc->ctypes[fc].inner = ret_t;
            cc->ctypes[fc].is_oldstyle = 1u;
            cc->ctypes[fc].complete = 1u;
            si = fast_cc_sym_bind(cc, lid, (FastSym){ .kind = FSYM_FUNC, .storage = 2u, .type = ftype_from_index(cc, fc) });
        }
        const uint32_t osym = fast_cc_ensure_obj_sym(cc, si);
        // memcmp and strlen are pure: a discarded call is -Wunused-value (#3577).
        const uint8_t pure = (uint8_t)(BUSTER_FVAL_MARK_PURE * (uint32_t)((bid == cc->id_builtin_memcmp) | (bid == cc->id_builtin_strlen)));
        *callee = (FastVal){ .type = cc->syms[si].type, .kind = FVAL_GLOBAL, .sym_idx = osym, .is_func_sym = 1u, .marks = pure };
    }

    // Resolve function type (handles both `func` and `func_ptr`!)
    FastType ft = callee->type;
    if (ftype_is_ptr(ft)) { ft = ftype_deref(cc, ft); }
    if (!ftype_is_func(ft))
    {
        fast_cc_bail(cc, "call to non-function");
        return;
    }
    const FastComplexType* fn = cc->ctypes + (ft & BUSTER_FTYPE_CIDX_MASK);
    const FastType ret_t = ftype_unqual(fn->inner);
    if (ftype_is_unsupported(ret_t)) { fast_cc_bail(cc, "unsupported call return type"); return; }
    // A call the full pipeline checks (#3577): a nonnull function's null constant for a pointer parameter is -Wnonnull
    // (any pointer parameter, a superset of the attribute's), and a warn_unused_result call's result keeps the mark for
    // the statement that would discard it.
    const uint8_t callee_marks = callee->marks;
    if ((callee_marks & BUSTER_FVAL_MARK_NONNULL) != 0 && !fast_cc_nonnull_arguments_clean(cc, fn, args, argc))
    {
        fast_cc_bail(cc, "null constant argument to a nonnull function");
        return;
    }
    // #3577: the argument count, in an unevaluated call (sizeof's) too; arguments to a function without a prototype are
    // the full pipeline's -Wdeprecated-non-prototype.
    {
        const uint32_t oldstyle = (uint32_t)(fn->is_oldstyle != 0);
        const uint32_t variadic = (uint32_t)(fn->is_variadic != 0);
        const uint32_t count_bad = (oldstyle ^ 1u) & (((variadic ^ 1u) & (uint32_t)(argc != fn->count)) | (variadic & (uint32_t)(argc < fn->count)));
        if ((count_bad | (oldstyle & (uint32_t)(argc != 0))) != 0)
        {
            fast_cc_bail(cc, "call argument count mismatch, or arguments without a prototype");
            return;
        }
    }

    if (ex->eval_only > 0)
    {
        // #3577: an unevaluated call's arguments convert to their parameters as an evaluated call's do (below); a void
        // argument to `...` is the full pipeline's error.
        for (uint32_t i = 0; i < argc && !cc->bailout; i += 1)
        {
            if (i < fn->count)
            {
                if (BUSTER_FAST_CONV_OWN(cc->param_types[fn->start + i], &args[i]) == 0)
                {
                    fast_cc_check_implicit_conv(cc, BUSTER_FTYPE_UNQUAL(cc->param_types[fn->start + i]), &args[i]);
                }
            }
            else if ((args[i].type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID)
            {
                fast_cc_bail(cc, "void argument");
            }
        }
        if (cc->bailout) { return; }
        for (uint32_t i = 0; i < argc; i += 1) { fval_release(ex, &args[i]); }
        fval_release(ex, callee);
        // The marks the code below gives the result: a dead branch's statement discards it (#3592).
        *out = (FastVal){ .type = ret_t, .kind = ftype_is_void(ret_t) ? FVAL_VOID : FVAL_REG,
                          .marks = (uint8_t)((callee_marks & BUSTER_FVAL_MARK_UNUSED_RESULT) |
                                             (BUSTER_FVAL_MARK_EFFECT * (uint32_t)((callee_marks & BUSTER_FVAL_MARK_PURE) == 0))) };
        return;
    }

    // Spill ALL live values below this call (both vstack and outer call_args!)
    fval_spill_all(ex, ex->vsp, outer_arg_sp);

    // Also spill callee if it's in a register, and spill all args that live in registers so ABI reg loading has ZERO conflicts!
    if (callee->kind != FVAL_GLOBAL || !callee->is_func_sym)
    {
        // Convert callee function pointer to spilled temp slot!
        const uint32_t cr = fval_to_gp(ex, callee, true);
        const int32_t cslot = fast_cc_alloc_tmp(cc, 8u, 8u);
        x86_store(obj, FREG_RBP, cslot, cr, 8u);
        ex->gp_used &= (uint16_t)~(1u << cr);
        callee->kind = FVAL_SPILL;
        callee->off = cslot;
        callee->is_func_sym = 0;
    }

    // Convert each argument to prototype parameter type (or default argument promotions) and spill any live reg/flags!
    // (The count was checked above.)
    const bool is_var = fn->is_variadic != 0 || fn->is_oldstyle != 0;
    for (uint32_t i = 0; i < argc; i += 1)
    {
        FastType pt = FTYPE_VOID;
        if (i < fn->count)
        {
            pt = ftype_unqual(cc->param_types[fn->start + i]);
            if (BUSTER_FAST_CONV_OWN(pt, &args[i]) == 0)
            {
                fast_cc_check_implicit_conv(cc, pt, &args[i]);
                if (cc->bailout) { return; }
            }
        }
        else
        {
            // Default argument promotion for variadic / old-style args
            FastType at = ftype_decay(cc, args[i].type);
            if (ftype_is_float(at) && !ftype_is_f64(at)) { pt = FTYPE_F64; }
            else if (ftype_is_int(at) && ftype_size(cc, at) < 4u) { pt = FTYPE_I32; }
            else { pt = ftype_unqual(at); }
        }
        // #3577: a void argument to `...` is the full pipeline's error.
        const uint32_t void_argument = (uint32_t)((args[i].type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
        if (((uint32_t)ftype_is_unsupported(pt) | (uint32_t)ftype_is_unsupported(args[i].type) | void_argument) != 0)
        {
            fast_cc_bail(cc, "unsupported call arg type, or a void argument");
            return;
        }
        if (!ftype_is_record(pt))
        {
            fval_convert(ex, &args[i], pt);
            fval_spill_one(ex, &args[i]);
        }
        else if (args[i].kind == FVAL_INDIRECT)
        {
            fval_spill_one(ex, &args[i]);
        }
    }

    // Classify return type: hidden sret pointer in RDI if struct/union > 16B (or FABI_MEM)
    bool has_sret = false;
    int32_t sret_slot = 0;
    if (ftype_is_record(ret_t))
    {
        const FastComplexType* rct = cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK);
        if (BUSTER_FAST_RECORD_OVERALIGNED(rct)) { fast_cc_bail(cc, "an aligned typedef's record by value"); }
        if (rct->abi_lo == FABI_MEM)
        {
            has_sret = true;
            sret_slot = fast_cc_alloc_tmp(cc, rct->size, rct->align < 8u ? 8u : rct->align);
        }
    }

    // Assign SysV AMD64 locations for every argument
    static const uint8_t abi_gp[6] = { FREG_RDI, FREG_RSI, FREG_RDX, FREG_RCX, FREG_R8, FREG_R9 };
    typedef struct ArgLoc ArgLoc;
    struct ArgLoc { uint8_t kind; uint8_t r0; uint8_t r1; uint8_t c0; uint8_t c1; int32_t stk_off; uint32_t sz; };
    ArgLoc locs[64];
    if (argc > 64u) { fast_cc_bail(cc, "too many call args"); return; }

    uint32_t gp_idx = has_sret ? 1u : 0u;
    uint32_t fp_idx = 0u;
    int32_t stk_cur = 0;

    for (uint32_t i = 0; i < argc; i += 1)
    {
        const FastType at = args[i].type;
        locs[i] = (ArgLoc){ 0 };
        if (ftype_is_record(at))
        {
            const FastComplexType* rct = cc->ctypes + (at & BUSTER_FTYPE_CIDX_MASK);
            if (BUSTER_FAST_RECORD_OVERALIGNED(rct)) { fast_cc_bail(cc, "an aligned typedef's record by value"); }
            locs[i].sz = rct->size;
            if (rct->abi_lo != FABI_MEM)
            {
                const uint32_t ngp = (rct->abi_lo == FABI_INT ? 1u : 0u) + (rct->abi_hi == FABI_INT ? 1u : 0u);
                const uint32_t nfp = (rct->abi_lo == FABI_SSE ? 1u : 0u) + (rct->abi_hi == FABI_SSE ? 1u : 0u);
                if (gp_idx + ngp <= 6u && fp_idx + nfp <= 8u)
                {
                    locs[i].kind = 3u; // register struct (1 or 2 eightbytes)
                    locs[i].c0 = rct->abi_lo;
                    locs[i].c1 = rct->abi_hi;
                    locs[i].r0 = rct->abi_lo == FABI_INT ? abi_gp[gp_idx++] : (uint8_t)(fp_idx++);
                    if (rct->abi_hi != FABI_NONE)
                    {
                        locs[i].r1 = rct->abi_hi == FABI_INT ? abi_gp[gp_idx++] : (uint8_t)(fp_idx++);
                    }
                    continue;
                }
            }
            locs[i].kind = 4u; // memory struct on stack
            locs[i].stk_off = stk_cur;
            stk_cur += (int32_t)((rct->size + 7u) & ~7u);
        }
        else if (ftype_is_float(at))
        {
            if (fp_idx < 8u) { locs[i].kind = 2u; locs[i].r0 = (uint8_t)(fp_idx++); }
            else { locs[i].kind = 5u; locs[i].stk_off = stk_cur; stk_cur += 8; }
        }
        else
        {
            if (gp_idx < 6u) { locs[i].kind = 1u; locs[i].r0 = abi_gp[gp_idx++]; }
            else { locs[i].kind = 5u; locs[i].stk_off = stk_cur; stk_cur += 8; }
        }
    }
    if (stk_cur > cc->cur_max_call_stack) { cc->cur_max_call_stack = stk_cur; }

    // Pass 1: Store all stack arguments into `[rsp + stk_off]` first (uses RAX/R10/R11/XMM0 freely!)
    for (uint32_t i = 0; i < argc; i += 1)
    {
        if (locs[i].kind == 4u)
        {
            const uint32_t src_a = fval_addr_to_gp(ex, &args[i], true);
            const uint32_t tmp_r = fval_alloc_gp(ex);
            x86_emit_memcpy_inline(obj, FREG_RSP, locs[i].stk_off, src_a, 0, locs[i].sz, tmp_r);
            ex->gp_used = 0;
        }
        else if (locs[i].kind == 5u)
        {
            if (ftype_is_float(args[i].type))
            {
                const uint32_t x = fval_to_xmm(ex, &args[i], true);
                x86_sse_store(obj, ftype_is_f64(args[i].type), FREG_RSP, locs[i].stk_off, x);
                ex->xmm_used = 0;
            }
            else
            {
                const uint32_t g = fval_to_gp(ex, &args[i], true);
                x86_store(obj, FREG_RSP, locs[i].stk_off, g, 8u);
                ex->gp_used = 0;
            }
        }
    }

    // Pass 2: Load all XMM register arguments (including SSE eightbytes of small structs)
    uint8_t loaded_xmm = 0;
    for (uint32_t i = 0; i < argc; i += 1)
    {
        if (locs[i].kind == 2u)
        {
            const bool is_f64 = ftype_is_f64(args[i].type);
            if (args[i].kind == FVAL_LOCAL || args[i].kind == FVAL_SPILL)
            {
                x86_sse_load(obj, is_f64, locs[i].r0, FREG_RBP, args[i].off);
            }
            else
            {
                ex->xmm_used = loaded_xmm;
                ex->gp_used = 0;
                const uint32_t x = fval_to_xmm(ex, &args[i], true);
                if (x != locs[i].r0) { x86_sse_rr(obj, is_f64, 0x10u, locs[i].r0, x); }
            }
            loaded_xmm |= (uint8_t)(1u << locs[i].r0);
            ex->xmm_used = loaded_xmm;
            ex->gp_used = 0;
        }
        else if (locs[i].kind == 3u && (locs[i].c0 == FABI_SSE || locs[i].c1 == FABI_SSE))
        {
            ex->gp_used = 0;
            const uint32_t a = fval_addr_to_gp(ex, &args[i], false);
            if (locs[i].c0 == FABI_SSE) { x86_sse_load(obj, true, locs[i].r0, a, 0); loaded_xmm |= (uint8_t)(1u << locs[i].r0); }
            if (locs[i].c1 == FABI_SSE) { x86_sse_load(obj, true, locs[i].r1, a, 8); loaded_xmm |= (uint8_t)(1u << locs[i].r1); }
            ex->gp_used = 0;
        }
    }

    // Pass 3: Load all GP register arguments into their exact ABI registers!
    // Because every argument was spilled/local/global/imm (ZERO live GP regs!), loading into RAX/R10 then moving to abi_gp
    // can never clobber an already-loaded ABI register (just avoid using already-loaded ABI regs as scratch!).
    uint16_t loaded_gp = 0;
    if (has_sret)
    {
        x86_lea(obj, FREG_RDI, FREG_RBP, sret_slot);
        loaded_gp |= (1u << FREG_RDI);
    }
    for (uint32_t i = 0; i < argc; i += 1)
    {
        if (locs[i].kind == 1u)
        {
            const uint32_t dst = locs[i].r0;
            ex->gp_used = loaded_gp;
            if (args[i].kind == FVAL_IMM)
            {
                x86_mov_ri(obj, ftype_size(cc, args[i].type) == 8u, dst, (uint64_t)args[i].imm);
            }
            else if (args[i].kind == FVAL_LOCAL_ADDR)
            {
                x86_lea(obj, dst, FREG_RBP, args[i].off);
            }
            else if (args[i].kind == FVAL_GLOBAL_ADDR)
            {
                x86_lea_sym(obj, dst, args[i].sym_idx, args[i].imm);
            }
            else if (args[i].kind == FVAL_LOCAL || args[i].kind == FVAL_SPILL)
            {
                if (ftype_is_array(args[i].type)) { x86_lea(obj, dst, FREG_RBP, args[i].off); }
                else { x86_load(obj, dst, FREG_RBP, args[i].off, ftype_size(cc, args[i].type), ftype_is_signed(cc, args[i].type)); }
            }
            else
            {
                const uint32_t g = fval_to_gp(ex, &args[i], true);
                if (g != dst) { x86_mov_rr(obj, true, dst, g); }
            }
            loaded_gp |= (uint16_t)(1u << dst);
        }
        else if (locs[i].kind == 3u && (locs[i].c0 == FABI_INT || locs[i].c1 == FABI_INT))
        {
            const uint16_t protect = (uint16_t)(loaded_gp |
                (locs[i].c0 == FABI_INT ? (1u << locs[i].r0) : 0u) |
                (locs[i].c1 == FABI_INT ? (1u << locs[i].r1) : 0u));
            ex->gp_used = protect;
            const uint32_t a = fval_addr_to_gp(ex, &args[i], true);
            if (locs[i].c0 == FABI_INT) { x86_load(obj, locs[i].r0, a, 0, 8u, false); loaded_gp |= (uint16_t)(1u << locs[i].r0); }
            if (locs[i].c1 == FABI_INT) { x86_load(obj, locs[i].r1, a, 8, 8u, false); loaded_gp |= (uint16_t)(1u << locs[i].r1); }
        }
    }
    ex->gp_used = 0;
    ex->xmm_used = 0;

    // Set AL = number of XMM arguments for variadic / old-style calls!
    if (is_var)
    {
        x86_mov_ri(obj, false, FREG_RAX, fp_idx);
    }

    // Emit the call!
    if (callee->kind == FVAL_GLOBAL && callee->is_func_sym)
    {
        x86_call_sym(obj, callee->sym_idx);
    }
    else
    {
        x86_load(obj, FREG_R11, FREG_RBP, callee->off, 8u, false);
        x86_call_reg(obj, FREG_R11);
    }

    // Collect return value per SysV AMD64 ABI!
    if (ftype_is_void(ret_t))
    {
        *out = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
    }
    else if (ftype_is_record(ret_t))
    {
        const FastComplexType* rct = cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK);
        if (has_sret)
        {
            *out = (FastVal){ .type = ret_t, .kind = FVAL_LOCAL, .off = sret_slot };
        }
        else
        {
            const int32_t rslot = fast_cc_alloc_tmp(cc, rct->size < 16u ? 16u : rct->size, 8u);
            uint32_t gp_r = 0;
            uint32_t fp_r = 0;
            if (rct->abi_lo == FABI_INT) { x86_store(obj, FREG_RBP, rslot, gp_r == 0 ? FREG_RAX : FREG_RDX, 8u); gp_r += 1u; }
            else if (rct->abi_lo == FABI_SSE) { x86_sse_store(obj, true, FREG_RBP, rslot, fp_r++); }
            if (rct->abi_hi == FABI_INT) { x86_store(obj, FREG_RBP, rslot + 8, gp_r == 0 ? FREG_RAX : FREG_RDX, 8u); }
            else if (rct->abi_hi == FABI_SSE) { x86_sse_store(obj, true, FREG_RBP, rslot + 8, fp_r); }
            *out = (FastVal){ .type = ret_t, .kind = FVAL_LOCAL, .off = rslot };
        }
    }
    else if (ftype_is_float(ret_t))
    {
        ex->xmm_used |= 1u; // XMM0
        *out = (FastVal){ .type = ret_t, .kind = FVAL_XMM, .reg = 0u };
    }
    else
    {
        // Ensure narrow signed/unsigned return values are properly extended in RAX!
        const uint32_t rsz = ftype_size(cc, ret_t);
        if (rsz < 4u) { x86_extend(obj, FREG_RAX, FREG_RAX, rsz, ftype_is_signed(cc, ret_t)); }
        ex->gp_used |= (1u << FREG_RAX);
        *out = (FastVal){ .type = ret_t, .kind = FVAL_REG, .reg = FREG_RAX };
    }
    // An effect unless the callee is const or pure (whose discarded call -Wunused-value questions).
    out->marks = (uint8_t)((callee_marks & BUSTER_FVAL_MARK_UNUSED_RESULT) |
                           (BUSTER_FVAL_MARK_EFFECT * (uint32_t)((callee_marks & BUSTER_FVAL_MARK_PURE) == 0)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 11: Iterative Pratt/Shunting-Yard Expression Compiler (ZERO recursion!)

typedef enum FastExprOpKind
{
    // Delimiters & special frames (prec = 0; never popped by binary operators)
    FEOP_LPAREN = 1,
    FEOP_SUBSCRIPT = 2,
    FEOP_CALL = 3,
    FEOP_GENERIC_CTRL = 4,
    FEOP_GENERIC_SEL = 5,
    FEOP_VA_START = 6,
    FEOP_VA_ARG = 7,
    FEOP_VA_END = 8,
    FEOP_VA_COPY = 9,
    FEOP_COMPOUND_LIT = 10,
    FEOP_TERNARY_Q = 11,   // barrier between `?` and `:` (allows `,` and `=` inside then-branch!)

    // Prefix unary operators (prec = 14, right-associative)
    FEOP_UNARY_PLUS = 20,
    FEOP_UNARY_MINUS = 21,
    FEOP_UNARY_TILDE = 22,
    FEOP_UNARY_BANG = 23,
    FEOP_DEREF = 24,
    FEOP_ADDR = 25,
    FEOP_PRE_INC = 26,
    FEOP_PRE_DEC = 27,
    FEOP_CAST = 28,
    FEOP_SIZEOF_UNARY = 29,
    FEOP_ALIGNOF_UNARY = 30,

    // Ternary completion (prec = 3, right-associative)
    FEOP_TERNARY_C = 36,

    // Binary / logical / assignment / comma operators use FTOK_* + 64 so they never collide with above!
    FEOP_TOK_BASE = 64
} FastExprOpKind;

// What a condition folded to (#3592), in a `?:`'s FastExprOp.constant and in FastCCState.branch_constant. The dead side of
// a constant condition is checked as an operand of sizeof is, with no code and no symbol.
#define BUSTER_FCONSTANT_NONE 0u
#define BUSTER_FCONSTANT_FALSE 1u
#define BUSTER_FCONSTANT_TRUE 2u

typedef struct FastExprOp FastExprOp;
struct FastExprOp
{
    uint16_t op;
    uint16_t extra;        // arg_count (CALL), elem_idx (COMPOUND_LIT), the then value's marks (TERNARY_C)
    uint32_t lbl0;         // false/true/else_lbl, or arg_start (CALL), or end_pos (GENERIC_SEL)
    uint32_t lbl1;         // then_tail_lbl (TERNARY_C)
    uint32_t lbl2;         // end_lbl (TERNARY_C), or a binary operator's token (#3577: ex->cmp_pos)
    FastType type;         // cast target, then_type (TERNARY_C), compound lit type
    int32_t slot;          // tern_slot or compound lit stack slot
    // #3577: the -Wunsequenced log's count + 1 where the operator's (right) operand begins: at its push, or a call's or a
    // compound literal's last separator (0 for an operator that doesn't keep it); and a sequence point's left operand's
    // first entry + 1 (`&&`, `||`, `,`, `?:`: where the operator below it began), and a conditional's else arm's.
    uint8_t seq_lo;
    uint8_t seq_mid;
    uint8_t seq_mid2;
    uint8_t constant;      // BUSTER_FCONSTANT_* (TERNARY_Q, TERNARY_C), or 1 where a constant left operand decided (&&, ||)
};

// #3577: whether compound literal CL stores its element of type ELEMENT at its position (extra: a member's index or an
// element's) as the full pipeline does, quietly: within CL's type (a scalar's one element, an array's count, a record's
// members; past them, -Wexcess-initializers), into a scalar or a record of the element's own type (an array or another
// record would take its elements by brace elision, which this path doesn't do), and over nothing an earlier element
// initialized (-Winitializer-overrides: fast_cc_init_overrides, from the positional element before a designator, lbl0,
// and the positions initialized, lbl1 and lbl2, which it marks). The next positional element is then past it.
static __attribute__((cold, noinline)) bool fast_cc_compound_element_quiet(const FastCCState* cc, FastExprOp* cl, FastType element)
{
    const uint32_t position = cl->extra;
    const FastComplexType* ct = cc->ctypes + (cl->type & BUSTER_FTYPE_CIDX_MASK);
    uint64_t touched = (uint64_t)cl->lbl1 | (uint64_t)cl->lbl2 << 32;
    FastType target = cl->type;
    int64_t target_bits = 0;
    bool within = position == 0;
    if (ftype_is_array(cl->type))
    {
        target = ct->inner;
        target_bits = (int64_t)position * ftype_size(cc, target) * 8;
        within = position < ct->count;
    }
    else if (ftype_is_record(cl->type))
    {
        within = position < ct->count;
        if (within)
        {
            const FastMemberInfo* mi = cc->member_info + ct->start + position;
            target = mi->type;
            target_bits = (int64_t)mi->byte_offset * 8 + (mi->bit_width != 0 ? mi->bit_offset : 0);
        }
    }
    const bool own_record = ftype_is_record(target) &&
                            BUSTER_FTYPE_CODEGEN(BUSTER_FTYPE_UNQUAL(target)) == BUSTER_FTYPE_CODEGEN(BUSTER_FTYPE_UNQUAL(element));
    const bool scalar = !ftype_is_array(target) && !ftype_is_record(target);
    const bool quiet = within && (scalar || own_record) && !fast_cc_init_overrides(cc, cl->type, 0, cl->lbl0, &touched, position, target_bits);
    cl->lbl0 = position + 1u;
    cl->lbl1 = (uint32_t)touched;
    cl->lbl2 = (uint32_t)(touched >> 32);
    return quiet;
}

static inline uint8_t fast_expr_tok_prec(uint8_t k)
{
    switch (k)
    {
        case FTOK_STAR: case FTOK_SLASH: case FTOK_PERCENT: return 13u;
        case FTOK_PLUS: case FTOK_MINUS: return 12u;
        case FTOK_LSHIFT: case FTOK_RSHIFT: return 11u;
        case FTOK_LT: case FTOK_LE: case FTOK_GT: case FTOK_GE: return 10u;
        case FTOK_EQ: case FTOK_NE: return 9u;
        case FTOK_AMP: return 8u;
        case FTOK_CARET: return 7u;
        case FTOK_PIPE: return 6u;
        case FTOK_LAND: return 5u;
        case FTOK_LOR: return 4u;
        case FTOK_QUESTION: return 3u;
        case FTOK_ASSIGN: case FTOK_ADD_ASSIGN: case FTOK_SUB_ASSIGN: case FTOK_MUL_ASSIGN:
        case FTOK_DIV_ASSIGN: case FTOK_MOD_ASSIGN: case FTOK_AND_ASSIGN: case FTOK_OR_ASSIGN:
        case FTOK_XOR_ASSIGN: case FTOK_SHL_ASSIGN: case FTOK_SHR_ASSIGN: return 2u;
        case FTOK_COMMA: return 1u;
        default: return 0u;
    }
}

static inline uint8_t fast_expr_op_prec(uint16_t op)
{
    if (op < FEOP_UNARY_PLUS) { return 0u; }
    if (op <= FEOP_ALIGNOF_UNARY) { return 14u; }
    if (op == FEOP_TERNARY_C) { return 3u; }
    return fast_expr_tok_prec((uint8_t)(op - FEOP_TOK_BASE));
}

static inline bool fast_expr_is_right_assoc(uint8_t k)
{
    return k == FTOK_QUESTION || (k >= FTOK_ASSIGN && k <= FTOK_SHR_ASSIGN);
}

// Emit SysV AMD64 `__builtin_va_arg(ap, type)` directly!
static __attribute__((noinline)) void fval_emit_va_arg(FastExprCtx* ex, FastVal* ap_val, FastType arg_t, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    arg_t = ftype_unqual(arg_t);
    if (ftype_is_unsupported(arg_t)) { fast_cc_bail(cc, "unsupported va_arg type"); return; }
    if (ex->eval_only > 0)
    {
        fval_release(ex, ap_val);
        *out = (FastVal){ .type = arg_t, .kind = FVAL_REG };
        return;
    }
    fval_flush_flags(ex);
    // `ap` decays to `struct __va_list_tag*`:
    //   [ap + 0]:  uint32_t gp_offset
    //   [ap + 4]:  uint32_t fp_offset
    //   [ap + 8]:  void*    overflow_arg_area
    //   [ap + 16]: void*    reg_save_area
    const uint32_t ap = fval_to_gp(ex, ap_val, true);
    const uint32_t ptr = fval_alloc_gp(ex);
    const uint32_t ofs = fval_alloc_gp(ex);
    const uint32_t lbl_ovf = fast_label_new(obj);
    const uint32_t lbl_done = fast_label_new(obj);

    if (ftype_is_float(arg_t))
    {
        // Check fp_offset <= 160 (48 + 7*16)
        x86_load(obj, ofs, ap, 4, 4u, false);
        x86_alu_ri(obj, false, 7u, ofs, 160);
        x86_jcc_label(obj, FCC_A, lbl_ovf);
        x86_load(obj, ptr, ap, 16, 8u, false); // reg_save_area
        x86_alu_rr(obj, true, 0x01u, ptr, ofs);
        x86_alu_ri(obj, false, 0u, ofs, 16);
        x86_store(obj, ap, 4, ofs, 4u);
        x86_jmp_label(obj, lbl_done);
        fast_label_bind(obj, lbl_ovf);
        x86_load(obj, ptr, ap, 8, 8u, false);  // overflow_arg_area
        x86_lea(obj, ofs, ptr, 8);
        x86_store(obj, ap, 8, ofs, 8u);
        fast_label_bind(obj, lbl_done);
        const uint32_t xr = fval_alloc_xmm(ex);
        x86_sse_load(obj, ftype_is_f64(arg_t), xr, ptr, 0);
        ex->gp_used &= (uint16_t)~((1u << ap) | (1u << ptr) | (1u << ofs));
        *out = (FastVal){ .type = arg_t, .kind = FVAL_XMM, .reg = (uint8_t)xr };
        return;
    }

    if (ftype_is_record(arg_t))
    {
        const FastComplexType* rct = cc->ctypes + (arg_t & BUSTER_FTYPE_CIDX_MASK);
        if (BUSTER_FAST_RECORD_OVERALIGNED(rct)) { fast_cc_bail(cc, "an aligned typedef's record by value"); }
        const int32_t rslot = fast_cc_alloc_tmp(cc, rct->size < 16u ? 16u : rct->size, rct->align < 8u ? 8u : rct->align);
        if (rct->abi_lo == FABI_MEM)
        {
            // Passed strictly in overflow_arg_area
            x86_load(obj, ptr, ap, 8, 8u, false);
            const uint32_t step = (rct->size + 7u) & ~7u;
            x86_lea(obj, ofs, ptr, (int32_t)step);
            x86_store(obj, ap, 8, ofs, 8u);
            ex->gp_used &= (uint16_t)~((1u << ap) | (1u << ofs));
            const uint32_t tmp_r = fval_alloc_gp(ex);
            x86_emit_memcpy_inline(obj, FREG_RBP, rslot, ptr, 0, rct->size, tmp_r);
            ex->gp_used &= (uint16_t)~((1u << ptr) | (1u << tmp_r));
            *out = (FastVal){ .type = arg_t, .kind = FVAL_LOCAL, .off = rslot };
            return;
        }
        // Small register struct (<= 16B) in va_arg -> bailout for rare mixed SSE/GP va_arg structs, or handle pure GP!
        if (rct->abi_lo == FABI_INT && (rct->abi_hi == FABI_NONE || rct->abi_hi == FABI_INT))
        {
            const uint32_t need = rct->abi_hi == FABI_INT ? 16u : 8u;
            x86_load(obj, ofs, ap, 0, 4u, false);
            x86_alu_ri(obj, false, 7u, ofs, (int32_t)(48u - need));
            x86_jcc_label(obj, FCC_A, lbl_ovf);
            x86_load(obj, ptr, ap, 16, 8u, false);
            x86_alu_rr(obj, true, 0x01u, ptr, ofs);
            x86_alu_ri(obj, false, 0u, ofs, (int32_t)need);
            x86_store(obj, ap, 0, ofs, 4u);
            x86_jmp_label(obj, lbl_done);
            fast_label_bind(obj, lbl_ovf);
            x86_load(obj, ptr, ap, 8, 8u, false);
            x86_lea(obj, ofs, ptr, (int32_t)need);
            x86_store(obj, ap, 8, ofs, 8u);
            fast_label_bind(obj, lbl_done);
            x86_load(obj, ofs, ptr, 0, 8u, false);
            x86_store(obj, FREG_RBP, rslot, ofs, 8u);
            if (need == 16u)
            {
                x86_load(obj, ofs, ptr, 8, 8u, false);
                x86_store(obj, FREG_RBP, rslot + 8, ofs, 8u);
            }
            ex->gp_used &= (uint16_t)~((1u << ap) | (1u << ptr) | (1u << ofs));
            *out = (FastVal){ .type = arg_t, .kind = FVAL_LOCAL, .off = rslot };
            return;
        }
        fast_cc_bail(cc, "complex SSE struct va_arg");
        return;
    }

    // Standard GP scalar / pointer va_arg!
    x86_load(obj, ofs, ap, 0, 4u, false);
    x86_alu_ri(obj, false, 7u, ofs, 40); // gp_offset <= 40
    x86_jcc_label(obj, FCC_A, lbl_ovf);
    x86_load(obj, ptr, ap, 16, 8u, false); // reg_save_area
    x86_alu_rr(obj, true, 0x01u, ptr, ofs);
    x86_alu_ri(obj, false, 0u, ofs, 8);
    x86_store(obj, ap, 0, ofs, 4u);
    x86_jmp_label(obj, lbl_done);
    fast_label_bind(obj, lbl_ovf);
    x86_load(obj, ptr, ap, 8, 8u, false); // overflow_arg_area
    x86_lea(obj, ofs, ptr, 8);
    x86_store(obj, ap, 8, ofs, 8u);
    fast_label_bind(obj, lbl_done);
    x86_load(obj, ptr, ptr, 0, ftype_size(cc, arg_t), ftype_is_signed(cc, arg_t));
    ex->gp_used &= (uint16_t)~((1u << ap) | (1u << ofs));
    *out = (FastVal){ .type = arg_t, .kind = FVAL_REG, .reg = (uint8_t)ptr };
}

// A constant condition's `?:` (#3592): the live arm is the value, in the common type and as an rvalue, as the branches'
// join in fast_expr_reduce_top makes it. A live then arm's value is under the dead else arm's placeholder. Out of
// line: few conditions are constants.
static __attribute__((cold, noinline)) void fast_expr_constant_ternary(FastExprCtx* ex, uint32_t constant, FastVal else_v, FastType common_t,
                                                                       uint8_t marks, uint32_t arms)
{
    FastCCState* cc = ex->cc;
    ex->eval_only = (uint8_t)(ex->eval_only - (constant == BUSTER_FCONSTANT_TRUE));
    FastVal live = else_v;
    bool ok = true;
    if (constant == BUSTER_FCONSTANT_TRUE)
    {
        ok = ex->vsp >= 1u;
        if (!ok) { fast_cc_bail(cc, "ternary stack underflow"); }
        else { live = ex->vstack[--ex->vsp]; }
    }
    // #3577: the marks and the arms' constants of the other path's value (both arms', which the full pipeline checks
    // whatever the condition).
    FastVal result = { .type = common_t, .kind = FVAL_VOID, .marks = marks };
    if (ok && ftype_is_void(common_t))
    {
        fval_release(ex, &live);
    }
    else if (ok && ftype_is_record(common_t))
    {
        const FastComplexType* rct = cc->ctypes + (common_t & BUSTER_FTYPE_CIDX_MASK);
        result.kind = FVAL_LOCAL;
        result.marks = (uint8_t)(marks & BUSTER_FVAL_MARK_EFFECT);
        result.off = ex->eval_only > 0 ? 0 : fast_cc_alloc_tmp(cc, rct->size, rct->align < 8u ? 8u : rct->align);
        FastVal dst = result;
        fval_store(ex, &dst, &live, false, NULL);
    }
    else if (ok)
    {
        fval_convert(ex, &live, common_t);
        const bool fp = ftype_is_float(common_t);
        result.kind = fp ? FVAL_XMM : FVAL_REG;
        result.reg = fp ? fval_to_xmm(ex, &live, true) : fval_to_gp(ex, &live, true);
        result.sym_idx = arms;
    }
    if (ok) { ex->vstack[ex->vsp++] = result; }
}

// A left side of `&&` or `||` that decided the value (#3592): the dead right side must still be a scalar, and the value
// is 0 for `&&` and 1 for `||`, an rvalue as the branches in fast_expr_reduce_top make it. Out of line, as above.
static __attribute__((cold, noinline)) void fast_expr_decided_logic(FastExprCtx* ex, uint8_t tok, const FastVal* rhs, uint8_t marks)
{
    ex->eval_only -= 1u;
    if (!ftype_is_scalar(ftype_decay(ex->cc, rhs->type)))
    {
        fast_cc_bail(ex->cc, "logical operand not scalar");
    }
    else
    {
        FastVal value = { .type = FTYPE_I32, .kind = FVAL_IMM, .imm = tok == FTOK_LOR };
        const uint32_t reg = fval_to_gp(ex, &value, true);
        ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = reg, .marks = marks };
    }
}

// Apply the top operator from `ops[--osp]` onto `ex->vstack`!
// Whether a value an expression discards (a statement's, a comma's left operand's) is one the full pipeline lets go
// quietly (#3577): one with an effect, or a volatile lvalue's read, and not a warn_unused_result call's. 1 or 0, from
// the value's marks, its lvalue bit and its type, computed in place (no call, no branch) where a value is discarded.
#define BUSTER_FAST_DISCARD_QUIET(v) \
    ((((uint32_t)(((v).marks & BUSTER_FVAL_MARK_EFFECT) != 0)) | \
      ((uint32_t)(v).is_lvalue & (uint32_t)(((v).type & BUSTER_FTYPE_VOL) != 0))) & \
     (uint32_t)(((v).marks & BUSTER_FVAL_MARK_UNUSED_RESULT) == 0))

static BUSTER_DIRECT_HOT void fast_expr_reduce_top(FastExprCtx* ex, FastExprOp* ops, uint32_t* osp_ptr)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    const FastExprOp cur = ops[--(*osp_ptr)];
    const uint16_t op = cur.op;

    if (op >= FEOP_UNARY_PLUS && op <= FEOP_ALIGNOF_UNARY)
    {
        if (ex->vsp < 1u) { fast_cc_bail(cc, "unary stack underflow"); return; }
        FastVal* v = ex->vstack + (ex->vsp - 1u);
        // A unary operator's value has none of its operand's marks (#3577): `-g()` discards a value with no effect.
        const uint8_t vmarks = v->marks;
        const uint8_t vlasting = v->lasting;
        const uint32_t from_float = (uint32_t)((v->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F32);
        v->marks = BUSTER_FVAL_MARK_NONLITERAL;
        v->op = 0;
        v->lasting = 0;
        // #3577: the operand C allows each, or the full pipeline's error or warning: + and - arithmetic, ~ an integer,
        // ! ++ -- a scalar; sizeof a complete type that isn't a bit-field's (void's and a function's are GNU's 1);
        // _Alignof of an expression is GNU's warning. Masks of the classes, by operator.
        {
            static const uint8_t unary_operands[32] = {
                [FEOP_UNARY_PLUS] = 3u, [FEOP_UNARY_MINUS] = 3u, [FEOP_UNARY_TILDE] = 1u, [FEOP_UNARY_BANG] = 7u,
                [FEOP_DEREF] = 15u, [FEOP_ADDR] = 15u, [FEOP_PRE_INC] = 7u, [FEOP_PRE_DEC] = 7u, [FEOP_CAST] = 15u,
                [FEOP_SIZEOF_UNARY] = 15u, [FEOP_ALIGNOF_UNARY] = 0u,
            };
            // (An operator that takes every class, or an integer scalar where integers are taken, is quiet without the
            // class: sizeof aside, which measures.)
            const uint32_t allowed = unary_operands[op & 31u];
            const uint32_t integer_scalar = (uint32_t)((v->type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0) &
                                            (BUSTER_FAST_INTEGER_KINDS >> (v->type & BUSTER_FTYPE_SCALAR_MASK));
            if (((uint32_t)(op == FEOP_SIZEOF_UNARY) | ((uint32_t)(allowed != 15u) & ((integer_scalar & allowed & 1u) ^ 1u))) != 0)
            {
                const uint32_t operand = BUSTER_FAST_OPERAND_CLASS(cc, v->type);
                const uint32_t sized = (uint32_t)(op == FEOP_SIZEOF_UNARY) & (BUSTER_FAST_TYPE_INCOMPLETE(cc, v->type) | (uint32_t)(v->kind == FVAL_BITFIELD));
                if ((((allowed >> operand) & 1u) == 0) | sized)
                {
                    fast_cc_bail(cc, "operand of a unary operator");
                    return;
                }
            }
        }
        if (op == FEOP_SIZEOF_UNARY || op == FEOP_ALIGNOF_UNARY)
        {
            const uint64_t val = op == FEOP_SIZEOF_UNARY ? ftype_size(cc, v->type) : ftype_align(cc, v->type);
            if (ex->eval_only > 0) { ex->eval_only -= 1u; }
            if (ex->unevaluated > 0) { ex->unevaluated -= 1u; }
            *v = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)val, .marks = BUSTER_FVAL_MARK_NONLITERAL };
            return;
        }
        if (op == FEOP_CAST)
        {
            // #3577: a cast to void, or between scalars but a floating value and a pointer, or the full pipeline's error.
            {
                const uint32_t from = BUSTER_FAST_OPERAND_CLASS(cc, v->type);
                const uint32_t to = BUSTER_FAST_OPERAND_CLASS(cc, cur.type);
                const uint32_t to_void = (uint32_t)((cur.type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
                const uint32_t pair = from * 4u + to;
                // Allowed pairs: integer, floating and pointer among themselves but floating with pointer.
                const uint32_t allowed = (UINT32_C(0x0537) >> pair) & 1u;
                if ((allowed | to_void) == 0)
                {
                    fast_cc_bail(cc, "cast the full pipeline refuses");
                    return;
                }
            }
            fval_convert(ex, v, cur.type);
            v->is_lvalue = 0;
            v->lasting = (uint8_t)((BUSTER_FVAL_LASTING_FLOAT_CAST *
                                    (from_float & (uint32_t)((cur.type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_F64))) |
                                   (vlasting & BUSTER_FVAL_LASTING_STRING));
            // A cast to void is an effect a statement discards quietly; a discarded warn_unused_result call stays one.
            const uint32_t to_void = (uint32_t)((cur.type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
            v->marks = (uint8_t)(((BUSTER_FVAL_MARK_EFFECT | (vmarks & BUSTER_FVAL_MARK_UNUSED_RESULT)) * to_void) |
                                 (BUSTER_FVAL_MARK_NONLITERAL * (to_void ^ 1u)));
            return;
        }
        if (op == FEOP_ADDR)
        {
            if (v->kind == FVAL_BITFIELD) { fast_cc_bail(cc, "address of bitfield"); return; }
            if ((vmarks & BUSTER_FVAL_MARK_REGISTER) != 0) { fast_cc_bail(cc, "address of register variable"); return; }
            const FastType pt = ftype_make_ptr(cc, v->type);
            // An object's address keeps its kind without code too (#3592's dead branches): a comparison's or a
            // condition's check reads it (-Wtautological-pointer-compare, -Wpointer-bool-conversion), and it makes none.
            if (v->kind == FVAL_LOCAL) { v->kind = FVAL_LOCAL_ADDR; v->type = pt; v->is_lvalue = 0; return; }
            if (v->kind == FVAL_GLOBAL) { v->kind = FVAL_GLOBAL_ADDR; v->type = pt; v->is_lvalue = 0; return; }
            if (ex->eval_only > 0) { *v = (FastVal){ .type = pt, .kind = FVAL_REG }; return; }
            const uint32_t r = fval_addr_to_gp(ex, v, true);
            *v = (FastVal){ .type = pt, .kind = FVAL_REG, .reg = (uint8_t)r };
            return;
        }
        if (op == FEOP_DEREF)
        {
            const FastType dt = ftype_decay(cc, v->type);
            if (!ftype_is_ptr(dt)) { fast_cc_bail(cc, "deref non-pointer"); return; }
            const FastType elem = ftype_deref(cc, dt);
            if (ex->eval_only > 0) { *v = (FastVal){ .type = elem, .kind = FVAL_INDIRECT, .is_lvalue = 1u }; return; }
            // #3577: `*` of a null pointer constant whose result isn't volatile is the full pipeline's -Wnull-dereference
            // ("indirection of non-volatile null pointer will be deleted, not trap").
            const uint32_t elem_volatile = (uint32_t)((elem & BUSTER_FTYPE_VOL) != 0);
            if (((uint32_t)(v->kind == FVAL_IMM) & (uint32_t)(v->imm == 0) & (elem_volatile ^ 1u)) != 0)
            {
                fast_cc_bail(cc, "indirection of a null pointer constant");
                return;
            }
            if (ftype_is_func(elem))
            {
                // `(*direct_func)` stays direct; `(*fn_ptr_lvalue)` MUST load the pointer from memory!
                if (v->kind == FVAL_GLOBAL && v->is_func_sym)
                {
                    v->type = elem;
                    return;
                }
                const uint32_t r = fval_to_gp(ex, v, true);
                *v = (FastVal){ .type = elem, .kind = FVAL_REG, .reg = (uint8_t)r };
                return;
            }
            const uint32_t r = fval_to_gp(ex, v, true);
            *v = (FastVal){ .type = elem, .kind = FVAL_INDIRECT, .reg = (uint8_t)r, .off = 0, .is_lvalue = 1u };
            return;
        }
        if (op == FEOP_PRE_INC || op == FEOP_PRE_DEC)
        {
            const uint32_t seq_result = BUSTER_FSEQ_RESULT(v);
            FastVal res = { 0 };
            fval_emit_incdec(ex, v, op == FEOP_PRE_INC, false, &res);
            *v = res;
            v->marks = BUSTER_FVAL_MARK_EFFECT;
            v->seq_ident = seq_result;
            return;
        }
        if (op == FEOP_UNARY_PLUS)
        {
            const FastType pt = ftype_is_float(v->type) ? ftype_unqual(v->type) : ftype_promote_int(cc, v->type);
            fval_convert(ex, v, pt);
            v->is_lvalue = 0;
            return;
        }
        if (op == FEOP_UNARY_MINUS)
        {
            const FastType pt = ftype_is_float(v->type) ? ftype_unqual(v->type) : ftype_promote_int(cc, v->type);
            if (ex->eval_only > 0) { *v = (FastVal){ .type = pt, .kind = FVAL_REG }; return; }
            fval_flush_flags(ex);
            fval_convert(ex, v, pt);
            if (ftype_is_float(pt))
            {
                if (v->kind == FVAL_FIMM)
                {
                    v->imm ^= ftype_is_f64(pt) ? (int64_t)UINT64_C(0x8000000000000000) : (int64_t)UINT32_C(0x80000000);
                    return;
                }
                const bool is_f64 = ftype_is_f64(pt);
                const uint32_t x = fval_to_xmm(ex, v, true);
                const uint32_t ro = is_f64 ? fast_cc_get_sign_f64(cc) : fast_cc_get_sign_f32(cc);
                x86_sse_xor_rip(obj, is_f64, x, obj->sec_sym_rodata, (int64_t)ro);
                *v = (FastVal){ .type = pt, .kind = FVAL_XMM, .reg = (uint8_t)x };
                return;
            }
            if (v->kind == FVAL_IMM)
            {
                const bool w64 = ftype_size(cc, pt) == 8u;
                // #3577: negating a signed type's minimum overflows (-Winteger-overflow); the negation below is then
                // defined.
                if (ftype_is_signed(cc, pt) && (w64 ? v->imm == INT64_MIN : (int32_t)v->imm == INT32_MIN))
                {
                    fast_cc_bail(cc, "signed overflow in a constant");
                    return;
                }
                v->imm = w64 ? (int64_t)(UINT64_C(0) - (uint64_t)v->imm) :
                         (ftype_is_signed(cc, pt) ? (int64_t)(-(int32_t)v->imm) : (int64_t)(uint64_t)(-(uint32_t)v->imm));
                return;
            }
            const uint32_t r = fval_to_gp(ex, v, true);
            x86_f7(obj, ftype_size(cc, pt) == 8u, 3u, r); // neg r
            *v = (FastVal){ .type = pt, .kind = FVAL_REG, .reg = (uint8_t)r };
            return;
        }
        if (op == FEOP_UNARY_TILDE)
        {
            const FastType pt = ftype_promote_int(cc, v->type);
            if (ex->eval_only > 0) { *v = (FastVal){ .type = pt, .kind = FVAL_REG }; return; }
            fval_flush_flags(ex);
            fval_convert(ex, v, pt);
            if (v->kind == FVAL_IMM)
            {
                const bool w64 = ftype_size(cc, pt) == 8u;
                v->imm = w64 ? ~v->imm : (ftype_is_signed(cc, pt) ? (int64_t)(~(int32_t)v->imm) : (int64_t)(uint64_t)(~(uint32_t)v->imm));
                return;
            }
            const uint32_t r = fval_to_gp(ex, v, true);
            x86_f7(obj, ftype_size(cc, pt) == 8u, 2u, r); // not r
            *v = (FastVal){ .type = pt, .kind = FVAL_REG, .reg = (uint8_t)r };
            return;
        }
        if (op == FEOP_UNARY_BANG)
        {
            // Each value `!` makes is FTOK_BANG's, for -Wlogical-not-parentheses (`!a == b`, #3577); its operand is a
            // condition.
            v->marks = vmarks;
            if (!BUSTER_FAST_CONDITION_QUIET(cc, v)) { fast_cc_bail(cc, "condition the full pipeline refuses or questions"); return; }
            v->marks = BUSTER_FVAL_MARK_NONLITERAL;
            if (ex->eval_only > 0) { *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .op = FTOK_BANG }; return; }
            if (v->kind == FVAL_FLAGS)
            {
                v->cc_code ^= 1u; // invert condition code in 0 instructions!!
                v->type = FTYPE_I32;
                v->op = FTOK_BANG;
                return;
            }
            if (v->kind == FVAL_IMM)
            {
                *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = v->imm == 0 ? 1 : 0, .marks = BUSTER_FVAL_MARK_NONLITERAL, .op = FTOK_BANG };
                return;
            }
            const FastType t = ftype_decay(cc, v->type);
            if (ftype_is_float(t))
            {
                const bool is_f64 = ftype_is_f64(t);
                const uint32_t x = fval_to_xmm(ex, v, true);
                const uint32_t xz = fval_alloc_xmm(ex);
                const uint32_t r1 = fval_alloc_gp(ex);
                const uint32_t r2 = fval_alloc_gp(ex);
                x86_sse_zero(obj, xz);
                x86_ucomis(obj, is_f64, x, xz);
                x86_setcc_zext(obj, FCC_E, r1);
                x86_setcc_zext(obj, FCC_NP, r2);
                x86_alu_rr(obj, false, 0x21u, r1, r2);
                ex->gp_used &= (uint16_t)~(1u << r2);
                ex->xmm_used &= (uint8_t)~((1u << x) | (1u << xz));
                *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = (uint8_t)r1, .op = FTOK_BANG };
                return;
            }
            const uint32_t r = fval_to_gp(ex, v, true);
            const uint32_t sz = ftype_size(cc, t);
            if (sz < 4u) { x86_extend(obj, r, r, sz, false); }
            x86_alu_rr(obj, sz == 8u, 0x85u, r, r);
            ex->gp_used &= (uint16_t)~(1u << r);
            *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_FLAGS, .cc_code = FCC_E, .op = FTOK_BANG };
            return;
        }
        return;
    }

    // Ternary `?:` completion (`FEOP_TERNARY_C`)
    if (op == FEOP_TERNARY_C)
    {
        if (ex->vsp < 1u) { fast_cc_bail(cc, "ternary stack underflow"); return; }
        FastVal else_v = ex->vstack[--ex->vsp];
        // #3577: the condition's entries are sequenced before the arms', and one arm's are never evaluated with the other's.
        BUSTER_FSEQ_REGION(ex, cur.seq_lo, cur.seq_mid, BUSTER_FSEQ_MARK(ex));
        BUSTER_FSEQ_REGION(ex, cur.seq_mid, cur.seq_mid2, BUSTER_FSEQ_MARK(ex));
        const FastType then_t = cur.type;
        // The arms' constants together (BUSTER_FVAL_MARK_CONST_ARMS), and an effect where either arm has one.
        const FastArm then_arm = {
            .value = (int64_t)(((uint64_t)(uint32_t)cur.slot << 32) | cur.lbl0),
            .kind = (uint8_t)((cur.extra >> 8) & 15u),
            .not_null_literal = (uint8_t)((cur.extra >> 12) & 1u),
        };
        FastArm arms = fast_cc_arms_merge(then_arm, fast_cc_arm(cc, &else_v));
        const int64_t tarms = arms.value;
        const FastType else_t = ftype_decay(cc, else_v.type);
        FastType common_t = FTYPE_VOID;
        if (ftype_is_void(then_t) || ftype_is_void(else_t)) { common_t = FTYPE_VOID; }
        else if (ftype_is_record(then_t)) { common_t = then_t; }
        else if (ftype_is_ptr(then_t) && ftype_is_ptr(else_t))
        {
            // Prefer non-void* pointer if the other is void*
            common_t = ftype_is_void(ftype_deref(cc, then_t)) ? else_t : then_t;
        }
        else if (ftype_is_ptr(then_t)) { common_t = then_t; }
        else if (ftype_is_ptr(else_t)) { common_t = else_t; }
        else { common_t = ftype_usual_arith(cc, then_t, else_t); }
        // A pointer conditional's integer arm is its null pointer: the literal 0 alone is quiet, and the value holds no
        // constant arms an implicit conversion would check.
        if (ftype_is_ptr(common_t))
        {
            if (arms.kind != FARMS_NONE && arms.not_null_literal != 0) { fast_cc_bail(cc, "conditional's integer arm as a pointer"); return; }
            arms.kind = FARMS_NONE;
        }
        const uint8_t tmarks = (uint8_t)(((cur.extra | else_v.marks) & BUSTER_FVAL_MARK_EFFECT) |
                                         (BUSTER_FVAL_MARK_CONST_ARMS * (uint32_t)(arms.kind != FARMS_NONE)));
        const uint32_t tkind = arms.kind;

        if (cur.constant != BUSTER_FCONSTANT_NONE)
        {
            fast_expr_constant_ternary(ex, cur.constant, else_v, common_t, tmarks, BUSTER_FARMS_PACK(tkind, tarms));
            return;
        }

        if (ex->eval_only > 0)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = ftype_is_void(common_t) ? FVAL_VOID : FVAL_REG, .sym_idx = BUSTER_FARMS_PACK(tkind, tarms),
                                               .marks = tmarks };
            return;
        }
        if (ftype_is_void(common_t))
        {
            (void)tarms;
            fval_release(ex, &else_v);
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            fast_label_bind(obj, cur.lbl2);
            ex->gp_used = 0;
            ex->xmm_used = 0;
            ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID, .marks = tmarks };
            return;
        }
        if (ftype_is_record(common_t))
        {
            FastVal dst = { .type = common_t, .kind = FVAL_LOCAL, .off = cur.slot };
            fval_store(ex, &dst, &else_v, false, NULL);
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            fast_label_bind(obj, cur.lbl2);
            ex->gp_used = 0;
            ex->xmm_used = 0;
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_LOCAL, .off = cur.slot,
                                               .marks = (uint8_t)(tmarks & BUSTER_FVAL_MARK_EFFECT) };
            return;
        }
        // Scalar/float common type: put `else` in RAX/XMM0, jump to end, then convert `then` in tail trampoline if needed!
        fval_convert(ex, &else_v, common_t);
        if (ftype_is_float(common_t))
        {
            const uint32_t x = fval_to_xmm(ex, &else_v, true);
            if (x != 0u) { x86_sse_rr(obj, ftype_is_f64(common_t), 0x10u, 0u, x); }
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            if (BUSTER_FTYPE_CODEGEN(ftype_unqual(then_t)) != BUSTER_FTYPE_CODEGEN(ftype_unqual(common_t)))
            {
                ex->gp_used = ftype_is_float(then_t) ? 0u : (uint16_t)(1u << FREG_RAX);
                ex->xmm_used = ftype_is_float(then_t) ? 1u : 0u;
                FastVal tv = { .type = then_t, .kind = ftype_is_float(then_t) ? FVAL_XMM : FVAL_REG, .reg = 0u };
                fval_convert(ex, &tv, common_t);
                const uint32_t tx = fval_to_xmm(ex, &tv, true);
                if (tx != 0u) { x86_sse_rr(obj, ftype_is_f64(common_t), 0x10u, 0u, tx); }
            }
            fast_label_bind(obj, cur.lbl2);
            ex->gp_used = 0;
            ex->xmm_used = 1u;
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_XMM, .reg = 0u, .sym_idx = BUSTER_FARMS_PACK(tkind, tarms), .marks = tmarks };
        }
        else
        {
            const uint32_t g = fval_to_gp(ex, &else_v, true);
            if (g != FREG_RAX) { x86_mov_rr(obj, true, FREG_RAX, g); }
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            if (BUSTER_FTYPE_CODEGEN(ftype_unqual(then_t)) != BUSTER_FTYPE_CODEGEN(ftype_unqual(common_t)))
            {
                ex->gp_used = (uint16_t)(1u << FREG_RAX);
                ex->xmm_used = 0u;
                FastVal tv = { .type = then_t, .kind = FVAL_REG, .reg = FREG_RAX };
                fval_convert(ex, &tv, common_t);
                const uint32_t tg = fval_to_gp(ex, &tv, true);
                if (tg != FREG_RAX) { x86_mov_rr(obj, true, FREG_RAX, tg); }
            }
            fast_label_bind(obj, cur.lbl2);
            ex->gp_used = (uint16_t)(1u << FREG_RAX);
            ex->xmm_used = 0;
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_REG, .reg = FREG_RAX, .sym_idx = BUSTER_FARMS_PACK(tkind, tarms), .marks = tmarks };
        }
        return;
    }

    // Binary / logical / assignment / comma operators
    const uint8_t tok = (uint8_t)(op - FEOP_TOK_BASE);
    if (tok == FTOK_LAND || tok == FTOK_LOR)
    {
        if (ex->vsp < 1u) { fast_cc_bail(cc, "logical stack underflow"); return; }
        FastVal rhs = ex->vstack[--ex->vsp];
        // #3577: the left operand's entries are sequenced before the right's.
        BUSTER_FSEQ_REGION(ex, cur.seq_lo, cur.seq_mid, BUSTER_FSEQ_MARK(ex));
        const uint8_t lmarks = (uint8_t)(rhs.marks & BUSTER_FVAL_MARK_EFFECT);
        // #3577: a right operand that is an integer constant but 0 or 1 is -Wconstant-logical-operand's; and the
        // operand is a condition (fval_branch checks it where code is made).
        {
            const uint32_t constant = (uint32_t)(rhs.kind == FVAL_IMM) & (uint32_t)(BUSTER_FAST_OPERAND_CLASS(cc, rhs.type) == BUSTER_FOPERAND_INTEGER) &
                                      (uint32_t)((rhs.type & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) != FTYPE_BOOL) &
                                      (uint32_t)((uint64_t)rhs.imm > 1u);
            if ((constant | (uint32_t)(BUSTER_FAST_CONDITION_QUIET(cc, &rhs) == 0)) != 0)
            {
                fast_cc_bail(cc, "logical operator's operand the full pipeline questions");
                return;
            }
        }
        if (cur.constant != 0)
        {
            fast_expr_decided_logic(ex, tok, &rhs, lmarks);
            return;
        }
        if (ex->eval_only > 0)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .marks = lmarks };
            return;
        }
        const uint32_t end_lbl = fast_label_new(obj);
        if (tok == FTOK_LAND)
        {
            fval_branch(ex, &rhs, false, cur.lbl0);
            x86_mov_ri(obj, false, FREG_RAX, 1u);
            x86_jmp_label(obj, end_lbl);
            fast_label_bind(obj, cur.lbl0);
            x86_mov_ri(obj, false, FREG_RAX, 0u);
            fast_label_bind(obj, end_lbl);
        }
        else
        {
            fval_branch(ex, &rhs, true, cur.lbl0);
            x86_mov_ri(obj, false, FREG_RAX, 0u);
            x86_jmp_label(obj, end_lbl);
            fast_label_bind(obj, cur.lbl0);
            x86_mov_ri(obj, false, FREG_RAX, 1u);
            fast_label_bind(obj, end_lbl);
        }
        ex->gp_used = (uint16_t)(1u << FREG_RAX);
        ex->xmm_used = 0;
        ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = FREG_RAX, .marks = lmarks };
        return;
    }

    if (ex->vsp < 2u) { fast_cc_bail(cc, "binary stack underflow"); return; }
    // The operands where they stand, above the stack's new top: a copy of a value just written a field at a time
    // waits for its stores (#2850).
    FastVal* const rhs = ex->vstack + (ex->vsp - 1u);
    FastVal* const lhs = ex->vstack + (ex->vsp - 2u);
    ex->vsp -= 2u;

    if (tok == FTOK_COMMA)
    {
        // The left operand is discarded: one with no effect is -Wunused-value (BUSTER_FAST_DISCARD_QUIET), which the full
        // pipeline doesn't give in an unevaluated operand (sizeof's).
        if (((uint32_t)(ex->unevaluated == 0) & (uint32_t)(BUSTER_FAST_DISCARD_QUIET(*lhs) == 0)) != 0) { fast_cc_bail(cc, "comma's left operand has no effect"); return; }
        fval_release(ex, lhs);
        // #3577: the left operand's entries are sequenced before the right's.
        BUSTER_FSEQ_REGION(ex, cur.seq_lo, cur.seq_mid, BUSTER_FSEQ_MARK(ex));
        rhs->seq_start = 0;
        rhs->seq_ident = 0;
        ex->vstack[ex->vsp++] = *rhs;
        return;
    }
    if (tok == FTOK_ASSIGN)
    {
        // #3577: a struct or union with a const member can't be assigned either (its record looked at where the lvalue is
        // a complex type that isn't a pointer).
        uint32_t record_const = 0;
        if (((uint32_t)(lhs->type >> 31) & (uint32_t)((lhs->type & BUSTER_FTYPE_PTR_MASK) == 0)) != 0)
        {
            record_const = (uint32_t)cc->ctypes[lhs->type & BUSTER_FTYPE_CIDX_MASK].has_const_mbr;
        }
        const uint32_t lhs_const = (uint32_t)((lhs->type & BUSTER_FTYPE_CONST) != 0);
        // And one in an unevaluated operand (sizeof's) is the full pipeline's -Wunevaluated-expression.
        if (((uint32_t)(lhs->is_lvalue == 0) | lhs_const | record_const | (uint32_t)(ex->unevaluated > 0)) != 0)
        {
            fast_cc_bail(cc, "assign to const or non-lvalue, or in an unevaluated operand");
            return;
        }
        const uint32_t seq_result = BUSTER_FSEQ_RESULT(lhs);
        BUSTER_FSEQ_MODIFY(ex, lhs)
        FastVal res = { 0 };
        fval_store(ex, lhs, rhs, true, &res);
        res.marks = BUSTER_FVAL_MARK_EFFECT;
        res.op = FTOK_ASSIGN;
        res.seq_ident = seq_result;
        ex->vstack[ex->vsp++] = res;
        return;
    }
    if (tok >= FTOK_ADD_ASSIGN && tok <= FTOK_SHR_ASSIGN)
    {
        const uint32_t seq_result = BUSTER_FSEQ_RESULT(lhs);
        FastVal res = { 0 };
        fval_emit_compound_assign(ex, tok, lhs, rhs, &res);
        res.marks = BUSTER_FVAL_MARK_EFFECT;
        res.op = tok;
        res.seq_ident = seq_result;
        ex->vstack[ex->vsp++] = res;
        return;
    }
    // #3577: an operand whose own operator C's precedence would question (-Wparentheses' family): a shift's `+` or
    // `-`, a bitwise operator's comparison, a relational operator's (a chained comparison), and a comparison's left
    // `!` (-Wlogical-not-parentheses). One table look-up a side, no branch while none is found.
    const uint32_t lclass = fast_expr_operand_class[lhs->op & 127u];
    const uint32_t rclass = fast_expr_operand_class[rhs->op & 127u];
    if ((((lclass | rclass) & fast_expr_conflicts_any[tok & 127u]) | (lclass & fast_expr_conflicts_left[tok & 127u])) != 0)
    {
        fast_cc_bail(cc, "operator precedence the full pipeline questions");
        return;
    }
    // #3577: a `+` with an unparenthesized string or character literal operand (op FTOK_STR_LIT or FTOK_CHAR_LIT) is
    // checked for -Wstring-plus-int and -Wstring-plus-char; one compare a side while there is none.
    const uint32_t literal_operand = (uint32_t)(((uint32_t)lhs->op - (uint32_t)FTOK_CHAR_LIT) < 2u) | (uint32_t)(((uint32_t)rhs->op - (uint32_t)FTOK_CHAR_LIT) < 2u);
    if ((literal_operand & (uint32_t)(tok == FTOK_PLUS)) != 0)
    {
        if (fast_cc_string_plus(cc, lhs, rhs) != 0)
        {
            fast_cc_bail(cc, "a string literal plus an integer, or a character literal plus a string");
            return;
        }
    }
    ex->cmp_pos = cur.lbl2;
    // The result where the left operand stands, which is where it goes. fval_emit_binary writes it once, as the last
    // thing it does, or bails out. A result in a local of this function was 40 cycles of waiting: the local is zeroed
    // through the stack pointer, written through fval_emit_binary's pointer and loaded through the stack pointer,
    // and the load is paired with the wrong store (#2850's profile on the 9700X: half of this function's cycles).
    fval_emit_binary(ex, tok, lhs, rhs, lhs);
    // #3577: the result's marks (a constant `<<`'s stays for the conditions that ask) and its operator.
    lhs->marks = (uint8_t)(BUSTER_FVAL_MARK_NONLITERAL | (lhs->marks & BUSTER_FVAL_MARK_SHIFTED));
    lhs->op = tok;
    ex->vsp += 1u;
}

// #3577: whether an expression's log (FastExprCtx's seq_*) is one the full pipeline's -Wunsequenced is quiet on: each
// modification's variable named or modified elsewhere only where a region sequences the two, or, a naming, within the
// modification's own operands. More entries than the log holds are refused; past the regions it keeps, fewer are apart.
// Entries are bits of 64-bit masks: the log's lanes compared at once, and each region's bounds a mask.
static __attribute__((cold, noinline)) bool fast_cc_sequenced(const FastExprCtx* ex)
{
    const uint32_t count = ex->seq_count;
    const uint32_t regions = ex->seq_regions < 32u ? ex->seq_regions : 32u;
    const uint64_t valid = _bzhi_u64(~UINT64_C(0), count);
    uint64_t pending = ex->seq_mods & valid;
    uint64_t conflicts = (uint64_t)(count > 64u);
    while (pending != 0 && conflicts == 0)
    {
        const uint32_t m = (uint32_t)__builtin_ctzll(pending);
        pending &= pending - 1u;
        const __m512i wanted = _mm512_set1_epi64((long long)ex->seq_log[m]);
#define BUSTER_FSEQ_SAME(chunk) \
    ((uint64_t)_mm512_mask_cmpeq_epi64_mask((__mmask8)(valid >> ((chunk) * 8u)), \
                                            _mm512_maskz_loadu_epi64((__mmask8)(valid >> ((chunk) * 8u)), ex->seq_log + (chunk) * 8u), wanted) << ((chunk) * 8u))
        uint64_t same = BUSTER_FSEQ_SAME(0) | BUSTER_FSEQ_SAME(1) | BUSTER_FSEQ_SAME(2) | BUSTER_FSEQ_SAME(3) |
                        BUSTER_FSEQ_SAME(4) | BUSTER_FSEQ_SAME(5) | BUSTER_FSEQ_SAME(6) | BUSTER_FSEQ_SAME(7);
#undef BUSTER_FSEQ_SAME
        same &= ~(UINT64_C(1) << m);
        // Namings within the modification's own operands, [own - 1, m).
        const uint32_t own = ex->seq_own[m];
        const uint64_t operands = (_bzhi_u64(~UINT64_C(0), m) & ~_bzhi_u64(~UINT64_C(0), own - (uint32_t)(own != 0))) & (UINT64_C(0) - (uint64_t)(own != 0));
        same &= ~(operands & ~ex->seq_mods);
        // A region's other side: [mid, hi) where the modification is in [lo, mid), and the reverse.
        for (uint32_t r = 0; r < regions; r += 1)
        {
            const uint32_t lo = ex->seq_region[r][0];
            const uint32_t mid = ex->seq_region[r][1];
            const uint32_t hi = ex->seq_region[r][2];
            const uint32_t known = (uint32_t)(lo != 0) & (uint32_t)(mid != 0) & (uint32_t)(hi != 0);
            const uint64_t left = _bzhi_u64(~UINT64_C(0), mid - known) & ~_bzhi_u64(~UINT64_C(0), lo - known);
            const uint64_t right = _bzhi_u64(~UINT64_C(0), hi - known) & ~_bzhi_u64(~UINT64_C(0), mid - known);
            const uint64_t in_left = UINT64_C(0) - ((left >> m) & 1u);
            const uint64_t in_right = UINT64_C(0) - ((right >> m) & 1u);
            same &= ~(((right & in_left) | (left & in_right)) & (UINT64_C(0) - (uint64_t)known));
        }
        conflicts |= same;
    }
    return conflicts == 0;
}

// #3577: whether sizeof's operand, at the current token, is a parameter declared an array by name, in up to two
// parentheses (FastSym's pad_sym[0]); deeper ones are taken as one.
static __attribute__((cold, noinline)) bool fast_cc_sizeof_array_param(const FastCCState* cc)
{
    uint32_t at = cc->pos;
    uint32_t depth = 0;
    while (at < cc->tcount && cc->tk[at] == FTOK_LPAREN)
    {
        at += 1u;
        depth += 1u;
    }
    bool result = false;
    if (at < cc->tcount && cc->tk[at] == FTOK_IDENT)
    {
        const uint32_t id = cc->td[at];
        const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
        result = si != 0 && cc->syms[si].kind == FSYM_LOCAL && cc->syms[si].pad_sym[0] != 0;
        // The name alone: its parentheses close, and no postfix operator follows.
        uint32_t closing = at + 1u;
        while (result && depth > 0 && closing < cc->tcount && cc->tk[closing] == FTOK_RPAREN)
        {
            closing += 1u;
            depth -= 1u;
        }
        const uint8_t next = closing < cc->tcount ? cc->tk[closing] : FTOK_SEMI;
        result = result && depth == 0 && next != FTOK_LBRACKET && next != FTOK_LPAREN && next != FTOK_DOT && next != FTOK_ARROW &&
                 next != FTOK_INC && next != FTOK_DEC;
    }
    return result;
}

static __attribute__((cold, noinline)) void fast_cc_expr_logged(FastCCState* cc, bool stop_at_comma);

// #3577: the expression that ends at cc->pos compiled again from its first token (START) for its -Wunsequenced log, as a
// compile with the log makes it: without code (eval_only; its unevaluated operands are counted apart); its end
// bails where the full pipeline warns. The types it makes (a string literal's array) are dropped after. One that
// declares anything (a tag, an enumerator, a member, a parameter: its first compile declared it already, and the
// second's types are dropped) is the full pipeline's, as is one that ends elsewhere than the first compile. False
// where it bailed.
static __attribute__((cold, noinline)) bool fast_cc_sequenced_again(FastCCState* cc, uint32_t start, bool stop_at_comma)
{
    const uint32_t end = cc->pos;
    const uint32_t ctypes = cc->ctype_count;
    const uint32_t declared = cc->sym_count + cc->undo_count + cc->member_count + cc->param_type_count + cc->param_name_count;
    cc->pos = start;
    fast_cc_expr_logged(cc, stop_at_comma);
    if (!cc->bailout && cc->sym_count + cc->undo_count + cc->member_count + cc->param_type_count + cc->param_name_count != declared)
    {
        fast_cc_bail(cc, "an expression compiled again for -Wunsequenced declares something");
    }
    if (!cc->bailout && cc->pos != end)
    {
        fast_cc_bail(cc, "an expression compiled again for -Wunsequenced ends elsewhere");
    }
    cc->ctype_count = ctypes;
    cc->pos = end;
    return !cc->bailout;
}

// #3577: -Wunsequenced for an expression compiled without the log that modified variables (EX's seq_mod_*) where the one
// modification isn't its last operator: each modified variable's name once among the expression's tokens, and none
// modified twice, is quiet (a warning needs another use or modification of the variable); else the expression is compiled
// again for its log (fast_cc_sequenced_again). The tokens 64 at a time, in masks of the names and their identifiers.
// False where it bailed.
static __attribute__((cold, noinline)) bool fast_cc_sequenced_lazy(FastCCState* cc, const FastExprCtx* ex, bool stop_at_comma)
{
    const uint32_t count = ex->seq_mod_count;
    const uint32_t start = ex->seq_start_pos;
    const uint32_t end = cc->pos;
    uint32_t once = (uint32_t)(count <= 4u);
    for (uint32_t m = 0; m < count && m < 4u && once != 0; m += 1)
    {
        const uint32_t id = ex->seq_mod_ids[m];
        const __m512i wanted = _mm512_set1_epi32((int)id);
        const __m512i name = _mm512_set1_epi8((char)FTOK_IDENT);
        uint32_t named = 0;
        for (uint32_t at = start; at < end; at += 64u)
        {
            const uint32_t left = end - at;
            const __mmask64 valid = _bzhi_u64(~UINT64_C(0), left < 64u ? left : 64u);
            const __mmask64 names = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, cc->tk + at), name);
            uint64_t same = 0;
            for (uint32_t part = 0; part < 4u; part += 1)
            {
                const __mmask16 lanes = (__mmask16)(valid >> (part * 16u));
                same |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, cc->td + at + part * 16u), wanted)
                        << (part * 16u);
            }
            named += (uint32_t)__builtin_popcountll(names & same);
        }
        once &= (uint32_t)(named == 1u);
        for (uint32_t earlier = 0; earlier < m; earlier += 1)
        {
            once &= (uint32_t)(ex->seq_mod_ids[earlier] != id);
        }
    }
    bool quiet = once != 0;
    if (!quiet)
    {
        quiet = fast_cc_sequenced_again(cc, start, stop_at_comma);
    }
    return quiet;
}

// Push a use of a symbol with use flags (#3577): a register variable as any local, marked so that `&` refuses it; a
// function whose calls the full pipeline checks as any function, marked for a call by its name (nonnull,
// warn_unused_result), where a format function's call is the full pipeline's, and so is a name in parentheses, which
// a call may follow; and anything else (an unsupported type, a deprecated declaration) is the full pipeline's too.
static __attribute__((cold, noinline)) bool fast_cc_push_flagged_symbol(FastCCState* cc, FastExprCtx* ex, FastSym* sym, uint32_t si)
{
    const uint8_t flags = sym->use_flags;
    const uint8_t next = fast_cc_peek(cc);
    bool pushed = false;
    if ((flags & BUSTER_FSYM_UNSUPPORTED) != 0)
    {
        fast_cc_bail(cc, "use of unsupported-type symbol");
    }
    else if ((flags & BUSTER_FSYM_DEPRECATED) != 0)
    {
        fast_cc_bail(cc, "use of a deprecated declaration");
    }
    else if (flags == BUSTER_FSYM_REGISTER && sym->kind == FSYM_LOCAL)
    {
        if (ex->vsp < 128u)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = sym->type, .kind = FVAL_LOCAL, .off = sym->stack_off, .is_lvalue = 1u,
                                               .marks = BUSTER_FVAL_MARK_REGISTER, .seq_start = BUSTER_FSEQ_MARK(ex),
                                               .seq_ident = sym->ident_id };
        }
        BUSTER_FSEQ_NAME(ex, sym->ident_id, (uint32_t)(ex->unevaluated == 0));
        pushed = true;
    }
    else if (((uint32_t)(sym->kind != FSYM_FUNC) | (uint32_t)(next == FTOK_RPAREN) |
              ((uint32_t)(next == FTOK_LPAREN) & (uint32_t)((flags & BUSTER_FSYM_FORMAT) != 0))) != 0)
    {
        fast_cc_bail(cc, "use of a checked symbol");
    }
    else
    {
        if (ex->eval_only == 0) { fast_cc_reference(cc, sym); }
        else if (ex->unevaluated == 0) { fast_cc_reference_dead(sym); }
        const uint32_t osym = ex->eval_only > 0 ? 0u : fast_cc_ensure_obj_sym(cc, si);
        const uint32_t called = (uint32_t)(next == FTOK_LPAREN);
        const uint8_t marks = (uint8_t)(called * ((BUSTER_FVAL_MARK_NONNULL * (uint32_t)((flags & BUSTER_FSYM_NONNULL) != 0)) |
                                                  (BUSTER_FVAL_MARK_UNUSED_RESULT * (uint32_t)((flags & BUSTER_FSYM_UNUSED_RESULT) != 0)) |
                                                  (BUSTER_FVAL_MARK_PURE * (uint32_t)((flags & BUSTER_FSYM_PURE) != 0))));
        if (ex->vsp < 128u)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = sym->type, .kind = FVAL_GLOBAL, .sym_idx = osym, .imm = 0, .is_func_sym = 1u,
                                               .marks = marks };
        }
        pushed = true;
    }
    return pushed;
}

// The expression compiler's body, made twice (#3577): without the -Wunsequenced log (LOGGING false: fast_cc_expr, every
// expression), and with it, for an expression compiled again without code (fast_cc_expr_logged).
static inline __attribute__((always_inline)) void fast_cc_expr_compile(FastCCState* cc, bool stop_at_comma, bool discard_result, bool is_branch,
                                                                      bool branch_on_true, uint32_t branch_label, FastVal* out_val,
                                                                      const bool logging)
{
    FastExprCtx ex;
    ex.cc = cc;
    ex.vsp = 0;
    ex.arg_sp = 0;
    ex.gp_used = 0;
    ex.xmm_used = 0;
    // #3577: an expression compiled again for its -Wunsequenced log (LOGGING, fast_cc_sequenced_again) makes no code and
    // keeps the log; any other counts its modifications alone. A dead branch's makes no code either (#3592); neither is
    // an unevaluated operand.
    ex.eval_only = (uint8_t)((uint32_t)logging | (uint32_t)(cc->dead != 0));
    ex.unevaluated = 0;
    ex.seq_logging = (uint8_t)logging;
    ex.seq_count = 0;
    ex.seq_mod_count = 0;
    ex.seq_start_pos = cc->pos;
    ex.cmp_pos = UINT32_MAX;
    if (logging)
    {
        ex.seq_hit = 0;
        ex.seq_mods = 0;
        ex.seq_regions = 0;
        ex.seq_bloom = 0;
    }

    FastExprOp ops[128];
    uint32_t osp = 0;
    bool expect_val = true;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (expect_val)
        {
            // HOT PATH #1 (>60% of expect_val tokens): Identifier (variable, enum constant, function, or recognized builtin)
            if (__builtin_expect(k == FTOK_IDENT, 1))
            {
                const uint32_t id = fast_cc_data(cc);
                cc->pos += 1u;
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                if (__builtin_expect(si != 0, 1))
                {
                    // The symbol's value by its kind without a branch on the kind: a local, an enumeration constant,
                    // an object and a function follow each other as the program has them, and the branch that told
                    // them apart was mispredicted 13 K times a compile of sqlite3.c (#2850). What stays is the first
                    // reference of an object or a function, which makes its ELF symbol.
                    static const uint8_t value_kinds[8] = { [FSYM_ENUM_CONST] = FVAL_IMM, [FSYM_LOCAL] = FVAL_LOCAL, [FSYM_GLOBAL] = FVAL_GLOBAL, [FSYM_FUNC] = FVAL_GLOBAL };
                    static const uint8_t value_lvalues[8] = { [FSYM_LOCAL] = 1u, [FSYM_GLOBAL] = 1u };
                    FastSym* sym = cc->syms + si;
                    // #3577: a symbol whose use needs a closer look (BUSTER_FSYM_*: an unsupported type's, a deprecated one,
                    // a format or nonnull function's, a register variable) takes a cold path of its own; the common use
                    // pays this one test, as it paid the unsupported type's alone.
                    if (__builtin_expect(sym->use_flags != 0, 0))
                    {
                        if (!fast_cc_push_flagged_symbol(cc, &ex, sym, si)) { break; }
                        expect_val = false;
                        continue;
                    }
                    const uint32_t symbol_kind = sym->kind & 7u;
                    if (__builtin_expect((symbol_kind < FSYM_ENUM_CONST) | (symbol_kind > FSYM_FUNC), 0))
                    {
                        fast_cc_bail(cc, "unexpected symbol kind in expr");
                        break;
                    }
                    const uint32_t evaluated = (uint32_t)(ex.eval_only == 0);
                    const uint32_t global = (uint32_t)(symbol_kind >= FSYM_GLOBAL);
                    const uint32_t emitted = global & evaluated;
                    if (emitted) { fast_cc_reference(cc, sym); }
                    else if ((global & (uint32_t)(ex.unevaluated == 0)) != 0) { fast_cc_reference_dead(sym); }
                    uint32_t osym = sym->obj_sym & (0u - emitted);
                    if (__builtin_expect((emitted != 0) & (osym == 0), 0))
                    {
                        osym = fast_cc_ensure_obj_sym(cc, si);
                    }
                    // #3577: the symbol's marks (an enumerator's nonliteral, a library function's purity, asked of the driver
                    // at its first use: a call without an effect); an enumerator carries its enum (sec_off) in sym_idx, for
                    // -Wimplicit-enum-enum-cast; a variable's use is logged for -Wunsequenced. Masks, as the kind above.
                    const uint32_t function = (uint32_t)(symbol_kind == FSYM_FUNC);
                    uint8_t push_marks = sym->push_marks;
                    if (__builtin_expect((push_marks & BUSTER_FSYM_PUSH_UNASKED) != 0, 0)) { push_marks = fast_cc_library_pure_query(cc, sym); }
                    const uint32_t enumerator = (uint32_t)(symbol_kind == FSYM_ENUM_CONST);
                    // (FSYM_LOCAL and FSYM_GLOBAL are adjacent: a variable.)
                    const uint32_t variable = (uint32_t)(symbol_kind - (uint32_t)FSYM_LOCAL < 2u);
                    const uint32_t pushed = (uint32_t)(ex.vsp < 128u);
                    if (pushed != 0)
                    {
                        ex.vstack[ex.vsp++] = (FastVal){
                            .type = sym->type,
                            .kind = value_kinds[symbol_kind],
                            .imm = (sym->const_val & (0 - (int64_t)(symbol_kind == FSYM_ENUM_CONST))) |
                                   (uint32_t)(sym->stack_off & (0 - (int32_t)(symbol_kind == FSYM_LOCAL))),
                            .sym_idx = osym | (sym->sec_off & (UINT32_C(0) - enumerator)),
                            .is_func_sym = (uint8_t)function,
                            .is_lvalue = value_lvalues[symbol_kind],
                            .marks = push_marks,
                            .seq_ident = id,
                        };
                    }
                    // The log's entry where it is kept (a compile again, #3577): the value's seq_start is its place + 1.
                    if (logging)
                    {
                        fast_cc_seq_push(&ex, id, variable, pushed);
                    }
                    expect_val = false;
                    continue;
                }
                if (id == cc->id_builtin_clzll || id == cc->id_builtin_clz || id == cc->id_builtin_ctzll ||
                    id == cc->id_builtin_ctz || id == cc->id_builtin_inff || id == cc->id_builtin_inf ||
                    id == cc->id_builtin_huge_valf || id == cc->id_builtin_huge_val ||
                    id == cc->id_builtin_nanf || id == cc->id_builtin_nan || id == cc->id_builtin_expect ||
                    id == cc->id_builtin_constant_p || id == cc->id_builtin_unreachable ||
                    id == cc->id_builtin_bswap16 || id == cc->id_builtin_bswap32 || id == cc->id_builtin_bswap64 ||
                    id == cc->id_builtin_popcount || id == cc->id_builtin_popcountll ||
                    id == cc->id_builtin_memcpy || id == cc->id_builtin_memset || id == cc->id_builtin_memmove ||
                    id == cc->id_builtin_memcmp || id == cc->id_builtin_strlen ||
                    id == cc->id_sync_synchronize || id == cc->id_atomic_load_n || id == cc->id_atomic_store_n)
                {
                    if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_BUILTIN, .sym_idx = id }; }
                    expect_val = false;
                    continue;
                }
                fast_cc_bail(cc, "undeclared identifier in expr");
                break;
            }
            // HOT PATH #2: inline 32-bit integer literal
            if (k == FTOK_INT_LIT)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = (int64_t)(int32_t)fast_cc_data(cc) }; }
                cc->pos += 1u; expect_val = false; continue;
            }

            if (k == FTOK_KW_EXTENSION) { cc->pos += 1u; continue; }

            // Check for `(`: could be cast `(T)expr`, compound literal `(T){...}`, or parenthesized `(expr)`!
            if (k == FTOK_LPAREN)
            {
                if (fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    cc->pos += 1u;
                    bool tok_ok = false;
                    const FastType ct = fast_cc_parse_typename(cc, &tok_ok);
                    if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN)
                    {
                        fast_cc_bail(cc, "bad cast or compound literal type");
                        break;
                    }
                    cc->pos += 1u;
                    if (fast_cc_peek(cc) == FTOK_LBRACE)
                    {
                        // Compound literal `(T){ e0, e1, ... }` handled iteratively via FEOP_COMPOUND_LIT!
                        if (ftype_is_unsupported(ct)) { fast_cc_bail(cc, "unsupported compound literal type"); break; }
                        // #3577: an array of unknown size (which its elements would size) or another incomplete type.
                        if (BUSTER_FAST_OBJECT_INCOMPLETE(cc, ct, 0u) != 0) { fast_cc_bail(cc, "compound literal of an incomplete type"); break; }
                        cc->pos += 1u; // '{'
                        const uint32_t csz = ftype_size(cc, ct);
                        const uint32_t cal = ftype_align(cc, ct);
                        // #3577: a compound literal lives to the end of its block, not of its statement (`p = &(int){ x };`
                        // reads it after): a local's slot (each one's for the whole function), above the temporaries the
                        // expression holds.
                        int32_t slot = 0;
                        if (ex.eval_only == 0)
                        {
                            cc->cur_frame_size = cc->cur_tmp_off;
                            slot = fast_cc_alloc_local(cc, csz, cal < 8u ? 8u : cal);
                            fval_spill_all(&ex, ex.vsp, ex.arg_sp);
                            x86_emit_memzero_inline(cc->obj, FREG_RBP, slot, csz);
                        }
                        if (fast_cc_peek(cc) == FTOK_RBRACE)
                        {
                            cc->pos += 1u;
                            if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = ct, .kind = FVAL_LOCAL, .off = slot, .is_lvalue = 1u }; }
                            expect_val = false;
                            continue;
                        }
                        if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                        ops[osp++] = (FastExprOp){ .op = FEOP_COMPOUND_LIT, .extra = 0u, .type = ct, .slot = slot, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                        // Handle optional designator `.field =` or `[idx] =` right before first element!
                        if (fast_cc_peek(cc) == FTOK_DOT && ftype_is_record(ct))
                        {
                            cc->pos += 1u;
                            if (fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "compound lit .field"); break; }
                            const FastComplexType* rec = cc->ctypes + (ct & BUSTER_FTYPE_CIDX_MASK);
                            const FastMemberInfo* mi = fast_struct_find_member(cc, ct & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
                            if (mi == NULL) { fast_cc_bail(cc, "compound lit unknown field"); break; }
                            ops[osp - 1u].extra = (uint16_t)(mi - (cc->member_info + rec->start));
                            cc->pos += 1u;
                            // #3577: a designator of more than one member (`.a.b =`, `.a[0] =`) is refused.
                            if (fast_cc_peek(cc) != FTOK_ASSIGN) { fast_cc_bail(cc, "compound literal designator"); break; }
                            cc->pos += 1u;
                        }
                        continue;
                    }
                    if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                    ops[osp++] = (FastExprOp){ .op = FEOP_CAST, .type = ct };
                    continue;
                }
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                ops[osp++] = (FastExprOp){ .op = FEOP_LPAREN, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                cc->pos += 1u;
                continue;
            }

            // Prefix unary operators
            if (k == FTOK_PLUS)  { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_UNARY_PLUS }; } cc->pos += 1u; continue; }
            if (k == FTOK_MINUS) { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_UNARY_MINUS }; } cc->pos += 1u; continue; }
            if (k == FTOK_TILDE) { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_UNARY_TILDE }; } cc->pos += 1u; continue; }
            if (k == FTOK_BANG)  { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_UNARY_BANG }; } cc->pos += 1u; continue; }
            if (k == FTOK_STAR)  { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_DEREF }; } cc->pos += 1u; continue; }
            if (k == FTOK_AMP)   { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_ADDR }; } cc->pos += 1u; continue; }
            if (k == FTOK_INC)   { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_PRE_INC }; } cc->pos += 1u; continue; }
            if (k == FTOK_DEC)   { if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = FEOP_PRE_DEC }; } cc->pos += 1u; continue; }

            // `sizeof` / `_Alignof`
            if (k == FTOK_KW_SIZEOF || k == FTOK_KW_ALIGNOF)
            {
                const bool is_al = k == FTOK_KW_ALIGNOF;
                cc->pos += 1u;
                if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    cc->pos += 1u;
                    bool tok_ok = false;
                    const FastType st = fast_cc_parse_typename(cc, &tok_ok);
                    if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "bad sizeof(type)"); break; }
                    cc->pos += 1u;
                    // #3577: an incomplete type's is the full pipeline's error.
                    if (BUSTER_FAST_TYPE_INCOMPLETE(cc, st) != 0) { fast_cc_bail(cc, "sizeof an incomplete type"); break; }
                    const uint64_t val = is_al ? ftype_align(cc, st) : ftype_size(cc, st);
                    if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)val, .marks = BUSTER_FVAL_MARK_NONLITERAL }; }
                    expect_val = false;
                    continue;
                }
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                // #3577: sizeof of a parameter declared an array is the full pipeline's -Wsizeof-array-argument; asked in a
                // function that has one.
                if (cc->cur_array_params != 0 && !is_al && fast_cc_sizeof_array_param(cc))
                {
                    fast_cc_bail(cc, "sizeof of an array parameter");
                    break;
                }
                ops[osp++] = (FastExprOp){ .op = is_al ? FEOP_ALIGNOF_UNARY : FEOP_SIZEOF_UNARY };
                ex.eval_only += 1u;
                ex.unevaluated += 1u;
                continue;
            }

            // `__builtin_offsetof(type, member)`
            if (k == FTOK_KW_BUILTIN_OFFSETOF)
            {
                const uint64_t val = fast_cc_parse_offsetof(cc);
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)val }; }
                expect_val = false;
                continue;
            }

            // `_Generic(controlling_expr, T1: e1, default: ed)` (used by glibc 2.43 <string.h>!)
            if (k == FTOK_KW_GENERIC)
            {
                // #3577: the full pipeline checks every association (their types, their expressions' diagnostics and
                // its own -Wunreachable-code-generic-assoc), where this path reads the selected one alone: only a shape
                // whose selected association carries all of that is taken (fast_cc_generic_shape_ok).
                if (!fast_cc_generic_shape_ok(cc, cc->pos + 1u))
                {
                    fast_cc_bail(cc, "_Generic");
                    break;
                }
                cc->pos += 1u;
                if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "bad _Generic"); break; }
                cc->pos += 1u;
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                ops[osp++] = (FastExprOp){ .op = FEOP_GENERIC_CTRL, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                ex.eval_only += 1u;
                ex.unevaluated += 1u;
                continue;
            }

            // `__builtin_va_start` / `va_arg` / `va_end` / `va_copy`
            if (k == FTOK_KW_BUILTIN_VA_START || k == FTOK_KW_BUILTIN_VA_ARG ||
                k == FTOK_KW_BUILTIN_VA_END || k == FTOK_KW_BUILTIN_VA_COPY)
            {
                cc->pos += 1u;
                if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "bad va builtin"); break; }
                cc->pos += 1u;
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                const uint16_t vop = k == FTOK_KW_BUILTIN_VA_START ? FEOP_VA_START :
                                     k == FTOK_KW_BUILTIN_VA_ARG ? FEOP_VA_ARG :
                                     k == FTOK_KW_BUILTIN_VA_END ? FEOP_VA_END : FEOP_VA_COPY;
                ops[osp++] = (FastExprOp){ .op = vop, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                continue;
            }

            // Literals (FTOK_INT_LIT handled on hot path above)
            if (k == FTOK_INT_U32)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_U32, .kind = FVAL_IMM, .imm = (int64_t)(uint64_t)fast_cc_data(cc) }; }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_INT_I64)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I64, .kind = FVAL_IMM, .imm = (int64_t)cc->pp->num_pool[fast_cc_data(cc)] }; }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_INT_U64)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)cc->pp->num_pool[fast_cc_data(cc)] }; }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_CHAR_LIT)
            {
                if (ex.vsp < 128u)
                {
                    ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = (int64_t)(int32_t)fast_cc_data(cc),
                                                     .marks = BUSTER_FVAL_MARK_NONLITERAL, .op = FTOK_CHAR_LIT };
                }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_FLOAT_LIT)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_F64, .kind = FVAL_FIMM, .imm = (int64_t)cc->pp->num_pool[fast_cc_data(cc)] }; }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_FLOAT32)
            {
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_F32, .kind = FVAL_FIMM, .imm = (int64_t)(uint64_t)fast_cc_data(cc) }; }
                cc->pos += 1u; expect_val = false; continue;
            }
            if (k == FTOK_STR_LIT || k == FTOK_KW_FUNC)
            {
                // __PRETTY_FUNCTION__ is the function's signature in C, as clang prints it, and not its name (#3618):
                // the full pipeline prints it. Without code too, since sizeof takes its length.
                if (k == FTOK_KW_FUNC && fast_cc_data(cc) == cc->id_pretty_function)
                {
                    fast_cc_bail(cc, "__PRETTY_FUNCTION__");
                    break;
                }
                const char* sp;
                uint32_t slen = 0;
                if (k == FTOK_STR_LIT)
                {
                    sp = fast_cc_parse_string_lit(cc, &slen);
                }
                else
                {
                    sp = fast_cc_ident_str(cc, cc->cur_func_name_id, &slen);
                    cc->pos += 1u;
                }
                // Create array[slen+1] of char type (so `sizeof("abc") == 4` AND decays to `char*`!)
                const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                if (ac != 0)
                {
                    const FastType ct = (cc->unsigned_char ? FTYPE_U8 : FTYPE_I8) | BUSTER_FTYPE_SCALAR_CHAR;
                    cc->ctypes[ac].inner = k == FTOK_KW_FUNC ? ftype_with_const(ct) : ct;
                    cc->ctypes[ac].count = slen + 1u;
                    cc->ctypes[ac].size = slen + 1u;
                    cc->ctypes[ac].align = 1u;
                    cc->ctypes[ac].complete = 1u;
                }
                const uint32_t ro_off = ex.eval_only > 0 ? 0u : fast_obj_add_string_lit(cc->obj, sp, slen);
                if (ex.vsp < 128u)
                {
                    ex.vstack[ex.vsp++] = (FastVal){
                        .type = ac != 0 ? ftype_from_index(cc, ac) : ftype_make_ptr(cc, FTYPE_I8),
                        .kind = FVAL_GLOBAL,
                        .sym_idx = cc->obj->sec_sym_rodata_str,
                        .imm = (int64_t)ro_off,
                        .is_lvalue = 1u,
                        .op = (uint8_t)((uint32_t)FTOK_STR_LIT * (uint32_t)(k == FTOK_STR_LIT)),
                        .lasting = (uint8_t)(BUSTER_FVAL_LASTING_STRING * (uint32_t)(k == FTOK_STR_LIT)),
                    };
                }
                expect_val = false;
                continue;
            }

            // Empty call arg list `f()`!
            if (k == FTOK_RPAREN && osp > 0 && ops[osp - 1u].op == FEOP_CALL && ops[osp - 1u].extra == 0)
            {
                const FastExprOp cop = ops[--osp];
                cc->pos += 1u;
                FastVal callee = ex.vstack[--ex.vsp];
                FastVal res = { 0 };
                fval_emit_call(&ex, &callee, ex.call_args + cop.lbl0, 0u, cop.lbl0, &res);
                ex.arg_sp = cop.lbl0;
                ex.vstack[ex.vsp++] = res;
                expect_val = false;
                continue;
            }
            // Trailing comma before `}` in compound literal `(T){ 1, }`
            if (k == FTOK_RBRACE && osp > 0 && ops[osp - 1u].op == FEOP_COMPOUND_LIT)
            {
                const FastExprOp cl = ops[--osp];
                cc->pos += 1u;
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = cl.type, .kind = FVAL_LOCAL, .off = cl.slot, .is_lvalue = 1u }; }
                expect_val = false;
                continue;
            }
            break;
        }

        // -------------------------------------------------------------------------------------------------------------
        // expect_val == false: handle postfix operators, closing delimiters, and binary/ternary/assign operators!

        // HOT PATH: `;` ends almost every expression statement immediately!
        if (k == FTOK_SEMI) { break; }

        // Postfix `.` and `->`
        if (k == FTOK_DOT || k == FTOK_ARROW)
        {
            const bool is_arrow = k == FTOK_ARROW;
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_IDENT || ex.vsp < 1u) { fast_cc_bail(cc, "bad member access"); break; }
            const uint32_t fid = fast_cc_data(cc);
            cc->pos += 1u;
            FastVal* v = ex.vstack + (ex.vsp - 1u);
            FastType rec_t = v->type;
            if (is_arrow)
            {
                rec_t = ftype_decay(cc, rec_t);
                if (!ftype_is_ptr(rec_t)) { fast_cc_bail(cc, "-> on non-ptr"); break; }
                rec_t = ftype_deref(cc, rec_t);
            }
            if (!ftype_is_record(rec_t)) { fast_cc_bail(cc, "member access on non-record"); break; }
            const FastMemberInfo* mi = fast_struct_find_member(cc, rec_t & BUSTER_FTYPE_CIDX_MASK, fid);
            if (mi == NULL) { fast_cc_bail(cc, "unknown struct member"); break; }
            if (is_arrow)
            {
                // Without code (sizeof's operand, #3592's dead branch), `->` keeps its member's bit-field (whose address is
                // an error) in a placeholder; `.` below makes no code, and keeps its object's kind too.
                const uint32_t pr = fval_to_gp(&ex, v, true);
                if (mi->bit_width != 0)
                {
                    *v = (FastVal){
                        .type = mi->type, .kind = FVAL_BITFIELD, .bf_base = FVAL_INDIRECT,
                        .reg = (uint8_t)pr, .off = (int32_t)mi->byte_offset,
                        .bf_off = mi->bit_offset, .bf_width = mi->bit_width,
                        .bf_unit = mi->bit_unit_size, .bf_signed = mi->bit_signed, .is_lvalue = 1u,
                    };
                }
                else
                {
                    *v = (FastVal){ .type = mi->type, .kind = FVAL_INDIRECT, .reg = (uint8_t)pr, .off = (int32_t)mi->byte_offset, .is_lvalue = 1u };
                }
            }
            else
            {
                const uint8_t bkind = v->kind;
                if (mi->bit_width != 0)
                {
                    v->kind = FVAL_BITFIELD;
                    v->bf_base = bkind;
                    v->bf_off = mi->bit_offset;
                    v->bf_width = mi->bit_width;
                    v->bf_unit = mi->bit_unit_size;
                    v->bf_signed = mi->bit_signed;
                    v->is_lvalue = 1u;
                }
                // #3577: a member isn't its object by name, the variable -Wunsequenced follows (BUSTER_FSEQ_MODIFY).
                v->seq_start = 0;
                v->seq_ident = 0;
                v->type = mi->type;
                if (bkind == FVAL_GLOBAL) { v->imm += (int64_t)mi->byte_offset; }
                else if (bkind == FVAL_SPILL_ADDR) { v->addr_disp += (int32_t)mi->byte_offset; }
                else { v->off += (int32_t)mi->byte_offset; }
            }
            continue;
        }

        // Postfix `++` / `--`
        if (k == FTOK_INC || k == FTOK_DEC)
        {
            if (ex.vsp < 1u) { fast_cc_bail(cc, "postfix inc/dec"); break; }
            cc->pos += 1u;
            const uint32_t seq_result = BUSTER_FSEQ_RESULT(&ex.vstack[ex.vsp - 1u]);
            FastVal res = { 0 };
            fval_emit_incdec(&ex, &ex.vstack[ex.vsp - 1u], k == FTOK_INC, true, &res);
            res.marks = BUSTER_FVAL_MARK_EFFECT;
            res.seq_ident = seq_result;
            ex.vstack[ex.vsp - 1u] = res;
            continue;
        }

        // Postfix `[` subscript
        if (k == FTOK_LBRACKET)
        {
            if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
            ops[osp++] = (FastExprOp){ .op = FEOP_SUBSCRIPT, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
            cc->pos += 1u;
            expect_val = true;
            continue;
        }

        // Postfix `(` function call
        if (k == FTOK_LPAREN)
        {
            if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
            ops[osp++] = (FastExprOp){ .op = FEOP_CALL, .extra = 0u, .lbl0 = ex.arg_sp, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
            cc->pos += 1u;
            expect_val = true;
            continue;
        }

        // Closing `]` for subscript!
        if (k == FTOK_RBRACKET)
        {
            while (osp > 0 && ops[osp - 1u].op >= FEOP_UNARY_PLUS && !cc->bailout)
            {
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            if (osp == 0 || ops[osp - 1u].op != FEOP_SUBSCRIPT) { break; }
            osp -= 1u;
            cc->pos += 1u;
            if (ex.vsp < 2u) { fast_cc_bail(cc, "subscript stack"); break; }
            FastVal idx_v = ex.vstack[--ex.vsp];
            FastVal base_v = ex.vstack[--ex.vsp];
            // Support C commutative `i[a]`!
            if (!ftype_is_ptr(ftype_decay(cc, base_v.type)) && ftype_is_ptr(ftype_decay(cc, idx_v.type)))
            {
                FastVal tmp = base_v; base_v = idx_v; idx_v = tmp;
            }
            const FastType bt = ftype_decay(cc, base_v.type);
            if (!ftype_is_ptr(bt)) { fast_cc_bail(cc, "subscript non-pointer"); break; }
            const FastType elem = ftype_deref(cc, bt);
            // #3577: an integer index, and a complete element type, or the full pipeline's error.
            if ((BUSTER_FAST_TYPE_INCOMPLETE(cc, elem) | (uint32_t)(BUSTER_FAST_OPERAND_CLASS(cc, idx_v.type) != BUSTER_FOPERAND_INTEGER)) != 0)
            {
                fast_cc_bail(cc, "subscript the full pipeline refuses");
                break;
            }
            const uint32_t esz = ftype_size(cc, elem) != 0 ? ftype_size(cc, elem) : 1u;
            if (ex.eval_only > 0)
            {
                ex.vstack[ex.vsp++] = (FastVal){ .type = elem, .kind = FVAL_INDIRECT, .is_lvalue = 1u };
                continue;
            }
            // Zero-instruction constant-index folding for local/global arrays & pointer offsets!
            if (idx_v.kind == FVAL_IMM)
            {
                const int64_t boff = idx_v.imm * (int64_t)esz;
                if (ftype_is_array(base_v.type) && base_v.kind == FVAL_LOCAL && boff >= INT32_MIN && boff <= INT32_MAX)
                {
                    ex.vstack[ex.vsp++] = (FastVal){ .type = elem, .kind = FVAL_LOCAL, .off = base_v.off + (int32_t)boff, .is_lvalue = 1u };
                    continue;
                }
                if (ftype_is_array(base_v.type) && base_v.kind == FVAL_GLOBAL)
                {
                    ex.vstack[ex.vsp++] = (FastVal){ .type = elem, .kind = FVAL_GLOBAL, .sym_idx = base_v.sym_idx, .imm = base_v.imm + boff, .is_lvalue = 1u };
                    continue;
                }
                if (boff >= INT32_MIN && boff <= INT32_MAX)
                {
                    const uint32_t pr = fval_to_gp(&ex, &base_v, true);
                    ex.vstack[ex.vsp++] = (FastVal){ .type = elem, .kind = FVAL_INDIRECT, .reg = (uint8_t)pr, .off = (int32_t)boff, .is_lvalue = 1u };
                    continue;
                }
            }
            FastVal ptr_v = { 0 };
            fval_emit_binary(&ex, FTOK_PLUS, &base_v, &idx_v, &ptr_v);
            const uint32_t pr = fval_to_gp(&ex, &ptr_v, true);
            ex.vstack[ex.vsp++] = (FastVal){ .type = elem, .kind = FVAL_INDIRECT, .reg = (uint8_t)pr, .off = 0, .is_lvalue = 1u };
            continue;
        }

        // Closing `}` for compound literal `(T){ ... }`!
        if (k == FTOK_RBRACE)
        {
            while (osp > 0 && ops[osp - 1u].op >= FEOP_UNARY_PLUS && !cc->bailout)
            {
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            if (osp == 0 || ops[osp - 1u].op != FEOP_COMPOUND_LIT) { break; }
            FastExprOp cl = ops[--osp];
            cc->pos += 1u;
            if (ex.vsp >= 1u)
            {
                FastVal elem_v = ex.vstack[--ex.vsp];
                if (!fast_cc_compound_element_quiet(cc, &cl, elem_v.type)) { fast_cc_bail(cc, "compound literal element the full pipeline questions"); break; }
                if (ex.eval_only == 0)
                {
                    if (ftype_is_scalar(cl.type))
                    {
                        FastVal dst = { .type = cl.type, .kind = FVAL_LOCAL, .off = cl.slot };
                        fval_store(&ex, &dst, &elem_v, false, NULL);
                    }
                    else if (ftype_is_array(cl.type))
                    {
                        const FastType et = cc->ctypes[cl.type & BUSTER_FTYPE_CIDX_MASK].inner;
                        FastVal dst = { .type = et, .kind = FVAL_LOCAL, .off = cl.slot + (int32_t)(cl.extra * ftype_size(cc, et)) };
                        fval_store(&ex, &dst, &elem_v, false, NULL);
                    }
                    else if (ftype_is_record(cl.type))
                    {
                        const FastComplexType* rec = cc->ctypes + (cl.type & BUSTER_FTYPE_CIDX_MASK);
                        if (cl.extra < rec->count)
                        {
                            const FastMemberInfo* mi = cc->member_info + rec->start + cl.extra;
                            FastVal dst = { .type = mi->type, .kind = mi->bit_width != 0 ? FVAL_BITFIELD : FVAL_LOCAL,
                                            .bf_base = FVAL_LOCAL, .off = cl.slot + (int32_t)mi->byte_offset,
                                            .bf_off = mi->bit_offset, .bf_width = mi->bit_width,
                                            .bf_unit = mi->bit_unit_size, .bf_signed = mi->bit_signed };
                            fval_store(&ex, &dst, &elem_v, false, NULL);
                        }
                    }
                }
            }
            ex.vstack[ex.vsp++] = (FastVal){ .type = cl.type, .kind = FVAL_LOCAL, .off = cl.slot, .is_lvalue = 1u };
            continue;
        }

        // Closing `)` for LPAREN / CALL / GENERIC_SEL / VA_*!
        if (k == FTOK_RPAREN)
        {
            while (osp > 0 && ops[osp - 1u].op >= FEOP_UNARY_PLUS && !cc->bailout)
            {
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            if (osp == 0) { break; }
            const uint16_t top = ops[osp - 1u].op;
            if (top == FEOP_LPAREN)
            {
                osp -= 1u;
                cc->pos += 1u;
                // A parenthesized value: no operator's precedence questions it any more (#3577). (Without a value, the
                // stack's bottom slot takes the store harmlessly.)
                ex.vstack[ex.vsp - (uint32_t)(ex.vsp != 0)].op = 0;
                continue;
            }
            if (top == FEOP_GENERIC_SEL)
            {
                cc->pos = ops[--osp].lbl0; // jump right past `_Generic(...)` closing ')'!
                continue;
            }
            if (top == FEOP_CALL)
            {
                const FastExprOp cop = ops[--osp];
                cc->pos += 1u;
                // Push final argument
                if (ex.vsp < 2u || ex.arg_sp >= 128u) { fast_cc_bail(cc, "call arg stack"); break; }
                ex.call_args[ex.arg_sp++] = ex.vstack[--ex.vsp];
                const uint32_t argc = ex.arg_sp - cop.lbl0;
                FastVal callee = ex.vstack[--ex.vsp];
                FastVal res = { 0 };
                fval_emit_call(&ex, &callee, ex.call_args + cop.lbl0, argc, cop.lbl0, &res);
                ex.arg_sp = cop.lbl0;
                ex.vstack[ex.vsp++] = res;
                continue;
            }
            if (top == FEOP_VA_END)
            {
                osp -= 1u; cc->pos += 1u;
                if (ex.vsp < 1u || !fast_cc_is_va_list(cc, ex.vstack[ex.vsp - 1u].type)) { fast_cc_bail(cc, "va_end of a non-va_list"); break; }
                if (ex.vsp >= 1u) { fval_release(&ex, &ex.vstack[ex.vsp - 1u]); ex.vstack[ex.vsp - 1u] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID, .marks = BUSTER_FVAL_MARK_EFFECT }; }
                continue;
            }
            if (top == FEOP_VA_COPY)
            {
                osp -= 1u; cc->pos += 1u;
                if (ex.vsp < 2u) { fast_cc_bail(cc, "va_copy args"); break; }
                if (!fast_cc_is_va_list(cc, ex.vstack[ex.vsp - 1u].type) || !fast_cc_is_va_list(cc, ex.vstack[ex.vsp - 2u].type))
                {
                    fast_cc_bail(cc, "va_copy of a non-va_list");
                    break;
                }
                FastVal src = ex.vstack[--ex.vsp];
                FastVal dst = ex.vstack[--ex.vsp];
                if (ex.eval_only == 0)
                {
                    const uint32_t dr = fval_to_gp(&ex, &dst, true);
                    const uint32_t sr = fval_to_gp(&ex, &src, true);
                    const uint32_t tr = fval_alloc_gp(&ex);
                    x86_emit_memcpy_inline(cc->obj, dr, 0, sr, 0, 24u, tr);
                    ex.gp_used &= (uint16_t)~((1u << dr) | (1u << sr) | (1u << tr));
                }
                ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID, .marks = BUSTER_FVAL_MARK_EFFECT };
                continue;
            }
            break;
        }

        // `:` for ternary `? ... : ...`!
        if (k == FTOK_COLON)
        {
            while (osp > 0 && ops[osp - 1u].op >= FEOP_UNARY_PLUS && ops[osp - 1u].op != FEOP_TERNARY_Q && !cc->bailout)
            {
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            if (osp == 0 || ops[osp - 1u].op != FEOP_TERNARY_Q) { break; }
            if (ex.vsp < 1u) { fast_cc_bail(cc, "ternary then missing"); break; }
            const uint32_t constant = ops[osp - 1u].constant;
            if (constant != BUSTER_FCONSTANT_NONE)
            {
                // A live then arm's value stays on the stack, under the dead else arm's; a dead then arm's goes. Its marks
                // and constants are kept as below (#3577), for the completion's checks of both arms.
                const FastVal then_v = ex.vstack[ex.vsp - 1u];
                const FastType arm_t = ftype_decay(cc, then_v.type);
                const FastArm then_arm = fast_cc_arm(cc, &then_v);
                ex.vsp -= (uint32_t)(constant == BUSTER_FCONSTANT_FALSE);
                ex.eval_only = (uint8_t)(ex.eval_only + (constant == BUSTER_FCONSTANT_TRUE) - (constant == BUSTER_FCONSTANT_FALSE));
                ops[osp - 1u] = (FastExprOp){
                    .op = FEOP_TERNARY_C, .extra = (uint16_t)(then_v.marks | ((uint32_t)then_arm.kind << 8) | ((uint32_t)then_arm.not_null_literal << 12)),
                    .lbl0 = (uint32_t)(uint64_t)then_arm.value, .type = arm_t,
                    .slot = ftype_is_record(arm_t) ? 0 : (int32_t)(uint32_t)((uint64_t)then_arm.value >> 32),
                    .seq_lo = ops[osp - 1u].seq_lo, .seq_mid = ops[osp - 1u].seq_mid, .seq_mid2 = BUSTER_FSEQ_MARK_IF(&ex, logging),
                    .constant = (uint8_t)constant,
                };
                cc->pos += 1u;
                expect_val = true;
                continue;
            }
            FastVal then_v = ex.vstack[--ex.vsp];
            const FastType then_t = ftype_decay(cc, then_v.type);
            const uint32_t else_lbl = ops[osp - 1u].lbl0;
            const uint32_t then_tail_lbl = ex.eval_only > 0 ? 0u : fast_label_new(cc->obj);
            const uint32_t end_lbl = ex.eval_only > 0 ? 0u : fast_label_new(cc->obj);
            int32_t tern_slot = 0;

            if (ex.eval_only == 0)
            {
                if (ftype_is_record(then_t))
                {
                    const FastComplexType* rct = cc->ctypes + (then_t & BUSTER_FTYPE_CIDX_MASK);
                    tern_slot = fast_cc_alloc_tmp(cc, rct->size, rct->align < 8u ? 8u : rct->align);
                    FastVal dst = { .type = then_t, .kind = FVAL_LOCAL, .off = tern_slot };
                    fval_store(&ex, &dst, &then_v, false, NULL);
                }
                else if (ftype_is_float(then_t))
                {
                    const uint32_t x = fval_to_xmm(&ex, &then_v, true);
                    if (x != 0u) { x86_sse_rr(cc->obj, ftype_is_f64(then_t), 0x10u, 0u, x); }
                }
                else if (!ftype_is_void(then_t))
                {
                    const uint32_t g = fval_to_gp(&ex, &then_v, true);
                    if (g != FREG_RAX) { x86_mov_rr(cc->obj, true, FREG_RAX, g); }
                }
                else
                {
                    fval_release(&ex, &then_v);
                }
                x86_jmp_label(cc->obj, then_tail_lbl);
                fast_label_bind(cc->obj, else_lbl);
                ex.gp_used = 0;
                ex.xmm_used = 0;
            }
            // The then arm's constants (fast_cc_arm), for the completion's: its value in lbl0 (low half) and in slot (high
            // half) where no record needs that, its kind and null flag above its marks in extra.
            const FastArm then_arm = fast_cc_arm(cc, &then_v);
            ops[osp - 1u] = (FastExprOp){
                .op = FEOP_TERNARY_C, .extra = (uint16_t)(then_v.marks | ((uint32_t)then_arm.kind << 8) | ((uint32_t)then_arm.not_null_literal << 12)),
                .lbl0 = (uint32_t)(uint64_t)then_arm.value, .lbl1 = then_tail_lbl, .lbl2 = end_lbl, .type = then_t,
                .slot = ftype_is_record(then_t) ? tern_slot : (int32_t)(uint32_t)((uint64_t)then_arm.value >> 32),
                .seq_lo = ops[osp - 1u].seq_lo, .seq_mid = ops[osp - 1u].seq_mid, .seq_mid2 = BUSTER_FSEQ_MARK_IF(&ex, logging),
            };
            cc->pos += 1u;
            expect_val = true;
            continue;
        }

        // `,` could be call arg separator, `_Generic` separator, `va_*` separator, compound lit separator, OR comma op!
        if (k == FTOK_COMMA)
        {
            while (osp > 0 && ops[osp - 1u].op >= FEOP_UNARY_PLUS && !cc->bailout)
            {
                // For comma operator inside `(a, b)`, stop only if barrier is NOT a plain LPAREN!
                if (fast_expr_op_prec(ops[osp - 1u].op) < 1u) { break; }
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            if (osp == 0)
            {
                if (stop_at_comma) { break; }
                // Top-level comma operator
                if (osp < 128u)
                {
                    const uint8_t below = BUSTER_FSEQ_BELOW_IF(ops, osp, logging);
                    ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + FTOK_COMMA), .seq_lo = below, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                }
                cc->pos += 1u;
                expect_val = true;
                continue;
            }
            const uint16_t top = ops[osp - 1u].op;
            if (top == FEOP_CALL)
            {
                if (ex.vsp < 1u || ex.arg_sp >= 128u) { fast_cc_bail(cc, "call arg overflow"); break; }
                // Spill argument if it holds a register so subsequent args can use all scratch regs!
                fval_spill_one(&ex, &ex.vstack[ex.vsp - 1u]);
                ex.call_args[ex.arg_sp++] = ex.vstack[--ex.vsp];
                ops[osp - 1u].extra += 1u;
                ops[osp - 1u].seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging);
                cc->pos += 1u;
                expect_val = true;
                continue;
            }
            if (top == FEOP_COMPOUND_LIT)
            {
                FastExprOp* cl = ops + (osp - 1u);
                if (ex.vsp >= 1u)
                {
                    FastVal elem_v = ex.vstack[--ex.vsp];
                    if (!fast_cc_compound_element_quiet(cc, cl, elem_v.type)) { fast_cc_bail(cc, "compound literal element the full pipeline questions"); break; }
                    if (ex.eval_only == 0)
                    {
                        // #3577: a scalar's element before a trailing `,` (`(int){ 1, }`) too.
                        if (ftype_is_scalar(cl->type))
                        {
                            FastVal dst = { .type = cl->type, .kind = FVAL_LOCAL, .off = cl->slot };
                            fval_store(&ex, &dst, &elem_v, false, NULL);
                        }
                        else if (ftype_is_array(cl->type))
                        {
                            const FastType et = cc->ctypes[cl->type & BUSTER_FTYPE_CIDX_MASK].inner;
                            FastVal dst = { .type = et, .kind = FVAL_LOCAL, .off = cl->slot + (int32_t)(cl->extra * ftype_size(cc, et)) };
                            fval_store(&ex, &dst, &elem_v, false, NULL);
                        }
                        else if (ftype_is_record(cl->type))
                        {
                            const FastComplexType* rec = cc->ctypes + (cl->type & BUSTER_FTYPE_CIDX_MASK);
                            if (cl->extra < rec->count)
                            {
                                const FastMemberInfo* mi = cc->member_info + rec->start + cl->extra;
                                FastVal dst = { .type = mi->type, .kind = mi->bit_width != 0 ? FVAL_BITFIELD : FVAL_LOCAL,
                                                .bf_base = FVAL_LOCAL, .off = cl->slot + (int32_t)mi->byte_offset,
                                                .bf_off = mi->bit_offset, .bf_width = mi->bit_width,
                                                .bf_unit = mi->bit_unit_size, .bf_signed = mi->bit_signed };
                                fval_store(&ex, &dst, &elem_v, false, NULL);
                            }
                        }
                    }
                    cl->extra += 1u;
                }
                cl->seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging);
                cc->pos += 1u;
                if (fast_cc_peek(cc) == FTOK_DOT && ftype_is_record(cl->type))
                {
                    cc->pos += 1u;
                    const FastComplexType* rec = cc->ctypes + (cl->type & BUSTER_FTYPE_CIDX_MASK);
                    const FastMemberInfo* mi = fast_cc_peek(cc) == FTOK_IDENT ? fast_struct_find_member(cc, cl->type & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc)) : NULL;
                    if (mi == NULL) { fast_cc_bail(cc, "compound lit .field"); break; }
                    cl->extra = (uint16_t)(mi - (cc->member_info + rec->start));
                    cc->pos += 1u;
                    // #3577: a designator of more than one member (`.a.b =`, `.a[0] =`) is refused.
                    if (fast_cc_peek(cc) != FTOK_ASSIGN) { fast_cc_bail(cc, "compound literal designator"); break; }
                    cc->pos += 1u;
                }
                expect_val = true;
                continue;
            }
            if (top == FEOP_GENERIC_CTRL)
            {
                // Pop controlling expression (evaluated in eval_only mode!) and scan association list!
                if (ex.vsp < 1u) { fast_cc_bail(cc, "_Generic ctrl missing"); break; }
                const FastType ctrl_t = ftype_decay(cc, ex.vstack[--ex.vsp].type);
                if (ex.eval_only > 0) { ex.eval_only -= 1u; }
                if (ex.unevaluated > 0) { ex.unevaluated -= 1u; }
                osp -= 1u;
                cc->pos += 1u; // ','
                uint32_t match_pos = UINT32_MAX;
                uint32_t def_pos = UINT32_MAX;
                uint32_t end_pos = UINT32_MAX;
                while (cc->pos < cc->tcount && !cc->bailout)
                {
                    bool is_def = false;
                    FastType assoc_t = FTYPE_VOID;
                    if (fast_cc_peek(cc) == FTOK_KW_DEFAULT) { is_def = true; cc->pos += 1u; }
                    else
                    {
                        bool tok_ok = false;
                        assoc_t = fast_cc_parse_typename(cc, &tok_ok);
                        if (!tok_ok) { fast_cc_bail(cc, "_Generic assoc type"); break; }
                    }
                    if (fast_cc_peek(cc) != FTOK_COLON) { fast_cc_bail(cc, "_Generic expected :"); break; }
                    cc->pos += 1u;
                    const uint32_t expr_pos = cc->pos;
                    if (is_def) { def_pos = expr_pos; }
                    else if (match_pos == UINT32_MAX && ftype_generic_match(cc, ctrl_t, assoc_t)) { match_pos = expr_pos; }
                    // Skip balanced tokens of this association expression up to `,` or `)` at depth 0
                    uint32_t bd = 0;
                    while (cc->pos < cc->tcount)
                    {
                        const uint8_t ak = fast_cc_peek(cc);
                        if (bd == 0 && (ak == FTOK_COMMA || ak == FTOK_RPAREN)) { break; }
                        if (ak == FTOK_LPAREN || ak == FTOK_LBRACKET || ak == FTOK_LBRACE) { bd += 1u; }
                        else if (ak == FTOK_RPAREN || ak == FTOK_RBRACKET || ak == FTOK_RBRACE) { if (bd > 0) { bd -= 1u; } }
                        cc->pos += 1u;
                    }
                    if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; continue; }
                    if (fast_cc_peek(cc) == FTOK_RPAREN) { end_pos = cc->pos + 1u; break; }
                    fast_cc_bail(cc, "_Generic unclosed");
                    break;
                }
                const uint32_t win = match_pos != UINT32_MAX ? match_pos : def_pos;
                if (win == UINT32_MAX || end_pos == UINT32_MAX) { fast_cc_bail(cc, "_Generic no match"); break; }
                ops[osp++] = (FastExprOp){ .op = FEOP_GENERIC_SEL, .lbl0 = end_pos, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
                cc->pos = win;
                expect_val = true;
                continue;
            }
            if (top == FEOP_GENERIC_SEL)
            {
                cc->pos = ops[--osp].lbl0; // jump past `_Generic(...)`!
                continue;
            }
            if (top == FEOP_VA_START)
            {
                // __builtin_va_start(ap, last_param) -> initialize 24B va_list struct at `ap`, skip `last_param)`!
                // #3577: in a variadic function, of a va_list and the last named parameter, or the full pipeline's
                // error or warning (-Wvarargs).
                osp -= 1u;
                cc->pos += 1u; // ','
                if (!cc->cur_variadic || fast_cc_peek(cc) != FTOK_IDENT || fast_cc_data(cc) != cc->cur_last_param ||
                    fast_cc_peek_at(cc, 1u) != FTOK_RPAREN || ex.vsp < 1u || !fast_cc_is_va_list(cc, ex.vstack[ex.vsp - 1u].type))
                {
                    fast_cc_bail(cc, "va_start the full pipeline refuses");
                    break;
                }
                cc->pos += 2u;
                FastVal ap_v = ex.vstack[--ex.vsp];
                if (ex.eval_only == 0)
                {
                    const uint32_t ap = fval_to_gp(&ex, &ap_v, true);
                    const uint32_t tmp = fval_alloc_gp(&ex);
                    x86_store_imm(cc->obj, ap, 0, (int32_t)cc->cur_va_gp_init, 4u);
                    x86_store_imm(cc->obj, ap, 4, (int32_t)cc->cur_va_fp_init, 4u);
                    x86_lea(cc->obj, tmp, FREG_RBP, cc->cur_va_overflow_off);
                    x86_store(cc->obj, ap, 8, tmp, 8u);
                    x86_lea(cc->obj, tmp, FREG_RBP, cc->cur_va_save_off);
                    x86_store(cc->obj, ap, 16, tmp, 8u);
                    ex.gp_used &= (uint16_t)~((1u << ap) | (1u << tmp));
                }
                ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID, .marks = BUSTER_FVAL_MARK_EFFECT };
                continue;
            }
            if (top == FEOP_VA_ARG)
            {
                osp -= 1u;
                cc->pos += 1u; // ','
                bool tok_ok = false;
                const FastType arg_t = fast_cc_parse_typename(cc, &tok_ok);
                if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN || ex.vsp < 1u) { fast_cc_bail(cc, "va_arg type"); break; }
                cc->pos += 1u; // ')'
                // #3577: of a va_list, and a complete type that doesn't promote (char, short, float, _Bool are
                // -Wvarargs'); anything else is the full pipeline's.
                {
                    const uint32_t sk = arg_t & BUSTER_FTYPE_SCALAR_MASK;
                    const uint32_t scalar = (uint32_t)((arg_t & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0);
                    const uint32_t promotes = scalar & ((uint32_t)(sk <= FTYPE_U16) | (uint32_t)(sk == FTYPE_F32));
                    if (!fast_cc_is_va_list(cc, ex.vstack[ex.vsp - 1u].type) || (promotes | BUSTER_FAST_TYPE_INCOMPLETE(cc, arg_t)) != 0)
                    {
                        fast_cc_bail(cc, "va_arg the full pipeline refuses");
                        break;
                    }
                }
                FastVal ap_v = ex.vstack[--ex.vsp];
                FastVal res = { 0 };
                fval_emit_va_arg(&ex, &ap_v, arg_t, &res);
                ex.vstack[ex.vsp++] = res;
                continue;
            }
            if (top == FEOP_VA_COPY)
            {
                // Leave FEOP_VA_COPY on stack and parse `src`!
                fval_spill_one(&ex, &ex.vstack[ex.vsp - 1u]);
                cc->pos += 1u;
                expect_val = true;
                continue;
            }
            // Otherwise comma operator inside `(...)` or `[...]`
            if (osp < 128u)
            {
                const uint8_t below = BUSTER_FSEQ_BELOW_IF(ops, osp, logging);
                ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + FTOK_COMMA), .seq_lo = below, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) };
            }
            cc->pos += 1u;
            expect_val = true;
            continue;
        }

        // Binary / logical / ternary `?` / assignment operators
        const uint8_t prec = fast_expr_tok_prec(k);
        if (prec == 0) { break; }
        const bool rassoc = fast_expr_is_right_assoc(k);
        while (osp > 0 && !cc->bailout)
        {
            const uint8_t tprec = fast_expr_op_prec(ops[osp - 1u].op);
            if (tprec == 0) { break; }
            if (tprec > prec || (tprec == prec && !rassoc))
            {
                fast_expr_reduce_top(&ex, ops, &osp);
            }
            else { break; }
        }
        if (cc->bailout) { break; }

        if (k == FTOK_LAND || k == FTOK_LOR)
        {
            if (ex.vsp < 1u) { fast_cc_bail(cc, "logical lhs missing"); break; }
            if (osp >= 128u) { fast_cc_bail(cc, "logical nesting depth"); break; }
            FastVal lhs = ex.vstack[--ex.vsp];
            uint32_t sc_lbl = 0;
            if (!BUSTER_FAST_CONDITION_QUIET(cc, &lhs)) { fast_cc_bail(cc, "logical operator's operand the full pipeline questions"); break; }
            // A constant left side that decides the value leaves the right side dead, checked without code (#3592).
            const uint32_t decided = (uint32_t)((ex.eval_only == 0) & (lhs.kind == FVAL_IMM) & ((lhs.imm != 0) == (k == FTOK_LOR)));
            ex.eval_only = (uint8_t)(ex.eval_only + decided);
            if (ex.eval_only == 0)
            {
                // Spill everything below LHS so both branch paths have identical empty reg state!
                fval_spill_all(&ex, ex.vsp, ex.arg_sp);
                sc_lbl = fast_label_new(cc->obj);
                fval_branch(&ex, &lhs, k == FTOK_LOR, sc_lbl);
                ex.gp_used = 0;
                ex.xmm_used = 0;
            }
            const uint8_t below = BUSTER_FSEQ_BELOW_IF(ops, osp, logging);
            ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + k), .lbl0 = sc_lbl, .seq_lo = below, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging),
                                       .constant = (uint8_t)decided };
        }
        else if (k == FTOK_QUESTION)
        {
            if (ex.vsp < 1u) { fast_cc_bail(cc, "ternary cond missing"); break; }
            if (osp >= 128u) { fast_cc_bail(cc, "ternary nesting depth"); break; }
            FastVal cond = ex.vstack[--ex.vsp];
            uint32_t else_lbl = 0;
            if (!BUSTER_FAST_CONDITION_QUIET(cc, &cond)) { fast_cc_bail(cc, "condition the full pipeline refuses or questions"); break; }
            // A constant condition takes no branch, and its dead arm is checked without code (#3592): musl's isnan()
            // of a double has a long double arm, a call of __fpclassifyl.
            const uint32_t constant = (uint32_t)((ex.eval_only == 0) & (cond.kind == FVAL_IMM)) * (BUSTER_FCONSTANT_FALSE + (uint32_t)(cond.imm != 0));
            ex.eval_only = (uint8_t)(ex.eval_only + (constant == BUSTER_FCONSTANT_FALSE));
            if ((ex.eval_only == 0) & (constant == BUSTER_FCONSTANT_NONE))
            {
                fval_spill_all(&ex, ex.vsp, ex.arg_sp);
                else_lbl = fast_label_new(cc->obj);
                fval_branch(&ex, &cond, false, else_lbl);
                ex.gp_used = 0;
                ex.xmm_used = 0;
            }
            const uint8_t below = BUSTER_FSEQ_BELOW_IF(ops, osp, logging);
            ops[osp++] = (FastExprOp){ .op = FEOP_TERNARY_Q, .lbl0 = else_lbl, .seq_lo = below, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging),
                                       .constant = (uint8_t)constant };
        }
        else
        {
            // #3577: a divisor (`/=`'s or `%=`'s whole right operand) that may load zero from a const object is the full
            // pipeline's -Wdivision-by-zero where it is evaluated: refused. Looked at only where its first token may start
            // one: a parenthesis, `*`, `&`, or an object that may fold to zero.
            const uint32_t compound_divide = (uint32_t)(k == FTOK_DIV_ASSIGN) | (uint32_t)(k == FTOK_MOD_ASSIGN);
            const uint32_t divide = (uint32_t)(k == FTOK_SLASH) | (uint32_t)(k == FTOK_PERCENT) | compound_divide;
            if (divide != 0)
            {
                const uint32_t first_at = cc->pos + (uint32_t)(cc->pos + 1u < cc->tcount);
                const uint8_t first = cc->tk[first_at];
                const uint32_t first_id = cc->td[first_at];
                const uint32_t first_known = (uint32_t)(first_id < cc->env_cap);
                const uint32_t first_si = cc->sym_cur[first_id * first_known] * first_known;
                const uint32_t divisor_start = (uint32_t)(first == FTOK_LPAREN) | (uint32_t)(first == FTOK_STAR) | (uint32_t)(first == FTOK_AMP) |
                                               (uint32_t)(first == FTOK_KW_EXTENSION) |
                                               ((uint32_t)(first == FTOK_IDENT) & (uint32_t)(cc->syms[first_si].pad_sym[1] == BUSTER_FFOLD_ZERO));
                if (divisor_start != 0 && ex.eval_only == 0 && !cc->quiet && fast_cc_divisor_may_fold_zero(cc, cc->pos + 1u, compound_divide != 0))
                {
                    fast_cc_bail(cc, "a divisor that may fold to zero through a const object");
                    break;
                }
            }
            if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + k), .lbl2 = cc->pos, .seq_mid = BUSTER_FSEQ_MARK_IF(&ex, logging) }; }
        }
        cc->pos += 1u;
        expect_val = true;
    }

    // Flush remaining operators on stack!
    while (osp > 0 && !cc->bailout)
    {
        if (ops[osp - 1u].op < FEOP_UNARY_PLUS || ops[osp - 1u].op == FEOP_TERNARY_Q)
        {
            fast_cc_bail(cc, "unclosed operator in expr");
            break;
        }
        fast_expr_reduce_top(&ex, ops, &osp);
    }

    if (cc->bailout) { return; }
    if (ex.vsp != 1u)
    {
        fast_cc_bail(cc, "expression did not produce single value");
        return;
    }
    FastVal final_v = ex.vstack[0];
    // #3577: -Wunsequenced. Without the log: no variable modified, or one whose modification is the expression's last
    // operator (its operands are evaluated before it stores; BUSTER_FSEQ_MODIFIED), is quiet, and any other modification
    // looks further (fast_cc_sequenced_lazy). With it (a compile again): a variable modified and named or modified again
    // where nothing sequences the two (seq_hit, set by the log, says where that may be).
    if (!logging)
    {
        if (ex.seq_mod_count != 0 && ((uint32_t)(ex.seq_mod_count != 1u) | (uint32_t)(final_v.seq_ident != BUSTER_FSEQ_MODIFIED)) != 0 &&
            !fast_cc_sequenced_lazy(cc, &ex, stop_at_comma))
        {
            return;
        }
    }
    else if ((ex.seq_hit | ((uint32_t)(ex.seq_count >= 254u) & (uint32_t)(ex.seq_bloom != 0))) != 0 && !fast_cc_sequenced(&ex))
    {
        fast_cc_bail(cc, "modifications and uses of a variable the full pipeline's -Wunsequenced questions");
        return;
    }
    if (is_branch)
    {
        // An assignment as a condition, outside parentheses, is -Wparentheses (#3577).
        if (final_v.op == FTOK_ASSIGN) { fast_cc_bail(cc, "assignment as a condition"); return; }
        cc->branch_constant = (uint8_t)((uint32_t)(final_v.kind == FVAL_IMM) * (BUSTER_FCONSTANT_FALSE + (uint32_t)(final_v.imm != 0)));
        fval_branch(&ex, &final_v, branch_on_true, branch_label);
    }
    else if (discard_result)
    {
        // A statement's value: one with no effect is -Wunused-value, a warn_unused_result call's -Wunused-result.
        if (BUSTER_FAST_DISCARD_QUIET(final_v) == 0) { fast_cc_bail(cc, "discarded value"); return; }
        fval_release(&ex, &final_v);
    }
    else if (out_val != NULL)
    {
        *out_val = final_v;
    }
}

// Main iterative C17 expression compiler (#3577: without the -Wunsequenced log; fast_cc_expr_compile).
// If `is_branch` is true, emits a direct branch to `branch_label` and releases the result.
// If `discard_result` is true, releases the result (used for expression statements).
static BUSTER_DIRECT_HOT void fast_cc_expr(FastCCState* cc, bool stop_at_comma, bool discard_result,
                         bool is_branch, bool branch_on_true, uint32_t branch_label, FastVal* out_val)
{
    fast_cc_expr_compile(cc, stop_at_comma, discard_result, is_branch, branch_on_true, branch_label, out_val, false);
}

// #3577: the expression at cc->pos compiled again without code for its -Wunsequenced log (fast_cc_sequenced_again); its
// end bails where the full pipeline warns.
static __attribute__((cold, noinline)) void fast_cc_expr_logged(FastCCState* cc, bool stop_at_comma)
{
    fast_cc_expr_compile(cc, stop_at_comma, false, false, false, 0u, NULL, true);
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 12: Iterative local variable initializer (ZERO recursion; calls fast_cc_expr only!)

static inline void fast_expr_ctx_reset(FastExprCtx* ex, FastCCState* cc)
{
    ex->cc = cc;
    ex->vsp = 0;
    ex->arg_sp = 0;
    ex->gp_used = 0;
    ex->xmm_used = 0;
    ex->eval_only = (uint8_t)(cc->dead != 0);
    ex->unevaluated = 0;
    ex->seq_logging = 0;
    ex->seq_count = 0;
    ex->seq_hit = 0;
    ex->seq_mods = 0;
    ex->seq_regions = 0;
    ex->seq_bloom = 0;
    ex->seq_mod_count = 0;
    ex->seq_start_pos = cc->pos;
    ex->cmp_pos = UINT32_MAX;
}

static void fast_cc_store_expr_to_val(FastCCState* cc, FastVal dst, bool stop_at_comma)
{
    FastVal rhs = { 0 };
    fast_cc_expr(cc, stop_at_comma, false, false, false, 0, &rhs);
    if (cc->bailout) { return; }
    cc->init_kind = rhs.kind;
    cc->init_value = rhs.imm;
    FastExprCtx ex;
    fast_expr_ctx_reset(&ex, cc);
    if (fval_holds_gp(&rhs)) { ex.gp_used |= (uint16_t)(1u << rhs.reg); }
    if (fval_holds_xmm(&rhs)) { ex.xmm_used |= (uint8_t)(1u << rhs.reg); }
    fval_store(&ex, &dst, &rhs, false, NULL);
    fast_cc_reset_tmps(cc);
}

// Compile initializer for a local variable `var_type` (which may be an incomplete array `int a[] = {...}` or `char s[] = "..."`).
// Allocates the local's stack slot and returns its `[rbp + off]` offset!
static __attribute__((noinline)) int32_t fast_cc_init_local(FastCCState* cc, FastType* io_type, uint32_t extra_align)
{
    typedef struct LInitFrame LInitFrame;
    struct LInitFrame
    {
        FastType type;
        int32_t base_off;
        uint32_t idx;
        bool braced;
        bool deep;          // the last designator reached into a member's member (#3577)
        uint64_t touched;   // the positions (members, elements) under 64 initialized (#3577: fast_cc_init_overrides)
    };

    FastType root_t = *io_type;
    if (ftype_is_unsupported(root_t)) { fast_cc_bail(cc, "unsupported local var type"); return 0; }
    uint32_t al = ftype_align(cc, root_t);
    if (extra_align > al) { al = extra_align; }

    // Fast path 1: `char s[] = "hello"` or `char s[N] = "hello"` (optionally braced)
    const bool is_arr = ftype_is_array(root_t);
    if (is_arr && ftype_is_char_type(cc->ctypes[root_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
        (fast_cc_peek(cc) == FTOK_STR_LIT ||
         (fast_cc_peek(cc) == FTOK_LBRACE && fast_cc_peek_at(cc, 1u) == FTOK_STR_LIT)))
    {
        const bool br = fast_cc_peek(cc) == FTOK_LBRACE;
        if (br) { cc->pos += 1u; }
        uint32_t slen = 0;
        const char* sp = fast_cc_parse_string_lit(cc, &slen);
        if (br)
        {
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
        }
        FastComplexType* ar = cc->ctypes + (root_t & BUSTER_FTYPE_CIDX_MASK);
        if (ar->count == UINT32_MAX)
        {
            ar->count = slen + 1u;
            ar->size = slen + 1u;
            ar->complete = 1u;
        }
        const int32_t off = fast_cc_alloc_local(cc, ar->size, al);
        const uint32_t ro_off = fast_obj_add_string_lit(cc->obj, sp, slen);
        const uint32_t cpy = (slen + 1u) < ar->size ? (slen + 1u) : ar->size;
        x86_lea_sym(cc->obj, FREG_R10, cc->obj->sec_sym_rodata_str, (int64_t)ro_off);
        x86_emit_memcpy_inline(cc->obj, FREG_RBP, off, FREG_R10, 0, cpy, FREG_RAX);
        if (ar->size > cpy)
        {
            x86_emit_memzero_inline(cc->obj, FREG_RBP, off + (int32_t)cpy, ar->size - cpy);
        }
        return off;
    }

    // Fast path 2: scalar or non-braced struct copy (`T x = expr;`)
    if (ftype_is_scalar(root_t) || (ftype_is_record(root_t) && fast_cc_peek(cc) != FTOK_LBRACE))
    {
        const int32_t off = fast_cc_alloc_local(cc, ftype_size(cc, root_t), al);
        const bool br = fast_cc_peek(cc) == FTOK_LBRACE;
        if (br) { cc->pos += 1u; }
        fast_cc_store_expr_to_val(cc, (FastVal){ .type = root_t, .kind = FVAL_LOCAL, .off = off }, true);
        if (br)
        {
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
        }
        return off;
    }

    // Braced aggregate local initializer `{ ... }`
    if (fast_cc_peek(cc) != FTOK_LBRACE)
    {
        fast_cc_bail(cc, "expected { for local aggregate init");
        return 0;
    }

    // If incomplete local array `T a[] = { e0, e1, ... }`, count top-level elements first so we know exact stack size!
    if (is_arr && cc->ctypes[root_t & BUSTER_FTYPE_CIDX_MASK].count == UINT32_MAX)
    {
        uint32_t scan = cc->pos + 1u;
        uint32_t depth = 1u;
        uint32_t elems = 0;
        bool has_tok = false;
        while (scan < cc->tcount && depth > 0)
        {
            const uint8_t sk = cc->tk[scan++];
            if (sk == FTOK_LBRACE || sk == FTOK_LPAREN || sk == FTOK_LBRACKET) { depth += 1u; has_tok = true; }
            else if (sk == FTOK_RBRACE || sk == FTOK_RPAREN || sk == FTOK_RBRACKET)
            {
                depth -= 1u;
                if (depth == 0 && has_tok) { elems += 1u; }
            }
            else if (depth == 1u && sk == FTOK_COMMA) { elems += 1u; has_tok = false; }
            else if (depth == 1u) { has_tok = true; }
        }
        FastComplexType* ar = cc->ctypes + (root_t & BUSTER_FTYPE_CIDX_MASK);
        ar->count = elems;
        ar->size = elems * ftype_size(cc, ar->inner);
        ar->complete = 1u;
    }

    const uint32_t total_sz = ftype_size(cc, root_t);
    const int32_t root_off = fast_cc_alloc_local(cc, total_sz, al);
    // Zero-initialize the entire local aggregate first (handles `{ 0 }`, partial init, and padding in 1 shot!)
    x86_emit_memzero_inline(cc->obj, FREG_RBP, root_off, total_sz);

    // Super-fast path for `{ 0 }` (extremely common in SQLite/C!)
    if (fast_cc_peek_at(cc, 1u) == FTOK_INT_LIT && cc->td[cc->pos + 1u] == 0u &&
        (fast_cc_peek_at(cc, 2u) == FTOK_RBRACE ||
         (fast_cc_peek_at(cc, 2u) == FTOK_COMMA && fast_cc_peek_at(cc, 3u) == FTOK_RBRACE)))
    {
        cc->pos += fast_cc_peek_at(cc, 2u) == FTOK_RBRACE ? 3u : 4u;
        return root_off;
    }

    LInitFrame st[32];
    uint32_t sp = 0;
    uint64_t overridden = 0;
    cc->pos += 1u; // '{'
    st[sp++] = (LInitFrame){ .type = root_t, .base_off = root_off, .idx = 0, .braced = true };

    while (sp > 0 && cc->pos < cc->tcount && !cc->bailout)
    {
        LInitFrame* f = st + (sp - 1u);
        if (fast_cc_peek(cc) == FTOK_RBRACE)
        {
            if (f->braced) { cc->pos += 1u; }
            sp -= 1u;
            if (sp > 0 && f->braced && fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            continue;
        }

        // Designated initializer `.field =` or `[idx] =`
        if (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
        {
            while (sp > 1u && !st[sp - 1u].braced) { sp -= 1u; }
            f = st + (sp - 1u);
            FastType cur_t = f->type;
            int32_t cur_off = f->base_off;
            const FastMemberInfo* cur_bf = NULL;
            const uint32_t idx_before = f->idx;
            uint32_t position = UINT32_MAX;
            uint32_t components = 0;
            while (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                components += 1u;
                if (fast_cc_peek(cc) == FTOK_DOT)
                {
                    cc->pos += 1u;
                    if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "local .designator"); break; }
                    const FastComplexType* rec = cc->ctypes + (cur_t & BUSTER_FTYPE_CIDX_MASK);
                    const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
                    if (mi == NULL) { fast_cc_bail(cc, "unknown local .designator"); break; }
                    if (cur_t == f->type)
                    {
                        position = (uint32_t)(mi - (cc->member_info + rec->start));
                        f->idx = position + 1u;
                    }
                    cur_off += (int32_t)mi->byte_offset;
                    cur_t = mi->type;
                    cur_bf = mi->bit_width != 0 ? mi : NULL;
                    cc->pos += 1u;
                }
                else
                {
                    cc->pos += 1u;
                    const int64_t didx = fast_cc_const_int_expr(cc, false);
                    if (fast_cc_peek(cc) != FTOK_RBRACKET || didx < 0 || !ftype_is_array(cur_t)) { fast_cc_bail(cc, "local [designator]"); break; }
                    cc->pos += 1u;
                    const FastComplexType* ar = cc->ctypes + (cur_t & BUSTER_FTYPE_CIDX_MASK);
                    if (cur_t == f->type)
                    {
                        position = (uint32_t)didx;
                        f->idx = (uint32_t)didx + 1u;
                    }
                    cur_off += (int32_t)((uint32_t)didx * ftype_size(cc, ar->inner));
                    cur_t = ar->inner;
                    cur_bf = NULL;
                }
            }
            if (fast_cc_peek(cc) == FTOK_ASSIGN) { cc->pos += 1u; }
            if (cc->bailout) { break; }
            // #3577: a target the frame initialized (-Winitializer-overrides); and the positional elements after a designator
            // into a member's member continue there, which this path doesn't follow.
            if (fast_cc_init_overrides(cc, f->type, f->base_off, idx_before, &f->touched, position, (int64_t)cur_off * 8 + (cur_bf != NULL ? cur_bf->bit_offset : 0)))
            {
                fast_cc_bail(cc, "designator overriding what the initializer initialized");
                break;
            }
            f->deep = components >= 2u;
            if (ftype_is_scalar(cur_t) || (ftype_is_record(cur_t) && fast_cc_peek(cc) != FTOK_LBRACE))
            {
                FastVal dst = { .type = cur_t, .kind = cur_bf != NULL ? FVAL_BITFIELD : FVAL_LOCAL, .bf_base = FVAL_LOCAL, .off = cur_off };
                if (cur_bf != NULL) { dst.bf_off = cur_bf->bit_offset; dst.bf_width = cur_bf->bit_width; dst.bf_unit = cur_bf->bit_unit_size; dst.bf_signed = cur_bf->bit_signed; }
                fast_cc_store_expr_to_val(cc, dst, true);
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            }
            else if (ftype_is_array(cur_t) && ftype_is_char_type(cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
                     fast_cc_peek(cc) == FTOK_STR_LIT)
            {
                uint32_t slen = 0;
                const char* sp_s = fast_cc_parse_string_lit(cc, &slen);
                const uint32_t asz = ftype_size(cc, cur_t);
                const uint32_t cpy = slen < asz ? slen : asz;
                const uint32_t ro = fast_obj_add_string_lit(cc->obj, sp_s, slen);
                x86_lea_sym(cc->obj, FREG_R10, cc->obj->sec_sym_rodata_str, (int64_t)ro);
                x86_emit_memcpy_inline(cc->obj, FREG_RBP, cur_off, FREG_R10, 0, cpy, FREG_RAX);
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            }
            else if (fast_cc_peek(cc) == FTOK_LBRACE)
            {
                cc->pos += 1u;
                if (sp >= 32u) { fast_cc_bail(cc, "local init depth"); break; }
                st[sp++] = (LInitFrame){ .type = cur_t, .base_off = cur_off, .idx = 0, .braced = true };
            }
            else
            {
                if (sp >= 32u) { fast_cc_bail(cc, "local init depth"); break; }
                st[sp++] = (LInitFrame){ .type = cur_t, .base_off = cur_off, .idx = 0, .braced = false };
            }
            continue;
        }

        // Positional element
        if (f->deep)
        {
            fast_cc_bail(cc, "positional element after a designator into a member's member");
            break;
        }
        FastType elem_t = FTYPE_VOID;
        int32_t elem_off = f->base_off;
        const FastMemberInfo* elem_bf = NULL;
        bool done = false;

        if (ftype_is_array(f->type))
        {
            const FastComplexType* ar = cc->ctypes + (f->type & BUSTER_FTYPE_CIDX_MASK);
            if (f->idx >= ar->count) { done = true; }
            else
            {
                elem_t = ar->inner;
                elem_off = f->base_off + (int32_t)(f->idx * ftype_size(cc, ar->inner));
                overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                f->idx += 1u;
            }
        }
        else if (ftype_is_record(f->type))
        {
            const FastComplexType* rec = cc->ctypes + (f->type & BUSTER_FTYPE_CIDX_MASK);
            if (f->idx >= rec->count || (rec->kind == FCTYPE_UNION && f->idx >= 1u)) { done = true; }
            else
            {
                const FastMemberInfo* mi = cc->member_info + rec->start + f->idx;
                elem_t = mi->type;
                elem_off = f->base_off + (int32_t)mi->byte_offset;
                elem_bf = mi->bit_width != 0 ? mi : NULL;
                overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                f->idx += 1u;
                // Skip flattened anonymous-union siblings sharing the same byte_offset in a struct!
                if (rec->kind == FCTYPE_STRUCT && mi->bit_width == 0)
                {
                    while (f->idx < rec->count && cc->member_info[rec->start + f->idx].byte_offset == mi->byte_offset &&
                           cc->member_info[rec->start + f->idx].bit_width == 0)
                    {
                        overridden |= f->touched & BUSTER_FAST_INIT_BIT(f->idx);
                        f->touched |= BUSTER_FAST_INIT_BIT(f->idx);
                        f->idx += 1u;
                    }
                }
            }
        }
        else { done = true; }

        if (done)
        {
            if (!f->braced) { sp -= 1u; continue; }
            fast_cc_bail(cc, "excess elements in local init");
            break;
        }

        if (ftype_is_array(elem_t) && ftype_is_char_type(cc->ctypes[elem_t & BUSTER_FTYPE_CIDX_MASK].inner) &&
            fast_cc_peek(cc) == FTOK_STR_LIT)
        {
            uint32_t slen = 0;
            const char* sp_s = fast_cc_parse_string_lit(cc, &slen);
            const uint32_t asz = ftype_size(cc, elem_t);
            const uint32_t cpy = slen < asz ? slen : asz;
            const uint32_t ro = fast_obj_add_string_lit(cc->obj, sp_s, slen);
            x86_lea_sym(cc->obj, FREG_R10, cc->obj->sec_sym_rodata_str, (int64_t)ro);
            x86_emit_memcpy_inline(cc->obj, FREG_RBP, elem_off, FREG_R10, 0, cpy, FREG_RAX);
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            continue;
        }

        if (ftype_is_scalar(elem_t))
        {
            const bool b2 = fast_cc_peek(cc) == FTOK_LBRACE;
            if (b2) { cc->pos += 1u; }
            FastVal dst = { .type = elem_t, .kind = elem_bf != NULL ? FVAL_BITFIELD : FVAL_LOCAL, .bf_base = FVAL_LOCAL, .off = elem_off };
            if (elem_bf != NULL) { dst.bf_off = elem_bf->bit_offset; dst.bf_width = elem_bf->bit_width; dst.bf_unit = elem_bf->bit_unit_size; dst.bf_signed = elem_bf->bit_signed; }
            fast_cc_store_expr_to_val(cc, dst, true);
            if (b2)
            {
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
                else { fast_cc_bail(cc, "expected } after braced scalar static init"); break; }
            }
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
        }
        else if (fast_cc_peek(cc) == FTOK_LBRACE)
        {
            cc->pos += 1u;
            if (sp >= 32u) { fast_cc_bail(cc, "local init depth"); break; }
            st[sp++] = (LInitFrame){ .type = elem_t, .base_off = elem_off, .idx = 0, .braced = true };
        }
        else if (ftype_is_record(elem_t))
        {
            // Could be a struct-typed expression OR brace-elided scalar elements! Check if expression evaluates to record?
            // Safe & fast: if next token is an identifier whose sym has record type `elem_t`, do struct copy; else brace-elide!
            bool is_struct_copy = false;
            if (fast_cc_peek(cc) == FTOK_IDENT)
            {
                const uint32_t id = fast_cc_data(cc);
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                if (si != 0 && BUSTER_FTYPE_CODEGEN(ftype_unqual(cc->syms[si].type)) == BUSTER_FTYPE_CODEGEN(ftype_unqual(elem_t))) { is_struct_copy = true; }
            }
            if (is_struct_copy)
            {
                fast_cc_store_expr_to_val(cc, (FastVal){ .type = elem_t, .kind = FVAL_LOCAL, .off = elem_off }, true);
                if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            }
            else
            {
                if (sp >= 32u) { fast_cc_bail(cc, "local init depth"); break; }
                st[sp++] = (LInitFrame){ .type = elem_t, .base_off = elem_off, .idx = 0, .braced = false };
            }
        }
        else
        {
            if (sp >= 32u) { fast_cc_bail(cc, "local init depth"); break; }
            st[sp++] = (LInitFrame){ .type = elem_t, .base_off = elem_off, .idx = 0, .braced = false };
        }
    }
    // #3577: a positional element over one a designator initialized (BUSTER_FAST_INIT_BIT).
    if (overridden != 0)
    {
        fast_cc_bail(cc, "initializer overrides prior initialization");
    }
    return root_off;
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 13: Iterative Statement & Function Body Compiler (ZERO recursion; dense .rodata PC32 jump tables!)

typedef enum FastStmtKind
{
    FSTK_BLOCK = 1,
    FSTK_IF_THEN = 2,
    FSTK_IF_ELSE = 3,
    FSTK_WHILE = 4,
    FSTK_DO_WHILE = 5,
    FSTK_FOR = 6,
    FSTK_SWITCH = 7
} FastStmtKind;

typedef struct FastStmtFrame FastStmtFrame;
struct FastStmtFrame
{
    uint8_t kind;
    uint8_t entered_body;
    uint8_t dead;          // BUSTER_FDEAD_* (IF_THEN, IF_ELSE)
    uint16_t sw_idx;
    uint32_t lbl0;         // else_lbl / cond_lbl / body_lbl / dispatch_lbl
    uint32_t lbl1;         // break_lbl / cont_lbl / end_lbl
    uint32_t lbl2;         // break_lbl (for / do-while)
    uint32_t scope_mark;   // undo_count mark for block / for
    uint32_t step_pos;     // token index of for-step expr (UINT32_MAX if none)
    // #3577: what -Wreturn-type's reachability needs: whether an if's then arm ended reachable, a break or a continue
    // left the loop or switch, and a loop's condition is a constant true (`for (;;)`, `while (1)`).
    uint8_t then_live;
    uint8_t broke;
    uint8_t continued;
    uint8_t infinite;
    // #3577: -Wdangling-else: the then statement is an if with an else (BUSTER_FSTK_ENDS_IN_ELSE), and this if's own
    // statement had a label, case or default before it, which passes nothing up.
    uint8_t trailing_else;
    uint8_t labeled;
};

// #3577: -Wdangling-else, as the full pipeline warns it: an if without an else whose then statement is, in no braces and
// with no label, an if with an else (not a loop's or a switch's body, a do's or an else arm). An if with an else marks
// its parent frame (SSTACK[SSP - 2], before its pop) where that is an if's then, and that if refuses where no else
// follows.
#define BUSTER_FSTK_ENDS_IN_ELSE(sstack, ssp) \
    if ((ssp) >= 2u) \
    { \
        FastStmtFrame* const fstk_parent = (sstack) + ((ssp) - 2u); \
        fstk_parent->trailing_else |= (uint8_t)(fstk_parent->kind == FSTK_IF_THEN); \
    }

#define BUSTER_FSWITCH_BUCKET_BITS 11u

// Whether `__attribute__((fallthrough));`, its tokens ending before `after`, directly precedes a case or default label
// of the switch it is in, as the full pipeline requires ("fallthrough annotation does not directly precede switch
// label", #3577): the label next, or after the `}` of enclosing blocks that follow and out of if arms, but neither out
// of a loop's body nor of the switch's own. Anything else (an else that follows, a label) is refused, the full
// pipeline's or not.
static bool fast_cc_fallthrough_placed(const FastCCState* cc, const FastStmtFrame* stack, uint32_t depth, uint32_t after)
{
    bool in_switch = false;
    for (uint32_t s = 0; s < depth; s += 1)
    {
        in_switch = in_switch || stack[s].kind == FSTK_SWITCH;
    }
    uint32_t i = after;
    uint32_t f = depth;
    bool placed = false;
    bool walking = in_switch;
    while (walking && f > 0)
    {
        const FastStmtFrame* frame = stack + (f - 1u);
        const bool label = i < cc->tcount && (cc->tk[i] == FTOK_KW_CASE || cc->tk[i] == FTOK_KW_DEFAULT);
        const bool switch_body = frame->kind == FSTK_BLOCK && f >= 2u && stack[f - 2u].kind == FSTK_SWITCH;
        if (frame->kind == FSTK_WHILE || frame->kind == FSTK_DO_WHILE || frame->kind == FSTK_FOR)
        {
            walking = false;
        }
        else if (label)
        {
            placed = true;
            walking = false;
        }
        else if (frame->kind == FSTK_IF_THEN || frame->kind == FSTK_IF_ELSE)
        {
            f -= 1u;
        }
        else if (frame->kind == FSTK_BLOCK && !switch_body && i < cc->tcount && cc->tk[i] == FTOK_RBRACE)
        {
            i += 1u;
            f -= 1u;
        }
        else
        {
            walking = false;
        }
    }
    return placed;
}

// An `if` with a constant condition (#3592): its dead branch is checked as an operand of sizeof is, with no code, when
// fast_cc_dead_simple takes it. FastCCState.dead counts the branches being checked so around a statement.
#define BUSTER_FDEAD_NONE 0u
#define BUSTER_FDEAD_THEN 1u       // a false condition: the then branch is checked without code
#define BUSTER_FDEAD_TRUE 2u       // a true condition: an else branch would be dead
#define BUSTER_FDEAD_ELSE 3u       // the else branch of a true condition is checked without code

// The end of an expression's tokens, at `terminator` outside brackets, or UINT32_MAX where the tokens hold a statement
// expression, which the statement compiler would compile, or end otherwise.
static __attribute__((cold, noinline)) uint32_t fast_cc_dead_expression_end(const FastCCState* cc, uint32_t pos, uint8_t terminator)
{
    uint32_t depth = 0;
    uint32_t end = UINT32_MAX;
    bool ok = true;
    for (uint32_t i = pos; i < cc->tcount && ok && end == UINT32_MAX; i += 1)
    {
        const uint8_t k = cc->tk[i];
        const bool opens = k == FTOK_LPAREN || k == FTOK_LBRACKET || k == FTOK_LBRACE;
        const bool closes = k == FTOK_RPAREN || k == FTOK_RBRACKET || k == FTOK_RBRACE;
        end = depth == 0 && k == terminator ? i : UINT32_MAX;
        ok = !(k == FTOK_LBRACE && i > 0 && cc->tk[i - 1u] == FTOK_LPAREN) && (end != UINT32_MAX || depth != 0 || (!closes && k != FTOK_SEMI));
        depth = depth + (uint32_t)opens - (uint32_t)closes;
    }
    return ok ? end : UINT32_MAX;
}

// Whether the statement at `pos` can be checked without code as a dead branch (#3592): blocks, `if`s and expression
// statements, with no declaration, label, jump, loop, switch, asm or statement expression, which would need a frame
// slot, a jump target or the statement compiler's code. Scalar and bounded: it reads only the dead branches of a unit's
// constant `if` conditions, a few tokens each (musl's FORCE_EVAL has `{ fp_force_evall(x); }`).
#define BUSTER_FDEAD_SCAN_BLOCK 0u
#define BUSTER_FDEAD_SCAN_IF 1u
#define BUSTER_FDEAD_SCAN_ELSE 2u

static __attribute__((cold, noinline)) bool fast_cc_dead_simple(const FastCCState* cc, uint32_t pos)
{
    uint8_t frames[32];
    uint32_t depth = 0;
    bool ok = true;
    bool ended = false;
    bool done = false;
    while (ok && !done)
    {
        const uint8_t k = pos < cc->tcount ? cc->tk[pos] : FTOK_EOF;
        if (ended)
        {
            // After a statement, a block takes its next one and an `if` its else; an `if` without one more ends too.
            if (depth == 0)
            {
                done = true;
            }
            else if (frames[depth - 1u] == BUSTER_FDEAD_SCAN_BLOCK)
            {
                ended = false;
            }
            else if (frames[depth - 1u] == BUSTER_FDEAD_SCAN_IF && k == FTOK_KW_ELSE)
            {
                frames[depth - 1u] = BUSTER_FDEAD_SCAN_ELSE;
                pos += 1u;
                ended = false;
            }
            else
            {
                depth -= 1u;
            }
        }
        else if (k == FTOK_RBRACE && depth > 0 && frames[depth - 1u] == BUSTER_FDEAD_SCAN_BLOCK)
        {
            depth -= 1u;
            pos += 1u;
            ended = true;
        }
        else if (k == FTOK_LBRACE || k == FTOK_KW_IF)
        {
            const bool condition = k == FTOK_KW_IF && pos + 1u < cc->tcount && cc->tk[pos + 1u] == FTOK_LPAREN;
            const uint32_t close = condition ? fast_cc_dead_expression_end(cc, pos + 2u, FTOK_RPAREN) : UINT32_MAX;
            ok = depth < 32u && (k == FTOK_LBRACE || close != UINT32_MAX);
            if (ok)
            {
                frames[depth] = k == FTOK_LBRACE ? BUSTER_FDEAD_SCAN_BLOCK : BUSTER_FDEAD_SCAN_IF;
                depth += 1u;
                pos = k == FTOK_LBRACE ? pos + 1u : close + 1u;
            }
        }
        else if (k == FTOK_SEMI)
        {
            pos += 1u;
            ended = true;
        }
        else
        {
            const bool statement = k == FTOK_KW_RETURN || k == FTOK_KW_BREAK || k == FTOK_KW_CONTINUE || k == FTOK_KW_GOTO ||
                                   k == FTOK_KW_WHILE || k == FTOK_KW_DO || k == FTOK_KW_FOR || k == FTOK_KW_SWITCH ||
                                   k == FTOK_KW_CASE || k == FTOK_KW_DEFAULT || k == FTOK_KW_ELSE || k == FTOK_KW_ASM ||
                                   k == FTOK_KW_STATIC_ASSERT || k == FTOK_KW_ATTRIBUTE || k == FTOK_KW_EXTENSION ||
                                   k == FTOK_RBRACE || k == FTOK_EOF;
            const bool label = k == FTOK_IDENT && pos + 1u < cc->tcount && cc->tk[pos + 1u] == FTOK_COLON;
            const uint32_t semicolon = statement || label ? UINT32_MAX : fast_cc_dead_expression_end(cc, pos, FTOK_SEMI);
            ok = semicolon != UINT32_MAX && !fast_cc_is_type_start(cc, pos);
            pos = ok ? semicolon + 1u : pos;
            ended = true;
        }
    }
    return ok;
}


typedef struct FastSwitchCase FastSwitchCase;
struct FastSwitchCase
{
    int64_t val;
    uint32_t label_id;
    uint16_t previous;  // one-based index of the previous case in this bucket
    uint16_t bucket;
};

typedef struct FastSwitchCtx FastSwitchCtx;
struct FastSwitchCtx
{
    int32_t val_slot;      // [rbp + val_slot] holds switch controlling value
    FastType val_type;
    uint32_t case_start;   // index into sw_cases[]
    uint32_t case_count;
    uint32_t default_lbl;  // UINT32_MAX if no default case
    uint32_t break_lbl;
    uint32_t enum_cidx;    // the controlling expression's enum (its type before promotion), else 0 (#3577: -Wswitch)
};

// #3577: whether a switch on an enum's value is one the full pipeline's -Wswitch is quiet on: every case value one of
// the enum's (or "case value not in enumerated type"), and without a default every enumerator's value a case's (or
// "enumeration value not handled in switch"). The enumerators are the definition's consecutive symbols (FastComplexType's
// start and count); any other is refused.
static __attribute__((cold, noinline)) bool fast_cc_switch_enum_quiet(const FastCCState* cc, const FastSwitchCtx* sw, const FastSwitchCase* cases)
{
    const FastComplexType* et = cc->ctypes + sw->enum_cidx;
    bool quiet = et->complete != 0 && et->start != 0 && et->start + et->count <= cc->sym_count;
    for (uint32_t e = 0; quiet && e < et->count; e += 1)
    {
        const FastSym* sym = cc->syms + et->start + e;
        quiet = sym->kind == FSYM_ENUM_CONST && sym->sec_off == sw->enum_cidx;
    }
    // Each case value an enumerator's.
    for (uint32_t c = 0; quiet && c < sw->case_count; c += 1)
    {
        bool found = false;
        for (uint32_t e = 0; !found && e < et->count; e += 1)
        {
            found = cc->syms[et->start + e].const_val == cases[c].val;
        }
        quiet = found;
    }
    // Without a default, each enumerator's value a case's.
    for (uint32_t e = 0; quiet && sw->default_lbl == UINT32_MAX && e < et->count; e += 1)
    {
        bool found = false;
        for (uint32_t c = 0; !found && c < sw->case_count; c += 1)
        {
            found = cc->syms[et->start + e].const_val == cases[c].val;
        }
        quiet = found;
    }
    return quiet;
}

typedef struct FastUserLabel FastUserLabel;
struct FastUserLabel
{
    uint32_t ident_id;
    uint32_t label_id;
    uint8_t defined;
    uint8_t pad[3];
};

static uint32_t fast_cc_get_user_label(FastObj* obj, FastUserLabel* ulabels, uint32_t* ucount, uint32_t cap, uint32_t id, bool define, bool* out_ok)
{
    for (uint32_t i = 0; i < *ucount; i += 1)
    {
        if (ulabels[i].ident_id == id)
        {
            if (define)
            {
                if (ulabels[i].defined) { *out_ok = false; return 0; }
                ulabels[i].defined = 1u;
            }
            return ulabels[i].label_id;
        }
    }
    if (*ucount >= cap) { *out_ok = false; return 0; }
    const uint32_t lid = fast_label_new(obj);
    ulabels[(*ucount)++] = (FastUserLabel){ .ident_id = id, .label_id = lid, .defined = define ? 1u : 0u };
    return lid;
}

// Emit switch dispatch (dense .rodata R_X86_64_PC32 jump table for large/dense switches like sqlite3VdbeExec,
// or compact compare chain for small/sparse switches!)
static __attribute__((noinline)) void fast_cc_emit_switch_dispatch(FastCCState* cc, const FastSwitchCtx* sw, const FastSwitchCase* cases)
{
    FastObj* obj = cc->obj;
    const uint32_t n = sw->case_count;
    const uint32_t def_lbl = sw->default_lbl != UINT32_MAX ? sw->default_lbl : sw->break_lbl;
    const bool w64 = ftype_size(cc, sw->val_type) == 8u;

    if (n == 0)
    {
        x86_jmp_label(obj, def_lbl);
        return;
    }

    // Load switch value into RAX (always 64-bit sign/zero-extended for clean comparison/indexing!)
    x86_load(obj, FREG_RAX, FREG_RBP, sw->val_slot, ftype_size(cc, sw->val_type), ftype_is_signed(cc, sw->val_type));
    if (!w64 && ftype_is_signed(cc, sw->val_type)) { x86_extend(obj, FREG_RAX, FREG_RAX, 4u, true); }

    // Check min/max range for O(1) dense .rodata PC32 jump table!
    int64_t min_v = cases[0].val;
    int64_t max_v = cases[0].val;
    for (uint32_t i = 1u; i < n; i += 1)
    {
        if (cases[i].val < min_v) { min_v = cases[i].val; }
        if (cases[i].val > max_v) { max_v = cases[i].val; }
    }
    const uint64_t span_m1 = (uint64_t)max_v - (uint64_t)min_v;

    if (n >= 4u && span_m1 < 1024u && (span_m1 + 1u) <= (uint64_t)n * 4u)
    {
        const uint32_t span = (uint32_t)(span_m1 + 1u);
        // Reserve a local trampoline label for default if def_lbl is not yet bound (e.g. break_lbl!)
        uint32_t def_text_off = obj->labels[def_lbl];
        uint32_t def_tramp = UINT32_MAX;
        if (def_text_off == UINT32_MAX)
        {
            def_tramp = fast_label_new(obj);
        }

        // sub rax, min_v; cmp rax, span-1; ja def_lbl
        if (min_v != 0)
        {
            if (min_v >= INT32_MIN && min_v <= INT32_MAX) { x86_alu_ri(obj, true, 5u, FREG_RAX, (int32_t)min_v); }
            else
            {
                x86_mov_ri(obj, true, FREG_RCX, (uint64_t)min_v);
                x86_alu_rr(obj, true, 0x29u, FREG_RAX, FREG_RCX);
            }
        }
        x86_alu_ri(obj, true, 7u, FREG_RAX, (int32_t)span_m1);
        x86_jcc_label(obj, FCC_A, def_lbl);

        // Emit jump table in .rodata (4 bytes per entry, aligned to 4)
        uint8_t* tbl_ptr = NULL;
        const uint32_t tbl_off = fast_obj_alloc_section(obj, FSEC_RODATA, span * 4u, 4u, &tbl_ptr);
        if (tbl_ptr == NULL) { cc->obj->overflow = true; return; }

        // lea rcx, [rip + .rodata + tbl_off]; movsxd rdx, dword [rcx + rax*4]; add rcx, rdx; jmp rcx
        x86_lea_sym(obj, FREG_RCX, obj->sec_sym_rodata, (int64_t)tbl_off);
        x86_movsxd_sib4(obj, FREG_RDX, FREG_RCX, FREG_RAX);
        x86_alu_rr(obj, true, 0x01u, FREG_RCX, FREG_RDX);
        x86_jmp_reg(obj, FREG_RCX);

        if (def_tramp != UINT32_MAX)
        {
            fast_label_bind(obj, def_tramp);
            def_text_off = obj->labels[def_tramp];
            x86_jmp_label(obj, def_lbl);
        }

        // Fill table targets (default everywhere, then overwrite present cases)
        uint32_t targets[1024];
        for (uint32_t k = 0; k < span; k += 1) { targets[k] = def_text_off; }
        for (uint32_t i = 0; i < n; i += 1)
        {
            const uint32_t idx = (uint32_t)((uint64_t)cases[i].val - (uint64_t)min_v);
            const uint32_t toff = obj->labels[cases[i].label_id];
            if (toff == UINT32_MAX) { cc->obj->overflow = true; return; }
            targets[idx] = toff;
        }
        for (uint32_t k = 0; k < span; k += 1)
        {
            // R_X86_64_PC32: S + A - P = (.text + targets[k] + 4*k) - (.rodata + tbl_off + 4*k) = (.text + targets[k]) - (.rodata + tbl_off)!
            fast_obj_add_rela(obj, &obj->rela_rodata, tbl_off + k * 4u, obj->sec_sym_text,
                              R_X86_64_PC32, (int64_t)targets[k] + (int64_t)(k * 4u));
        }
        return;
    }

    // Sparse / small switch: linear compare chain
    for (uint32_t i = 0; i < n; i += 1)
    {
        if (cases[i].val >= INT32_MIN && cases[i].val <= INT32_MAX)
        {
            x86_alu_ri(obj, true, 7u, FREG_RAX, (int32_t)cases[i].val);
        }
        else
        {
            x86_mov_ri(obj, true, FREG_RCX, (uint64_t)cases[i].val);
            x86_alu_rr(obj, true, 0x39u, FREG_RAX, FREG_RCX);
        }
        x86_jcc_label(obj, FCC_E, cases[i].label_id);
    }
    x86_jmp_label(obj, def_lbl);
}

// ---------------------------------------------------------------------------------------------------------------------
// Declarations the full pipeline would diagnose (#3577): what each kind of declaration checks of its attributes, its
// redeclarations' types and linkage, before the direct path accepts it.

typedef enum FastDeclEntity
{
    FDECL_FUNCTION = 0,   // a function's declaration
    FDECL_DEFINITION,     // a function's definition
    FDECL_OBJECT,         // a file-scope object, or a block's static one
    FDECL_LOCAL,          // a block's automatic object
    FDECL_TYPEDEF,
} FastDeclEntity;

// Whether the attributes a declaration's specifiers (spec) and declarator (decl) carry are ones it may have, as the
// full pipeline sees them; *out_flags receives the symbol's use flags (BUSTER_FSYM_*) they ask for. A function's
// attributes elsewhere, packed (a record definition's, which the record parser reads), weak on what has no external
// linkage, and noreturn or nonnull on a definition (whose body the full pipeline checks against them) are its.
static __attribute__((noinline)) bool fast_cc_decl_attributes_valid(const FastCCState* cc, const FastAttrInfo* spec, const FastAttrInfo* decl,
                                                                    FastType type, bool is_func, FastDeclEntity entity, bool is_static,
                                                                    uint8_t* out_flags)
{
    // Branch-free but where a format or nonnull list is checked: a header declares functions with attributes by the
    // thousand.
    const uint32_t function = (uint32_t)(entity == FDECL_FUNCTION) | (uint32_t)(entity == FDECL_DEFINITION);
    const uint32_t object = (uint32_t)(entity == FDECL_OBJECT);
    const uint32_t linked = (function | object) & (uint32_t)!is_static;
    const uint32_t deprecated = (uint32_t)((spec->deprecated | decl->deprecated) != 0);
    const uint32_t format = (uint32_t)((spec->format | decl->format) != 0);
    const uint32_t definition = (uint32_t)(entity == FDECL_DEFINITION);
    uint32_t valid = (uint32_t)(entity == FDECL_TYPEDEF) | (uint32_t)(function == (uint32_t)is_func);
    valid &= function | (uint32_t)((spec->function_only | decl->function_only) == 0);
    valid &= (uint32_t)((spec->packed | decl->packed) == 0);
    valid &= linked | (uint32_t)((spec->weak | decl->weak | spec->visibility | decl->visibility) == 0);
    valid &= (deprecated ^ 1u) | function | object;
    valid &= (function ^ 1u) | (uint32_t)((spec->mode_size | decl->mode_size) == 0);
    valid &= (definition ^ 1u) | (uint32_t)((spec->noreturn | decl->noreturn | spec->nonnull | decl->nonnull) == 0);
    if ((valid & format) != 0)
    {
        // format(printf|scanf, INDEX, FIRST) on one list, naming a character pointer parameter, and FIRST the
        // variadic arguments' position or 0.
        const FastAttrInfo* f = spec->format != 0 ? spec : decl;
        const FastComplexType* fn = cc->ctypes + (type & BUSTER_FTYPE_CIDX_MASK);
        valid = (spec->format & decl->format) == 0 && !fn->is_oldstyle && f->format_index >= 1u && f->format_index <= fn->count;
        if (valid != 0)
        {
            const FastType ft = ftype_unqual(cc->param_types[fn->start + f->format_index - 1u]);
            valid = ftype_is_ptr(ft) && ftype_is_char_type(ftype_unqual(ftype_deref(cc, ft))) &&
                    (f->format_first == 0 || (fn->is_variadic && f->format_first == fn->count + 1u));
        }
    }
    const uint32_t nonnull_mask = spec->nonnull_mask | decl->nonnull_mask;
    if (valid != 0 && nonnull_mask != 0)
    {
        // nonnull names pointer parameters of the function (every one's, with no list, of which there is one).
        const FastComplexType* fn = cc->ctypes + (type & BUSTER_FTYPE_CIDX_MASK);
        uint32_t pointers = 0;
        for (uint32_t i = 0; i < fn->count && i < 32u; i += 1)
        {
            pointers |= ftype_is_ptr(ftype_unqual(cc->param_types[fn->start + i])) ? UINT32_C(1) << i : 0u;
        }
        valid = function != 0 && !fn->is_oldstyle && (nonnull_mask == UINT32_MAX ? pointers != 0 : (nonnull_mask & ~pointers) == 0);
    }
    *out_flags = (uint8_t)((BUSTER_FSYM_DEPRECATED * deprecated) | (BUSTER_FSYM_FORMAT * format) |
                           (BUSTER_FSYM_PURE * (uint32_t)((spec->pure | decl->pure) != 0)) |
                           (BUSTER_FSYM_NONNULL * (uint32_t)((spec->nonnull | decl->nonnull) != 0)) |
                           (BUSTER_FSYM_UNUSED_RESULT * (uint32_t)((spec->unused_result | decl->unused_result) != 0)));
    return valid != 0;
}

// Whether a specifier list with no declarator is quiet: a struct, union or enum's declaration or definition, with no
// storage class, qualifier, function specifier or attribute ("declaration does not declare anything", "'static'
// ignored on this declaration"). Branch-free: a file declares its tags by the hundred.
static inline bool fast_cc_tag_declaration_quiet(const FastDeclSpecs* ds)
{
    const uint32_t noise = (uint32_t)(ds->is_typedef | ds->is_static | ds->is_extern | ds->is_inline | ds->is_const | ds->is_volatile |
                                      ds->is_noreturn | ds->is_register | ds->attr.present) | ((uint32_t)ds->has_tag ^ 1u);
    return noise == 0;
}

// Whether two declarations of one name have one type, as the full pipeline's redeclaration check asks: equal, or alike
// part by part (a function's result and parameters, an array's element and count, a pointer's target), an incomplete
// array completed by the other; anything else is its "conflicting types" or "redeclaration with a different type".
// Parameters compare without their top-level qualifiers. A function without a prototype matches only one of none.
// Whether a typedef's aligned attributes (0 for none) leave its type's own alignment (#3577): another one (Linux's
// `__s128 __attribute__((aligned(16)))` is its own) is fast_cc_aligned_typedef's.
static inline bool fast_cc_typedef_alignment_natural(const FastCCState* cc, uint32_t before, uint32_t after, FastType t)
{
    const uint32_t natural = ftype_align(cc, t);
    return ((uint32_t)(before != 0 && before != natural) | (uint32_t)(after != 0 && after != natural)) == 0;
}

// #3577: the type of a typedef aligned other than its type (BEFORE and AFTER its aligned attributes, 0 for none). The
// full pipeline keeps the alignment as the typedef's type's and its size as it was (glibc's <pthread.h> aligns
// __pthread_unwind_buf_t to 16): a complete struct or union aligned above its own becomes a copy of the record with that
// alignment, which objects, members and _Alignof then take (BUSTER_FAST_RECORD_OVERALIGNED refuses it by value, and
// BUSTER_FAST_ELEMENT_MISALIGNED an array of it whose size isn't a multiple). Any other is refused where the typedef is
// used: *FLAGS gets BUSTER_FSYM_ALIGNED.
static __attribute__((cold, noinline)) FastType fast_cc_aligned_typedef(FastCCState* cc, uint32_t before, uint32_t after, FastType t, uint8_t* flags)
{
    const uint32_t want = after != 0 ? after : before;
    const uint32_t cidx = t & BUSTER_FTYPE_CIDX_MASK;
    const bool usable = ftype_is_record(t) && cc->ctypes[cidx].complete != 0 && (before == 0 || after == 0 || before == after) &&
                        want > ftype_align(cc, t);
    const uint32_t copy = usable ? fast_cc_alloc_ctype(cc, cc->ctypes[cidx].kind) : 0u;
    FastType result = t;
    if (copy != 0)
    {
        cc->ctypes[copy] = cc->ctypes[cidx];
        cc->ctypes[copy].align = want;
        cc->ctypes[copy].is_variadic = 1u;
        result = ftype_from_index(cc, copy) | (t & (BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL));
    }
    else
    {
        *flags = BUSTER_FSYM_ALIGNED;
    }
    return result;
}

// Whether two types are one (#3577: a typedef's redefinition): redeclarable, and no array of unknown size made
// complete by the other, through arrays and pointers.
static __attribute__((noinline)) bool fast_cc_types_identical(const FastCCState* cc, FastType a, FastType b)
{
    bool same = a == b || fast_cc_types_redeclarable(cc, a, b);
    FastType x = a;
    FastType y = b;
    for (uint32_t depth = 0; same && x != y && depth < 64u; depth += 1)
    {
        const bool arrays = ftype_is_array(x) && ftype_is_array(y);
        const bool pointers = !arrays && ftype_is_ptr(x) && ftype_is_ptr(y);
        if (arrays)
        {
            same = cc->ctypes[x & BUSTER_FTYPE_CIDX_MASK].count == cc->ctypes[y & BUSTER_FTYPE_CIDX_MASK].count;
            x = cc->ctypes[x & BUSTER_FTYPE_CIDX_MASK].inner;
            y = cc->ctypes[y & BUSTER_FTYPE_CIDX_MASK].inner;
        }
        else if (pointers)
        {
            x = ftype_deref(cc, x);
            y = ftype_deref(cc, y);
        }
        else
        {
            break;
        }
    }
    return same;
}

static __attribute__((noinline)) bool fast_cc_types_redeclarable(const FastCCState* cc, FastType a, FastType b)
{
    FastType left[64];
    FastType right[64];
    uint32_t count = 1;
    left[0] = a;
    right[0] = b;
    uint32_t same = 1u;
    // Branch-free but for a pair that differs: function pointers' conversions ask this by the hundred.
    while (((uint32_t)(count != 0) & same) != 0)
    {
        count -= 1u;
        const FastType x = left[count];
        const FastType y = right[count];
        // Both complex, alike but for the index (every table has an entry 0, which a scalar reads).
        const uint32_t complex = UINT32_C(0) - (((uint32_t)x & (uint32_t)y) >> 31);
        const FastComplexType* cx = cc->ctypes + (x & BUSTER_FTYPE_CIDX_MASK & complex);
        const FastComplexType* cy = cc->ctypes + (y & BUSTER_FTYPE_CIDX_MASK & complex);
        const uint32_t kind = cx->kind;
        const uint32_t differ = (uint32_t)(x != y);
        const uint32_t function = (uint32_t)(kind == FCTYPE_FUNC);
        const uint32_t shaped = (complex & 1u) & (uint32_t)(((x ^ y) & ~BUSTER_FTYPE_CIDX_MASK) == 0) & (uint32_t)(kind == cy->kind) &
                                (function | (uint32_t)(kind == FCTYPE_ARRAY) | (uint32_t)(kind == FCTYPE_PTR));
        same = (differ ^ 1u) | shaped;
        if ((differ & shaped) != 0)
        {
            if (function != 0)
            {
                // One without a prototype matches only one with no parameters (and no `...`); prototypes match parameter by
                // parameter, which alike as one block (the common case) need no pair each.
                const uint32_t none = (uint32_t)(cx->is_oldstyle != cy->is_oldstyle);
                const FastComplexType* proto = cx->is_oldstyle != 0 ? cy : cx;
                same = (none & (uint32_t)(proto->count == 0) & (uint32_t)(proto->is_variadic == 0)) |
                       ((none ^ 1u) & (uint32_t)(cx->count == cy->count) & (uint32_t)(cx->is_variadic == cy->is_variadic));
                same &= (uint32_t)(count + 1u + (none ^ 1u) * cx->count <= 64u);
                const uint32_t compared = cx->count * same * (none ^ 1u);
                const uint32_t each = (uint32_t)(memcmp(cc->param_types + cx->start, cc->param_types + cy->start, (size_t)compared * sizeof(FastType)) != 0);
                left[count] = cx->inner;
                right[count] = cy->inner;
                count += same;
                for (uint32_t i = 0; i < compared * each; i += 1)
                {
                    left[count] = BUSTER_FTYPE_UNQUAL(cc->param_types[cx->start + i]);
                    right[count] = BUSTER_FTYPE_UNQUAL(cc->param_types[cy->start + i]);
                    count += 1u;
                }
            }
            else
            {
                // An array's count where both are known; a pointer's target.
                same = ((uint32_t)(kind == FCTYPE_PTR) | (uint32_t)(cx->count == cy->count) | (uint32_t)(cx->complete == 0) | (uint32_t)(cy->complete == 0)) &
                       (uint32_t)(count < 64u);
                left[count] = cx->inner;
                right[count] = cy->inner;
                count += same;
            }
        }
    }
    return same != 0;
}

// Whether a file-scope function's second declaration (or its definition) may follow its first: one type, no mix of a
// declaration without a prototype and one with parameters (-Wdeprecated-non-prototype), and no internal linkage after
// external. `storage` is the new one's. (An inline definition's linkage the unit's end decides, #3595.)
static __attribute__((noinline)) bool fast_cc_function_redeclarable(const FastCCState* cc, const FastSym* old, FastType type, uint8_t storage)
{
    const FastComplexType* of = cc->ctypes + (old->type & BUSTER_FTYPE_CIDX_MASK);
    const FastComplexType* nf = cc->ctypes + (type & BUSTER_FTYPE_CIDX_MASK);
    const uint32_t old_static = (uint32_t)(old->storage == 1u) | (uint32_t)(old->storage == 4u);
    const uint32_t new_static = (uint32_t)(storage == 1u) | (uint32_t)(storage == 4u);
    const uint32_t old_params = (uint32_t)(of->count != 0) | (uint32_t)of->is_variadic;
    const uint32_t new_params = (uint32_t)(nf->count != 0) | (uint32_t)nf->is_variadic;
    const uint32_t mixed = ((uint32_t)of->is_oldstyle & new_params) | ((uint32_t)nf->is_oldstyle & old_params);
    // The common redeclaration, a prototype and its definition with the same parameters, compares as one block.
    const uint32_t alike = (uint32_t)(of->inner == nf->inner) & (uint32_t)(of->count == nf->count) &
                           (uint32_t)(of->is_variadic == nf->is_variadic) & (uint32_t)(of->is_oldstyle == nf->is_oldstyle);
    bool same = alike != 0 && memcmp(cc->param_types + of->start, cc->param_types + nf->start, (size_t)of->count * sizeof(FastType)) == 0;
    if (!same)
    {
        same = fast_cc_types_redeclarable(cc, old->type, type);
    }
    return (mixed | (new_static & (old_static ^ 1u))) == 0 && same;
}

// ---------------------------------------------------------------------------------------------------------------------
// GNU's inline assembly of musl's two shapes (#3592)
//
// `syscall`, as musl's __syscall0 to __syscall6 write it: the number in rax ("a"), the arguments in rdi, rsi and rdx
// ("D", "S", "d") and in the local register variables r10, r8 and r9 ("r"), the result in rax ("=a"). And
// `mov %%fs:0,%0`, __get_tp's thread pointer, into a register ("=r"). Each input is evaluated into a slot of its own
// before any is loaded, so that loading one can't clobber another; then the instruction, then the output's store. A
// statement keeps no value in a register across it, so rcx and r11, which `syscall` clobbers, and memory need nothing.
// Any other template, operand, constraint or clobber leaves the direct path.

#define BUSTER_FASM_NONE 0u
#define BUSTER_FASM_SYSCALL 1u
#define BUSTER_FASM_THREAD_POINTER 2u
#define BUSTER_FASM_OPERANDS 8u

// The register a local register variable's asm label names: one a function may clobber, so that loading it saves
// nothing (rsp, rbp and the callee-saved registers aren't taken). UINT8_MAX for any other name.
static uint8_t fast_cc_asm_register(const char* name, uint32_t length)
{
    static const char names[9][4] = { "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11" };
    static const uint8_t registers[9] = { FREG_RAX, FREG_RCX, FREG_RDX, FREG_RSI, FREG_RDI, FREG_R8, FREG_R9, FREG_R10, FREG_R11 };
    uint8_t result = UINT8_MAX;
    for (uint32_t i = 0; result == UINT8_MAX && i < 9u; i += 1)
    {
        result = strlen(names[i]) == length && memcmp(names[i], name, length) == 0 ? registers[i] : UINT8_MAX;
    }
    return result;
}

// Whether the token at `pos` is a literal spelled as its bytes, and those are `text`.
static bool fast_cc_asm_literal_is(const FastCCState* cc, uint32_t pos, const char* text)
{
    bool result = pos < cc->tcount && cc->tk[pos] == FTOK_STR_LIT && fast_cc_literal_plain(cc, cc->td[pos]);
    if (result)
    {
        const FastLitSlice sl = cc->pp->lit_slices[cc->td[pos]];
        result = sl.length == strlen(text) && memcmp(cc->pp->lit_bytes + sl.offset, text, sl.length) == 0;
    }
    return result;
}

typedef struct FastAsmOperand FastAsmOperand;
struct FastAsmOperand
{
    int32_t off;   // an input's slot, or the output's local, at [rbp + off]
    uint8_t reg;
};

// One operand, "constraint" (expression). The output is a 64-bit integer or pointer local that isn't const ("=a",
// "=r"). An input is an integer or pointer expression of up to 64 bits ("a", "D", "S", "d"), or a local register
// variable ("r"), evaluated into a slot of its own. Its register, or UINT8_MAX where the direct path doesn't take the
// operand.
static uint8_t fast_cc_asm_operand(FastCCState* cc, bool output, FastAsmOperand* operand)
{
    const uint32_t at = cc->pos;
    const bool named = at + 3u < cc->tcount && cc->tk[at + 1u] == FTOK_LPAREN && cc->tk[at + 2u] == FTOK_IDENT && cc->tk[at + 3u] == FTOK_RPAREN;
    const uint32_t name = named ? cc->td[at + 2u] : 0u;
    const uint32_t si = name != 0 && name < cc->env_cap ? cc->sym_cur[name] : 0u;
    const FastSym* sym = cc->syms + si;
    const bool local = si != 0 && sym->kind == FSYM_LOCAL && ftype_size(cc, sym->type) == 8u && (ftype_is_int(sym->type) || ftype_is_ptr(sym->type));
    uint8_t reg = UINT8_MAX;
    if (output && local && !ftype_is_const(sym->type) && (fast_cc_asm_literal_is(cc, at, "=a") || fast_cc_asm_literal_is(cc, at, "=r")))
    {
        reg = FREG_RAX;
        operand->off = sym->stack_off;
        cc->pos += 4u;
    }
    else if (!output)
    {
        if (fast_cc_asm_literal_is(cc, at, "a")) { reg = FREG_RAX; }
        else if (fast_cc_asm_literal_is(cc, at, "D")) { reg = FREG_RDI; }
        else if (fast_cc_asm_literal_is(cc, at, "S")) { reg = FREG_RSI; }
        else if (fast_cc_asm_literal_is(cc, at, "d")) { reg = FREG_RDX; }
        else if (fast_cc_asm_literal_is(cc, at, "r") && local && sym->asm_name != NULL) { reg = fast_cc_asm_register(sym->asm_name, sym->asm_name_len); }
        reg = fast_cc_peek_at(cc, 1u) == FTOK_LPAREN ? reg : (uint8_t)UINT8_MAX;
        if (reg != UINT8_MAX)
        {
            cc->pos += 2u;
            FastVal value = { 0 };
            fast_cc_expr(cc, false, false, false, false, 0, &value);
            const bool fits = !cc->bailout && fast_cc_peek(cc) == FTOK_RPAREN && (ftype_is_int(value.type) || ftype_is_ptr(value.type)) &&
                              ftype_size(cc, value.type) <= 8u;
            if (fits)
            {
                operand->off = fast_cc_alloc_local(cc, 8u, 8u);
                FastExprCtx ex;
                fast_expr_ctx_reset(&ex, cc);
                if (fval_holds_gp(&value)) { ex.gp_used |= (uint16_t)(1u << value.reg); }
                FastVal slot = { .type = ftype_is_ptr(value.type) ? value.type : (FastType)FTYPE_I64, .kind = FVAL_LOCAL, .off = operand->off };
                fval_store(&ex, &slot, &value, false, NULL);
                fast_cc_reset_tmps(cc);
                cc->pos += 1u;
            }
            reg = fits ? reg : (uint8_t)UINT8_MAX;
        }
    }
    operand->reg = reg;
    return reg;
}

// musl's atomics (arch/x86_64/atomic_arch.h, #3592's step 6c), each a template of its own with fixed operands, and the
// empty template of a compiler barrier (a_barrier, explicit_bzero's input, __libc_start_main's "+r"). A memory operand
// ("=m", "m") is `*p` or `*(T *)p` of a pointer local, whose value is loaded into rdx; the operation's size is what it
// names, 4 or 8 bytes (incl and decl name 4). A register operand ("a", "r", "0", "=a", "=r") is rax, but cmpxchg's
// source, which is rcx, and a register output is stored to its local after the instruction. As syscall's, each input is
// evaluated into a slot of its own before any is loaded. A "+r" output is kept in its local, which the empty template
// doesn't change.
#define BUSTER_FASM_CAS 3u        // lock ; cmpxchg %3, %1              "=a"(t), "=m"(*p) : "a"(t), "r"(s)
#define BUSTER_FASM_SWAP 4u       // xchg %0, %1                        "=r"(v), "=m"(*p) : "0"(v)
#define BUSTER_FASM_FETCH_ADD 5u  // lock ; xadd %0, %1                 "=r"(v), "=m"(*p) : "0"(v)
#define BUSTER_FASM_AND 6u        // lock ; and %1, %0                  "=m"(*p) : "r"(v)
#define BUSTER_FASM_OR 7u         // lock ; or %1, %0                   "=m"(*p) : "r"(v)
#define BUSTER_FASM_INC 8u        // lock ; incl %0                     "=m"(*p) : "m"(*p)
#define BUSTER_FASM_DEC 9u        // lock ; decl %0                     "=m"(*p) : "m"(*p)
#define BUSTER_FASM_STORE 10u     // mov %1, %0 ; lock ; orl $0,(%%rsp)  "=m"(*p) : "r"(x)
#define BUSTER_FASM_EMPTY 11u     // (no instruction)                   "+r"(x) or "r"(x), or none
#define BUSTER_FASM_PAUSE 12u     // pause
#define BUSTER_FASM_HLT 13u       // hlt
#define BUSTER_FASM_BSF 14u       // bsf %1,%0                          "=r"(x) : "r"(x)
#define BUSTER_FASM_BSR 15u       // bsr %1,%0 ; xor $63,%0             "=r"(x) : "r"(x)

static uint32_t fast_cc_asm_atomic_shape(const FastCCState* cc, uint32_t pos)
{
    static const char* const templates[] = {
        "lock ; cmpxchg %3, %1",
        "xchg %0, %1",
        "lock ; xadd %0, %1",
        "lock ; and %1, %0",
        "lock ; or %1, %0",
        "lock ; incl %0",
        "lock ; decl %0",
        "mov %1, %0 ; lock ; orl $0,(%%rsp)",
        "",
        "pause",
        "hlt",
        "bsf %1,%0",
        "bsr %1,%0 ; xor $63,%0",
    };
    uint32_t shape = BUSTER_FASM_NONE;
    for (uint32_t i = 0; shape == BUSTER_FASM_NONE && i < (uint32_t)(sizeof(templates) / sizeof(templates[0])); i += 1)
    {
        shape = fast_cc_asm_literal_is(cc, pos, templates[i]) ? BUSTER_FASM_CAS + i : BUSTER_FASM_NONE;
    }
    return shape;
}

// One operand of musl's atomics: its constraint as `kind` ('a' for "=a" or "a", 'r' for "=r" or "r", '+' for "+r", '0',
// 'm' for "=m" or "m"); its slot, which is a memory operand's pointer local's, an output's local's or an input's own; and
// its size, 4 or 8 bytes. False where the direct path doesn't take it.
typedef struct FastAsmAtomicOperand FastAsmAtomicOperand;
struct FastAsmAtomicOperand
{
    int32_t off;
    uint32_t size;
    uint8_t kind;
};

static bool fast_cc_asm_atomic_operand(FastCCState* cc, bool output, FastAsmAtomicOperand* operand)
{
    const uint32_t at = cc->pos;
    uint8_t kind = 0;
    if (output)
    {
        kind = fast_cc_asm_literal_is(cc, at, "=a") ? 'a' : fast_cc_asm_literal_is(cc, at, "=r") ? 'r' : fast_cc_asm_literal_is(cc, at, "+r") ? '+' : 0u;
        kind = kind == 0 && fast_cc_asm_literal_is(cc, at, "=m") ? 'm' : kind;
    }
    else
    {
        kind = fast_cc_asm_literal_is(cc, at, "a") ? 'a' : fast_cc_asm_literal_is(cc, at, "r") ? 'r' : fast_cc_asm_literal_is(cc, at, "0") ? '0' : 0u;
        kind = kind == 0 && fast_cc_asm_literal_is(cc, at, "m") ? 'm' : kind;
    }
    bool ok = kind != 0 && fast_cc_peek_at(cc, 1u) == FTOK_LPAREN;
    cc->pos += ok ? 2u : 0u;
    uint32_t size = 0;
    if (ok && kind == 'm')
    {
        // `*p` or `*(T *)p`, p a pointer local: its slot holds the address.
        ok = fast_cc_peek(cc) == FTOK_STAR;
        cc->pos += ok ? 1u : 0u;
        FastType pointer = 0;
        if (ok && fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_is_type_start(cc, cc->pos + 1u))
        {
            cc->pos += 1u;
            bool parsed = false;
            pointer = fast_cc_parse_typename(cc, &parsed);
            ok = parsed && !cc->bailout && fast_cc_peek(cc) == FTOK_RPAREN && ftype_is_ptr(pointer);
            cc->pos += ok ? 1u : 0u;
        }
        const uint32_t name = ok && fast_cc_peek(cc) == FTOK_IDENT ? cc->td[cc->pos] : 0u;
        const uint32_t si = name != 0 && name < cc->env_cap ? cc->sym_cur[name] : 0u;
        const FastSym* sym = cc->syms + si;
        ok = si != 0 && sym->kind == FSYM_LOCAL && ftype_is_ptr(sym->type) && fast_cc_peek_at(cc, 1u) == FTOK_RPAREN;
        size = ok ? ftype_size(cc, ftype_deref(cc, pointer != 0 ? pointer : sym->type)) : 0u;
        operand->off = ok ? sym->stack_off : 0;
        cc->pos += ok ? 2u : 0u;
    }
    else if (ok && output)
    {
        // A local integer or pointer, stored after the instruction ("=a", "=r") or left in place ("+r").
        const uint32_t name = fast_cc_peek(cc) == FTOK_IDENT ? cc->td[cc->pos] : 0u;
        const uint32_t si = name != 0 && name < cc->env_cap ? cc->sym_cur[name] : 0u;
        const FastSym* sym = cc->syms + si;
        ok = si != 0 && sym->kind == FSYM_LOCAL && (ftype_is_int(sym->type) || ftype_is_ptr(sym->type)) && !ftype_is_const(sym->type) &&
             fast_cc_peek_at(cc, 1u) == FTOK_RPAREN;
        size = ok ? ftype_size(cc, sym->type) : 0u;
        operand->off = ok ? sym->stack_off : 0;
        cc->pos += ok ? 2u : 0u;
    }
    else if (ok)
    {
        // An input's value, into a slot of its own.
        FastVal value = { 0 };
        fast_cc_expr(cc, false, false, false, false, 0, &value);
        ok = !cc->bailout && fast_cc_peek(cc) == FTOK_RPAREN && (ftype_is_int(value.type) || ftype_is_ptr(value.type));
        size = ok ? ftype_size(cc, value.type) : 0u;
        if (ok)
        {
            operand->off = fast_cc_alloc_local(cc, 8u, 8u);
            FastExprCtx ex;
            fast_expr_ctx_reset(&ex, cc);
            if (fval_holds_gp(&value)) { ex.gp_used |= (uint16_t)(1u << value.reg); }
            FastVal slot = { .type = ftype_is_ptr(value.type) ? value.type : (FastType)FTYPE_I64, .kind = FVAL_LOCAL, .off = operand->off };
            fval_store(&ex, &slot, &value, false, NULL);
            fast_cc_reset_tmps(cc);
            cc->pos += 1u;
        }
    }
    ok = ok && (size == 4u || size == 8u);
    operand->size = size;
    operand->kind = ok ? kind : 0u;
    return ok;
}

// The asm statement of musl's atomic `shape`, its template at the token at cc->pos; or leaves the direct path.
static void fast_cc_asm_atomic(FastCCState* cc, uint32_t shape)
{
    FastObj* obj = cc->obj;
    cc->pos += 1u;
    FastAsmAtomicOperand outputs[2] = { { 0 } };
    FastAsmAtomicOperand inputs[2] = { { 0 } };
    uint32_t outs = 0;
    uint32_t ins = 0;
    bool memory = false;
    bool ok = true;
    // Outputs, inputs and clobbers ("memory" alone), any of them left out at the end.
    for (uint32_t section = 0; ok && section < 3u && fast_cc_peek(cc) == FTOK_COLON; section += 1)
    {
        cc->pos += 1u;
        bool more = fast_cc_peek(cc) == FTOK_STR_LIT;
        while (ok && more)
        {
            if (section == 0u)
            {
                ok = outs < 2u && fast_cc_asm_atomic_operand(cc, true, outputs + outs);
                outs += ok ? 1u : 0u;
            }
            else if (section == 1u)
            {
                ok = ins < 2u && fast_cc_asm_atomic_operand(cc, false, inputs + ins);
                ins += ok ? 1u : 0u;
            }
            else
            {
                ok = fast_cc_asm_literal_is(cc, cc->pos, "memory");
                memory |= ok;
                cc->pos += ok ? 1u : 0u;
            }
            more = ok && fast_cc_peek(cc) == FTOK_COMMA;
            cc->pos += more ? 1u : 0u;
        }
    }
    const FastAsmAtomicOperand out = outputs[0];
    const FastAsmAtomicOperand in = inputs[0];
    // The memory operand: an exchange's second output, an update's only one.
    const bool exchange = shape == BUSTER_FASM_CAS || shape == BUSTER_FASM_SWAP || shape == BUSTER_FASM_FETCH_ADD;
    const bool update = shape == BUSTER_FASM_AND || shape == BUSTER_FASM_OR || shape == BUSTER_FASM_STORE || shape == BUSTER_FASM_INC || shape == BUSTER_FASM_DEC;
    const FastAsmAtomicOperand place = exchange ? outputs[1] : out;
    const uint32_t size = place.size;
    switch (shape)
    {
        case BUSTER_FASM_CAS:
            ok = ok && memory && outs == 2u && out.kind == 'a' && place.kind == 'm' && ins == 2u && in.kind == 'a' && inputs[1].kind == 'r' &&
                 out.size == size && in.size == size && inputs[1].size == size;
            break;
        case BUSTER_FASM_SWAP:
        case BUSTER_FASM_FETCH_ADD:
            ok = ok && memory && outs == 2u && out.kind == 'r' && place.kind == 'm' && ins == 1u && in.kind == '0' && out.size == size && in.size == size;
            break;
        case BUSTER_FASM_AND:
        case BUSTER_FASM_OR:
        case BUSTER_FASM_STORE:
            ok = ok && memory && outs == 1u && out.kind == 'm' && ins == 1u && in.kind == 'r' && in.size == size;
            break;
        case BUSTER_FASM_INC:
        case BUSTER_FASM_DEC:
            ok = ok && memory && outs == 1u && out.kind == 'm' && ins == 1u && in.kind == 'm' && in.off == out.off;
            break;
        case BUSTER_FASM_EMPTY:
            ok = ok && (outs == 0u || (outs == 1u && out.kind == '+')) && (ins == 0u || (ins == 1u && in.kind == 'r'));
            break;
        case BUSTER_FASM_PAUSE:
        case BUSTER_FASM_HLT:
            ok = ok && outs == 0u && ins == 0u;
            break;
        default: // bsf, bsr
            ok = ok && !memory && outs == 1u && out.kind == 'r' && ins == 1u && in.kind == 'r' && in.size == out.size;
            break;
    }
    ok = ok && fast_cc_peek(cc) == FTOK_RPAREN && fast_cc_peek_at(cc, 1u) == FTOK_SEMI;
    if (!ok)
    {
        fast_cc_bail(cc, "asm statement");
    }
    else
    {
        // REX.W for a 64-bit operation (bsf's and bsr's size is their output's, which `place` is).
        const uint8_t wide = size == 8u ? 0x48u : 0u;
        if (exchange || update)
        {
            x86_load(obj, FREG_RDX, FREG_RBP, place.off, 8u, false);
        }
        if (ins != 0u && in.kind != 'm' && shape != BUSTER_FASM_EMPTY)
        {
            x86_load(obj, FREG_RAX, FREG_RBP, in.off, 8u, false);
        }
        if (shape == BUSTER_FASM_CAS)
        {
            x86_load(obj, FREG_RCX, FREG_RBP, inputs[1].off, 8u, false);
        }
        // The instruction, its memory operand [rdx], its register eax or rax (ecx or rcx for cmpxchg's source).
        static const uint8_t lock = 0xf0u;
        switch (shape)
        {
            case BUSTER_FASM_CAS:
            case BUSTER_FASM_FETCH_ADD:
                x86_u8(obj, lock);
                if (wide != 0u) { x86_u8(obj, wide); }
                x86_u8(obj, 0x0fu);
                x86_u8(obj, shape == BUSTER_FASM_CAS ? 0xb1u : 0xc1u);
                x86_u8(obj, shape == BUSTER_FASM_CAS ? 0x0au : 0x02u);
                break;
            case BUSTER_FASM_SWAP:
                if (wide != 0u) { x86_u8(obj, wide); }
                x86_u8(obj, 0x87u);
                x86_u8(obj, 0x02u);
                break;
            case BUSTER_FASM_AND:
            case BUSTER_FASM_OR:
                x86_u8(obj, lock);
                if (wide != 0u) { x86_u8(obj, wide); }
                x86_u8(obj, shape == BUSTER_FASM_AND ? 0x21u : 0x09u);
                x86_u8(obj, 0x02u);
                break;
            case BUSTER_FASM_INC:
            case BUSTER_FASM_DEC:
                x86_u8(obj, lock);
                x86_u8(obj, 0xffu);
                x86_u8(obj, shape == BUSTER_FASM_INC ? 0x02u : 0x0au);
                break;
            case BUSTER_FASM_STORE:
                // mov [rdx], eax; lock or dword [rsp], 0
                if (wide != 0u) { x86_u8(obj, wide); }
                x86_u8(obj, 0x89u);
                x86_u8(obj, 0x02u);
                x86_u8(obj, lock);
                x86_u8(obj, 0x83u);
                x86_u8(obj, 0x0cu);
                x86_u8(obj, 0x24u);
                x86_u8(obj, 0x00u);
                break;
            case BUSTER_FASM_PAUSE:
                x86_u8(obj, 0xf3u);
                x86_u8(obj, 0x90u);
                break;
            case BUSTER_FASM_HLT:
                x86_u8(obj, 0xf4u);
                break;
            case BUSTER_FASM_BSF:
            case BUSTER_FASM_BSR:
                // bsf/bsr rax, rax; bsr's xor rax, 63
                if (wide != 0u) { x86_u8(obj, wide); }
                x86_u8(obj, 0x0fu);
                x86_u8(obj, shape == BUSTER_FASM_BSF ? 0xbcu : 0xbdu);
                x86_u8(obj, 0xc0u);
                if (shape == BUSTER_FASM_BSR)
                {
                    if (wide != 0u) { x86_u8(obj, wide); }
                    x86_u8(obj, 0x83u);
                    x86_u8(obj, 0xf0u);
                    x86_u8(obj, 0x3fu);
                }
                break;
            default:
                break;
        }
        // The register output: cmpxchg's rax, xchg's and xadd's register, bsf's and bsr's.
        if (outs != 0u && (out.kind == 'a' || out.kind == 'r'))
        {
            x86_store(obj, FREG_RBP, out.off, FREG_RAX, out.size);
        }
        cc->pos += 2u;
    }
}

static void fast_cc_asm_statement(FastCCState* cc)
{
    FastObj* obj = cc->obj;
    cc->pos += 1u;
    cc->pos += fast_cc_peek(cc) == FTOK_KW_VOLATILE ? 1u : 0u;
    const uint32_t atomic = fast_cc_peek(cc) == FTOK_LPAREN ? fast_cc_asm_atomic_shape(cc, cc->pos + 1u) : BUSTER_FASM_NONE;
    if (atomic != BUSTER_FASM_NONE)
    {
        cc->pos += 1u;
        fast_cc_asm_atomic(cc, atomic);
        return;
    }
    uint32_t shape = BUSTER_FASM_NONE;
    if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_asm_literal_is(cc, cc->pos + 1u, "syscall")) { shape = BUSTER_FASM_SYSCALL; }
    else if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_asm_literal_is(cc, cc->pos + 1u, "mov %%fs:0,%0")) { shape = BUSTER_FASM_THREAD_POINTER; }
    bool ok = shape != BUSTER_FASM_NONE && fast_cc_peek_at(cc, 2u) == FTOK_COLON;
    cc->pos += ok ? 3u : 0u;
    // The one output: syscall's result is rax's.
    FastAsmOperand operands[BUSTER_FASM_OPERANDS];
    ok = ok && (shape != BUSTER_FASM_SYSCALL || fast_cc_asm_literal_is(cc, cc->pos, "=a")) && fast_cc_asm_operand(cc, true, operands) != UINT8_MAX;
    uint32_t count = 1u;
    uint32_t taken = 0;
    if (ok && fast_cc_peek(cc) == FTOK_COLON)
    {
        cc->pos += 1u;
        bool more = fast_cc_peek(cc) == FTOK_STR_LIT;
        while (ok && more)
        {
            ok = count < BUSTER_FASM_OPERANDS && fast_cc_asm_operand(cc, false, operands + count) != UINT8_MAX &&
                 (taken & (1u << operands[count].reg)) == 0;
            taken |= ok ? 1u << operands[count].reg : 0u;
            count += ok ? 1u : 0u;
            more = ok && fast_cc_peek(cc) == FTOK_COMMA;
            cc->pos += more ? 1u : 0u;
        }
    }
    // A clobbered register can't hold an input: clang refuses that.
    uint32_t clobbered = 0;
    if (ok && fast_cc_peek(cc) == FTOK_COLON)
    {
        cc->pos += 1u;
        bool more = fast_cc_peek(cc) == FTOK_STR_LIT;
        while (ok && more)
        {
            const uint32_t at = cc->pos;
            const bool rcx = fast_cc_asm_literal_is(cc, at, "rcx");
            const bool r11 = fast_cc_asm_literal_is(cc, at, "r11");
            ok = rcx || r11 || fast_cc_asm_literal_is(cc, at, "memory") || fast_cc_asm_literal_is(cc, at, "cc");
            clobbered |= (rcx ? 1u << FREG_RCX : 0u) | (r11 ? 1u << FREG_R11 : 0u);
            cc->pos += 1u;
            more = ok && fast_cc_peek(cc) == FTOK_COMMA;
            cc->pos += more ? 1u : 0u;
        }
    }
    // syscall's number is in rax; the thread pointer takes no input.
    ok = ok && fast_cc_peek(cc) == FTOK_RPAREN && fast_cc_peek_at(cc, 1u) == FTOK_SEMI && (taken & clobbered) == 0 &&
         (shape == BUSTER_FASM_SYSCALL ? (taken & (1u << FREG_RAX)) != 0 : count == 1u);
    if (!ok)
    {
        fast_cc_bail(cc, "asm statement");
    }
    else
    {
        for (uint32_t i = 1u; i < count; i += 1)
        {
            x86_load(obj, operands[i].reg, FREG_RBP, operands[i].off, 8u, false);
        }
        if (shape == BUSTER_FASM_SYSCALL)
        {
            x86_u8(obj, 0x0fu);
            x86_u8(obj, 0x05u);
        }
        else
        {
            // mov rax, qword fs:[0]
            x86_u8(obj, 0x64u);
            x86_u8(obj, 0x48u);
            x86_u8(obj, 0x8bu);
            x86_u8(obj, 0x04u);
            x86_u8(obj, 0x25u);
            x86_u32(obj, 0u);
        }
        x86_store(obj, FREG_RBP, operands[0].off, FREG_RAX, 8u);
        cc->pos += 2u;
    }
}

// Compile one complete function body starting at `cc->pos` == '{'!
static BUSTER_DIRECT_HOT void fast_cc_compile_func(FastCCState* cc, uint32_t sym_idx, uint32_t param_names_start, uint16_t param_count)
{
    FastObj* obj = cc->obj;
    FastSym* fn_sym = cc->syms + sym_idx;
    const FastType fn_t = fn_sym->type;
    if (!ftype_is_func(fn_t)) { fast_cc_bail(cc, "compile non-func"); return; }
    const FastComplexType* fn = cc->ctypes + (fn_t & BUSTER_FTYPE_CIDX_MASK);
    const FastType ret_t = ftype_unqual(fn->inner);
    if (ftype_is_unsupported(ret_t)) { fast_cc_bail(cc, "unsupported return type"); return; }

    // 1. AVX-512 body prescan: verify matching '}' and reject unsupported extensions BEFORE emitting any bytes!
    const FastBodyScan scan = fast_cc_prescan_body(cc, cc->pos);
    if (scan.end_pos == UINT32_MAX || scan.has_unsupported)
    {
        fast_cc_bail(cc, scan.has_unsupported ? "unsupported token in func body" : "unclosed func body");
        return;
    }

    // 2. Align .text to 16 bytes and emit prologue (`push rbp; mov rbp, rsp; sub rsp, imm32`)
    while ((obj->text.len & 15u) != 0) { x86_u8(obj, 0x90u); }
    const uint32_t func_start = obj->text.len;
    x86_push(obj, FREG_RBP);                 // 55
    x86_mov_rr(obj, true, FREG_RBP, FREG_RSP); // 48 89 E5
    x86_u8(obj, 0x48u); x86_u8(obj, 0x81u); x86_u8(obj, 0xecu); // sub rsp, imm32
    const uint32_t sub_rsp_patch = obj->text.len;
    x86_u32(obj, 0u);

    // 3. Reset per-function state
    cc->cur_func_sym = sym_idx;
    cc->cur_func_name_id = fn_sym->ident_id;
    cc->cur_ret_type = ret_t;
    cc->cur_ret_label = fast_label_new(obj);
    cc->cur_sret_off = 0;
    cc->cur_frame_size = 0;
    cc->cur_tmp_off = 0;
    cc->dead = 0;
    cc->cur_max_frame = 0;
    cc->cur_max_call_stack = 0;
    cc->cur_va_save_off = 0;
    cc->cur_va_gp_init = 0;
    cc->cur_va_fp_init = 48u;
    cc->cur_va_overflow_off = 16;
    cc->cur_last_param = param_count != 0 ? cc->param_names[param_names_start + param_count - 1u] & BUSTER_FPARAM_NAME_MASK : 0u;
    cc->cur_variadic = cc->ctypes[fn_sym->type & BUSTER_FTYPE_CIDX_MASK].is_variadic;

    const uint32_t file_sym_count = cc->sym_count;
    const uint32_t func_scope_mark = fast_cc_scope_enter(cc);

    // 4. SysV AMD64 parameter homing
    static const uint8_t abi_gp[6] = { FREG_RDI, FREG_RSI, FREG_RDX, FREG_RCX, FREG_R8, FREG_R9 };
    uint32_t gp_idx = 0;
    uint32_t fp_idx = 0;
    int32_t stk_off = 16; // first stack parameter at [rbp + 16]

    // Hidden sret pointer for >16B struct return (#3577: an aligned typedef's record is refused, BUSTER_FAST_RECORD_OVERALIGNED).
    if (ftype_is_record(ret_t) &&
        ((uint32_t)(cc->ctypes[ret_t & BUSTER_FTYPE_CIDX_MASK].abi_lo == FABI_MEM) | (uint32_t)BUSTER_FAST_RECORD_OVERALIGNED(cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK))) != 0)
    {
        if (BUSTER_FAST_RECORD_OVERALIGNED(cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK))) { fast_cc_bail(cc, "an aligned typedef's record by value"); }
        cc->cur_sret_off = fast_cc_alloc_local(cc, 8u, 8u);
        x86_store(obj, FREG_RBP, cc->cur_sret_off, FREG_RDI, 8u);
        gp_idx = 1u;
    }

    // Variadic 176-byte register save area!
    if (fn->is_variadic)
    {
        cc->cur_va_save_off = fast_cc_alloc_local(cc, 176u, 16u);
        for (uint32_t r = 0; r < 6u; r += 1)
        {
            x86_store(obj, FREG_RBP, cc->cur_va_save_off + (int32_t)(r * 8u), abi_gp[r], 8u);
        }
        const uint32_t lbl_skip_fp = fast_label_new(obj);
        x86_test_r8(obj, FREG_RAX);
        x86_jcc_label(obj, FCC_E, lbl_skip_fp);
        for (uint32_t x = 0; x < 8u; x += 1)
        {
            // movaps [rbp + off], xmm (using movsd/movupd — movsd saves low 8B; full movaps needs 16B align, movupd is unaligned-safe!)
            // Emit movupd [rbp + off], xmm: 66 [REX] 0F 11 ModRM
            x86_u8(obj, 0x66u);
            x86_rex(obj, false, x, 0, FREG_RBP, false);
            x86_u8(obj, 0x0fu);
            x86_u8(obj, 0x11u);
            x86_mem_disp(obj, x, FREG_RBP, cc->cur_va_save_off + 48 + (int32_t)(x * 16u));
        }
        fast_label_bind(obj, lbl_skip_fp);
    }

    // Home each parameter into `[rbp + off]` and bind in function scope. The parameters share the body's outermost
    // block's scope (#3577): bound one level deeper, a declaration there of a parameter's name is its redefinition, which
    // the binding's same-scope check then finds.
    const uint32_t pcnt = fn->count;
    cc->scope_depth += 1u;
    cc->cur_array_params = 0;
    for (uint32_t i = 0; i < pcnt && !cc->bailout; i += 1)
    {
        const FastType pt = cc->param_types[fn->start + i];
        if (ftype_is_unsupported(pt)) { fast_cc_bail(cc, "unsupported param type"); break; }
        const uint32_t recorded = i < param_count ? cc->param_names[param_names_start + i] : 0u;
        const uint32_t pname = recorded & BUSTER_FPARAM_NAME_MASK;
        const uint8_t array_param = (uint8_t)(recorded >> 31);
        cc->cur_array_params |= array_param;
        // #3577: a definition's unnamed parameter is a C23 extension the full pipeline warns of.
        if (pname == 0) { fast_cc_bail(cc, "unnamed parameter in a definition"); break; }
        int32_t slot = 0;

        if (ftype_is_record(pt))
        {
            const FastComplexType* rct = cc->ctypes + (pt & BUSTER_FTYPE_CIDX_MASK);
            if (BUSTER_FAST_RECORD_OVERALIGNED(rct)) { fast_cc_bail(cc, "an aligned typedef's record by value"); }
            bool in_regs = false;
            if (rct->abi_lo != FABI_MEM)
            {
                const uint32_t ngp = (rct->abi_lo == FABI_INT ? 1u : 0u) + (rct->abi_hi == FABI_INT ? 1u : 0u);
                const uint32_t nfp = (rct->abi_lo == FABI_SSE ? 1u : 0u) + (rct->abi_hi == FABI_SSE ? 1u : 0u);
                if (gp_idx + ngp <= 6u && fp_idx + nfp <= 8u)
                {
                    in_regs = true;
                    slot = fast_cc_alloc_local(cc, rct->size < 16u ? 16u : rct->size, rct->align < 8u ? 8u : rct->align);
                    if (rct->abi_lo == FABI_INT) { x86_store(obj, FREG_RBP, slot, abi_gp[gp_idx++], 8u); }
                    else if (rct->abi_lo == FABI_SSE) { x86_sse_store(obj, true, FREG_RBP, slot, fp_idx++); }
                    if (rct->abi_hi == FABI_INT) { x86_store(obj, FREG_RBP, slot + 8, abi_gp[gp_idx++], 8u); }
                    else if (rct->abi_hi == FABI_SSE) { x86_sse_store(obj, true, FREG_RBP, slot + 8, fp_idx++); }
                }
            }
            if (!in_regs)
            {
                slot = stk_off;
                stk_off += (int32_t)((rct->size + 7u) & ~7u);
            }
        }
        else if (ftype_is_float(pt))
        {
            if (fp_idx < 8u)
            {
                slot = fast_cc_alloc_local(cc, ftype_size(cc, pt), ftype_align(cc, pt));
                x86_sse_store(obj, ftype_is_f64(pt), FREG_RBP, slot, fp_idx++);
            }
            else
            {
                slot = stk_off;
                stk_off += 8;
            }
        }
        else
        {
            if (gp_idx < 6u)
            {
                const uint32_t sz = ftype_size(cc, pt);
                slot = fast_cc_alloc_local(cc, sz < 4u ? 4u : sz, sz < 4u ? 4u : sz);
                x86_store(obj, FREG_RBP, slot, abi_gp[gp_idx++], sz);
            }
            else
            {
                slot = stk_off;
                stk_off += 8;
            }
        }

        if (pname != 0)
        {
            fast_cc_sym_bind(cc, pname, (FastSym){ .kind = FSYM_LOCAL, .type = pt, .stack_off = slot, .pad_sym = { array_param, 0u },
                                                   .use_flags = (uint8_t)(BUSTER_FSYM_REGISTER * (uint32_t)((recorded & BUSTER_FPARAM_REGISTER) != 0)) });
        }
    }
    cc->cur_va_gp_init = gp_idx * 8u;
    cc->cur_va_fp_init = 48u + fp_idx * 16u;
    cc->cur_va_overflow_off = stk_off;
    cc->scope_depth -= 1u;

    // 5. Iterative statement execution state machine!
    FastStmtFrame sstack[64];
    uint32_t ssp = 0;

    // Break / continue target stacks
    uint32_t break_stack[64];
    uint32_t break_sp = 0;
    uint32_t cont_stack[64];
    uint32_t cont_sp = 0;

    // Active switch frames & case pool (in scratch arena so sqlite3VdbeExec's ~200 cases never overflow stack!)
    const uint64_t scratch_mark = cc->scratch->used;
    FastSwitchCtx sw_stack[16];
    uint32_t sw_sp = 0;
    FastSwitchCase* sw_cases = (FastSwitchCase*)fast_arena_alloc(cc->scratch, 4096u * sizeof(FastSwitchCase), 8);
    uint32_t sw_case_used = 0;
    uint16_t case_heads[1u << BUSTER_FSWITCH_BUCKET_BITS];
    if (scan.has_switch)
    {
        memset(case_heads, 0, sizeof(case_heads));
    }
    FastUserLabel* ulabels = (FastUserLabel*)fast_arena_alloc(cc->scratch, 512u * sizeof(FastUserLabel), 8);
    uint32_t ulabel_count = 0;
    // #3577: whether the point compiled may be reached, as the full pipeline's -Wreturn-type sees it at the body's end:
    // over-approximated (an if's condition may be false, a loop's but a constant true one's ends it, a label is a
    // goto's target), so that an end this path calls unreachable is one the full pipeline does. A break or a continue
    // marks the frame of its target (break_frame, cont_frame).
    bool live = true;
    uint8_t break_frame[64];
    uint8_t cont_frame[64];

    if (sw_cases == NULL || ulabels == NULL)
    {
        fast_cc_bail(cc, "func scratch full");
        fast_cc_scope_leave(cc, func_scope_mark);
        cc->sym_count = file_sym_count;
        cc->scratch->used = scratch_mark;
        return;
    }

    // Push function body `{ ... }` block!
    cc->pos += 1u; // consume opening '{'
    sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_BLOCK, .scope_mark = fast_cc_scope_enter(cc) };

    while (ssp > 0 && cc->pos < cc->tcount && !cc->bailout)
    {
        FastStmtFrame* top = sstack + (ssp - 1u);

        // If a control-flow frame ALREADY finished executing its child statement, run its post-body epilogue!
        if (top->kind != FSTK_BLOCK && top->entered_body != 0)
        {
            if (top->kind == FSTK_IF_THEN)
            {
                cc->dead -= (uint32_t)(top->dead == BUSTER_FDEAD_THEN);
                if (fast_cc_peek(cc) == FTOK_KW_ELSE)
                {
                    cc->pos += 1u;
                    const uint32_t end_lbl = fast_label_new(obj);
                    // No jump past a dead branch, or out of one: neither has code.
                    bool dead_else = false;
                    if (top->dead == BUSTER_FDEAD_TRUE)
                    {
                        dead_else = fast_cc_dead_simple(cc, cc->pos);
                    }
                    if ((cc->dead == 0) & !dead_else & (top->dead != BUSTER_FDEAD_THEN)) { x86_jmp_label(obj, end_lbl); }
                    cc->dead += (uint32_t)dead_else;
                    fast_label_bind(obj, top->lbl0); // else_lbl
                    top->kind = FSTK_IF_ELSE;
                    top->dead = (uint8_t)((uint32_t)dead_else * BUSTER_FDEAD_ELSE);
                    top->lbl0 = end_lbl;
                    top->entered_body = 0;
                    top->then_live = (uint8_t)live;
                    live = true;
                    continue;
                }
                if (top->trailing_else != 0)
                {
                    fast_cc_bail(cc, "an else the full pipeline's -Wdangling-else questions");
                    break;
                }
                fast_label_bind(obj, top->lbl0);
                ssp -= 1u;
                live = true;
                continue;
            }
            if (top->kind == FSTK_IF_ELSE)
            {
                cc->dead -= (uint32_t)(top->dead == BUSTER_FDEAD_ELSE);
                fast_label_bind(obj, top->lbl0);
                live = live || top->then_live != 0;
                if (top->labeled == 0)
                {
                    BUSTER_FSTK_ENDS_IN_ELSE(sstack, ssp)
                }
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_WHILE)
            {
                x86_jmp_label(obj, top->lbl0); // cond_lbl
                fast_label_bind(obj, top->lbl1); // break_lbl
                live = ((uint32_t)(top->infinite == 0) | (uint32_t)(top->broke != 0)) != 0;
                break_sp -= 1u;
                cont_sp -= 1u;
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_DO_WHILE)
            {
                fast_label_bind(obj, top->lbl1); // cont_lbl
                if (fast_cc_peek(cc) != FTOK_KW_WHILE || fast_cc_peek_at(cc, 1u) != FTOK_LPAREN)
                {
                    fast_cc_bail(cc, "expected while(...) after do");
                    break;
                }
                cc->pos += 2u;
                const uint32_t infinite = (uint32_t)(fast_cc_peek(cc) == FTOK_INT_LIT) & (uint32_t)(fast_cc_data(cc) != 0) &
                                          (uint32_t)(fast_cc_peek_at(cc, 1u) == FTOK_RPAREN);
                fast_cc_expr(cc, false, false, true, true, top->lbl0, NULL); // jump back to body_lbl if true!
                fast_cc_reset_tmps(cc);
                if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) in do-while"); break; }
                cc->pos += 1u;
                // #3577: the statement ends with `;`, or the full pipeline's error.
                if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
                cc->pos += 1u;
                fast_label_bind(obj, top->lbl2); // break_lbl
                // Left by a break, or by the condition, reached from the body's end or a continue, unless a constant true.
                live = ((uint32_t)(top->broke != 0) | (((uint32_t)live | (uint32_t)(top->continued != 0)) & (infinite ^ 1u))) != 0;
                break_sp -= 1u;
                cont_sp -= 1u;
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_FOR)
            {
                fast_label_bind(obj, top->lbl1); // cont_lbl
                if (top->step_pos != UINT32_MAX)
                {
                    const uint32_t save_after = cc->pos;
                    cc->pos = top->step_pos;
                    fast_cc_expr(cc, false, true, false, false, 0, NULL);
                    fast_cc_reset_tmps(cc);
                    cc->pos = save_after;
                }
                x86_jmp_label(obj, top->lbl0); // cond_lbl
                fast_label_bind(obj, top->lbl2); // break_lbl
                fast_cc_scope_leave(cc, top->scope_mark);
                live = ((uint32_t)(top->infinite == 0) | (uint32_t)(top->broke != 0)) != 0;
                break_sp -= 1u;
                cont_sp -= 1u;
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_SWITCH)
            {
                x86_jmp_label(obj, top->lbl1); // jump past dispatch to break_lbl!
                fast_label_bind(obj, top->lbl0); // dispatch_lbl
                const FastSwitchCtx* sw = sw_stack + top->sw_idx;
                if (sw->enum_cidx != 0 && !cc->quiet && !fast_cc_switch_enum_quiet(cc, sw, sw_cases + sw->case_start))
                {
                    fast_cc_bail(cc, "switch on an enum the full pipeline's -Wswitch questions");
                    break;
                }
                fast_cc_emit_switch_dispatch(cc, sw, sw_cases + sw->case_start);
                fast_label_bind(obj, top->lbl1); // break_lbl
                live = ((uint32_t)live | (uint32_t)(sw->default_lbl == UINT32_MAX) | (uint32_t)(top->broke != 0)) != 0;
                // Pop this switch's hash chains. An enclosing switch can already have a case in the same bucket.
                while (sw_case_used > sw->case_start)
                {
                    const FastSwitchCase* entry = &sw_cases[--sw_case_used];
                    case_heads[entry->bucket] = entry->previous;
                }
                sw_sp -= 1u;
                break_sp -= 1u;
                ssp -= 1u;
                continue;
            }
        }

        // Check closing '}' of active FSTK_BLOCK!
        if (top->kind == FSTK_BLOCK && fast_cc_peek(cc) == FTOK_RBRACE)
        {
            cc->pos += 1u;
            fast_cc_scope_leave(cc, top->scope_mark);
            ssp -= 1u;
            continue;
        }

        // Consume any leading `case expr :`, `default :`, or `label :` prefixes!
        bool hit_block_end = false;
        const uint32_t prefix_start = cc->pos;
        while (!cc->bailout)
        {
            const uint8_t pk = fast_cc_peek(cc);
            if (pk == FTOK_KW_CASE)
            {
                if (sw_sp == 0) { fast_cc_bail(cc, "case outside switch"); break; }
                cc->pos += 1u;
                const int64_t cv = fast_cc_const_int_expr(cc, false);
                if (fast_cc_peek(cc) != FTOK_COLON) { fast_cc_bail(cc, "expected : after case"); break; }
                cc->pos += 1u;
                const uint32_t clbl = fast_label_new(obj);
                fast_label_bind(obj, clbl);
                FastSwitchCtx* cur_sw = sw_stack + (sw_sp - 1u);
                const uint64_t hash = (uint64_t)cv * UINT64_C(0x9e3779b97f4a7c15);
                const uint16_t bucket = (uint16_t)(hash >> (64u - BUSTER_FSWITCH_BUCKET_BITS));
                const uint16_t head = case_heads[bucket];
                for (uint32_t link = head; link > cur_sw->case_start; link = sw_cases[link - 1u].previous)
                {
                    if (sw_cases[link - 1u].val == cv)
                    {
                        fast_cc_bail(cc, "duplicate case value");
                        break;
                    }
                }
                if (cc->bailout) { break; }
                if (sw_case_used < 4096u)
                {
                    sw_cases[sw_case_used++] = (FastSwitchCase){
                        .val = cv,
                        .label_id = clbl,
                        .previous = head,
                        .bucket = bucket,
                    };
                    case_heads[bucket] = (uint16_t)sw_case_used;
                    cur_sw->case_count += 1u;
                }
                else { fast_cc_bail(cc, "too many switch cases"); break; }
                live = true;
                continue;
            }
            if (pk == FTOK_KW_DEFAULT)
            {
                if (sw_sp == 0) { fast_cc_bail(cc, "default outside switch"); break; }
                cc->pos += 1u;
                if (fast_cc_peek(cc) != FTOK_COLON) { fast_cc_bail(cc, "expected : after default"); break; }
                cc->pos += 1u;
                if (sw_stack[sw_sp - 1u].default_lbl != UINT32_MAX) { fast_cc_bail(cc, "duplicate default label"); break; }
                const uint32_t dlbl = fast_label_new(obj);
                fast_label_bind(obj, dlbl);
                sw_stack[sw_sp - 1u].default_lbl = dlbl;
                live = true;
                continue;
            }
            if (pk == FTOK_IDENT && fast_cc_peek_at(cc, 1u) == FTOK_COLON)
            {
                const uint32_t lid_name = fast_cc_data(cc);
                cc->pos += 2u;
                bool lok = true;
                const uint32_t ll = fast_cc_get_user_label(obj, ulabels, &ulabel_count, 512u, lid_name, true, &lok);
                if (!lok) { fast_cc_bail(cc, "duplicate or overflow label"); break; }
                fast_label_bind(obj, ll);
                live = true;
                continue;
            }
            if (pk == FTOK_KW_ATTRIBUTE)
            {
                // GCC's `__attribute__((fallthrough));`, which must directly precede a case or default label
                // (fast_cc_fallthrough_placed); any other statement attribute, or one elsewhere, is the full pipeline's
                // (#3577).
                bool fallthrough = fast_cc_peek_at(cc, 1u) == FTOK_LPAREN && fast_cc_peek_at(cc, 2u) == FTOK_LPAREN &&
                                   fast_cc_peek_at(cc, 3u) == FTOK_IDENT && fast_cc_peek_at(cc, 4u) == FTOK_RPAREN &&
                                   fast_cc_peek_at(cc, 5u) == FTOK_RPAREN && fast_cc_peek_at(cc, 6u) == FTOK_SEMI;
                if (fallthrough)
                {
                    uint32_t flen = 0;
                    const char* fs = fast_cc_ident_str(cc, cc->td[cc->pos + 3u], &flen);
                    fallthrough = ((flen == 11u && memcmp(fs, "fallthrough", 11) == 0) || (flen == 15u && memcmp(fs, "__fallthrough__", 15) == 0)) &&
                                  fast_cc_fallthrough_placed(cc, sstack, ssp, cc->pos + 7u);
                }
                if (!fallthrough)
                {
                    fast_cc_bail(cc, "statement attribute");
                    break;
                }
                cc->pos += 7u;
                continue;
            }
            break;
        }
        if (cc->bailout) { break; }
        if (top->kind == FSTK_BLOCK && fast_cc_peek(cc) == FTOK_RBRACE)
        {
            hit_block_end = true;
        }
        if (hit_block_end) { continue; }

        // Mark non-block parent frame as now executing its single child statement!
        if (top->kind != FSTK_BLOCK) { top->entered_body = 1u; }

        const uint8_t k = fast_cc_peek(cc);

        // 1. Nested compound block `{ ... }`
        if (k == FTOK_LBRACE)
        {
            if (ssp >= 64u) { fast_cc_bail(cc, "stmt nesting depth"); break; }
            cc->pos += 1u;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_BLOCK, .scope_mark = fast_cc_scope_enter(cc) };
            continue;
        }

        // 2. Empty statement `;`
        if (k == FTOK_SEMI)
        {
            cc->pos += 1u;
            continue;
        }

        // 3. `_Static_assert(...)` inside function
        if (k == FTOK_KW_STATIC_ASSERT)
        {
            fast_cc_static_assert(cc);
            if (cc->bailout) { break; }
            continue;
        }

        // 4. `if (cond) stmt [else stmt]`
        if (k == FTOK_KW_IF)
        {
            if (ssp >= 64u) { fast_cc_bail(cc, "stmt nesting depth"); break; }
            const uint8_t if_labeled = (uint8_t)(cc->pos != prefix_start);
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after if"); break; }
            cc->pos += 1u;
            const uint32_t else_lbl = fast_label_new(obj);
            fast_cc_expr(cc, false, false, true, false, else_lbl, NULL);
            fast_cc_reset_tmps(cc);
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) after if cond"); break; }
            cc->pos += 1u;
            // A constant condition's dead branch is checked without code where it's simple enough (#3592): musl's
            // FORCE_EVAL of a double has a long double branch, a call of fp_force_evall.
            const uint32_t constant = cc->branch_constant * (uint32_t)(cc->dead == 0);
            bool dead_then = false;
            if (constant == BUSTER_FCONSTANT_FALSE)
            {
                dead_then = fast_cc_dead_simple(cc, cc->pos);
            }
            cc->dead += (uint32_t)dead_then;
            const uint8_t dead = (uint8_t)((uint32_t)dead_then * BUSTER_FDEAD_THEN + (uint32_t)(constant == BUSTER_FCONSTANT_TRUE) * BUSTER_FDEAD_TRUE);
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_IF_THEN, .dead = dead, .lbl0 = else_lbl, .labeled = if_labeled };
            continue;
        }

        // 5. `while (cond) stmt`
        if (k == FTOK_KW_WHILE)
        {
            if (ssp >= 64u || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after while"); break; }
            cc->pos += 1u;
            const bool infinite = fast_cc_peek(cc) == FTOK_INT_LIT && fast_cc_data(cc) != 0 && fast_cc_peek_at(cc, 1u) == FTOK_RPAREN;
            const uint32_t cond_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, cond_lbl);
            fast_cc_expr(cc, false, false, true, false, brk_lbl, NULL);
            fast_cc_reset_tmps(cc);
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) after while cond"); break; }
            cc->pos += 1u;
            break_frame[break_sp] = (uint8_t)ssp;
            cont_frame[cont_sp] = (uint8_t)ssp;
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cond_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_WHILE, .lbl0 = cond_lbl, .lbl1 = brk_lbl, .infinite = (uint8_t)infinite };
            continue;
        }

        // 6. `do stmt while (cond);`
        if (k == FTOK_KW_DO)
        {
            if (ssp >= 64u || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
            cc->pos += 1u;
            const uint32_t body_lbl = fast_label_new(obj);
            const uint32_t cont_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, body_lbl);
            break_frame[break_sp] = (uint8_t)ssp;
            cont_frame[cont_sp] = (uint8_t)ssp;
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cont_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_DO_WHILE, .lbl0 = body_lbl, .lbl1 = cont_lbl, .lbl2 = brk_lbl };
            continue;
        }

        // 7. `for (init; cond; step) stmt`
        if (k == FTOK_KW_FOR)
        {
            if (ssp >= 64u || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after for"); break; }
            cc->pos += 1u;
            const uint32_t fscope = fast_cc_scope_enter(cc);
            // Init clause: declaration OR expression OR empty
            if (fast_cc_peek(cc) == FTOK_SEMI)
            {
                cc->pos += 1u;
            }
            else if (fast_cc_is_type_start(cc, cc->pos))
            {
                FastDeclSpecs ds = { 0 };
                const FastType btype = fast_cc_parse_decl_specs(cc, &ds);
                // #3577: a for clause's declaration declares automatic objects alone (#3599: clang takes static, extern and
                // typedef there too, and inline and _Noreturn belong to functions); and its attributes are a local's.
                if (((uint32_t)((ds.written & ~(BUSTER_FSPEC_REGISTER | BUSTER_FSPEC_AUTO)) != 0) | (uint32_t)ds.attr.present) != 0)
                {
                    fast_cc_bail(cc, "a for's declaration of a non-local");
                    break;
                }
                // #3577: and the rules of a block's: a declaration declares something (fast_cc_tag_declaration_quiet);
                // an object's type is supported and complete; a const local has a fold class, and is initialized.
                if (fast_cc_peek(cc) == FTOK_SEMI && !fast_cc_tag_declaration_quiet(&ds)) { fast_cc_bail(cc, "declaration declares nothing"); break; }
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
                {
                    FastDeclaratorResult dr = { 0 };
                    if (!fast_cc_parse_declarator(cc, btype, false, &dr, false)) { break; }
                    if (dr.attr.asm_label != NULL) { fast_cc_bail(cc, "asm label in a for's declaration"); break; }
                    const uint32_t initialized = (uint32_t)(fast_cc_peek(cc) == FTOK_ASSIGN);
                    const uint32_t incomplete = BUSTER_FAST_OBJECT_INCOMPLETE(cc, dr.type, initialized);
                    if (((uint32_t)(dr.attr.present != 0) | (uint32_t)ftype_is_func(dr.type) | (uint32_t)ds.is_unsupported_type |
                         (uint32_t)ftype_is_unsupported(dr.type) | incomplete) != 0)
                    {
                        fast_cc_bail(cc, incomplete != 0 ? "object of incomplete type" : "for clause's declarator");
                        break;
                    }
                    const uint32_t lsi = dr.name_id != 0 ? fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_LOCAL, .type = dr.type, .use_flags = (uint8_t)(ds.is_register * BUSTER_FSYM_REGISTER) }) : 0u;
                    int32_t loff = 0;
                    if (initialized != 0)
                    {
                        cc->pos += 1u;
                        const uint32_t init_start = cc->pos;
                        cc->init_kind = FVAL_VOID;
                        loff = fast_cc_init_local(cc, &dr.type, ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned);
                        BUSTER_FAST_LOCAL_FOLD(cc, lsi, dr.type, init_start)
                    }
                    else
                    {
                        uint32_t uninitialized_const = 0;
                        BUSTER_FAST_UNINITIALIZED_CONST(cc, dr.type, uninitialized_const)
                        if (uninitialized_const != 0)
                        {
                            fast_cc_bail(cc, "uninitialized const object or const member");
                            break;
                        }
                        loff = fast_cc_alloc_local(cc, ftype_size(cc, dr.type), ftype_align(cc, dr.type));
                    }
                    if (lsi != 0)
                    {
                        cc->syms[lsi].type = dr.type;
                        cc->syms[lsi].stack_off = loff;
                    }
                    if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                }
                // #3577: the statement ends with `;`, or the full pipeline's error.
                if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
                cc->pos += 1u;
            }
            else
            {
                fast_cc_expr(cc, false, true, false, false, 0, NULL);
                fast_cc_reset_tmps(cc);
                // #3577: the statement ends with `;`, or the full pipeline's error.
                if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
                cc->pos += 1u;
            }
            const uint32_t cond_lbl = fast_label_new(obj);
            const uint32_t cont_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, cond_lbl);
            const bool infinite = fast_cc_peek(cc) == FTOK_SEMI ||
                                  (fast_cc_peek(cc) == FTOK_INT_LIT && fast_cc_data(cc) != 0 && fast_cc_peek_at(cc, 1u) == FTOK_SEMI);
            // Cond clause
            if (fast_cc_peek(cc) != FTOK_SEMI)
            {
                fast_cc_expr(cc, false, false, true, false, brk_lbl, NULL);
                fast_cc_reset_tmps(cc);
            }
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; in for"); break; }
            cc->pos += 1u;
            // Record step_pos and skip balanced parens to closing ')'!
            uint32_t step_pos = UINT32_MAX;
            if (fast_cc_peek(cc) != FTOK_RPAREN)
            {
                step_pos = cc->pos;
                uint32_t pd = 1u;
                while (cc->pos < cc->tcount && pd > 0)
                {
                    const uint8_t sk = fast_cc_peek(cc);
                    if (sk == FTOK_LPAREN) { pd += 1u; }
                    else if (sk == FTOK_RPAREN) { pd -= 1u; if (pd == 0) { break; } }
                    cc->pos += 1u;
                }
            }
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) in for"); break; }
            cc->pos += 1u;
            break_frame[break_sp] = (uint8_t)ssp;
            cont_frame[cont_sp] = (uint8_t)ssp;
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cont_lbl;
            sstack[ssp++] = (FastStmtFrame){
                .kind = FSTK_FOR, .lbl0 = cond_lbl, .lbl1 = cont_lbl, .lbl2 = brk_lbl,
                .scope_mark = fscope, .step_pos = step_pos, .infinite = (uint8_t)infinite,
            };
            continue;
        }

        // 8. `switch (expr) stmt`
        if (k == FTOK_KW_SWITCH)
        {
            if (ssp >= 64u || sw_sp >= 16u || break_sp >= 64u) { fast_cc_bail(cc, "switch nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after switch"); break; }
            cc->pos += 1u;
            FastVal sv = { 0 };
            fast_cc_expr(cc, false, false, false, false, 0, &sv);
            if (cc->bailout) { break; }
            // #3577: the controlling expression's enum, its type before promotion (the store below converts it).
            const uint32_t sv_complex = BUSTER_FTYPE_COMPLEX_ALL(sv.type) & (UINT32_C(0) - (uint32_t)((sv.type & BUSTER_FTYPE_PTR_MASK) == 0));
            const uint32_t sv_cidx = sv.type & BUSTER_FTYPE_CIDX_MASK & sv_complex;
            const FastType st = ftype_promote_int(cc, sv.type);
            const int32_t slot = fast_cc_alloc_local(cc, 8u, 8u);
            {
                FastExprCtx ex;
                fast_expr_ctx_reset(&ex, cc);
                if (fval_holds_gp(&sv)) { ex.gp_used |= (uint16_t)(1u << sv.reg); }
                fval_store(&ex, &(FastVal){ .type = st, .kind = FVAL_LOCAL, .off = slot }, &sv, false, NULL);
            }
            fast_cc_reset_tmps(cc);
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) after switch"); break; }
            cc->pos += 1u;
            const uint32_t disp_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            x86_jmp_label(obj, disp_lbl);
            const uint16_t swi = (uint16_t)sw_sp++;
            sw_stack[swi] = (FastSwitchCtx){
                .val_slot = slot, .val_type = st, .case_start = sw_case_used,
                .case_count = 0, .default_lbl = UINT32_MAX, .break_lbl = brk_lbl,
                .enum_cidx = sv_cidx & (UINT32_C(0) - (uint32_t)(cc->ctypes[sv_cidx].kind == FCTYPE_ENUM)),
            };
            break_frame[break_sp] = (uint8_t)ssp;
            break_stack[break_sp++] = brk_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_SWITCH, .sw_idx = swi, .lbl0 = disp_lbl, .lbl1 = brk_lbl };
            continue;
        }

        // 9. `break;` / `continue;` / `goto label;`
        if (k == FTOK_KW_BREAK)
        {
            if (break_sp == 0) { fast_cc_bail(cc, "break outside loop/switch"); break; }
            cc->pos += 1u;
            // #3577: the statement ends with `;`, or the full pipeline's error.
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
            cc->pos += 1u;
            x86_jmp_label(obj, break_stack[break_sp - 1u]);
            sstack[break_frame[break_sp - 1u]].broke = 1u;
            live = false;
            continue;
        }
        if (k == FTOK_KW_CONTINUE)
        {
            if (cont_sp == 0) { fast_cc_bail(cc, "continue outside loop"); break; }
            cc->pos += 1u;
            // #3577: the statement ends with `;`, or the full pipeline's error.
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
            cc->pos += 1u;
            x86_jmp_label(obj, cont_stack[cont_sp - 1u]);
            sstack[cont_frame[cont_sp - 1u]].continued = 1u;
            live = false;
            continue;
        }
        if (k == FTOK_KW_GOTO)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "computed goto unsupported"); break; }
            const uint32_t lid = fast_cc_data(cc);
            cc->pos += 1u;
            // #3577: the statement ends with `;`, or the full pipeline's error.
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
            cc->pos += 1u;
            bool lok = true;
            const uint32_t ll = fast_cc_get_user_label(obj, ulabels, &ulabel_count, 512u, lid, false, &lok);
            if (!lok) { fast_cc_bail(cc, "label table full"); break; }
            x86_jmp_label(obj, ll);
            live = false;
            continue;
        }

        // 10. `return [expr];`
        if (k == FTOK_KW_RETURN)
        {
            live = false;
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_SEMI)
            {
                FastVal rv = { 0 };
                fast_cc_expr(cc, false, false, false, false, 0, &rv);
                if (cc->bailout) { break; }
                if (ftype_is_void(ret_t) && !ftype_is_void(rv.type))
                {
                    fast_cc_bail(cc, "return value in void func");
                    break;
                }
                fast_cc_check_implicit_conv(cc, ret_t, &rv);
                if (cc->bailout) { break; }
                FastExprCtx ex;
                fast_expr_ctx_reset(&ex, cc);
                if (fval_holds_gp(&rv)) { ex.gp_used |= (uint16_t)(1u << rv.reg); }
                if (fval_holds_xmm(&rv)) { ex.xmm_used |= (uint8_t)(1u << rv.reg); }

                if (ftype_is_record(ret_t))
                {
                    const FastComplexType* rct = cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK);
                    const uint32_t src_a = fval_addr_to_gp(&ex, &rv, true);
                    if (rct->abi_lo == FABI_MEM)
                    {
                        // Copy into hidden sret pointer `[rbp + cur_sret_off]` and return pointer in RAX!
                        const uint32_t dst_a = fval_alloc_gp(&ex);
                        x86_load(obj, dst_a, FREG_RBP, cc->cur_sret_off, 8u, false);
                        const uint32_t tmp = fval_alloc_gp(&ex);
                        x86_emit_memcpy_inline(obj, dst_a, 0, src_a, 0, rct->size, tmp);
                        x86_load(obj, FREG_RAX, FREG_RBP, cc->cur_sret_off, 8u, false);
                    }
                    else
                    {
                        // Load 1-2 eightbytes into RAX/RDX/XMM0/XMM1! Ensure src_a is not clobbered before second load!
                        uint32_t base_r = src_a;
                        if (base_r == FREG_RAX || base_r == FREG_RDX)
                        {
                            x86_mov_rr(obj, true, FREG_R10, base_r);
                            base_r = FREG_R10;
                        }
                        uint32_t gp_r = 0;
                        uint32_t fp_r = 0;
                        if (rct->abi_lo == FABI_INT) { x86_load(obj, gp_r == 0 ? FREG_RAX : FREG_RDX, base_r, 0, 8u, false); gp_r += 1u; }
                        else if (rct->abi_lo == FABI_SSE) { x86_sse_load(obj, true, fp_r++, base_r, 0); }
                        if (rct->abi_hi == FABI_INT) { x86_load(obj, gp_r == 0 ? FREG_RAX : FREG_RDX, base_r, 8, 8u, false); }
                        else if (rct->abi_hi == FABI_SSE) { x86_sse_load(obj, true, fp_r, base_r, 8); }
                    }
                }
                else if (ftype_is_float(ret_t))
                {
                    fval_convert(&ex, &rv, ret_t);
                    const uint32_t x = fval_to_xmm(&ex, &rv, true);
                    if (x != 0u) { x86_sse_rr(obj, ftype_is_f64(ret_t), 0x10u, 0u, x); }
                }
                else if (!ftype_is_void(ret_t))
                {
                    fval_convert(&ex, &rv, ret_t);
                    const uint32_t g = fval_to_gp(&ex, &rv, true);
                    if (g != FREG_RAX) { x86_mov_rr(obj, true, FREG_RAX, g); }
                }
                else
                {
                    fval_release(&ex, &rv);
                }
                fast_cc_reset_tmps(cc);
            }
            else if (!ftype_is_void(ret_t))
            {
                fast_cc_bail(cc, "bare return in non-void func");
                break;
            }
            // #3577: the statement ends with `;`, or the full pipeline's error.
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
            cc->pos += 1u;
            x86_jmp_label(obj, cc->cur_ret_label);
            continue;
        }

        // 11. Local declaration (inside block)
        if (top->kind == FSTK_BLOCK && fast_cc_is_type_start(cc, cc->pos))
        {
            FastDeclSpecs ds = { 0 };
            const FastType btype = fast_cc_parse_decl_specs(cc, &ds);
            if (cc->bailout) { break; }
            if (fast_cc_peek(cc) == FTOK_SEMI)
            {
                if (!fast_cc_tag_declaration_quiet(&ds)) { fast_cc_bail(cc, "declaration declares nothing"); break; }
                cc->pos += 1u;
                continue;
            }
            while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
            {
                const uint32_t decl_start = cc->pos;
                FastDeclaratorResult dr = { 0 };
                if (!fast_cc_parse_declarator(cc, btype, false, &dr, false) || !fast_cc_declarator_named(cc, dr.name_id)) { break; }
                const uint32_t extra_al = ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned;
                // #3577: inline and _Noreturn belong to file-scope functions, and a visibility on a typedef is the full
                // pipeline's warning (#3595); the attributes must be the kind's.
                if ((ds.is_inline | ds.is_noreturn) != 0)
                {
                    fast_cc_bail(cc, "block-scope inline or _Noreturn declaration");
                    break;
                }
                if (ds.is_typedef && (ds.attr.visibility_set || dr.attr.visibility_set))
                {
                    fast_cc_bail(cc, "visibility attribute on a typedef");
                    break;
                }
                // A block's asm label names a local register variable (`register long r10 __asm__("r10")`, #3592): a
                // `register` automatic 64-bit integer, in a register a function may clobber. Any other leaves.
                if (dr.attr.asm_label != NULL &&
                    ((ds.written & BUSTER_FSPEC_REGISTER) == 0 || ftype_size(cc, dr.type) != 8u ||
                     !(ftype_is_int(dr.type) || ftype_is_ptr(dr.type)) || fast_cc_asm_register(dr.attr.asm_label, dr.attr.asm_label_len) == UINT8_MAX))
                {
                    fast_cc_bail(cc, "asm label on a block's declaration");
                    break;
                }
                if ((ds.attr.present | dr.attr.present) != 0)
                {
                    uint8_t attr_flags = 0;
                    const FastDeclEntity entity = ds.is_typedef ? FDECL_TYPEDEF : ds.is_static ? FDECL_OBJECT : FDECL_LOCAL;
                    if (!fast_cc_decl_attributes_valid(cc, &ds.attr, &dr.attr, dr.type, ftype_is_func(dr.type), entity, ds.is_static != 0, &attr_flags) ||
                        attr_flags != 0)
                    {
                        fast_cc_bail(cc, "attributes of a block-scope declaration");
                        break;
                    }
                }
                if (ds.is_typedef)
                {
                    if (dr.name_id != 0)
                    {
                        // A typedef may name its type again in its scope (C11); anything else there is an error.
                        const uint32_t prev = cc->sym_cur[dr.name_id];
                        const bool again = prev != 0 && cc->syms[prev].kind == FSYM_TYPEDEF && cc->syms[prev].scope_depth == cc->scope_depth &&
                                           fast_cc_types_identical(cc, cc->syms[prev].type, dr.type);
                        // #3577: one with an alignment of its own (fast_cc_aligned_typedef).
                        uint8_t aligned = 0;
                        FastType typedef_type = dr.type;
                        if (!fast_cc_typedef_alignment_natural(cc, ds.attr.aligned, dr.attr.aligned, dr.type))
                        {
                            typedef_type = fast_cc_aligned_typedef(cc, ds.attr.aligned, dr.attr.aligned, dr.type, &aligned);
                        }
                        if (!again)
                        {
                            fast_cc_sym_bind(cc, dr.name_id, (FastSym){ .kind = FSYM_TYPEDEF, .type = typedef_type, .use_flags = aligned });
                        }
                        else
                        {
                            cc->syms[prev].use_flags |= aligned;
                        }
                    }
                }
                else if (ftype_is_func(dr.type) || ds.is_extern)
                {
                    // A block's declaration of a function or an extern object names the entity of the file-scope
                    // declaration it sees, with that one's linkage and attributes (C11 6.2.2p4), so it binds no symbol
                    // of its own (#3595). One that sees none (the file declares it later or never: symbol 0, of no
                    // kind), or that a local of the name hides, or that brings attributes of its own, is left to the
                    // full pipeline. (Its asm label left above.) #3577: so is one whose type the full pipeline checks
                    // against the file's and refuses.
                    if (ftype_is_func(dr.type))
                    {
                        fast_cc_check_main(cc, dr.name_id, dr.type);
                    }
                    const uint32_t seen = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                    const uint8_t kind = ftype_is_func(dr.type) ? (uint8_t)FSYM_FUNC : (uint8_t)FSYM_GLOBAL;
                    if (dr.name_id == 0 || cc->syms[seen].kind != kind || (kind == FSYM_GLOBAL && cc->syms[seen].storage == 1u) ||
                        ds.is_static || ds.attr.present || dr.attr.present ||
                        (kind == FSYM_FUNC && !fast_cc_function_redeclarable(cc, cc->syms + seen, dr.type, 2u)) ||
                        (kind == FSYM_GLOBAL && !fast_cc_types_redeclarable(cc, cc->syms[seen].type, dr.type)))
                    {
                        fast_cc_bail(cc, "a block's declaration of a function or extern object");
                        break;
                    }
                }
                else if (ds.is_static)
                {
                    // Local `static` variable: unique internal STB_LOCAL symbol in .rodata / .data / .data.rel.ro / .bss!
                    // #3577: a modifiable one in an inline definition the unit's end decides (C99's plain one, GNU's extern
                    // one, which the full pipeline may leave to another unit) is its -Wstatic-local-in-inline.
                    if ((uint32_t)((cc->syms[cc->cur_func_sym].inline_rules & BUSTER_FINLINE_PENDING) != 0) & (uint32_t)!ftype_is_const(dr.type))
                    {
                        fast_cc_bail(cc, "a modifiable static local in an inline definition");
                        break;
                    }
                    // #3577: an object of incomplete type is the full pipeline's error.
                    if (BUSTER_FAST_OBJECT_INCOMPLETE(cc, dr.type, fast_cc_peek(cc) == FTOK_ASSIGN) != 0) { fast_cc_bail(cc, "object of incomplete type"); break; }
                    char sname[96];
                    uint32_t nlen = 0;
                    const char* raw_n = dr.name_id != 0 ? fast_cc_ident_str(cc, dr.name_id, &nlen) : "s";
                    const uint32_t seq = cc->static_local_seq++;
                    const int r = snprintf(sname, sizeof(sname), "%.*s.%u", (int)(nlen < 64u ? nlen : 64u), raw_n, seq);
                    const uint32_t slen = r > 0 ? (uint32_t)r : 1u;
                    const uint32_t osym = fast_obj_add_sym(obj, sname, slen, STB_LOCAL, STT_OBJECT);
                    FastSecId sec = FSEC_BSS;
                    uint32_t sec_off = 0;
                    if (fast_cc_peek(cc) == FTOK_ASSIGN)
                    {
                        cc->pos += 1u;
                        const bool is_ro = ftype_is_object_const(cc, dr.type);
                        sec = is_ro ? (ftype_contains_ptr(cc, dr.type) ? FSEC_DATA_REL_RO : FSEC_RODATA) : FSEC_DATA;
                        sec_off = fast_cc_init_static(cc, &dr.type, sec, extra_al);
                    }
                    else
                    {
                        uint32_t al = ftype_align(cc, dr.type);
                        if (extra_al > al) { al = extra_al; }
                        sec_off = fast_obj_alloc_section(obj, FSEC_BSS, ftype_size(cc, dr.type), al, NULL);
                    }
                    if (osym != 0)
                    {
                        obj->syms[osym].section = sec;
                        obj->syms[osym].value = sec_off;
                        obj->syms[osym].size = ftype_size(cc, dr.type);
                        obj->syms[osym].defined = 1u;
                    }
                    if (dr.name_id != 0)
                    {
                        const uint8_t fold = sec == FSEC_RODATA || sec == FSEC_DATA_REL_RO ? fast_cc_static_fold_class(cc, dr.type, sec, sec_off) : 0u;
                        fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                            .kind = FSYM_GLOBAL, .storage = 1u, .defined = 1u, .type = dr.type,
                            .obj_sym = osym, .section = sec, .sec_off = sec_off, .pad_sym = { 0u, fold },
                        });
                    }
                }
                else
                {
                    // Automatic local variable on stack (bind BEFORE initializer so `T *p = malloc(sizeof(*p))` works!).
                    // A local register variable keeps its register's name for the asm statements that read it. #3577: an
                    // object of incomplete type is the full pipeline's error.
                    const uint32_t initialized = (uint32_t)(fast_cc_peek(cc) == FTOK_ASSIGN);
                    const uint32_t incomplete = BUSTER_FAST_OBJECT_INCOMPLETE(cc, dr.type, initialized);
                    if (((uint32_t)ds.is_unsupported_type | (uint32_t)ftype_is_unsupported(dr.type) | incomplete) != 0)
                    {
                        fast_cc_bail(cc, incomplete != 0 ? "object of incomplete type" : "unsupported local var type");
                        break;
                    }
                    // A register variable: a scalar's address is the full pipeline's error, which the use flag finds; an
                    // array's or a record's use takes it.
                    if (ds.is_register && !ftype_is_scalar(dr.type)) { fast_cc_bail(cc, "register aggregate"); break; }
                    const uint32_t lsi = dr.name_id != 0 ? fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_LOCAL, .type = dr.type, .use_flags = (uint8_t)(ds.is_register * BUSTER_FSYM_REGISTER),
                        .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    }) : 0u;
                    int32_t loff = 0;
                    if (initialized != 0)
                    {
                        cc->pos += 1u;
                        const uint32_t init_start = cc->pos;
                        cc->init_kind = FVAL_VOID;
                        loff = fast_cc_init_local(cc, &dr.type, extra_al);
                        BUSTER_FAST_LOCAL_FOLD(cc, lsi, dr.type, init_start)
                    }
                    else
                    {
                        uint32_t uninitialized_const = 0;
                        BUSTER_FAST_UNINITIALIZED_CONST(cc, dr.type, uninitialized_const)
                        if (uninitialized_const != 0)
                        {
                            fast_cc_bail(cc, "uninitialized const object or const member");
                            break;
                        }
                        uint32_t al = ftype_align(cc, dr.type);
                        if (extra_al > al) { al = extra_al; }
                        loff = fast_cc_alloc_local(cc, ftype_size(cc, dr.type), al);
                    }
                    if (lsi != 0)
                    {
                        cc->syms[lsi].type = dr.type;
                        cc->syms[lsi].stack_off = loff;
                    }
                }
                if (fast_cc_peek(cc) == FTOK_COMMA) { fast_cc_declarator_comma(cc); }
                else if (cc->pos == decl_start && !cc->bailout) { fast_cc_bail(cc, "local decl stalled"); break; }
                else { break; }
            }
            if (cc->bailout) { break; }
            // #3577: the statement ends with `;`, or the full pipeline's error.
            if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
            cc->pos += 1u;
            continue;
        }

        // 11b. An asm statement of the shapes fast_cc_asm_statement takes (#3592)
        if (k == FTOK_KW_ASM)
        {
            fast_cc_asm_statement(cc);
            if (cc->bailout) { break; }
            continue;
        }

        // 12. Expression statement `expr;` — ultra-fast skip for `((void)(0));` (7,460 assert() expansions in sqlite3.c!)
        if (k == FTOK_LPAREN && cc->pos + 7u < cc->tcount &&
            cc->tk[cc->pos + 1u] == FTOK_LPAREN && cc->tk[cc->pos + 2u] == FTOK_KW_VOID &&
            cc->tk[cc->pos + 3u] == FTOK_RPAREN && cc->tk[cc->pos + 4u] == FTOK_LPAREN &&
            cc->tk[cc->pos + 5u] == FTOK_INT_LIT && cc->tk[cc->pos + 6u] == FTOK_RPAREN &&
            cc->tk[cc->pos + 7u] == FTOK_RPAREN && cc->pos + 8u < cc->tcount && cc->tk[cc->pos + 8u] == FTOK_SEMI)
        {
            cc->pos += 9u;
            continue;
        }
        fast_cc_expr(cc, false, true, false, false, 0, NULL);
        fast_cc_reset_tmps(cc);
        // #3577: the statement ends with `;`, or the full pipeline's error.
        if (fast_cc_peek(cc) != FTOK_SEMI) { fast_cc_bail(cc, "expected ; after a statement"); break; }
        cc->pos += 1u;
    }

    // 6. Verify all goto labels were defined and non-void functions return a value
    for (uint32_t i = 0; i < ulabel_count && !cc->bailout; i += 1)
    {
        if (!ulabels[i].defined) { fast_cc_bail(cc, "undefined goto label"); }
    }
    // #3577: a non-void function's end that may be reached is the full pipeline's -Wreturn-type (main's returns 0).
    if (!cc->bailout && !cc->quiet && !ftype_is_void(ret_t) && cc->cur_func_name_id != cc->id_main && live)
    {
        fast_cc_bail(cc, "non-void function's end may be reached");
    }

    // 7. Implicit `return 0` in `main`, epilogue, frame patch, jump fixup, FDE, ELF symbol!
    if (!cc->bailout)
    {
        if (cc->cur_func_name_id == cc->id_main)
        {
            x86_mov_ri(obj, false, FREG_RAX, 0u);
        }
        fast_label_bind(obj, cc->cur_ret_label);
        x86_leave(obj);
        x86_ret(obj);

        const uint64_t total_frame64 = ((uint64_t)(uint32_t)cc->cur_max_frame + (uint64_t)(uint32_t)cc->cur_max_call_stack + 15u) & ~UINT64_C(15);
        if (total_frame64 > BUSTER_FDIRECT_MAX_FRAME)
        {
            fast_cc_bail(cc, "stack frame too large");
        }
        else
        {
            const uint32_t total_frame = (uint32_t)total_frame64;
            memcpy(obj->text.data + sub_rsp_patch, &total_frame, 4);
        }

        fast_obj_patch_func_jumps(obj);
        if (obj->overflow) { fast_cc_bail(cc, "object buffer overflow"); }

        const uint32_t func_size = obj->text.len - func_start;
        if (obj->fde_count < obj->fde_cap)
        {
            obj->fdes[obj->fde_count++] = (FastFuncFDE){ .text_off = func_start, .text_size = func_size };
        }
        else { obj->overflow = true; }

        const uint32_t osym = fast_cc_ensure_obj_sym(cc, sym_idx);
        if (osym != 0)
        {
            obj->syms[osym].section = FSEC_TEXT;
            obj->syms[osym].value = func_start;
            obj->syms[osym].size = func_size;
            obj->syms[osym].defined = 1u;
        }
        fn_sym->defined = 1u;
    }

    fast_cc_scope_leave(cc, func_scope_mark);
    cc->sym_count = file_sym_count;
    cc->scratch->used = scratch_mark;
}

// #3577: whether the tokens of a body, [start, end), name a function or an object with internal linkage as the file sees
// it where the body begins: an inline definition with external linkage that uses one is the full pipeline's
// -Wstatic-in-inline (a static inline function aside, which it calls quietly). By the tokens: a parameter, a local or a
// member of such a name refuses the definition too, and so does one the main file defines, which the full pipeline
// shows nothing for.
static __attribute__((cold, noinline)) bool fast_cc_body_names_internal(const FastCCState* cc, uint32_t start, uint32_t end)
{
    bool named = false;
    for (uint32_t at = start; at < end && !named; at += 1u)
    {
        if (cc->tk[at] == FTOK_IDENT)
        {
            const uint32_t id = cc->td[at];
            const FastSym* sym = cc->syms + (id < cc->env_cap ? cc->sym_cur[id] : 0u);
            named = (sym->kind == FSYM_FUNC || sym->kind == FSYM_GLOBAL) && sym->storage == 1u;
        }
    }
    return named;
}

// #3577: checks an inline function's body where it is defined, as the full pipeline does: in the scope there (a
// declaration later in the unit isn't visible to it yet), and whether or not anything uses the function. The check
// compiles the body, and then drops what the compile added: the object's code, data, relocations, unwind records and
// symbols (the object's state, saved whole, and the symbols the compile gave their first object symbol), the function's
// own symbol's definition, and the constants and static-local names it cached. Its symbol is left defined, not emitted
// (2): where something references it, the ready queue compiles it again at the unit's end, as before. Uses in the body
// count for the unit's end alone (fast_cc_reference).
static __attribute__((noinline)) void fast_cc_check_body(FastCCState* cc, uint32_t sym_idx, uint32_t param_names_start, uint16_t param_count)
{
    FastObj* obj = cc->obj;
    const FastObj saved = *obj;
    const uint32_t own = cc->syms[sym_idx].obj_sym < obj->sym_count ? cc->syms[sym_idx].obj_sym : 0u;
    const FastObjSym own_symbol = obj->syms[own];
    const uint32_t cached[] = {
        cc->ro_sign_f32_off,
        cc->ro_sign_f64_off,
        cc->ro_u64_to_f64_off,
        cc->ro_f64_to_u64_off,
        cc->ro_f32_to_u64_off,
        cc->ro_one_f32_off,
        cc->ro_one_f64_off,
        cc->static_local_seq,
    };
    cc->checking = 1u;
    cc->checked_obj_sym_count = 0;
    fast_cc_compile_func(cc, sym_idx, param_names_start, param_count);
    cc->checking = 0u;
    *obj = saved;
    obj->syms[own] = own_symbol;
    for (uint32_t i = 0; i < cc->checked_obj_sym_count; i += 1u)
    {
        cc->syms[cc->checked_obj_syms[i]].obj_sym = 0;
    }
    cc->syms[sym_idx].defined = 2u;
    cc->ro_sign_f32_off = cached[0];
    cc->ro_sign_f64_off = cached[1];
    cc->ro_u64_to_f64_off = cached[2];
    cc->ro_f64_to_u64_off = cached[3];
    cc->ro_f32_to_u64_off = cached[4];
    cc->ro_one_f32_off = cached[5];
    cc->ro_one_f64_off = cached[6];
    cc->static_local_seq = cached[7];
}

// ---------------------------------------------------------------------------------------------------------------------
// Level 14: Translation unit driver (file-scope decls, deferred static inline fixed-point, tentative .bss!)

// What a file-scope declaration gives its symbol beside its type (#3595): weak, an explicit visibility (the declarator's
// over its specifiers', as the later one), an asm label, and its inline spelling (BUSTER_FINLINE_*).
typedef struct FastDeclJoin FastDeclJoin;
struct FastDeclJoin
{
    const char* label;
    uint32_t label_len;
    uint8_t visibility;
    uint8_t visibility_set;
    uint8_t weak;
    uint8_t inline_rules;
};

static FastDeclJoin fast_cc_decl_join(const FastDeclSpecs* ds, const FastDeclaratorResult* dr, bool function, bool body)
{
    uint8_t rules = 0;
    if (function)
    {
        rules |= (ds->attr.gnu_inline || dr->attr.gnu_inline) ? BUSTER_FINLINE_GNU : 0u;
        rules |= (ds->is_inline && !ds->is_extern) ? BUSTER_FINLINE_PLAIN : 0u;
        rules |= (!body && (!ds->is_inline || ds->is_extern)) ? BUSTER_FINLINE_EXTERNAL : 0u;
    }
    return (FastDeclJoin){
        .label = dr->attr.asm_label,
        .label_len = dr->attr.asm_label_len,
        .visibility = dr->attr.visibility_set ? dr->attr.visibility : ds->attr.visibility,
        .visibility_set = (ds->attr.visibility_set || dr->attr.visibility_set) ? 1u : 0u,
        .weak = (ds->attr.weak || dr->attr.weak) ? 1u : 0u,
        .inline_rules = rules,
    };
}

// A later file-scope declaration of a symbol joins its weak, visibility, asm label and inline spelling, as the full
// pipeline's does (#3595). Two explicit visibilities or two labels that differ are clang's error; a change to a symbol
// already in the object (used or defined before this declaration) is left to the full pipeline, whose rule differs by
// attribute (a function's first linkage stays, its visibility doesn't), unless the declaration is an alias, which the
// unit's end defines with its final binding and visibility (fast_cc_alias_define); and so is a gnu_inline after a
// pending inline definition, whose rule it would change.
static void fast_cc_join_declaration(FastCCState* cc, uint32_t si, const FastDeclJoin* join, bool alias)
{
    FastSym* sym = cc->syms + si;
    const bool relabels = join->label != NULL && (sym->asm_name == NULL || sym->asm_name_len != join->label_len ||
                                                  memcmp(sym->asm_name, join->label, join->label_len) != 0);
    const bool revisits = join->visibility_set != 0 && join->visibility != sym->visibility;
    const bool changes = relabels || revisits || (join->weak != 0 && sym->is_weak == 0);
    if ((revisits && sym->visibility_set != 0) || (relabels && sym->asm_name != NULL))
    {
        fast_cc_bail(cc, "a declaration's visibility or label against an earlier one");
    }
    else if (changes && sym->obj_sym != 0 && !alias)
    {
        fast_cc_bail(cc, "an attribute after the symbol's first use");
    }
    else if ((join->inline_rules & BUSTER_FINLINE_GNU) != 0 && (sym->inline_rules & (BUSTER_FINLINE_GNU | BUSTER_FINLINE_PENDING)) == BUSTER_FINLINE_C99_PENDING)
    {
        fast_cc_bail(cc, "gnu_inline after an inline definition");
    }
    else
    {
        sym->is_weak |= join->weak;
        sym->visibility = join->visibility_set != 0 ? join->visibility : sym->visibility;
        sym->visibility_set |= join->visibility_set;
        sym->asm_name = join->label != NULL ? join->label : sym->asm_name;
        sym->asm_name_len = join->label != NULL ? join->label_len : sym->asm_name_len;
        sym->inline_rules |= join->inline_rules;
    }
}

// At the unit's end, C99's and GNU's inline definitions (#3595): one is emitted where the unit's declarations make it
// external, whether or not it is referenced, and otherwise not at all, its references the external function's. GNU's
// extern inline one beside the function's ordinary definition (its idiom) stays out: the bodies' visit skips a
// defined function, as the full pipeline keeps the ordinary one.
static void fast_cc_decide_inline_definitions(FastCCState* cc, uint32_t deferred_count)
{
    for (uint32_t i = 0; i < deferred_count; i += 1)
    {
        const uint8_t rules = cc->syms[cc->deferred[i].sym_idx].inline_rules;
        const bool external = ((rules & BUSTER_FINLINE_C99_PENDING) != 0 && (rules & BUSTER_FINLINE_EXTERNAL) != 0) ||
                              ((rules & BUSTER_FINLINE_GNU_PENDING) != 0 && (rules & BUSTER_FINLINE_PLAIN) != 0);
        if (external)
        {
            fast_cc_ready_task(cc, i);
        }
    }
}

// GNU's alias attribute (#3592): a file-scope declaration of a function or object with alias("target"), not a definition
// and not static, is defined at the end of the unit at its target's place. Recorded where it is declared.
static void fast_cc_alias_add(FastCCState* cc, uint32_t sym_idx, const FastAttrInfo* attr)
{
    if (cc->alias_count < BUSTER_FALIAS_LIMIT)
    {
        cc->aliases[cc->alias_count] = (FastAlias){ .sym_idx = sym_idx, .target = attr->alias_name, .target_len = attr->alias_len };
        cc->alias_count += 1u;
        cc->syms[sym_idx].is_alias = 1u;
    }
    else
    {
        fast_cc_bail(cc, "too many aliases");
    }
}

// Before the reachable bodies are compiled: every alias attribute the parser saw was recorded by a file-scope declaration
// of a function or object without an initializer (one anywhere else, on a definition, a typedef, an initialized object, in
// a block, a member or a parameter, is not, and leaves here), and each
// target is a function or object of its alias's kind, named without an asm label, not an alias itself, and declared by
// now; the alias references it, so that a static inline target is compiled. clang refuses the rest, or the direct path
// doesn't take them (a chain of aliases, a target whose ELF name its asm label gives).
static void fast_cc_alias_targets(FastCCState* cc)
{
    if (cc->alias_count != cc->alias_attributes)
    {
        fast_cc_bail(cc, "alias attribute outside a file-scope declaration");
    }
    for (uint32_t i = 0; i < cc->alias_count && !cc->bailout; i += 1)
    {
        FastAlias* alias = cc->aliases + i;
        const uint32_t id = fast_ident_intern(&cc->pp->idents, (const uint8_t*)alias->target, alias->target_len);
        const uint32_t target = id != 0 && id < cc->env_cap ? cc->sym_cur[id] : 0u;
        const uint8_t kind = cc->syms[alias->sym_idx].kind;
        bool ok = target != 0 && target != alias->sym_idx && cc->syms[target].kind == kind && (kind == FSYM_FUNC || kind == FSYM_GLOBAL) &&
                  cc->syms[target].asm_name == NULL;
        for (uint32_t j = 0; ok && j < cc->alias_count; j += 1)
        {
            ok = cc->aliases[j].sym_idx != target;
        }
        if (ok)
        {
            alias->target_idx = target;
            fast_cc_reference(cc, cc->syms + target);
        }
        else
        {
            fast_cc_bail(cc, "alias's target");
        }
    }
}

// After the tentative definitions: each alias is defined where its target is, the target's section, value and size,
// with its own binding (weak with __weak__) and visibility. A target this unit doesn't define is clang's error.
static void fast_cc_alias_define(FastCCState* cc)
{
    for (uint32_t i = 0; i < cc->alias_count && !cc->bailout; i += 1)
    {
        const FastAlias* alias = cc->aliases + i;
        FastSym* target = cc->syms + alias->target_idx;
        FastSym* sym = cc->syms + alias->sym_idx;
        const uint32_t target_obj = target->defined != 0 ? fast_cc_ensure_obj_sym(cc, alias->target_idx) : 0u;
        const uint32_t sym_obj = target_obj != 0 ? fast_cc_ensure_obj_sym(cc, alias->sym_idx) : 0u;
        if (sym_obj != 0)
        {
            const FastObjSym place = cc->obj->syms[target_obj];
            FastObjSym* out = cc->obj->syms + sym_obj;
            out->section = place.section;
            out->value = place.value;
            out->size = place.size;
            out->defined = 1u;
            out->binding = sym->is_weak ? STB_WEAK : STB_GLOBAL;
            out->visibility = sym->visibility;
            sym->defined = 1u;
            sym->section = target->section;
            sym->sec_off = target->sec_off;
        }
        else
        {
            fast_cc_bail(cc, "alias of a target the unit doesn't define");
        }
    }
}

static bool fast_cc_compile_unit(FastCCState* cc)
{
    FastObj* obj = cc->obj;
    FastBodyTask* deferred = (FastBodyTask*)fast_arena_alloc(cc->arena, BUSTER_FDEFERRED_LIMIT * sizeof(FastBodyTask), 8);
    cc->deferred = deferred;
    uint32_t deferred_count = 0;
    if (deferred == NULL) { fast_cc_bail(cc, "deferred inline pool alloc"); return false; }

    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_EOF && !cc->bailout)
    {
        const uint32_t tu_loop_pos = cc->pos;
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_SEMI || k == FTOK_KW_EXTENSION) { cc->pos += 1u; continue; }
        if (k == FTOK_KW_STATIC_ASSERT)
        {
            fast_cc_static_assert(cc);
            if (cc->bailout) { break; }
            continue;
        }
        if (k == FTOK_KW_ASM) { fast_cc_bail(cc, "file-scope asm"); break; }

        FastDeclSpecs ds = { 0 };
        const FastType btype = fast_cc_parse_decl_specs(cc, &ds);
        if ((ds.written & (BUSTER_FSPEC_REGISTER | BUSTER_FSPEC_AUTO)) != 0) { fast_cc_bail(cc, "register or auto at file scope"); }
        if (cc->bailout) { break; }
        if (fast_cc_peek(cc) == FTOK_SEMI)
        {
            // #3577: a list that declares nothing but a tag, quietly (fast_cc_tag_declaration_quiet).
            if (!fast_cc_tag_declaration_quiet(&ds)) { fast_cc_bail(cc, "declaration declares nothing"); break; }
            cc->pos += 1u;
            continue;
        }
        // File-scope register, and _Noreturn or inline on what isn't a function (checked per declarator below).
        if (ds.is_register) { fast_cc_bail(cc, "file-scope register"); break; }

        bool first = true;
        bool ended_by_body = false;
        while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
        {
            FastDeclaratorResult dr = { 0 };
            const uint32_t ctypes_before = cc->ctype_count;
            if (!fast_cc_parse_declarator(cc, btype, false, &dr, first) || !fast_cc_declarator_named(cc, dr.name_id)) { break; }
            const uint32_t extra_al = ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned;
            const bool is_func = ftype_is_func(dr.type);
            const bool is_def = first && is_func && fast_cc_peek(cc) == FTOK_LBRACE;
            const FastDeclJoin join = fast_cc_decl_join(&ds, &dr, is_func, is_def);
            const FastAttrInfo* alias = dr.attr.alias_name != NULL ? &dr.attr : ds.attr.alias_name != NULL ? &ds.attr : NULL;
            // #3577: inline and _Noreturn belong to functions, and a _Noreturn body is the full pipeline's to check
            // (-Winvalid-noreturn); the attributes, where there are any, must be the declared kind's.
            if (((uint32_t)(ds.is_inline | ds.is_noreturn) &
                 ((uint32_t)!is_func | (uint32_t)ds.is_typedef | ((uint32_t)is_def & (uint32_t)ds.is_noreturn))) != 0)
            {
                fast_cc_bail(cc, "inline or _Noreturn where the full pipeline checks it");
                break;
            }
            uint8_t attr_flags = 0;
            if ((ds.attr.present | dr.attr.present) != 0)
            {
                const FastDeclEntity entity = ds.is_typedef ? FDECL_TYPEDEF : is_def ? FDECL_DEFINITION : is_func ? FDECL_FUNCTION : FDECL_OBJECT;
                if (!fast_cc_decl_attributes_valid(cc, &ds.attr, &dr.attr, dr.type, is_func, entity, ds.is_static != 0, &attr_flags))
                {
                    fast_cc_bail(cc, "attributes of a file-scope declaration");
                    break;
                }
            }
            // (BUSTER_FSYM_UNSUPPORTED is bit 0, is_unsupported_type 0 or 1.)
            // #3577: printf, fprintf, sprintf and snprintf are the library's builtins, whose formats the full pipeline
            // checks as an attribute's.
            const uint8_t use_flags = (uint8_t)(ds.is_unsupported_type | attr_flags |
                                                (BUSTER_FSYM_FORMAT * (uint32_t)(cc->pp->idents.entries[dr.name_id].pp_kw == FPP_KW_FORMAT_FUNCTION)));
            // A function's nonnull parameters go in its type (fast_cc_nonnull_arguments_clean), which must be this
            // declarator's own, not a typedef's that other functions share.
            const uint32_t nonnull_mask = ds.attr.nonnull_mask | dr.attr.nonnull_mask;
            if (nonnull_mask != 0)
            {
                if ((dr.type & BUSTER_FTYPE_CIDX_MASK) < ctypes_before) { fast_cc_bail(cc, "nonnull on a typedef's function type"); break; }
                cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].tag_ident = nonnull_mask;
            }

            if (!ds.is_typedef)
            {
                fast_cc_check_main(cc, dr.name_id, dr.type);
            }
            if (alias != NULL && (ds.is_static || ds.is_inline || join.label != NULL))
            {
                fast_cc_bail(cc, "alias attribute on a static, inline or labelled declaration");
                break;
            }
            // Function definition!
            if (is_def)
            {
                const uint8_t stor = ds.is_static ? (ds.is_inline ? 4u : 1u) :
                                     (ds.is_inline && !ds.is_extern) ? 3u : 0u;
                // Reuse existing prototype FastSym if present at file scope!
                uint32_t si = (dr.name_id < cc->env_cap) ? cc->sym_cur[dr.name_id] : 0u;
                if (si != 0 && cc->syms[si].kind == FSYM_FUNC)
                {
                    // A pending inline definition allows a later one only as GNU's idiom: its extern inline one,
                    // then the external definition, not inline (#3595). (The extern inline body was checked where it
                    // was defined, which leaves its symbol defined, not emitted: 2, #3577.)
                    const uint8_t pending = cc->syms[si].inline_rules & BUSTER_FINLINE_PENDING;
                    const bool gnu_idiom = pending == BUSTER_FINLINE_GNU_PENDING && !ds.is_inline;
                    if (cc->syms[si].defined == 1u || cc->syms[si].is_alias != 0 || ((uint32_t)(cc->syms[si].defined | pending) != 0 && !gnu_idiom))
                    {
                        fast_cc_bail(cc, "redefinition of function");
                        break;
                    }
                    if (!fast_cc_function_redeclarable(cc, cc->syms + si, dr.type, stor))
                    {
                        fast_cc_bail(cc, "conflicting function redeclaration");
                        break;
                    }
                    if (dr.attr.asm_label != NULL && cc->syms[si].obj_sym != 0)
                    {
                        fast_cc_bail(cc, "asm label after the symbol's use");
                        break;
                    }
                    cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].tag_ident |= cc->ctypes[cc->syms[si].type & BUSTER_FTYPE_CIDX_MASK].tag_ident;
                    cc->syms[si].type = dr.type;
                    if (stor == 1u || stor == 4u) { cc->syms[si].storage = stor; }
                    cc->syms[si].use_flags |= use_flags;
                    fast_cc_join_declaration(cc, si, &join, false);
                    if (cc->bailout) { break; }
                }
                else
                {
                    si = fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_FUNC, .storage = stor, .is_weak = join.weak, .visibility = join.visibility,
                        .visibility_set = join.visibility_set, .inline_rules = join.inline_rules,
                        .use_flags = use_flags, .type = dr.type,
                        .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    });
                    if (cc->bailout) { break; }
                }

                // An inline definition with external linkage is C99's or GNU's (#3595): GNU's extern inline one and
                // C99's plain one wait for the unit's end, which decides whether either is emitted; GNU's plain one and
                // C99's extern one are external definitions, emitted here. With internal linkage (static, or a static
                // declaration before it), an inline body waits for its first reference, so that unused helpers emit
                // nothing. #3577: a body that waits is checked here (fast_cc_check_body), in the scope it has here,
                // whether it is emitted or not.
                const uint8_t storage = cc->syms[si].storage;
                const bool internal = storage == 1u || storage == 4u;
                const bool gnu = (cc->syms[si].inline_rules & BUSTER_FINLINE_GNU) != 0;
                const uint8_t decide = !ds.is_inline || internal ? 0u :
                                       gnu && ds.is_extern ? (uint8_t)BUSTER_FINLINE_GNU_PENDING :
                                       !gnu && !ds.is_extern ? (uint8_t)BUSTER_FINLINE_C99_PENDING : 0u;
                cc->syms[si].inline_rules |= decide;
                // #3577: an inline definition with external linkage that names a static function or object is the full
                // pipeline's -Wstatic-in-inline (fast_cc_body_names_internal).
                if (ds.is_inline && !internal)
                {
                    const FastBodyScan named = fast_cc_prescan_body(cc, cc->pos);
                    if (named.end_pos == UINT32_MAX || fast_cc_body_names_internal(cc, cc->pos, named.end_pos))
                    {
                        fast_cc_bail(cc, "an inline definition with external linkage that names a static declaration");
                        break;
                    }
                }
                if ((ds.is_inline && internal) || decide != 0)
                {
                    const FastBodyScan bscan = fast_cc_prescan_body(cc, cc->pos);
                    if (bscan.end_pos == UINT32_MAX) { fast_cc_bail(cc, "unclosed inline body"); break; }
                    if (deferred_count < BUSTER_FDEFERRED_LIMIT)
                    {
                        deferred[deferred_count] = (FastBodyTask){
                            .sym_idx = si, .body_start = cc->pos,
                            .param_names_start = dr.param_names_start, .param_count = dr.param_count,
                            .previous_task = cc->syms[si].deferred_task,
                        };
                        if (cc->syms[si].referenced == 1u && decide == 0) { fast_cc_ready_task(cc, deferred_count); }
                        cc->syms[si].deferred_task = (uint16_t)(++deferred_count);
                    }
                    else { fast_cc_bail(cc, "too many inline functions"); break; }
                    // #3577: checked here, in its own scope, whether used or not.
                    if ((use_flags & BUSTER_FSYM_UNSUPPORTED) != 0) { fast_cc_bail(cc, "unsupported type in an inline definition"); break; }
                    fast_cc_check_body(cc, si, dr.param_names_start, dr.param_count);
                    cc->pos = bscan.end_pos + 1u;
                }
                else
                {
                    if (ds.is_unsupported_type) { fast_cc_bail(cc, "unsupported type in func def"); break; }
                    fast_cc_compile_func(cc, si, dr.param_names_start, dr.param_count);
                }
                ended_by_body = true;
                break; // function definition ends without ';'
            }
            first = false;

            // Typedef (a visibility on one is the full pipeline's warning, #3595)
            if (ds.is_typedef && (ds.attr.visibility_set || dr.attr.visibility_set))
            {
                fast_cc_bail(cc, "visibility attribute on a typedef");
                break;
            }
            if (ds.is_typedef)
            {
                if (dr.name_id != 0)
                {
                    // A typedef may name its type again (C11); another type, or another kind of symbol, is an error.
                    // #3577: an alignment of its own, other than its type's, is this path's to refuse where the typedef
                    // is used (it keeps none for a typedef): glibc's <pthread.h> declares one nothing uses.
                    const uint32_t prev = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                    const bool again = prev != 0 && cc->syms[prev].kind == FSYM_TYPEDEF && cc->syms[prev].scope_depth == 0 &&
                                       fast_cc_types_identical(cc, cc->syms[prev].type, dr.type);
                    uint8_t aligned = 0;
                    FastType typedef_type = dr.type;
                    if (!fast_cc_typedef_alignment_natural(cc, ds.attr.aligned, dr.attr.aligned, dr.type))
                    {
                        typedef_type = fast_cc_aligned_typedef(cc, ds.attr.aligned, dr.attr.aligned, dr.type, &aligned);
                    }
                    if (!again)
                    {
                        fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                            .kind = FSYM_TYPEDEF, .type = typedef_type, .use_flags = (uint8_t)(use_flags | aligned),
                        });
                    }
                    else
                    {
                        cc->syms[prev].use_flags |= aligned;
                    }
                }
            }
            // Function prototype
            else if (is_func)
            {
                if (dr.name_id != 0)
                {
                    const uint32_t existing = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                    if (existing != 0 && cc->syms[existing].kind == FSYM_FUNC)
                    {
                        const uint8_t stor = ds.is_static ? 1u : (ds.is_inline && !ds.is_extern) ? 3u : 2u;
                        if (!fast_cc_function_redeclarable(cc, cc->syms + existing, dr.type, stor))
                        {
                            fast_cc_bail(cc, "conflicting function redeclaration");
                            break;
                        }
                        // #3577: an attribute after the definition is ignored with a warning (-Wignored-attributes), and an
                        // asm label after the symbol's use or definition is the full pipeline's.
                        if (((ds.attr.present | dr.attr.present) != 0 && cc->syms[existing].defined != 0) ||
                            (dr.attr.asm_label != NULL && (cc->syms[existing].defined != 0 || cc->syms[existing].obj_sym != 0)))
                        {
                            fast_cc_bail(cc, "attribute or label after the definition");
                            break;
                        }
                        // The declarations' nonnull parameters together, in both types (either may stay the symbol's).
                        const uint32_t merged = cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].tag_ident |
                                                cc->ctypes[cc->syms[existing].type & BUSTER_FTYPE_CIDX_MASK].tag_ident;
                        cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].tag_ident = merged;
                        cc->ctypes[cc->syms[existing].type & BUSTER_FTYPE_CIDX_MASK].tag_ident = merged;
                        // Keep definition/obj_sym if already defined, update prototype info if more specific!
                        const FastComplexType* old_fn = cc->ctypes + (cc->syms[existing].type & BUSTER_FTYPE_CIDX_MASK);
                        if (old_fn->is_oldstyle && !cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].is_oldstyle)
                        {
                            cc->syms[existing].type = dr.type;
                        }
                        // An alias of a function a header declared: not of one defined, static or with an inline
                        // definition pending, which are clang's errors. An alias is a definition, so another alias, and
                        // an attribute on a later declaration (the full pipeline's warning), leave too: any declaration
                        // after an alias does (#3592).
                        const FastSym* declared = cc->syms + existing;
                        if (declared->is_alias != 0 ||
                            (alias != NULL && (declared->defined != 0 || declared->storage == 1u || declared->storage == 4u ||
                                               (declared->inline_rules & BUSTER_FINLINE_PENDING) != 0)))
                        {
                            fast_cc_bail(cc, "a declaration of an alias, or an alias of a defined or static function");
                            break;
                        }
                        // #3577: a later declaration's weak makes the symbol weak (the unit's end refuses one with
                        // internal linkage); its visibility and label join as #3595's.
                        fast_cc_join_declaration(cc, existing, &join, alias != NULL);
                        if (cc->bailout) { break; }
                        cc->syms[existing].use_flags |= use_flags;
                        if (alias != NULL)
                        {
                            fast_cc_alias_add(cc, existing, alias);
                        }
                    }
                    else
                    {
                        const uint8_t stor = ds.is_static ? 1u : 2u;
                        const uint32_t si = fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                            .kind = FSYM_FUNC, .storage = stor, .is_weak = join.weak, .visibility = join.visibility,
                            .visibility_set = join.visibility_set, .inline_rules = join.inline_rules,
                            .use_flags = use_flags, .type = dr.type,
                            .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                        });
                        if (alias != NULL && si != 0)
                        {
                            fast_cc_alias_add(cc, si, alias);
                        }
                    }
                }
            }
            // File-scope global / static variable
            else if (dr.name_id != 0)
            {
                const bool has_init = fast_cc_peek(cc) == FTOK_ASSIGN;
                const uint32_t declared = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                if ((declared != 0 && cc->syms[declared].kind == FSYM_GLOBAL && cc->syms[declared].is_alias != 0) ||
                    (alias != NULL && declared != 0 && cc->syms[declared].kind == FSYM_GLOBAL &&
                     (cc->syms[declared].defined != 0 || cc->syms[declared].is_tentative != 0 || cc->syms[declared].storage == 1u)))
                {
                    fast_cc_bail(cc, "a declaration of an alias, or an alias of a defined, tentative or static object");
                    break;
                }
                const uint8_t stor = ds.is_static ? 1u : ds.is_extern ? 2u : 0u;
                uint32_t si = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                if (si != 0 && cc->syms[si].kind == FSYM_GLOBAL)
                {
                    // One type (an incomplete array completed), and no external linkage after internal or the reverse; no
                    // attribute after the definition, and no asm label after the symbol's use or definition.
                    const bool old_static = cc->syms[si].storage == 1u;
                    if (!fast_cc_types_redeclarable(cc, cc->syms[si].type, dr.type) || (stor == 1u && !old_static) || (stor == 0u && old_static))
                    {
                        fast_cc_bail(cc, "conflicting object redeclaration");
                        break;
                    }
                    if (((ds.attr.present | dr.attr.present) != 0 && cc->syms[si].defined != 0) ||
                        (dr.attr.asm_label != NULL && (cc->syms[si].defined != 0 || cc->syms[si].obj_sym != 0)))
                    {
                        fast_cc_bail(cc, "attribute or label after the definition");
                        break;
                    }
                    // Update incomplete array type or storage if redeclared!
                    if (ftype_is_array(cc->syms[si].type) && !cc->ctypes[cc->syms[si].type & BUSTER_FTYPE_CIDX_MASK].complete &&
                        ftype_is_array(dr.type) && cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].complete)
                    {
                        cc->syms[si].type = dr.type;
                    }
                    if (stor == 1u) { cc->syms[si].storage = 1u; }
                    cc->syms[si].use_flags |= use_flags;
                    fast_cc_join_declaration(cc, si, &join, alias != NULL);
                    if (cc->bailout) { break; }
                }
                else
                {
                    si = fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_GLOBAL, .storage = stor, .is_weak = join.weak, .visibility = join.visibility,
                        .visibility_set = join.visibility_set,
                        .is_const_var = (ds.is_const || ftype_is_const(dr.type)) ? 1u : 0u,
                        .use_flags = use_flags, .type = dr.type,
                        .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    });
                    if (cc->bailout) { break; }
                }

                if (has_init)
                {
                    if (cc->syms[si].defined != 0)
                    {
                        fast_cc_bail(cc, "redefinition of global variable");
                        break;
                    }
                    if (ds.is_unsupported_type || ftype_is_unsupported(dr.type)) { fast_cc_bail(cc, "unsupported global init type"); break; }
                    cc->pos += 1u; // '='
                    const bool is_ro = ftype_is_object_const(cc, dr.type);
                    const FastSecId sec = is_ro ? (ftype_contains_ptr(cc, dr.type) ? FSEC_DATA_REL_RO : FSEC_RODATA) : FSEC_DATA;
                    const uint32_t sec_off = fast_cc_init_static(cc, &dr.type, sec, extra_al);
                    cc->syms[si].type = dr.type;
                    cc->syms[si].defined = 1u;
                    if (sec != FSEC_DATA) { cc->syms[si].pad_sym[1] = fast_cc_static_fold_class(cc, dr.type, sec, sec_off); }
                    cc->syms[si].is_tentative = 0u;
                    cc->syms[si].section = sec;
                    cc->syms[si].sec_off = sec_off;
                    const uint32_t osym = fast_cc_ensure_obj_sym(cc, si);
                    if (osym != 0)
                    {
                        obj->syms[osym].section = sec;
                        obj->syms[osym].value = sec_off;
                        obj->syms[osym].size = ftype_size(cc, dr.type);
                        obj->syms[osym].defined = 1u;
                    }
                }
                else if (alias != NULL)
                {
                    fast_cc_alias_add(cc, si, alias);
                }
                else if (!ds.is_extern)
                {
                    cc->syms[si].is_tentative = 1u;
                }
            }

            if (fast_cc_peek(cc) == FTOK_COMMA) { fast_cc_declarator_comma(cc); }
            else { break; }
        }
        if (cc->bailout) { break; }
        // #3577: a declaration ends with `;`, a function definition with its body.
        if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
        else if (!ended_by_body) { fast_cc_bail(cc, "expected ; after top-level declaration"); break; }
        if (cc->pos == tu_loop_pos && !cc->bailout) { fast_cc_bail(cc, "top-level decl stalled"); break; }
    }

    fast_cc_alias_targets(cc);
    fast_cc_decide_inline_definitions(cc, deferred_count);

    // Visit newly reachable bodies in the same order as a forward scan: a reference to an earlier
    // definition waits until the scan wraps. This preserves .text, symbol and relocation order.
    // #3577: each body was checked where it was defined (fast_cc_check_body); these compiles emit the ones referenced.
    uint32_t cursor = 0;
    while (cc->ready_groups != 0 && !cc->bailout)
    {
        uint32_t group = cursor >> 6;
        uint64_t ready = cc->ready_tasks[group] & (UINT64_MAX << (cursor & 63u));
        if (ready == 0)
        {
            const uint64_t later = cc->ready_groups & ~_bzhi_u64(UINT64_MAX, group + 1u);
            group = (uint32_t)__builtin_ctzll(later != 0 ? later : cc->ready_groups);
            ready = cc->ready_tasks[group];
        }
        const uint32_t index = group * 64u + (uint32_t)__builtin_ctzll(ready);
        cc->ready_tasks[group] ^= _blsi_u64(ready);
        if (cc->ready_tasks[group] == 0) { cc->ready_groups &= ~(UINT64_C(1) << group); }
        cursor = (index + 1u) % BUSTER_FDEFERRED_LIMIT;

        const FastBodyTask* task = deferred + index;
        const FastSym* sym = cc->syms + task->sym_idx;
        if (sym->defined != 1u)
        {
            if ((sym->use_flags & BUSTER_FSYM_UNSUPPORTED) != 0) { fast_cc_bail(cc, "called unsupported static inline"); break; }
            cc->pos = task->body_start;
            fast_cc_compile_func(cc, task->sym_idx, task->param_names_start, task->param_count);
        }
    }

    // Finalize all tentative file-scope definitions into `.bss`!
    // #3577: and the unit's end the full pipeline checks: a static function used but never defined
    // (-Wundefined-internal), a tentative array definition never sized (-Wtentative-definition-array), a tentative
    // definition of a struct or union never completed (its error). Branch-free for the symbols that are none of these.
    uint32_t unit_end_bad = 0;
    for (uint32_t i = 1u; i < cc->sym_count && !cc->bailout; i += 1)
    {
        FastSym* s = cc->syms + i;
        unit_end_bad |= (uint32_t)(s->kind == FSYM_FUNC) & ((uint32_t)(s->storage == 1u) | (uint32_t)(s->storage == 4u)) &
                        (uint32_t)(s->referenced != 0) & (uint32_t)(s->defined == 0);
        // A weak declaration of a name with internal linkage is the full pipeline's error.
        unit_end_bad |= (uint32_t)(s->is_weak != 0) & ((uint32_t)(s->storage == 1u) | (uint32_t)(s->storage == 4u));
        if (s->kind == FSYM_GLOBAL && s->is_tentative != 0 && s->defined == 0)
        {
            if ((s->use_flags & BUSTER_FSYM_UNSUPPORTED) != 0) { fast_cc_bail(cc, "tentative unsupported global"); break; }
            if (BUSTER_FAST_OBJECT_INCOMPLETE(cc, s->type, 0u) != 0) { fast_cc_bail(cc, "tentative definition of an incomplete type"); break; }
            const uint32_t sz = ftype_size(cc, s->type);
            const uint32_t al = ftype_align(cc, s->type);
            const uint32_t boff = fast_obj_alloc_section(obj, FSEC_BSS, sz, al, NULL);
            s->defined = 1u;
            s->section = FSEC_BSS;
            s->sec_off = boff;
            const uint32_t osym = fast_cc_ensure_obj_sym(cc, i);
            if (osym != 0)
            {
                obj->syms[osym].section = FSEC_BSS;
                obj->syms[osym].value = boff;
                obj->syms[osym].size = sz;
                obj->syms[osym].defined = 1u;
            }
        }
    }
    if (unit_end_bad != 0)
    {
        fast_cc_bail(cc, "static function used but never defined");
    }

    fast_cc_alias_define(cc);

    return !cc->bailout && !obj->overflow;
}

#endif
