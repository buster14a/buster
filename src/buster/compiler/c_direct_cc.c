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
#include "buster/compiler/c_direct_pretty.c"

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
    if (!sym->referenced && (sym->inline_rules & BUSTER_FINLINE_PENDING) == 0)
    {
        sym->referenced = 1u;
        for (uint32_t link = sym->deferred_task; link != 0; link = cc->deferred[link - 1u].previous_task)
        {
            fast_cc_ready_task(cc, link - 1u);
        }
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
    uint8_t pad;
    const char* str_ptr;
    uint32_t str_len;
    FastType type;
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

static void fsc_cast_to(const FastCCState* cc, FastStaticConst* v, FastType target)
{
    if (v->sym_idx != 0)
    {
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
        return;
    }
    uint64_t x = 0;
    if (v->is_float != 0)
    {
        const double d = fsc_as_double(*v);
        if (ftype_is_bool(target)) { x = d != 0.0 ? 1u : 0u; }
        else if (ftype_is_signed(cc, target)) { x = (uint64_t)(int64_t)d; }
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
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = (int64_t)(int32_t)fast_cc_data(cc), .type = FTYPE_I32 }; }
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
                    if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = sym->const_val, .type = sym->type }; }
                    expect = false;
                    continue;
                }
                if (sym->kind == FSYM_FUNC)
                {
                    if (sym->is_unsupported) { fast_cc_bail(cc, "unsupported func ref in static init"); break; }
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
                if (vsp < 64u) { vals[vsp++] = (FastStaticConst){ .val = v, .is_unsigned = 1u, .type = FTYPE_U64 }; }
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
                    else { fsc_apply_op(cc, op, vals, &vsp); }
                }
                if (osp > 0) { osp -= 1u; }
                pdepth -= 1u;
                cc->pos += 1u;
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
                    else { fsc_apply_op(cc, op, vals, &vsp); }
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
                    else { fsc_apply_op(cc, top, vals, &vsp); }
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
        else { fsc_apply_op(cc, top, vals, &vsp); }
    }
    if (vsp != 1u && !cc->bailout) { fast_cc_bail(cc, "bad static const expr"); }
    return (!cc->bailout && vsp == 1u) ? vals[0] : (FastStaticConst){ 0 };
}

// Write one scalar static constant into `sec_buf + byte_off` (adding `R_X86_64_64` relocation if sym_idx != 0).
static void fast_cc_write_static_scalar(FastCCState* cc, FastSecId sec, uint32_t byte_off, FastType target,
                                        const FastMemberInfo* bf, FastStaticConst v)
{
    FastByteBuf* buf = sec == FSEC_DATA ? &cc->obj->data :
                       sec == FSEC_RODATA ? &cc->obj->rodata : &cc->obj->data_rel_ro;
    FastRelaBuf* rela = sec == FSEC_DATA ? &cc->obj->rela_data :
                        sec == FSEC_RODATA ? &cc->obj->rela_rodata : &cc->obj->rela_data_rel_ro;
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
        uint32_t elem_size;
        uint32_t max_elem;
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
            while (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
            {
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
                        f->idx = (uint32_t)(mi - (cc->member_info + rec->start)) + 1u;
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
                f->idx += 1u;
                // Skip flattened anonymous-union siblings sharing the same byte_offset in a struct!
                if (rec->kind == FCTYPE_STRUCT && mi->bit_width == 0)
                {
                    while (f->idx < rec->count && cc->member_info[rec->start + f->idx].byte_offset == mi->byte_offset &&
                           cc->member_info[rec->start + f->idx].bit_width == 0)
                    {
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
};

_Static_assert(sizeof(FastVal) == 24u, "expression stack entry size");

// Scratch register allocation order (RAX, R10, R11 first so ABI arg regs stay free longer!)
static const uint8_t fast_gp_pool[9] = {
    FREG_RAX, FREG_R10, FREG_R11, FREG_RCX, FREG_RDX, FREG_RSI, FREG_RDI, FREG_R8, FREG_R9
};

typedef struct FastExprCtx FastExprCtx;
struct FastExprCtx
{
    FastCCState* cc;
    FastVal vstack[128];
    uint32_t vsp;
    FastVal call_args[128];
    uint32_t arg_sp;
    uint16_t gp_used;      // bitmask of live GP scratch regs in vstack/call_args
    uint8_t xmm_used;      // bitmask of live XMM scratch regs (0..7)
    uint8_t eval_only;     // >0 inside sizeof(expr) / _Alignof(expr) / _Generic ctrl: emit ZERO machine code!
};

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
static __attribute__((noinline)) void fval_spill_one_full(FastExprCtx* ex, FastVal* v)
{
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

// The value kinds a spill writes: a register, a register's address, a bit-field through one, the flags. A local, a
// global, a constant and an address of either stay as they are, which is 82% of sqlite3.c's 61,000 spill requests:
// the filter is a bit test inline, and the spill out of line (#3581).
#define BUSTER_FVAL_SPILL_KINDS ((1u << FVAL_REG) | (1u << FVAL_XMM) | (1u << FVAL_INDIRECT) | (1u << FVAL_FLAGS) | (1u << FVAL_BITFIELD))

static inline __attribute__((always_inline)) void fval_spill_one(FastExprCtx* ex, FastVal* v)
{
    const uint32_t spills = (uint32_t)((BUSTER_FVAL_SPILL_KINDS >> v->kind) & 1u) & (uint32_t)(ex->eval_only == 0);
    if (spills != 0) { fval_spill_one_full(ex, v); }
}

// Materialize any FVAL_FLAGS currently on vstack/call_args before emitting an instruction that modifies EFLAGS!
static __attribute__((noinline, cold)) void fval_flush_flags_full(FastExprCtx* ex)
{
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

// Whether any value below holds the flags, as a leaf scan: sqlite3.c asks 30,000 times and finds flags 7 times. The
// materialization is the full function's (#3581).
static __attribute__((noinline)) void fval_flush_flags(FastExprCtx* ex)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < ex->vsp; i += 1) { found |= (uint32_t)(ex->vstack[i].kind == FVAL_FLAGS); }
    for (uint32_t i = 0; i < ex->arg_sp; i += 1) { found |= (uint32_t)(ex->call_args[i].kind == FVAL_FLAGS); }
    if ((found & (uint32_t)(ex->eval_only == 0)) != 0) { fval_flush_flags_full(ex); }
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
static __attribute__((noinline)) uint32_t fval_alloc_gp_avoid_full(FastExprCtx* ex, uint16_t avoid_mask)
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

// The first free register in the pool's order, without a loop: the free bits gathered in that order (RAX, R10, R11,
// RCX, RDX, RSI, RDI, R8, R9), and the lowest one's register from the pool. sqlite3.c asks 72,735 times and never
// finds the pool full; that case, and sizeof's operands that emit nothing, stay in the full function (#3581).
static inline __attribute__((always_inline)) uint32_t fval_alloc_gp_avoid(FastExprCtx* ex, uint16_t avoid_mask)
{
    const uint32_t free = ~(uint32_t)(ex->gp_used | avoid_mask);
    const uint32_t in_order = (free & 1u) | ((free >> 9) & 6u) | ((free << 2) & 0x18u) | ((free >> 1) & 0x1e0u);
    uint32_t r = 0;
    if (__builtin_expect((in_order != 0) & (ex->eval_only == 0), 1))
    {
        r = fast_gp_pool[__builtin_ctz(in_order)];
        ex->gp_used |= (uint16_t)(1u << r);
    }
    else
    {
        r = fval_alloc_gp_avoid_full(ex, avoid_mask);
    }
    return r;
}

static inline __attribute__((always_inline)) uint32_t fval_alloc_gp(FastExprCtx* ex) { return fval_alloc_gp_avoid(ex, 0); }

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
static __attribute__((noinline, cold)) uint32_t fval_to_gp_rare(FastExprCtx* ex, FastVal* v, bool consume)
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
        // The shifts set the flags: a comparison still in them, such as a call's last argument while an earlier one is
        // converted, is materialized first (#3659).
        if ((shl | shr) != 0) { fval_flush_flags(ex); }
        if (shl > 0) { x86_shift_ri(obj, w64, 4u, r, (uint8_t)shl); }
        if (shr > 0) { x86_shift_ri(obj, w64, v->bf_signed ? 7u : 5u, r, (uint8_t)shr); }
        return r;
    }
    fast_cc_bail(cc, "unexpected value kind in fval_to_gp");
    return fval_alloc_gp(ex);
}

// The loads sqlite3.c asks for (81,000 of its 130,000 requests): a constant, a local or spill slot, and an object
// through a register, none of them an array or a function, which decay. The other kinds, and sizeof's operands, which
// emit nothing, go to the rare function, whose many paths made this one save every register it might use (#3581).
#define BUSTER_FVAL_LOAD_KINDS ((1u << FVAL_IMM) | (1u << FVAL_LOCAL) | (1u << FVAL_SPILL) | (1u << FVAL_INDIRECT))

static __attribute__((noinline)) BUSTER_DIRECT_HOT uint32_t fval_to_gp_full(FastExprCtx* ex, FastVal* v, bool consume)
{
    const uint32_t kind = v->kind;
    const uint32_t shape = v->type & (BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_KIND_MASK);
    const uint32_t load = (uint32_t)((BUSTER_FVAL_LOAD_KINDS >> kind) & 1u) & (uint32_t)(ex->eval_only == 0) &
                          (uint32_t)(shape != ((uint32_t)FCTYPE_ARRAY << BUSTER_FTYPE_KIND_SH)) &
                          (uint32_t)(shape != ((uint32_t)FCTYPE_FUNC << BUSTER_FTYPE_KIND_SH));
    uint32_t r = 0;
    if (load != 0)
    {
        FastCCState* cc = ex->cc;
        FastObj* obj = cc->obj;
        const uint32_t sz = ftype_size(cc, v->type);
        const bool sgn = ftype_is_signed(cc, v->type);
        if (kind == FVAL_IMM)
        {
            r = fval_alloc_gp(ex);
            x86_mov_ri(obj, sz == 8u, r, (uint64_t)v->imm);
        }
        else if (kind == FVAL_INDIRECT)
        {
            const uint32_t base = v->reg;
            if (consume)
            {
                r = base;
                v->kind = FVAL_VOID;
            }
            else
            {
                r = fval_alloc_gp(ex);
            }
            x86_load(obj, r, base, v->off, sz, sgn);
        }
        else
        {
            r = fval_alloc_gp(ex);
            x86_load(obj, r, FREG_RBP, v->off, sz, sgn);
        }
    }
    else
    {
        r = fval_to_gp_rare(ex, v, consume);
    }
    return r;
}

// A value already in a register, taken: its register, with no code. 43,000 of sqlite3.c's 130,000 requests; the rest
// go out of line (#3581).
static inline __attribute__((always_inline)) uint32_t fval_to_gp(FastExprCtx* ex, FastVal* v, bool consume)
{
    uint32_t r = 0;
    if (((uint32_t)(v->kind == FVAL_REG) & (uint32_t)consume & (uint32_t)(ex->eval_only == 0)) != 0)
    {
        r = v->reg;
        v->kind = FVAL_VOID;
    }
    else
    {
        r = fval_to_gp_full(ex, v, consume);
    }
    return r;
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
// A path that sets the flags materializes any comparison still in them first (fval_flush_flags): a call converts its
// arguments in order, and its last one can be a comparison, whose value the flags still hold (#3659).
static __attribute__((noinline)) BUSTER_DIRECT_HOT void fval_convert_full(FastExprCtx* ex, FastVal* v, FastType dst_type)
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
    if (src_u == dst_u && !ftype_is_array(v->type) && !ftype_is_func(v->type))
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
            // A constant 0's load is `xor`, and the high half's `sar` or `xor` set the flags too.
            fval_flush_flags(ex);
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
                fval_flush_flags(ex);
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
            fval_flush_flags(ex);
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
            fval_flush_flags(ex);
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
                fval_flush_flags(ex);
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
        fval_flush_flags(ex);
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

// An integer constant converted to another integer or a pointer is folded: truncated and extended to the target's size
// and signedness, as the full function's fold does once its other cases are excluded (sqlite3.c: 10,800 a compile).
// Floats, bool, 128-bit integers, void, the unsupported types, arrays, functions and sizeof's operands go to it.
static __attribute__((noinline)) void fval_convert_constant(FastExprCtx* ex, FastVal* v, FastType dst_type)
{
    FastCCState* cc = ex->cc;
    const FastType dst_u = ftype_unqual(dst_type);
    const FastType src_u = ftype_unqual(v->type);
    const uint32_t shape = v->type & (BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_KIND_MASK);
    const uint32_t foldable = (uint32_t)!ftype_is_unsupported(dst_u) & (uint32_t)!ftype_is_unsupported(src_u) &
                              (uint32_t)!ftype_is_void(dst_u) & (uint32_t)!ftype_is_float(dst_u) & (uint32_t)!ftype_is_bool(dst_u) &
                              (uint32_t)!ftype_is_i128(dst_u) & (uint32_t)!ftype_is_i128(src_u) & (uint32_t)(ex->eval_only == 0) &
                              (uint32_t)(shape != ((uint32_t)FCTYPE_ARRAY << BUSTER_FTYPE_KIND_SH)) &
                              (uint32_t)(shape != ((uint32_t)FCTYPE_FUNC << BUSTER_FTYPE_KIND_SH));
    if (foldable != 0)
    {
        uint64_t x = (uint64_t)v->imm;
        const uint32_t dsz = ftype_size(cc, dst_u);
        const bool dsgn = ftype_is_signed(cc, dst_u);
        if (dsz == 1u) { x = dsgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
        else if (dsz == 2u) { x = dsgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
        else if (dsz == 4u) { x = dsgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
        v->imm = (int64_t)x;
        v->type = dst_u;
    }
    else
    {
        fval_convert_full(ex, v, dst_type);
    }
}

// A conversion to the value's own type changes nothing but its qualifiers: 82,000 of sqlite3.c's 116,000. Arrays and
// functions decay, and void and the unsupported types have their own paths; those, and every other pair, go to the
// full function (#3581).
static inline __attribute__((always_inline)) void fval_convert(FastExprCtx* ex, FastVal* v, FastType dst_type)
{
    const FastType dst_u = ftype_unqual(dst_type);
    const uint32_t shape = v->type & (BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_KIND_MASK);
    const uint32_t scalar = dst_u & BUSTER_FTYPE_SCALAR_MASK;
    const uint32_t complex = (uint32_t)((dst_u & BUSTER_FTYPE_COMPLEX_BIT) != 0);
    // ftype_is_unsupported and ftype_is_void, as the full function asks them of the target.
    const uint32_t supported = complex | (uint32_t)(scalar < FTYPE_UNSUP16);
    const uint32_t not_void = (uint32_t)((dst_u & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) != 0) | (uint32_t)(scalar != FTYPE_VOID);
    const uint32_t plain = (uint32_t)(ftype_unqual(v->type) == dst_u) &
                           (uint32_t)(shape != ((uint32_t)FCTYPE_ARRAY << BUSTER_FTYPE_KIND_SH)) &
                           (uint32_t)(shape != ((uint32_t)FCTYPE_FUNC << BUSTER_FTYPE_KIND_SH)) & supported & not_void;
    if (plain != 0) { v->type = dst_u; }
    else if (v->kind == FVAL_IMM) { fval_convert_constant(ex, v, dst_type); }
    else { fval_convert_full(ex, v, dst_type); }
}

// Soundness check for implicit conversions (assignment, initialization, prototype args, return).
// Bails out cleanly to the full pipeline on C semantic errors (int<->ptr, incompatible pointers) or default warnings!
static __attribute__((noinline)) void fast_cc_check_implicit_conv_full(FastCCState* cc, FastType dst_t, const FastVal* src)
{
    const FastType dt = ftype_unqual(dst_t);
    const FastType st = ftype_decay(cc, src->type);
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
        if (ftype_is_void(d0) || ftype_is_void(s0))
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
            return;
        }
        fast_cc_bail(cc, "incompatible pointer target conversion");
    }
}

// Without a pointer on either side there is nothing to check: 31,000 of sqlite3.c's 62,000 (#3581).
static inline __attribute__((always_inline)) void fast_cc_check_implicit_conv(FastCCState* cc, FastType dst_t, const FastVal* src)
{
    const FastType st = ftype_decay(cc, src->type);
    const uint32_t pointers = (uint32_t)ftype_is_ptr(dst_t) | (uint32_t)ftype_is_ptr(st);
    if (pointers != 0) { fast_cc_check_implicit_conv_full(cc, dst_t, src); }
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
    fast_cc_check_implicit_conv(cc, lhs->type, rhs);
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

// Emit binary operator `op` on `lhs` and `rhs`, returning result in `*out`. Consumes `lhs` and `rhs`!
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
        if (lhs->kind == FVAL_IMM && rhs->kind == FVAL_IMM)
        {
            const uint32_t sh = (uint32_t)rhs->imm & (w64 ? 63u : 31u);
            uint64_t v = w64 ? (uint64_t)lhs->imm : (uint32_t)lhs->imm;
            if (op == FTOK_LSHIFT) { v = w64 ? (v << sh) : (uint32_t)(v << sh); }
            else if (sgn) { v = w64 ? (uint64_t)((int64_t)v >> sh) : (uint64_t)(int64_t)((int32_t)v >> sh); }
            else { v = w64 ? (v >> sh) : ((uint32_t)v >> sh); }
            *out = (FastVal){ .type = res_t, .kind = FVAL_IMM, .imm = (int64_t)v };
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
    if (ex->eval_only > 0) { *out = (FastVal){ .type = t, .kind = FVAL_REG }; return; }
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

// __builtin_ceil, __builtin_floor and their float forms (#3592), as the full pipeline computes them, with no library
// reference: its helper (c_check_builtin_math.c's runtime ceil and floor) works on the bits, so that no flag but a
// signaling NaN's invalid is raised, a NaN is quieted (x + x), a zero, an infinity and an integer come back as they are,
// and a denormal with MXCSR's DAZ set is zero. A magnitude below 1 rounds to 0 or 1; another drops its fraction's bits
// and adds a unit where its sign says. Every live value is spilled first, so the sequence takes rax, rcx, rdx and r8 to
// r11 as it needs them; the result stays in the argument's register.
static __attribute__((cold, noinline)) void fval_emit_round_builtin(FastExprCtx* ex, bool is_double, bool is_ceil, FastVal* args, uint32_t argc, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    const FastType ft = is_double ? FTYPE_F64 : FTYPE_F32;
    if (argc != 1u || !(ftype_is_int(args[0].type) || ftype_is_float(args[0].type)) || ftype_is_unsupported(args[0].type))
    {
        fast_cc_bail(cc, "builtin ceil/floor operand");
        return;
    }
    if (ex->eval_only > 0)
    {
        fval_release(ex, &args[0]);
        *out = (FastVal){ .type = ft, .kind = FVAL_XMM };
        return;
    }
    fval_convert(ex, &args[0], ft);
    fval_spill_all(ex, ex->vsp, ex->arg_sp);
    const uint32_t xr = fval_to_xmm(ex, &args[0], true);
    const int32_t mxcsr = fast_cc_alloc_tmp(cc, 4u, 4u);
    const bool w = is_double;
    const uint64_t magnitude_mask = is_double ? UINT64_C(0x7fffffffffffffff) : UINT64_C(0x7fffffff);
    const uint64_t infinity = is_double ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000);
    const uint64_t sign_mask = is_double ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
    const uint64_t one = is_double ? UINT64_C(0x3ff0000000000000) : UINT64_C(0x3f800000);
    const uint8_t mantissa = is_double ? 52u : 23u;
    const int32_t integral = is_double ? 0x433 : 0x96;   // the exponent from which every value is an integer
    const int32_t unit_exponent = is_double ? 0x3ff : 0x7f;
    const uint32_t nan = fast_label_new(obj);
    const uint32_t same = fast_label_new(obj);
    const uint32_t zero = fast_label_new(obj);
    const uint32_t normal = fast_label_new(obj);
    const uint32_t small = fast_label_new(obj);
    const uint32_t adjust = fast_label_new(obj);
    const uint32_t signed_result = fast_label_new(obj);
    const uint32_t done = fast_label_new(obj);
    x86_mov_xmm_gpr(obj, w, false, xr, FREG_RAX);               // rax = the bits
    x86_mov_ri(obj, w, FREG_R8, magnitude_mask);
    x86_mov_rr(obj, w, FREG_RDX, FREG_RAX);
    x86_alu_rr(obj, w, 0x21u, FREG_RDX, FREG_R8);               // rdx = |x|'s bits
    x86_mov_ri(obj, w, FREG_R8, infinity);
    x86_alu_rr(obj, w, 0x39u, FREG_RDX, FREG_R8);
    x86_jcc_label(obj, FCC_A, nan);
    x86_jcc_label(obj, FCC_E, same);
    x86_alu_rr(obj, w, 0x85u, FREG_RDX, FREG_RDX);
    x86_jcc_label(obj, FCC_E, same);
    x86_mov_rr(obj, w, FREG_RCX, FREG_RDX);
    x86_shift_ri(obj, w, 5u, FREG_RCX, mantissa);               // rcx = the exponent
    x86_alu_ri(obj, false, 7u, FREG_RCX, integral);
    x86_jcc_label(obj, FCC_AE, same);
    x86_mov_ri(obj, w, FREG_R8, sign_mask);
    x86_mov_rr(obj, w, FREG_R9, FREG_RAX);
    x86_alu_rr(obj, w, 0x21u, FREG_R9, FREG_R8);                // r9 = the sign
    x86_alu_rr(obj, false, 0x85u, FREG_RCX, FREG_RCX);
    x86_jcc_label(obj, FCC_NE, normal);
    x86_u8(obj, 0x0fu);                                         // stmxcsr [rbp + mxcsr]
    x86_u8(obj, 0xaeu);
    x86_u8(obj, 0x9du);
    x86_u32(obj, (uint32_t)mxcsr);
    x86_load(obj, FREG_R8, FREG_RBP, mxcsr, 4u, false);
    x86_alu_ri(obj, false, 4u, FREG_R8, 0x40);                  // DAZ
    x86_jcc_label(obj, FCC_NE, zero);
    fast_label_bind(obj, normal);
    x86_alu_ri(obj, false, 7u, FREG_RCX, unit_exponent);
    x86_jcc_label(obj, FCC_B, small);
    x86_mov_ri(obj, false, FREG_R10, (uint64_t)(uint32_t)integral);
    x86_alu_rr(obj, false, 0x29u, FREG_R10, FREG_RCX);
    x86_mov_rr(obj, false, FREG_RCX, FREG_R10);                 // cl = the fraction's width
    x86_mov_ri(obj, w, FREG_R10, 1u);
    x86_shift_cl(obj, w, 4u, FREG_R10);                         // r10 = the unit
    x86_mov_rr(obj, w, FREG_R11, FREG_R10);
    x86_alu_ri(obj, w, 5u, FREG_R11, 1);                        // r11 = the fraction's mask
    x86_alu_rr(obj, w, 0x85u, FREG_RDX, FREG_R11);
    x86_jcc_label(obj, FCC_E, same);
    x86_f7(obj, w, 2u, FREG_R11);
    x86_alu_rr(obj, w, 0x21u, FREG_RDX, FREG_R11);              // rdx = the magnitude without its fraction
    x86_jmp_label(obj, adjust);
    fast_label_bind(obj, small);
    x86_alu_rr(obj, false, 0x31u, FREG_RDX, FREG_RDX);
    x86_mov_ri(obj, w, FREG_R10, one);
    fast_label_bind(obj, adjust);
    // ceil adds the unit to a positive value, floor to a negative one.
    x86_alu_rr(obj, w, 0x85u, FREG_R9, FREG_R9);
    x86_jcc_label(obj, is_ceil ? FCC_NE : FCC_E, signed_result);
    x86_alu_rr(obj, w, 0x01u, FREG_RDX, FREG_R10);
    fast_label_bind(obj, signed_result);
    x86_alu_rr(obj, w, 0x09u, FREG_RDX, FREG_R9);
    x86_mov_xmm_gpr(obj, w, true, xr, FREG_RDX);
    x86_jmp_label(obj, done);
    fast_label_bind(obj, zero);
    x86_mov_xmm_gpr(obj, w, true, xr, FREG_R9);
    x86_jmp_label(obj, done);
    fast_label_bind(obj, nan);
    x86_sse_rr(obj, is_double, 0x58u, xr, xr);                  // quiets a NaN
    fast_label_bind(obj, same);
    fast_label_bind(obj, done);
    *out = (FastVal){ .type = ft, .kind = FVAL_XMM, .reg = (uint8_t)xr };
}

// Emit recognized __builtin_* call (clz, ctz, bswap, popcount, expect, constant_p, unreachable, inf/nan, memcpy/memset/...)
static __attribute__((noinline)) void fval_emit_builtin_call(FastExprCtx* ex, uint32_t id, FastVal* args, uint32_t argc, FastVal* out)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;
    if (id == cc->id_builtin_ceil || id == cc->id_builtin_floor || id == cc->id_builtin_ceilf || id == cc->id_builtin_floorf)
    {
        fval_emit_round_builtin(ex, id == cc->id_builtin_ceil || id == cc->id_builtin_floor, id == cc->id_builtin_ceil || id == cc->id_builtin_ceilf, args, argc, out);
        return;
    }
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

// A call without code, in a sizeof or a dead arm (#3592), is checked as one with code is, but for its argument types: a
// dead arm of musl's isnan() of a double calls __fpclassifyl(long double). Out of line: few calls are without code.
static __attribute__((cold, noinline)) void fast_cc_check_call_without_code(FastCCState* cc, const FastComplexType* fn, const FastVal* args, uint32_t argc)
{
    if (!fn->is_oldstyle && ((!fn->is_variadic && argc != fn->count) || (fn->is_variadic && argc < fn->count)))
    {
        fast_cc_bail(cc, "call argument count mismatch");
    }
    const uint32_t checked = argc < fn->count ? argc : fn->count;
    for (uint32_t i = 0; i < checked; i += 1)
    {
        fast_cc_check_implicit_conv(cc, ftype_unqual(cc->param_types[fn->start + i]), &args[i]);
    }
}

// Emit full SysV AMD64 ABI function call (direct or indirect, 6 GP + 8 XMM + stack args, 1-16B & >16B structs, variadic!)
static BUSTER_DIRECT_HOT void fval_emit_call(FastExprCtx* ex, FastVal* callee, FastVal* args, uint32_t argc, uint32_t outer_arg_sp)
{
    FastCCState* cc = ex->cc;
    FastObj* obj = cc->obj;

    // Handle recognized compiler builtins directly!
    if (callee->kind == FVAL_BUILTIN)
    {
        const uint32_t bid = callee->sym_idx;
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
            fval_emit_builtin_call(ex, bid, args, argc, callee);
            return;
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
        *callee = (FastVal){ .type = cc->syms[si].type, .kind = FVAL_GLOBAL, .sym_idx = osym, .is_func_sym = 1u };
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

    if (ex->eval_only > 0)
    {
        fast_cc_check_call_without_code(cc, fn, args, argc);
        for (uint32_t i = 0; i < argc; i += 1) { fval_release(ex, &args[i]); }
        fval_release(ex, callee);
        *callee = (FastVal){ .type = ret_t, .kind = ftype_is_void(ret_t) ? FVAL_VOID : FVAL_REG };
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
    if (!fn->is_oldstyle && ((!fn->is_variadic && argc != fn->count) || (fn->is_variadic && argc < fn->count)))
    {
        fast_cc_bail(cc, "call argument count mismatch");
        return;
    }
    const bool is_var = fn->is_variadic != 0 || fn->is_oldstyle != 0;
    for (uint32_t i = 0; i < argc; i += 1)
    {
        FastType pt = FTYPE_VOID;
        if (i < fn->count)
        {
            pt = ftype_unqual(cc->param_types[fn->start + i]);
            fast_cc_check_implicit_conv(cc, pt, &args[i]);
            if (cc->bailout) { return; }
        }
        else
        {
            // Default argument promotion for variadic / old-style args
            FastType at = ftype_decay(cc, args[i].type);
            if (ftype_is_float(at) && !ftype_is_f64(at)) { pt = FTYPE_F64; }
            else if (ftype_is_int(at) && ftype_size(cc, at) < 4u) { pt = FTYPE_I32; }
            else { pt = ftype_unqual(at); }
        }
        if (ftype_is_unsupported(pt) || ftype_is_unsupported(args[i].type))
        {
            fast_cc_bail(cc, "unsupported call arg type");
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
        *callee = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
    }
    else if (ftype_is_record(ret_t))
    {
        const FastComplexType* rct = cc->ctypes + (ret_t & BUSTER_FTYPE_CIDX_MASK);
        if (has_sret)
        {
            *callee = (FastVal){ .type = ret_t, .kind = FVAL_LOCAL, .off = sret_slot };
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
            *callee = (FastVal){ .type = ret_t, .kind = FVAL_LOCAL, .off = rslot };
        }
    }
    else if (ftype_is_float(ret_t))
    {
        ex->xmm_used |= 1u; // XMM0
        *callee = (FastVal){ .type = ret_t, .kind = FVAL_XMM, .reg = 0u };
    }
    else
    {
        // Ensure narrow signed/unsigned return values are properly extended in RAX!
        const uint32_t rsz = ftype_size(cc, ret_t);
        if (rsz < 4u) { x86_extend(obj, FREG_RAX, FREG_RAX, rsz, ftype_is_signed(cc, ret_t)); }
        ex->gp_used |= (1u << FREG_RAX);
        *callee = (FastVal){ .type = ret_t, .kind = FVAL_REG, .reg = FREG_RAX };
    }
}

// While the call is emitted, its callee sits just outside the live value stack.
// The returned value occupies that same slot.
static inline void fast_expr_finish_call(FastExprCtx* ex, uint32_t first_arg, uint32_t argc)
{
    FastVal* callee = &ex->vstack[--ex->vsp];
    fval_emit_call(ex, callee, ex->call_args + first_arg, argc, first_arg);
    ex->arg_sp = first_arg;
    ex->vsp += 1u;
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

// What a condition folded to (#3592), in a `?:`'s FastExprOp.extra and in FastCCState.branch_constant. The dead side of
// a constant condition is checked as an operand of sizeof is, with no code and no symbol.
#define BUSTER_FCONSTANT_NONE 0u
#define BUSTER_FCONSTANT_FALSE 1u
#define BUSTER_FCONSTANT_TRUE 2u

typedef struct FastExprOp FastExprOp;
struct FastExprOp
{
    uint16_t op;
    uint16_t extra;        // arg_count (CALL), elem_idx (COMPOUND_LIT), BUSTER_FCONSTANT_* (TERNARY_Q, TERNARY_C), decided (&&, ||)
    uint32_t lbl0;         // false/true/else_lbl, or arg_start (CALL), or end_pos (GENERIC_SEL)
    uint32_t lbl1;         // then_tail_lbl (TERNARY_C)
    uint32_t lbl2;         // end_lbl (TERNARY_C)
    FastType type;         // cast target, then_type (TERNARY_C), compound lit type
    int32_t slot;          // tern_slot or compound lit stack slot
};

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
static __attribute__((cold, noinline)) void fast_expr_constant_ternary(FastExprCtx* ex, uint32_t constant, FastVal else_v, FastType common_t)
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
    FastVal result = { .type = common_t, .kind = FVAL_VOID };
    if (ok && ftype_is_void(common_t))
    {
        fval_release(ex, &live);
    }
    else if (ok && ftype_is_record(common_t))
    {
        const FastComplexType* rct = cc->ctypes + (common_t & BUSTER_FTYPE_CIDX_MASK);
        result.kind = FVAL_LOCAL;
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
    }
    if (ok) { ex->vstack[ex->vsp++] = result; }
}

// A left side of `&&` or `||` that decided the value (#3592): the dead right side must still be a scalar, and the value
// is 0 for `&&` and 1 for `||`, an rvalue as the branches in fast_expr_reduce_top make it. Out of line, as above.
static __attribute__((cold, noinline)) void fast_expr_decided_logic(FastExprCtx* ex, uint8_t tok, const FastVal* rhs)
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
        ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = reg };
    }
}

// Apply the top operator from `ops[--osp]` onto `ex->vstack`!
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
        if (op == FEOP_SIZEOF_UNARY || op == FEOP_ALIGNOF_UNARY)
        {
            const uint64_t val = op == FEOP_SIZEOF_UNARY ? ftype_size(cc, v->type) : ftype_align(cc, v->type);
            if (ex->eval_only > 0) { ex->eval_only -= 1u; }
            *v = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)val };
            return;
        }
        if (op == FEOP_CAST)
        {
            fval_convert(ex, v, cur.type);
            v->is_lvalue = 0;
            return;
        }
        if (op == FEOP_ADDR)
        {
            if (v->kind == FVAL_BITFIELD) { fast_cc_bail(cc, "address of bitfield"); return; }
            const FastType pt = ftype_make_ptr(cc, v->type);
            if (ex->eval_only > 0) { *v = (FastVal){ .type = pt, .kind = FVAL_REG }; return; }
            if (v->kind == FVAL_LOCAL) { v->kind = FVAL_LOCAL_ADDR; v->type = pt; v->is_lvalue = 0; return; }
            if (v->kind == FVAL_GLOBAL) { v->kind = FVAL_GLOBAL_ADDR; v->type = pt; v->is_lvalue = 0; return; }
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
            FastVal res = { 0 };
            fval_emit_incdec(ex, v, op == FEOP_PRE_INC, false, &res);
            *v = res;
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
                v->imm = w64 ? -v->imm : (ftype_is_signed(cc, pt) ? (int64_t)(-(int32_t)v->imm) : (int64_t)(uint64_t)(-(uint32_t)v->imm));
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
            if (ex->eval_only > 0) { *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG }; return; }
            if (v->kind == FVAL_FLAGS)
            {
                v->cc_code ^= 1u; // invert condition code in 0 instructions!!
                v->type = FTYPE_I32;
                return;
            }
            if (v->kind == FVAL_IMM)
            {
                *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = v->imm == 0 ? 1 : 0 };
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
                *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = (uint8_t)r1 };
                return;
            }
            const uint32_t r = fval_to_gp(ex, v, true);
            const uint32_t sz = ftype_size(cc, t);
            if (sz < 4u) { x86_extend(obj, r, r, sz, false); }
            x86_alu_rr(obj, sz == 8u, 0x85u, r, r);
            ex->gp_used &= (uint16_t)~(1u << r);
            *v = (FastVal){ .type = FTYPE_I32, .kind = FVAL_FLAGS, .cc_code = FCC_E };
            return;
        }
        return;
    }

    // Ternary `?:` completion (`FEOP_TERNARY_C`)
    if (op == FEOP_TERNARY_C)
    {
        if (ex->vsp < 1u) { fast_cc_bail(cc, "ternary stack underflow"); return; }
        FastVal else_v = ex->vstack[--ex->vsp];
        const FastType then_t = cur.type;
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

        if (cur.extra != BUSTER_FCONSTANT_NONE)
        {
            fast_expr_constant_ternary(ex, cur.extra, else_v, common_t);
            return;
        }

        if (ex->eval_only > 0)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = ftype_is_void(common_t) ? FVAL_VOID : FVAL_REG };
            return;
        }
        if (ftype_is_void(common_t))
        {
            fval_release(ex, &else_v);
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            fast_label_bind(obj, cur.lbl2);
            ex->gp_used = 0;
            ex->xmm_used = 0;
            ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
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
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_LOCAL, .off = cur.slot };
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
            if (ftype_unqual(then_t) != ftype_unqual(common_t))
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
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_XMM, .reg = 0u };
        }
        else
        {
            const uint32_t g = fval_to_gp(ex, &else_v, true);
            if (g != FREG_RAX) { x86_mov_rr(obj, true, FREG_RAX, g); }
            x86_jmp_label(obj, cur.lbl2);
            fast_label_bind(obj, cur.lbl1);
            if (ftype_unqual(then_t) != ftype_unqual(common_t))
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
            ex->vstack[ex->vsp++] = (FastVal){ .type = common_t, .kind = FVAL_REG, .reg = FREG_RAX };
        }
        return;
    }

    // Binary / logical / assignment / comma operators
    const uint8_t tok = (uint8_t)(op - FEOP_TOK_BASE);
    if (tok == FTOK_LAND || tok == FTOK_LOR)
    {
        if (ex->vsp < 1u) { fast_cc_bail(cc, "logical stack underflow"); return; }
        FastVal rhs = ex->vstack[--ex->vsp];
        if (cur.extra != 0)
        {
            fast_expr_decided_logic(ex, tok, &rhs);
            return;
        }
        if (ex->eval_only > 0)
        {
            ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG };
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
        ex->vstack[ex->vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_REG, .reg = FREG_RAX };
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
        fval_release(ex, lhs);
        ex->vstack[ex->vsp++] = *rhs;
        return;
    }
    if (tok == FTOK_ASSIGN)
    {
        if (!lhs->is_lvalue || ftype_is_const(lhs->type))
        {
            fast_cc_bail(cc, "assign to const or non-lvalue");
            return;
        }
        FastVal res = { 0 };
        fval_store(ex, lhs, rhs, true, &res);
        ex->vstack[ex->vsp++] = res;
        return;
    }
    if (tok >= FTOK_ADD_ASSIGN && tok <= FTOK_SHR_ASSIGN)
    {
        FastVal res = { 0 };
        fval_emit_compound_assign(ex, tok, lhs, rhs, &res);
        ex->vstack[ex->vsp++] = res;
        return;
    }
    // The result where the left operand stands, which is where it goes. fval_emit_binary writes it once, as the last
    // thing it does, or bails out. A result in a local of this function was 40 cycles of waiting: the local is zeroed
    // through the stack pointer, written through fval_emit_binary's pointer and loaded through the stack pointer,
    // and the load is paired with the wrong store (#2850's profile on the 9700X: half of this function's cycles).
    fval_emit_binary(ex, tok, lhs, rhs, lhs);
    ex->vsp += 1u;
}

// Main iterative C17 expression compiler (ZERO recursion!).
// If `is_branch` is true, emits a direct branch to `branch_label` and releases the result.
// glibc's assert in GNU's modes (#3592) is a statement expression:
//     ((void) sizeof ((e) ? 1 : 0), __extension__ ({ if (e) ; else __assert_fail (#e, __FILE__, __LINE__, f); }))
// and SQLite's ALWAYS and NEVER put one in an expression. The direct path compiles no statement in an expression, so
// at `__extension__ ({`, with `start` at the __extension__, this shape alone is rewritten in place as the conditional
// expression it computes, of as many tokens:
//     __extension__ ((e) ? (void) 0 : __assert_fail (#e, __FILE__, __LINE__, f))
// e is evaluated once in both, the call only where it is false, and the value is void in both, as __assert_fail's is.
// Another statement expression keeps its tokens, and leaves the direct path at its `{`.
static __attribute__((cold, noinline)) void fast_cc_assert_rewrite(FastCCState* cc, uint32_t start)
{
    uint8_t* const tk = (uint8_t*)cc->tk;
    uint32_t* const td = (uint32_t*)cc->td;
    // The matching closer of the opener at `at`, or UINT32_MAX.
    uint32_t closes[2] = { UINT32_MAX, UINT32_MAX };
    const uint32_t opens[2] = { start + 4u, 0 };
    bool shape = start + 4u < cc->tcount && tk[start + 3u] == FTOK_KW_IF && tk[start + 4u] == FTOK_LPAREN;
    for (uint32_t which = 0; which < 2u && shape; which += 1)
    {
        const uint32_t open = which == 0 ? opens[0] : closes[0] + 4u;
        uint32_t depth = 0;
        for (uint32_t i = open; i < cc->tcount; i += 1)
        {
            const uint8_t k = tk[i];
            depth += (uint32_t)(k == FTOK_LPAREN || k == FTOK_LBRACKET || k == FTOK_LBRACE);
            depth -= (uint32_t)(k == FTOK_RPAREN || k == FTOK_RBRACKET || k == FTOK_RBRACE);
            if (depth == 0)
            {
                closes[which] = i;
                break;
            }
        }
        shape = closes[which] != UINT32_MAX && tk[closes[which]] == FTOK_RPAREN;
        if (shape && which == 0)
        {
            const uint32_t c = closes[0];
            shape = c + 4u < cc->tcount && tk[c + 1u] == FTOK_SEMI && tk[c + 2u] == FTOK_KW_ELSE && tk[c + 3u] == FTOK_IDENT &&
                    td[c + 3u] == cc->id_assert_fail && tk[c + 4u] == FTOK_LPAREN;
        }
    }
    const uint32_t call_end = closes[1] + 1u;
    shape = shape && call_end + 2u < cc->tcount && tk[call_end] == FTOK_SEMI && tk[call_end + 1u] == FTOK_RBRACE && tk[call_end + 2u] == FTOK_RPAREN;
    const uint32_t end = call_end + 3u;
    uint8_t* kinds = shape ? (uint8_t*)fast_arena_alloc(cc->scratch, end - start, 1) : NULL;
    uint32_t* datas = kinds != NULL ? (uint32_t*)fast_arena_alloc(cc->scratch, (uint64_t)(end - start) * sizeof(uint32_t), 4) : NULL;
    if (datas != NULL)
    {
        // `( ( e ) ? ( void ) 0 : call )` after the __extension__, built aside and copied over the span.
        const uint32_t cond_first = start + 5u;
        const uint32_t cond_end = closes[0];
        const uint32_t call_first = closes[0] + 3u;
        uint32_t n = 0;
        kinds[n] = FTOK_LPAREN; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_LPAREN; datas[n] = 0; n += 1u;
        for (uint32_t i = cond_first; i < cond_end; i += 1) { kinds[n] = tk[i]; datas[n] = td[i]; n += 1u; }
        kinds[n] = FTOK_RPAREN; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_QUESTION; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_LPAREN; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_KW_VOID; datas[n] = cc->id_void; n += 1u;
        kinds[n] = FTOK_RPAREN; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_INT_LIT; datas[n] = 0; n += 1u;
        kinds[n] = FTOK_COLON; datas[n] = 0; n += 1u;
        for (uint32_t i = call_first; i < call_end; i += 1) { kinds[n] = tk[i]; datas[n] = td[i]; n += 1u; }
        kinds[n] = FTOK_RPAREN; datas[n] = 0; n += 1u;
        if (n == end - start - 1u)
        {
            memcpy(tk + start + 1u, kinds, n);
            memcpy(td + start + 1u, datas, (uint64_t)n * sizeof(uint32_t));
        }
    }
}

static BUSTER_DIRECT_HOT void fast_cc_expr(FastCCState* cc, bool stop_at_comma,
                         bool is_branch, bool branch_on_true, uint32_t branch_label, FastVal* out_val)
{
    // Rewrites change token contents in place. The buffers and their length stay fixed.
    const uint8_t* const kinds = cc->tk;
    const uint32_t* const data = cc->td;
    const uint32_t token_count = cc->tcount;
    FastExprCtx ex;
    ex.cc = cc;
    ex.vsp = 0;
    ex.arg_sp = 0;
    ex.gp_used = 0;
    ex.xmm_used = 0;
    ex.eval_only = (uint8_t)(cc->dead != 0);

    FastExprOp ops[128];
    uint32_t osp = 0;
    bool expect_val = true;

    while (cc->pos < token_count && !cc->bailout)
    {
        const uint8_t k = kinds[cc->pos];
        if (expect_val)
        {
            // HOT PATH #1 (>60% of expect_val tokens): Identifier (variable, enum constant, function, or recognized builtin)
            if (__builtin_expect(k == FTOK_IDENT, 1))
            {
                const uint32_t id = data[cc->pos];
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
                    const uint32_t symbol_kind = sym->kind & 7u;
                    if (__builtin_expect((sym->is_unsupported != 0) | (symbol_kind < FSYM_ENUM_CONST) | (symbol_kind > FSYM_FUNC), 0))
                    {
                        fast_cc_bail(cc, sym->is_unsupported != 0 ? "use of unsupported-type symbol" : "unexpected symbol kind in expr");
                        break;
                    }
                    const uint32_t emitted = (uint32_t)(symbol_kind >= FSYM_GLOBAL) & (uint32_t)(ex.eval_only == 0);
                    if (emitted) { fast_cc_reference(cc, sym); }
                    uint32_t osym = sym->obj_sym & (0u - emitted);
                    if (__builtin_expect((emitted != 0) & (osym == 0), 0))
                    {
                        osym = fast_cc_ensure_obj_sym(cc, si);
                    }
                    if (ex.vsp < 128u)
                    {
                        ex.vstack[ex.vsp++] = (FastVal){
                            .type = sym->type,
                            .kind = value_kinds[symbol_kind],
                            .imm = (sym->const_val & (0 - (int64_t)(symbol_kind == FSYM_ENUM_CONST))) |
                                   (uint32_t)(sym->stack_off & (0 - (int32_t)(symbol_kind == FSYM_LOCAL))),
                            .sym_idx = osym,
                            .is_func_sym = (uint8_t)(symbol_kind == FSYM_FUNC),
                            .is_lvalue = value_lvalues[symbol_kind],
                        };
                    }
                    expect_val = false;
                    continue;
                }
                if (id == cc->id_builtin_clzll || id == cc->id_builtin_clz || id == cc->id_builtin_ctzll ||
                    id == cc->id_builtin_ctz || id == cc->id_builtin_inff || id == cc->id_builtin_inf ||
                    id == cc->id_builtin_huge_valf || id == cc->id_builtin_huge_val ||
                    id == cc->id_builtin_ceil || id == cc->id_builtin_floor || id == cc->id_builtin_ceilf || id == cc->id_builtin_floorf ||
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
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = (int64_t)(int32_t)data[cc->pos] }; }
                cc->pos += 1u; expect_val = false; continue;
            }

            if (k == FTOK_KW_EXTENSION)
            {
                if (cc->pos + 2u < cc->tcount && cc->tk[cc->pos + 1u] == FTOK_LPAREN && cc->tk[cc->pos + 2u] == FTOK_LBRACE)
                {
                    fast_cc_assert_rewrite(cc, cc->pos);
                }
                cc->pos += 1u;
                continue;
            }

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
                        cc->pos += 1u; // '{'
                        const uint32_t csz = ftype_size(cc, ct);
                        const uint32_t cal = ftype_align(cc, ct);
                        const int32_t slot = ex.eval_only > 0 ? 0 : fast_cc_alloc_tmp(cc, csz, cal < 8u ? 8u : cal);
                        if (ex.eval_only == 0)
                        {
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
                        ops[osp++] = (FastExprOp){ .op = FEOP_COMPOUND_LIT, .extra = 0u, .type = ct, .slot = slot };
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
                            if (fast_cc_peek(cc) == FTOK_ASSIGN) { cc->pos += 1u; }
                        }
                        continue;
                    }
                    if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                    ops[osp++] = (FastExprOp){ .op = FEOP_CAST, .type = ct };
                    continue;
                }
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                ops[osp++] = (FastExprOp){ .op = FEOP_LPAREN };
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
                    const uint64_t val = is_al ? ftype_align(cc, st) : ftype_size(cc, st);
                    if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_U64, .kind = FVAL_IMM, .imm = (int64_t)val }; }
                    expect_val = false;
                    continue;
                }
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                ops[osp++] = (FastExprOp){ .op = is_al ? FEOP_ALIGNOF_UNARY : FEOP_SIZEOF_UNARY };
                ex.eval_only += 1u;
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
                cc->pos += 1u;
                if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "bad _Generic"); break; }
                cc->pos += 1u;
                if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
                ops[osp++] = (FastExprOp){ .op = FEOP_GENERIC_CTRL };
                ex.eval_only += 1u;
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
                ops[osp++] = (FastExprOp){ .op = vop };
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
                if (ex.vsp < 128u) { ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_I32, .kind = FVAL_IMM, .imm = (int64_t)(int32_t)fast_cc_data(cc) }; }
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
                const char* sp;
                uint32_t slen = 0;
                if (k == FTOK_STR_LIT)
                {
                    sp = fast_cc_parse_string_lit(cc, &slen);
                }
                else if (fast_cc_data(cc) == cc->id_pretty_function)
                {
                    // __PRETTY_FUNCTION__ is the function's signature in C, as clang prints it (#3618, #3592); a
                    // definition it can't print so leaves the direct path.
                    sp = fast_cc_pretty_function(cc, &slen);
                    cc->pos += 1u;
                    if (cc->bailout) { break; }
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
                fast_expr_finish_call(&ex, cop.lbl0, 0u);
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
            if (ex.eval_only > 0)
            {
                *v = (FastVal){ .type = mi->type, .kind = FVAL_INDIRECT, .is_lvalue = 1u };
                continue;
            }
            if (is_arrow)
            {
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
            FastVal res = { 0 };
            fval_emit_incdec(&ex, &ex.vstack[ex.vsp - 1u], k == FTOK_INC, true, &res);
            ex.vstack[ex.vsp - 1u] = res;
            continue;
        }

        // Postfix `[` subscript
        if (k == FTOK_LBRACKET)
        {
            if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
            ops[osp++] = (FastExprOp){ .op = FEOP_SUBSCRIPT };
            cc->pos += 1u;
            expect_val = true;
            continue;
        }

        // Postfix `(` function call
        if (k == FTOK_LPAREN)
        {
            if (osp >= 128u) { fast_cc_bail(cc, "expr op stack full"); break; }
            ops[osp++] = (FastExprOp){ .op = FEOP_CALL, .extra = 0u, .lbl0 = ex.arg_sp };
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
            const FastExprOp cl = ops[--osp];
            cc->pos += 1u;
            if (ex.vsp >= 1u)
            {
                FastVal elem_v = ex.vstack[--ex.vsp];
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
                fast_expr_finish_call(&ex, cop.lbl0, argc);
                continue;
            }
            if (top == FEOP_VA_END)
            {
                osp -= 1u; cc->pos += 1u;
                if (ex.vsp >= 1u) { fval_release(&ex, &ex.vstack[ex.vsp - 1u]); ex.vstack[ex.vsp - 1u] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID }; }
                continue;
            }
            if (top == FEOP_VA_COPY)
            {
                osp -= 1u; cc->pos += 1u;
                if (ex.vsp < 2u) { fast_cc_bail(cc, "va_copy args"); break; }
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
                ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
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
            const uint32_t constant = ops[osp - 1u].extra;
            if (constant != BUSTER_FCONSTANT_NONE)
            {
                // A live then arm's value stays on the stack, under the dead else arm's; a dead then arm's goes.
                const FastType arm_t = ftype_decay(cc, ex.vstack[ex.vsp - 1u].type);
                ex.vsp -= (uint32_t)(constant == BUSTER_FCONSTANT_FALSE);
                ex.eval_only = (uint8_t)(ex.eval_only + (constant == BUSTER_FCONSTANT_TRUE) - (constant == BUSTER_FCONSTANT_FALSE));
                ops[osp - 1u] = (FastExprOp){ .op = FEOP_TERNARY_C, .extra = (uint16_t)constant, .type = arm_t };
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
            ops[osp - 1u] = (FastExprOp){
                .op = FEOP_TERNARY_C, .lbl1 = then_tail_lbl, .lbl2 = end_lbl, .type = then_t, .slot = tern_slot
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
                if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + FTOK_COMMA) }; }
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
                    if (ex.eval_only == 0)
                    {
                        if (ftype_is_array(cl->type))
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
                cc->pos += 1u;
                if (fast_cc_peek(cc) == FTOK_DOT && ftype_is_record(cl->type))
                {
                    cc->pos += 1u;
                    const FastComplexType* rec = cc->ctypes + (cl->type & BUSTER_FTYPE_CIDX_MASK);
                    const FastMemberInfo* mi = fast_cc_peek(cc) == FTOK_IDENT ? fast_struct_find_member(cc, cl->type & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc)) : NULL;
                    if (mi == NULL) { fast_cc_bail(cc, "compound lit .field"); break; }
                    cl->extra = (uint16_t)(mi - (cc->member_info + rec->start));
                    cc->pos += 1u;
                    if (fast_cc_peek(cc) == FTOK_ASSIGN) { cc->pos += 1u; }
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
                osp -= 1u;
                cc->pos += 1u; // ','
                uint32_t match_pos = UINT32_MAX;
                uint32_t def_pos = UINT32_MAX;
                uint32_t end_pos = UINT32_MAX;
                while (cc->pos < token_count && !cc->bailout)
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
                    while (cc->pos < token_count)
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
                ops[osp++] = (FastExprOp){ .op = FEOP_GENERIC_SEL, .lbl0 = end_pos };
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
                osp -= 1u;
                cc->pos += 1u; // ','
                // Skip second argument up to ')'
                uint32_t bd = 0;
                while (cc->pos < token_count)
                {
                    const uint8_t ak = fast_cc_peek(cc);
                    if (bd == 0 && ak == FTOK_RPAREN) { cc->pos += 1u; break; }
                    if (ak == FTOK_LPAREN) { bd += 1u; } else if (ak == FTOK_RPAREN && bd > 0) { bd -= 1u; }
                    cc->pos += 1u;
                }
                if (ex.vsp < 1u) { fast_cc_bail(cc, "va_start ap"); break; }
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
                ex.vstack[ex.vsp++] = (FastVal){ .type = FTYPE_VOID, .kind = FVAL_VOID };
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
            if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + FTOK_COMMA) }; }
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
            ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + k), .extra = (uint16_t)decided, .lbl0 = sc_lbl };
        }
        else if (k == FTOK_QUESTION)
        {
            if (ex.vsp < 1u) { fast_cc_bail(cc, "ternary cond missing"); break; }
            if (osp >= 128u) { fast_cc_bail(cc, "ternary nesting depth"); break; }
            FastVal cond = ex.vstack[--ex.vsp];
            uint32_t else_lbl = 0;
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
            ops[osp++] = (FastExprOp){ .op = FEOP_TERNARY_Q, .extra = (uint16_t)constant, .lbl0 = else_lbl };
        }
        else
        {
            if (osp < 128u) { ops[osp++] = (FastExprOp){ .op = (uint16_t)(FEOP_TOK_BASE + k) }; }
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
    if (is_branch)
    {
        ex.vsp = 0;
        cc->branch_constant = (uint8_t)((uint32_t)(ex.vstack[0].kind == FVAL_IMM) * (BUSTER_FCONSTANT_FALSE + (uint32_t)(ex.vstack[0].imm != 0)));
        fval_branch(&ex, &ex.vstack[0], branch_on_true, branch_label);
    }
    else if (out_val != NULL)
    {
        *out_val = ex.vstack[0];
    }
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
}

static void fast_cc_store_expr_to_val(FastCCState* cc, FastVal dst, bool stop_at_comma)
{
    FastVal rhs = { 0 };
    fast_cc_expr(cc, stop_at_comma, false, false, 0, &rhs);
    if (cc->bailout) { return; }
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
            while (fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                if (fast_cc_peek(cc) == FTOK_DOT)
                {
                    cc->pos += 1u;
                    if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "local .designator"); break; }
                    const FastComplexType* rec = cc->ctypes + (cur_t & BUSTER_FTYPE_CIDX_MASK);
                    const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
                    if (mi == NULL) { fast_cc_bail(cc, "unknown local .designator"); break; }
                    if (cur_t == f->type) { f->idx = (uint32_t)(mi - (cc->member_info + rec->start)) + 1u; }
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
                    if (cur_t == f->type) { f->idx = (uint32_t)didx + 1u; }
                    cur_off += (int32_t)((uint32_t)didx * ftype_size(cc, ar->inner));
                    cur_t = ar->inner;
                    cur_bf = NULL;
                }
            }
            if (fast_cc_peek(cc) == FTOK_ASSIGN) { cc->pos += 1u; }
            if (cc->bailout) { break; }
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
                f->idx += 1u;
                // Skip flattened anonymous-union siblings sharing the same byte_offset in a struct!
                if (rec->kind == FCTYPE_STRUCT && mi->bit_width == 0)
                {
                    while (f->idx < rec->count && cc->member_info[rec->start + f->idx].byte_offset == mi->byte_offset &&
                           cc->member_info[rec->start + f->idx].bit_width == 0)
                    {
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
                if (si != 0 && ftype_unqual(cc->syms[si].type) == ftype_unqual(elem_t)) { is_struct_copy = true; }
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
};

#define BUSTER_FSWITCH_BUCKET_BITS 11u

// The statement frames a function body may nest (#3592): SQLite's shell.c's do_meta_command is an `else if` chain of 72
// branches, a frame each.
#define BUSTER_FSTMT_FRAMES 256u


// An `if` with a constant condition (#3592): its dead branch is checked as an operand of sizeof is, with no code, when
// fast_cc_dead_simple takes it. FastCCState.dead counts the branches being checked so around a statement.
#define BUSTER_FDEAD_NONE 0u
#define BUSTER_FDEAD_THEN 1u       // a false condition: the then branch is checked without code
#define BUSTER_FDEAD_TRUE 2u       // a true condition: an else branch would be dead
#define BUSTER_FDEAD_ELSE 3u       // the else branch of a true condition is checked without code

// The end of an expression's tokens, at `terminator` outside brackets, or UINT32_MAX where the tokens hold a statement
// expression, which the statement compiler would compile, or end otherwise. glibc's assert's is rewritten first, as
// the expression compiler would rewrite it (fast_cc_assert_rewrite).
static __attribute__((cold, noinline)) uint32_t fast_cc_dead_expression_end(FastCCState* cc, uint32_t pos, uint8_t terminator)
{
    uint32_t depth = 0;
    uint32_t end = UINT32_MAX;
    bool ok = true;
    for (uint32_t i = pos; i < cc->tcount && ok && end == UINT32_MAX; i += 1)
    {
        if (cc->tk[i] == FTOK_LBRACE && i >= 2u && cc->tk[i - 1u] == FTOK_LPAREN && cc->tk[i - 2u] == FTOK_KW_EXTENSION)
        {
            fast_cc_assert_rewrite(cc, i - 2u);
        }
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

static __attribute__((cold, noinline)) bool fast_cc_dead_simple(FastCCState* cc, uint32_t pos)
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
    uint32_t text_off;
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
};

// Label names are already interned when preprocessing finishes. The opening brace's token position
// identifies the function, including deferred bodies, so a new function needs no table clear.
static uint32_t fast_cc_get_user_label(FastCCState* cc, uint32_t ident_id)
{
    if (cc->user_labels == NULL)
    {
        const uint32_t count = cc->pp->idents.count + 1u;
        cc->user_labels = (FastUserLabel*)fast_arena_alloc(cc->arena, (uint64_t)count * sizeof(FastUserLabel), 8);
    }
    uint32_t label_id = UINT32_MAX;
    if (cc->user_labels != NULL)
    {
        FastUserLabel* entry = cc->user_labels + ident_id;
        if (entry->body_pos != cc->cur_func_body_pos)
        {
            entry->body_pos = cc->cur_func_body_pos;
            entry->label_id = fast_label_new(cc->obj);
        }
        label_id = entry->label_id;
    }
    else
    {
        fast_cc_bail(cc, "label table full");
    }
    return label_id;
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
        uint32_t def_text_off = obj->labels[def_lbl];

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

        if (def_text_off == UINT32_MAX)
        {
            def_text_off = obj->text.len;
            x86_jmp_label(obj, def_lbl);
        }

        // Fill table targets (default everywhere, then overwrite present cases)
        uint32_t targets[1024];
        for (uint32_t k = 0; k < span; k += 1) { targets[k] = def_text_off; }
        for (uint32_t i = 0; i < n; i += 1)
        {
            const uint32_t idx = (uint32_t)((uint64_t)cases[i].val - (uint64_t)min_v);
            targets[idx] = cases[i].text_off;
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
        x86_jcc_offset(obj, FCC_E, cases[i].text_off);
    }
    x86_jmp_label(obj, def_lbl);
}

// Compile one complete function body starting at `cc->pos` == '{'!
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
            fast_cc_expr(cc, false, false, false, 0, &value);
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
        fast_cc_expr(cc, false, false, false, 0, &value);
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
    cc->cur_func_body_pos = cc->pos;
    cc->cur_func_pretty = NULL;
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

    const uint32_t file_sym_count = cc->sym_count;
    const uint32_t func_scope_mark = fast_cc_scope_enter(cc);

    // 4. SysV AMD64 parameter homing
    static const uint8_t abi_gp[6] = { FREG_RDI, FREG_RSI, FREG_RDX, FREG_RCX, FREG_R8, FREG_R9 };
    uint32_t gp_idx = 0;
    uint32_t fp_idx = 0;
    int32_t stk_off = 16; // first stack parameter at [rbp + 16]

    // Hidden sret pointer for >16B struct return
    if (ftype_is_record(ret_t) && cc->ctypes[ret_t & BUSTER_FTYPE_CIDX_MASK].abi_lo == FABI_MEM)
    {
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

    // Home each parameter into `[rbp + off]` and bind in function scope
    const uint32_t pcnt = fn->count;
    for (uint32_t i = 0; i < pcnt && !cc->bailout; i += 1)
    {
        const FastType pt = cc->param_types[fn->start + i];
        if (ftype_is_unsupported(pt)) { fast_cc_bail(cc, "unsupported param type"); break; }
        const uint32_t pname = i < param_count ? cc->param_names[param_names_start + i] : 0u;
        int32_t slot = 0;

        if (ftype_is_record(pt))
        {
            const FastComplexType* rct = cc->ctypes + (pt & BUSTER_FTYPE_CIDX_MASK);
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
            fast_cc_sym_bind(cc, pname, (FastSym){ .kind = FSYM_LOCAL, .type = pt, .stack_off = slot });
        }
    }
    cc->cur_va_gp_init = gp_idx * 8u;
    cc->cur_va_fp_init = 48u + fp_idx * 16u;
    cc->cur_va_overflow_off = stk_off;

    // 5. Iterative statement execution state machine!
    FastStmtFrame sstack[BUSTER_FSTMT_FRAMES];
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
    bool saw_return_or_exit = false;

    if (sw_cases == NULL)
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
                    continue;
                }
                fast_label_bind(obj, top->lbl0);
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_IF_ELSE)
            {
                cc->dead -= (uint32_t)(top->dead == BUSTER_FDEAD_ELSE);
                fast_label_bind(obj, top->lbl0);
                ssp -= 1u;
                continue;
            }
            if (top->kind == FSTK_WHILE)
            {
                x86_jmp_label(obj, top->lbl0); // cond_lbl
                fast_label_bind(obj, top->lbl1); // break_lbl
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
                fast_cc_expr(cc, false, true, true, top->lbl0, NULL); // jump back to body_lbl if true!
                fast_cc_reset_tmps(cc);
                if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) in do-while"); break; }
                cc->pos += 1u;
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
                fast_label_bind(obj, top->lbl2); // break_lbl
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
                    fast_cc_expr(cc, false, false, false, 0, NULL);
                    fast_cc_reset_tmps(cc);
                    cc->pos = save_after;
                }
                x86_jmp_label(obj, top->lbl0); // cond_lbl
                fast_label_bind(obj, top->lbl2); // break_lbl
                fast_cc_scope_leave(cc, top->scope_mark);
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
                fast_cc_emit_switch_dispatch(cc, sw, sw_cases + sw->case_start);
                fast_label_bind(obj, top->lbl1); // break_lbl
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

        // The statement's first token, read once: most statements have no label, and the tests below ask for it four
        // times (#3581).
        const uint32_t first_pos = cc->pos;
        const uint8_t first = fast_cc_peek(cc);

        // Check closing '}' of active FSTK_BLOCK!
        if (top->kind == FSTK_BLOCK && first == FTOK_RBRACE)
        {
            cc->pos += 1u;
            fast_cc_scope_leave(cc, top->scope_mark);
            ssp -= 1u;
            continue;
        }

        // Consume any leading `case expr :`, `default :`, or `label :` prefixes!
        while (!cc->bailout)
        {
            const uint8_t pk = cc->pos == first_pos ? first : fast_cc_peek(cc);
            if (pk == FTOK_KW_CASE)
            {
                if (sw_sp == 0) { fast_cc_bail(cc, "case outside switch"); break; }
                cc->pos += 1u;
                const int64_t cv = fast_cc_const_int_expr(cc, false);
                if (fast_cc_peek(cc) != FTOK_COLON) { fast_cc_bail(cc, "expected : after case"); break; }
                cc->pos += 1u;
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
                        .text_off = obj->text.len,
                        .previous = head,
                        .bucket = bucket,
                    };
                    case_heads[bucket] = (uint16_t)sw_case_used;
                    cur_sw->case_count += 1u;
                }
                else { fast_cc_bail(cc, "too many switch cases"); break; }
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
                continue;
            }
            if (pk == FTOK_IDENT && fast_cc_peek_at(cc, 1u) == FTOK_COLON)
            {
                const uint32_t lid_name = fast_cc_data(cc);
                cc->pos += 2u;
                const uint32_t ll = fast_cc_get_user_label(cc, lid_name);
                if (cc->bailout) { break; }
                if (obj->labels[ll] != UINT32_MAX) { fast_cc_bail(cc, "duplicate label"); break; }
                fast_label_bind(obj, ll);
                continue;
            }
            if (pk == FTOK_KW_ATTRIBUTE)
            {
                // GCC/C23 __attribute__((fallthrough));
                fast_cc_parse_attributes(cc, NULL);
                continue;
            }
            break;
        }
        if (cc->bailout) { break; }
        const uint8_t k = cc->pos == first_pos ? first : fast_cc_peek(cc);
        if (top->kind == FSTK_BLOCK && k == FTOK_RBRACE) { continue; }

        // Mark non-block parent frame as now executing its single child statement!
        if (top->kind != FSTK_BLOCK) { top->entered_body = 1u; }

        // 1. Nested compound block `{ ... }`
        if (k == FTOK_LBRACE)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES) { fast_cc_bail(cc, "stmt nesting depth"); break; }
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
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_LPAREN)
            {
                cc->pos += 1u;
                if (fast_cc_const_int_expr(cc, true) == 0) { fast_cc_bail(cc, "_Static_assert failed"); break; }
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            }
            continue;
        }

        // 4. `if (cond) stmt [else stmt]`
        if (k == FTOK_KW_IF)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES) { fast_cc_bail(cc, "stmt nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after if"); break; }
            cc->pos += 1u;
            const uint32_t else_lbl = fast_label_new(obj);
            fast_cc_expr(cc, false, true, false, else_lbl, NULL);
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
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_IF_THEN, .dead = dead, .lbl0 = else_lbl };
            continue;
        }

        // 5. `while (cond) stmt`
        if (k == FTOK_KW_WHILE)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after while"); break; }
            cc->pos += 1u;
            const uint32_t cond_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, cond_lbl);
            fast_cc_expr(cc, false, true, false, brk_lbl, NULL);
            fast_cc_reset_tmps(cc);
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) after while cond"); break; }
            cc->pos += 1u;
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cond_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_WHILE, .lbl0 = cond_lbl, .lbl1 = brk_lbl };
            continue;
        }

        // 6. `do stmt while (cond);`
        if (k == FTOK_KW_DO)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
            cc->pos += 1u;
            const uint32_t body_lbl = fast_label_new(obj);
            const uint32_t cont_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, body_lbl);
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cont_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_DO_WHILE, .lbl0 = body_lbl, .lbl1 = cont_lbl, .lbl2 = brk_lbl };
            continue;
        }

        // 7. `for (init; cond; step) stmt`
        if (k == FTOK_KW_FOR)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES || break_sp >= 64u || cont_sp >= 64u) { fast_cc_bail(cc, "loop nesting depth"); break; }
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
                // A for's declaration is of automatic objects; clang takes static, extern and typedef there too (#3599).
                if ((ds.written & ~(BUSTER_FSPEC_REGISTER | BUSTER_FSPEC_AUTO)) != 0)
                {
                    fast_cc_bail(cc, "a for's declaration of a non-local");
                }
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
                {
                    FastDeclaratorResult dr = { 0 };
                    if (!fast_cc_parse_declarator(cc, btype, false, &dr, false)) { break; }
                    if (dr.attr.asm_label != NULL) { fast_cc_bail(cc, "asm label in a for's declaration"); break; }
                    const uint32_t lsi = dr.name_id != 0 ? fast_cc_sym_bind(cc, dr.name_id, (FastSym){ .kind = FSYM_LOCAL, .type = dr.type }) : 0u;
                    int32_t loff = 0;
                    if (fast_cc_peek(cc) == FTOK_ASSIGN)
                    {
                        cc->pos += 1u;
                        loff = fast_cc_init_local(cc, &dr.type, ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned);
                    }
                    else
                    {
                        loff = fast_cc_alloc_local(cc, ftype_size(cc, dr.type), ftype_align(cc, dr.type));
                    }
                    if (lsi != 0)
                    {
                        cc->syms[lsi].type = dr.type;
                        cc->syms[lsi].stack_off = loff;
                    }
                    if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                }
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            }
            else
            {
                fast_cc_expr(cc, false, false, false, 0, NULL);
                fast_cc_reset_tmps(cc);
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            }
            const uint32_t cond_lbl = fast_label_new(obj);
            const uint32_t cont_lbl = fast_label_new(obj);
            const uint32_t brk_lbl = fast_label_new(obj);
            fast_label_bind(obj, cond_lbl);
            // Cond clause
            if (fast_cc_peek(cc) != FTOK_SEMI)
            {
                fast_cc_expr(cc, false, true, false, brk_lbl, NULL);
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
            break_stack[break_sp++] = brk_lbl;
            cont_stack[cont_sp++] = cont_lbl;
            sstack[ssp++] = (FastStmtFrame){
                .kind = FSTK_FOR, .lbl0 = cond_lbl, .lbl1 = cont_lbl, .lbl2 = brk_lbl,
                .scope_mark = fscope, .step_pos = step_pos,
            };
            continue;
        }

        // 8. `switch (expr) stmt`
        if (k == FTOK_KW_SWITCH)
        {
            if (ssp >= BUSTER_FSTMT_FRAMES || sw_sp >= 16u || break_sp >= 64u) { fast_cc_bail(cc, "switch nesting depth"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "expected ( after switch"); break; }
            cc->pos += 1u;
            FastVal sv = { 0 };
            fast_cc_expr(cc, false, false, false, 0, &sv);
            if (cc->bailout) { break; }
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
            };
            break_stack[break_sp++] = brk_lbl;
            sstack[ssp++] = (FastStmtFrame){ .kind = FSTK_SWITCH, .sw_idx = swi, .lbl0 = disp_lbl, .lbl1 = brk_lbl };
            continue;
        }

        // 9. `break;` / `continue;` / `goto label;`
        if (k == FTOK_KW_BREAK)
        {
            if (break_sp == 0) { fast_cc_bail(cc, "break outside loop/switch"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            x86_jmp_label(obj, break_stack[break_sp - 1u]);
            continue;
        }
        if (k == FTOK_KW_CONTINUE)
        {
            if (cont_sp == 0) { fast_cc_bail(cc, "continue outside loop"); break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            x86_jmp_label(obj, cont_stack[cont_sp - 1u]);
            continue;
        }
        if (k == FTOK_KW_GOTO)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "computed goto unsupported"); break; }
            const uint32_t lid = fast_cc_data(cc);
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            const uint32_t ll = fast_cc_get_user_label(cc, lid);
            if (cc->bailout) { break; }
            x86_jmp_label(obj, ll);
            continue;
        }

        // 10. `return [expr];`
        if (k == FTOK_KW_RETURN)
        {
            saw_return_or_exit = true;
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_SEMI)
            {
                FastVal rv = { 0 };
                fast_cc_expr(cc, false, false, false, 0, &rv);
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
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            x86_jmp_label(obj, cc->cur_ret_label);
            continue;
        }

        // 11. Local declaration (inside block)
        if (top->kind == FSTK_BLOCK && fast_cc_is_type_start(cc, cc->pos))
        {
            FastDeclSpecs ds = { 0 };
            const FastType btype = fast_cc_parse_decl_specs(cc, &ds);
            if (cc->bailout) { break; }
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; continue; }
            while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
            {
                const uint32_t decl_start = cc->pos;
                FastDeclaratorResult dr = { 0 };
                if (!fast_cc_parse_declarator(cc, btype, false, &dr, false) || !fast_cc_declarator_named(cc, dr.name_id)) { break; }
                const uint32_t extra_al = ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned;
                if ((ds.written & (BUSTER_FSPEC_INLINE | BUSTER_FSPEC_NORETURN)) != 0 && (ds.is_typedef != 0 || !ftype_is_func(dr.type)))
                {
                    fast_cc_bail(cc, "inline or _Noreturn on an object");
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
                if (ds.is_typedef)
                {
                    if (dr.name_id != 0)
                    {
                        fast_cc_sym_bind(cc, dr.name_id, (FastSym){ .kind = FSYM_TYPEDEF, .type = dr.type });
                    }
                }
                else if (ftype_is_func(dr.type) || ds.is_extern)
                {
                    // A block's declaration of a function or an extern object names the entity of the file-scope
                    // declaration it sees, with that one's linkage and attributes (C11 6.2.2p4), so it binds no symbol
                    // of its own (#3595). One that sees none (the file declares it later or never: symbol 0, of no
                    // kind), or that a local of the name hides, or that brings attributes of its own, is left to the
                    // full pipeline. (Its asm label left above.)
                    if (ftype_is_func(dr.type))
                    {
                        fast_cc_check_main(cc, dr.name_id, dr.type);
                    }
                    const uint32_t seen = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                    const uint8_t kind = ftype_is_func(dr.type) ? (uint8_t)FSYM_FUNC : (uint8_t)FSYM_GLOBAL;
                    if (dr.name_id == 0 || cc->syms[seen].kind != kind || (kind == FSYM_GLOBAL && cc->syms[seen].storage == 1u) ||
                        ds.is_static || ds.attr.visibility_set || dr.attr.visibility_set || ds.attr.weak || dr.attr.weak)
                    {
                        fast_cc_bail(cc, "a block's declaration of a function or extern object");
                        break;
                    }
                }
                else if (ds.is_static)
                {
                    // Local `static` variable: unique internal STB_LOCAL symbol in .rodata / .data / .data.rel.ro / .bss!
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
                        fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                            .kind = FSYM_GLOBAL, .storage = 1u, .defined = 1u, .type = dr.type,
                            .obj_sym = osym, .section = sec, .sec_off = sec_off,
                        });
                    }
                }
                else
                {
                    // Automatic local variable on stack (bind BEFORE initializer so `T *p = malloc(sizeof(*p))` works!).
                    // A local register variable keeps its register's name for the asm statements that read it.
                    if (ds.is_unsupported_type || ftype_is_unsupported(dr.type)) { fast_cc_bail(cc, "unsupported local var type"); break; }
                    const uint32_t lsi = dr.name_id != 0 ? fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_LOCAL, .type = dr.type, .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    }) : 0u;
                    int32_t loff = 0;
                    if (fast_cc_peek(cc) == FTOK_ASSIGN)
                    {
                        cc->pos += 1u;
                        loff = fast_cc_init_local(cc, &dr.type, extra_al);
                    }
                    else
                    {
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
            }
            if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            else { fast_cc_bail(cc, "expected ; after local decl"); break; }
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
        fast_cc_expr(cc, false, false, false, 0, NULL);
        fast_cc_reset_tmps(cc);
        if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
        else { fast_cc_bail(cc, "expected ; after expr stmt"); break; }
    }

    // Every goto has a jump fixup. fast_obj_patch_func_jumps rejects an unbound target.
    if (!cc->bailout && !cc->quiet && !ftype_is_void(ret_t) && cc->cur_func_name_id != cc->id_main && !saw_return_or_exit)
    {
        fast_cc_bail(cc, "non-void function missing return");
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
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_LPAREN)
            {
                cc->pos += 1u;
                if (fast_cc_const_int_expr(cc, true) == 0) { fast_cc_bail(cc, "_Static_assert failed"); break; }
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            }
            continue;
        }
        if (k == FTOK_KW_ASM) { fast_cc_bail(cc, "file-scope asm"); break; }

        FastDeclSpecs ds = { 0 };
        const FastType btype = fast_cc_parse_decl_specs(cc, &ds);
        if ((ds.written & (BUSTER_FSPEC_REGISTER | BUSTER_FSPEC_AUTO)) != 0) { fast_cc_bail(cc, "register or auto at file scope"); }
        if (cc->bailout) { break; }
        if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; continue; }

        bool first = true;
        while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
        {
            FastDeclaratorResult dr = { 0 };
            if (!fast_cc_parse_declarator(cc, btype, false, &dr, first) || !fast_cc_declarator_named(cc, dr.name_id)) { break; }
            if ((ds.written & (BUSTER_FSPEC_INLINE | BUSTER_FSPEC_NORETURN)) != 0 && (ds.is_typedef != 0 || !ftype_is_func(dr.type)))
            {
                fast_cc_bail(cc, "inline or _Noreturn on an object");
                break;
            }
            const uint32_t extra_al = ds.attr.aligned > dr.attr.aligned ? ds.attr.aligned : dr.attr.aligned;
            const bool body = first && ftype_is_func(dr.type) && fast_cc_peek(cc) == FTOK_LBRACE;
            const FastDeclJoin join = fast_cc_decl_join(&ds, &dr, ftype_is_func(dr.type), body);
            const FastAttrInfo* alias = dr.attr.alias_name != NULL ? &dr.attr : ds.attr.alias_name != NULL ? &ds.attr : NULL;

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
            if (body)
            {
                const uint8_t stor = ds.is_static ? (ds.is_inline ? 4u : 1u) :
                                     (ds.is_inline && !ds.is_extern) ? 3u : 0u;
                // Reuse existing prototype FastSym if present at file scope!
                uint32_t si = (dr.name_id < cc->env_cap) ? cc->sym_cur[dr.name_id] : 0u;
                const uint8_t declared = (uint8_t)(si != 0 && cc->syms[si].kind == FSYM_FUNC);
                if (si != 0 && cc->syms[si].kind == FSYM_FUNC)
                {
                    // A pending inline definition allows a later one only as GNU's idiom: its extern inline one,
                    // then the external definition, not inline (#3595).
                    const uint8_t pending = cc->syms[si].inline_rules & BUSTER_FINLINE_PENDING;
                    if (cc->syms[si].defined != 0 || cc->syms[si].is_alias != 0 || (pending != 0 && (pending != BUSTER_FINLINE_GNU_PENDING || ds.is_inline)))
                    {
                        fast_cc_bail(cc, "redefinition of function");
                        break;
                    }
                    cc->syms[si].type = dr.type;
                    if (stor == 1u || stor == 4u) { cc->syms[si].storage = stor; }
                    if (ds.is_unsupported_type) { cc->syms[si].is_unsupported = 1u; }
                    fast_cc_join_declaration(cc, si, &join, false);
                    if (cc->bailout) { break; }
                }
                else
                {
                    si = fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_FUNC, .storage = stor, .is_weak = join.weak, .visibility = join.visibility,
                        .visibility_set = join.visibility_set, .inline_rules = join.inline_rules,
                        .is_unsupported = ds.is_unsupported_type, .type = dr.type,
                        .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    });
                }

                // An inline definition with external linkage is C99's or GNU's (#3595): GNU's extern inline one and
                // C99's plain one wait for the unit's end, which decides whether either is emitted; GNU's plain one and
                // C99's extern one are external definitions, emitted here. With internal linkage (static, or a static
                // declaration before it), an inline body waits for its first reference.
                const uint8_t storage = cc->syms[si].storage;
                const bool internal = storage == 1u || storage == 4u;
                const bool gnu = (cc->syms[si].inline_rules & BUSTER_FINLINE_GNU) != 0;
                const uint8_t decide = !ds.is_inline || internal ? 0u :
                                       gnu && ds.is_extern ? (uint8_t)BUSTER_FINLINE_GNU_PENDING :
                                       !gnu && !ds.is_extern ? (uint8_t)BUSTER_FINLINE_C99_PENDING : 0u;
                cc->syms[si].inline_rules |= decide;
                if ((ds.is_inline && internal) || decide != 0)
                {
                    const FastBodyScan bscan = fast_cc_prescan_body(cc, cc->pos);
                    if (bscan.end_pos == UINT32_MAX) { fast_cc_bail(cc, "unclosed inline body"); break; }
                    if (deferred_count < BUSTER_FDEFERRED_LIMIT)
                    {
                        deferred[deferred_count] = (FastBodyTask){
                            .sym_idx = si, .decl_start = tu_loop_pos, .body_start = cc->pos,
                            .param_names_start = dr.param_names_start, .param_count = dr.param_count, .declared = declared,
                            .previous_task = cc->syms[si].deferred_task,
                        };
                        if (cc->syms[si].referenced && decide == 0) { fast_cc_ready_task(cc, deferred_count); }
                        cc->syms[si].deferred_task = (uint16_t)(++deferred_count);
                    }
                    else { fast_cc_bail(cc, "too many inline functions"); break; }
                    cc->pos = bscan.end_pos + 1u;
                }
                else
                {
                    if (ds.is_unsupported_type) { fast_cc_bail(cc, "unsupported type in func def"); break; }
                    cc->cur_func_decl_pos = tu_loop_pos;
                    cc->cur_func_declared = declared;
                    fast_cc_compile_func(cc, si, dr.param_names_start, dr.param_count);
                }
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
                    fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_TYPEDEF, .type = dr.type, .is_unsupported = ds.is_unsupported_type,
                    });
                }
            }
            // Function prototype
            else if (ftype_is_func(dr.type))
            {
                if (dr.name_id != 0)
                {
                    const uint32_t existing = dr.name_id < cc->env_cap ? cc->sym_cur[dr.name_id] : 0u;
                    if (existing != 0 && cc->syms[existing].kind == FSYM_FUNC)
                    {
                        // Keep definition/obj_sym if already defined, update prototype info if more specific!
                        const FastComplexType* old_fn = cc->ctypes + (cc->syms[existing].type & BUSTER_FTYPE_CIDX_MASK);
                        if (old_fn->is_oldstyle && !cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].is_oldstyle)
                        {
                            cc->syms[existing].type = dr.type;
                        }
                        // An alias of a function a header declared: not of one defined, static or with an inline
                        // definition pending, which are clang's errors. An alias is a definition, so another alias, and
                        // an attribute on a later declaration (the full pipeline's warning), leave too: any declaration
                        // after an alias does.
                        const FastSym* declared = cc->syms + existing;
                        if (declared->is_alias != 0 ||
                            (alias != NULL && (declared->defined != 0 || declared->storage == 1u || declared->storage == 4u ||
                                               (declared->inline_rules & BUSTER_FINLINE_PENDING) != 0)))
                        {
                            fast_cc_bail(cc, "a declaration of an alias, or an alias of a defined or static function");
                            break;
                        }
                        fast_cc_join_declaration(cc, existing, &join, alias != NULL);
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
                            .is_unsupported = ds.is_unsupported_type, .type = dr.type,
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
                    // Update incomplete array type or storage if redeclared!
                    if (ftype_is_array(cc->syms[si].type) && !cc->ctypes[cc->syms[si].type & BUSTER_FTYPE_CIDX_MASK].complete &&
                        ftype_is_array(dr.type) && cc->ctypes[dr.type & BUSTER_FTYPE_CIDX_MASK].complete)
                    {
                        cc->syms[si].type = dr.type;
                    }
                    if (stor == 1u) { cc->syms[si].storage = 1u; }
                    fast_cc_join_declaration(cc, si, &join, alias != NULL);
                    if (cc->bailout) { break; }
                }
                else
                {
                    si = fast_cc_sym_bind(cc, dr.name_id, (FastSym){
                        .kind = FSYM_GLOBAL, .storage = stor, .is_weak = join.weak, .visibility = join.visibility,
                        .visibility_set = join.visibility_set,
                        .is_const_var = (ds.is_const || ftype_is_const(dr.type)) ? 1u : 0u,
                        .is_unsupported = ds.is_unsupported_type, .type = dr.type,
                        .asm_name = dr.attr.asm_label, .asm_name_len = dr.attr.asm_label_len,
                    });
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
        if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
        if (cc->pos == tu_loop_pos && !cc->bailout) { fast_cc_bail(cc, "top-level decl stalled"); break; }
    }

    fast_cc_alias_targets(cc);
    fast_cc_decide_inline_definitions(cc, deferred_count);

    // Visit newly reachable bodies in the same order as a forward scan: a reference to an earlier
    // definition waits until the scan wraps. This preserves .text, symbol and relocation order.
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
        if (!sym->defined)
        {
            if (sym->is_unsupported) { fast_cc_bail(cc, "called unsupported static inline"); break; }
            cc->pos = task->body_start;
            cc->cur_func_decl_pos = task->decl_start;
            cc->cur_func_declared = task->declared;
            fast_cc_compile_func(cc, task->sym_idx, task->param_names_start, task->param_count);
        }
    }

    // Finalize all tentative file-scope definitions into `.bss`!
    for (uint32_t i = 1u; i < cc->sym_count && !cc->bailout; i += 1)
    {
        FastSym* s = cc->syms + i;
        if (s->kind == FSYM_GLOBAL && s->is_tentative != 0 && s->defined == 0)
        {
            if (s->is_unsupported) { fast_cc_bail(cc, "tentative unsupported global"); break; }
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

    fast_cc_alias_define(cc);

    return !cc->bailout && !obj->overflow;
}

#endif
