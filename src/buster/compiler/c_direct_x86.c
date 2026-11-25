// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Direct x86-64 machine-code byte emitter and zero-copy ELF64 object writer for c_direct (#2850).
//
// Design:
//   - Emits x86-64 machine-code bytes directly into a linear .text buffer via unaligned scalar stores,
//     avoiding X86Instruction materialization, branch-sizing passes, code atoms, and intermediate copies.
//   - Builds Elf64_Rela relocations, Elf64_Sym symbols, .rodata/.rodata.str1.1/.data/.data.rel.ro/.bss,
//     and standard DWARF .eh_frame CIE+FDE records directly in ELF64 binary format.
//   - Writes the complete ELF64 .o file in a single writev() call.
//   - Strictly iterative (no recursion), arena-backed, safe bounds checking.

#ifndef BUSTER_C_DIRECT_X86_INCLUDED
#define BUSTER_C_DIRECT_X86_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <elf.h>

// ---------------------------------------------------------------------------------------------------------------------
// x86-64 register & condition code constants

typedef enum FastReg
{
    FREG_RAX = 0,
    FREG_RCX = 1,
    FREG_RDX = 2,
    FREG_RBX = 3,
    FREG_RSP = 4,
    FREG_RBP = 5,
    FREG_RSI = 6,
    FREG_RDI = 7,
    FREG_R8  = 8,
    FREG_R9  = 9,
    FREG_R10 = 10,
    FREG_R11 = 11,
    FREG_R12 = 12,
    FREG_R13 = 13,
    FREG_R14 = 14,
    FREG_R15 = 15
} FastReg;

// x86-64 condition codes (exact opcode low nibble for 0F 8x jcc / 0F 9x setcc)
typedef enum FastCC
{
    FCC_O  = 0x0,
    FCC_NO = 0x1,
    FCC_B  = 0x2,   // unsigned <
    FCC_AE = 0x3,   // unsigned >=
    FCC_E  = 0x4,   // ==
    FCC_NE = 0x5,   // !=
    FCC_BE = 0x6,   // unsigned <=
    FCC_A  = 0x7,   // unsigned >
    FCC_S  = 0x8,
    FCC_NS = 0x9,
    FCC_P  = 0xa,   // parity (NaN in ucomisd)
    FCC_NP = 0xb,
    FCC_L  = 0xc,   // signed <
    FCC_GE = 0xd,   // signed >=
    FCC_LE = 0xe,   // signed <=
    FCC_G  = 0xf    // signed >
} FastCC;

// Section IDs matching our ELF64 section header table
typedef enum FastSecId
{
    FSEC_NULL = 0,
    FSEC_TEXT = 1,
    FSEC_RELA_TEXT = 2,
    FSEC_RODATA = 3,
    FSEC_RELA_RODATA = 4,
    FSEC_RODATA_STR = 5,
    FSEC_DATA = 6,
    FSEC_RELA_DATA = 7,
    FSEC_DATA_REL_RO = 8,
    FSEC_RELA_DATA_REL_RO = 9,
    FSEC_BSS = 10,
    FSEC_EH_FRAME = 11,
    FSEC_RELA_EH_FRAME = 12,
    FSEC_NOTE_STACK = 13,
    FSEC_COMMENT = 14,
    FSEC_SYMTAB = 15,
    FSEC_STRTAB = 16,
    FSEC_SHSTRTAB = 17,
    FSEC_COUNT = 18
} FastSecId;

// ---------------------------------------------------------------------------------------------------------------------
// Object buffers, symbols, relocations, and per-function label/jump tables

typedef struct FastByteBuf FastByteBuf;
struct FastByteBuf
{
    uint8_t* data;
    uint32_t len;
    uint32_t cap;
};

typedef struct FastRelaBuf FastRelaBuf;
struct FastRelaBuf
{
    Elf64_Rela* data;
    uint32_t count;
    uint32_t cap;
};

// Symbol metadata recorded during compilation (sorted local-first when finalizing .symtab!)
typedef struct FastObjSym FastObjSym;
struct FastObjSym
{
    uint32_t name_off;    // offset in strtab
    uint32_t section;     // FSEC_* or SHN_UNDEF (0)
    uint64_t value;       // offset within section
    uint64_t size;        // symbol size
    uint8_t binding;      // STB_LOCAL, STB_GLOBAL, STB_WEAK
    uint8_t type;         // STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION
    uint8_t visibility;   // STV_DEFAULT, STV_HIDDEN, STV_PROTECTED
    uint8_t defined;      // 1 if defined in this TU
    uint32_t final_idx;   // remapped index in ELF .symtab (assigned at finalize!)
};

typedef struct FastJumpFixup FastJumpFixup;
struct FastJumpFixup
{
    uint32_t patch_off;   // offset of rel32 inside .text
    uint32_t label_id;    // target label ID
};

typedef struct FastFuncFDE FastFuncFDE;
struct FastFuncFDE
{
    uint32_t text_off;    // function start offset in .text
    uint32_t text_size;   // function byte length
};

typedef struct FastObj FastObj;
struct FastObj
{
    FastArena* arena;
    FastByteBuf text;
    FastByteBuf rodata;
    FastByteBuf rodata_str;
    FastByteBuf data;
    FastByteBuf data_rel_ro;
    FastByteBuf eh_frame;
    uint64_t bss_size;
    uint64_t bss_align;

    FastRelaBuf rela_text;
    FastRelaBuf rela_rodata;
    FastRelaBuf rela_data;
    FastRelaBuf rela_data_rel_ro;
    FastRelaBuf rela_eh_frame;

    FastObjSym* syms;
    uint32_t sym_count;
    uint32_t sym_cap;

    FastByteBuf strtab;

    // Pre-allocated section symbol IDs (indices 1..6 in syms[])
    uint32_t sec_sym_text;
    uint32_t sec_sym_rodata;
    uint32_t sec_sym_rodata_str;
    uint32_t sec_sym_data;
    uint32_t sec_sym_data_rel_ro;
    uint32_t sec_sym_bss;

    // FDE records for .eh_frame
    FastFuncFDE* fdes;
    uint32_t fde_count;
    uint32_t fde_cap;

    // Per-function local labels & jump fixups (reset per function!)
    uint32_t* labels;          // label_id -> .text offset (UINT32_MAX if unbound)
    uint32_t label_count;
    uint32_t label_cap;
    FastJumpFixup* fixups;
    uint32_t fixup_count;
    uint32_t fixup_cap;

    // Section max alignments
    uint32_t text_align;
    uint32_t rodata_align;
    uint32_t data_align;
    uint32_t data_rel_ro_align;

    bool pic;
    bool unwind_tables;
    bool overflow;
};

static bool fast_obj_init(FastObj* obj, FastArena* arena, bool pic, bool unwind_tables)
{
    memset(obj, 0, sizeof(*obj));
    obj->arena = arena;
    obj->pic = pic;
    obj->unwind_tables = unwind_tables;
    obj->text_align = 16u;
    obj->rodata_align = 16u;
    obj->data_align = 16u;
    obj->data_rel_ro_align = 16u;
    obj->bss_align = 8u;

    obj->text.cap = 2560u * 1024u;
    obj->rodata.cap = 256u * 1024u;
    obj->rodata_str.cap = 256u * 1024u;
    obj->data.cap = 256u * 1024u;
    obj->data_rel_ro.cap = 128u * 1024u;
    obj->eh_frame.cap = 256u * 1024u;
    obj->strtab.cap = 256u * 1024u;

    obj->rela_text.cap = 65536u;
    obj->rela_rodata.cap = 8192u;
    obj->rela_data.cap = 4096u;
    obj->rela_data_rel_ro.cap = 4096u;
    obj->rela_eh_frame.cap = 8192u;

    obj->sym_cap = 16384u;
    obj->fde_cap = 8192u;
    obj->label_cap = 8192u;
    obj->fixup_cap = 16384u;

    obj->text.data = (uint8_t*)fast_arena_alloc(arena, obj->text.cap, 64);
    obj->rodata.data = (uint8_t*)fast_arena_alloc(arena, obj->rodata.cap, 64);
    obj->rodata_str.data = (uint8_t*)fast_arena_alloc(arena, obj->rodata_str.cap, 64);
    obj->data.data = (uint8_t*)fast_arena_alloc(arena, obj->data.cap, 64);
    obj->data_rel_ro.data = (uint8_t*)fast_arena_alloc(arena, obj->data_rel_ro.cap, 64);
    obj->eh_frame.data = (uint8_t*)fast_arena_alloc(arena, obj->eh_frame.cap, 64);
    obj->strtab.data = (uint8_t*)fast_arena_alloc(arena, obj->strtab.cap, 64);

    obj->rela_text.data = (Elf64_Rela*)fast_arena_alloc(arena, (uint64_t)obj->rela_text.cap * sizeof(Elf64_Rela), 64);
    obj->rela_rodata.data = (Elf64_Rela*)fast_arena_alloc(arena, (uint64_t)obj->rela_rodata.cap * sizeof(Elf64_Rela), 64);
    obj->rela_data.data = (Elf64_Rela*)fast_arena_alloc(arena, (uint64_t)obj->rela_data.cap * sizeof(Elf64_Rela), 64);
    obj->rela_data_rel_ro.data = (Elf64_Rela*)fast_arena_alloc(arena, (uint64_t)obj->rela_data_rel_ro.cap * sizeof(Elf64_Rela), 64);
    obj->rela_eh_frame.data = (Elf64_Rela*)fast_arena_alloc(arena, (uint64_t)obj->rela_eh_frame.cap * sizeof(Elf64_Rela), 64);

    obj->syms = (FastObjSym*)fast_arena_alloc(arena, (uint64_t)obj->sym_cap * sizeof(FastObjSym), 64);
    obj->fdes = (FastFuncFDE*)fast_arena_alloc(arena, (uint64_t)obj->fde_cap * sizeof(FastFuncFDE), 64);
    obj->labels = (uint32_t*)fast_arena_alloc(arena, (uint64_t)obj->label_cap * sizeof(uint32_t), 64);
    obj->fixups = (FastJumpFixup*)fast_arena_alloc(arena, (uint64_t)obj->fixup_cap * sizeof(FastJumpFixup), 64);

    bool ok = obj->text.data != NULL && obj->rodata.data != NULL && obj->rodata_str.data != NULL &&
              obj->data.data != NULL && obj->data_rel_ro.data != NULL && obj->eh_frame.data != NULL &&
              obj->strtab.data != NULL && obj->rela_text.data != NULL && obj->rela_rodata.data != NULL &&
              obj->rela_data.data != NULL && obj->rela_data_rel_ro.data != NULL && obj->rela_eh_frame.data != NULL &&
              obj->syms != NULL && obj->fdes != NULL && obj->labels != NULL && obj->fixups != NULL;
    if (ok)
    {
        // Strtab byte 0 is always '\0'
        obj->strtab.data[0] = 0;
        obj->strtab.len = 1u;

        // Symbol 0 is always STN_UNDEF (NULL symbol)
        obj->syms[0] = (FastObjSym){ 0 };
        obj->sym_count = 1u;

        // Add section symbols (always local, indices 1..6)
        const uint32_t secs[6] = { FSEC_TEXT, FSEC_RODATA, FSEC_RODATA_STR, FSEC_DATA, FSEC_DATA_REL_RO, FSEC_BSS };
        for (uint32_t i = 0; i < 6u; i += 1)
        {
            const uint32_t idx = obj->sym_count++;
            obj->syms[idx] = (FastObjSym){
                .name_off = 0,
                .section = secs[i],
                .value = 0,
                .size = 0,
                .binding = STB_LOCAL,
                .type = STT_SECTION,
                .visibility = STV_DEFAULT,
                .defined = 1u,
            };
        }
        obj->sec_sym_text = 1u;
        obj->sec_sym_rodata = 2u;
        obj->sec_sym_rodata_str = 3u;
        obj->sec_sym_data = 4u;
        obj->sec_sym_data_rel_ro = 5u;
        obj->sec_sym_bss = 6u;
    }
    return ok;
}

static uint32_t fast_obj_add_str(FastObj* obj, const char* s, uint32_t len)
{
    uint32_t off = 0;
    if (obj->strtab.len + len + 1u <= obj->strtab.cap)
    {
        off = obj->strtab.len;
        memcpy(obj->strtab.data + off, s, len);
        obj->strtab.data[off + len] = '\0';
        obj->strtab.len += len + 1u;
    }
    else
    {
        obj->overflow = true;
    }
    return off;
}

static uint32_t fast_obj_add_sym(FastObj* obj, const char* name, uint32_t name_len, uint8_t binding, uint8_t type)
{
    uint32_t idx = 0;
    if (obj->sym_count < obj->sym_cap)
    {
        idx = obj->sym_count++;
        obj->syms[idx] = (FastObjSym){
            .name_off = name_len > 0 ? fast_obj_add_str(obj, name, name_len) : 0u,
            .section = SHN_UNDEF,
            .value = 0,
            .size = 0,
            .binding = binding,
            .type = type,
            .visibility = STV_DEFAULT,
            .defined = 0u,
        };
    }
    else
    {
        obj->overflow = true;
    }
    return idx;
}

static uint32_t fast_obj_add_string_lit(FastObj* obj, const char* bytes, uint32_t len)
{
    uint32_t off = 0;
    if (obj->rodata_str.len + len + 1u <= obj->rodata_str.cap)
    {
        off = obj->rodata_str.len;
        memcpy(obj->rodata_str.data + off, bytes, len);
        obj->rodata_str.data[off + len] = '\0';
        obj->rodata_str.len += len + 1u;
    }
    else
    {
        obj->overflow = true;
    }
    return off;
}

static uint32_t fast_obj_alloc_section(FastObj* obj, FastSecId sec, uint32_t size, uint32_t align, uint8_t** out_ptr)
{
    uint32_t off = 0;
    if (align == 0) { align = 1u; }
    if (sec == FSEC_BSS)
    {
        const uint64_t mask = (uint64_t)align - 1u;
        const uint64_t aligned = (obj->bss_size + mask) & ~mask;
        off = (uint32_t)aligned;
        obj->bss_size = aligned + size;
        if (align > obj->bss_align) { obj->bss_align = align; }
        if (out_ptr != NULL) { *out_ptr = NULL; }
        return off;
    }
    FastByteBuf* buf = sec == FSEC_DATA ? &obj->data :
                       sec == FSEC_RODATA ? &obj->rodata :
                       sec == FSEC_DATA_REL_RO ? &obj->data_rel_ro : &obj->rodata_str;
    if (sec == FSEC_DATA && align > obj->data_align) { obj->data_align = align; }
    if (sec == FSEC_RODATA && align > obj->rodata_align) { obj->rodata_align = align; }
    if (sec == FSEC_DATA_REL_RO && align > obj->data_rel_ro_align) { obj->data_rel_ro_align = align; }
    const uint32_t mask = align - 1u;
    const uint32_t aligned = (buf->len + mask) & ~mask;
    if (aligned + size <= buf->cap)
    {
        if (aligned > buf->len)
        {
            memset(buf->data + buf->len, 0, aligned - buf->len);
        }
        memset(buf->data + aligned, 0, size);
        off = aligned;
        buf->len = aligned + size;
        if (out_ptr != NULL) { *out_ptr = buf->data + off; }
    }
    else
    {
        obj->overflow = true;
        if (out_ptr != NULL) { *out_ptr = NULL; }
    }
    return off;
}

static inline void fast_obj_add_rela(FastObj* obj, FastRelaBuf* rb, uint64_t offset, uint32_t sym_idx, uint32_t rtype, int64_t addend)
{
    if (__builtin_expect(rb->count < rb->cap, 1))
    {
        rb->data[rb->count++] = (Elf64_Rela){
            .r_offset = offset,
            .r_info = ELF64_R_INFO((uint64_t)sym_idx, (uint64_t)rtype),
            .r_addend = addend,
        };
    }
    else
    {
        obj->overflow = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Direct inline x86-64 machine-code emission helpers (zero-overhead unaligned stores into .text!)

static inline void x86_u8(FastObj* obj, uint8_t b)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        obj->text.data[obj->text.len++] = b;
    }
    else
    {
        obj->overflow = true;
    }
}

static inline void x86_u16(FastObj* obj, uint16_t v)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        memcpy(obj->text.data + obj->text.len, &v, 2);
        obj->text.len += 2u;
    }
    else
    {
        obj->overflow = true;
    }
}

static inline void x86_u32(FastObj* obj, uint32_t v)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        memcpy(obj->text.data + obj->text.len, &v, 4);
        obj->text.len += 4u;
    }
    else
    {
        obj->overflow = true;
    }
}

static inline void x86_u64(FastObj* obj, uint64_t v)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        memcpy(obj->text.data + obj->text.len, &v, 8);
        obj->text.len += 8u;
    }
    else
    {
        obj->overflow = true;
    }
}

// REX prefix builder: W=64-bit, R=reg.3, X=idx.3, B=rm/base.3
static inline void x86_rex(FastObj* obj, bool w, uint32_t reg, uint32_t idx, uint32_t rm, bool force)
{
    const uint8_t rex = (uint8_t)(0x40u | (w ? 8u : 0u) | ((reg & 8u) >> 1) | ((idx & 8u) >> 2) | ((rm & 8u) >> 3));
    if (force || rex != 0x40u)
    {
        x86_u8(obj, rex);
    }
}

// Emit ModRM + optional SIB + disp directly into `p`, returning updated `p`!
static inline __attribute__((always_inline)) uint8_t* x86_put_mem_disp(uint8_t* p, uint32_t reg, uint32_t base, int32_t disp)
{
    const uint32_t r = reg & 7u;
    const uint32_t b = base & 7u;
    if (disp == 0 && b != 5u)
    {
        *p++ = (uint8_t)((r << 3) | b);
        if (b == 4u) { *p++ = 0x24u; }
    }
    else if (disp >= -128 && disp <= 127)
    {
        *p++ = (uint8_t)(0x40u | (r << 3) | b);
        if (b == 4u) { *p++ = 0x24u; }
        *p++ = (uint8_t)(int8_t)disp;
    }
    else
    {
        *p++ = (uint8_t)(0x80u | (r << 3) | b);
        if (b == 4u) { *p++ = 0x24u; }
        memcpy(p, &disp, 4);
        p += 4;
    }
    return p;
}

// Emit ModRM + optional SIB + disp for [base + disp32]
static inline __attribute__((always_inline)) void x86_mem_disp(FastObj* obj, uint32_t reg, uint32_t base, int32_t disp)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        uint8_t* p = obj->text.data + obj->text.len;
        p = x86_put_mem_disp(p, reg, base, disp);
        obj->text.len = (uint32_t)(p - obj->text.data);
    }
    else { obj->overflow = true; }
}

// Emit ModRM + SIB + disp for [base + idx*scale + disp] (scale_log2: 0=1, 1=2, 2=4, 3=8; idx != RSP)
static inline void x86_mem_sib(FastObj* obj, uint32_t reg, uint32_t base, uint32_t idx, uint32_t scale_log2, int32_t disp)
{
    const uint32_t r = reg & 7u;
    const uint32_t b = base & 7u;
    const uint32_t x = idx & 7u;
    const uint8_t sib = (uint8_t)((scale_log2 << 6) | (x << 3) | b);
    if (disp == 0 && b != 5u)
    {
        x86_u8(obj, (uint8_t)((r << 3) | 4u));
        x86_u8(obj, sib);
    }
    else if (disp >= -128 && disp <= 127)
    {
        x86_u8(obj, (uint8_t)(0x40u | (r << 3) | 4u));
        x86_u8(obj, sib);
        x86_u8(obj, (uint8_t)(int8_t)disp);
    }
    else
    {
        x86_u8(obj, (uint8_t)(0x80u | (r << 3) | 4u));
        x86_u8(obj, sib);
        x86_u32(obj, (uint32_t)disp);
    }
}

// Emit [rip + disp32] with relocation
static inline void x86_mem_rip(FastObj* obj, uint32_t reg, uint32_t sym_idx, uint32_t rtype, int64_t addend)
{
    x86_u8(obj, (uint8_t)(((reg & 7u) << 3) | 5u));
    const uint32_t rel_off = obj->text.len;
    x86_u32(obj, 0u);
    fast_obj_add_rela(obj, &obj->rela_text, rel_off, sym_idx, rtype, addend - 4);
}

// Basic register instructions
static inline void x86_push(FastObj* obj, uint32_t reg)
{
    if (reg >= 8u) { x86_u8(obj, 0x41u); }
    x86_u8(obj, (uint8_t)(0x50u + (reg & 7u)));
}

static inline void x86_pop(FastObj* obj, uint32_t reg)
{
    if (reg >= 8u) { x86_u8(obj, 0x41u); }
    x86_u8(obj, (uint8_t)(0x58u + (reg & 7u)));
}

static inline __attribute__((always_inline)) void x86_mov_rr(FastObj* obj, bool w64, uint32_t dst, uint32_t src)
{
    if (dst == src && w64) { return; }
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        uint8_t* p = obj->text.data + obj->text.len;
        const uint8_t rex = (uint8_t)(0x40u | (w64 ? 8u : 0u) | ((src & 8u) >> 1) | ((dst & 8u) >> 3));
        if (rex != 0x40u) { *p++ = rex; }
        *p++ = 0x89u;
        *p++ = (uint8_t)(0xc0u | ((src & 7u) << 3) | (dst & 7u));
        obj->text.len = (uint32_t)(p - obj->text.data);
    }
    else { obj->overflow = true; }
}

static inline __attribute__((always_inline)) void x86_mov_ri(FastObj* obj, bool w64, uint32_t dst, uint64_t imm)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        uint8_t* p = obj->text.data + obj->text.len;
        if (imm == 0)
        {
            const uint8_t rex = (uint8_t)(0x40u | ((dst & 8u) >> 1) | ((dst & 8u) >> 3));
            if (rex != 0x40u) { *p++ = rex; }
            *p++ = 0x31u;
            *p++ = (uint8_t)(0xc0u | ((dst & 7u) << 3) | (dst & 7u));
        }
        else if (imm <= UINT32_MAX)
        {
            if (dst >= 8u) { *p++ = 0x41u; }
            *p++ = (uint8_t)(0xb8u + (dst & 7u));
            const uint32_t v32 = (uint32_t)imm;
            memcpy(p, &v32, 4); p += 4;
        }
        else if (w64 && (int64_t)imm >= INT32_MIN && (int64_t)imm <= INT32_MAX)
        {
            *p++ = (uint8_t)(0x48u | ((dst & 8u) >> 3));
            *p++ = 0xc7u;
            *p++ = (uint8_t)(0xc0u | (dst & 7u));
            const uint32_t v32 = (uint32_t)(int32_t)(int64_t)imm;
            memcpy(p, &v32, 4); p += 4;
        }
        else
        {
            *p++ = (uint8_t)(0x48u | ((dst & 8u) >> 3));
            *p++ = (uint8_t)(0xb8u + (dst & 7u));
            memcpy(p, &imm, 8); p += 8;
        }
        obj->text.len = (uint32_t)(p - obj->text.data);
    }
    else { obj->overflow = true; }
}

// Load 1/2/4/8 bytes from [base + disp] into dst (sign- or zero-extending into 32/64-bit register!)
static inline __attribute__((always_inline)) void x86_load(FastObj* obj, uint32_t dst, uint32_t base, int32_t disp, uint32_t size, bool is_signed)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        uint8_t* p = obj->text.data + obj->text.len;
        const uint8_t rex = (uint8_t)(0x40u | (size == 8u ? 8u : 0u) | ((dst & 8u) >> 1) | ((base & 8u) >> 3));
        if (rex != 0x40u) { *p++ = rex; }
        if (size >= 4u) { *p++ = 0x8bu; }
        else
        {
            *p++ = 0x0fu;
            *p++ = size == 2u ? (is_signed ? 0xbfu : 0xb7u) : (is_signed ? 0xbeu : 0xb6u);
        }
        p = x86_put_mem_disp(p, dst, base, disp);
        obj->text.len = (uint32_t)(p - obj->text.data);
    }
    else { obj->overflow = true; }
}

// Store 1/2/4/8 bytes from src to [base + disp]
static inline __attribute__((always_inline)) void x86_store(FastObj* obj, uint32_t base, int32_t disp, uint32_t src, uint32_t size)
{
    if (__builtin_expect(obj->text.len + 16u <= obj->text.cap, 1))
    {
        uint8_t* p = obj->text.data + obj->text.len;
        if (size == 2u) { *p++ = 0x66u; }
        const uint8_t rex = (uint8_t)(0x40u | (size == 8u ? 8u : 0u) | ((src & 8u) >> 1) | ((base & 8u) >> 3));
        if (rex != 0x40u || (size == 1u && src >= 4u && src <= 7u)) { *p++ = rex; }
        *p++ = size == 1u ? 0x88u : 0x89u;
        p = x86_put_mem_disp(p, src, base, disp);
        obj->text.len = (uint32_t)(p - obj->text.data);
    }
    else { obj->overflow = true; }
}

// Store immediate into [base + disp] (size 1/2/4/8 where imm fits sign-extended imm32)
static inline void x86_store_imm(FastObj* obj, uint32_t base, int32_t disp, int32_t imm, uint32_t size)
{
    if (size == 8u)
    {
        x86_rex(obj, true, 0, 0, base, false);
        x86_u8(obj, 0xc7u);
        x86_mem_disp(obj, 0, base, disp);
        x86_u32(obj, (uint32_t)imm);
    }
    else if (size == 4u)
    {
        x86_rex(obj, false, 0, 0, base, false);
        x86_u8(obj, 0xc7u);
        x86_mem_disp(obj, 0, base, disp);
        x86_u32(obj, (uint32_t)imm);
    }
    else if (size == 2u)
    {
        x86_u8(obj, 0x66u);
        x86_rex(obj, false, 0, 0, base, false);
        x86_u8(obj, 0xc7u);
        x86_mem_disp(obj, 0, base, disp);
        x86_u16(obj, (uint16_t)imm);
    }
    else
    {
        x86_rex(obj, false, 0, 0, base, false);
        x86_u8(obj, 0xc6u);
        x86_mem_disp(obj, 0, base, disp);
        x86_u8(obj, (uint8_t)imm);
    }
}

// LEA dst, [base + disp]
static inline void x86_lea(FastObj* obj, uint32_t dst, uint32_t base, int32_t disp)
{
    x86_rex(obj, true, dst, 0, base, false);
    x86_u8(obj, 0x8du);
    x86_mem_disp(obj, dst, base, disp);
}

// LEA dst, [base + idx * (1<<scale) + disp]
static inline void x86_lea_sib(FastObj* obj, uint32_t dst, uint32_t base, uint32_t idx, uint32_t scale_log2, int32_t disp)
{
    x86_rex(obj, true, dst, idx, base, false);
    x86_u8(obj, 0x8du);
    x86_mem_sib(obj, dst, base, idx, scale_log2, disp);
}

// LEA dst, [rip + sym + addend] (or MOV dst, [rip + sym@REX_GOTPCRELX] for external/PIC symbols!)
static inline void x86_lea_sym(FastObj* obj, uint32_t dst, uint32_t sym_idx, int64_t addend)
{
    const FastObjSym* s = obj->syms + sym_idx;
    const bool use_got = (obj->pic || s->section == 0) && s->binding != STB_LOCAL && s->visibility == STV_DEFAULT && s->type != STT_SECTION;
    if (use_got)
    {
        // mov dst, [rip + sym@GOTPCREL] (relaxable to lea by linker in non-PIE!)
        x86_rex(obj, true, dst, 0, 0, false);
        x86_u8(obj, 0x8bu);
        x86_mem_rip(obj, dst, sym_idx, R_X86_64_REX_GOTPCRELX, 0);
        if (addend != 0)
        {
            if (addend >= INT32_MIN && addend <= INT32_MAX)
            {
                x86_lea(obj, dst, dst, (int32_t)addend);
            }
            else
            {
                // rare >2GB addend
                x86_rex(obj, true, 0, 0, dst, false);
                x86_u8(obj, 0x81u);
                x86_u8(obj, (uint8_t)(0xc0u | (dst & 7u)));
                x86_u32(obj, (uint32_t)addend);
            }
        }
    }
    else
    {
        // lea dst, [rip + sym + addend]
        x86_rex(obj, true, dst, 0, 0, false);
        x86_u8(obj, 0x8du);
        x86_mem_rip(obj, dst, sym_idx, R_X86_64_PC32, addend);
    }
}

// Integer ALU reg, reg: op_base = 0x01(ADD), 0x09(OR), 0x21(AND), 0x29(SUB), 0x31(XOR), 0x39(CMP), 0x85(TEST)
static inline void x86_alu_rr(FastObj* obj, bool w64, uint8_t op, uint32_t dst, uint32_t src)
{
    x86_rex(obj, w64, src, 0, dst, false);
    x86_u8(obj, op);
    x86_u8(obj, (uint8_t)(0xc0u | ((src & 7u) << 3) | (dst & 7u)));
}

// Integer ALU reg, imm32 (ext: 0=ADD, 1=OR, 4=AND, 5=SUB, 6=XOR, 7=CMP)
static inline void x86_alu_ri(FastObj* obj, bool w64, uint8_t ext, uint32_t dst, int32_t imm)
{
    x86_rex(obj, w64, 0, 0, dst, false);
    if (imm >= -128 && imm <= 127)
    {
        x86_u8(obj, 0x83u);
        x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (dst & 7u)));
        x86_u8(obj, (uint8_t)(int8_t)imm);
    }
    else
    {
        x86_u8(obj, 0x81u);
        x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (dst & 7u)));
        x86_u32(obj, (uint32_t)imm);
    }
}

// IMUL dst, src
static inline void x86_imul_rr(FastObj* obj, bool w64, uint32_t dst, uint32_t src)
{
    x86_rex(obj, w64, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xafu);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

// IMUL dst, src, imm32
static inline void x86_imul_rri(FastObj* obj, bool w64, uint32_t dst, uint32_t src, int32_t imm)
{
    x86_rex(obj, w64, dst, 0, src, false);
    if (imm >= -128 && imm <= 127)
    {
        x86_u8(obj, 0x6bu);
        x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
        x86_u8(obj, (uint8_t)(int8_t)imm);
    }
    else
    {
        x86_u8(obj, 0x69u);
        x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
        x86_u32(obj, (uint32_t)imm);
    }
}

// Unary F7: ext=2(NOT), 3(NEG), 6(DIV), 7(IDIV)
static inline void x86_f7(FastObj* obj, bool w64, uint8_t ext, uint32_t reg)
{
    x86_rex(obj, w64, 0, 0, reg, false);
    x86_u8(obj, 0xf7u);
    x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (reg & 7u)));
}

// Shift reg by imm8 or CL (ext: 4=SHL, 5=SHR, 7=SAR)
static inline void x86_shift_ri(FastObj* obj, bool w64, uint8_t ext, uint32_t reg, uint8_t count)
{
    x86_rex(obj, w64, 0, 0, reg, false);
    if (count == 1u)
    {
        x86_u8(obj, 0xd1u);
        x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (reg & 7u)));
    }
    else
    {
        x86_u8(obj, 0xc1u);
        x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (reg & 7u)));
        x86_u8(obj, count);
    }
}

static inline void x86_shift_cl(FastObj* obj, bool w64, uint8_t ext, uint32_t reg)
{
    x86_rex(obj, w64, 0, 0, reg, false);
    x86_u8(obj, 0xd3u);
    x86_u8(obj, (uint8_t)(0xc0u | (ext << 3) | (reg & 7u)));
}

// Double-precision 64-bit shifts for 128-bit integers: shld (0xA4) / shrd (0xAC) dst, src, imm8
static inline void x86_shld_ri(FastObj* obj, uint32_t dst, uint32_t src, uint8_t count)
{
    x86_rex(obj, true, src, 0, dst, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xa4u);
    x86_u8(obj, (uint8_t)(0xc0u | ((src & 7u) << 3) | (dst & 7u)));
    x86_u8(obj, count);
}

static inline void x86_shrd_ri(FastObj* obj, uint32_t dst, uint32_t src, uint8_t count)
{
    x86_rex(obj, true, src, 0, dst, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xacu);
    x86_u8(obj, (uint8_t)(0xc0u | ((src & 7u) << 3) | (dst & 7u)));
    x86_u8(obj, count);
}

// Sign / zero extend register in-place (dst = ext(src))
static inline void x86_extend(FastObj* obj, uint32_t dst, uint32_t src, uint32_t from_size, bool is_signed)
{
    if (from_size == 1u)
    {
        x86_rex(obj, false, dst, 0, src, src >= 4u && src <= 7u);
        x86_u8(obj, 0x0fu);
        x86_u8(obj, is_signed ? 0xbeu : 0xb6u);
        x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
    }
    else if (from_size == 2u)
    {
        x86_rex(obj, false, dst, 0, src, false);
        x86_u8(obj, 0x0fu);
        x86_u8(obj, is_signed ? 0xbfu : 0xb7u);
        x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
    }
    else if (from_size == 4u)
    {
        if (is_signed)
        {
            // movsxd dst64, src32
            x86_rex(obj, true, dst, 0, src, false);
            x86_u8(obj, 0x63u);
            x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
        }
        else
        {
            // mov dst32, src32 (zero-extends to 64!)
            x86_rex(obj, false, src, 0, dst, false);
            x86_u8(obj, 0x89u);
            x86_u8(obj, (uint8_t)(0xc0u | ((src & 7u) << 3) | (dst & 7u)));
        }
    }
}

// setcc r8 + movzx r32, r8
static inline void x86_setcc_zext(FastObj* obj, uint32_t cc, uint32_t dst)
{
    x86_rex(obj, false, 0, 0, dst, dst >= 4u && dst <= 7u);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, (uint8_t)(0x90u + (cc & 0xfu)));
    x86_u8(obj, (uint8_t)(0xc0u | (dst & 7u)));
    x86_extend(obj, dst, dst, 1u, false);
}

// SSE2 scalar float/double instructions (is_double: true -> F2 prefix, false -> F3 prefix)
static inline void x86_sse_rr(FastObj* obj, bool is_double, uint8_t op, uint32_t dst, uint32_t src)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, false, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, op); // 0x10=mov, 0x58=add, 0x5c=sub, 0x59=mul, 0x5e=div
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

static inline void x86_sse_load(FastObj* obj, bool is_double, uint32_t xmm, uint32_t base, int32_t disp)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, false, xmm, 0, base, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x10u);
    x86_mem_disp(obj, xmm, base, disp);
}

static inline void x86_sse_store(FastObj* obj, bool is_double, uint32_t base, int32_t disp, uint32_t xmm)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, false, xmm, 0, base, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x11u);
    x86_mem_disp(obj, xmm, base, disp);
}

static inline void x86_ucomis(FastObj* obj, bool is_double, uint32_t lhs, uint32_t rhs)
{
    if (is_double) { x86_u8(obj, 0x66u); }
    x86_rex(obj, false, lhs, 0, rhs, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x2eu);
    x86_u8(obj, (uint8_t)(0xc0u | ((lhs & 7u) << 3) | (rhs & 7u)));
}

// cvtsi2sd/ss xmm, gpr (w64=true for 64-bit int, false for 32-bit int)
static inline void x86_cvtsi2fp(FastObj* obj, bool is_double, bool w64, uint32_t xmm, uint32_t gpr)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, w64, xmm, 0, gpr, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x2au);
    x86_u8(obj, (uint8_t)(0xc0u | ((xmm & 7u) << 3) | (gpr & 7u)));
}

// cvttsd/ss2si gpr, xmm (w64=true for 64-bit int, false for 32-bit int)
static inline void x86_cvttfp2si(FastObj* obj, bool is_double, bool w64, uint32_t gpr, uint32_t xmm)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, w64, gpr, 0, xmm, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x2cu);
    x86_u8(obj, (uint8_t)(0xc0u | ((gpr & 7u) << 3) | (xmm & 7u)));
}

// movq/movd xmm, gpr (to_xmm=true -> 66 0F 6E; to_xmm=false -> 66 0F 7E)
static inline void x86_mov_xmm_gpr(FastObj* obj, bool w64, bool to_xmm, uint32_t xmm, uint32_t gpr)
{
    x86_u8(obj, 0x66u);
    x86_rex(obj, w64, xmm, 0, gpr, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, to_xmm ? 0x6eu : 0x7eu);
    x86_u8(obj, (uint8_t)(0xc0u | ((xmm & 7u) << 3) | (gpr & 7u)));
}

// Call direct symbol (PLT32) or indirect register
static inline void x86_call_sym(FastObj* obj, uint32_t sym_idx)
{
    x86_u8(obj, 0xe8u);
    const uint32_t rel_off = obj->text.len;
    x86_u32(obj, 0u);
    fast_obj_add_rela(obj, &obj->rela_text, rel_off, sym_idx, R_X86_64_PLT32, -4);
}

static inline void x86_call_reg(FastObj* obj, uint32_t reg)
{
    x86_rex(obj, false, 0, 0, reg, false);
    x86_u8(obj, 0xffu);
    x86_u8(obj, (uint8_t)(0xd0u | (reg & 7u)));
}

static inline void x86_jmp_reg(FastObj* obj, uint32_t reg)
{
    x86_rex(obj, false, 0, 0, reg, false);
    x86_u8(obj, 0xffu);
    x86_u8(obj, (uint8_t)(0xe0u | (reg & 7u)));
}

static inline void x86_ret(FastObj* obj) { x86_u8(obj, 0xc3u); }
static inline void x86_leave(FastObj* obj) { x86_u8(obj, 0xc9u); }
static inline void x86_cdq(FastObj* obj) { x86_u8(obj, 0x99u); }
static inline void x86_cqo(FastObj* obj) { x86_u8(obj, 0x48u); x86_u8(obj, 0x99u); }
static inline void x86_rep_movsb(FastObj* obj) { x86_u8(obj, 0xf3u); x86_u8(obj, 0xa4u); }
static inline void x86_rep_stosb(FastObj* obj) { x86_u8(obj, 0xf3u); x86_u8(obj, 0xaau); }
static inline void x86_ud2(FastObj* obj) { x86_u8(obj, 0x0fu); x86_u8(obj, 0x0bu); }

// movsxd dst64, dword [base + idx*4] (for dense switch jump tables!)
static inline void x86_movsxd_sib4(FastObj* obj, uint32_t dst, uint32_t base, uint32_t idx)
{
    x86_rex(obj, true, dst, idx, base, false);
    x86_u8(obj, 0x63u);
    x86_mem_sib(obj, dst, base, idx, 2u, 0);
}

// SSE conversions float <-> double, zeroing, RIP-relative load & sign-flip XOR
static inline void x86_cvtss2sd(FastObj* obj, uint32_t dst, uint32_t src)
{
    x86_u8(obj, 0xf3u);
    x86_rex(obj, false, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x5au);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

static inline void x86_cvtsd2ss(FastObj* obj, uint32_t dst, uint32_t src)
{
    x86_u8(obj, 0xf2u);
    x86_rex(obj, false, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x5au);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

static inline void x86_sse_zero(FastObj* obj, uint32_t xmm)
{
    x86_rex(obj, false, xmm, 0, xmm, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x57u); // xorps xmm, xmm (zero-idiom!)
    x86_u8(obj, (uint8_t)(0xc0u | ((xmm & 7u) << 3) | (xmm & 7u)));
}

static inline void x86_sse_load_rip(FastObj* obj, bool is_double, uint32_t xmm, uint32_t sym_idx, int64_t addend)
{
    x86_u8(obj, is_double ? 0xf2u : 0xf3u);
    x86_rex(obj, false, xmm, 0, 0, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x10u);
    x86_mem_rip(obj, xmm, sym_idx, R_X86_64_PC32, addend);
}

static inline void x86_sse_xor_rip(FastObj* obj, bool is_double, uint32_t xmm, uint32_t sym_idx, int64_t addend)
{
    if (is_double) { x86_u8(obj, 0x66u); }
    x86_rex(obj, false, xmm, 0, 0, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0x57u); // xorpd / xorps xmm, [rip + disp]
    x86_mem_rip(obj, xmm, sym_idx, R_X86_64_PC32, addend);
}

// Bit-scan & byte-swap instructions for __builtin_clz/ctz/bswap/popcount
static inline void x86_bsr(FastObj* obj, bool w64, uint32_t dst, uint32_t src)
{
    x86_rex(obj, w64, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xbdu);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

static inline void x86_bsf(FastObj* obj, bool w64, uint32_t dst, uint32_t src)
{
    x86_rex(obj, w64, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xbcu);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

static inline void x86_bswap(FastObj* obj, bool w64, uint32_t reg)
{
    x86_rex(obj, w64, 0, 0, reg, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, (uint8_t)(0xc8u + (reg & 7u)));
}

static inline void x86_popcnt(FastObj* obj, bool w64, uint32_t dst, uint32_t src)
{
    x86_u8(obj, 0xf3u);
    x86_rex(obj, w64, dst, 0, src, false);
    x86_u8(obj, 0x0fu);
    x86_u8(obj, 0xb8u);
    x86_u8(obj, (uint8_t)(0xc0u | ((dst & 7u) << 3) | (src & 7u)));
}

// test r8, r8 (e.g. test al, al in variadic prologue)
static inline void x86_test_r8(FastObj* obj, uint32_t reg)
{
    x86_rex(obj, false, reg, 0, reg, reg >= 4u && reg <= 7u);
    x86_u8(obj, 0x84u);
    x86_u8(obj, (uint8_t)(0xc0u | ((reg & 7u) << 3) | (reg & 7u)));
}

// Labels and jumps (per-function)
static inline uint32_t fast_label_new(FastObj* obj)
{
    uint32_t id = 0;
    if (__builtin_expect(obj->label_count < obj->label_cap, 1))
    {
        id = obj->label_count++;
        obj->labels[id] = UINT32_MAX;
    }
    else
    {
        obj->overflow = true;
    }
    return id;
}

static inline void fast_label_bind(FastObj* obj, uint32_t id)
{
    if (id < obj->label_count)
    {
        obj->labels[id] = obj->text.len;
    }
}

static inline void x86_jmp_label(FastObj* obj, uint32_t label_id)
{
    x86_u8(obj, 0xe9u);
    const uint32_t patch = obj->text.len;
    x86_u32(obj, 0u);
    if (__builtin_expect(obj->fixup_count < obj->fixup_cap, 1))
    {
        obj->fixups[obj->fixup_count++] = (FastJumpFixup){ .patch_off = patch, .label_id = label_id };
    }
    else
    {
        obj->overflow = true;
    }
}

static inline void x86_jcc_label(FastObj* obj, uint32_t cc, uint32_t label_id)
{
    x86_u8(obj, 0x0fu);
    x86_u8(obj, (uint8_t)(0x80u + (cc & 0xfu)));
    const uint32_t patch = obj->text.len;
    x86_u32(obj, 0u);
    if (__builtin_expect(obj->fixup_count < obj->fixup_cap, 1))
    {
        obj->fixups[obj->fixup_count++] = (FastJumpFixup){ .patch_off = patch, .label_id = label_id };
    }
    else
    {
        obj->overflow = true;
    }
}

static void fast_obj_patch_func_jumps(FastObj* obj)
{
    uint8_t* const text = obj->text.data;
    const uint32_t* const labels = obj->labels;
    const FastJumpFixup* const fixups = obj->fixups;
    const uint32_t count = obj->fixup_count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t target = labels[fixups[i].label_id];
        if (__builtin_expect(target == UINT32_MAX, 0))
        {
            obj->overflow = true;
            break;
        }
        const int32_t rel = (int32_t)(target - (fixups[i].patch_off + 4u));
        memcpy(text + fixups[i].patch_off, &rel, 4);
    }
    obj->label_count = 0;
    obj->fixup_count = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Build .eh_frame (standard SysV AMD64 CIE + 24-byte FDE per function) and write complete ELF64 .o via writev()

static void fast_obj_build_eh_frame(FastObj* obj)
{
    if (!obj->unwind_tables || obj->fde_count == 0)
    {
        return;
    }
    // Standard 24-byte CIE for x86-64:
    //   length=20 (4B), cie_id=0 (4B), version=1 (1B), aug="zR\0" (3B),
    //   code_align=1 (1B), data_align=-8 (0x78, 1B), ret_reg=16 (1B),
    //   aug_len=1 (1B), fde_enc=0x1b (DW_EH_PE_pcrel|DW_EH_PE_sdata4, 1B),
    //   DW_CFA_def_cfa(rsp=7, 8): 0x0c 0x07 0x08 (3B),
    //   DW_CFA_offset(rip=16, -8 -> 1): 0x90 0x01 (2B),
    //   nop padding (2B): 0x00 0x00
    static const uint8_t cie[24] = {
        0x14, 0x00, 0x00, 0x00, // length = 20
        0x00, 0x00, 0x00, 0x00, // CIE_id = 0
        0x01,                   // version = 1
        'z', 'R', 0x00,         // augmentation = "zR"
        0x01,                   // code_alignment_factor = 1
        0x78,                   // data_alignment_factor = -8 (SLEB128)
        0x10,                   // return_address_register = 16 (rip)
        0x01, 0x1b,             // aug_data: len=1, encoding=pcrel|sdata4
        0x0c, 0x07, 0x08,       // DW_CFA_def_cfa: r7 (rsp) ofs 8
        0x90, 0x01,             // DW_CFA_offset: r16 (rip) at cfa-8
        0x00, 0x00,             // DW_CFA_nop x2
    };
    const uint64_t total_eh = 24u + (uint64_t)obj->fde_count * 32u;
    if (total_eh > obj->eh_frame.cap)
    {
        obj->overflow = true;
        return;
    }
    memcpy(obj->eh_frame.data, cie, 24u);
    uint32_t eh_off = 24u;
    const uint32_t text_sym = obj->sec_sym_text;

    for (uint32_t i = 0; i < obj->fde_count; i += 1)
    {
        // 32-byte FDE for `push rbp (1B); mov rbp, rsp (3B)`:
        //   length=28 (4B), cie_ptr=eh_off+4 (4B), pc_begin=0 (4B + R_X86_64_PC32), pc_range=size (4B),
        //   aug_len=0 (1B), DW_CFA_advance_loc(1)=0x41, DW_CFA_def_cfa_offset(16)=0x0e 0x10,
        //   DW_CFA_offset(rbp=6, 2)=0x86 0x02, DW_CFA_advance_loc(3)=0x43, DW_CFA_def_cfa_register(rbp=6)=0x0d 0x06,
        //   7B nop padding to 8-byte alignment.
        uint8_t* f = obj->eh_frame.data + eh_off;
        const uint32_t fde_len = 28u;
        const uint32_t cie_ptr = eh_off + 4u;
        const uint32_t pc_range = obj->fdes[i].text_size;
        memcpy(f + 0, &fde_len, 4);
        memcpy(f + 4, &cie_ptr, 4);
        memset(f + 8, 0, 4);
        memcpy(f + 12, &pc_range, 4);
        f[16] = 0x00u; // aug len = 0
        f[17] = 0x41u; // advance_loc 1 (after push rbp)
        f[18] = 0x0eu; f[19] = 0x10u; // def_cfa_offset 16
        f[20] = 0x86u; f[21] = 0x02u; // offset r6 (rbp) at cfa-16
        f[22] = 0x43u; // advance_loc 3 (after mov rbp, rsp)
        f[23] = 0x0du; f[24] = 0x06u; // def_cfa_register r6 (rbp)
        memset(f + 25, 0, 7u);
        fast_obj_add_rela(obj, &obj->rela_eh_frame, eh_off + 8u, text_sym, R_X86_64_PC32, (int64_t)obj->fdes[i].text_off);
        eh_off += 32u;
    }
    obj->eh_frame.len = eh_off;
}

static void fast_obj_remap_relas(FastRelaBuf* rb, const FastObjSym* syms)
{
    Elf64_Rela* const relas = rb->data;
    const uint32_t n = rb->count;
    for (uint32_t i = 0; i < n; i += 1)
    {
        const uint32_t old_sym = (uint32_t)ELF64_R_SYM(relas[i].r_info);
        const uint32_t rtype = (uint32_t)ELF64_R_TYPE(relas[i].r_info);
        relas[i].r_info = ELF64_R_INFO((uint64_t)syms[old_sym].final_idx, (uint64_t)rtype);
    }
}

static bool fast_obj_write_elf(FastObj* obj, const char* path)
{
    if (obj->overflow)
    {
        return false;
    }
    fast_obj_build_eh_frame(obj);
    if (obj->overflow)
    {
        return false;
    }

    // Build final ELF64 .symtab with all STB_LOCAL symbols first, then STB_GLOBAL / STB_WEAK!
    Elf64_Sym* elf_syms = (Elf64_Sym*)fast_arena_alloc(obj->arena, (uint64_t)obj->sym_count * sizeof(Elf64_Sym), 8);
    if (elf_syms == NULL)
    {
        return false;
    }
    elf_syms[0] = (Elf64_Sym){ 0 };
    obj->syms[0].final_idx = 0;
    uint32_t out_idx = 1u;

    // Pass 1: locals
    for (uint32_t i = 1u; i < obj->sym_count; i += 1)
    {
        if (obj->syms[i].binding == STB_LOCAL)
        {
            obj->syms[i].final_idx = out_idx;
            elf_syms[out_idx++] = (Elf64_Sym){
                .st_name = obj->syms[i].name_off,
                .st_info = ELF64_ST_INFO(obj->syms[i].binding, obj->syms[i].type),
                .st_other = obj->syms[i].visibility,
                .st_shndx = (uint16_t)obj->syms[i].section,
                .st_value = obj->syms[i].value,
                .st_size = obj->syms[i].size,
            };
        }
    }
    const uint32_t first_global = out_idx;

    // Pass 2: globals & weaks
    for (uint32_t i = 1u; i < obj->sym_count; i += 1)
    {
        if (obj->syms[i].binding != STB_LOCAL)
        {
            obj->syms[i].final_idx = out_idx;
            elf_syms[out_idx++] = (Elf64_Sym){
                .st_name = obj->syms[i].name_off,
                .st_info = ELF64_ST_INFO(obj->syms[i].binding, obj->syms[i].type),
                .st_other = obj->syms[i].visibility,
                .st_shndx = (uint16_t)obj->syms[i].section,
                .st_value = obj->syms[i].value,
                .st_size = obj->syms[i].size,
            };
        }
    }

    // Remap all relocation symbol indices to final ELF .symtab indices!
    fast_obj_remap_relas(&obj->rela_text, obj->syms);
    fast_obj_remap_relas(&obj->rela_rodata, obj->syms);
    fast_obj_remap_relas(&obj->rela_data, obj->syms);
    fast_obj_remap_relas(&obj->rela_data_rel_ro, obj->syms);
    fast_obj_remap_relas(&obj->rela_eh_frame, obj->syms);

    // Fixed section header string table
    static const char shstrtab[] =
        "\0"                        // 0
        ".text\0"                   // 1
        ".rela.text\0"              // 7
        ".rodata\0"                 // 18
        ".rela.rodata\0"            // 26
        ".rodata.str1.1\0"          // 39
        ".data\0"                   // 54
        ".rela.data\0"              // 60
        ".data.rel.ro\0"            // 71
        ".rela.data.rel.ro\0"       // 84
        ".bss\0"                    // 102
        ".eh_frame\0"               // 107
        ".rela.eh_frame\0"          // 117
        ".note.GNU-stack\0"         // 132
        ".comment\0"                // 148
        ".symtab\0"                 // 157
        ".strtab\0"                 // 165
        ".shstrtab\0";              // 173
    static const char comment_str[] = "buster direct";
    static const uint8_t zero_pad[16] = { 0 };

    // Layout all sections into file offsets and an iovec array for a single writev()!
    Elf64_Shdr shdrs[FSEC_COUNT];
    memset(shdrs, 0, sizeof(shdrs));

    struct iovec iov[48];
    uint32_t iov_n = 0;

    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = FSEC_COUNT;
    ehdr.e_shstrndx = FSEC_SHSTRTAB;

    iov[iov_n++] = (struct iovec){ .iov_base = &ehdr, .iov_len = sizeof(ehdr) };
    uint64_t foff = sizeof(ehdr);

    // Helper macro to align and append section payload to iov
    #define BUSTER_ELF_ADD_SEC(id, nm_off, typ, flg, lnk, inf, aln, ent, ptr, sz) do { \
        const uint64_t a_ = (aln) > 1u ? (aln) : 1u; \
        const uint64_t pad_ = ((foff + a_ - 1u) & ~(a_ - 1u)) - foff; \
        if (pad_ > 0 && (typ) != SHT_NOBITS) { \
            iov[iov_n++] = (struct iovec){ .iov_base = (void*)(uintptr_t)zero_pad, .iov_len = pad_ }; \
            foff += pad_; \
        } \
        shdrs[(id)] = (Elf64_Shdr){ \
            .sh_name = (nm_off), .sh_type = (typ), .sh_flags = (flg), .sh_addr = 0, \
            .sh_offset = foff, .sh_size = (sz), .sh_link = (lnk), .sh_info = (inf), \
            .sh_addralign = (aln), .sh_entsize = (ent), \
        }; \
        if ((sz) > 0 && (typ) != SHT_NOBITS) { \
            iov[iov_n++] = (struct iovec){ .iov_base = (void*)(uintptr_t)(ptr), .iov_len = (sz) }; \
            foff += (sz); \
        } \
    } while (0)

    BUSTER_ELF_ADD_SEC(FSEC_TEXT, 1u, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, 0, obj->text_align, 0,
                       obj->text.data, obj->text.len);
    BUSTER_ELF_ADD_SEC(FSEC_RELA_TEXT, 7u, SHT_RELA, SHF_INFO_LINK, FSEC_SYMTAB, FSEC_TEXT, 8u, sizeof(Elf64_Rela),
                       obj->rela_text.data, (uint64_t)obj->rela_text.count * sizeof(Elf64_Rela));
    BUSTER_ELF_ADD_SEC(FSEC_RODATA, 18u, SHT_PROGBITS, SHF_ALLOC, 0, 0, obj->rodata_align, 0,
                       obj->rodata.data, obj->rodata.len);
    BUSTER_ELF_ADD_SEC(FSEC_RELA_RODATA, 26u, SHT_RELA, SHF_INFO_LINK, FSEC_SYMTAB, FSEC_RODATA, 8u, sizeof(Elf64_Rela),
                       obj->rela_rodata.data, (uint64_t)obj->rela_rodata.count * sizeof(Elf64_Rela));
    BUSTER_ELF_ADD_SEC(FSEC_RODATA_STR, 39u, SHT_PROGBITS, SHF_ALLOC, 0, 0, 1u, 0u,
                       obj->rodata_str.data, obj->rodata_str.len);
    BUSTER_ELF_ADD_SEC(FSEC_DATA, 54u, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, 0, 0, obj->data_align, 0,
                       obj->data.data, obj->data.len);
    BUSTER_ELF_ADD_SEC(FSEC_RELA_DATA, 60u, SHT_RELA, SHF_INFO_LINK, FSEC_SYMTAB, FSEC_DATA, 8u, sizeof(Elf64_Rela),
                       obj->rela_data.data, (uint64_t)obj->rela_data.count * sizeof(Elf64_Rela));
    BUSTER_ELF_ADD_SEC(FSEC_DATA_REL_RO, 71u, SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, 0, 0, obj->data_rel_ro_align, 0,
                       obj->data_rel_ro.data, obj->data_rel_ro.len);
    BUSTER_ELF_ADD_SEC(FSEC_RELA_DATA_REL_RO, 84u, SHT_RELA, SHF_INFO_LINK, FSEC_SYMTAB, FSEC_DATA_REL_RO, 8u, sizeof(Elf64_Rela),
                       obj->rela_data_rel_ro.data, (uint64_t)obj->rela_data_rel_ro.count * sizeof(Elf64_Rela));
    BUSTER_ELF_ADD_SEC(FSEC_BSS, 102u, SHT_NOBITS, SHF_WRITE | SHF_ALLOC, 0, 0, obj->bss_align, 0,
                       NULL, obj->bss_size);
    BUSTER_ELF_ADD_SEC(FSEC_EH_FRAME, 107u, SHT_X86_64_UNWIND, SHF_ALLOC, 0, 0, 8u, 0,
                       obj->eh_frame.data, obj->eh_frame.len);
    BUSTER_ELF_ADD_SEC(FSEC_RELA_EH_FRAME, 117u, SHT_RELA, SHF_INFO_LINK, FSEC_SYMTAB, FSEC_EH_FRAME, 8u, sizeof(Elf64_Rela),
                       obj->rela_eh_frame.data, (uint64_t)obj->rela_eh_frame.count * sizeof(Elf64_Rela));
    BUSTER_ELF_ADD_SEC(FSEC_NOTE_STACK, 132u, SHT_PROGBITS, 0, 0, 0, 1u, 0, NULL, 0);
    BUSTER_ELF_ADD_SEC(FSEC_COMMENT, 148u, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 0, 0, 1u, 1u,
                       comment_str, sizeof(comment_str));
    BUSTER_ELF_ADD_SEC(FSEC_SYMTAB, 157u, SHT_SYMTAB, 0, FSEC_STRTAB, first_global, 8u, sizeof(Elf64_Sym),
                       elf_syms, (uint64_t)out_idx * sizeof(Elf64_Sym));
    BUSTER_ELF_ADD_SEC(FSEC_STRTAB, 165u, SHT_STRTAB, 0, 0, 0, 1u, 0,
                       obj->strtab.data, obj->strtab.len);
    BUSTER_ELF_ADD_SEC(FSEC_SHSTRTAB, 173u, SHT_STRTAB, 0, 0, 0, 1u, 0,
                       shstrtab, sizeof(shstrtab));

    #undef BUSTER_ELF_ADD_SEC

    // Align section headers to 8 bytes
    const uint64_t sh_pad = ((foff + 7u) & ~UINT64_C(7)) - foff;
    if (sh_pad > 0)
    {
        iov[iov_n++] = (struct iovec){ .iov_base = (void*)(uintptr_t)zero_pad, .iov_len = sh_pad };
        foff += sh_pad;
    }
    ehdr.e_shoff = foff;
    iov[iov_n++] = (struct iovec){ .iov_base = shdrs, .iov_len = sizeof(shdrs) };
    const uint64_t total_bytes = foff + sizeof(shdrs);

    // An object that is there already is written over where it stands and cut to the new length afterwards. Opening it
    // with O_TRUNC gave its pages back to the kernel first and took as many again for the same bytes: a sixth of the
    // compile's time in the kernel for sqlite3.c's 2 MB (#2850's profile on the 9700X).
    const int fd = open(path, O_WRONLY | O_CREAT, 0644);
    bool ok = false;
    if (fd >= 0)
    {
        // Write all iovecs (loop in case of partial writev)
        uint64_t written = 0;
        uint32_t cur = 0;
        ok = true;
        while (cur < iov_n)
        {
            const ssize_t n = writev(fd, iov + cur, (int)(iov_n - cur));
            if (n <= 0)
            {
                ok = false;
                break;
            }
            written += (uint64_t)n;
            uint64_t adv = (uint64_t)n;
            while (cur < iov_n && adv >= iov[cur].iov_len)
            {
                adv -= iov[cur].iov_len;
                cur += 1u;
            }
            if (cur < iov_n && adv > 0)
            {
                iov[cur].iov_base = (uint8_t*)iov[cur].iov_base + adv;
                iov[cur].iov_len -= adv;
            }
        }
        struct stat status;
        if (fstat(fd, &status) == 0 && S_ISREG(status.st_mode) && (uint64_t)status.st_size != written)
        {
            // What is left of a longer object, or of this one where a write failed.
            if (ftruncate(fd, (off_t)written) != 0) { ok = false; }
        }
        if (close(fd) != 0) { ok = false; }
        ok = ok && written == total_bytes;
    }
    return ok;
}

#endif
