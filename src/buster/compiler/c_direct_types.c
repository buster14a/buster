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
// Bits 29 and 30, free in both forms, are a pointer's pointee volatile and a qualifier between a deeper pointer's
// levels, which the encoding otherwise drops (#3577).

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
// #3577: a long double, among the 16-byte scalars the direct path doesn't compile (FTYPE_UNSUP16: _Float128 and _Complex
// too), is the one with the long mark: a checked inline body takes it without code (fast_cc_check_body).
#define BUSTER_FTYPE_LONG_DOUBLE     (FTYPE_UNSUP16 | BUSTER_FTYPE_SCALAR_LONG)

#define BUSTER_FTYPE_CIDX_MASK       UINT32_C(0x000fffff)
#define BUSTER_FTYPE_PTR_SH          20u
#define BUSTER_FTYPE_PTR_MASK        UINT32_C(0x00700000)
#define BUSTER_FTYPE_CONST           UINT32_C(0x00800000)
#define BUSTER_FTYPE_PCONST          UINT32_C(0x01000000)
#define BUSTER_FTYPE_VOL             UINT32_C(0x02000000)

#define BUSTER_FTYPE_KIND_SH         26u
#define BUSTER_FTYPE_KIND_MASK       UINT32_C(0x1c000000)

// A pointer's pointee volatile (ptr_depth == 1, as PCONST), and a qualifier of a level a deeper pointer can't keep, which
// marks the type so that a conversion that would drop either finds it (#3577).
#define BUSTER_FTYPE_PVOL            UINT32_C(0x20000000)
#define BUSTER_FTYPE_QDEEP           UINT32_C(0x40000000)

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
    uint8_t bit_signed;    // 1 if signed bitfield; a member that isn't one: BUSTER_FMEMBER_UNALIGNED (#3577)
};

// #3577: in a member's bit_signed, a member that isn't a bit-field and sits below its type's alignment.
#define BUSTER_FMEMBER_UNALIGNED 2u

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
    uint8_t is_variadic;   // for FCTYPE_FUNC; a struct or union's: 1 for an aligned typedef's copy (#3577, below)
    uint8_t abi_lo;        // FastAbiClass for bytes 0..7
    uint8_t abi_hi;        // FastAbiClass for bytes 8..15
    uint8_t is_oldstyle;   // for FCTYPE_FUNC with empty ()
    uint8_t has_const_mbr;
    uint32_t size;
    uint32_t align;
    FastType inner;        // elem type (ARRAY), ret type (FUNC), pointee (PTR), underlying (ENUM)
    uint32_t count;        // elem count (ARRAY), param count (FUNC), member count (STRUCT/UNION), enumerator count (ENUM)
    uint32_t start;        // param_start (FUNC), member_start (STRUCT/UNION), the first enumerator's symbol (ENUM)
    uint32_t tag_ident;    // tag IdentId if any; FCTYPE_FUNC: its nonnull parameters (bit i: parameter i + 1, #3577)
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

// A symbol's use flags (#3577), tested at once where an expression names it, so that the common use pays one test:
// a type the direct path doesn't compile, a declaration whose use is the full pipeline's warning (deprecated, a
// function whose calls it checks), and a register variable, whose address is an error.
#define BUSTER_FSYM_UNSUPPORTED 0x01u // its type is one the direct path doesn't compile
#define BUSTER_FSYM_DEPRECATED  0x02u // __attribute__((deprecated)): a use is -Wdeprecated-declarations
#define BUSTER_FSYM_FORMAT      0x04u // a function whose calls the full pipeline's -Wformat checks
#define BUSTER_FSYM_NONNULL     0x08u // __attribute__((nonnull)): a null constant argument is -Wnonnull
#define BUSTER_FSYM_UNUSED_RESULT 0x10u // __attribute__((warn_unused_result)): a discarded call is -Wunused-result
#define BUSTER_FSYM_REGISTER    0x20u // a register variable: taking its address is an error
#define BUSTER_FSYM_PURE        0x40u // __attribute__((const)) or ((pure)): a discarded call is -Wunused-value
#define BUSTER_FSYM_ALIGNED     0x80u // a typedef with an alignment of its own, which this path keeps none of, or with a mode it
                                      // doesn't model (#3577, BUSTER_FMODE_UNMODELED): a use is refused
// #3577: a mode attribute's size this path doesn't model (TI, TF, TC, XF, HF, the vector modes): the specifiers' type keeps
// its base kind, unsupported, and is refused where its size would count.
#define BUSTER_FMODE_UNMODELED  16u
// A symbol's push_marks (#3577), the value marks (c_direct_cc.c's BUSTER_FVAL_MARK_*) a use by name gives without a
// branch: an enumerator's is a nonliteral, a library function the driver calls const or pure is pure; and until a
// function's purity is asked (fast_cc_library_pure_query, at its first use), UNASKED, a mark no use by name carries.
#define BUSTER_FSYM_PUSH_UNASKED    0x01u
// A library function whose calls the full pipeline checks by its name (#3577: -Wsizeof-pointer-memaccess's memcpy family,
// -Wabsolute-value's abs family), marked as a nonnull one is, so that its calls take the call's cold argument check.
#define BUSTER_FSYM_PUSH_CHECKED    0x02u
#define BUSTER_FSYM_PUSH_PURE       0x10u
#define BUSTER_FSYM_PUSH_NONLITERAL 0x20u
// A parameter's name in cc->param_names, whose high bit marks one declared an array, and the next one declared register
// (#3577): its address is the full pipeline's error, as a register local's is.
#define BUSTER_FPARAM_NAME_MASK 0x3fffffffu
#define BUSTER_FPARAM_REGISTER  0x40000000u

// #3577: what a variable's value is to the full pipeline's constant folding, which its -Wdivision-by-zero runs on a
// divisor (clang's EvaluateAsInt reads the initializer of an object that is const and not volatile), in FastSym's
// pad_sym[1]: not foldable (not const, volatile, or without an initializer at that point); foldable to a value that may
// be zero (zero, or one this path doesn't follow: a floating or pointer value, an aggregate's); an integer known not zero.
#define BUSTER_FFOLD_NONE    0u
#define BUSTER_FFOLD_ZERO    1u
#define BUSTER_FFOLD_NONZERO 2u

// The symbols a checked inline body may give their first object symbols (fast_cc_check_body); more are refused.
#define BUSTER_FCHECKED_OBJ_SYMS 512u

typedef struct FastSym FastSym;
struct FastSym
{
    uint8_t kind;          // FastSymKind
    uint8_t storage;       // 0=default, 1=static, 2=extern, 3=inline, 4=static_inline
    uint8_t defined;       // 1 if body/initializer defined; 2 an inline body checked, not (yet) emitted (#3577)
    uint8_t referenced;    // 1 if referenced (for dead static elimination / diagnostics); 2 used in a checked body alone
    uint8_t is_tentative;  // 1 if uninitialized file-scope tentative definition
    uint8_t is_const_var;  // 1 if const-qualified
    uint8_t is_noreturn;
    uint8_t is_weak;
    uint8_t use_flags;     // BUSTER_FSYM_*: what a use of the symbol must look at first (one test at every use)
    uint8_t visibility_set; // a declaration gave the visibility, so another one that differs is clang's error (#3595)
    uint8_t inline_rules;   // BUSTER_FINLINE_*: what a function's declarations say of its inline definition (#3595)
    uint8_t push_marks;     // the marks a use by name gives its value (#3577): BUSTER_FSYM_PUSH_*
    uint8_t is_alias;       // an alias attribute defines it at the unit's end (#3592)
    uint8_t visibility;    // STV_DEFAULT / STV_HIDDEN / STV_PROTECTED
    uint8_t scope_depth;   // the scope it was declared in (0 = file scope), for a redeclaration in the same one (#3577)
    uint8_t pad_sym[2];    // [0] a function's library purity (fast_cc_library_pure_query), a parameter's declared array (1);
                           // [1] a variable's BUSTER_FFOLD_*
    uint16_t deferred_task; // one-based index of a deferred definition, zero for other symbols
    FastType type;
    uint32_t ident_id;
    uint32_t obj_sym;      // symbol index in FastObj (for GLOBAL/FUNC)
    int32_t stack_off;     // rbp-relative offset (for LOCAL, negative for locals, >=16 for stack params)
    uint32_t section;      // FastSecId if defined global
    uint32_t sec_off;      // byte offset within section
    uint32_t asm_name_len;
    int64_t const_val;     // value (for FSYM_ENUM_CONST)
    const char* asm_name;  // optional __asm__("label") override
};

// A symbol fits a cache line, as before #3577's fields (cc->syms is 64-byte aligned): the push of a name reads most of it.
_Static_assert(sizeof(FastSym) == 64u, "symbol entry size");

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
    uint32_t decl_start;   // token index of the definition's specifiers (__PRETTY_FUNCTION__, #3592)
    uint32_t body_start;   // token index of '{'
    uint32_t param_names_start;
    uint16_t param_count;
    uint16_t previous_task;
    uint8_t declared;      // a declaration came before the definition (__PRETTY_FUNCTION__)
};

#define BUSTER_FDEFERRED_LIMIT 4096u

// A declaration with GNU's alias attribute (#3592): its symbol, defined at the end of the unit at its target's place,
// and the target's name as the attribute spells it (the target's ELF name, which a C name without an asm label is).
typedef struct FastAlias FastAlias;
struct FastAlias
{
    uint32_t sym_idx;
    uint32_t target_idx;   // the target's FastSym, found before the reachable bodies are compiled
    const char* target;
    uint32_t target_len;
};

#define BUSTER_FALIAS_LIMIT 1024u

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
    uint32_t* member_stamp; // ident_id -> the last record check that saw it as a member's name (#3577)
    uint32_t member_serial;
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
    uint32_t param_depth;  // parameter lists being parsed: a tag first declared there is -Wvisibility (#3577)

    // Local static counter for unique internal names
    uint32_t static_local_seq;

    // Special cached IdentIds (va_list, builtins, attributes)
    uint32_t id_builtin_clzll;
    uint32_t id_builtin_clz;
    uint32_t id_builtin_ctzll;
    uint32_t id_builtin_ctz;
    uint32_t id_builtin_inff;
    uint32_t id_builtin_huge_val;  // HUGE_VAL, __builtin_inf's value (#3592)
    uint32_t id_builtin_huge_valf;
    uint32_t id_builtin_inf;
    uint32_t id_builtin_ceil;      // computed inline, as the full pipeline does (#3592)
    uint32_t id_builtin_floor;
    uint32_t id_builtin_ceilf;
    uint32_t id_builtin_floorf;
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
    uint32_t id_pretty_function; // __PRETTY_FUNCTION__, which shares __func__'s token kind (#3618)
    uint32_t id_assert_fail;     // glibc's assert's (#3592)
    uint32_t id_void;
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
    uint32_t cur_func_decl_pos;    // the definition's first token, its specifiers' (__PRETTY_FUNCTION__, #3592)
    uint32_t cur_func_body_pos;    // its body's `{`
    const char* cur_func_pretty;   // its __PRETTY_FUNCTION__ once printed (c_direct_pretty.c), else NULL
    uint32_t cur_func_pretty_length;
    uint8_t cur_func_declared;     // a declaration came before the definition
    FastType cur_ret_type;
    uint32_t cur_ret_label;
    int32_t cur_sret_off;      // [rbp + cur_sret_off] hidden pointer for >16B struct return (0 if none)
    int32_t cur_frame_size;    // grows downward as locals are allocated (positive byte count)
    int32_t cur_tmp_off;       // temporary spill offset within current statement (positive byte count)
    int32_t cur_max_frame;     // high-water mark of cur_frame_size + spills across function
    int32_t cur_max_call_stack;
    int32_t cur_va_save_off;   // [rbp + cur_va_save_off] 176B register save area for variadic functions
    uint32_t cur_va_gp_init;   // initial gp_offset (8 * num_gp_params)
    uint32_t cur_last_param;   // the function's last named parameter, va_start's second argument (#3577)
    uint8_t cur_variadic;      // the function takes `...`: va_start may be used (#3577)
    uint8_t cur_array_params;  // a parameter is declared an array (its symbol's pad_sym[0]): sizeof looks (#3577)
    uint8_t init_kind;         // the last scalar initializer's FastValKind and value, for a const local's fold class (#3577)
    int64_t init_value;
    uint32_t cur_va_fp_init;   // initial fp_offset (48 + 16 * num_fp_params)
    int32_t cur_va_overflow_off; // rbp + 16 + overflow_bytes

    // Dead code (#3592): how many `if` branches around the statement are checked without code, and what the last
    // branch condition folded to (BUSTER_FCONSTANT_*).
    uint32_t dead;
    uint8_t branch_constant;

    // GNU's alias attribute (#3592): the declarations that have one, and how many the attribute parser saw, so that one
    // anywhere else (a block, a parameter, a definition) leaves the direct path.
    FastAlias* aliases;
    uint32_t alias_count;
    uint32_t alias_attributes;

    // Cached .rodata constants (UINT32_MAX = not yet emitted)
    uint32_t ro_sign_f32_off;
    uint32_t ro_sign_f64_off;
    uint32_t ro_u64_to_f64_off; // 0x43f0000000000000 (2^64) for u64 -> double/float
    uint32_t ro_f64_to_u64_off; // 0x43e0000000000000 (2^63) for double -> u64
    uint32_t ro_f32_to_u64_off; // 0x5f000000 (2^63f) for float -> u64
    uint32_t ro_one_f32_off;
    uint32_t ro_one_f64_off;

    // #3577: an inline body compiled to be checked where it is defined (fast_cc_check_body), and the symbols whose first
    // object symbol that compile created, which go with it.
    uint8_t checking;
    uint32_t checked_obj_sym_count;
    uint32_t checked_obj_syms[BUSTER_FCHECKED_OBJ_SYMS];
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
    // The kind's byte alone: Clang widened its load to the record's first four bytes, whose shift drops the other
    // three, and a record just made (fast_cc_alloc_ctype: the kind's byte, eight zero bytes from offset 1, then the
    // complete flag's) holds those four bytes in no one store, so the load waited for them to retire (#3577: 0.38 M
    // cycles a compile of sqlite3.c on the 9700X). The empty asm takes the zero-extended byte.
    uint32_t kind = cc->ctypes[idx].kind;
    __asm__("" : "+r"(kind));
    return BUSTER_FTYPE_COMPLEX_BIT | (kind << BUSTER_FTYPE_KIND_SH) | idx;
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

// ftype_unqual in place, for the paths every call argument takes (#3577).
#define BUSTER_FTYPE_UNQUAL(t) ((FastType)(t) & ~(BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL))

static inline FastType ftype_unqual(FastType t)
{
    return t & ~(BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL);
}

static inline FastType ftype_with_const(FastType t)
{
    return t | BUSTER_FTYPE_CONST;
}

// The type without the qualifier records #3577 added (a pointee's volatile, a deeper level's qualifier), which no code
// generation reads: comparisons that choose code compare types this way, as they did before the bits existed, so that
// a value's code is the same with them.
// (A macro, not a function: it is in every conversion's path, and computed in place it is a few instructions.)
#define BUSTER_FTYPE_CODEGEN(t) ((t) & ~(BUSTER_FTYPE_PVOL | BUSTER_FTYPE_QDEEP))

// #3577: whether an integer constant VALUE, from a source of scalar kind SKIND, converts quietly to a scalar target of kind
// DKIND (FTYPE_BOOL to FTYPE_F64) beyond doubt, whatever the finer rules (fast_cc_constant_conversion_quiet): from -128
// to 127 into any; in the int32 range into a 32- or 64-bit integer, _Bool or double; and from 0 to 2^32 - 1 into an
// integer whose width holds it as a non-negative value. An unsigned 64-bit source's bits of 2^63 or more are none of
// these. 1 or 0, branch-free: constants are converted by the ten thousand.
// The three ranges' union is one interval for each target kind, [lo, hi] below (an empty one, 1 to 0, for the kinds none
// of them takes): two compares and the unsigned 64-bit source's test.
static const int64_t fast_int_settled_lo[32] = {
    1, INT32_MIN, -128, -128, -128, -128, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN, -128, INT32_MIN, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static const int64_t fast_int_settled_hi[32] = {
    0, INT32_MAX, 127, 255, 32767, 65535, INT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 127, INT32_MAX, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
#define BUSTER_FAST_INT_SETTLED(value, skind, dkind) \
    ((uint32_t)(((skind) != FTYPE_U64) | ((value) >= 0)) & (uint32_t)((int64_t)(value) >= fast_int_settled_lo[(dkind) & 31u]) & \
     (uint32_t)((int64_t)(value) <= fast_int_settled_hi[(dkind) & 31u]))

// A pointer's own volatile (`int *volatile`), which a deeper pointer records as a qualifier it drops (#3577).
static inline FastType ftype_with_volatile(FastType t)
{
    return ftype_is_complex(t) ? (t | BUSTER_FTYPE_VOL) : (t | BUSTER_FTYPE_VOL);
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

// The class of operand C's operators take (#3577), branch-free: 0 an integer (an enum's too), 1 a floating value, 2 a
// pointer (an array's or a function's too, which decay to one), 3 anything else: void, a struct or union, a scalar
// this path doesn't support.
#define BUSTER_FOPERAND_INTEGER  0u
#define BUSTER_FOPERAND_FLOATING 1u
#define BUSTER_FOPERAND_POINTER  2u
#define BUSTER_FOPERAND_OTHER    3u
// (Macros, not functions: an operator's operands are classed by the ten thousand.) All ones for a complex type, else 0.
#define BUSTER_FTYPE_COMPLEX_ALL(t) (UINT32_C(0) - ((uint32_t)(t) >> 31))
static const uint8_t fast_operand_scalar_class[32] = {
    3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};
// FCTYPE_NONE, STRUCT, UNION, ENUM, ARRAY, FUNC, PTR.
static const uint8_t fast_operand_complex_class[8] = { 3, 3, 3, 0, 2, 2, 2, 3 };
#define BUSTER_FAST_OPERAND_POINTER(t) \
    ((uint32_t)(((uint32_t)(t) & BUSTER_FTYPE_PTR_MASK) != 0))
#define BUSTER_FAST_OPERAND_BASE(cc, t) \
    (((uint32_t)fast_operand_complex_class[(cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(t)].kind & 7u] & BUSTER_FTYPE_COMPLEX_ALL(t)) | \
     ((uint32_t)fast_operand_scalar_class[(uint32_t)(t) & 31u] & ~BUSTER_FTYPE_COMPLEX_ALL(t)))
// 1 for a struct, a union or void (not a pointer to one), else 0 (#3577).
#define BUSTER_FAST_RECORD_OR_VOID(cc, t) \
    ((uint32_t)(((uint32_t)(t) & (BUSTER_FTYPE_PTR_MASK & BUSTER_FTYPE_COMPLEX_ALL(t))) == 0) & \
     (((uint32_t)((uint32_t)(t) >> 31) & ((uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(t)].kind == FCTYPE_STRUCT) | \
                                          (uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(t)].kind == FCTYPE_UNION))) | \
      (uint32_t)(((uint32_t)(t) & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID)))
#define BUSTER_FAST_OPERAND_CLASS(cc, t) \
    ((BUSTER_FAST_OPERAND_BASE(cc, t) & (BUSTER_FAST_OPERAND_POINTER(t) - 1u)) | (BUSTER_FOPERAND_POINTER & (UINT32_C(0) - BUSTER_FAST_OPERAND_POINTER(t))))

// #3577: a struct or union an aligned typedef copied with its own alignment (fast_cc_aligned_typedef), which this path's
// calls don't place (no argument goes above 8-byte alignment): passing or returning one by value is refused.
#define BUSTER_FAST_RECORD_OVERALIGNED(ct) ((ct)->is_variadic != 0)

// Whether a type is one sizeof can't measure (#3577): a struct, union, enum or array not (yet) complete, which the full
// pipeline refuses; void and a function type are GNU's 1. Branch-free.
// (A macro, not a function: every subscript and sizeof asks.)
// (A scalar or a pointer, most types asked, is complete without the table: a test of the type's bits first.)
#define BUSTER_FAST_TYPE_INCOMPLETE(cc, t) \
    ((((uint32_t)(t) & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) != BUSTER_FTYPE_COMPLEX_BIT) ? 0u : \
     ((uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK].complete == 0) & \
      (uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK].kind != FCTYPE_FUNC) & \
      (uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK].kind != FCTYPE_PTR)))

// Whether an object can't have a type (#3577), the full pipeline's "variable has incomplete type": void, or a struct,
// union, enum or array not (yet) complete; an array of unknown size takes its size from an initializer, where SIZED is
// 1. Branch-free: every declarator asks.
#define BUSTER_FAST_OBJECT_INCOMPLETE(cc, t, sized) \
    ((((uint32_t)(t) & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) != BUSTER_FTYPE_COMPLEX_BIT) ? \
     (uint32_t)(((uint32_t)(t) & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID) : \
     (BUSTER_FAST_TYPE_INCOMPLETE(cc, t) & \
      ~((uint32_t)(sized) & (uint32_t)((cc)->ctypes[(uint32_t)(t) & BUSTER_FTYPE_CIDX_MASK].kind == FCTYPE_ARRAY))))

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
        // #3577: a volatile pointee is kept as a const one is (PVOL), and a qualifier of an inner pointer that a deeper
        // one can't keep marks the type (QDEEP), so that a conversion that would drop either finds it. Asked of a
        // qualified pointee alone, which few are.
        if ((t & (BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL)) != 0)
        {
            res |= BUSTER_FTYPE_PVOL * ((uint32_t)(d == 0) & (uint32_t)((t & BUSTER_FTYPE_VOL) != 0));
            res |= BUSTER_FTYPE_QDEEP * (uint32_t)(d != 0);
        }
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
        // #3577: a volatile pointee becomes the value's volatile; a dropped qualifier of the level a depth-2 pointer reaches
        // is that value's own, which no conversion reads (taken as const, to stay on the safe side). Asked of a type
        // with either mark alone.
        if ((t & (BUSTER_FTYPE_PVOL | BUSTER_FTYPE_QDEEP)) != 0)
        {
            const uint32_t pointee_volatile = (uint32_t)(d == 1u) & (uint32_t)((t & BUSTER_FTYPE_PVOL) != 0);
            const uint32_t level_qualified = (uint32_t)(d == 2u) & (uint32_t)((t & BUSTER_FTYPE_QDEEP) != 0);
            res = (res & ~(BUSTER_FTYPE_PVOL * pointee_volatile) & ~(BUSTER_FTYPE_QDEEP * level_qualified)) |
                  (BUSTER_FTYPE_VOL * pointee_volatile) | (BUSTER_FTYPE_CONST * level_qualified);
        }
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

// An array or a function decays to a pointer; every other type is its own. The test is a compare of the kind and
// pointer bits, inline at each caller: out of line, the function spent more on its call than on its answer (#3581).
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
        // #3577: a second declaration of an identifier in the scope of its first is an error the full pipeline reports
        // (a redefinition, another kind of symbol, conflicting types), but where the declaration's own code checks it
        // and updates the first instead (file-scope functions and objects, an identical typedef).
        // (The depth stays far below 256: statements nest at most 64 deep. Symbol 0 is the zeroed sentinel.)
        const uint32_t depth = cc->scope_depth;
        const uint32_t prev = cc->sym_cur[id];
        if (((uint32_t)(prev != 0) & (uint32_t)(cc->syms[prev].scope_depth == depth)) != 0)
        {
            fast_cc_bail(cc, "redeclaration in the same scope");
        }
        idx = cc->sym_count++;
        sym.ident_id = id;
        sym.scope_depth = (uint8_t)depth;
        sym.push_marks = (uint8_t)((BUSTER_FSYM_PUSH_UNASKED * (uint32_t)(sym.kind == FSYM_FUNC)) |
                                   (BUSTER_FSYM_PUSH_NONLITERAL * (uint32_t)(sym.kind == FSYM_ENUM_CONST)));
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
    uint8_t mode_size;    // 0=none, 1/2/4/8, or BUSTER_FMODE_UNMODELED (for __attribute__((__mode__(...))))
    uint8_t mode_float;
    uint8_t mode_quiet;   // #3577: an unmodeled mode the full pipeline takes quietly on its class's types (fast_cc_mode_type)
    // What the declaration that carries the list must check against what it declares (#3577): attributes of
    // functions alone, and the ones whose uses or calls the full pipeline checks.
    uint8_t present;        // any attribute: the declaration then checks the list (fast_cc_decl_attributes_valid)
    uint8_t function_only;  // nothrow, leaf, const, pure, noinline, cold, ...: a warning on anything but a function
    uint8_t pure;           // const or pure: a discarded call is -Wunused-value
    uint8_t deprecated;
    uint8_t unused_result;  // warn_unused_result
    uint8_t nonnull;
    uint32_t nonnull_mask;  // nonnull's parameters (bit i: parameter i + 1), every one's for nonnull without a list
    uint8_t format;         // format(printf|scanf, index, first)
    uint8_t format_scanf;
    uint32_t format_index;
    uint32_t format_arg;    // format_arg(index): the index (#3577), 0 for none
    uint32_t format_first;
    const char* asm_label;
    uint32_t asm_label_len;
    const char* alias_name;
    uint32_t alias_len;
};

// What an attribute's name asks of the direct path (#3577). The list is the attributes it can check where they are
// declared; any other name, unknown ones included (-Wunknown-attributes), is the full pipeline's.
typedef enum FastAttrKind
{
    FATTR_UNCHECKED = 0,  // the full pipeline's: unknown, or one whose meaning the direct path doesn't compile
    FATTR_ANYWHERE,       // unused, maybe_unused
    FATTR_FUNCTION,       // nothrow, leaf, noinline, always_inline, artificial, cold, hot, ...
    FATTR_GNU_INLINE,     // a function's, which its inline definition's rules read (#3595)
    FATTR_ALIAS,          // alias("target"): a file-scope declaration's, defined at the unit's end (#3592)
    FATTR_PURE,           // const, pure
    FATTR_MALLOC,
    FATTR_NORETURN,
    FATTR_WEAK,
    FATTR_ALIGNED,
    FATTR_PACKED,
    FATTR_MODE,
    FATTR_VISIBILITY,
    FATTR_DEPRECATED,
    FATTR_UNUSED_RESULT,
    FATTR_FORMAT,
    FATTR_NONNULL,
    FATTR_FORMAT_ARG,     // format_arg(index): a function's, whose parameter and value are strings (#3577)
} FastAttrKind;

// An attribute name's kind, its reserved spelling (`__name__`) already stripped.
static FastAttrKind fast_cc_attribute_kind(const char* s, uint32_t len)
{
    typedef struct FastAttrName FastAttrName;
    struct FastAttrName
    {
        const char* name;
        uint8_t length;
        uint8_t kind;
    };
    // The commonest first: glibc's declarations carry nothrow, leaf, nonnull, const and pure by the thousand.
    static const FastAttrName names[] = {
        { "nothrow", 7, FATTR_FUNCTION },
        { "leaf", 4, FATTR_FUNCTION },
        { "nonnull", 7, FATTR_NONNULL },
        { "const", 5, FATTR_PURE },
        { "pure", 4, FATTR_PURE },
        { "noinline", 8, FATTR_FUNCTION },
        { "malloc", 6, FATTR_MALLOC },
        { "format", 6, FATTR_FORMAT },
        { "noreturn", 8, FATTR_NORETURN },
        { "warn_unused_result", 18, FATTR_UNUSED_RESULT },
        { "deprecated", 10, FATTR_DEPRECATED },
        { "mode", 4, FATTR_MODE },
        { "aligned", 7, FATTR_ALIGNED },
        { "packed", 6, FATTR_PACKED },
        { "weak", 4, FATTR_WEAK },
        { "visibility", 10, FATTR_VISIBILITY },
        { "unused", 6, FATTR_ANYWHERE },
        { "maybe_unused", 12, FATTR_ANYWHERE },
        { "always_inline", 13, FATTR_FUNCTION },
        { "gnu_inline", 10, FATTR_GNU_INLINE },
        { "alias", 5, FATTR_ALIAS },
        { "artificial", 10, FATTR_FUNCTION },
        { "cold", 4, FATTR_FUNCTION },
        { "hot", 3, FATTR_FUNCTION },
        { "no_instrument_function", 22, FATTR_FUNCTION },
        { "no_sanitize_address", 19, FATTR_FUNCTION },
        { "no_sanitize_thread", 18, FATTR_FUNCTION },
        { "no_sanitize_memory", 18, FATTR_FUNCTION },
        { "no_sanitize_undefined", 21, FATTR_FUNCTION },
        { "no_address_safety_analysis", 26, FATTR_FUNCTION },
        // #3577: setjmp's and gettext's declarations (musl's headers) and musl's word types: a function's, a
        // function's with its string parameter checked where it is declared, and anything's, as the full pipeline
        // takes them quietly.
        { "returns_twice", 13, FATTR_FUNCTION },
        { "format_arg", 10, FATTR_FORMAT_ARG },
        { "may_alias", 9, FATTR_ANYWHERE },
    };
    FastAttrKind kind = FATTR_UNCHECKED;
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i += 1)
    {
        if (names[i].length == len && memcmp(names[i].name, s, len) == 0)
        {
            kind = (FastAttrKind)names[i].kind;
            break;
        }
    }
    return kind;
}

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
                // #3577: only the attributes the direct path checks where they are declared; any other is the full
                // pipeline's, and so is any attribute in a position whose declaration doesn't check the list.
                const FastAttrKind kind = fast_cc_attribute_kind(s, len);
                const bool has_args = fast_cc_peek(cc) == FTOK_LPAREN;
                const bool bare = kind == FATTR_ANYWHERE || kind == FATTR_FUNCTION || kind == FATTR_GNU_INLINE || kind == FATTR_PURE ||
                                  kind == FATTR_MALLOC || kind == FATTR_NORETURN || kind == FATTR_WEAK || kind == FATTR_PACKED ||
                                  kind == FATTR_UNUSED_RESULT;
                if (kind == FATTR_UNCHECKED)
                {
                    fast_cc_bail(cc, "unchecked attribute");
                    break;
                }
                if (attr == NULL && kind != FATTR_ANYWHERE)
                {
                    fast_cc_bail(cc, "attribute in an unchecked position");
                    break;
                }
                if (bare && has_args)
                {
                    fast_cc_bail(cc, "attribute with unexpected arguments");
                    break;
                }
                if (attr != NULL) { attr->present = 1u; }
                if (kind == FATTR_FUNCTION || kind == FATTR_GNU_INLINE || kind == FATTR_PURE || kind == FATTR_MALLOC || kind == FATTR_NORETURN ||
                    kind == FATTR_UNUSED_RESULT)
                {
                    attr->function_only = 1u;
                }
                if (kind == FATTR_PURE) { attr->pure = 1u; }
                else if (kind == FATTR_GNU_INLINE) { attr->gnu_inline = 1u; }
                else if (kind == FATTR_NORETURN) { attr->noreturn = 1u; }
                else if (kind == FATTR_WEAK) { attr->weak = 1u; }
                else if (kind == FATTR_PACKED) { attr->packed = 1u; }
                else if (kind == FATTR_UNUSED_RESULT) { attr->unused_result = 1u; }
                else if (kind == FATTR_ALIGNED)
                {
                    if (has_args)
                    {
                        cc->pos += 1u;
                        const int64_t al = fast_cc_const_int_expr(cc, false);
                        // A power of two the direct path lays out; any other is the full pipeline's error.
                        if (cc->bailout || al <= 0 || al > 4096 || (al & (al - 1)) != 0 || fast_cc_peek(cc) != FTOK_RPAREN)
                        {
                            fast_cc_bail(cc, "aligned attribute's argument");
                            break;
                        }
                        attr->aligned = (uint32_t)al;
                        cc->pos += 1u;
                    }
                    else
                    {
                        attr->aligned = 16u;
                    }
                }
                else if (kind == FATTR_MODE)
                {
                    if (!has_args || fast_cc_peek_at(cc, 1u) != FTOK_IDENT || fast_cc_peek_at(cc, 2u) != FTOK_RPAREN)
                    {
                        fast_cc_bail(cc, "mode attribute's argument");
                        break;
                    }
                    cc->pos += 1u;
                    uint32_t mlen = 0;
                    const char* ms = fast_cc_ident_str(cc, fast_cc_data(cc), &mlen);
                    if (mlen > 4u && ms[0] == '_' && ms[1] == '_' && ms[mlen - 2u] == '_' && ms[mlen - 1u] == '_')
                    {
                        ms += 2; mlen -= 4u;
                    }
                    if (mlen == 2u && memcmp(ms, "QI", 2) == 0) { attr->mode_size = 1u; }
                    else if (mlen == 2u && memcmp(ms, "HI", 2) == 0) { attr->mode_size = 2u; }
                    else if (mlen == 2u && memcmp(ms, "SI", 2) == 0) { attr->mode_size = 4u; }
                    else if (mlen == 2u && memcmp(ms, "DI", 2) == 0) { attr->mode_size = 8u; }
                    else if (mlen == 4u && memcmp(ms, "word", 4) == 0) { attr->mode_size = 8u; }
                    else if (mlen == 7u && memcmp(ms, "pointer", 7) == 0) { attr->mode_size = 8u; }
                    else if (mlen == 2u && memcmp(ms, "SF", 2) == 0) { attr->mode_size = 4u; attr->mode_float = 1u; }
                    else if (mlen == 2u && memcmp(ms, "DF", 2) == 0) { attr->mode_size = 8u; attr->mode_float = 1u; }
                    else
                    {
                        // TI/TC/XF etc. (only in unused glibc typedefs!): #3577: TI an integer's, TF, XF, KF and HF a
                        // floating type's, TC a _Complex's, as the full pipeline takes them quietly (fast_cc_mode_type).
                        attr->mode_size = BUSTER_FMODE_UNMODELED;
                        attr->mode_quiet = (uint8_t)(mlen != 2u ? 0u : memcmp(ms, "TI", 2) == 0 ? 1u :
                                                     (memcmp(ms, "TF", 2) == 0 || memcmp(ms, "XF", 2) == 0 || memcmp(ms, "KF", 2) == 0 ||
                                                      memcmp(ms, "HF", 2) == 0) ? 2u : memcmp(ms, "TC", 2) == 0 ? 3u : 0u);
                    }
                    cc->pos += 2u;
                }
                else if (kind == FATTR_VISIBILITY)
                {
                    // visibility("default" | "hidden" | "internal" | "protected"), one literal spelled as its bytes;
                    // internal is hidden, as clang emits it, and the last of a list counts. Any other is the full
                    // pipeline's error or warning (#3595).
                    uint8_t visibility = UINT8_MAX;
                    if (has_args && fast_cc_peek_at(cc, 1u) == FTOK_STR_LIT && fast_cc_peek_at(cc, 2u) == FTOK_RPAREN &&
                        fast_cc_literal_plain(cc, cc->td[cc->pos + 1u]))
                    {
                        const FastLitSlice sl = cc->pp->lit_slices[cc->td[cc->pos + 1u]];
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
                    cc->pos += 3u;
                }
                else if (kind == FATTR_ALIAS)
                {
                    // alias("name"), a single literal spelled as its bytes (#3595); the declaration that has it is
                    // fast_cc_compile_unit's to take, and cc->alias_attributes counts it so that no other place can (#3592).
                    bool named = has_args && fast_cc_peek_at(cc, 1u) == FTOK_STR_LIT && fast_cc_peek_at(cc, 2u) == FTOK_RPAREN &&
                                 fast_cc_literal_plain(cc, cc->td[cc->pos + 1u]);
                    if (named)
                    {
                        // Any bytes: one that names no declared function or object leaves at fast_cc_alias_targets.
                        const FastLitSlice sl = cc->pp->lit_slices[cc->td[cc->pos + 1u]];
                        named = sl.length != 0;
                        attr->alias_name = cc->pp->lit_bytes + sl.offset;
                        attr->alias_len = sl.length;
                    }
                    cc->alias_attributes += 1u;
                    if (!named)
                    {
                        fast_cc_bail(cc, "alias attribute's operand");
                        break;
                    }
                    cc->pos += 3u;
                }
                else if (kind == FATTR_DEPRECATED)
                {
                    attr->deprecated = 1u;
                    if (has_args)
                    {
                        if (fast_cc_peek_at(cc, 1u) != FTOK_STR_LIT || fast_cc_peek_at(cc, 2u) != FTOK_RPAREN)
                        {
                            fast_cc_bail(cc, "deprecated attribute's argument");
                            break;
                        }
                        cc->pos += 3u;
                    }
                }
                else if (kind == FATTR_FORMAT)
                {
                    // format(printf|scanf, INDEX, FIRST): the declaration checks the two numbers against the function.
                    attr->function_only = 1u;
                    attr->format = 1u;
                    bool shaped = has_args && fast_cc_peek_at(cc, 1u) == FTOK_IDENT && fast_cc_peek_at(cc, 2u) == FTOK_COMMA &&
                                  fast_cc_peek_at(cc, 3u) == FTOK_INT_LIT && fast_cc_peek_at(cc, 4u) == FTOK_COMMA &&
                                  fast_cc_peek_at(cc, 5u) == FTOK_INT_LIT && fast_cc_peek_at(cc, 6u) == FTOK_RPAREN;
                    if (shaped)
                    {
                        uint32_t alen = 0;
                        const char* as = fast_cc_ident_str(cc, cc->td[cc->pos + 1u], &alen);
                        if (alen > 4u && as[0] == '_' && as[1] == '_' && as[alen - 2u] == '_' && as[alen - 1u] == '_')
                        {
                            as += 2; alen -= 4u;
                        }
                        const bool printf_like = alen == 6u && memcmp(as, "printf", 6) == 0;
                        const bool scanf_like = alen == 5u && memcmp(as, "scanf", 5) == 0;
                        shaped = printf_like || scanf_like;
                        attr->format_scanf = scanf_like ? 1u : 0u;
                        attr->format_index = cc->td[cc->pos + 3u];
                        attr->format_first = cc->td[cc->pos + 5u];
                    }
                    if (!shaped)
                    {
                        fast_cc_bail(cc, "format attribute's arguments");
                        break;
                    }
                    cc->pos += 7u;
                }
                else if (kind == FATTR_FORMAT_ARG)
                {
                    // format_arg(INDEX): the declaration checks the index against the function.
                    attr->function_only = 1u;
                    const bool shaped = has_args && fast_cc_peek_at(cc, 1u) == FTOK_INT_LIT && fast_cc_peek_at(cc, 2u) == FTOK_RPAREN &&
                                        cc->td[cc->pos + 1u] != 0;
                    if (!shaped)
                    {
                        fast_cc_bail(cc, "format_arg attribute's argument");
                        break;
                    }
                    attr->format_arg = cc->td[cc->pos + 1u];
                    cc->pos += 3u;
                }
                else if (kind == FATTR_NONNULL)
                {
                    // nonnull or nonnull(I, J, ...): a call checks every pointer parameter, a superset of the list.
                    attr->function_only = 1u;
                    attr->nonnull = 1u;
                    if (has_args)
                    {
                        cc->pos += 1u;
                        bool listed = true;
                        while (fast_cc_peek(cc) == FTOK_INT_LIT && (fast_cc_peek_at(cc, 1u) == FTOK_COMMA || fast_cc_peek_at(cc, 1u) == FTOK_RPAREN))
                        {
                            const uint32_t index = fast_cc_data(cc);
                            listed = listed && index >= 1u && index <= 32u;
                            attr->nonnull_mask |= listed ? UINT32_C(1) << (index - 1u) : 0u;
                            cc->pos += fast_cc_peek_at(cc, 1u) == FTOK_COMMA ? 2u : 1u;
                        }
                        if (!listed || fast_cc_peek(cc) != FTOK_RPAREN || cc->tk[cc->pos - 1u] == FTOK_COMMA)
                        {
                            fast_cc_bail(cc, "nonnull attribute's arguments");
                            break;
                        }
                        cc->pos += 1u;
                    }
                    else
                    {
                        attr->nonnull_mask = UINT32_MAX;
                    }
                }
                continue;
            }
            // Anything but an attribute's name, a comma or a parenthesis between the attribute list's parentheses.
            fast_cc_bail(cc, "malformed __attribute__ list");
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
        else if (memchr(label, '.', length) != NULL)
        {
            // #3577: a label can name a block-scope static's symbol (`f.n`, the full pipeline's name for f's static n),
            // which the full pipeline refuses (#2439's c_check_program_emit.c); this path names its statics apart (`n.0`).
            // A label with a '.' leaves, whatever it names.
            fast_cc_bail(cc, "asm label with a '.'");
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
            const FastSym* typedef_sym = cc->syms + cc->sym_cur[fast_cc_data(cc)];
            base = typedef_sym->type;
            // #3577: a typedef with an alignment of its own (BUSTER_FSYM_ALIGNED).
            if ((typedef_sym->use_flags & BUSTER_FSYM_ALIGNED) != 0) { fast_cc_bail(cc, "use of an aligned typedef"); }
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
            base = saw_long ? BUSTER_FTYPE_LONG_DOUBLE : FTYPE_F64;
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
        bool pv = false;
        while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE || fast_cc_peek(cc) == FTOK_KW_RESTRICT)
        {
            if (fast_cc_peek(cc) == FTOK_KW_CONST) { pc = true; }
            if (fast_cc_peek(cc) == FTOK_KW_VOLATILE) { pv = true; }
            cc->pos += 1u;
        }
        base = ftype_make_ptr(cc, base);
        if (pc) { base = ftype_with_const(base); }
        if (pv) { base = ftype_with_volatile(base); }
    }
    // Simple `(*)(...)` function pointer or `(*)[N]` pointer-to-array (size is always 8!)
    if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_peek_at(cc, 1u) == FTOK_STAR)
    {
        // #3577: `(*)`, its stars and qualifiers alone: anything else in it (an array of pointers, `[n]`) is a type the
        // skip doesn't read.
        cc->pos += 1u;
        while (fast_cc_peek(cc) == FTOK_STAR || fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE ||
               fast_cc_peek(cc) == FTOK_KW_RESTRICT)
        {
            cc->pos += 1u;
        }
        if (fast_cc_peek(cc) != FTOK_RPAREN)
        {
            fast_cc_bail(cc, "abstract declarator in a constant expression's type");
            return FTYPE_VOID;
        }
        cc->pos += 1u;
        if (fast_cc_peek(cc) == FTOK_LPAREN)
        {
            // #3577: a parameter list the skip doesn't check: a name (a repeated one, `void bad`) or a nested list is
            // the full pipeline's; type keywords, typedef names, qualifiers, `*` and `,` alone are the skip's.
            uint32_t d = 1u;
            cc->pos += 1u;
            while (d > 0 && cc->pos < cc->tcount && !cc->bailout)
            {
                const uint8_t pk = fast_cc_peek(cc);
                if (pk == FTOK_LPAREN || (pk == FTOK_IDENT && !fast_cc_is_typedef_name(cc, fast_cc_data(cc))))
                {
                    fast_cc_bail(cc, "parameter list in a constant expression's type");
                }
                else if (pk == FTOK_RPAREN) { d -= 1u; }
                cc->pos += 1u;
            }
            base = ftype_make_ptr(cc, FTYPE_VOID);
        }
        else if (fast_cc_peek(cc) == FTOK_LBRACKET)
        {
            // #3577: dimensions of an integer literal, or none; another is an expression the skip doesn't read.
            while (fast_cc_peek(cc) == FTOK_LBRACKET)
            {
                cc->pos += 1u;
                const uint8_t dk = fast_cc_peek(cc);
                if (dk == FTOK_INT_LIT || dk == FTOK_INT_U32 || dk == FTOK_INT_I64 || dk == FTOK_INT_U64) { cc->pos += 1u; }
                if (fast_cc_peek(cc) != FTOK_RBRACKET)
                {
                    fast_cc_bail(cc, "array dimension in a constant expression's type");
                    return FTYPE_VOID;
                }
                cc->pos += 1u;
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
            const uint32_t esz = ftype_size(cc, base);
            const uint32_t eal = ftype_align(cc, base);
            const uint64_t total_sz = (uint64_t)dims[i] * (uint64_t)esz;
            // #3577: and an element whose size isn't a multiple of its alignment (an aligned typedef's), an error.
            if (((uint32_t)(total_sz > (uint64_t)INT32_MAX) | (uint32_t)(esz % eal != 0)) != 0)
            {
                fast_cc_bail(cc, "array size overflow or misaligned element");
                break;
            }
            const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
            if (ac != 0)
            {
                cc->ctypes[ac].inner = base;
                cc->ctypes[ac].count = dims[i];
                cc->ctypes[ac].size = (uint32_t)total_sz;
                cc->ctypes[ac].align = eal;
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
        if (mi->bit_width != 0) { fast_cc_bail(cc, "offsetof a bit-field"); break; }
        total_off += mi->byte_offset;
        cur_t = mi->type;
        cc->pos += 1u;
    }
    if (fast_cc_peek(cc) != FTOK_RPAREN) { fast_cc_bail(cc, "offsetof )"); return 0; }
    cc->pos += 1u;
    return total_off;
}

// #3577: the classes of an operand's own operator (FastVal's op), and the classes each binary operator questions in its
// operands (either one's, or the left one's alone), as the full pipeline's -Wparentheses family does by default.
#define BUSTER_FEXPR_ADDITIVE   0x01u   // + -
#define BUSTER_FEXPR_EQUALITY   0x02u   // == !=
#define BUSTER_FEXPR_RELATIONAL 0x04u   // < > <= >=
#define BUSTER_FEXPR_NOT        0x08u   // !
static const uint8_t fast_expr_operand_class[128] = {
    [FTOK_PLUS] = BUSTER_FEXPR_ADDITIVE, [FTOK_MINUS] = BUSTER_FEXPR_ADDITIVE,
    [FTOK_EQ] = BUSTER_FEXPR_EQUALITY, [FTOK_NE] = BUSTER_FEXPR_EQUALITY,
    [FTOK_LT] = BUSTER_FEXPR_RELATIONAL, [FTOK_GT] = BUSTER_FEXPR_RELATIONAL,
    [FTOK_LE] = BUSTER_FEXPR_RELATIONAL, [FTOK_GE] = BUSTER_FEXPR_RELATIONAL,
    [FTOK_BANG] = BUSTER_FEXPR_NOT,
};
static const uint8_t fast_expr_conflicts_any[128] = {
    [FTOK_LSHIFT] = BUSTER_FEXPR_ADDITIVE, [FTOK_RSHIFT] = BUSTER_FEXPR_ADDITIVE,
    [FTOK_AMP] = BUSTER_FEXPR_EQUALITY | BUSTER_FEXPR_RELATIONAL, [FTOK_PIPE] = BUSTER_FEXPR_EQUALITY | BUSTER_FEXPR_RELATIONAL,
    [FTOK_CARET] = BUSTER_FEXPR_EQUALITY | BUSTER_FEXPR_RELATIONAL,
    [FTOK_LT] = BUSTER_FEXPR_RELATIONAL, [FTOK_GT] = BUSTER_FEXPR_RELATIONAL,
    [FTOK_LE] = BUSTER_FEXPR_RELATIONAL, [FTOK_GE] = BUSTER_FEXPR_RELATIONAL,
};
static const uint8_t fast_expr_conflicts_left[128] = {
    [FTOK_EQ] = BUSTER_FEXPR_NOT, [FTOK_NE] = BUSTER_FEXPR_NOT,
    [FTOK_LT] = BUSTER_FEXPR_NOT, [FTOK_GT] = BUSTER_FEXPR_NOT, [FTOK_LE] = BUSTER_FEXPR_NOT, [FTOK_GE] = BUSTER_FEXPR_NOT,
};

// A constant expression's value with C's type (#3577): its 64-bit bits, unsigned or not, and 64 bits wide (long,
// unsigned long, size_t) or 32 (int and unsigned int, which char and short promote to).
typedef struct FastConstVal FastConstVal;
struct FastConstVal
{
    uint64_t v;
    bool u;
    bool w64;
    uint8_t op;   // the binary operator (FTOK_*) or `!` that made it, outside parentheses, for the precedence checks
};

// Apply a cast marker (0x1000 | size << 2 | signed << 1 | bool) to the top value: truncated or extended to the type, and
// typed as it promotes (a narrower type to int).
static void fast_cc_const_cast(uint16_t op, FastConstVal* top)
{
    const uint32_t sz = (op >> 2) & 0xfu;
    const bool sgn = (op & 2u) != 0;
    const bool is_b = (op & 1u) != 0;
    uint64_t x = top->v;
    if (is_b) { x = x != 0 ? 1u : 0u; }
    else if (sz == 1u) { x = sgn ? (uint64_t)(int64_t)(int8_t)x : (x & 0xffu); }
    else if (sz == 2u) { x = sgn ? (uint64_t)(int64_t)(int16_t)x : (x & 0xffffu); }
    else if (sz == 4u) { x = sgn ? (uint64_t)(int64_t)(int32_t)x : (x & 0xffffffffu); }
    *top = (FastConstVal){ .v = x, .u = !sgn && sz >= 4u, .w64 = sz == 8u };
}

// Apply a binary or unary operator to the stack's top values as C does (#3577), with the preprocessor's 64-bit
// arithmetic (fast_pp_apply_op, which refuses a signed overflow, a negative value made unsigned, a bad shift): the
// result takes C's type (the usual conversions; a shift's left operand's; int for a comparison or logical operator),
// a 32-bit unsigned result wraps as C's does, and a 32-bit signed result outside int's range, or a 64-bit signed sum or
// product of non-negative operands that the unsigned arithmetic carried past INT64_MAX, is an overflow the full
// pipeline diagnoses.
static void fast_cc_const_apply(FastCCState* cc, uint16_t op, FastConstVal* vals, uint32_t* vsp)
{
    // (Branch-free but for the refusals: enumerators, case labels and array sizes apply operators by the thousand.)
    const uint32_t unary = (uint32_t)((uint32_t)op - BUSTER_FPP_OP_UNARY_PLUS <= BUSTER_FPP_OP_UNARY_BANG - BUSTER_FPP_OP_UNARY_PLUS);
    const uint32_t ternary = (uint32_t)(op == BUSTER_FPP_OP_TERNARY);
    const uint32_t operands = 2u - unary + ternary;
    if (*vsp < operands)
    {
        fast_cc_bail(cc, "constant expression stack");
        return;
    }
    const uint32_t base = *vsp - operands;
    const FastConstVal a = vals[base + ternary];
    const FastConstVal b = vals[*vsp - 1u];
    // #3577: the runtime expressions' checks, in a constant one too: an operand whose own operator the binary one's
    // precedence questions (-Wparentheses' family), and a right operand of && or || other than 0 or 1
    // (-Wconstant-logical-operand).
    {
        const uint32_t binary = (unary | ternary) ^ 1u;
        const uint32_t lclass = fast_expr_operand_class[a.op & 127u];
        const uint32_t rclass = fast_expr_operand_class[b.op & 127u];
        const uint32_t conflict = (((lclass | rclass) & fast_expr_conflicts_any[op & 127u]) | (lclass & fast_expr_conflicts_left[op & 127u])) != 0;
        const uint32_t logical_constant = ((uint32_t)(op == FTOK_LAND) | (uint32_t)(op == FTOK_LOR)) & (uint32_t)(b.v > 1u);
        if ((binary & (conflict | logical_constant)) != 0)
        {
            fast_cc_bail(cc, "constant expression the full pipeline questions");
            return;
        }
    }
    const uint32_t shift = (uint32_t)(op == FTOK_LSHIFT) | (uint32_t)(op == FTOK_RSHIFT);
    const uint32_t boolean = (uint32_t)(op == FTOK_LAND) | (uint32_t)(op == FTOK_LOR) | (uint32_t)(op == FTOK_EQ) |
                             (uint32_t)(op == FTOK_NE) | (uint32_t)(op == FTOK_LT) | (uint32_t)(op == FTOK_LE) |
                             (uint32_t)(op == FTOK_GT) | (uint32_t)(op == FTOK_GE) | (uint32_t)(op == BUSTER_FPP_OP_UNARY_BANG);
    // The usual conversions (a unary operator's and a shift's result has the left operand's type): the wider type's,
    // or at one width unsigned if either is (a long holds an unsigned int).
    const uint32_t converts = (unary | shift) ^ 1u;
    const uint32_t a_w64 = (uint32_t)a.w64;
    const uint32_t b_w64 = (uint32_t)b.w64;
    const uint32_t converted_w64 = a_w64 | b_w64;
    const uint32_t converted_u = ((uint32_t)(a_w64 == b_w64) & ((uint32_t)a.u | (uint32_t)b.u)) | (a_w64 & (b_w64 ^ 1u) & (uint32_t)a.u) |
                                 ((a_w64 ^ 1u) & b_w64 & (uint32_t)b.u);
    const uint32_t w64 = ((a_w64 & (converts ^ 1u)) | (converted_w64 & converts)) & (boolean ^ 1u);
    const uint32_t u = (((uint32_t)a.u & (converts ^ 1u)) | (converted_u & converts)) & (boolean ^ 1u);
    FastPPVal pv[3];
    for (uint32_t i = 0; i < operands; i += 1) { pv[i] = (FastPPVal){ vals[base + i].v, vals[base + i].u }; }
    uint32_t pvsp = operands;
    fast_pp_apply_op(cc->pp, op, pv, &pvsp);
    if (cc->pp->bailout)
    {
        fast_cc_bail(cc, cc->pp->bailout_reason);
        return;
    }
    const uint64_t r = pv[0].val;
    // A 32-bit unsigned result wraps; a 32-bit signed one must fit; a 64-bit signed sum or product of non-negative
    // operands must not have been carried past INT64_MAX by the preprocessor's unsigned arithmetic (a long holding an
    // unsigned int's value is signed here, where #if's is unsigned).
    const uint32_t narrow_signed_overflow = (w64 ^ 1u) & (u ^ 1u) & (boolean ^ 1u) & (uint32_t)(r + UINT64_C(0x80000000) > UINT64_C(0xffffffff));
    const uint32_t wide_signed_overflow = w64 & (u ^ 1u) & ((uint32_t)(op == FTOK_PLUS) | (uint32_t)(op == FTOK_STAR)) &
                                          (uint32_t)((int64_t)a.v >= 0) & (uint32_t)((int64_t)b.v >= 0) & (uint32_t)((int64_t)r < 0);
    if ((narrow_signed_overflow | wide_signed_overflow) != 0)
    {
        fast_cc_bail(cc, "signed overflow in a constant expression");
        return;
    }
    const uint64_t narrow_mask = UINT64_C(0xffffffff) | (UINT64_C(0) - (uint64_t)(w64 | (u ^ 1u) | boolean));
    *vsp = base;
    const uint8_t made = (uint8_t)(((uint32_t)op & (UINT32_C(0) - ((unary | ternary) ^ 1u))) | ((uint32_t)FTOK_BANG * (uint32_t)(op == BUSTER_FPP_OP_UNARY_BANG)));
    vals[(*vsp)++] = (FastConstVal){ .v = r & narrow_mask, .u = u != 0, .w64 = w64 != 0, .op = made };
}

// Level 2: Pure leaf iterative integer constant expression evaluator (for array bounds, enums, bitfields, case labels,
// aligned(N), _Static_assert, etc.) — NEVER calls runtime expression compiler or struct definition parser! #3577: where
// out_unsigned isn't NULL, whether the value's type is unsigned (a value past INT64_MAX returns negative).
static int64_t fast_cc_const_int_expr_full(FastCCState* cc, bool stop_at_comma, bool* out_unsigned)
{
    FastConstVal vals[64];
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
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = fast_cc_data(cc), .u = false }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_U32)
            {
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = fast_cc_data(cc), .u = true }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_I64)
            {
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = cc->pp->num_pool[fast_cc_data(cc)], .u = false, .w64 = true }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_INT_U64)
            {
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = cc->pp->num_pool[fast_cc_data(cc)], .u = true, .w64 = true }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_CHAR_LIT)
            {
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = (uint64_t)(int64_t)(int32_t)fast_cc_data(cc), .u = false }; }
                cc->pos += 1u;
                expect = false;
            }
            else if (k == FTOK_IDENT)
            {
                const uint32_t id = fast_cc_data(cc);
                const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
                if (si != 0 && cc->syms[si].kind == FSYM_ENUM_CONST)
                {
                    if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = (uint64_t)cc->syms[si].const_val, .u = false, .w64 = cc->syms[si].type == FTYPE_I64 }; }
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
                    // #3577: an incomplete type's is the full pipeline's error.
                    if (BUSTER_FAST_TYPE_INCOMPLETE(cc, st) != 0) { fast_cc_bail(cc, "sizeof an incomplete type"); break; }
                    const uint64_t res = is_align ? ftype_align(cc, st) : ftype_size(cc, st);
                    if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = res, .u = true, .w64 = true }; }
                    expect = false;
                }
                else
                {
                    // sizeof(var) or sizeof(str_lit). #3577: _Alignof of an expression is GNU's, which the full pipeline
                    // warns of.
                    if (is_align) { fast_cc_bail(cc, "_Alignof of an expression"); break; }
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
                                    // #3577: an index of one integer literal, of an array or a pointer; another is read
                                    // by the full pipeline, whose checks it needs.
                                    const uint8_t ik = fast_cc_peek_at(cc, 1u);
                                    const bool literal = ik == FTOK_INT_LIT || ik == FTOK_INT_U32 || ik == FTOK_INT_I64 || ik == FTOK_INT_U64;
                                    if (!literal || fast_cc_peek_at(cc, 2u) != FTOK_RBRACKET || (!ftype_is_array(cur_t) && !ftype_is_ptr(cur_t)))
                                    {
                                        fast_cc_bail(cc, "sizeof of a subscript");
                                        break;
                                    }
                                    cc->pos += 3u;
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
                                    if (mi->bit_width != 0) { fast_cc_bail(cc, "sizeof or alignof a bit-field"); break; }
                                    cur_t = mi->type;
                                    cc->pos += 1u;
                                }
                                res = is_align ? ftype_align(cc, cur_t) : ftype_size(cc, cur_t);
                            }
                            // #3577: an incomplete type's is the full pipeline's error.
                            if (BUSTER_FAST_TYPE_INCOMPLETE(cc, cur_t) != 0) { fast_cc_bail(cc, "sizeof an incomplete type"); break; }
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
                    if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = res, .u = true, .w64 = true }; }
                    expect = false;
                }
            }
            else if (k == FTOK_KW_BUILTIN_OFFSETOF)
            {
                const uint64_t total_off = fast_cc_parse_offsetof(cc);
                if (vsp < 64u) { vals[vsp++] = (FastConstVal){ .v = total_off, .u = true, .w64 = true }; }
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
                        if (vsp > 0) { fast_cc_const_cast(op, &vals[vsp - 1u]); }
                    }
                    else
                    {
                        fast_cc_const_apply(cc, op, vals, &vsp);
                        if (cc->bailout) { break; }
                    }
                }
                if (osp > 0) { osp -= 1u; }
                pdepth -= 1u;
                cc->pos += 1u;
                // A parenthesized value's operator is no operand's to question.
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
                if (!has_q) { break; } // ':' belongs to case/bitfield!
                while (osp > 0 && ops[osp - 1u] != FTOK_QUESTION)
                {
                    const uint16_t op = ops[--osp];
                    fast_cc_const_apply(cc, op, vals, &vsp);
                    if (cc->bailout) { break; }
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
                        if (vsp > 0) { fast_cc_const_cast(top, &vals[vsp - 1u]); }
                    }
                    else
                    {
                        fast_cc_const_apply(cc, top, vals, &vsp);
                        if (cc->bailout) { break; }
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
            if (vsp > 0) { fast_cc_const_cast(top, &vals[vsp - 1u]); }
        }
        else
        {
            fast_cc_const_apply(cc, top, vals, &vsp);
            if (cc->bailout) { break; }
        }
    }
    if (vsp != 1u && !cc->bailout)
    {
        fast_cc_bail(cc, "empty const_int_expr");
    }
    const bool valued = !cc->bailout && vsp == 1u;
    if (out_unsigned != NULL)
    {
        *out_unsigned = valued && vals[0].u;
    }
    return valued ? (int64_t)vals[0].v : 0;
}

static int64_t fast_cc_const_int_expr(FastCCState* cc, bool stop_at_comma)
{
    return fast_cc_const_int_expr_full(cc, stop_at_comma, NULL);
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
    uint8_t is_register;
    uint8_t has_tag;             // struct, union or enum: a list without a declarator may declare the tag (#3577)
    uint8_t written;             // BUSTER_FSPEC_*: the storage classes and function specifiers written (#3599)
    FastAttrInfo attr;
};

// The storage classes and function specifiers a declaration's specifiers wrote (FastDeclSpecs.written), for the places
// that refuse some (#3599): a parameter takes register alone, a for's declaration register and auto, a member or a type
// name none.
#define BUSTER_FSPEC_TYPEDEF 1u
#define BUSTER_FSPEC_STATIC 2u
#define BUSTER_FSPEC_EXTERN 4u
#define BUSTER_FSPEC_REGISTER 8u
#define BUSTER_FSPEC_AUTO 16u
#define BUSTER_FSPEC_INLINE 32u
#define BUSTER_FSPEC_NORETURN 64u
#define BUSTER_FSPEC_STORAGE (BUSTER_FSPEC_TYPEDEF | BUSTER_FSPEC_STATIC | BUSTER_FSPEC_EXTERN | BUSTER_FSPEC_REGISTER | BUSTER_FSPEC_AUTO)

// One storage class of a declaration's specifiers, `bit` among BUSTER_FSPEC_STORAGE's, and its keyword read. A second
// leaves (#3599): clang refuses two, and warns of one written twice.
static void fast_cc_storage_class(FastCCState* cc, FastDeclSpecs* sp, uint8_t bit)
{
    if ((sp->written & BUSTER_FSPEC_STORAGE) != 0)
    {
        fast_cc_bail(cc, "two storage classes");
    }
    sp->written |= bit;
    cc->pos += 1u;
}

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
        // #3577: an enum first declared in a parameter list is -Wvisibility's.
        if (cc->param_depth != 0) { fast_cc_bail(cc, "tag first declared in a parameter list"); return FTYPE_I32; }
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
        // #3577: the next enumerator's implicit value would be past INT64_MAX.
        bool implicit_past = false;
        // #3577: the enumerators are consecutive symbols, from here (a switch's -Wswitch reads them).
        const uint32_t first_enumerator = cc->sym_count;
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
            // #3577: a value past INT64_MAX, written or implicit, which int64_t values don't hold: the full pipeline gives
            // the enum an unsigned 64-bit type, or warns -Wenum-too-large beside a negative value.
            if (fast_cc_peek(cc) == FTOK_ASSIGN)
            {
                cc->pos += 1u;
                bool value_unsigned = false;
                next_val = fast_cc_const_int_expr_full(cc, true, &value_unsigned);
                if (value_unsigned && next_val < 0) { fast_cc_bail(cc, "an enumerator past INT64_MAX"); break; }
            }
            else if (implicit_past)
            {
                fast_cc_bail(cc, "an enumerator past INT64_MAX");
                break;
            }
            if (next_val < 0) { has_neg = true; }
            if (next_val > INT32_MAX || next_val < INT32_MIN)
            {
                if (has_neg || next_val > UINT32_MAX) { need_i64 = true; }
            }
            // sec_off: the enumerator's enum, which a use carries for -Wimplicit-enum-enum-cast (#3577).
            fast_cc_sym_bind(cc, eid, (FastSym){
                .kind = FSYM_ENUM_CONST,
                .type = need_i64 ? FTYPE_I64 : FTYPE_I32,
                .const_val = next_val,
                .sec_off = cidx,
            });
            implicit_past = next_val == INT64_MAX;
            next_val = (int64_t)((uint64_t)next_val + 1u);
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
            cc->ctypes[cidx].start = first_enumerator;
            cc->ctypes[cidx].count = cc->sym_count - first_enumerator;
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
    uint8_t is_volatile;   // a pointer's own volatile (#3577)
    uint8_t is_restrict;   // a pointer's restrict, which a pointer to a function can't have (#3577)
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

// #3577: what a mode attribute makes of BASE (the specifiers' type, or a declarator's base), as clang and the full pipeline
// make it: QI, HI, SI, DI, word and pointer on an integer give an integer of that size and the base's signedness (an
// 8-byte one long, where main's direct path gave long long), SF and DF on a float or a double that floating type. A mode
// whose size this path doesn't model (BUSTER_FMODE_UNMODELED) is one the full pipeline takes quietly only in the pairs
// mode_quiet names (TI on an integer; TF, XF, KF and HF on a floating type; TC on a _Complex, glibc's __cfloat128): then
// *UNMODELED is set and the base is returned without its long mark, refused where its size would count. Anything else (a
// class that isn't the type's, an unknown or vector mode, a _Bool's, an enum's, a record's) is the full pipeline's error
// or warning, or a type this path doesn't model, and leaves.
static __attribute__((cold, noinline)) FastType fast_cc_mode_type(FastCCState* cc, FastType base, const FastAttrInfo* attr, bool* unmodeled)
{
    const FastType quals = base & (BUSTER_FTYPE_CONST | BUSTER_FTYPE_VOL);
    const FastType t = BUSTER_FTYPE_UNQUAL(base);
    const uint32_t kind = t & BUSTER_FTYPE_SCALAR_MASK;
    const bool scalar = (t & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK)) == 0;
    const bool integer = scalar && kind >= FTYPE_I8 && kind <= FTYPE_U64;
    const bool floating = scalar && (kind == FTYPE_F32 || kind == FTYPE_F64);
    FastType res = t;
    *unmodeled = false;
    if (attr->mode_size == BUSTER_FMODE_UNMODELED)
    {
        *unmodeled = (attr->mode_quiet == 1u && integer) || (attr->mode_quiet == 2u && (floating || t == BUSTER_FTYPE_LONG_DOUBLE)) ||
                     (attr->mode_quiet == 3u && t == FTYPE_UNSUP16);
        res = t & ~BUSTER_FTYPE_SCALAR_LONG;
        if (!*unmodeled) { fast_cc_bail(cc, "a mode the full pipeline refuses or this path doesn't model"); }
    }
    else if (attr->mode_float != 0 ? !floating : !integer)
    {
        fast_cc_bail(cc, "a mode whose class isn't its type's");
    }
    else if (attr->mode_float != 0)
    {
        res = attr->mode_size == 4u ? FTYPE_F32 : FTYPE_F64;
    }
    else
    {
        const bool u = (kind & 1u) != 0;
        res = attr->mode_size == 1u ? (u ? FTYPE_U8 : FTYPE_I8) :
              attr->mode_size == 2u ? (u ? FTYPE_U16 : FTYPE_I16) :
              attr->mode_size == 4u ? (u ? FTYPE_U32 : FTYPE_I32) :
                                      ((u ? FTYPE_U64 : FTYPE_I64) | BUSTER_FTYPE_SCALAR_LONG);
    }
    return res | quals;
}

// The type of a declaration whose specifiers are one keyword of a scalar type, by keyword (64..127), and whether the
// keyword is one. Char is signed here; -funsigned-char sets its low bit (FTYPE_I8 to FTYPE_U8).
static const FastType fast_cc_single_keyword_types[64] = {
    [FTOK_KW_VOID - 64] = FTYPE_VOID,
    [FTOK_KW_CHAR - 64] = FTYPE_I8 | BUSTER_FTYPE_SCALAR_CHAR,
    [FTOK_KW_SHORT - 64] = FTYPE_I16,
    [FTOK_KW_INT - 64] = FTYPE_I32,
    [FTOK_KW_LONG - 64] = FTYPE_I64 | BUSTER_FTYPE_SCALAR_LONG,
    [FTOK_KW_FLOAT - 64] = FTYPE_F32,
    [FTOK_KW_DOUBLE - 64] = FTYPE_F64,
    [FTOK_KW_SIGNED - 64] = FTYPE_I32,
    [FTOK_KW_UNSIGNED - 64] = FTYPE_U32,
    [FTOK_KW_BOOL - 64] = FTYPE_BOOL,
};
#define BUSTER_FTOK_SCALAR_KEYWORDS (BUSTER_FTOK_BIT(FTOK_KW_VOID) | BUSTER_FTOK_BIT(FTOK_KW_CHAR) | BUSTER_FTOK_BIT(FTOK_KW_SHORT) | \
    BUSTER_FTOK_BIT(FTOK_KW_INT) | BUSTER_FTOK_BIT(FTOK_KW_LONG) | BUSTER_FTOK_BIT(FTOK_KW_FLOAT) | BUSTER_FTOK_BIT(FTOK_KW_DOUBLE) | \
    BUSTER_FTOK_BIT(FTOK_KW_SIGNED) | BUSTER_FTOK_BIT(FTOK_KW_UNSIGNED) | BUSTER_FTOK_BIT(FTOK_KW_BOOL))

// Specifiers of one token, a scalar type's keyword or a typedef's name, before a token that isn't a specifier: 12,500
// of sqlite3.c's 20,300 declarations and 15,000 of its 18,100 parameters. They give the type alone, as both full
// parsers' loops and their tables of keyword counts would; the token is taken and the specifiers filled when they are
// found (#3581).
static inline __attribute__((always_inline)) bool fast_cc_specs_of_one_token(FastCCState* cc, FastDeclSpecs* out_specs, FastType* out_type)
{
    const uint32_t at = cc->pos;
    uint32_t found = 0;
    FastType type = FTYPE_I32;
    if (at + 1u < cc->tcount)
    {
        const uint32_t k = cc->tk[at];
        const uint32_t next = cc->tk[at + 1u];
        const uint32_t next_specifies = (uint32_t)(next >= 64u) & (uint32_t)(BUSTER_FTOK_TYPE_START_KEYWORDS >> ((next - 64u) & 63u));
        if (k == FTOK_IDENT)
        {
            const uint32_t id = cc->td[at];
            found = (uint32_t)fast_cc_is_typedef_name(cc, id);
            if (found != 0)
            {
                // #3577: a typedef whose uses are refused or questioned (BUSTER_FSYM_ALIGNED: an alignment or a mode of
                // its own; its declaration's other use flags) takes the full parsers, which check them.
                const FastSym* const sym = cc->syms + cc->sym_cur[id];
                type = sym->type;
                found = (uint32_t)(sym->use_flags == 0);
            }
        }
        else
        {
            found = (uint32_t)(k >= 64u) & (uint32_t)(BUSTER_FTOK_SCALAR_KEYWORDS >> ((k - 64u) & 63u));
            type = fast_cc_single_keyword_types[(k - 64u) & 63u] | ((uint32_t)(k == FTOK_KW_CHAR) & (uint32_t)cc->unsigned_char);
        }
        found &= (uint32_t)(next_specifies == 0) & (uint32_t)!cc->bailout;
    }
    if (found != 0)
    {
        if (out_specs != NULL) { *out_specs = (FastDeclSpecs){ .type = type }; }
        cc->pos = at + 1u;
        *out_type = type;
    }
    return found != 0;
}

// Level 5: Leaf declaration specifiers (handles scalars, typedefs, enums, attributes, _Alignas, and existing/forward
// struct/union tags — NEVER calls fast_cc_parse_record_iter so the call graph stays strictly acyclic!).
static __attribute__((noinline)) BUSTER_DIRECT_HOT FastType fast_cc_parse_scalar_or_tag_specs_full(FastCCState* cc, FastDeclSpecs* out_specs)
{
    // Filled where the caller reads it: a copy of a record written a field at a time waits on those stores.
    FastDeclSpecs local_specs;
    FastDeclSpecs* const sp = out_specs != NULL ? out_specs : &local_specs;
    *sp = (FastDeclSpecs){ 0 };
    uint32_t saw_void = 0, saw_char = 0, saw_short = 0, saw_int = 0, saw_long = 0;
    uint32_t saw_float = 0, saw_double = 0, saw_signed = 0, saw_unsigned = 0, saw_bool = 0;
    // #3577: what C allows once (a storage class, each qualifier and function specifier) and the type keywords'
    // combination, checked after the list.
    uint32_t saw_complex = 0, storage = 0, saw_const = 0, saw_volatile = 0, saw_inline = 0, saw_noreturn = 0;
    uint32_t int128_signs = UINT32_MAX;
    bool has_explicit_type = false;
    FastType explicit_type = FTYPE_I32;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_KW_TYPEDEF) { sp->is_typedef = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_TYPEDEF); }
        else if (k == FTOK_KW_STATIC) { sp->is_static = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_STATIC); }
        else if (k == FTOK_KW_EXTERN) { sp->is_extern = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_EXTERN); }
        else if (k == FTOK_KW_INLINE) { sp->is_inline = 1u; saw_inline += 1u; sp->written |= BUSTER_FSPEC_INLINE; cc->pos += 1u; }
        else if (k == FTOK_KW_REGISTER) { sp->is_register = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_REGISTER); }
        else if (k == FTOK_KW_AUTO) { storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_AUTO); }
        else if (k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
        else if (k == FTOK_KW_NORETURN) { sp->is_noreturn = 1u; saw_noreturn += 1u; sp->written |= BUSTER_FSPEC_NORETURN; cc->pos += 1u; }
        else if (k == FTOK_KW_CONST) { sp->is_const = 1u; saw_const += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_VOLATILE) { sp->is_volatile = 1u; saw_volatile += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_RESTRICT || k == FTOK_KW_ALIGNAS)
        {
            // restrict on a specifier list's type, and _Alignas: the full pipeline's. (auto is a storage class, and
            // without a type specifier C23's inference, which leaves at the type's end, #3599.)
            fast_cc_bail(cc, "restrict or _Alignas specifier");
            break;
        }
        else if (k == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &sp->attr); }
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
            int128_signs = saw_signed + saw_unsigned;
            has_explicit_type = true;
            explicit_type = is_u ? FTYPE_U128 : FTYPE_I128;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_COMPLEX)
        {
            sp->is_unsupported_type = 1u;
            saw_complex += 1u;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_FLOAT128 || k == FTOK_KW_FLOAT16)
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
                // #3577: a tag first declared in a parameter list is -Wvisibility's (its scope is the list's).
                if (cc->param_depth != 0) { fast_cc_bail(cc, "tag first declared in a parameter list"); break; }
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
            const FastSym* typedef_sym = cc->syms + cc->sym_cur[fast_cc_data(cc)];
            explicit_type = typedef_sym->type;
            // #3577: a typedef with an alignment of its own (BUSTER_FSYM_ALIGNED).
            if ((typedef_sym->use_flags & BUSTER_FSYM_ALIGNED) != 0) { fast_cc_bail(cc, "use of an aligned typedef"); }
            cc->pos += 1u;
        }
        else
        {
            break;
        }
    }

    // #3577: the type keywords make one of C's types, and what C allows once comes once. A list that names no type
    // (implicit int, an error since C99), two types, a keyword more often than C allows (`int int`, `long long long`,
    // `signed unsigned`, `const const`, two storage classes), or a combination C has none of (`long char`, `signed
    // float`, `short long`, `_Complex int`, a sign after __int128) is the full pipeline's diagnostic.
    const uint32_t signs = saw_signed + saw_unsigned;
    const uint32_t named = (uint32_t)has_explicit_type;
    // Most lists are one type word (`int`, a typedef's name, a tag) with at most one storage class and qualifier of each
    // kind, which C allows whatever the word: the whole check below runs for the other lists.
    const uint32_t words = saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_bool + signs + saw_complex + named;
    const uint32_t one_word = (uint32_t)(words == 1u) & (uint32_t)(saw_complex == 0) &
                              (uint32_t)((storage | saw_const | saw_volatile | saw_inline | saw_noreturn) < 2u);
    if (one_word == 0)
    {
        const uint32_t bases = (uint32_t)(saw_void != 0) + (uint32_t)(saw_char != 0) + (uint32_t)(saw_int != 0) + (uint32_t)(saw_float != 0) +
                               (uint32_t)(saw_double != 0) + (uint32_t)(saw_bool != 0) + named;
        const uint32_t int128_first = (uint32_t)(int128_signs == signs);
        const uint32_t specifiers_bad =
            (uint32_t)(bases > 1u) | (uint32_t)(bases + saw_short + saw_long + signs == 0) |
            (uint32_t)(saw_void > 1u) | (uint32_t)(saw_char > 1u) | (uint32_t)(saw_int > 1u) | (uint32_t)(saw_float > 1u) |
            (uint32_t)(saw_double > 1u) | (uint32_t)(saw_bool > 1u) | (uint32_t)(saw_short > 1u) | (uint32_t)(saw_long > 2u) |
            (uint32_t)(signs > 1u) | (uint32_t)(saw_complex > 1u) |
            ((uint32_t)(saw_short != 0) & ((uint32_t)(saw_long != 0) | ((uint32_t)(bases != 0) & (uint32_t)(saw_int == 0)))) |
            ((uint32_t)(saw_long != 0) & (uint32_t)(bases != 0) & (uint32_t)(saw_int == 0) & ((uint32_t)(saw_double == 0) | (uint32_t)(saw_long > 1u))) |
            ((uint32_t)(signs != 0) & (uint32_t)(bases != 0) & (uint32_t)(saw_char == 0) & (uint32_t)(saw_int == 0) & (int128_first ^ 1u)) |
            (named & (uint32_t)((saw_short | saw_long) != 0)) | ((uint32_t)(saw_complex != 0) & (uint32_t)((saw_float | saw_double) == 0)) |
            (uint32_t)(storage > 1u) | (uint32_t)(saw_const > 1u) | (uint32_t)(saw_volatile > 1u) | (uint32_t)(saw_inline > 1u) |
            (uint32_t)(saw_noreturn > 1u);
        if (specifiers_bad != 0)
        {
            fast_cc_bail(cc, "declaration specifiers");
        }
    }
    FastType res = FTYPE_I32;
    if (has_explicit_type) { res = explicit_type; }
    else if (saw_void) { res = FTYPE_VOID; }
    else if (saw_bool) { res = FTYPE_BOOL; }
    else if (saw_float) { res = FTYPE_F32; }
    else if (saw_double)
    {
        res = saw_long ? BUSTER_FTYPE_LONG_DOUBLE : FTYPE_F64;
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
    else
    {
        // No type specifier is implicit int, which C99 refused, or C23's auto, whose type is its initializer's (#3599).
        res = FTYPE_I32;
        if (saw_int + saw_signed == 0u) { fast_cc_bail(cc, "a declaration without a type specifier"); }
    }
    if (saw_complex != 0) { res = FTYPE_UNSUP16; }

    // #3577: a mode's type (fast_cc_mode_type); a member's or a parameter's size counts, so an unmodeled one leaves.
    if (sp->attr.mode_size != 0)
    {
        bool unmodeled = false;
        res = fast_cc_mode_type(cc, res, &sp->attr, &unmodeled);
        if (unmodeled) { fast_cc_bail(cc, "a member's or a parameter's mode this path doesn't model"); }
    }

    if (sp->is_const) { res = ftype_with_const(res); }
    if (sp->is_volatile) { res |= BUSTER_FTYPE_VOL; }
    sp->type = res;
    return res;
}

static inline __attribute__((always_inline)) FastType fast_cc_parse_scalar_or_tag_specs(FastCCState* cc, FastDeclSpecs* out_specs)
{
    FastType res = FTYPE_I32;
    if (!fast_cc_specs_of_one_token(cc, out_specs, &res)) { res = fast_cc_parse_scalar_or_tag_specs_full(cc, out_specs); }
    return res;
}

// Level 6: Parse ONE declarator (abstract or named) iteratively with ZERO recursion!
// Handles nested `(*fp)(int, char*)`, `int *(*a[4])(void)`, `void (*signal(int, void(*)(int)))(int)`, etc.!
static __attribute__((noinline)) BUSTER_DIRECT_HOT bool fast_cc_parse_declarator_full(FastCCState* cc, FastType base_type, bool allow_abstract,
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
            uint8_t pvolatile = 0;
            uint8_t prestrict = 0;
            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE ||
                   fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE ||
                   fast_cc_peek(cc) == FTOK_KW_ATOMIC)
            {
                if (fast_cc_peek(cc) == FTOK_KW_CONST) { pconst = 1u; cc->pos += 1u; }
                else if (fast_cc_peek(cc) == FTOK_KW_VOLATILE) { pvolatile = 1u; cc->pos += 1u; }
                else if (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &attr); }
                else if (fast_cc_peek(cc) == FTOK_KW_ATOMIC) { fast_cc_bail(cc, "_Atomic pointer"); break; }
                else { prestrict = 1u; cc->pos += 1u; }
            }
            if (mod_count < 64u)
            {
                mods[mod_count++] = (FastDeclMod){ .kind = FDMOD_PTR, .is_const = pconst, .is_volatile = pvolatile, .is_restrict = prestrict };
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
                // #3577: a qualifier or static in an array declarator belongs to a parameter's (parsed elsewhere).
                if (fast_cc_peek(cc) == FTOK_KW_STATIC || fast_cc_peek(cc) == FTOK_KW_CONST ||
                    fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_VOLATILE)
                {
                    fast_cc_bail(cc, "qualifier in an array declarator");
                    break;
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
                uint64_t name_filter = 0;
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
                    cc->param_depth += 1u;
                    while (cc->pos < cc->tcount && fast_cc_peek(cc) != FTOK_RPAREN && !cc->bailout)
                    {
                        if (fast_cc_peek(cc) == FTOK_ELLIPSIS)
                        {
                            // #3577: `...` alone is C23's; before it, an error.
                            if (pcnt == 0 && !cc->pp->c23) { fast_cc_bail(cc, "variadic function without a named parameter"); break; }
                            is_var = true;
                            cc->pos += 1u;
                            break;
                        }
                        FastDeclSpecs pspecs = { 0 };
                        FastType pt = fast_cc_parse_scalar_or_tag_specs(cc, &pspecs);
                        if ((pspecs.written & ~BUSTER_FSPEC_REGISTER) != 0)
                        {
                            fast_cc_bail(cc, "a parameter's storage class or function specifier");
                        }
                        if (cc->bailout) { break; }

                        // Inline mini-declarator for parameter (handles `*`, `(*fp)(...)`, `[]`, name, `__attribute__`)!
                        uint32_t pname = 0;
                        // Leading pointers
                        while (fast_cc_peek(cc) == FTOK_STAR)
                        {
                            cc->pos += 1u;
                            bool pc = false;
                            bool pv = false;
                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE ||
                                   fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE)
                            {
                                if (fast_cc_peek(cc) == FTOK_KW_CONST) { pc = true; cc->pos += 1u; }
                                else if (fast_cc_peek(cc) == FTOK_KW_VOLATILE) { pv = true; cc->pos += 1u; }
                                else if (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, NULL); }
                                else { cc->pos += 1u; }
                            }
                            pt = ftype_make_ptr(cc, pt);
                            if (pc) { pt = ftype_with_const(pt); }
                            if (pv) { pt = ftype_with_volatile(pt); }
                        }
                        // Check for function-pointer parameter `(*fp)(args)`
                        if (fast_cc_peek(cc) == FTOK_LPAREN && fast_cc_peek_at(cc, 1u) == FTOK_STAR)
                        {
                            cc->pos += 2u; // `(*`
                            uint32_t extra_ptrs = 1u;
                            while (fast_cc_peek(cc) == FTOK_STAR) { extra_ptrs += 1u; cc->pos += 1u; }
                            // #3577: a const or volatile of the pointer the parameter is (`int (* const p)(void)`), which
                            // its type keeps for an assignment's check; one before another `*` (an inner pointer's) this
                            // form would drop.
                            bool fp_const = false;
                            bool fp_volatile = false;
                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE)
                            {
                                fp_const = fp_const || fast_cc_peek(cc) == FTOK_KW_CONST;
                                fp_volatile = fp_volatile || fast_cc_peek(cc) == FTOK_KW_VOLATILE;
                                cc->pos += 1u;
                            }
                            // A restrict of a function's pointer is the full pipeline's error.
                            if (fast_cc_peek(cc) == FTOK_STAR || fast_cc_peek(cc) == FTOK_KW_RESTRICT)
                            {
                                fast_cc_bail(cc, "qualified function-pointer parameter");
                                break;
                            }
                            if (fast_cc_peek(cc) == FTOK_IDENT) { pname = fast_cc_data(cc); cc->pos += 1u; }
                            if (fast_cc_peek(cc) == FTOK_LBRACKET)
                            {
                                fast_cc_bail(cc, "array of function pointers as a parameter");
                                break;
                            }
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
                            // #3577: a pointer to an array, `(*p)[2][3]`, whose brackets are all the array's under the
                            // pointer, and a pointer in parentheses, `(*p)`: main's direct path built only a function's
                            // pointer here, and made the first an int * (the brackets read as the parameter's own) and the
                            // second an int. The element is a complete object type, an inner dimension a known one.
                            if (fast_cc_peek(cc) == FTOK_LBRACKET)
                            {
                                uint32_t adims[8];
                                uint32_t andim = 0;
                                while (fast_cc_peek(cc) == FTOK_LBRACKET && !cc->bailout)
                                {
                                    cc->pos += 1u;
                                    uint32_t dc = UINT32_MAX;
                                    if (fast_cc_peek(cc) != FTOK_RBRACKET)
                                    {
                                        const int64_t dv = fast_cc_const_int_expr(cc, false);
                                        dc = (dv < 0 || dv > (int64_t)UINT32_MAX - 1) ? UINT32_MAX - 1u : (uint32_t)dv;
                                    }
                                    if (((uint32_t)(andim >= 8u) | (uint32_t)(dc == UINT32_MAX - 1u) | ((uint32_t)(dc == UINT32_MAX) & (uint32_t)(andim != 0)) |
                                         (uint32_t)(fast_cc_peek(cc) != FTOK_RBRACKET)) != 0)
                                    {
                                        fast_cc_bail(cc, "a pointer to an array parameter's dimension");
                                        break;
                                    }
                                    cc->pos += 1u;
                                    adims[andim++] = dc;
                                }
                                for (int32_t di = (int32_t)andim - 1; di >= 0 && !cc->bailout; di -= 1)
                                {
                                    const uint32_t esz = ftype_size(cc, pt);
                                    const uint32_t eal = ftype_align(cc, pt);
                                    const uint64_t total_sz = adims[di] == UINT32_MAX ? 0u : (uint64_t)adims[di] * (uint64_t)esz;
                                    if ((BUSTER_FAST_TYPE_INCOMPLETE(cc, pt) | (uint32_t)ftype_is_void(pt) | (uint32_t)ftype_is_func(pt) |
                                         (uint32_t)(total_sz > (uint64_t)INT32_MAX) | (uint32_t)(eal == 0 || esz % eal != 0)) != 0)
                                    {
                                        fast_cc_bail(cc, "a pointer to an array parameter's element");
                                        break;
                                    }
                                    const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                                    if (ac != 0)
                                    {
                                        cc->ctypes[ac].inner = pt;
                                        cc->ctypes[ac].count = adims[di];
                                        cc->ctypes[ac].size = (uint32_t)total_sz;
                                        cc->ctypes[ac].align = eal;
                                        cc->ctypes[ac].complete = adims[di] != UINT32_MAX ? 1u : 0u;
                                        pt = ftype_from_index(cc, ac);
                                    }
                                }
                                if (cc->bailout) { break; }
                                if (fast_cc_peek(cc) == FTOK_LPAREN) { fast_cc_bail(cc, "an array of functions as a parameter"); break; }
                            }
                            if (fast_cc_peek(cc) != FTOK_LPAREN)
                            {
                                for (uint32_t ep = 0; ep < extra_ptrs; ep += 1) { pt = ftype_make_ptr(cc, pt); }
                                if (fp_const) { pt = ftype_with_const(pt); }
                                if (fp_volatile) { pt = ftype_with_volatile(pt); }
                            }
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
                                        if ((fps.written & ~BUSTER_FSPEC_REGISTER) != 0)
                                        {
                                            fast_cc_bail(cc, "a parameter's storage class or function specifier");
                                        }
                                        if (cc->bailout) { break; }
                                        while (fast_cc_peek(cc) == FTOK_STAR)
                                        {
                                            cc->pos += 1u;
                                            bool fpc = false;
                                            bool fpv = false;
                                            while (fast_cc_peek(cc) == FTOK_KW_CONST || fast_cc_peek(cc) == FTOK_KW_VOLATILE || fast_cc_peek(cc) == FTOK_KW_RESTRICT)
                                            {
                                                fpc = fpc || fast_cc_peek(cc) == FTOK_KW_CONST;
                                                fpv = fpv || fast_cc_peek(cc) == FTOK_KW_VOLATILE;
                                                cc->pos += 1u;
                                            }
                                            fpt = ftype_make_ptr(cc, fpt);
                                            if (fpc) { fpt = ftype_with_const(fpt); }
                                            if (fpv) { fpt = ftype_with_volatile(fpt); }
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
                                    if (fp_const) { pt = ftype_with_const(pt); }
                                    if (fp_volatile) { pt = ftype_with_volatile(pt); }
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
                        // #3577: one declared an array (here or by a typedef) is marked in its name's high bit: its sizeof
                        // is the full pipeline's -Wsizeof-array-argument.
                        uint32_t declared_array = 0;
                        if (fast_cc_peek(cc) == FTOK_LBRACKET)
                        {
                            declared_array = 1u;
                            uint32_t dims[8];
                            uint32_t ndim = 0;
                            bool pointer_const = false;
                            bool pointer_volatile = false;
                            while (fast_cc_peek(cc) == FTOK_LBRACKET && ndim < 8u)
                            {
                                cc->pos += 1u;
                                // #3577: a qualifier or static belongs to a parameter's first dimension alone, and its
                                // const and volatile qualify the pointer the parameter is (`int x[const 3]` is an
                                // `int *const x`, which an assignment checks).
                                while (fast_cc_peek(cc) == FTOK_KW_STATIC || fast_cc_peek(cc) == FTOK_KW_CONST ||
                                       fast_cc_peek(cc) == FTOK_KW_RESTRICT || fast_cc_peek(cc) == FTOK_KW_VOLATILE)
                                {
                                    if (ndim != 0)
                                    {
                                        fast_cc_bail(cc, "qualifier in a parameter's array declarator");
                                    }
                                    pointer_const = pointer_const || fast_cc_peek(cc) == FTOK_KW_CONST;
                                    pointer_volatile = pointer_volatile || fast_cc_peek(cc) == FTOK_KW_VOLATILE;
                                    cc->pos += 1u;
                                }
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
                                const uint32_t esz = ftype_size(cc, pt);
                                const uint32_t eal = ftype_align(cc, pt);
                                const uint64_t total_sz = dims[di] == UINT32_MAX ? 0u : (uint64_t)dims[di] * (uint64_t)esz;
                                // #3577: and an element whose size isn't a multiple of its alignment, an error.
                                if (((uint32_t)(total_sz > (uint64_t)INT32_MAX) | (uint32_t)(esz % eal != 0)) != 0)
                                {
                                    fast_cc_bail(cc, "array size overflow or misaligned element");
                                    break;
                                }
                                const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                                if (ac != 0)
                                {
                                    cc->ctypes[ac].inner = pt;
                                    cc->ctypes[ac].count = dims[di];
                                    cc->ctypes[ac].size = (uint32_t)total_sz;
                                    cc->ctypes[ac].align = eal;
                                    cc->ctypes[ac].complete = dims[di] != UINT32_MAX ? 1u : 0u;
                                    pt = ftype_from_index(cc, ac);
                                }
                            }
                            pt = ftype_make_ptr(cc, pt);
                            if (pointer_const) { pt = ftype_with_const(pt); }
                            if (pointer_volatile) { pt = ftype_with_volatile(pt); }
                        }
                        fast_cc_parse_attributes(cc, NULL);
                        // Array typedef param decays to pointer (e.g. `va_list ap`!)
                        declared_array |= (uint32_t)(pt >> 31) & (uint32_t)((pt & BUSTER_FTYPE_PTR_MASK) == 0) &
                                          (uint32_t)(cc->ctypes[pt & BUSTER_FTYPE_CIDX_MASK & BUSTER_FTYPE_COMPLEX_ALL(pt)].kind == FCTYPE_ARRAY);
                        pt = ftype_decay(cc, pt);
                        // #3577: a parameter's storage class other than register (#3599's check above leaves it
                        // first) or function specifier, a void parameter in a list (FTYPE_VOID is 0), and a name given
                        // twice (a 64-bit filter of the names, the list read only where two share a bit) are the full
                        // pipeline's. A register one is kept in its name (BUSTER_FPARAM_REGISTER): its address is.
                        const uint32_t param_bad = (uint32_t)(pspecs.is_typedef | pspecs.is_static | pspecs.is_extern | pspecs.is_inline |
                                                              pspecs.is_noreturn) |
                                                   (uint32_t)((pt & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
                        const uint64_t name_bit = (UINT64_C(1) << (pname & 63u)) & (UINT64_C(0) - (uint64_t)(pname != 0));
                        if ((param_bad | (uint32_t)((name_filter & name_bit) != 0)) != 0)
                        {
                            for (uint32_t pi = 0; pi < pcnt; pi += 1)
                            {
                                if (pname != 0 && (local_pn[pi] & BUSTER_FPARAM_NAME_MASK) == pname) { fast_cc_bail(cc, "parameter declared twice"); }
                            }
                            if (param_bad != 0) { fast_cc_bail(cc, "parameter's specifiers"); }
                            if (cc->bailout) { break; }
                        }
                        name_filter |= name_bit;
                        if (pcnt < 64u)
                        {
                            local_pt[pcnt] = pt;
                            local_pn[pcnt] = pname | (declared_array << 31) | (BUSTER_FPARAM_REGISTER * (uint32_t)pspecs.is_register);
                            pcnt += 1u;
                        }
                        else { fast_cc_bail(cc, "too many function params"); break; }
                        if (fast_cc_peek(cc) == FTOK_COMMA) { cc->pos += 1u; }
                        else { break; }
                    }
                    cc->param_depth -= 1u;
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
    // #3577: a mode attribute on the declarator gives its type (fast_cc_mode_type), as one among the specifiers does; main's
    // direct path ignored it (glibc's `typedef int register_t __attribute__ ((__mode__ (__word__)))` was an int). One on a
    // pointer's, an array's or a function's declarator is the full pipeline's error. An unmodeled one stays in the
    // attributes: a typedef's uses are refused (BUSTER_FSYM_ALIGNED), and any other declaration with it leaves.
    if (attr.mode_size != 0)
    {
        uint32_t derived = 0;
        for (uint32_t gi = 0; gi < gdepth; gi += 1) { derived |= groups[gi].ptr_count | groups[gi].suffix_count; }
        bool unmodeled = false;
        if (derived != 0) { fast_cc_bail(cc, "a mode on a derived declarator"); }
        else { base_type = fast_cc_mode_type(cc, base_type, &attr, &unmodeled); }
    }

    // Step 3: Build final FastType from outermost group down to innermost group (right-to-left C declarator rule!)
    FastType cur = base_type;
    for (uint32_t gi = 0; gi < gdepth && !cc->bailout; gi += 1)
    {
        // First apply this group's prefix pointers (left to right)
        for (uint32_t pi = 0; pi < groups[gi].ptr_count; pi += 1)
        {
            const FastDeclMod pm = mods[groups[gi].ptr_start + pi];
            // #3577: restrict qualifies a pointer to an object; to a function it is the full pipeline's error.
            if (((uint32_t)pm.is_restrict & (uint32_t)(cur >> 31) & (uint32_t)((cur & BUSTER_FTYPE_PTR_MASK) == 0) &
                 (uint32_t)(cc->ctypes[cur & BUSTER_FTYPE_CIDX_MASK & (UINT32_C(0) - (cur >> 31))].kind == FCTYPE_FUNC)) != 0)
            {
                fast_cc_bail(cc, "restrict pointer to a function");
                break;
            }
            cur = ftype_make_ptr(cc, cur);
            if (pm.is_const) { cur = ftype_with_const(cur); }
            if (pm.is_volatile) { cur = ftype_with_volatile(cur); }
        }
        // Then apply this group's suffixes in REVERSE order (innermost array/func wraps outermost)!
        for (int32_t si = (int32_t)groups[gi].suffix_count - 1; si >= 0; si -= 1)
        {
            const FastDeclMod sm = mods[groups[gi].suffix_start + (uint32_t)si];
            // #3577: an array's element is a complete object type, and a function returns neither an array nor a
            // function: anything else is the full pipeline's error.
            {
                const uint32_t complex = UINT32_C(0) - (uint32_t)(cur >> 31);
                const uint8_t kind = cc->ctypes[(cur & BUSTER_FTYPE_CIDX_MASK) & complex].kind;
                const uint32_t direct = (complex & 1u) & (uint32_t)((cur & BUSTER_FTYPE_PTR_MASK) == 0);
                const uint32_t function = direct & (uint32_t)(kind == FCTYPE_FUNC);
                const uint32_t array = direct & (uint32_t)(kind == FCTYPE_ARRAY);
                const uint32_t is_void = (uint32_t)((cur & (BUSTER_FTYPE_COMPLEX_BIT | BUSTER_FTYPE_PTR_MASK | BUSTER_FTYPE_SCALAR_MASK)) == FTYPE_VOID);
                const uint32_t bad_element = (uint32_t)(sm.kind == FDMOD_ARRAY) & (BUSTER_FAST_TYPE_INCOMPLETE(cc, cur) | function | is_void);
                const uint32_t bad_result = (uint32_t)(sm.kind == FDMOD_FUNC) & (function | array);
                if ((bad_element | bad_result) != 0)
                {
                    fast_cc_bail(cc, "array element or function result the full pipeline refuses");
                    break;
                }
            }
            if (sm.kind == FDMOD_ARRAY)
            {
                const uint32_t esz = ftype_size(cc, cur);
                const uint32_t eal = ftype_align(cc, cur);
                const uint64_t total_sz = sm.count == UINT32_MAX ? 0u : (uint64_t)sm.count * (uint64_t)esz;
                // #3577: and an element whose size isn't a multiple of its alignment (an aligned typedef's), an error.
                if (((uint32_t)(total_sz > (uint64_t)INT32_MAX) | (uint32_t)(esz % eal != 0)) != 0)
                {
                    fast_cc_bail(cc, "array size overflow or misaligned element");
                    break;
                }
                const uint32_t ac = fast_cc_alloc_ctype(cc, FCTYPE_ARRAY);
                if (ac != 0)
                {
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

// `_Static_assert ( constant-expression [, string-literal...] ) ;` (#3577), its keyword next: a true condition, a
// message of plain string literals (or none), and the `;`; anything else is the full pipeline's error.
static void fast_cc_static_assert(FastCCState* cc)
{
    cc->pos += 1u;
    bool ok = fast_cc_peek(cc) == FTOK_LPAREN;
    if (ok)
    {
        cc->pos += 1u;
        ok = fast_cc_const_int_expr(cc, true) != 0 && !cc->bailout;
    }
    if (ok && fast_cc_peek(cc) == FTOK_COMMA)
    {
        cc->pos += 1u;
        ok = fast_cc_peek(cc) == FTOK_STR_LIT;
        while (ok && fast_cc_peek(cc) == FTOK_STR_LIT) { cc->pos += 1u; }
    }
    ok = ok && fast_cc_peek(cc) == FTOK_RPAREN && fast_cc_peek_at(cc, 1u) == FTOK_SEMI;
    if (ok)
    {
        cc->pos += 2u;
    }
    else
    {
        fast_cc_bail(cc, "_Static_assert the full pipeline refuses");
    }
}

// A name with up to two plain `*` before it and nothing after it that the declarator would take (no `[`, `(`, asm
// label or attribute): half of sqlite3.c's 23,000 declarators. Its type is the base's pointers, without the full
// parser's groups and modifier lists (#3581).
static inline __attribute__((always_inline)) bool fast_cc_parse_declarator(FastCCState* cc, FastType base_type, bool allow_abstract,
                                                                          FastDeclaratorResult* out_res, bool record_param_names)
{
    const uint32_t at = cc->pos;
    bool ok = false;
    uint32_t stars = 0;
    if (at + 4u < cc->tcount)
    {
        const uint8_t* const tk = cc->tk;
        stars = (uint32_t)(tk[at] == FTOK_STAR);
        stars += (uint32_t)(stars != 0) & (uint32_t)(tk[at + 1u] == FTOK_STAR);
        const uint32_t name = at + stars;
        const uint32_t after = tk[name + 1u];
        const uint32_t plain = (uint32_t)(tk[name] == FTOK_IDENT) & (uint32_t)(after != FTOK_LBRACKET) & (uint32_t)(after != FTOK_LPAREN) &
                               (uint32_t)(after != FTOK_KW_ASM) & (uint32_t)(after != FTOK_KW_ATTRIBUTE) & (uint32_t)(after != FTOK_KW_EXTENSION) &
                               (uint32_t)!cc->bailout;
        if (plain != 0)
        {
            FastType type = base_type;
            for (uint32_t i = 0; i < stars; i += 1) { type = ftype_make_ptr(cc, type); }
            *out_res = (FastDeclaratorResult){ .type = type, .name_id = cc->td[name] };
            cc->pos = name + 1u;
            ok = !cc->bailout;
            stars = UINT32_MAX;
        }
    }
    if (stars != UINT32_MAX) { ok = fast_cc_parse_declarator_full(cc, base_type, allow_abstract, out_res, record_param_names); }
    return ok;
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
        uint32_t attr_align;      // #3577: the alignment its own attributes ask (a packed record's is theirs alone)
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
        // #3577: a tag of another kind (struct S, union S) is the full pipeline's error.
        if (cc->ctypes[existing].kind != rkind)
        {
            fast_cc_bail(cc, "tag of another kind");
            cc->scratch->used = scratch_mark;
            return FTYPE_VOID;
        }
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
        .attr_align = hattr.aligned > 1u ? hattr.aligned : 1u,
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
            // #3577: a packed record's alignment is its attributes', before the braces or after (main's direct path read
            // only the ones after: `struct __attribute__((packed, aligned(4))) Q { char c; int j; }` was 5 bytes aligned
            // to 1, where it is 8 aligned to 4).
            const uint32_t attr_al = tattr.aligned > rf->attr_align ? tattr.aligned : rf->attr_align;
            const uint32_t al = rf->packed ? attr_al : rf->max_align;
            const uint32_t final_sz = (raw_sz + al - 1u) & ~(al - 1u);

            // Commit members contiguously into cc->member_ids / cc->member_info
            const uint32_t fcnt = rf->field_count;
            // #3577: a member name given twice (an anonymous member's too), a member of incomplete type, and a flexible
            // array member that isn't the last (or in a union), or a struct ending in one that isn't a struct's last
            // member (a union may hold one anywhere), are the full pipeline's. A name is given twice when its stamp is
            // this check's already (unnamed members, ident 0, share a stamp and never count).
            {
                const uint32_t serial = ++cc->member_serial;
                uint32_t member_bad = 0;
                uint32_t const_member = 0;
                for (uint32_t k = 0; k < fcnt; k += 1)
                {
                    const uint32_t mid = tmp_ids[rf->field_start + k];
                    const FastType mt = tmp_info[rf->field_start + k].type;
                    member_bad |= (uint32_t)(cc->member_stamp[mid] == serial) & (uint32_t)(mid != 0);
                    cc->member_stamp[mid] = serial;
                    const uint32_t complex = (uint32_t)((mt & BUSTER_FTYPE_COMPLEX_BIT) != 0) & (uint32_t)((mt & BUSTER_FTYPE_PTR_MASK) == 0);
                    const FastComplexType* mct = cc->ctypes + (complex * (mt & BUSTER_FTYPE_CIDX_MASK));
                    const uint32_t last = (uint32_t)(k + 1u == fcnt);
                    const uint32_t incomplete = complex & (uint32_t)(mct->complete == 0);
                    const uint32_t flexible = incomplete & (uint32_t)(mct->kind == FCTYPE_ARRAY);
                    // A complete struct's last member (index 0 otherwise, which is read and masked), and whether its
                    // type is a flexible array.
                    const uint32_t whole_struct = complex & (uint32_t)(mct->kind == FCTYPE_STRUCT) & (uint32_t)(mct->count != 0) & (uint32_t)(mct->complete != 0);
                    const uint32_t last_member = (mct->start + mct->count - 1u) & (UINT32_C(0) - whole_struct);
                    const FastType lt = cc->member_info[last_member].type;
                    const uint32_t l_complex = (uint32_t)((lt & BUSTER_FTYPE_COMPLEX_BIT) != 0) & (uint32_t)((lt & BUSTER_FTYPE_PTR_MASK) == 0);
                    const FastComplexType* lct = cc->ctypes + (l_complex * (lt & BUSTER_FTYPE_CIDX_MASK));
                    const uint32_t ends_flexible = whole_struct & l_complex & (uint32_t)(lct->kind == FCTYPE_ARRAY) & (uint32_t)(lct->complete == 0);
                    member_bad |= (incomplete & (flexible ^ 1u)) | (flexible & ((last ^ 1u) | (uint32_t)rf->is_union)) |
                                  (ends_flexible & (last ^ 1u) & ((uint32_t)rf->is_union ^ 1u));
                    // A const member, or one that holds one (a record's, an array's element), makes assigning the
                    // record the full pipeline's error.
                    const uint32_t mcomplex = UINT32_C(0) - (uint32_t)(mt >> 31);
                    const FastType inner = mct->inner & (UINT32_C(0) - ((uint32_t)(mct->kind == FCTYPE_ARRAY) & complex));
                    const uint32_t icomplex = UINT32_C(0) - (uint32_t)(inner >> 31);
                    const FastComplexType* ict = cc->ctypes + ((inner & BUSTER_FTYPE_CIDX_MASK) & icomplex & (UINT32_C(0) - (uint32_t)((inner & BUSTER_FTYPE_PTR_MASK) == 0)));
                    const_member |= (uint32_t)((mt & ((BUSTER_FTYPE_CONST & mcomplex) | (BUSTER_FTYPE_CONST & ~mcomplex))) != 0) |
                                    (complex & (uint32_t)mct->has_const_mbr) |
                                    (uint32_t)((inner & ((BUSTER_FTYPE_CONST & icomplex) | (BUSTER_FTYPE_CONST & ~icomplex))) != 0) |
                                    (uint32_t)ict->has_const_mbr;
                }
                if (member_bad != 0) { fast_cc_bail(cc, "record member's name or type"); break; }
                cc->ctypes[rf->cidx].has_const_mbr = (uint8_t)const_member;
            }
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
            // #3577: a member below its type's alignment (a packed record's, or one packed itself), whose address is the
            // full pipeline's -Waddress-of-packed-member, is marked in its bit_signed's second bit (BUSTER_FMEMBER_UNALIGNED),
            // which a member that isn't a bit-field doesn't use otherwise.
            for (uint32_t k = 0; k < fcnt; k += 1)
            {
                FastMemberInfo* m = cc->member_info + mstart + k;
                const uint32_t offset_align = m->byte_offset == 0 ? al : (m->byte_offset & (0u - m->byte_offset));
                const uint32_t actual = offset_align < al ? offset_align : al;
                m->bit_signed |= (uint8_t)(BUSTER_FMEMBER_UNALIGNED * ((uint32_t)(m->bit_width == 0) & (uint32_t)(ftype_align(cc, m->type) > actual)));
            }

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
                    // #3577: a tagged one declares its tag and no member ("declaration does not declare anything").
                    if (cc->ctypes[finished_cidx].tag_ident != 0) { fast_cc_bail(cc, "tagged record member declares nothing"); break; }
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
                        // #3577: a mode this path can't give its member (fast_cc_mode_type).
                        if (dr.attr.mode_size == BUSTER_FMODE_UNMODELED) { fast_cc_bail(cc, "a member's mode this path doesn't model"); break; }
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
        // #3577: __extension__ before _Static_assert in a member list is the full pipeline's error.
        if (fast_cc_peek(cc) == FTOK_KW_EXTENSION && fast_cc_peek_at(cc, 1u) == FTOK_KW_STATIC_ASSERT)
        {
            fast_cc_bail(cc, "__extension__ _Static_assert member");
            break;
        }
        if (fast_cc_peek(cc) == FTOK_SEMI || fast_cc_peek(cc) == FTOK_KW_EXTENSION)
        {
            cc->pos += 1u;
            continue;
        }
        if (fast_cc_peek(cc) == FTOK_KW_STATIC_ASSERT)
        {
            fast_cc_static_assert(cc);
            if (cc->bailout) { break; }
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
            // #3577: a qualified nested record's members carry the qualifier, which this layout drops.
            if (nest_const || cc->pos != save_nest + 0u)
            {
                bool qualified = false;
                for (uint32_t q = save_nest; q < cc->pos; q += 1) { qualified = qualified || cc->tk[q] == FTOK_KW_CONST || cc->tk[q] == FTOK_KW_VOLATILE; }
                if (qualified) { fast_cc_bail(cc, "qualified nested record member"); break; }
            }
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
            // #3577: a record defined inside a packed one isn't packed (main's direct path packed it: its members at their
            // natural offsets, its alignment 1); only its place as the outer record's member is.
            stack[sp++] = (RecFrame){
                .cidx = sub_cidx,
                .is_union = sub_union,
                .packed = 0,
                .cur_byte = 0,
                .cur_bit = 0,
                .max_align = 1u,
                .attr_align = 1u,
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
        if (fspecs.written != 0) { fast_cc_bail(cc, "a member's storage class or function specifier"); }
        if (cc->bailout) { break; }
        if (fast_cc_peek(cc) == FTOK_SEMI)
        {
            // Anonymous struct/union typedef field (e.g. `MyUnion;`): a Microsoft extension the full pipeline warns of,
            // or a declaration that declares nothing (#3577).
            fast_cc_bail(cc, "member that declares nothing or a typedef's anonymous record");
            break;
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
            // #3577: a mode this path can't give its member (fast_cc_mode_type).
            if (dr.attr.mode_size == BUSTER_FMODE_UNMODELED) { fast_cc_bail(cc, "a member's mode this path doesn't model"); break; }
            if (fast_cc_peek(cc) == FTOK_COLON)
            {
                // Bitfield! SysV AMD64 ABI bitfield layout
                cc->pos += 1u;
                const int64_t bw = fast_cc_const_int_expr(cc, true);
                if (bw < 0 || bw > 64) { fast_cc_bail(cc, "invalid bitfield width"); break; }
                // #3577: an attribute after the width is the bit-field's (packed, aligned, a mode), which this layout
                // doesn't take: left unread, main's direct path took it for another member's start, an int (`unsigned
                // a : 3 __attribute__((packed)); int b;` was 12 bytes, the full pipeline's 8).
                if (fast_cc_peek(cc) == FTOK_KW_ATTRIBUTE) { fast_cc_bail(cc, "an attribute after a bit-field's width"); break; }
                // #3577: a zero width is an unnamed bit-field's alone.
                if (bw == 0 && dr.name_id != 0) { fast_cc_bail(cc, "named zero-width bit-field"); break; }
                const uint32_t usize = ftype_size(cc, dr.type);
                const uint32_t ualign = rf->packed ? 1u : ftype_align(cc, dr.type);
                const uint32_t ubits = usize * 8u;
                // #3577: wider than its type (a _Bool's is one bit) is the full pipeline's error.
                if ((uint64_t)bw > (ftype_is_bool(dr.type) ? 1u : ubits)) { fast_cc_bail(cc, "a bit-field wider than its type"); break; }
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
static __attribute__((noinline)) BUSTER_DIRECT_HOT FastType fast_cc_parse_decl_specs_full(FastCCState* cc, FastDeclSpecs* out_specs)
{
    // Filled where the caller reads it: a copy of a record written a field at a time waits on those stores.
    FastDeclSpecs local_specs;
    FastDeclSpecs* const sp = out_specs != NULL ? out_specs : &local_specs;
    *sp = (FastDeclSpecs){ 0 };
    uint32_t saw_void = 0, saw_char = 0, saw_short = 0, saw_int = 0, saw_long = 0;
    uint32_t saw_float = 0, saw_double = 0, saw_signed = 0, saw_unsigned = 0, saw_bool = 0;
    // #3577: what C allows once (a storage class, each qualifier and function specifier) and the type keywords'
    // combination, checked after the list.
    uint32_t saw_complex = 0, storage = 0, saw_const = 0, saw_volatile = 0, saw_inline = 0, saw_noreturn = 0;
    uint32_t int128_signs = UINT32_MAX;
    bool has_explicit_type = false;
    bool saw_typeof = false;
    FastType explicit_type = FTYPE_I32;

    while (cc->pos < cc->tcount && !cc->bailout)
    {
        const uint8_t k = fast_cc_peek(cc);
        if (k == FTOK_KW_TYPEDEF) { sp->is_typedef = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_TYPEDEF); }
        else if (k == FTOK_KW_STATIC) { sp->is_static = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_STATIC); }
        else if (k == FTOK_KW_EXTERN) { sp->is_extern = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_EXTERN); }
        else if (k == FTOK_KW_INLINE) { sp->is_inline = 1u; saw_inline += 1u; sp->written |= BUSTER_FSPEC_INLINE; cc->pos += 1u; }
        else if (k == FTOK_KW_REGISTER) { sp->is_register = 1u; storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_REGISTER); }
        else if (k == FTOK_KW_AUTO) { storage += 1u; fast_cc_storage_class(cc, sp, BUSTER_FSPEC_AUTO); }
        else if (k == FTOK_KW_EXTENSION) { cc->pos += 1u; }
        else if (k == FTOK_KW_NORETURN) { sp->is_noreturn = 1u; saw_noreturn += 1u; sp->written |= BUSTER_FSPEC_NORETURN; cc->pos += 1u; }
        else if (k == FTOK_KW_CONST) { sp->is_const = 1u; saw_const += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_VOLATILE) { sp->is_volatile = 1u; saw_volatile += 1u; cc->pos += 1u; }
        else if (k == FTOK_KW_RESTRICT || k == FTOK_KW_ALIGNAS)
        {
            // restrict on a specifier list's type, and _Alignas: the full pipeline's. (auto is a storage class, and
            // without a type specifier C23's inference, which leaves at the type's end, #3599.)
            fast_cc_bail(cc, "restrict or _Alignas specifier");
            break;
        }
        else if (k == FTOK_KW_ATTRIBUTE) { fast_cc_parse_attributes(cc, &sp->attr); }
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
            int128_signs = saw_signed + saw_unsigned;
            has_explicit_type = true;
            explicit_type = is_u ? FTYPE_U128 : FTYPE_I128;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_COMPLEX)
        {
            sp->is_unsupported_type = 1u;
            saw_complex += 1u;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_FLOAT128 || k == FTOK_KW_FLOAT16)
        {
            sp->is_unsupported_type = 1u;
            has_explicit_type = true;
            explicit_type = k == FTOK_KW_FLOAT16 ? FTYPE_UNSUP2 : FTYPE_UNSUP16;
            cc->pos += 1u;
        }
        else if (k == FTOK_KW_TYPEOF && !has_explicit_type && fast_cc_peek_at(cc, 1u) == FTOK_LPAREN &&
                 fast_cc_peek_at(cc, 2u) == FTOK_IDENT && fast_cc_peek_at(cc, 3u) == FTOK_RPAREN)
        {
            // typeof(name) of a declared function, object or typedef name: its type, qualifiers and all, as GNU's
            // weak_alias(old, new) asks of it (#3592). Any other operand leaves, as before.
            const uint32_t id = cc->td[cc->pos + 2u];
            const uint32_t si = id < cc->env_cap ? cc->sym_cur[id] : 0u;
            const uint8_t kind = si != 0 ? cc->syms[si].kind : (uint8_t)FSYM_NONE;
            if (kind != FSYM_FUNC && kind != FSYM_GLOBAL && kind != FSYM_LOCAL && kind != FSYM_TYPEDEF)
            {
                fast_cc_bail(cc, "unsupported type specifier");
                break;
            }
            has_explicit_type = true;
            saw_typeof = true;
            explicit_type = cc->syms[si].type;
            if ((cc->syms[si].use_flags & BUSTER_FSYM_UNSUPPORTED) != 0) { sp->is_unsupported_type = 1u; }
            // #3577: a typedef with an alignment of its own, as its name's use is.
            if ((cc->syms[si].use_flags & BUSTER_FSYM_ALIGNED) != 0) { fast_cc_bail(cc, "use of an aligned typedef"); break; }
            cc->pos += 4u;
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
            // #3577: a struct or union declares something by itself only with a tag (`struct { ... };` declares
            // nothing, -Wmissing-declarations).
            sp->has_tag = (uint8_t)((explicit_type >> 31) & (uint32_t)(cc->ctypes[explicit_type & BUSTER_FTYPE_CIDX_MASK & (UINT32_C(0) - (explicit_type >> 31))].tag_ident != 0));
        }
        else if (k == FTOK_KW_ENUM)
        {
            if (has_explicit_type) { break; }
            has_explicit_type = true;
            sp->has_tag = 1u;
            explicit_type = fast_cc_parse_enum(cc);
        }
        else if (k == FTOK_IDENT && !has_explicit_type &&
                 (saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_signed + saw_unsigned + saw_bool) == 0 &&
                 fast_cc_is_typedef_name(cc, fast_cc_data(cc)))
        {
            has_explicit_type = true;
            const FastSym* typedef_sym = cc->syms + cc->sym_cur[fast_cc_data(cc)];
            explicit_type = typedef_sym->type;
            // #3577: a typedef with an alignment of its own (BUSTER_FSYM_ALIGNED).
            if ((typedef_sym->use_flags & BUSTER_FSYM_ALIGNED) != 0) { fast_cc_bail(cc, "use of an aligned typedef"); }
            cc->pos += 1u;
        }
        else
        {
            break;
        }
    }

    // typeof(name) is the whole type: another type specifier or a mode attribute with it leaves.
    if (saw_typeof && ((saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_signed + saw_unsigned + saw_bool) != 0 ||
                       sp->attr.mode_size != 0))
    {
        fast_cc_bail(cc, "type specifiers beside typeof");
    }
    // #3577: the type keywords make one of C's types, and what C allows once comes once. A list that names no type
    // (implicit int, an error since C99), two types, a keyword more often than C allows (`int int`, `long long long`,
    // `signed unsigned`, `const const`, two storage classes), or a combination C has none of (`long char`, `signed
    // float`, `short long`, `_Complex int`, a sign after __int128) is the full pipeline's diagnostic.
    const uint32_t signs = saw_signed + saw_unsigned;
    const uint32_t named = (uint32_t)has_explicit_type;
    // Most lists are one type word (`int`, a typedef's name, a tag) with at most one storage class and qualifier of each
    // kind, which C allows whatever the word: the whole check below runs for the other lists.
    const uint32_t words = saw_void + saw_char + saw_short + saw_int + saw_long + saw_float + saw_double + saw_bool + signs + saw_complex + named;
    const uint32_t one_word = (uint32_t)(words == 1u) & (uint32_t)(saw_complex == 0) &
                              (uint32_t)((storage | saw_const | saw_volatile | saw_inline | saw_noreturn) < 2u);
    if (one_word == 0)
    {
        const uint32_t bases = (uint32_t)(saw_void != 0) + (uint32_t)(saw_char != 0) + (uint32_t)(saw_int != 0) + (uint32_t)(saw_float != 0) +
                               (uint32_t)(saw_double != 0) + (uint32_t)(saw_bool != 0) + named;
        const uint32_t int128_first = (uint32_t)(int128_signs == signs);
        const uint32_t specifiers_bad =
            (uint32_t)(bases > 1u) | (uint32_t)(bases + saw_short + saw_long + signs == 0) |
            (uint32_t)(saw_void > 1u) | (uint32_t)(saw_char > 1u) | (uint32_t)(saw_int > 1u) | (uint32_t)(saw_float > 1u) |
            (uint32_t)(saw_double > 1u) | (uint32_t)(saw_bool > 1u) | (uint32_t)(saw_short > 1u) | (uint32_t)(saw_long > 2u) |
            (uint32_t)(signs > 1u) | (uint32_t)(saw_complex > 1u) |
            ((uint32_t)(saw_short != 0) & ((uint32_t)(saw_long != 0) | ((uint32_t)(bases != 0) & (uint32_t)(saw_int == 0)))) |
            ((uint32_t)(saw_long != 0) & (uint32_t)(bases != 0) & (uint32_t)(saw_int == 0) & ((uint32_t)(saw_double == 0) | (uint32_t)(saw_long > 1u))) |
            ((uint32_t)(signs != 0) & (uint32_t)(bases != 0) & (uint32_t)(saw_char == 0) & (uint32_t)(saw_int == 0) & (int128_first ^ 1u)) |
            (named & (uint32_t)((saw_short | saw_long) != 0)) | ((uint32_t)(saw_complex != 0) & (uint32_t)((saw_float | saw_double) == 0)) |
            (uint32_t)(storage > 1u) | (uint32_t)(saw_const > 1u) | (uint32_t)(saw_volatile > 1u) | (uint32_t)(saw_inline > 1u) |
            (uint32_t)(saw_noreturn > 1u);
        if (specifiers_bad != 0)
        {
            fast_cc_bail(cc, "declaration specifiers");
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
        res = saw_long ? BUSTER_FTYPE_LONG_DOUBLE : FTYPE_F64;
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
    else
    {
        // No type specifier is implicit int, which C99 refused, or C23's auto, whose type is its initializer's (#3599).
        res = FTYPE_I32;
        if (saw_int + saw_signed == 0u) { fast_cc_bail(cc, "a declaration without a type specifier"); }
    }
    if (saw_complex != 0) { res = FTYPE_UNSUP16; }

    // Apply __attribute__((__mode__(...))) overrides (used by glibc stdint/math headers!)
    // #3577: a mode's type (fast_cc_mode_type); an unmodeled one is refused where it is used (a typedef's uses,
    // BUSTER_FSYM_ALIGNED; an object's, unsupported).
    if (sp->attr.mode_size != 0)
    {
        bool unmodeled = false;
        res = fast_cc_mode_type(cc, res, &sp->attr, &unmodeled);
        sp->is_unsupported_type |= (uint8_t)unmodeled;
    }

    if (sp->is_const) { res = ftype_with_const(res); }
    if (sp->is_volatile) { res |= BUSTER_FTYPE_VOL; }
    sp->type = res;
    return res;
}


static inline __attribute__((always_inline)) FastType fast_cc_parse_decl_specs(FastCCState* cc, FastDeclSpecs* out_specs)
{
    FastType res = FTYPE_I32;
    if (!fast_cc_specs_of_one_token(cc, out_specs, &res)) { res = fast_cc_parse_decl_specs_full(cc, out_specs); }
    return res;
}

// Level 8: Full typename parser for casts `(T)`, `sizeof(T)`, `_Alignof(T)`, `offsetof(T, ...)`, `_Generic`, compound literals.
static FastType fast_cc_parse_typename(FastCCState* cc, bool* out_ok)
{
    FastDeclSpecs sp = { 0 };
    const FastType base = fast_cc_parse_decl_specs(cc, &sp);
    if (sp.is_unsupported_type) { fast_cc_bail(cc, "unsupported type in typename"); }
    // #3577: a type name declares no object or function: a storage class, a function specifier (#3599's written ones)
    // or an attribute in one is the full pipeline's to judge.
    if (((uint32_t)(sp.written != 0) | (uint32_t)sp.attr.present) != 0)
    {
        fast_cc_bail(cc, "specifier or attribute in a type name");
    }
    FastDeclaratorResult dr = { 0 };
    bool ok = !cc->bailout && fast_cc_parse_declarator(cc, base, true, &dr, false);
    if (ok && dr.attr.present != 0)
    {
        fast_cc_bail(cc, "attribute in a type name");
        ok = false;
    }
    if (out_ok != NULL) { *out_ok = ok; }
    return ok ? dr.type : FTYPE_VOID;
}

static bool fast_cc_init_state(FastCCState* cc, FastArena* arena, FastArena* scratch, FastPP* pp, FastObj* obj,
                               bool unsigned_char, bool quiet)
{
    // The state, including deferred bodies, starts on fresh zero-filled scratch pages.
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
    cc->member_stamp = (uint32_t*)fast_arena_alloc(arena, (uint64_t)cc->env_cap * sizeof(uint32_t), 64);
    cc->syms = (FastSym*)fast_arena_alloc(arena, (uint64_t)cc->sym_cap * sizeof(FastSym), 64);
    cc->undo = (FastScopeUndo*)fast_arena_alloc(arena, (uint64_t)cc->undo_cap * sizeof(FastScopeUndo), 64);
    cc->aliases = (FastAlias*)fast_arena_alloc(arena, (uint64_t)BUSTER_FALIAS_LIMIT * sizeof(FastAlias), 64);

    bool ok = cc->ctypes != NULL && cc->member_ids != NULL && cc->member_info != NULL && cc->member_hash != NULL &&
              cc->param_types != NULL && cc->param_names != NULL && cc->sym_cur != NULL && cc->tag_cur != NULL &&
              cc->member_stamp != NULL && cc->syms != NULL && cc->undo != NULL && cc->aliases != NULL;
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
        cc->id_builtin_ceil = fast_ident_intern_cstr(&pp->idents, "__builtin_ceil");
        cc->id_builtin_floor = fast_ident_intern_cstr(&pp->idents, "__builtin_floor");
        cc->id_builtin_ceilf = fast_ident_intern_cstr(&pp->idents, "__builtin_ceilf");
        cc->id_builtin_floorf = fast_ident_intern_cstr(&pp->idents, "__builtin_floorf");
        cc->id_builtin_huge_val = fast_ident_intern_cstr(&pp->idents, "__builtin_huge_val");
        cc->id_builtin_huge_valf = fast_ident_intern_cstr(&pp->idents, "__builtin_huge_valf");
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
        cc->id_assert_fail = fast_ident_intern_cstr(&pp->idents, "__assert_fail");
        cc->id_void = fast_ident_intern_cstr(&pp->idents, "void");
        cc->id_pretty_function = fast_ident_intern_cstr(&pp->idents, "__PRETTY_FUNCTION__");
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
