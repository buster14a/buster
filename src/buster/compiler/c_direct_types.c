// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Compact bit-packed type system, AVX-512 struct member lookup, flat IdentId-indexed symbol scopes,
// leaf constant-expression evaluator, and iterative C17 declaration/declarator parser for c_direct (#2850).
//
// Call-graph discipline: every function in this file is strictly iterative and forms an acyclic DAG
// (never calls the runtime expression/statement compiler in c_direct_cc.c).

#ifndef BUSTER_C_DIRECT_TYPES_INCLUDED
#define BUSTER_C_DIRECT_TYPES_INCLUDED

#include "buster/compiler/c_direct_lex.c"
#include "buster/compiler/c_direct_x86.c"

// ---------------------------------------------------------------------------------------------------------------------
// Bit-packed 32-bit FastType representation
//
// Bits 20..22 hold pointer depth for every type, and bits 23..25 hold qualifiers.
// A scalar's low bits identify its base kind, plain char and long. A complex type
// has bit 31 set, its immutable FastComplexKind in bits 26..28, and the table
// index in bits 0..19. Completing a tag updates its size and members in the table.

typedef uint32_t FastType;

typedef enum FastScalarKind
{
    FTYPE_VOID = 0,
    FTYPE_BOOL = 1,
    FTYPE_I8   = 2,
    FTYPE_U8   = 3,
    FTYPE_I16  = 4,
    FTYPE_U16  = 5,
    FTYPE_I32  = 6,
    FTYPE_U32  = 7,
    FTYPE_I64  = 8,
    FTYPE_U64  = 9,
    FTYPE_F32  = 10,
    FTYPE_F64  = 11,
    FTYPE_I128 = 12,
    FTYPE_U128 = 13,
    FTYPE_UNSUP16 = 14,  // long double / _Float128 / _Complex (16B, align 16; bails if used in codegen)
    FTYPE_UNSUP2  = 15   // _Float16 (2B, align 2; bails if used in codegen)
} FastScalarKind;

#define BUSTER_FTYPE_COMPLEX_BIT     UINT32_C(0x80000000)
#define BUSTER_FTYPE_SCALAR_MASK     UINT32_C(0x0000001f)
#define BUSTER_FTYPE_SCALAR_CHAR     UINT32_C(0x00000400)
#define BUSTER_FTYPE_SCALAR_LONG     UINT32_C(0x00000800)

#define BUSTER_FTYPE_CIDX_MASK       UINT32_C(0x000fffff)
#define BUSTER_FTYPE_PTR_SH          20u
#define BUSTER_FTYPE_PTR_MASK        UINT32_C(0x00700000)
#define BUSTER_FTYPE_CONST           UINT32_C(0x00800000)
#define BUSTER_FTYPE_PCONST          UINT32_C(0x01000000)
#define BUSTER_FTYPE_VOL             UINT32_C(0x02000000)

#define BUSTER_FTYPE_KIND_SH         26u
#define BUSTER_FTYPE_KIND_MASK       UINT32_C(0x1c000000)

typedef enum FastComplexKind
{
    FCTYPE_NONE = 0,
    FCTYPE_STRUCT = 1,
    FCTYPE_UNION = 2,
    FCTYPE_ENUM = 3,
    FCTYPE_ARRAY = 4,
    FCTYPE_FUNC = 5,
    FCTYPE_PTR = 6
} FastComplexKind;

// Member of a struct/union (SoA member_ids[] alongside FastMemberInfo[] for AVX-512 vpcmpeqd lookup!)
typedef struct FastMemberInfo FastMemberInfo;
struct FastMemberInfo
{
    FastType type;
    uint32_t byte_offset;
    uint8_t bit_offset;    // 0..63 within storage unit if bit_width != 0
    uint8_t bit_width;     // 0 if normal field, else 1..64
    uint8_t bit_unit_size; // 1, 2, 4, or 8 bytes storage unit for bitfield
    uint8_t bit_signed;    // 1 if signed bitfield
};

// SysV AMD64 eightbyte classification for structs <= 16 bytes
typedef enum FastAbiClass
{
    FABI_NONE = 0,
    FABI_INT = 1,
    FABI_SSE = 2,
    FABI_MEM = 3
} FastAbiClass;

typedef struct FastComplexType FastComplexType;
struct FastComplexType
{
    uint8_t kind;          // FastComplexKind
    uint8_t complete;      // 1 if struct/union/enum defined or array sized
    uint8_t packed;        // __attribute__((packed))
    uint8_t is_variadic;   // for FCTYPE_FUNC
    uint8_t abi_lo;        // FastAbiClass for bytes 0..7
    uint8_t abi_hi;        // FastAbiClass for bytes 8..15
    uint8_t is_oldstyle;   // for FCTYPE_FUNC with empty ()
    uint8_t has_const_mbr;
    uint32_t size;
    uint32_t align;
    FastType inner;        // elem type (ARRAY), ret type (FUNC), pointee (PTR), underlying (ENUM)
    uint32_t count;        // elem count (ARRAY), param count (FUNC), member count (STRUCT/UNION)
    uint32_t start;        // param_start (FUNC), member_start (STRUCT/UNION)
    uint32_t tag_ident;    // tag IdentId if any
};

// ---------------------------------------------------------------------------------------------------------------------
// Flat IdentId-indexed symbol & scope tables

typedef enum FastSymKind
{
    FSYM_NONE = 0,
    FSYM_TYPEDEF = 1,
    FSYM_ENUM_CONST = 2,
    FSYM_LOCAL = 3,        // stack frame variable at [rbp + stack_off]
    FSYM_GLOBAL = 4,       // global/static variable in ELF object (obj_sym)
    FSYM_FUNC = 5          // function (obj_sym)
} FastSymKind;

typedef struct FastSym FastSym;
struct FastSym
{
    uint8_t kind;          // FastSymKind
    uint8_t storage;       // 0=default, 1=static, 2=extern, 3=inline, 4=static_inline
    uint8_t defined;       // 1 if body/initializer defined
    uint8_t referenced;    // 1 if referenced (for dead static elimination / diagnostics)
    uint8_t is_tentative;  // 1 if uninitialized file-scope tentative definition
    uint8_t is_const_var;  // 1 if const-qualified
    uint8_t is_noreturn;
    uint8_t is_weak;
    uint8_t is_unsupported;
    uint8_t visibility_set; // a declaration gave the visibility, so another one that differs is clang's error (#3595)
    uint8_t inline_rules;   // BUSTER_FINLINE_*: what a function's declarations say of its inline definition (#3595)
    FastType type;
    uint32_t ident_id;
    uint32_t obj_sym;      // symbol index in FastObj (for GLOBAL/FUNC)
    int32_t stack_off;     // rbp-relative offset (for LOCAL, negative for locals, >=16 for stack params)
    int64_t const_val;     // value (for FSYM_ENUM_CONST)
    uint32_t section;      // FastSecId if defined global
    uint32_t sec_off;      // byte offset within section
    const char* asm_name;  // optional __asm__("label") override
    uint32_t asm_name_len;
    uint8_t visibility;    // STV_DEFAULT / STV_HIDDEN / STV_PROTECTED
    uint16_t deferred_task; // one-based index of a deferred definition, zero for other symbols
};

// A function's declarations, for whether its inline definition is emitted as C99's and GNU's rules say, as the full
// pipeline decides it (#3595). GNU's rules (gnu_inline on a declaration, or -fgnu89-inline, which keeps a unit off the
// direct path): an extern inline definition is the unit's only, unless a declaration is plain inline. C99's: an inline
// definition without extern is the unit's only, unless a file-scope declaration lacks inline or has extern.
#define BUSTER_FINLINE_GNU 1u            // a declaration has gnu_inline
#define BUSTER_FINLINE_PLAIN 2u          // a declaration is inline without extern
#define BUSTER_FINLINE_EXTERNAL 4u       // a file-scope declaration without a body lacks inline or has extern
#define BUSTER_FINLINE_C99_PENDING 8u    // its definition is C99's inline one, decided at the unit's end
#define BUSTER_FINLINE_GNU_PENDING 16u   // its definition is GNU's extern inline one, decided at the unit's end
#define BUSTER_FINLINE_PENDING (BUSTER_FINLINE_C99_PENDING | BUSTER_FINLINE_GNU_PENDING)

typedef struct FastScopeUndo FastScopeUndo;
struct FastScopeUndo
{
    uint32_t ident_id;
    uint32_t prev_sym;
    uint8_t is_tag;
    uint8_t pad[3];
};

// Hash entry for fast member lookup on large structs (> 64 fields)
#define BUSTER_FMEMBER_HASH_SIZE 4096u
#define BUSTER_FMEMBER_HASH_MASK (BUSTER_FMEMBER_HASH_SIZE - 1u)

typedef struct FastMemberHash FastMemberHash;
struct FastMemberHash
{
    uint64_t key;          // ((uint64_t)struct_cidx << 32) | field_ident
    uint32_t member_idx;   // index into cc->member_info[] (UINT32_MAX = empty)
    uint32_t pad;
};

// Recorded function body slice for compilation
typedef struct FastBodyTask FastBodyTask;
struct FastBodyTask
{
    uint32_t sym_idx;
    uint32_t body_start;   // token index of '{'
    uint32_t param_names_start;
    uint16_t param_count;
    uint16_t previous_task;
};

#define BUSTER_FDEFERRED_LIMIT 4096u

// Main compiler context shared across c_direct_types.c and c_direct_cc.c
typedef struct FastCCState FastCCState;
struct FastCCState
{
    FastArena* arena;
    FastArena* scratch;
    FastPP* pp;
    FastObj* obj;

    // Preprocessed SoA token stream cursor
    const uint8_t* tk;     // pp->out_kind
    const uint32_t* td;    // pp->out_data
    uint32_t tcount;       // pp->out_count
    uint32_t pos;          // current token index

    // Complex type pool
    FastComplexType* ctypes;
    uint32_t ctype_count;
    uint32_t ctype_cap;

    // Struct/union members in SoA for AVX-512 vpcmpeqd search!
    uint32_t* member_ids;
    FastMemberInfo* member_info;
    uint32_t member_count;
    uint32_t member_cap;
    FastMemberHash* member_hash;

    // Function parameter type pool & parameter name pool
    FastType* param_types;
    uint32_t param_type_count;
    uint32_t param_type_cap;
    uint32_t* param_names;
    uint32_t param_name_count;
    uint32_t param_name_cap;

    // Flat IdentId-indexed symbol/tag environments
    uint32_t* sym_cur;     // ident_id -> current FastSym index (0 = none)
    uint32_t* tag_cur;     // ident_id -> current complex type index (0 = none)
    uint32_t env_cap;

    // Symbol table
    FastSym* syms;
    uint32_t sym_count;
    uint32_t sym_cap;

    // Scope undo stack
    FastScopeUndo* undo;
    uint32_t undo_count;
    uint32_t undo_cap;
    uint32_t scope_depth;  // 0 = file scope

    // Local static counter for unique internal names
    uint32_t static_local_seq;

    // Special cached IdentIds (va_list, builtins, attributes)
    uint32_t id_builtin_clzll;
    uint32_t id_builtin_clz;
    uint32_t id_builtin_ctzll;
    uint32_t id_builtin_ctz;
    uint32_t id_builtin_inff;
    uint32_t id_builtin_inf;
    uint32_t id_builtin_nanf;
    uint32_t id_builtin_nan;
    uint32_t id_builtin_expect;
    uint32_t id_builtin_constant_p;
    uint32_t id_builtin_unreachable;
    uint32_t id_builtin_bswap16;
    uint32_t id_builtin_bswap32;
    uint32_t id_builtin_bswap64;
    uint32_t id_builtin_popcount;
    uint32_t id_builtin_popcountll;
    uint32_t id_builtin_memcpy;
    uint32_t id_builtin_memset;
    uint32_t id_builtin_memmove;
    uint32_t id_builtin_memcmp;
    uint32_t id_builtin_strlen;
    uint32_t id_sync_synchronize;
    uint32_t id_atomic_load_n;
    uint32_t id_atomic_store_n;
    uint32_t id_main;
    FastType va_list_type; // __builtin_va_list (array[1] of struct __va_list_tag)
    FastType va_list_tag_type;

    // Options & status
    bool unsigned_char;
    bool quiet;            // -w (suppress warnings)
    bool bailout;
    const char* bailout_reason;
    uint32_t bailout_tok;

    FastBodyTask* deferred;
    uint64_t ready_tasks[BUSTER_FDEFERRED_LIMIT / 64u];
    uint64_t ready_groups;

    // Current function state (for c_direct_cc.c)
    uint32_t cur_func_sym;
    uint32_t cur_func_name_id;
    FastType cur_ret_type;
    uint32_t cur_ret_label;
    int32_t cur_sret_off;      // [rbp + cur_sret_off] hidden pointer for >16B struct return (0 if none)
    int32_t cur_frame_size;    // grows downward as locals are allocated (positive byte count)
    int32_t cur_tmp_off;       // temporary spill offset within current statement (positive byte count)
    int32_t cur_max_frame;     // high-water mark of cur_frame_size + spills across function
    int32_t cur_max_call_stack;
    int32_t cur_va_save_off;   // [rbp + cur_va_save_off] 176B register save area for variadic functions
    uint32_t cur_va_gp_init;   // initial gp_offset (8 * num_gp_params)
    uint32_t cur_va_fp_init;   // initial fp_offset (48 + 16 * num_fp_params)
    int32_t cur_va_overflow_off; // rbp + 16 + overflow_bytes

    // Cached .rodata constants (UINT32_MAX = not yet emitted)
    uint32_t ro_sign_f32_off;
    uint32_t ro_sign_f64_off;
    uint32_t ro_u64_to_f64_off; // 0x43f0000000000000 (2^64) for u64 -> double/float
    uint32_t ro_f64_to_u64_off; // 0x43e0000000000000 (2^63) for double -> u64
    uint32_t ro_f32_to_u64_off; // 0x5f000000 (2^63f) for float -> u64
    uint32_t ro_one_f32_off;
    uint32_t ro_one_f64_off;
};

static __attribute__((cold, noinline)) void fast_cc_bail(FastCCState* cc, const char* reason)
{
    if (!cc->bailout)
    {
        cc->bailout = true;
        cc->bailout_reason = reason;
        cc->bailout_tok = cc->pos;
    }
}

static inline uint8_t fast_cc_peek(const FastCCState* cc)
{
    return cc->pos < cc->tcount ? cc->tk[cc->pos] : FTOK_EOF;
}

static inline uint8_t fast_cc_peek_at(const FastCCState* cc, uint32_t offset)
{
    const uint32_t idx = cc->pos + offset;
    return idx < cc->tcount ? cc->tk[idx] : FTOK_EOF;
}

static inline uint32_t fast_cc_data(const FastCCState* cc)
{
    return cc->pos < cc->tcount ? cc->td[cc->pos] : 0u;
}

static inline const char* fast_cc_ident_str(const FastCCState* cc, uint32_t id, uint32_t* out_len)
{
    const FastIdentEntry* e = cc->pp->idents.entries + id;
    if (out_len != NULL) { *out_len = e->length; }
    return cc->pp->idents.strings + e->str_offset;
}

// ---------------------------------------------------------------------------------------------------------------------
// FastType inline operations (register-only bit math for scalars & pointers!)

static inline FastType ftype_from_index(const FastCCState* cc, uint32_t idx)
{
    return BUSTER_FTYPE_COMPLEX_BIT | ((uint32_t)cc->ctypes[idx].kind << BUSTER_FTYPE_KIND_SH) | idx;
}

// Pointer depth participates in the comparison: an array and a pointer to that array have different kinds here.
static inline bool ftype_has_kind(FastType t, FastComplexKind kind)
{
    return (t & (BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_KIND_MASK)) == ((uint32_t)kind << BUSTER_FTYPE_KIND_SH);
}

static inline bool ftype_is_complex(FastType t)
{
    return (t & BUSTER_FTYPE_COMPLEX_BIT) != 0;
}

static inline uint32_t ftype_ptr_depth(FastType t)
{
    return (t & BUSTER_FTYPE_PTR_MASK) >> BUSTER_FTYPE_PTR_SH;
}

static inline bool ftype_is_ptr(FastType t)
{
    return ftype_ptr_depth(t) != 0 || ftype_has_kind(t, FCTYPE_PTR);
}

static inline bool ftype_is_void(FastType t)
{
    return !ftype_is_complex(t) && ftype_ptr_depth(t) == 0 && (t & BUSTER_FTYPE_SCALAR_MASK) == FTYPE_VOID;
}

static inline bool ftype_is_bool(FastType t)
{
    return !ftype_is_complex(t) && ftype_ptr_depth(t) == 0 && (t & BUSTER_FTYPE_SCALAR_MASK) == FTYPE_BOOL;
}

static inline bool ftype_is_float(FastType t)
{
    if (ftype_is_complex(t) || ftype_ptr_depth(t) > 0) { return false; }
    const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
    return sk == FTYPE_F32 || sk == FTYPE_F64;
}

static inline bool ftype_is_f64(FastType t)
{
    return !ftype_is_complex(t) && ftype_ptr_depth(t) == 0 && (t & BUSTER_FTYPE_SCALAR_MASK) == FTYPE_F64;
}

static inline bool ftype_is_i128(FastType t)
{
    if (ftype_is_complex(t) || ftype_ptr_depth(t) > 0) { return false; }
    const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
    return sk == FTYPE_I128 || sk == FTYPE_U128;
}

static inline bool ftype_is_int(FastType t)
{
    if (ftype_ptr_depth(t) > 0) { return false; }
    if (!ftype_is_complex(t))
    {
        const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
        return (sk >= FTYPE_BOOL && sk <= FTYPE_U64) || sk == FTYPE_I128 || sk == FTYPE_U128;
    }
    return ftype_has_kind(t, FCTYPE_ENUM);
}

static inline bool ftype_is_scalar(FastType t)
{
    return ftype_is_ptr(t) || ftype_is_int(t) || ftype_is_float(t);
}

static inline bool ftype_is_array(FastType t)
{
    return ftype_has_kind(t, FCTYPE_ARRAY);
}

static inline bool ftype_is_func(FastType t)
{
    return ftype_has_kind(t, FCTYPE_FUNC);
}

static inline bool ftype_is_record(FastType t)
{
    return ftype_has_kind(t, FCTYPE_STRUCT) || ftype_has_kind(t, FCTYPE_UNION);
}

static inline bool ftype_is_const(FastType t)
{
    return (t & BUSTER_FTYPE_CONST) != 0;
}

static inline FastType ftype_unqual(FastType t)
{
    return t & ~(BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL);
}

static inline FastType ftype_with_const(FastType t)
{
    return t | BUSTER_FTYPE_CONST;
}

static inline bool ftype_is_signed(const FastCCState* cc, FastType t)
{
    if (ftype_ptr_depth(t) > 0) { return false; }
    if (ftype_is_complex(t))
    {
        const FastComplexType* ct = cc->ctypes + (t & BUSTER_FTYPE_CIDX_MASK);
        if (ct->kind == FCTYPE_ENUM) { t = ct->inner; }
        else { return false; }
    }
    const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
    return sk == FTYPE_I8 || sk == FTYPE_I16 || sk == FTYPE_I32 || sk == FTYPE_I64 || sk == FTYPE_I128;
}

static inline bool ftype_is_unsupported(FastType t)
{
    return !ftype_is_complex(t) && (t & BUSTER_FTYPE_SCALAR_MASK) >= FTYPE_UNSUP16;
}

static inline uint32_t ftype_size(const FastCCState* cc, FastType t)
{
    if (ftype_ptr_depth(t) > 0) { return 8u; }
    if (!ftype_is_complex(t))
    {
        static const uint8_t sz[16] = { 0, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 16, 16, 16, 2 };
        return sz[t & BUSTER_FTYPE_SCALAR_MASK];
    }
    const FastComplexType* ct = cc->ctypes + (t & BUSTER_FTYPE_CIDX_MASK);
    return ct->kind == FCTYPE_PTR ? 8u : ct->size;
}

static inline uint32_t ftype_align(const FastCCState* cc, FastType t)
{
    if (ftype_ptr_depth(t) > 0) { return 8u; }
    if (!ftype_is_complex(t))
    {
        static const uint8_t al[16] = { 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 16, 16, 16, 2 };
        return al[t & BUSTER_FTYPE_SCALAR_MASK];
    }
    const FastComplexType* ct = cc->ctypes + (t & BUSTER_FTYPE_CIDX_MASK);
    return ct->kind == FCTYPE_PTR ? 8u : (ct->align != 0 ? ct->align : 1u);
}

static uint32_t fast_cc_alloc_ctype(FastCCState* cc, uint8_t kind)
{
    uint32_t idx = 0;
    if (cc->ctype_count < cc->ctype_cap)
    {
        idx = cc->ctype_count++;
        cc->ctypes[idx] = (FastComplexType){ .kind = kind, .align = 1u };
    }
    else
    {
        fast_cc_bail(cc, "ctype table full");
    }
    return idx;
}

// Make pointer to `t` (0 allocations for ptr_depth 0..2!!)
static FastType ftype_make_ptr(FastCCState* cc, FastType t)
{
    const uint32_t d = ftype_ptr_depth(t);
    const bool inner_const = ftype_is_const(t);
    if (d < 3u)
    {
        FastType res = ftype_unqual(t) + (UINT32_C(1) << BUSTER_FTYPE_PTR_SH);
        if (d == 0 && inner_const) { res |= BUSTER_FTYPE_PCONST; }
        return res;
    }
    const uint32_t idx = fast_cc_alloc_ctype(cc, FCTYPE_PTR);
    if (idx != 0)
    {
        cc->ctypes[idx].inner = t;
        cc->ctypes[idx].size = 8u;
        cc->ctypes[idx].align = 8u;
        cc->ctypes[idx].complete = 1u;
    }
    return ftype_from_index(cc, idx);
}

// Dereference pointer type `t` (1 subtraction for ptr_depth 1..3!!)
static FastType ftype_deref(const FastCCState* cc, FastType t)
{
    const uint32_t d = ftype_ptr_depth(t);
    if (d > 0)
    {
        FastType res = ftype_unqual(t) - (UINT32_C(1) << BUSTER_FTYPE_PTR_SH);
        if (d == 1u && (t & BUSTER_FTYPE_PCONST) != 0) { res = (res & ~BUSTER_FTYPE_PCONST) | BUSTER_FTYPE_CONST; }
        return res;
    }
    if (ftype_has_kind(t, FCTYPE_PTR))
    {
        return cc->ctypes[t & BUSTER_FTYPE_CIDX_MASK].inner;
    }
    return FTYPE_VOID;
}

static __attribute__((noinline)) FastType ftype_decay_slow(FastCCState* cc, FastType t)
{
    if (ftype_is_array(t))
    {
        return ftype_make_ptr(cc, cc->ctypes[t & BUSTER_FTYPE_CIDX_MASK].inner);
    }
    return ftype_make_ptr(cc, t);
}

// An array or a function decays to a pointer; every other type is its own. The test is one compare of the kind and
// pointer bits, inline at each of its ~80 callers: the out-of-line function it was spent more on its call than on its
// answer (#3581).
static inline __attribute__((always_inline)) FastType ftype_decay(FastCCState* cc, FastType t)
{
    const uint32_t shape = t & (BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_KIND_MASK);
    const uint32_t decays = (uint32_t)(shape == ((uint32_t)FCTYPE_ARRAY << BUSTER_FTYPE_KIND_SH)) |
                            (uint32_t)(shape == ((uint32_t)FCTYPE_FUNC << BUSTER_FTYPE_KIND_SH));
    if (__builtin_expect(decays != 0, 0)) { return ftype_decay_slow(cc, t); }
    return t;
}

static inline FastType ftype_promote_int(const FastCCState* cc, FastType t)
{
    if (ftype_has_kind(t, FCTYPE_ENUM))
    {
        t = cc->ctypes[t & BUSTER_FTYPE_CIDX_MASK].inner;
    }
    if (!ftype_is_complex(t) && ftype_ptr_depth(t) == 0)
    {
        const uint32_t sk = t & BUSTER_FTYPE_SCALAR_MASK;
        if (sk >= FTYPE_BOOL && sk <= FTYPE_U16)
        {
            return FTYPE_I32;
        }
    }
    return ftype_unqual(t);
}

static inline FastType ftype_usual_arith(const FastCCState* cc, FastType a, FastType b)
{
    a = ftype_promote_int(cc, a);
    b = ftype_promote_int(cc, b);
    const uint32_t sa = a & BUSTER_FTYPE_SCALAR_MASK;
    const uint32_t sb = b & BUSTER_FTYPE_SCALAR_MASK;
    if (sa == FTYPE_F64 || sb == FTYPE_F64) { return FTYPE_F64; }
    if (sa == FTYPE_F32 || sb == FTYPE_F32) { return FTYPE_F32; }
    if (sa == FTYPE_U128 || sb == FTYPE_U128) { return FTYPE_U128; }
    if (sa == FTYPE_I128 || sb == FTYPE_I128) { return FTYPE_I128; }
    if (sa == FTYPE_U64 || sb == FTYPE_U64) { return FTYPE_U64; }
    if (sa == FTYPE_I64 || sb == FTYPE_I64) { return FTYPE_I64; }
    if (sa == FTYPE_U32 || sb == FTYPE_U32) { return FTYPE_U32; }
    return FTYPE_I32;
}

// ---------------------------------------------------------------------------------------------------------------------
// AVX-512 struct member lookup (16 IdentIds per ZMM register via vpcmpeqd; hash table for > 64 fields!)

static void fast_member_hash_put(FastCCState* cc, uint32_t cidx, uint32_t field_id, uint32_t member_idx)
{
    const uint64_t key = ((uint64_t)cidx << 32) | field_id;
    const uint64_t h = (key ^ (key >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    uint32_t slot = (uint32_t)(h ^ (h >> 32)) & BUSTER_FMEMBER_HASH_MASK;
    for (uint32_t probe = 0; probe < BUSTER_FMEMBER_HASH_SIZE; probe += 1)
    {
        if (cc->member_hash[slot].key == 0 || cc->member_hash[slot].key == key)
        {
            cc->member_hash[slot].key = key;
            cc->member_hash[slot].member_idx = member_idx;
            break;
        }
        slot = (slot + 1u) & BUSTER_FMEMBER_HASH_MASK;
    }
}

static const FastMemberInfo* fast_struct_find_member(const FastCCState* cc, uint32_t cidx, uint32_t field_id)
{
    const FastComplexType* st = cc->ctypes + cidx;
    const uint32_t count = st->count;
    const FastMemberInfo* found = NULL;
    if (count <= 64u)
    {
        // AVX-512 vectorized scan: 16 uint32_t field IDs per cycle!
        const uint32_t* const ids = cc->member_ids + st->start;
        const __m512i needle = _mm512_set1_epi32((int32_t)field_id);
        for (uint32_t i = 0; i < count; i += 16u)
        {
            const uint32_t rem = count - i;
            const __mmask16 mask = rem >= 16u ? 0xffffu : (uint16_t)((1u << rem) - 1u);
            const __m512i v = _mm512_maskz_loadu_epi32(mask, ids + i);
            const __mmask16 eq = _mm512_mask_cmpeq_epi32_mask(mask, v, needle);
            if (eq != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz((uint32_t)eq);
                found = cc->member_info + st->start + i + lane;
                break;
            }
        }
    }
    else
    {
        const uint64_t key = ((uint64_t)cidx << 32) | field_id;
        const uint64_t h = (key ^ (key >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        uint32_t slot = (uint32_t)(h ^ (h >> 32)) & BUSTER_FMEMBER_HASH_MASK;
        for (uint32_t probe = 0; probe < BUSTER_FMEMBER_HASH_SIZE; probe += 1)
        {
            const uint64_t sk = cc->member_hash[slot].key;
            if (sk == 0) { break; }
            if (sk == key)
            {
                found = cc->member_info + cc->member_hash[slot].member_idx;
                break;
            }
            slot = (slot + 1u) & BUSTER_FMEMBER_HASH_MASK;
        }
    }
    return found;
}

// Compute SysV AMD64 ABI eightbyte classification for a struct/union (iterative, <= 16 bytes)
static void fast_struct_classify_abi(FastCCState* cc, uint32_t cidx)
{
    FastComplexType* st = cc->ctypes + cidx;
    if (st->size == 0 || st->size > 16u || st->packed != 0)
    {
        st->abi_lo = FABI_MEM;
        st->abi_hi = FABI_MEM;
        return;
    }
    uint8_t cls[2] = { FABI_NONE, FABI_NONE };
    // Iterative worklist of (type, base_offset)
    FastType wt[64];
    uint32_t wo[64];
    uint32_t wsp = 0;
    wt[wsp] = ftype_from_index(cc, cidx);
    wo[wsp] = 0;
    wsp += 1u;

    while (wsp > 0)
    {
        wsp -= 1u;
        const FastType cur = wt[wsp];
        const uint32_t off = wo[wsp];
        if (ftype_is_ptr(cur) || ftype_is_int(cur))
        {
            const uint32_t eb = off >> 3;
            if (eb < 2u) { cls[eb] = FABI_INT; }
        }
        else if (ftype_is_float(cur))
        {
            const uint32_t eb = off >> 3;
            if (eb < 2u && cls[eb] != FABI_INT) { cls[eb] = FABI_SSE; }
        }
        else if (ftype_is_array(cur))
        {
            const FastComplexType* ar = cc->ctypes + (cur & BUSTER_FTYPE_CIDX_MASK);
            const uint32_t esz = ftype_size(cc, ar->inner);
            for (uint32_t i = 0; i < ar->count && wsp < 64u; i += 1)
            {
                wt[wsp] = ar->inner;
                wo[wsp] = off + i * esz;
                wsp += 1u;
            }
        }
        else if (ftype_is_record(cur))
        {
            const FastComplexType* sub = cc->ctypes + (cur & BUSTER_FTYPE_CIDX_MASK);
            for (uint32_t i = 0; i < sub->count && wsp < 64u; i += 1)
            {
                const FastMemberInfo* mi = cc->member_info + sub->start + i;
                wt[wsp] = mi->type;
                wo[wsp] = off + mi->byte_offset;
                wsp += 1u;
            }
        }
    }
    st->abi_lo = cls[0] != FABI_NONE ? cls[0] : FABI_INT;
    st->abi_hi = st->size > 8u ? (cls[1] != FABI_NONE ? cls[1] : FABI_INT) : FABI_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// Scope & symbol helpers

static inline uint32_t fast_cc_scope_enter(FastCCState* cc)
{
    cc->scope_depth += 1u;
    return cc->undo_count;
}

static inline void fast_cc_scope_leave(FastCCState* cc, uint32_t mark)
{
    uint32_t* const sym_cur = cc->sym_cur;
    uint32_t* const tag_cur = cc->tag_cur;
    const FastScopeUndo* const undo = cc->undo;
    for (uint32_t i = cc->undo_count; i > mark; i -= 1)
    {
        const FastScopeUndo u = undo[i - 1u];
        if (u.is_tag) { tag_cur[u.ident_id] = u.prev_sym; }
        else { sym_cur[u.ident_id] = u.prev_sym; }
    }
    cc->undo_count = mark;
    cc->scope_depth -= 1u;
}

// Inlined: the caller's record is then built where it is kept, with no copy of fields just stored one by one.
static inline __attribute__((always_inline)) uint32_t fast_cc_sym_bind(FastCCState* cc, uint32_t id, FastSym sym)
{
    uint32_t idx = 0;
    if (id < cc->env_cap && cc->sym_count < cc->sym_cap)
    {
        idx = cc->sym_count++;
        sym.ident_id = id;
        cc->syms[idx] = sym;
        if (cc->scope_depth > 0)
        {
            if (cc->undo_count < cc->undo_cap)
            {
                cc->undo[cc->undo_count++] = (FastScopeUndo){ .ident_id = id, .prev_sym = cc->sym_cur[id], .is_tag = 0 };
            }
            else
            {
                fast_cc_bail(cc, "undo stack full");
            }
        }
        cc->sym_cur[id] = idx;
    }
    else
    {
        fast_cc_bail(cc, "sym table full");
    }
    return idx;
}

static void fast_cc_tag_bind(FastCCState* cc, uint32_t id, uint32_t cidx)
{
    if (id < cc->env_cap)
    {
        if (cc->scope_depth > 0)
        {
            if (cc->undo_count < cc->undo_cap)
            {
                cc->undo[cc->undo_count++] = (FastScopeUndo){ .ident_id = id, .prev_sym = cc->tag_cur[id], .is_tag = 1u };
            }
            else
            {
                fast_cc_bail(cc, "undo stack full");
            }
        }
        cc->tag_cur[id] = cidx;
    }
}

static inline bool fast_cc_is_typedef_name(const FastCCState* cc, uint32_t id)
{
    if (id == 0 || id >= cc->env_cap) { return false; }
    const uint32_t s = cc->sym_cur[id];
    return s != 0 && cc->syms[s].kind == FSYM_TYPEDEF;
}

// The keywords that start a C type specifier / qualifier / storage class, as bits of their token kinds (64..127).
#define BUSTER_FTOK_BIT(k) (UINT64_C(1) << ((uint32_t)(k) - 64u))
#define BUSTER_FTOK_TYPE_START_KEYWORDS (BUSTER_FTOK_BIT(FTOK_KW_VOID) | BUSTER_FTOK_BIT(FTOK_KW_CHAR) | BUSTER_FTOK_BIT(FTOK_KW_SHORT) | \
    BUSTER_FTOK_BIT(FTOK_KW_INT) | BUSTER_FTOK_BIT(FTOK_KW_LONG) | BUSTER_FTOK_BIT(FTOK_KW_FLOAT) | BUSTER_FTOK_BIT(FTOK_KW_DOUBLE) | \
    BUSTER_FTOK_BIT(FTOK_KW_SIGNED) | BUSTER_FTOK_BIT(FTOK_KW_UNSIGNED) | BUSTER_FTOK_BIT(FTOK_KW_BOOL) | BUSTER_FTOK_BIT(FTOK_KW_STRUCT) | \
    BUSTER_FTOK_BIT(FTOK_KW_UNION) | BUSTER_FTOK_BIT(FTOK_KW_ENUM) | BUSTER_FTOK_BIT(FTOK_KW_CONST) | BUSTER_FTOK_BIT(FTOK_KW_VOLATILE) | \
    BUSTER_FTOK_BIT(FTOK_KW_RESTRICT) | BUSTER_FTOK_BIT(FTOK_KW_STATIC) | BUSTER_FTOK_BIT(FTOK_KW_EXTERN) | BUSTER_FTOK_BIT(FTOK_KW_TYPEDEF) | \
    BUSTER_FTOK_BIT(FTOK_KW_REGISTER) | BUSTER_FTOK_BIT(FTOK_KW_AUTO) | BUSTER_FTOK_BIT(FTOK_KW_INLINE) | BUSTER_FTOK_BIT(FTOK_KW_NORETURN) | \
    BUSTER_FTOK_BIT(FTOK_KW_ALIGNAS) | BUSTER_FTOK_BIT(FTOK_KW_ATTRIBUTE) | BUSTER_FTOK_BIT(FTOK_KW_EXTENSION) | \
    BUSTER_FTOK_BIT(FTOK_KW_BUILTIN_VA_LIST) | BUSTER_FTOK_BIT(FTOK_KW_ATOMIC) | BUSTER_FTOK_BIT(FTOK_KW_COMPLEX) | \
    BUSTER_FTOK_BIT(FTOK_KW_INT128) | BUSTER_FTOK_BIT(FTOK_KW_FLOAT128) | BUSTER_FTOK_BIT(FTOK_KW_FLOAT16) | \
    BUSTER_FTOK_BIT(FTOK_KW_THREAD_LOCAL) | BUSTER_FTOK_BIT(FTOK_KW_TYPEOF))

// Check if current token starts a C type specifier / qualifier / storage class. A keyword is a bit test, inline at each
// caller; an identifier asks whether it names a typedef (sqlite3.c: 50,000 questions a compile, #3581).
static inline __attribute__((always_inline)) bool fast_cc_is_type_start(const FastCCState* cc, uint32_t at)
{
    bool starts = false;
    if (at < cc->tcount)
    {
        const uint32_t k = cc->tk[at];
        if (k == FTOK_IDENT) { starts = fast_cc_is_typedef_name(cc, cc->td[at]); }
        else { starts = ((uint32_t)(k >= 64u) & (uint32_t)(BUSTER_FTOK_TYPE_START_KEYWORDS >> ((k - 64u) & 63u))) != 0; }
    }
    return starts;
}

// ---------------------------------------------------------------------------------------------------------------------
// Attribute & __asm__ label parser helpers

typedef struct FastAttrInfo FastAttrInfo;
struct FastAttrInfo
{
    uint32_t aligned;
    uint8_t packed;
    uint8_t noreturn;
    uint8_t weak;
    uint8_t visibility;   // STV_DEFAULT / STV_HIDDEN (also "internal", as clang emits it) / STV_PROTECTED
    uint8_t visibility_set; // a visibility attribute gave it; the last one of a list counts
    uint8_t gnu_inline;
    uint8_t mode_size;    // 0=none, 1/2/4/8/16 (for __attribute__((__mode__(...))))
    uint8_t mode_float;
    const char* asm_label;
    uint32_t asm_label_len;
    const char* alias_name;
    uint32_t alias_len;
};

static int64_t fast_cc_const_int_expr(FastCCState* cc, bool stop_at_comma);

// Whether a string literal is spelled as its bytes, without an encoding prefix or a numeric escape: the only operand
// that clang reads as its bytes in an attribute's string and an asm label (#3595).
static inline bool fast_cc_literal_plain(const FastCCState* cc, uint32_t literal)
{
    return ((cc->pp->lit_coded[literal >> 6] >> (literal & 63u)) & 1u) == 0;
}

// Skip or parse __attribute__((...)) sequence
static __attribute__((noinline)) void fast_cc_parse_attributes_full(FastCCState* cc, FastAttrInfo* attr)
{
    while (!cc->bailout && (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE || fast_cc_peek(cc) == FTOK_KW_EXTENSION))
    {
        if (fast_cc_peek(cc) == FTOK_KW_EXTENSION)
        {
            cc->pos += 1u;
            continue;
        }
        cc->pos += 1u; // __attribute__
        if (fast_cc_peek(cc) != FTOK_LPAREN || fast_cc_peek_at(cc, 1u) != FTOK_LPAREN)
        {
            fast_cc_bail(cc, "malformed __attribute__");
            break;
        }
        cc->pos += 2u;
        uint32_t depth = 2u;
        while (depth > 0 && cc->pos < cc->tcount && !cc->bailout)
        {
            const uint8_t k = fast_cc_peek(cc);
            if (k == FTOK_RPAREN)
            {
                depth -= 1u;
                cc->pos += 1u;
                continue;
            }
            if (k == FTOK_LPAREN)
            {
                depth += 1u;
                cc->pos += 1u;
                continue;
            }
            if (k == FTOK_COMMA)
            {
                cc->pos += 1u;
                continue;
            }
            if (depth == 2u && (k == FTOK_IDENT || (k >= 64u && k < 122u)))
            {
                uint32_t len = 0;
                const char* s = fast_cc_ident_str(cc, fast_cc_data(cc), &len);
                // Strip leading/trailing "__"
                if (len > 4u && s[0] == '_' && s[1] == '_' && s[len - 2u] == '_' && s[len - 1u] == '_')
                {
                    s += 2;
                    len -= 4u;
                }
                cc->pos += 1u;
                if (len == 6u && memcmp(s, "packed", 6) == 0)
                {
                    if (attr != NULL) { attr->packed = 1u; }
                }
                else if (len == 8u && memcmp(s, "noreturn", 8) == 0)
                {
                    if (attr != NULL) { attr->noreturn = 1u; }
                }
                else if (len == 4u && memcmp(s, "weak", 4) == 0)
                {
                    if (attr != NULL) { attr->weak = 1u; }
                }
                else if (len == 7u && memcmp(s, "aligned", 7) == 0)
                {
                    if (fast_cc_peek(cc) == FTOK_LPAREN)
                    {
                        cc->pos += 1u;
                        const int64_t al = fast_cc_const_int_expr(cc, false);
                        if (attr != NULL && al > 0 && al <= 4096) { attr->aligned = (uint32_t)al; }
                        if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                    }
                    else if (attr != NULL)
                    {
                        attr->aligned = 16u;
                    }
                }
                else if (len == 4u && memcmp(s, "mode", 4) == 0 && fast_cc_peek(cc) == FTOK_LPAREN)
                {
                    cc->pos += 1u;
                    if (fast_cc_peek(cc) == FTOK_IDENT)
                    {
                        uint32_t mlen = 0;
                        const char* ms = fast_cc_ident_str(cc, fast_cc_data(cc), &mlen);
                        if (mlen > 4u && ms[0] == '_' && ms[1] == '_' && ms[mlen - 2u] == '_' && ms[mlen - 1u] == '_')
                        {
                            ms += 2; mlen -= 4u;
                        }
                        if (attr != NULL)
                        {
                            if (mlen == 2u && memcmp(ms, "QI", 2) == 0) { attr->mode_size = 1u; }
                            else if (mlen == 2u && memcmp(ms, "HI", 2) == 0) { attr->mode_size = 2u; }
                            else if (mlen == 2u && memcmp(ms, "SI", 2) == 0) { attr->mode_size = 4u; }
                            else if (mlen == 2u && memcmp(ms, "DI", 2) == 0) { attr->mode_size = 8u; }
                            else if (mlen == 4u && memcmp(ms, "word", 4) == 0) { attr->mode_size = 8u; }
                            else if (mlen == 7u && memcmp(ms, "pointer", 7) == 0) { attr->mode_size = 8u; }
                            else if (mlen == 2u && memcmp(ms, "SF", 2) == 0) { attr->mode_size = 4u; attr->mode_float = 1u; }
                            else if (mlen == 2u && memcmp(ms, "DF", 2) == 0) { attr->mode_size = 8u; attr->mode_float = 1u; }
                            else { attr->mode_size = 16u; } // TI/TC/XF etc. (only in unused glibc typedefs!)
                        }
                        cc->pos += 1u;
                    }
                    if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                }
                else if (len == 10u && memcmp(s, "visibility", 10) == 0 && fast_cc_peek(cc) == FTOK_LPAREN)
                {
                    // visibility("default" | "hidden" | "internal" | "protected"), one literal spelled as its bytes,
                    // on a declaration; internal is hidden, as clang emits it, and the last of a list counts. Any
                    // other is the full pipeline's error or warning (#3595).
                    cc->pos += 1u;
                    uint8_t visibility = UINT8_MAX;
                    if (attr != NULL && fast_cc_peek(cc) == FTOK_STR_LIT && fast_cc_peek_at(cc, 1u) == FTOK_RPAREN &&
                        fast_cc_literal_plain(cc, fast_cc_data(cc)))
                    {
                        const FastLitSlice sl = cc->pp->lit_slices[fast_cc_data(cc)];
                        const char* vs = cc->pp->lit_bytes + sl.offset;
                        if (sl.length == 7u && memcmp(vs, "default", 7) == 0) { visibility = STV_DEFAULT; }
                        else if (sl.length == 6u && memcmp(vs, "hidden", 6) == 0) { visibility = STV_HIDDEN; }
                        else if (sl.length == 8u && memcmp(vs, "internal", 8) == 0) { visibility = STV_HIDDEN; }
                        else if (sl.length == 9u && memcmp(vs, "protected", 9) == 0) { visibility = STV_PROTECTED; }
                    }
                    if (visibility == UINT8_MAX)
                    {
                        fast_cc_bail(cc, "visibility attribute's operand");
                        break;
                    }
                    attr->visibility = visibility;
                    attr->visibility_set = 1u;
                    cc->pos += 2u;
                }
                else if (len == 10u && memcmp(s, "gnu_inline", 10) == 0)
                {
                    if (attr != NULL) { attr->gnu_inline = 1u; }
                }
                else if (len == 5u && memcmp(s, "alias", 5) == 0 && fast_cc_peek(cc) == FTOK_LPAREN)
                {
                    fast_cc_bail(cc, "alias attribute");
                    break;
                }
                else if (len == 7u && memcmp(s, "section", 7) == 0)
                {
                    fast_cc_bail(cc, "section attribute");
                    break;
                }
                else if (len == 11u && memcmp(s, "constructor", 11) == 0)
                {
                    fast_cc_bail(cc, "constructor attribute");
                    break;
                }
                else if (len == 10u && memcmp(s, "destructor", 10) == 0)
                {
                    fast_cc_bail(cc, "destructor attribute");
                    break;
                }
                else if (len == 7u && memcmp(s, "cleanup", 7) == 0)
                {
                    fast_cc_bail(cc, "cleanup attribute");
                    break;
                }
                else if (len == 11u && memcmp(s, "vector_size", 11) == 0)
                {
                    fast_cc_bail(cc, "vector_size attribute");
                    break;
                }
                else if ((len == 6u && memcmp(s, "target", 6) == 0) ||
                         (len == 7u && memcmp(s, "flatten", 7) == 0) ||
                         (len == 5u && memcmp(s, "naked", 5) == 0) ||
                         (len == 15u && memcmp(s, "ext_vector_type", 15) == 0) ||
                         (len == 17u && memcmp(s, "transparent_union", 17) == 0) ||
                         (len == 9u && memcmp(s, "tls_model", 9) == 0) ||
                         (len == 7u && memcmp(s, "regparm", 7) == 0) ||
                         (len == 8u && memcmp(s, "fastcall", 8) == 0) ||
                         (len == 7u && memcmp(s, "stdcall", 7) == 0) ||
                         (len == 6u && memcmp(s, "ms_abi", 6) == 0) ||
                         (len == 8u && memcmp(s, "sysv_abi", 8) == 0) ||
                         (len == 9u && memcmp(s, "interrupt", 9) == 0) ||
                         (len == 24u && memcmp(s, "patchable_function_entry", 24) == 0) ||
                         (len == 5u && memcmp(s, "error", 5) == 0) ||
                         (len == 7u && memcmp(s, "warning", 7) == 0) ||
                         (len == 11u && memcmp(s, "unavailable", 11) == 0) ||
                         (len == 9u && memcmp(s, "enable_if", 9) == 0) ||
                         (len == 11u && memcmp(s, "diagnose_if", 11) == 0) ||
                         (len == 12u && memcmp(s, "overloadable", 12) == 0) ||
                         (len == 9u && memcmp(s, "selectany", 9) == 0) ||
                         (len == 5u && memcmp(s, "ifunc", 5) == 0) ||
                         (len == 7u && memcmp(s, "weakref", 7) == 0) ||
                         (len == 8u && memcmp(s, "musttail", 8) == 0) ||
                         (len == 18u && memcmp(s, "no_stack_protector", 18) == 0) ||
                         (len == 13u && memcmp(s, "stack_protect", 13) == 0) ||
                         (len == 6u && memcmp(s, "common", 6) == 0) ||
                         (len == 8u && memcmp(s, "nocommon", 8) == 0))
                {
                    fast_cc_bail(cc, "unsupported codegen/diagnostic attribute");
                    break;
                }
                else
                {
                    // Benign annotation attribute (noinline, always_inline, format, nonnull, nothrow, pure, const, malloc, unused, deprecated, cold, fallthrough, warn_unused_result, alloc_align, alloc_size, no_sanitize_*, etc.)
                    if (fast_cc_peek(cc) == FTOK_LPAREN)
                    {
                        uint32_t ad = 1u;
                        cc->pos += 1u;
                        while (ad > 0 && cc->pos < cc->tcount)
                        {
                            const uint8_t ak = fast_cc_peek(cc);
                            if (ak == FTOK_LPAREN) { ad += 1u; }
                            else if (ak == FTOK_RPAREN) { ad -= 1u; }
                            cc->pos += 1u;
                        }
                    }
                }
                continue;
            }
            cc->pos += 1u;
        }
    }
}

// Most declarations and declarators have no __attribute__ and no __extension__: the test is inline, the parser out of
// line (#3581).
static inline __attribute__((always_inline)) void fast_cc_parse_attributes(FastCCState* cc, FastAttrInfo* attr)
{
    const uint32_t k = fast_cc_peek(cc);
    if (((uint32_t)(k == FTOK_KW_ATTRIBUTE) | (uint32_t)(k == FTOK_KW_EXTENSION)) != 0) { fast_cc_parse_attributes_full(cc, attr); }
}

// Parse optional `__asm__("label")` on a declaration (glibc's symbol renaming). The label is its literals joined, as
// clang joins them: glibc's __REDIRECT writes __asm__ ("" "__isoc99_scanf") (#3595). One literal with bytes is used
// where it is; several are joined in the unit's arena, which outlives the body's scratch. An empty label, or a
// literal with an encoding prefix or a numeric escape, is the full pipeline's error.
static void fast_cc_parse_asm_label(FastCCState* cc, FastAttrInfo* attr)
{
    if (fast_cc_peek(cc) == FTOK_KW_ASM && fast_cc_peek_at(cc, 1u) == FTOK_LPAREN)
    {
        cc->pos += 2u;
        const uint32_t first = cc->pos;
        uint32_t length = 0;
        uint32_t pieces = 0;
        uint32_t piece = first;
        bool plain = true;
        while (fast_cc_peek(cc) == FTOK_STR_LIT)
        {
            const uint32_t literal = fast_cc_data(cc);
            const uint32_t bytes = cc->pp->lit_slices[literal].length;
            plain = plain && fast_cc_literal_plain(cc, literal);
            pieces += bytes != 0 ? 1u : 0u;
            piece = bytes != 0 ? cc->pos : piece;
            length += bytes;
            cc->pos += 1u;
        }
        const char* label = NULL;
        if (fast_cc_peek(cc) == FTOK_RPAREN && length != 0 && plain && pieces == 1u)
        {
            label = cc->pp->lit_bytes + cc->pp->lit_slices[cc->td[piece]].offset;
        }
        else if (fast_cc_peek(cc) == FTOK_RPAREN && length != 0 && plain)
        {
            char* joined = (char*)fast_arena_alloc(cc->arena, length, 1);
            uint32_t at = 0;
            for (uint32_t i = first; joined != NULL && i < cc->pos; i += 1)
            {
                const FastLitSlice sl = cc->pp->lit_slices[cc->td[i]];
                memcpy(joined + at, cc->pp->lit_bytes + sl.offset, sl.length);
                at += sl.length;
            }
            label = joined;
        }
        if (label == NULL)
        {
            fast_cc_bail(cc, "asm label's operand");
        }
        else
        {
            if (attr != NULL)
            {
                attr->asm_label = label;
                attr->asm_label_len = length;
            }
            cc->pos += 1u;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Strictly acyclic call-graph levels (ZERO direct or mutual recursion cycles!):
//   Level 1: fast_cc_parse_simple_typename_leaf (pure leaf; no const_int_expr, no record defs)
//   Level 2: fast_cc_const_int_expr (calls Level 1)
//   Level 3: fast_cc_parse_attributes (calls Level 2)
//   Level 4: fast_cc_parse_enum (calls Levels 2-3)
//   Level 5: fast_cc_parse_scalar_or_tag_specs (calls Levels 1-4; tags only, never calls record_iter!)
//   Level 6: fast_cc_parse_declarator (calls Levels 2-3, 5)
//   Level 7: fast_cc_parse_record_iter (calls Levels 2-3, 5-6)
//   Level 8: fast_cc_parse_decl_specs & fast_cc_parse_typename (calls Levels 1-7)

// Level 1: Pure leaf typename parser for casts/sizeof/alignof/offsetof inside constant expressions.
// Handles scalars, typedefs, existing struct/union/enum tags, const/volatile/restrict, pointers,
// literal-sized arrays `[N]`, and simple function pointers `(*)(...)`. NEVER calls const_int_expr or record_iter!
static FastType fast_cc_parse_simple_typename_leaf(FastCCState* cc, bool* out_ok)
{
    uint32_t saw_void = 0, saw_char = 0, saw_short = 0, saw_int = 0, saw_long = 0;
    uint32_t saw_float = 0, saw_double = 0, saw_signed = 0, saw_unsigned = 0, saw_bool = 0;
    bool is_const = false;
    bool is_vol = false;
    bool has_explicit = false;
    FastType base = FTYPE_I32;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_KW_CONST) { is_const = true; cc->pos += 1u; }
        else if (k == FTOK_KW_VOLATILE) { is_vol = true; cc->pos += 1u; }
        else if (k == FTOK_KW_RESTRICT || k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
        else if (k == FTOK_KW_VOID) { saw_void += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_CHAR) { saw_char += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SHORT) { saw_short += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_INT) { saw_int += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_LONG) { saw_long += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_FLOAT) { saw_float += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_DOUBLE) { saw_double += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SIGNED) { saw_signed += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_UNSIGNED) { saw_unsigned += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BOOL) { saw_bool += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BUILTIN_VA_LIST) { has_explicit = true; base = cc->va_list_type; cc->pos += 1u; }
        else if (k == FTOK_KW_INT128)
        {
            uint32_t ilen = 0;
            const char* is = fast_cc_ident_str(cc, fast_cc_data(cc), &ilen);
            const bool is_u = (ilen > 2u && is[2] == 'u') || saw_unsigned != 0;
            has_explicit = true;
            base = is_u ? FTYPE_U128 : FTYPE_I128;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_FLOAT128 || k == FTOK_KW_COMPLEX) { has_explicit = true; base = FTYPE_UNSUP16; cc->pos += 1u; }
        else if (k == FTOK_KW_FLOAT16) { has_explicit = true; base = FTYPE_UNSUP2; cc->pos += 1u; }
        else if (k == FTOK_KW_STRUCT || k == FTOK_KW_UNION || k == FTOK_KW_ENUM)
        {
            if (has_explicit) { break; }
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_IDENT || fast_cc_peek_at(cc, 1u) == FTOK_LBRACE)
            {
                if (out_ok != NULL) { *out_ok = false; }
                return FTYPE_VOID;
            }
            const uint32_t tid = fast_cc_data(cc);
            cc->pos += 1u;
            const uint32_t cidx = tid < cc->env_cap ? cc->tag_cur[tid] : 0u;
            if (cidx == 0)
            {
                if (out_ok != NULL) { *out_ok = false; }
                return FTYPE_VOID;
            }
            has_explicit = true;
            base = ftype_from_index(cc, cidx);
        }
        else if (k == FTOK_IDENT && !has_explicit &&
                 (saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_signed + saw_unsigned + saw_bool) == 0 &&
                 fast_cc_is_typedef_name(cc, fast_cc_data(cc)))
        {
            has_explicit = true;
            base = cc->syms[cc->sym_cur[fast_cc_data(cc)]].type;
            cc->pos += 1u;
        }
        else
        {
            break;
        }
    }

    if (!has_explicit)
    {
        if (saw_void) { base = FTYPE_VOID; }
        else if (saw_bool) { base = FTYPE_BOOL; }
        else if (saw_float) { base = FTYPE_F32; }
        else if (saw_double)
        {
            base = saw_long ? FTYPE_UNSUP16 : FTYPE_F64;
        }
        else if (saw_char)
        {
            base = saw_unsigned ? FTYPE_U8 : saw_signed ? FTYPE_I8 : (cc->unsigned_char ? FTYPE_U8 : FTYPE_I8);
            if (!saw_signed && !saw_unsigned) { base |= BUSTER_FTYPE_SCALAR_CHAR; }
        }
        else if (saw_short) { base = saw_unsigned ? FTYPE_U16 : FTYPE_I16; }
        else if (saw_long >= 2u) { base = saw_unsigned ? FTYPE_U64 : FTYPE_I64; }
        else if (saw_long == 1u) { base = (saw_unsigned ? FTYPE_U64 : FTYPE_I64) | BUSTER_FTYPE_SCALAR_LONG; }
        else if (saw_unsigned) { base = FTYPE_U32; }
        else if (saw_int || saw_signed) { base = FTYPE_I32; }
        else { if (out_ok != NULL) { *out_ok = false; } return FTYPE_VOID; }
    }
    if (is_const) { base = ftype_with_const(base); }
    if (is_vol) { base |= BUSTER_FTYPE_VOL; }

    // Abstract pointers `*`
    while (fast_cc_peek(cc) == FTOK_STAR)
    {
        cc->pos += 1u;
        bool pc = false;
        while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE || fast_cc_peek(cc) == FTOK_KW_RESTRICT)
        {
            if (fast_cc_peek(cc) == FTOK_KW_CONST) { pc = true; }
            cc->pos += 1u;
        }
        base = ftype_make_ptr(cc, base);
        if (pc) { base = ftype_with_const(base); }
    }
    // Simple `(*)(...)` function pointer or `(*)[N]` pointer-to-array (size is always 8!)
    if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_peek_at(cc, 1u) == FTOK_STAR)
    {
        uint32_t d = 1u;
        cc->pos += 1u;
        while (d > 0 && cc->pos < cc->tcount)
        {
            if (fast_cc_peek(cc) == FTOK_LPAREN) { d += 1u; }
            else if (fast_cc_peek(cc) == FTOK_RPAREN) { d -= 1u; }
            cc->pos += 1u;
        }
        if (fast_cc_peek(cc) == FTOK_LPAREN)
        {
            d = 1u; cc->pos += 1u;
            while (d > 0 && cc->pos < cc->tcount)
            {
                if (fast_cc_peek(cc) == FTOK_LPAREN) { d += 1u; }
                else if (fast_cc_peek(cc) == FTOK_RPAREN) { d -= 1u; }
                cc->pos += 1u;
            }
            base = ftype_make_ptr(cc, FTYPE_VOID);
        }
        else if (fast_cc_peek(cc) == FTOK_LBRACKET)
        {
            while (fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                cc->pos += 1u;
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RBRACKET) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RBRACKET) { cc->pos += 1u; }
            }
            base = ftype_make_ptr(cc, FTYPE_VOID);
        }
    }
    // Simple literal array dimensions `[N]`
    if (fast_cc_peek(cc) == FTOK_LBRACKET)
    {
        uint32_t dims[8];
        uint32_t nd = 0;
        while (fast_cc_peek(cc) == FTOK_LBRACKET && nd < 8u)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_INT_LIT && fast_cc_peek(cc) != FTOK_INT_U32)
            {
                if (out_ok != NULL) { *out_ok = false; }
                return FTYPE_VOID;
            }
            dims[nd++] = fast_cc_data(cc);
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_RBRACKET)
            {
                if (out_ok != NULL) { *out_ok = false; }
                return FTYPE_VOID;
            }
            cc->pos += 1u;
        }
        for (int32_t i = (int32_t)nd - 1; i >= 0 && !cc->bailout; i -= 1)
        {
            const uint64_t total_sz = (uint64_t)dims[i] * (uint64_t)ftype_size(cc, base);
            if (total_sz > (uint64_t)INT32_MAX)
            {
                fast_cc_bail(cc, "array size overflow");
                break;
            }
            const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
            if (ac != 0)
            {
                cc->ctypes[ac].inner = base;
                cc->ctypes[ac].count = dims[i];
                cc->ctypes[ac].size = (uint32_t)total_sz;
                cc->ctypes[ac].align = ftype_align(cc, base);
                cc->ctypes[ac].complete = 1u;
                base = ftype_from_index(cc, ac);
            }
        }
    }
    if (out_ok != NULL) { *out_ok = !cc->bailout; }
    return base;
}

// Parse ONLY `__builtin_offsetof(type, member[.member|[idx]]*)` and stop immediately after `)`!
static uint64_t fast_cc_parse_offsetof(FastCCState* cc)
{
    cc->pos += 1u; // __builtin_offsetof
    if (fast_cc_peek(cc) != FTOK_LPAREN) { fast_cc_bail(cc, "offsetof ("); return 0; }
    cc->pos += 1u;
    bool tok_ok = false;
    FastType cur_t = fast_cc_parse_simple_typename_leaf(cc, &tok_ok);
    if (!tok_ok || fast_cc_peek(cc) != FTOK_COMMA) { fast_cc_bail(cc, "offsetof type"); return 0; }
    cc->pos += 1u;
    uint64_t total_off = 0;
    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN && !cc->bailout)
    {
        if (fast_cc_peek(cc) == FTOK_DOT) { cc->pos += 1u; continue; }
        if (fast_cc_peek(cc) == FTOK_LBRACKET)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_INT_LIT && fast_cc_peek(cc) != FTOK_INT_U32) { fast_cc_bail(cc, "offsetof [idx]"); break; }
            const uint64_t idx_v = fast_cc_data(cc);
            cc->pos += 1u;
            if (fast_cc_peek(cc) != FTOK_RBRACKET) { fast_cc_bail(cc, "offsetof ]"); break; }
            cc->pos += 1u;
            if (!ftype_is_array(cur_t)) { fast_cc_bail(cc, "offsetof non-array"); break; }
            cur_t = cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner;
            total_off += idx_v * ftype_size(cc, cur_t);
            continue;
        }
        if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "offsetof field"); break; }
        const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
        if (mi == NULL) { fast_cc_bail(cc, "offsetof unknown field"); break; }
        total_off += mi->byte_offset;
        cur_t = mi->type;
        cc->pos += 1u;
    }
    if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "offsetof )"); return 0; }
    cc->pos += 1u;
    return total_off;
}

// Level 2: Pure leaf iterative integer constant expression evaluator (for array bounds, enums, bitfields, case labels,
// aligned(N), _Static_assert, etc.) — NEVER calls runtime expression compiler or struct definition parser!
static int64_t fast_cc_const_int_expr(FastCCState* cc, bool stop_at_comma)
{
    typedef struct CVal CVal;
    struct CVal { uint64_t v; bool u; };
    CVal vals[64];
    uint16_t ops[64];
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
                // Check if C-style cast `(type)expr`
                if (fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    cc->pos += 1u;
                    bool tok_ok = false;
                    const FastType ct = fast_cc_parse_simple_typename_leaf(cc, &tok_ok);
                    if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN)
                    {
                        fast_cc_bail(cc, "complex cast in const_int_expr");
                        break;
                    }
                    cc->pos += 1u;
                    // Push cast op encoded as 0x1000 | (size<<2) | (signed<<1) | (bool)
                    const uint32_t sz = ftype_size(cc, ct);
                    const uint16_t cop = (uint16_t)(0x1000u | (sz << 2) | (ftype_is_signed(cc, ct) ? 2u : 0u) | (ftype_is_bool(ct) ? 1u : 0u));
                    if (osp < 64u) { ops[osp++] = cop; }
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
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = fast_cc_data(cc), .u = false }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_U32)
            {
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = fast_cc_data(cc), .u = true }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_I64)
            {
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = cc->pp->num_pool[fast_cc_data(cc)], .u = false }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_U64)
            {
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = cc->pp->num_pool[fast_cc_data(cc)], .u = true }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_CHAR_LIT)
            {
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = (uint64_t)(int64_t)(int32_t)fast_cc_data(cc), .u = false }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_IDENT)
            {
                const uint32_t id = fast_cc_data(cc);
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                if (si != 0 && cc->syms[si].kind == FSYM_ENUM_CONST)
                {
                    if (vsp < 64u) { vals[vsp++] = (CVal){ .v = (uint64_t)cc->syms[si].const_val, .u = false }; }
                    cc->pos += 1u;
                    expect = false;
                }
                else
                {
                    fast_cc_bail(cc, "non-const ident in const_int_expr");
                    break;
                }
            }
            else if (k == FTOK_KW_SIZEOF || k == FTOK_KW_ALIGNOF)
            {
                const bool is_align = k == FTOK_KW_ALIGNOF;
                cc->pos += 1u;
                if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_is_type_start(cc, cc->pos + 1u))
                {
                    cc->pos += 1u;
                    bool tok_ok = false;
                    const FastType st = fast_cc_parse_simple_typename_leaf(cc, &tok_ok);
                    if (!tok_ok || fast_cc_peek(cc) != FTOK_RPAREN)
                    {
                        fast_cc_bail(cc, "complex sizeof(type) in const_int_expr");
                        break;
                    }
                    cc->pos += 1u;
                    const uint64_t res = is_align ? ftype_align(cc, st) : ftype_size(cc, st);
                    if (vsp < 64u) { vals[vsp++] = (CVal){ .v = res, .u = true }; }
                    expect = false;
                }
                else
                {
                    // sizeof(var) or sizeof(str_lit)
                    const bool paren = fast_cc_peek(cc) == FTOK_LPAREN;
                    if (paren) { cc->pos += 1u; }
                    uint64_t res = 0;
                    if (fast_cc_peek(cc) == FTOK_IDENT)
                    {
                        const uint32_t id = fast_cc_data(cc);
                        const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                        if (si != 0 && (cc->syms[si].kind == FSYM_LOCAL || cc->syms[si].kind == FSYM_GLOBAL))
                        {
                            res = is_align ? ftype_align(cc, cc->syms[si].type) : ftype_size(cc, cc->syms[si].type);
                            cc->pos += 1u;
                            // Handle sizeof(arr[0]) or sizeof(s.field)
                            FastType cur_t = cc->syms[si].type;
                            while (fast_cc_peek(cc) == FTOK_LBRACKET || fast_cc_peek(cc) == FTOK_DOT || fast_cc_peek(cc) == FTOK_ARROW)
                            {
                                if (fast_cc_peek(cc) == FTOK_LBRACKET)
                                {
                                    cc->pos += 1u;
                                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RBRACKET) { cc->pos += 1u; }
                                    if (fast_cc_peek(cc) == FTOK_RBRACKET) { cc->pos += 1u; }
                                    cur_t = ftype_is_array(cur_t) ? cc->ctypes[cur_t & BUSTER_FTYPE_CIDX_MASK].inner : ftype_deref(cc, cur_t);
                                }
                                else
                                {
                                    const bool is_arrow = fast_cc_peek(cc) == FTOK_ARROW;
                                    cc->pos += 1u;
                                    if (is_arrow) { cur_t = ftype_deref(cc, cur_t); }
                                    if (!ftype_is_record(cur_t) || fast_cc_peek(cc) != FTOK_IDENT) { fast_cc_bail(cc, "sizeof member"); break; }
                                    const FastMemberInfo* mi = fast_struct_find_member(cc, cur_t & BUSTER_FTYPE_CIDX_MASK, fast_cc_data(cc));
                                    if (mi == NULL) { fast_cc_bail(cc, "sizeof unknown member"); break; }
                                    cur_t = mi->type;
                                    cc->pos += 1u;
                                }
                                res = is_align ? ftype_align(cc, cur_t) : ftype_size(cc, cur_t);
                            }
                        }
                        else
                        {
                            fast_cc_bail(cc, "sizeof complex expr in const_int_expr");
                            break;
                        }
                    }
                    else if (fast_cc_peek(cc) == FTOK_STR_LIT)
                    {
                        uint32_t slen = 0;
                        while (fast_cc_peek(cc) == FTOK_STR_LIT)
                        {
                            slen += cc->pp->lit_slices[fast_cc_data(cc)].length;
                            cc->pos += 1u;
                        }
                        res = slen + 1u;
                    }
                    else
                    {
                        fast_cc_bail(cc, "sizeof expr in const_int_expr");
                        break;
                    }
                    if (paren)
                    {
                        if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "sizeof missing )"); break; }
                        cc->pos += 1u;
                    }
                    if (vsp < 64u) { vals[vsp++] = (CVal){ .v = res, .u = true }; }
                    expect = false;
                }
            }
            else if (k == FTOK_KW_BUILTIN_OFFSETOF)
            {
                const uint64_t total_off = fast_cc_parse_offsetof(cc);
                if (vsp < 64u) { vals[vsp++] = (CVal){ .v = total_off, .u = true }; }
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
                    if ((op & 0x1000u) != 0)
                    {
                        if (vsp > 0)
                        {
                            const uint32_t sz = (op >> 2) & 0xfu;
                            const bool sgn = (op & 2u) != 0;
                            const bool is_b = (op & 1u) != 0;
                            uint64_t x = vals[vsp - 1u].v;
                            if (is_b) { x = x != 0 ? 1u : 0u; }
                            else if (sz == 1u) { x = sgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
                            else if (sz == 2u) { x = sgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
                            else if (sz == 4u) { x = sgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
                            vals[vsp - 1u] = (CVal){ .v = x, .u = !sgn };
                        }
                    }
                    else
                    {
                        FastPPVal tmp_v[64];
                        for (uint32_t i = 0; i < vsp; i += 1) { tmp_v[i] = (FastPPVal){ vals[i].v, vals[i].u }; }
                        fast_pp_apply_op(cc->pp, op, tmp_v, &vsp);
                        if (cc->pp->bailout) { fast_cc_bail(cc, cc->pp->bailout_reason); break; }
                        for (uint32_t i = 0; i < vsp; i += 1) { vals[i] = (CVal){ tmp_v[i].val, tmp_v[i].is_unsigned }; }
                    }
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
                if (!has_q) { break; } // ':' belongs to case/bitfield!
                while (osp > 0 && ops[osp - 1u] != FTOK_QUESTION)
                {
                    const uint16_t op = ops[--osp];
                    FastPPVal tmp_v[64];
                    for (uint32_t i = 0; i < vsp; i += 1) { tmp_v[i] = (FastPPVal){ vals[i].v, vals[i].u }; }
                    fast_pp_apply_op(cc->pp, op, tmp_v, &vsp);
                    if (cc->pp->bailout) { fast_cc_bail(cc, cc->pp->bailout_reason); break; }
                    for (uint32_t i = 0; i < vsp; i += 1) { vals[i] = (CVal){ tmp_v[i].val, tmp_v[i].is_unsigned }; }
                }
                if (osp > 0) { ops[osp - 1u] = BUSTER_FPP_OP_TERNARY; }
                cc->pos += 1u;
                expect = true;
                continue;
            }
            if (k == FTOK_COMMA && stop_at_comma && pdepth == 0) { break; }
            const uint8_t prec = fast_pp_prec(k);
            if (prec == 0) { break; }
            while (osp > 0 && ops[osp - 1u] != BUSTER_FPP_OP_LPAREN)
            {
                const uint16_t top = ops[osp - 1u];
                const uint8_t tprec = ((top & 0x1000u) != 0 || (top >= BUSTER_FPP_OP_UNARY_PLUS && top <= BUSTER_FPP_OP_UNARY_BANG)) ? 12u :
                                      top == BUSTER_FPP_OP_TERNARY ? 1u : fast_pp_prec((uint8_t)top);
                if (tprec > prec || (tprec == prec && k != FTOK_QUESTION))
                {
                    osp -= 1u;
                    if ((top & 0x1000u) != 0)
                    {
                        if (vsp > 0)
                        {
                            const uint32_t sz = (top >> 2) & 0xfu;
                            const bool sgn = (top & 2u) != 0;
                            const bool is_b = (top & 1u) != 0;
                            uint64_t x = vals[vsp - 1u].v;
                            if (is_b) { x = x != 0 ? 1u : 0u; }
                            else if (sz == 1u) { x = sgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
                            else if (sz == 2u) { x = sgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
                            else if (sz == 4u) { x = sgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
                            vals[vsp - 1u] = (CVal){ .v = x, .u = !sgn };
                        }
                    }
                    else
                    {
                        FastPPVal tmp_v[64];
                        for (uint32_t i = 0; i < vsp; i += 1) { tmp_v[i] = (FastPPVal){ vals[i].v, vals[i].u }; }
                        fast_pp_apply_op(cc->pp, top, tmp_v, &vsp);
                        if (cc->pp->bailout) { fast_cc_bail(cc, cc->pp->bailout_reason); break; }
                        for (uint32_t i = 0; i < vsp; i += 1) { vals[i] = (CVal){ tmp_v[i].val, tmp_v[i].is_unsigned }; }
                    }
                }
                else
                {
                    break;
                }
            }
            if (osp < 64u) { ops[osp++] = k; }
            cc->pos += 1u;
            expect = true;
        }
    }

    while (osp > 0 && !cc->bailout)
    {
        const uint16_t top = ops[--osp];
        if (top == BUSTER_FPP_OP_LPAREN || top == FTOK_QUESTION)
        {
            fast_cc_bail(cc, "unclosed op in const_int_expr");
            break;
        }
        if ((top & 0x1000u) != 0)
        {
            if (vsp > 0)
            {
                const uint32_t sz = (top >> 2) & 0xfu;
                const bool sgn = (top & 2u) != 0;
                const bool is_b = (top & 1u) != 0;
                uint64_t x = vals[vsp - 1u].v;
                if (is_b) { x = x != 0 ? 1u : 0u; }
                else if (sz == 1u) { x = sgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
                else if (sz == 2u) { x = sgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
                else if (sz == 4u) { x = sgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
                vals[vsp - 1u] = (CVal){ .v = x, .u = !sgn };
            }
        }
        else
        {
            FastPPVal tmp_v[64];
            for (uint32_t i = 0; i < vsp; i += 1) { tmp_v[i] = (FastPPVal){ vals[i].v, vals[i].u }; }
            fast_pp_apply_op(cc->pp, top, tmp_v, &vsp);
            if (cc->pp->bailout) { fast_cc_bail(cc, cc->pp->bailout_reason); break; }
            for (uint32_t i = 0; i < vsp; i += 1) { vals[i] = (CVal){ tmp_v[i].val, tmp_v[i].is_unsigned }; }
        }
    }
    if (vsp != 1u && !cc->bailout)
    {
        fast_cc_bail(cc, "empty const_int_expr");
    }
    return (!cc->bailout && vsp == 1u) ? (int64_t)vals[0].v : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Iterative declaration-specifier & declarator parser (ZERO recursion!)

typedef struct FastDeclSpecs FastDeclSpecs;
struct FastDeclSpecs
{
    FastType type;
    uint8_t is_typedef;
    uint8_t is_static;
    uint8_t is_extern;
    uint8_t is_inline;
    uint8_t is_const;
    uint8_t is_volatile;
    uint8_t is_noreturn;
    uint8_t is_long_double;
    uint8_t is_unsupported_type; // __int128, _Float128, _Complex, _Atomic (allowed in unused extern prototypes!)
    FastAttrInfo attr;
};

// Parse enum { A, B = 5, ... } (strictly leaf, calls fast_cc_const_int_expr only)
static FastType fast_cc_parse_enum(FastCCState* cc)
{
    cc->pos += 1u; // 'enum'
    FastAttrInfo attr = { 0 };
    fast_cc_parse_attributes(cc, &attr);
    uint32_t tag_id = 0;
    if (fast_cc_peek(cc) == FTOK_IDENT)
    {
        tag_id = fast_cc_data(cc);
        cc->pos += 1u;
    }
    uint32_t cidx = 0;
    if (tag_id != 0 && tag_id < cc->env_cap && cc->tag_cur[tag_id] != 0)
    {
        cidx = cc->tag_cur[tag_id];
        if (cc->ctypes[cidx].kind != FCTYPE_ENUM)
        {
            fast_cc_bail(cc, "tag redeclared as enum");
            return FTYPE_I32;
        }
    }
    if (cidx == 0)
    {
        cidx = fast_cc_alloc_ctype(cc, FCTYPE_ENUM);
        if (cidx != 0)
        {
            cc->ctypes[cidx].inner = FTYPE_I32;
            cc->ctypes[cidx].size = 4u;
            cc->ctypes[cidx].align = 4u;
            cc->ctypes[cidx].tag_ident = tag_id;
            if (tag_id != 0) { fast_cc_tag_bind(cc, tag_id, cidx); }
        }
    }
    if (fast_cc_peek(cc) == FTOK_LBRACE)
    {
        cc->pos += 1u;
        int64_t next_val = 0;
        bool need_i64 = false;
        bool has_neg = false;
        while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RBRACE && !cc->bailout)
        {
            if (fast_cc_peek(cc) != FTOK_IDENT)
            {
                fast_cc_bail(cc, "expected enumerator name");
                break;
            }
            const uint32_t eid = fast_cc_data(cc);
            cc->pos += 1u;
            fast_cc_parse_attributes(cc, NULL);
            if (fast_cc_peek(cc) == FTOK_ASSIGN)
            {
                cc->pos += 1u;
                next_val = fast_cc_const_int_expr(cc, true);
            }
            if (next_val < 0) { has_neg = true; }
            if (next_val > INT32_MAX || next_val < INT32_MIN)
            {
                if (has_neg || next_val > UINT32_MAX) { need_i64 = true; }
            }
            fast_cc_sym_bind(cc, eid, (FastSym){
                .kind = FSYM_ENUM_CONST,
                .type = need_i64 ? FTYPE_I64 : FTYPE_I32,
                .const_val = next_val,
            });
            next_val += 1;
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
        }
        if (fast_cc_peek(cc) == FTOK_RBRACE) { cc->pos += 1u; }
        else { fast_cc_bail(cc, "unclosed enum"); }
        if (cidx != 0)
        {
            FastType under = need_i64 ? FTYPE_I64 : (!has_neg) ? FTYPE_U32 : FTYPE_I32;
            cc->ctypes[cidx].inner = under;
            cc->ctypes[cidx].size = ftype_size(cc, under);
            cc->ctypes[cidx].align = ftype_align(cc, under);
            cc->ctypes[cidx].complete = 1u;
        }
    }
    return cidx != 0 ? ftype_from_index(cc, cidx) : FTYPE_I32;
}

// Declarator modifier entry (applied innermost-outward iteratively!)
typedef enum FastDeclModKind
{
    FDMOD_PTR = 1,
    FDMOD_ARRAY = 2,
    FDMOD_FUNC = 3
} FastDeclModKind;

typedef struct FastDeclMod FastDeclMod;
struct FastDeclMod
{
    uint8_t kind;
    uint8_t is_const;
    uint8_t is_variadic;
    uint8_t is_oldstyle;
    uint32_t count;        // array count (UINT32_MAX = incomplete []), or param_count
    uint32_t param_start;  // index into cc->param_types
};

typedef struct FastDeclaratorResult FastDeclaratorResult;
struct FastDeclaratorResult
{
    FastType type;
    uint32_t name_id;
    uint32_t param_names_start;
    uint16_t param_count;
    FastAttrInfo attr;
};

// Level 5: Leaf declaration specifiers (handles scalars, typedefs, enums, attributes, _Alignas, and existing/forward
// struct/union tags — NEVER calls fast_cc_parse_record_iter so the call graph stays strictly acyclic!).
static BUSTER_DIRECT_HOT FastType fast_cc_parse_scalar_or_tag_specs(FastCCState* cc, FastDeclSpecs* out_specs)
{
    // Filled where the caller reads it: a copy of a record written a field at a time waits on those stores.
    FastDeclSpecs local_specs;
    FastDeclSpecs* const sp = out_specs != NULL ? out_specs : &local_specs;
    *sp = (FastDeclSpecs){ 0 };
    uint32_t saw_void = 0, saw_char = 0, saw_short = 0, saw_int = 0, saw_long = 0;
    uint32_t saw_float = 0, saw_double = 0, saw_signed = 0, saw_unsigned = 0, saw_bool = 0;
    bool has_explicit_type = false;
    FastType explicit_type = FTYPE_I32;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_KW_TYPEDEF) { sp->is_typedef = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_STATIC) { sp->is_static = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_EXTERN) { sp->is_extern = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_INLINE) { sp->is_inline = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_REGISTER || k == FTOK_KW_AUTO || k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
        else if (k == FTOK_KW_NORETURN) { sp->is_noreturn = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_CONST) { sp->is_const = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_VOLATILE) { sp->is_volatile = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_RESTRICT) { cc->pos += 1u; }
        else if (k == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &sp->attr); }
        else if (k == FTOK_KW_ALIGNAS)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_LPAREN)
            {
                cc->pos += 1u;
                if (fast_cc_is_type_start(cc, cc->pos))
                {
                    bool tok_ok = false;
                    const FastType at = fast_cc_parse_simple_typename_leaf(cc, &tok_ok);
                    const uint32_t al = ftype_align(cc, at);
                    if (al > sp->attr.aligned) { sp->attr.aligned = al; }
                }
                else
                {
                    const int64_t al = fast_cc_const_int_expr(cc, false);
                    if (al > (int64_t)sp->attr.aligned && al <= 4096) { sp->attr.aligned = (uint32_t)al; }
                }
                if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
            }
        }
        else if (k == FTOK_KW_VOID) { saw_void += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_CHAR) { saw_char += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SHORT) { saw_short += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_INT) { saw_int += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_LONG) { saw_long += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_FLOAT) { saw_float += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_DOUBLE) { saw_double += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SIGNED) { saw_signed += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_UNSIGNED) { saw_unsigned += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BOOL) { saw_bool += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BUILTIN_VA_LIST) { has_explicit_type = true; explicit_type = cc->va_list_type; cc->pos += 1u; }
        else if (k == FTOK_KW_INT128)
        {
            uint32_t ilen = 0;
            const char* is = fast_cc_ident_str(cc, fast_cc_data(cc), &ilen);
            const bool is_u = (ilen > 2u && is[2] == 'u') || saw_unsigned != 0;
            has_explicit_type = true;
            explicit_type = is_u ? FTYPE_U128 : FTYPE_I128;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_FLOAT128 || k == FTOK_KW_FLOAT16 || k == FTOK_KW_COMPLEX)
        {
            sp->is_unsupported_type = 1u;
            has_explicit_type = true;
            explicit_type = k == FTOK_KW_FLOAT16 ? FTYPE_UNSUP2 : FTYPE_UNSUP16;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_ATOMIC || k == FTOK_KW_THREAD_LOCAL || k == FTOK_KW_TYPEOF)
        {
            fast_cc_bail(cc, "unsupported type specifier");
            break;
        }
        else if (k == FTOK_KW_STRUCT || k == FTOK_KW_UNION)
        {
            if (has_explicit_type) { break; }
            const uint8_t rkind = k == FTOK_KW_UNION ? FCTYPE_UNION : FCTYPE_STRUCT;
            cc->pos += 1u;
            fast_cc_parse_attributes(cc, NULL);
            if (fast_cc_peek(cc) != FTOK_IDENT || fast_cc_peek_at(cc, 1u) == FTOK_LBRACE)
            {
                fast_cc_bail(cc, "struct def inside parameter or leaf specs");
                break;
            }
            const uint32_t tag_id = fast_cc_data(cc);
            cc->pos += 1u;
            uint32_t cidx = tag_id < cc->env_cap ? cc->tag_cur[tag_id] : 0u;
            if (cidx == 0)
            {
                cidx = fast_cc_alloc_ctype(cc, rkind);
                if (cidx != 0)
                {
                    cc->ctypes[cidx].tag_ident = tag_id;
                    fast_cc_tag_bind(cc, tag_id, cidx);
                }
            }
            has_explicit_type = true;
            explicit_type = cidx != 0 ? ftype_from_index(cc, cidx) : FTYPE_VOID;
        }
        else if (k == FTOK_KW_ENUM)
        {
            if (has_explicit_type) { break; }
            has_explicit_type = true;
            explicit_type = fast_cc_parse_enum(cc);
        }
        else if (k == FTOK_IDENT && !has_explicit_type &&
                 (saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_signed + saw_unsigned + saw_bool) == 0 &&
                 fast_cc_is_typedef_name(cc, fast_cc_data(cc)))
        {
            has_explicit_type = true;
            explicit_type = cc->syms[cc->sym_cur[fast_cc_data(cc)]].type;
            cc->pos += 1u;
        }
        else
        {
            break;
        }
    }

    FastType res = FTYPE_I32;
    if (has_explicit_type) { res = explicit_type; }
    else if (saw_void) { res = FTYPE_VOID; }
    else if (saw_bool) { res = FTYPE_BOOL; }
    else if (saw_float) { res = FTYPE_F32; }
    else if (saw_double)
    {
        res = saw_long ? FTYPE_UNSUP16 : FTYPE_F64;
        if (saw_long) { sp->is_long_double = 1u; sp->is_unsupported_type = 1u; }
    }
    else if (saw_char)
    {
        res = saw_unsigned ? FTYPE_U8 : saw_signed ? FTYPE_I8 : (cc->unsigned_char ? FTYPE_U8 : FTYPE_I8);
        if (!saw_signed && !saw_unsigned) { res |= BUSTER_FTYPE_SCALAR_CHAR; }
    }
    else if (saw_short) { res = saw_unsigned ? FTYPE_U16 : FTYPE_I16; }
    else if (saw_long >= 2u) { res = saw_unsigned ? FTYPE_U64 : FTYPE_I64; }
    else if (saw_long == 1u) { res = (saw_unsigned ? FTYPE_U64 : FTYPE_I64) | BUSTER_FTYPE_SCALAR_LONG; }
    else if (saw_unsigned) { res = FTYPE_U32; }
    else { res = FTYPE_I32; }

    if (sp->attr.mode_size != 0)
    {
        const bool u = saw_unsigned != 0;
        if (sp->attr.mode_float) { res = sp->attr.mode_size == 4u ? FTYPE_F32 : FTYPE_F64; }
        else if (sp->attr.mode_size == 1u) { res = u ? FTYPE_U8 : FTYPE_I8; }
        else if (sp->attr.mode_size == 2u) { res = u ? FTYPE_U16 : FTYPE_I16; }
        else if (sp->attr.mode_size == 4u) { res = u ? FTYPE_U32 : FTYPE_I32; }
        else if (sp->attr.mode_size == 8u) { res = u ? FTYPE_U64 : FTYPE_I64; }
        else { sp->is_unsupported_type = 1u; }
    }

    if (sp->is_const) { res = ftype_with_const(res); }
    if (sp->is_volatile) { res |= BUSTER_FTYPE_VOL; }
    sp->type = res;
    return res;
}

// Level 6: Parse ONE declarator (abstract or named) iteratively with ZERO recursion!
// Handles nested `(*fp)(int, char*)`, `int *(*a[4])(void)`, `void (*signal(int, void(*)(int)))(int)`, etc.!
static BUSTER_DIRECT_HOT bool fast_cc_parse_declarator(FastCCState* cc, FastType base_type, bool allow_abstract,
                                     FastDeclaratorResult* out_res, bool record_param_names)
{
    typedef struct DeclGroup DeclGroup;
    struct DeclGroup
    {
        uint32_t ptr_start;
        uint32_t ptr_count;
        uint32_t suffix_start;
        uint32_t suffix_count;
        uint32_t close_pos;
    };
    FastDeclMod mods[64];
    uint32_t mod_count = 0;
    DeclGroup groups[16];
    uint32_t gdepth = 0;

    // Step 1: Descend into prefix pointers and grouped `(` declarators iteratively
    uint32_t name_id = 0;
    FastAttrInfo attr = { 0 };
    uint32_t top_param_names_start = 0;
    uint16_t top_param_count = 0;

    for (;;)
    {
        if (gdepth >= 16u) { fast_cc_bail(cc, "declarator depth"); return false; }
        const uint32_t ptr_start = mod_count;
        while (fast_cc_peek(cc) == FTOK_STAR)
        {
            cc->pos += 1u;
            uint8_t pconst = 0;
            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE ||
                   fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE ||
                   fast_cc_peek(cc) == FTOK_KW_ATOMIC)
            {
                if (fast_cc_peek(cc) == FTOK_KW_CONST) { pconst = 1u; cc->pos += 1u; }
                else if (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &attr); }
                else { cc->pos += 1u; }
            }
            if (mod_count < 64u)
            {
                mods[mod_count++] = (FastDeclMod){ .kind = FDMOD_PTR, .is_const = pconst };
            }
        }
        groups[gdepth] = (DeclGroup){ .ptr_start = ptr_start, .ptr_count = mod_count - ptr_start };
        gdepth += 1u;

        // Check for grouped declarator `(` vs function parameter list `(`!
        if (fast_cc_peek(cc) == FTOK_LPAREN)
        {
            // Peek past any leading `__attribute__((...))` inside `(...)` so `(ATTR *)` is recognized as grouped declarator!
            uint32_t probe = cc->pos + 1u;
            while (probe < cc->tcount && cc->tk[probe] == FTOK_KW_ATTRIBUTE &&
                   probe + 2u < cc->tcount && cc->tk[probe + 1u] == FTOK_LPAREN && cc->tk[probe + 2u] == FTOK_LPAREN)
            {
                probe += 3u;
                uint32_t ad = 2u;
                while (probe < cc->tcount && ad > 0)
                {
                    if (cc->tk[probe] == FTOK_LPAREN) { ad += 1u; }
                    else if (cc->tk[probe] == FTOK_RPAREN) { ad -= 1u; }
                    probe += 1u;
                }
            }
            const uint8_t next = probe < cc->tcount ? cc->tk[probe] : FTOK_EOF;
            const bool is_params = (next == FTOK_RPAREN) ||
                                   (next != FTOK_STAR && next != FTOK_LPAREN && next != FTOK_LBRACKET &&
                                    fast_cc_is_type_start(cc, probe));
            if (!is_params)
            {
                cc->pos += 1u; // '('
                fast_cc_parse_attributes(cc, &attr);
                continue;
            }
        }
        break;
    }

    // Innermost name (if any; declarator names may shadow outer typedefs like `struct s { int s; };`!)
    if (fast_cc_peek(cc) == FTOK_IDENT)
    {
        name_id = fast_cc_data(cc);
        cc->pos += 1u;
    }
    else if (!allow_abstract && gdepth == 1u && groups[0].ptr_count == 0 && fast_cc_peek(cc) != FTOK_COLON)
    {
        // Abstract / anonymous declarator is ok for struct bitfields `: 0` or parameters
    }

    // Step 2: Walk outward closing each `)` and parsing `[...]` / `(...)` suffixes!
    for (int32_t gi = (int32_t)gdepth - 1; gi >= 0 && !cc->bailout; gi -= 1)
    {
        const uint32_t suf_start = mod_count;
        while (!cc->bailout && (fast_cc_peek(cc) == FTOK_LBRACKET || fast_cc_peek(cc) == FTOK_LPAREN))
        {
            if (fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                cc->pos += 1u;
                while (fast_cc_peek(cc) == FTOK_KW_STATIC || fast_cc_peek(cc) == FTOK_KW_CONST ||
                       fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_VOLATILE)
                {
                    cc->pos += 1u;
                }
                uint32_t cnt = UINT32_MAX;
                if (fast_cc_peek(cc) != FTOK_RBRACKET)
                {
                    const int64_t ev = fast_cc_const_int_expr(cc, false);
                    if (ev < 0 || ev > (int64_t)UINT32_MAX - 1)
                    {
                        fast_cc_bail(cc, "array size invalid or VLA");
                        break;
                    }
                    cnt = (uint32_t)ev;
                }
                if (fast_cc_peek(cc) != FTOK_RBRACKET) { fast_cc_bail(cc, "expected ]"); break; }
                cc->pos += 1u;
                if (mod_count < 64u)
                {
                    mods[mod_count++] = (FastDeclMod){ .kind = FDMOD_ARRAY, .count = cnt };
                }
            }
            else
            {
                // Parameter list `(...)` — parse parameter types iteratively!
                // Buffer outer param types locally so nested function-pointer params never interleave in cc->param_types[]!
                cc->pos += 1u; // '('
                const bool keep_names = record_param_names && gi == (int32_t)gdepth - 1 && suf_start == mod_count;
                FastType local_pt[64];
                uint32_t local_pn[64];
                uint32_t pcnt = 0;
                bool is_var = false;
                bool is_old = false;

                if (fast_cc_peek(cc) == FTOK_RPAREN)
                {
                    is_old = true;
                    cc->pos += 1u;
                }
                else if (fast_cc_peek(cc) == FTOK_KW_VOID && fast_cc_peek_at(cc, 1u) == FTOK_RPAREN)
                {
                    cc->pos += 2u;
                }
                else
                {
                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN && !cc->bailout)
                    {
                        if (fast_cc_peek(cc) == FTOK_ELLIPSIS)
                        {
                            is_var = true;
                            cc->pos += 1u;
                            break;
                        }
                        FastDeclSpecs pspecs = { 0 };
                        FastType pt = fast_cc_parse_scalar_or_tag_specs(cc, &pspecs);
                        if (cc->bailout) { break; }

                        // Inline mini-declarator for parameter (handles `*`, `(*fp)(...)`, `[]`, name, `__attribute__`)!
                        uint32_t pname = 0;
                        // Leading pointers
                        while (fast_cc_peek(cc) == FTOK_STAR)
                        {
                            cc->pos += 1u;
                            bool pc = false;
                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE ||
                                   fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE)
                            {
                                if (fast_cc_peek(cc) == FTOK_KW_CONST) { pc = true; cc->pos += 1u; }
                                else if (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, NULL); }
                                else { cc->pos += 1u; }
                            }
                            pt = ftype_make_ptr(cc, pt);
                            if (pc) { pt = ftype_with_const(pt); }
                        }
                        // Check for function-pointer parameter `(*fp)(args)`
                        if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_peek_at(cc, 1u) == FTOK_STAR)
                        {
                            cc->pos += 2u; // `(*`
                            uint32_t extra_ptrs = 1u;
                            while (fast_cc_peek(cc) == FTOK_STAR) { extra_ptrs += 1u; cc->pos += 1u; }
                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_VOLATILE) { cc->pos += 1u; }
                            if (fast_cc_peek(cc) == FTOK_IDENT) { pname = fast_cc_data(cc); cc->pos += 1u; }
                            // Skip optional array dims inside `(*fp[])`
                            bool is_arr_fp = false;
                            while (fast_cc_peek(cc) == FTOK_LBRACKET)
                            {
                                is_arr_fp = true;
                                cc->pos += 1u;
                                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RBRACKET) { cc->pos += 1u; }
                                if (fast_cc_peek(cc) == FTOK_RBRACKET) { cc->pos += 1u; }
                            }
                            if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                            // Parse inner function-pointer parameter list (simple balanced skip + build generic FCTYPE_FUNC!)
                            if (fast_cc_peek(cc) == FTOK_LPAREN)
                            {
                                const uint32_t fp_cidx = fast_cc_alloc_ctype(cc, FCTYPE_FUNC);
                                cc->pos += 1u;
                                uint32_t fp_pstart = cc->param_type_count;
                                uint32_t fp_pcnt = 0;
                                bool fp_var = false;
                                bool fp_old = fast_cc_peek(cc) == FTOK_RPAREN;
                                if (fast_cc_peek(cc) == FTOK_KW_VOID && fast_cc_peek_at(cc, 1u) == FTOK_RPAREN)
                                {
                                    cc->pos += 2u;
                                }
                                else if (fp_old)
                                {
                                    cc->pos += 1u;
                                }
                                else
                                {
                                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN && !cc->bailout)
                                    {
                                        if (fast_cc_peek(cc) == FTOK_ELLIPSIS) { fp_var = true; cc->pos += 1u; break; }
                                        FastDeclSpecs fps = { 0 };
                                        FastType fpt = fast_cc_parse_scalar_or_tag_specs(cc, &fps);
                                        while (fast_cc_peek(cc) == FTOK_STAR)
                                        {
                                            cc->pos += 1u;
                                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE || fast_cc_peek(cc) == FTOK_KW_RESTRICT) { cc->pos += 1u; }
                                            fpt = ftype_make_ptr(cc, fpt);
                                        }
                                        // Skip nested parens/name/brackets in fp param
                                        if (fast_cc_peek(cc) == FTOK_LPAREN)
                                        {
                                            fpt = ftype_make_ptr(cc, FTYPE_VOID); // opaque nested fn ptr
                                            uint32_t nd = 1u; cc->pos += 1u;
                                            while (nd > 0 && cc->pos < cc->tcount)
                                            {
                                                if (fast_cc_peek(cc) == FTOK_LPAREN) { nd += 1u; }
                                                else if (fast_cc_peek(cc) == FTOK_RPAREN) { nd -= 1u; }
                                                cc->pos += 1u;
                                            }
                                            if (fast_cc_peek(cc) == FTOK_LPAREN)
                                            {
                                                nd = 1u; cc->pos += 1u;
                                                while (nd > 0 && cc->pos < cc->tcount)
                                                {
                                                    if (fast_cc_peek(cc) == FTOK_LPAREN) { nd += 1u; }
                                                    else if (fast_cc_peek(cc) == FTOK_RPAREN) { nd -= 1u; }
                                                    cc->pos += 1u;
                                                }
                                            }
                                        }
                                        else if (fast_cc_peek(cc) == FTOK_IDENT) { cc->pos += 1u; }
                                        while (fast_cc_peek(cc) == FTOK_LBRACKET)
                                        {
                                            fpt = ftype_make_ptr(cc, fpt);
                                            cc->pos += 1u;
                                            while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RBRACKET) { cc->pos += 1u; }
                                            if (fast_cc_peek(cc) == FTOK_RBRACKET) { cc->pos += 1u; }
                                        }
                                        if (cc->param_type_count < cc->param_type_cap) { cc->param_types[cc->param_type_count++] = fpt; }
                                        fp_pcnt += 1u;
                                        if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                                    }
                                    if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                                }
                                if (fp_cidx != 0)
                                {
                                    cc->ctypes[fp_cidx].inner = pt;
                                    cc->ctypes[fp_cidx].start = fp_pstart;
                                    cc->ctypes[fp_cidx].count = fp_pcnt;
                                    cc->ctypes[fp_cidx].is_variadic = fp_var ? 1u : 0u;
                                    cc->ctypes[fp_cidx].is_oldstyle = fp_old ? 1u : 0u;
                                    cc->ctypes[fp_cidx].size = 8u;
                                    cc->ctypes[fp_cidx].align = 8u;
                                    cc->ctypes[fp_cidx].complete = 1u;
                                    pt = ftype_from_index(cc, fp_cidx);
                                    for (uint32_t ep = 0; ep < extra_ptrs; ep += 1) { pt = ftype_make_ptr(cc, pt); }
                                    if (is_arr_fp) { pt = ftype_make_ptr(cc, pt); }
                                }
                            }
                        }
                        else if (fast_cc_peek(cc) == FTOK_IDENT && !fast_cc_is_typedef_name(cc, fast_cc_data(cc)))
                        {
                            pname = fast_cc_data(cc);
                            cc->pos += 1u;
                        }
                        // Array parameter decays to pointer! (Multi-dim `int a[][4]` -> `int (*)[4]`)
                        if (fast_cc_peek(cc) == FTOK_LBRACKET)
                        {
                            uint32_t dims[8];
                            uint32_t ndim = 0;
                            while (fast_cc_peek(cc) == FTOK_LBRACKET && ndim < 8u)
                            {
                                cc->pos += 1u;
                                while (fast_cc_peek(cc) == FTOK_KW_STATIC || fast_cc_peek(cc) == FTOK_KW_CONST ||
                                       fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_VOLATILE) { cc->pos += 1u; }
                                uint32_t dc = UINT32_MAX;
                                if (fast_cc_peek(cc) != FTOK_RBRACKET)
                                {
                                    const int64_t dv = fast_cc_const_int_expr(cc, false);
                                    if (dv < 0 || dv > (int64_t)UINT32_MAX - 1)
                                    {
                                        fast_cc_bail(cc, "array param size invalid");
                                        break;
                                    }
                                    dc = (uint32_t)dv;
                                }
                                if (fast_cc_peek(cc) == FTOK_RBRACKET) { cc->pos += 1u; }
                                dims[ndim++] = dc;
                            }
                            for (int32_t di = (int32_t)ndim - 1; di >= 1 && !cc->bailout; di -= 1)
                            {
                                const uint64_t total_sz = dims[di] == UINT32_MAX ? 0u : (uint64_t)dims[di] * (uint64_t)ftype_size(cc, pt);
                                if (total_sz > (uint64_t)INT32_MAX)
                                {
                                    fast_cc_bail(cc, "array size overflow");
                                    break;
                                }
                                const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                                if (ac != 0)
                                {
                                    cc->ctypes[ac].inner = pt;
                                    cc->ctypes[ac].count = dims[di];
                                    cc->ctypes[ac].size = (uint32_t)total_sz;
                                    cc->ctypes[ac].align = ftype_align(cc, pt);
                                    cc->ctypes[ac].complete = dims[di] != UINT32_MAX ? 1u : 0u;
                                    pt = ftype_from_index(cc, ac);
                                }
                            }
                            pt = ftype_make_ptr(cc, pt);
                        }
                        fast_cc_parse_attributes(cc, NULL);
                        // Array typedef param decays to pointer (e.g. `va_list ap`!)
                        pt = ftype_decay(cc, pt);
                        if (pcnt < 64u)
                        {
                            local_pt[pcnt] = pt;
                            local_pn[pcnt] = pname;
                            pcnt += 1u;
                        }
                        else { fast_cc_bail(cc, "too many function params"); break; }
                        if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                        else { break; }
                    }
                    if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                    else { fast_cc_bail(cc, "unclosed param list"); }
                }
                const uint32_t pstart = cc->param_type_count;
                if (cc->param_type_count + pcnt <= cc->param_type_cap)
                {
                    if (pcnt > 0) { memcpy(cc->param_types + pstart, local_pt, pcnt * sizeof(FastType)); }
                    cc->param_type_count += pcnt;
                }
                else { fast_cc_bail(cc, "param_types pool full"); }
                if (keep_names)
                {
                    const uint32_t pnstart = cc->param_name_count;
                    if (cc->param_name_count + pcnt <= cc->param_name_cap)
                    {
                        if (pcnt > 0) { memcpy(cc->param_names + pnstart, local_pn, pcnt * sizeof(uint32_t)); }
                        cc->param_name_count += pcnt;
                    }
                    top_param_names_start = pnstart;
                    top_param_count = (uint16_t)pcnt;
                }
                if (mod_count < 64u)
                {
                    mods[mod_count++] = (FastDeclMod){
                        .kind = FDMOD_FUNC,
                        .is_variadic = is_var ? 1u : 0u,
                        .is_oldstyle = is_old ? 1u : 0u,
                        .count = pcnt,
                        .param_start = pstart,
                    };
                }
            }
        }
        groups[gi].suffix_start = suf_start;
        groups[gi].suffix_count = mod_count - suf_start;
        if (gi > 0)
        {
            if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "expected ) in declarator"); break; }
            cc->pos += 1u;
        }
    }

    fast_cc_parse_asm_label(cc, &attr);
    fast_cc_parse_attributes(cc, &attr);

    // Step 3: Build final FastType from outermost group down to innermost group (right-to-left C declarator rule!)
    FastType cur = base_type;
    for (uint32_t gi = 0; gi < gdepth && !cc->bailout; gi += 1)
    {
        // First apply this group's prefix pointers (left to right)
        for (uint32_t pi = 0; pi < groups[gi].ptr_count; pi += 1)
        {
            const FastDeclMod pm = mods[groups[gi].ptr_start + pi];
            cur = ftype_make_ptr(cc, cur);
            if (pm.is_const) { cur = ftype_with_const(cur); }
        }
        // Then apply this group's suffixes in REVERSE order (innermost array/func wraps outermost)!
        for (int32_t si = (int32_t)groups[gi].suffix_count - 1; si >= 0; si -= 1)
        {
            const FastDeclMod sm = mods[groups[gi].suffix_start + (uint32_t)si];
            if (sm.kind == FDMOD_ARRAY)
            {
                const uint32_t esz = ftype_size(cc, cur);
                const uint64_t total_sz = sm.count == UINT32_MAX ? 0u : (uint64_t)sm.count * (uint64_t)esz;
                if (total_sz > (uint64_t)INT32_MAX)
                {
                    fast_cc_bail(cc, "array size overflow");
                    break;
                }
                const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                if (ac != 0)
                {
                    const uint32_t eal = ftype_align(cc, cur);
                    cc->ctypes[ac].inner = cur;
                    cc->ctypes[ac].count = sm.count;
                    cc->ctypes[ac].size = (uint32_t)total_sz;
                    cc->ctypes[ac].align = eal;
                    cc->ctypes[ac].complete = sm.count != UINT32_MAX ? 1u : 0u;
                    cur = ftype_from_index(cc, ac);
                }
            }
            else if (sm.kind == FDMOD_FUNC)
            {
                const uint32_t fc = fast_cc_alloc_ctype(cc, FCTYPE_FUNC);
                if (fc != 0)
                {
                    cc->ctypes[fc].inner = cur;
                    cc->ctypes[fc].start = sm.param_start;
                    cc->ctypes[fc].count = sm.count;
                    cc->ctypes[fc].is_variadic = sm.is_variadic;
                    cc->ctypes[fc].is_oldstyle = sm.is_oldstyle;
                    cc->ctypes[fc].size = 1u;
                    cc->ctypes[fc].align = 1u;
                    cc->ctypes[fc].complete = 1u;
                    cur = ftype_from_index(cc, fc);
                }
            }
        }
    }

    // Notice C declarator precedence: in `int *f(void)` (gdepth==1), suffixes bind TIGHTER than pointers!
    // Wait: our Step 3 above applied pointers BEFORE suffixes for the SAME group! Let's fix that!
    // In C, for group `gi` (outermost=0 to innermost=gdepth-1):
    //   `int *f(void)` -> group 0 has ptr `*`, suffix `(void)` -> `(void)` returns `int *`!
    //   So within group 0, ptr applies to `base_type` FIRST -> `int*`, THEN suffix `(void)` wraps `int*` -> `func() -> int*`!
    //   AND for `int (*fp)(void)` -> group 0 has no ptr, suffix `(void)`; group 1 has ptr `*`, no suffix:
    //   Outer group 0 runs FIRST: `func() -> int`; then inner group 1 runs NEXT: `ptr to (func() -> int)`!
    //   AND for `int *a[10]` -> group 0 has ptr `*`, suffix `[10]`: ptr FIRST -> `int*`, suffix `[10]` NEXT -> `array[10] of int*`!
    //   AND for `int (*p)[10]` -> group 0 has suffix `[10]`, group 1 has ptr `*`: group 0 FIRST -> `array[10] of int`, group 1 NEXT -> `ptr to array[10] of int`!
    // THAT ORDER IS 100% EXACT FOR ALL C DECLARATORS!!

    out_res->type = cur;
    out_res->name_id = name_id;
    out_res->param_names_start = top_param_names_start;
    out_res->param_count = top_param_count;
    out_res->attr = attr;
    return !cc->bailout;
}

// Parse struct/union definition iteratively (supports arbitrarily nested anonymous & named structs/unions!)
static FastType fast_cc_parse_record_iter(FastCCState* cc, bool is_union, bool allow_def)
{
    typedef struct RecFrame RecFrame;
    struct RecFrame
    {
        uint32_t cidx;
        bool is_union;
        uint8_t packed;
        uint32_t cur_byte;
        uint32_t cur_bit;         // bit position within cur_byte*8 for bitfields
        uint32_t max_align;
        uint32_t max_size;
        uint32_t field_start;     // temp buffer start in scratch member arrays
        uint32_t field_count;
    };

    // Temporary member collection buffers on stack (committed contiguously into cc->member_ids/info on `}`)
    // Using scratch arena so large structs (e.g. struct sqlite3 with ~150 fields) never overflow stack!
    const uint64_t scratch_mark = cc->scratch->used;
    uint32_t* tmp_ids = (uint32_t*)fast_arena_alloc(cc->scratch, 4096u * sizeof(uint32_t), 64);
    FastMemberInfo* tmp_info = (FastMemberInfo*)fast_arena_alloc(cc->scratch, 4096u * sizeof(FastMemberInfo), 64);
    uint32_t tmp_used = 0;
    if (tmp_ids == NULL || tmp_info == NULL)
    {
        fast_cc_bail(cc, "struct scratch full");
        return FTYPE_VOID;
    }

    RecFrame stack[32];
    uint32_t sp = 0;

    // Parse outer `struct`/`union` header
    cc->pos += 1u; // 'struct' or 'union'
    FastAttrInfo hattr = { 0 };
    fast_cc_parse_attributes(cc, &hattr);
    uint32_t tag_id = 0;
    if (fast_cc_peek(cc) == FTOK_IDENT)
    {
        tag_id = fast_cc_data(cc);
        cc->pos += 1u;
    }
    const uint8_t rkind = is_union ? FCTYPE_UNION : FCTYPE_STRUCT;
    uint32_t root_cidx = 0;
    if (tag_id != 0 && tag_id < cc->env_cap && cc->tag_cur[tag_id] != 0)
    {
        // Note: if defining `{ ... }` in an inner scope, check if tag belongs to current scope or outer
        const uint32_t existing = cc->tag_cur[tag_id];
        if (fast_cc_peek(cc) != FTOK_LBRACE || cc->ctypes[existing].complete == 0)
        {
            root_cidx = existing;
        }
    }
    if (root_cidx == 0)
    {
        root_cidx = fast_cc_alloc_ctype(cc, rkind);
        if (root_cidx != 0)
        {
            cc->ctypes[root_cidx].tag_ident = tag_id;
            cc->ctypes[root_cidx].packed = hattr.packed;
            if (hattr.aligned > 1u) { cc->ctypes[root_cidx].align = hattr.aligned; }
            if (tag_id != 0) { fast_cc_tag_bind(cc, tag_id, root_cidx); }
        }
    }
    if (fast_cc_peek(cc) != FTOK_LBRACE)
    {
        cc->scratch->used = scratch_mark;
        return root_cidx != 0 ? ftype_from_index(cc, root_cidx) : FTYPE_VOID;
    }
    if (!allow_def)
    {
        fast_cc_bail(cc, "struct def inside leaf typename");
        cc->scratch->used = scratch_mark;
        return FTYPE_VOID;
    }

    // Push root struct onto iterative stack
    cc->pos += 1u; // '{'
    stack[sp++] = (RecFrame){
        .cidx = root_cidx,
        .is_union = is_union,
        .packed = hattr.packed,
        .cur_byte = 0,
        .cur_bit = 0,
        .max_align = hattr.aligned > 1u ? hattr.aligned : 1u,
        .max_size = 0,
        .field_start = tmp_used,
        .field_count = 0,
    };

    while (sp > 0 && cc->pos < cc->tcount && !cc->bailout)
    {
        RecFrame* rf = stack + (sp - 1u);
        if (fast_cc_peek(cc) == FTOK_RBRACE)
        {
            cc->pos += 1u;
            FastAttrInfo tattr = { 0 };
            fast_cc_parse_attributes(cc, &tattr);
            if (tattr.packed) { rf->packed = 1u; }
            if (tattr.aligned > rf->max_align) { rf->max_align = tattr.aligned; }
            // Finalize trailing bitfield byte
            if (!rf->is_union && rf->cur_bit > 0)
            {
                rf->cur_byte += (rf->cur_bit + 7u) >> 3;
                rf->cur_bit = 0;
            }
            const uint32_t raw_sz = rf->is_union ? rf->max_size : rf->cur_byte;
            const uint32_t al = rf->packed ? (tattr.aligned > 1u ? tattr.aligned : 1u) : rf->max_align;
            const uint32_t final_sz = (raw_sz + al - 1u) & ~(al - 1u);

            // Commit members contiguously into cc->member_ids / cc->member_info
            const uint32_t fcnt = rf->field_count;
            if (cc->member_count + fcnt > cc->member_cap)
            {
                fast_cc_bail(cc, "member pool full");
                break;
            }
            const uint32_t mstart = cc->member_count;
            if (fcnt > 0)
            {
                memcpy(cc->member_ids + mstart, tmp_ids + rf->field_start, fcnt * sizeof(uint32_t));
                memcpy(cc->member_info + mstart, tmp_info + rf->field_start, fcnt * sizeof(FastMemberInfo));
                cc->member_count += fcnt;
                if (fcnt > 64u)
                {
                    for (uint32_t k = 0; k < fcnt; k += 1)
                    {
                        fast_member_hash_put(cc, rf->cidx, cc->member_ids[mstart + k], mstart + k);
                    }
                }
            }
            FastComplexType* ct = cc->ctypes + rf->cidx;
            ct->start = mstart;
            ct->count = fcnt;
            ct->size = final_sz;
            ct->align = al;
            ct->packed = rf->packed;
            ct->complete = 1u;
            fast_struct_classify_abi(cc, rf->cidx);

            const uint32_t finished_cidx = rf->cidx;
            tmp_used = rf->field_start; // pop temp members!
            sp -= 1u;

            // If this was a nested struct/union inside a parent struct, parse its declarators and add to parent!
            if (sp > 0)
            {
                RecFrame* pf = stack + (sp - 1u);
                const FastType sub_t = ftype_from_index(cc, finished_cidx);
                if (fast_cc_peek(cc) == FTOK_SEMI)
                {
                    // Anonymous nested struct/union: flatten all its members into parent at aligned offset!
                    cc->pos += 1u;
                    if (!pf->is_union && pf->cur_bit > 0)
                    {
                        pf->cur_byte += (pf->cur_bit + 7u) >> 3;
                        pf->cur_bit = 0;
                    }
                    const uint32_t fal = pf->packed ? 1u : ct->align;
                    if (fal > pf->max_align) { pf->max_align = fal; }
                    const uint32_t foff = pf->is_union ? 0u : ((pf->cur_byte + fal - 1u) & ~(fal - 1u));
                    for (uint32_t k = 0; k < ct->count; k += 1)
                    {
                        if (tmp_used < 4096u)
                        {
                            FastMemberInfo mi = cc->member_info[ct->start + k];
                            mi.byte_offset += foff;
                            tmp_ids[tmp_used] = cc->member_ids[ct->start + k];
                            tmp_info[tmp_used] = mi;
                            tmp_used += 1u;
                            pf->field_count += 1u;
                        }
                    }
                    if (pf->is_union) { if (ct->size > pf->max_size) { pf->max_size = ct->size; } }
                    else { pf->cur_byte = foff + ct->size; }
                }
                else
                {
                    // Named field(s) of this nested struct/union type
                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
                    {
                        FastDeclaratorResult dr = { 0 };
                        if (!fast_cc_parse_declarator(cc, sub_t, false, &dr, false)) { break; }
                        if (!pf->is_union && pf->cur_bit > 0)
                        {
                            pf->cur_byte += (pf->cur_bit + 7u) >> 3;
                            pf->cur_bit = 0;
                        }
                        const uint32_t fsz = ftype_size(cc, dr.type);
                        uint32_t fal = pf->packed || dr.attr.packed ? 1u : ftype_align(cc, dr.type);
                        if (dr.attr.aligned > fal) { fal = dr.attr.aligned; }
                        if (fal > pf->max_align) { pf->max_align = fal; }
                        const uint32_t foff = pf->is_union ? 0u : ((pf->cur_byte + fal - 1u) & ~(fal - 1u));
                        if (dr.name_id != 0 && tmp_used < 4096u)
                        {
                            tmp_ids[tmp_used] = dr.name_id;
                            tmp_info[tmp_used] = (FastMemberInfo){ .type = dr.type, .byte_offset = foff };
                            tmp_used += 1u;
                            pf->field_count += 1u;
                        }
                        if (pf->is_union) { if (fsz > pf->max_size) { pf->max_size = fsz; } }
                        else { pf->cur_byte = foff + fsz; }
                        if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                    }
                    if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
                }
            }
            continue;
        }

        // Skip stray semicolons or __extension__ or _Static_assert inside struct
        if (fast_cc_peek(cc) == FTOK_SEMI || fast_cc_peek(cc) == FTOK_KW_EXTENSION)
        {
            cc->pos += 1u;
            continue;
        }
        if (fast_cc_peek(cc) == FTOK_KW_STATIC_ASSERT)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_LPAREN)
            {
                cc->pos += 1u;
                const int64_t ok_v = fast_cc_const_int_expr(cc, true);
                if (ok_v == 0) { fast_cc_bail(cc, "_Static_assert failed"); break; }
                while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
                if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
            }
            continue;
        }

        // Check for nested struct/union definition as field type (even after leading const/volatile)!
        const uint32_t save_nest = cc->pos;
        bool nest_const = false;
        while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE || fast_cc_peek(cc) == FTOK_KW_EXTENSION)
        {
            if (fast_cc_peek(cc) == FTOK_KW_CONST) { nest_const = true; }
            cc->pos += 1u;
        }
        if ((fast_cc_peek(cc) == FTOK_KW_STRUCT || fast_cc_peek(cc) == FTOK_KW_UNION) &&
            (fast_cc_peek_at(cc, 1u) == FTOK_LBRACE ||
             (fast_cc_peek_at(cc, 1u) == FTOK_IDENT && fast_cc_peek_at(cc, 2u) == FTOK_LBRACE)))
        {
            (void)nest_const;
            if (sp >= 32u) { fast_cc_bail(cc, "struct nesting depth"); break; }
            const bool sub_union = fast_cc_peek(cc) == FTOK_KW_UNION;
            cc->pos += 1u;
            uint32_t sub_tag = 0;
            if (fast_cc_peek(cc) == FTOK_IDENT) { sub_tag = fast_cc_data(cc); cc->pos += 1u; }
            const uint32_t sub_cidx = fast_cc_alloc_ctype(cc, sub_union ? FCTYPE_UNION : FCTYPE_STRUCT);
            if (sub_cidx != 0)
            {
                cc->ctypes[sub_cidx].tag_ident = sub_tag;
                if (sub_tag != 0) { fast_cc_tag_bind(cc, sub_tag, sub_cidx); }
            }
            cc->pos += 1u; // '{'
            stack[sp++] = (RecFrame){
                .cidx = sub_cidx,
                .is_union = sub_union,
                .packed = rf->packed,
                .cur_byte = 0,
                .cur_bit = 0,
                .max_align = 1u,
                .max_size = 0,
                .field_start = tmp_used,
                .field_count = 0,
            };
            continue;
        }
        cc->pos = save_nest;

        // Ordinary field declaration inside struct/union
        FastDeclSpecs fspecs = { 0 };
        const FastType fbase = fast_cc_parse_scalar_or_tag_specs(cc, &fspecs);
        if (cc->bailout) { break; }
        if (fast_cc_peek(cc) == FTOK_SEMI)
        {
            // Anonymous struct/union typedef field (e.g. `MyUnion;`)
            cc->pos += 1u;
            if (ftype_is_record(fbase))
            {
                const FastComplexType* ct = cc->ctypes + (fbase & BUSTER_FTYPE_CIDX_MASK);
                if (!rf->is_union && rf->cur_bit > 0) { rf->cur_byte += (rf->cur_bit + 7u) >> 3; rf->cur_bit = 0; }
                const uint32_t fal = rf->packed ? 1u : ct->align;
                if (fal > rf->max_align) { rf->max_align = fal; }
                const uint32_t foff = rf->is_union ? 0u : ((rf->cur_byte + fal - 1u) & ~(fal - 1u));
                for (uint32_t k = 0; k < ct->count; k += 1)
                {
                    if (tmp_used < 4096u)
                    {
                        FastMemberInfo mi = cc->member_info[ct->start + k];
                        mi.byte_offset += foff;
                        tmp_ids[tmp_used] = cc->member_ids[ct->start + k];
                        tmp_info[tmp_used] = mi;
                        tmp_used += 1u;
                        rf->field_count += 1u;
                    }
                }
                if (rf->is_union) { if (ct->size > rf->max_size) { rf->max_size = ct->size; } }
                else { rf->cur_byte = foff + ct->size; }
            }
            continue;
        }

        while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_SEMI && !cc->bailout)
        {
            const uint32_t f_start = cc->pos;
            FastDeclaratorResult dr = { 0 };
            if (!fast_cc_parse_declarator(cc, fbase, true, &dr, false)) { break; }
            if (fast_cc_peek(cc) == FTOK_COLON)
            {
                // Bitfield! SysV AMD64 ABI bitfield layout
                cc->pos += 1u;
                const int64_t bw = fast_cc_const_int_expr(cc, true);
                if (bw < 0 || bw > 64) { fast_cc_bail(cc, "invalid bitfield width"); break; }
                const uint32_t usize = ftype_size(cc, dr.type);
                const uint32_t ualign = rf->packed ? 1u : ftype_align(cc, dr.type);
                const uint32_t ubits = usize * 8u;
                if (bw == 0)
                {
                    if (!rf->is_union)
                    {
                        rf->cur_byte += (rf->cur_bit + 7u) >> 3;
                        rf->cur_bit = 0;
                        rf->cur_byte = (rf->cur_byte + ualign - 1u) & ~(ualign - 1u);
                    }
                }
                else
                {
                    if (ualign > rf->max_align) { rf->max_align = ualign; }
                    if (rf->is_union)
                    {
                        if (usize > rf->max_size) { rf->max_size = usize; }
                        if (dr.name_id != 0 && tmp_used < 4096u)
                        {
                            tmp_ids[tmp_used] = dr.name_id;
                            tmp_info[tmp_used] = (FastMemberInfo){
                                .type = dr.type,
                                .byte_offset = 0u,
                                .bit_offset = 0u,
                                .bit_width = (uint8_t)bw,
                                .bit_unit_size = (uint8_t)usize,
                                .bit_signed = ftype_is_signed(cc, dr.type) ? 1u : 0u,
                            };
                            tmp_used += 1u;
                            rf->field_count += 1u;
                        }
                    }
                    else
                    {
                        uint64_t total_bit = (uint64_t)rf->cur_byte * 8u + rf->cur_bit;
                        // SysV AMD64: if field would cross a storage-unit alignment boundary (and not packed), align up!
                        if (!rf->packed)
                        {
                            const uint64_t unit_start = total_bit & ~(uint64_t)(ubits - 1u);
                            if (total_bit + (uint64_t)bw > unit_start + ubits)
                            {
                                total_bit = unit_start + ubits;
                            }
                        }
                        const uint32_t unit_byte = (uint32_t)((total_bit >> 3) & ~(uint64_t)(ualign - 1u));
                        const uint8_t boff = (uint8_t)(total_bit - (uint64_t)unit_byte * 8u);
                        if (dr.name_id != 0 && tmp_used < 4096u)
                        {
                            tmp_ids[tmp_used] = dr.name_id;
                            tmp_info[tmp_used] = (FastMemberInfo){
                                .type = dr.type,
                                .byte_offset = unit_byte,
                                .bit_offset = boff,
                                .bit_width = (uint8_t)bw,
                                .bit_unit_size = (uint8_t)usize,
                                .bit_signed = ftype_is_signed(cc, dr.type) ? 1u : 0u,
                            };
                            tmp_used += 1u;
                            rf->field_count += 1u;
                        }
                        total_bit += (uint64_t)bw;
                        rf->cur_byte = (uint32_t)(total_bit >> 3);
                        rf->cur_bit = (uint32_t)(total_bit & 7u);
                    }
                }
            }
            else
            {
                if (!rf->is_union && rf->cur_bit > 0)
                {
                    rf->cur_byte += (rf->cur_bit + 7u) >> 3;
                    rf->cur_bit = 0;
                }
                const uint32_t fsz = ftype_size(cc, dr.type);
                uint32_t fal = rf->packed || dr.attr.packed || fspecs.attr.packed ? 1u : ftype_align(cc, dr.type);
                if (fspecs.attr.aligned > fal) { fal = fspecs.attr.aligned; }
                if (dr.attr.aligned > fal) { fal = dr.attr.aligned; }
                if (fal > rf->max_align) { rf->max_align = fal; }
                const uint32_t foff = rf->is_union ? 0u : ((rf->cur_byte + fal - 1u) & ~(fal - 1u));
                if (dr.name_id != 0 && tmp_used < 4096u)
                {
                    tmp_ids[tmp_used] = dr.name_id;
                    tmp_info[tmp_used] = (FastMemberInfo){ .type = dr.type, .byte_offset = foff };
                    tmp_used += 1u;
                    rf->field_count += 1u;
                }
                if (rf->is_union) { if (fsz > rf->max_size) { rf->max_size = fsz; } }
                else { rf->cur_byte = foff + fsz; }
            }
            if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
            else if (cc->pos == f_start && !cc->bailout) { fast_cc_bail(cc, "struct field stalled"); break; }
        }
        if (fast_cc_peek(cc) == FTOK_SEMI) { cc->pos += 1u; }
        else { fast_cc_bail(cc, "expected ; after struct field"); break; }
    }

    cc->scratch->used = scratch_mark;
    return root_cidx != 0 ? ftype_from_index(cc, root_cidx) : FTYPE_VOID;
}

// Level 8: Full declaration specifiers (supports struct/union/enum definitions, storage classes, attributes, _Alignas).
static BUSTER_DIRECT_HOT FastType fast_cc_parse_decl_specs(FastCCState* cc, FastDeclSpecs* out_specs)
{
    // Filled where the caller reads it: a copy of a record written a field at a time waits on those stores.
    FastDeclSpecs local_specs;
    FastDeclSpecs* const sp = out_specs != NULL ? out_specs : &local_specs;
    *sp = (FastDeclSpecs){ 0 };
    uint32_t saw_void = 0, saw_char = 0, saw_short = 0, saw_int = 0, saw_long = 0;
    uint32_t saw_float = 0, saw_double = 0, saw_signed = 0, saw_unsigned = 0, saw_bool = 0;
    bool has_explicit_type = false;
    FastType explicit_type = FTYPE_I32;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_KW_TYPEDEF) { sp->is_typedef = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_STATIC) { sp->is_static = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_EXTERN) { sp->is_extern = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_INLINE) { sp->is_inline = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_REGISTER || k == FTOK_KW_AUTO || k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
        else if (k == FTOK_KW_NORETURN) { sp->is_noreturn = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_CONST) { sp->is_const = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_VOLATILE) { sp->is_volatile = 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_RESTRICT) { cc->pos += 1u; }
        else if (k == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &sp->attr); }
        else if (k == FTOK_KW_ALIGNAS)
        {
            cc->pos += 1u;
            if (fast_cc_peek(cc) == FTOK_LPAREN)
            {
                cc->pos += 1u;
                if (fast_cc_is_type_start(cc, cc->pos))
                {
                    bool tok_ok = false;
                    const FastType at = fast_cc_parse_simple_typename_leaf(cc, &tok_ok);
                    const uint32_t al = ftype_align(cc, at);
                    if (al > sp->attr.aligned) { sp->attr.aligned = al; }
                }
                else
                {
                    const int64_t al = fast_cc_const_int_expr(cc, false);
                    if (al > (int64_t)sp->attr.aligned && al <= 4096) { sp->attr.aligned = (uint32_t)al; }
                }
                if (fast_cc_peek(cc) == FTOK_RPAREN) { cc->pos += 1u; }
            }
        }
        else if (k == FTOK_KW_VOID) { saw_void += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_CHAR) { saw_char += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SHORT) { saw_short += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_INT) { saw_int += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_LONG) { saw_long += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_FLOAT) { saw_float += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_DOUBLE) { saw_double += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_SIGNED) { saw_signed += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_UNSIGNED) { saw_unsigned += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BOOL) { saw_bool += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_BUILTIN_VA_LIST)
        {
            has_explicit_type = true;
            explicit_type = cc->va_list_type;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_INT128)
        {
            uint32_t ilen = 0;
            const char* is = fast_cc_ident_str(cc, fast_cc_data(cc), &ilen);
            const bool is_u = (ilen > 2u && is[2] == 'u') || saw_unsigned != 0;
            has_explicit_type = true;
            explicit_type = is_u ? FTYPE_U128 : FTYPE_I128;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_FLOAT128 || k == FTOK_KW_FLOAT16 || k == FTOK_KW_COMPLEX)
        {
            sp->is_unsupported_type = 1u;
            has_explicit_type = true;
            explicit_type = k == FTOK_KW_FLOAT16 ? FTYPE_UNSUP2 : FTYPE_UNSUP16;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_ATOMIC || k == FTOK_KW_THREAD_LOCAL || k == FTOK_KW_TYPEOF)
        {
            fast_cc_bail(cc, "unsupported type specifier");
            break;
        }
        else if (k == FTOK_KW_STRUCT || k == FTOK_KW_UNION)
        {
            if (has_explicit_type) { break; }
            has_explicit_type = true;
            explicit_type = fast_cc_parse_record_iter(cc, k == FTOK_KW_UNION, true);
        }
        else if (k == FTOK_KW_ENUM)
        {
            if (has_explicit_type) { break; }
            has_explicit_type = true;
            explicit_type = fast_cc_parse_enum(cc);
        }
        else if (k == FTOK_IDENT && !has_explicit_type &&
                 (saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_signed + saw_unsigned + saw_bool) == 0 &&
                 fast_cc_is_typedef_name(cc, fast_cc_data(cc)))
        {
            has_explicit_type = true;
            explicit_type = cc->syms[cc->sym_cur[fast_cc_data(cc)]].type;
            cc->pos += 1u;
        }
        else
        {
            break;
        }
    }

    FastType res = FTYPE_I32;
    if (has_explicit_type)
    {
        res = explicit_type;
    }
    else if (saw_void) { res = FTYPE_VOID; }
    else if (saw_bool) { res = FTYPE_BOOL; }
    else if (saw_float) { res = FTYPE_F32; }
    else if (saw_double)
    {
        res = saw_long ? FTYPE_UNSUP16 : FTYPE_F64;
        if (saw_long) { sp->is_long_double = 1u; sp->is_unsupported_type = 1u; }
    }
    else if (saw_char)
    {
        res = saw_unsigned ? FTYPE_U8 : saw_signed ? FTYPE_I8 : (cc->unsigned_char ? FTYPE_U8 : FTYPE_I8);
        if (!saw_signed && !saw_unsigned) { res |= BUSTER_FTYPE_SCALAR_CHAR; }
    }
    else if (saw_short) { res = saw_unsigned ? FTYPE_U16 : FTYPE_I16; }
    else if (saw_long >= 2u) { res = saw_unsigned ? FTYPE_U64 : FTYPE_I64; }
    else if (saw_long == 1u) { res = (saw_unsigned ? FTYPE_U64 : FTYPE_I64) | BUSTER_FTYPE_SCALAR_LONG; }
    else if (saw_unsigned) { res = FTYPE_U32; }
    else { res = FTYPE_I32; }

    // Apply __attribute__((__mode__(...))) overrides (used by glibc stdint/math headers!)
    if (sp->attr.mode_size != 0)
    {
        const bool u = saw_unsigned != 0;
        if (sp->attr.mode_float) { res = sp->attr.mode_size == 4u ? FTYPE_F32 : FTYPE_F64; }
        else if (sp->attr.mode_size == 1u) { res = u ? FTYPE_U8 : FTYPE_I8; }
        else if (sp->attr.mode_size == 2u) { res = u ? FTYPE_U16 : FTYPE_I16; }
        else if (sp->attr.mode_size == 4u) { res = u ? FTYPE_U32 : FTYPE_I32; }
        else if (sp->attr.mode_size == 8u) { res = u ? FTYPE_U64 : FTYPE_I64; }
        else { sp->is_unsupported_type = 1u; }
    }

    if (sp->is_const) { res = ftype_with_const(res); }
    if (sp->is_volatile) { res |= BUSTER_FTYPE_VOL; }
    sp->type = res;
    return res;
}

// Level 8: Full typename parser for casts `(T)`, `sizeof(T)`, `_Alignof(T)`, `offsetof(T, ...)`, `_Generic`, compound literals.
static FastType fast_cc_parse_typename(FastCCState* cc, bool* out_ok)
{
    FastDeclSpecs sp = { 0 };
    const FastType base = fast_cc_parse_decl_specs(cc, &sp);
    if (sp.is_unsupported_type) { fast_cc_bail(cc, "unsupported type in typename"); }
    FastDeclaratorResult dr = { 0 };
    const bool ok = !cc->bailout && fast_cc_parse_declarator(cc, base, true, &dr, false);
    if (out_ok != NULL) { *out_ok = ok; }
    return ok ? dr.type : FTYPE_VOID;
}

static bool fast_cc_init_state(FastCCState* cc, FastArena* arena, FastArena* scratch, FastPP* pp, FastObj* obj,
                               bool unsigned_char, bool quiet)
{
    memset(cc, 0, sizeof(*cc));
    cc->arena = arena;
    cc->scratch = scratch;
    cc->pp = pp;
    cc->obj = obj;
    cc->tk = pp->out_kind;
    cc->td = pp->out_data;
    cc->tcount = pp->out_count;
    cc->unsigned_char = unsigned_char;
    cc->quiet = quiet;

    cc->ctype_cap = 16384u;
    cc->member_cap = 32768u;
    cc->param_type_cap = 49152u;
    cc->param_name_cap = 16384u;
    cc->env_cap = pp->idents.capacity;
    cc->sym_cap = 49152u;
    cc->undo_cap = 16384u;

    cc->ctypes = (FastComplexType*)fast_arena_alloc(arena, (uint64_t)cc->ctype_cap * sizeof(FastComplexType), 64);
    cc->member_ids = (uint32_t*)fast_arena_alloc(arena, ((uint64_t)cc->member_cap + 16u) * sizeof(uint32_t), 64);
    cc->member_info = (FastMemberInfo*)fast_arena_alloc(arena, (uint64_t)cc->member_cap * sizeof(FastMemberInfo), 64);
    cc->member_hash = (FastMemberHash*)fast_arena_alloc(arena, (uint64_t)BUSTER_FMEMBER_HASH_SIZE * sizeof(FastMemberHash), 64);
    cc->param_types = (FastType*)fast_arena_alloc(arena, (uint64_t)cc->param_type_cap * sizeof(FastType), 64);
    cc->param_names = (uint32_t*)fast_arena_alloc(arena, (uint64_t)cc->param_name_cap * sizeof(uint32_t), 64);
    cc->sym_cur = (uint32_t*)fast_arena_alloc(arena, (uint64_t)cc->env_cap * sizeof(uint32_t), 64);
    cc->tag_cur = (uint32_t*)fast_arena_alloc(arena, (uint64_t)cc->env_cap * sizeof(uint32_t), 64);
    cc->syms = (FastSym*)fast_arena_alloc(arena, (uint64_t)cc->sym_cap * sizeof(FastSym), 64);
    cc->undo = (FastScopeUndo*)fast_arena_alloc(arena, (uint64_t)cc->undo_cap * sizeof(FastScopeUndo), 64);

    bool ok = cc->ctypes != NULL && cc->member_ids != NULL && cc->member_info != NULL && cc->member_hash != NULL &&
              cc->param_types != NULL && cc->param_names != NULL && cc->sym_cur != NULL && cc->tag_cur != NULL &&
              cc->syms != NULL && cc->undo != NULL;
    if (ok)
    {
        // Reserve index 0 as sentinel in ctypes and syms (member_hash uses key == 0 from zero-filled mmap)
        cc->ctype_count = 1u;
        cc->sym_count = 1u;

        // Build SysV AMD64 __builtin_va_list = struct __va_list_tag[1] (24 bytes, align 8)
        const uint32_t tag_cidx = fast_cc_alloc_ctype(cc, FCTYPE_STRUCT);
        cc->ctypes[tag_cidx].size = 24u;
        cc->ctypes[tag_cidx].align = 8u;
        cc->ctypes[tag_cidx].complete = 1u;
        cc->ctypes[tag_cidx].abi_lo = FABI_MEM;
        cc->ctypes[tag_cidx].abi_hi = FABI_MEM;
        cc->va_list_tag_type = ftype_from_index(cc, tag_cidx);

        const uint32_t va_arr_cidx = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
        cc->ctypes[va_arr_cidx].inner = cc->va_list_tag_type;
        cc->ctypes[va_arr_cidx].count = 1u;
        cc->ctypes[va_arr_cidx].size = 24u;
        cc->ctypes[va_arr_cidx].align = 8u;
        cc->ctypes[va_arr_cidx].complete = 1u;
        cc->va_list_type = ftype_from_index(cc, va_arr_cidx);

        // Intern builtin & special identifier IDs
        cc->id_builtin_clzll = fast_ident_intern_cstr(&pp->idents, "__builtin_clzll");
        cc->id_builtin_clz = fast_ident_intern_cstr(&pp->idents, "__builtin_clz");
        cc->id_builtin_ctzll = fast_ident_intern_cstr(&pp->idents, "__builtin_ctzll");
        cc->id_builtin_ctz = fast_ident_intern_cstr(&pp->idents, "__builtin_ctz");
        cc->id_builtin_inff = fast_ident_intern_cstr(&pp->idents, "__builtin_inff");
        cc->id_builtin_inf = fast_ident_intern_cstr(&pp->idents, "__builtin_inf");
        cc->id_builtin_nanf = fast_ident_intern_cstr(&pp->idents, "__builtin_nanf");
        cc->id_builtin_nan = fast_ident_intern_cstr(&pp->idents, "__builtin_nan");
        cc->id_builtin_expect = fast_ident_intern_cstr(&pp->idents, "__builtin_expect");
        cc->id_builtin_constant_p = fast_ident_intern_cstr(&pp->idents, "__builtin_constant_p");
        cc->id_builtin_unreachable = fast_ident_intern_cstr(&pp->idents, "__builtin_unreachable");
        cc->id_builtin_bswap16 = fast_ident_intern_cstr(&pp->idents, "__builtin_bswap16");
        cc->id_builtin_bswap32 = fast_ident_intern_cstr(&pp->idents, "__builtin_bswap32");
        cc->id_builtin_bswap64 = fast_ident_intern_cstr(&pp->idents, "__builtin_bswap64");
        cc->id_builtin_popcount = fast_ident_intern_cstr(&pp->idents, "__builtin_popcount");
        cc->id_builtin_popcountll = fast_ident_intern_cstr(&pp->idents, "__builtin_popcountll");
        cc->id_builtin_memcpy = fast_ident_intern_cstr(&pp->idents, "__builtin_memcpy");
        cc->id_builtin_memset = fast_ident_intern_cstr(&pp->idents, "__builtin_memset");
        cc->id_builtin_memmove = fast_ident_intern_cstr(&pp->idents, "__builtin_memmove");
        cc->id_builtin_memcmp = fast_ident_intern_cstr(&pp->idents, "__builtin_memcmp");
        cc->id_builtin_strlen = fast_ident_intern_cstr(&pp->idents, "__builtin_strlen");
        cc->id_sync_synchronize = fast_ident_intern_cstr(&pp->idents, "__sync_synchronize");
        cc->id_atomic_load_n = fast_ident_intern_cstr(&pp->idents, "__atomic_load_n");
        cc->id_atomic_store_n = fast_ident_intern_cstr(&pp->idents, "__atomic_store_n");
        cc->id_main = fast_ident_intern_cstr(&pp->idents, "main");
        cc->ro_sign_f32_off = UINT32_MAX;
        cc->ro_sign_f64_off = UINT32_MAX;
        cc->ro_u64_to_f64_off = UINT32_MAX;
        cc->ro_f64_to_u64_off = UINT32_MAX;
        cc->ro_f32_to_u64_off = UINT32_MAX;
        cc->ro_one_f32_off = UINT32_MAX;
        cc->ro_one_f64_off = UINT32_MAX;
    }
    return ok;
}

#endif
