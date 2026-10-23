// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The link representation: the compiler's encoded code and data as the object writer and the linker take them
// (docs/object-writer-and-linker.md, "Representation"). Four tables of column arrays, each indexed by a 32-bit id of its
// own, in which id 0 means none and its record is zero:
//   - Names (NameId): a spelling's bytes in the name arena, followed by a NUL for the ELF string tables, its length, and
//     its 64-bit hash (link_hash), computed once when the name is added. The table doesn't deduplicate: the front end
//     interns, and gives each spelling one id.
//   - Atoms (AtomId): what the linker places, and keeps or drops, as a whole (a function, a global object, a pool of
//     string literals, a piece of a synthetic section): its class (BUSTER_LINK_CLASS_*, the output section it goes
//     to), alignment, size, bytes (null for the zero-fill classes), unit (the translation unit or input it comes from,
//     which is the object the object writer puts it in), the range of its fixups, and after layout its address.
//   - Symbols (SymbolId): what binding is about: a name (or none, for a string literal or another local the code
//     refers to only by id), the atom that defines it and the offset there (value), size, flags (ELF's type, binding
//     and visibility: BUSTER_LINK_SYMBOL_*), source (where it is defined: BUSTER_LINK_SOURCE_*), and four columns the
//     linker fills in: its GOT slot, its .dynsym index, its version, and `reference`, the only symbol column the
//     patch kernel reads.
//   - Fixups (FixupId): the references the encoder couldn't fill in, 13 bytes each in four columns: the field's offset
//     in its atom, the target symbol, the addend relative to the field, and the kind (BUSTER_LINK_FIXUP_*), which says
//     how the instruction or the data reaches the target, and so both the object's relocation and the executable's
//     arithmetic. An addend outside int32 goes to a side table: the kind gets BUSTER_LINK_FIXUP_WIDE and the addend
//     column holds the addend's index there. link_fixup_addend reads either.
//
// Who writes what: the front end and the code generator (and the test programs, link_program.c) add the records, and
// define the symbols they added before their atoms existed; after that, only the linker writes, and only the columns
// that are its own: atoms' address, and symbols' got_slot, dynamic_index, version and reference. The object writer
// reads. The atoms' bytes are the code generator's until the executable's patching, which writes the fields in place.
//
// Memory: link_initialize pushes every column from one arena (link_capacity_bytes says how much), with room for the
// counts it is given: count, place, fill, so the counts come first, from the front end or the test program's options.
// A small program's tables then share a few base pages. Names' bytes go to a name arena of their own. An add past a
// table's capacity, or a name the name arena can't hold, fails and returns 0: nothing is truncated, and link->error
// keeps the first failure.
//
// What the adds enforce, so that the object writer and the linker can rely on it:
//   - Every id a record holds exists, and every record a table holds was added whole.
//   - An atom of a zero-fill class has no bytes, and any other atom with a size has them. Alignments are powers of two
//     up to 2^BUSTER_LINK_MAX_ALIGNMENT_LOG2.
//   - A symbol defined in an atom (with link_add_symbol or later with link_define_symbol) lies within it, and it is a
//     TLS symbol exactly when the atom's class is TDATA or TBSS.
//   - An absolute symbol (source ABSOLUTE) has no atom, and is neither a TLS variable nor an IFUNC.
//   - An atom's fixups are one range of consecutive ids, sorted by offset, whose fields don't overlap and lie within
//     the atom, which has bytes. The ranges of different atoms don't interleave: an atom's fixups are added together.
//     The ranges are in the order the atoms' first fixups were added, which is atom order when atoms are built one
//     after another.
//   - The priorities are an init or fini array atom's each, by atom in id order.
// link_check verifies what only the whole representation shows, once it is built: every program symbol is defined, a
// TLS kind targets a TLS symbol and no other kind does, a local symbol is referred to only from its own unit, and a
// shared library's symbol only by the kinds that can reach one from an executable: through the GOT (CALL_GOT, LOAD_GOT,
// GOTPCREL and GOTTPOFF), by ABS64, which the executable relocates when it loads, and for a function by CALL, which
// the executable sends through a stub. It checks the fixup rules above again too, for columns that lanes filled
// themselves.
//
// Code: link_add_code_fixups turns x86_encode's output into an atom's fixups (docs/object-writer-and-linker.md, "Fixups
// from the encoder"). The code generator marks each instruction with a symbolic field (BUSTER_X86_FORM_FIXUP) and
// gives a LinkReference per field: the target and the addend as the source has them, and the kind. x86_encode reports
// each marked instruction's end, x86_fixup_distance says how far before the end the field starts, and a PC-relative
// kind's addend loses that distance, since the processor measures from the instruction's end: -4 for call f and lea
// rax, [rip + x], -5 for cmp byte ptr [rip + x], 1. The fields are zeroed in the atom's bytes, as GNU as writes them,
// so that an object can be written from the bytes as they are and an executable patches over them.
//
// Lanes: the tables are plain columns, so a phase split across lanes counts its records per lane, has one lane reserve
// each table's range (count += the total), and has each lane fill its own part of the columns (docs/object-writer-and-
// linker.md, "Lanes", rule 3). The adds below are the one-lane path, which the first implementation and the test
// programs use.
//
// main.c includes this file after x86_64_encoder.c and lane.c, whose X86Instruction, x86_fixup_distance and Arena it
// uses. Layout, in this order:
//   - API: the ids, the classes (BUSTER_LINK_CLASS_*), the symbol flags (BUSTER_LINK_SYMBOL_*) and sources
//     (BUSTER_LINK_SOURCE_*), the fixup kinds (BUSTER_LINK_FIXUP_*), the instruction fields they patch (LinkField) and
//     their properties (LinkFixupKind and link_fixup_kinds), LinkError, the tables (LinkNames, LinkAtoms, LinkSymbols,
//     LinkFixups and LinkPriorities), Link, LinkCapacity, LinkReference and the entry points' prototypes.
//   - Names: link_hash, link_hash_rows, link_hash_names, link_add_name and link_add_name_hashed.
//   - Tables: link_capacity_bytes, link_initialize, link_add_atom, link_add_symbol, link_define_symbol, link_add_fixup,
//     link_fixup_addend and link_add_priority.
//   - Code: link_instruction_field, link_kind_fits and link_add_code_fixups.
//   - Checks: link_check_fixups, link_check_target and link_check.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef uint32_t NameId;
typedef uint32_t AtomId;
typedef uint32_t SymbolId;
typedef uint32_t FixupId;

// Atom classes: the output section an atom goes to, numbered in the order of lld's segments, so that sorting atoms by
// class gives the layout's order of sections: R (.rodata, the string pools and .eh_frame), RX (.text, .init and
// .fini), RW and RELRO (.tdata, .tbss, .init_array, .fini_array, .data.rel.ro), RW (.data, .bss). TBSS and BSS are
// zero-fill: their atoms have no bytes. The atoms of the classes whose order is what the program runs keep the order of
// their ids: EH_FRAME, INIT, FINI, INIT_ARRAY and FINI_ARRAY.
#define BUSTER_LINK_CLASS_NONE 0
#define BUSTER_LINK_CLASS_RODATA 1
// Pools of string literals: .rodata.str1.1 in an object (SHF_MERGE | SHF_STRINGS), .rodata in an executable.
#define BUSTER_LINK_CLASS_STRINGS 2
// Unwind tables: CIEs and FDEs, as an object's .eh_frame holds them, the C runtime's (crtendS.o's terminator last).
#define BUSTER_LINK_CLASS_EH_FRAME 3
#define BUSTER_LINK_CLASS_TEXT 4
// The C runtime's .init and .fini: fragments of one function each, from crti.o's prologue to crtn.o's epilogue, which
// run into each other.
#define BUSTER_LINK_CLASS_INIT 5
#define BUSTER_LINK_CLASS_FINI 6
#define BUSTER_LINK_CLASS_TDATA 7
#define BUSTER_LINK_CLASS_TBSS 8
#define BUSTER_LINK_CLASS_INIT_ARRAY 9
#define BUSTER_LINK_CLASS_FINI_ARRAY 10
#define BUSTER_LINK_CLASS_DATA_RELRO 11
#define BUSTER_LINK_CLASS_DATA 12
#define BUSTER_LINK_CLASS_BSS 13
#define BUSTER_LINK_CLASS_COUNT 14
// An atom's alignment is at most 2^this: 4 GB.
#define BUSTER_LINK_MAX_ALIGNMENT_LOG2 32

// Symbol flags, laid out so that an Elf64_Sym's st_info is flags & BUSTER_LINK_SYMBOL_INFO and its st_other is
// flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT: the type (STT_*) in bits 0 to 3, the binding (STB_*) in bits 4 and 5
// and the visibility (STV_*) in bits 6 and 7. The object writer makes the section and file symbols itself.
#define BUSTER_LINK_SYMBOL_TYPE 0x0f
#define BUSTER_LINK_SYMBOL_NOTYPE 0x00
#define BUSTER_LINK_SYMBOL_OBJECT 0x01
#define BUSTER_LINK_SYMBOL_FUNCTION 0x02
#define BUSTER_LINK_SYMBOL_TLS 0x06
// STT_GNU_IFUNC: a function whose address its resolver, the code at the symbol, returns when the program starts (as
// libc.a's memcpy and strlen are).
#define BUSTER_LINK_SYMBOL_IFUNC 0x0a
#define BUSTER_LINK_SYMBOL_BINDING 0x30
#define BUSTER_LINK_SYMBOL_LOCAL 0x00
#define BUSTER_LINK_SYMBOL_GLOBAL 0x10
#define BUSTER_LINK_SYMBOL_WEAK 0x20
#define BUSTER_LINK_SYMBOL_VISIBILITY 0xc0
#define BUSTER_LINK_SYMBOL_DEFAULT 0x00
#define BUSTER_LINK_SYMBOL_INTERNAL 0x40
#define BUSTER_LINK_SYMBOL_HIDDEN 0x80
#define BUSTER_LINK_SYMBOL_PROTECTED 0xc0
#define BUSTER_LINK_SYMBOL_INFO 0x3f
#define BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT 6

// Where a symbol is defined. A symbol of the program, of the C runtime's objects and archive members (RUNTIME_ARCHIVE:
// the crt objects, libc_nonshared.a's or libc.a's members) or of a foreign object is defined in an atom; one of a shared
// library or of the linker (__init_array_start, _GLOBAL_OFFSET_TABLE_) has none, and neither has one defined nowhere
// (NONE: undefined, or a weak reference that binds to 0) or an absolute one (ABSOLUTE: an object's SHN_ABS symbol, such
// as libc.a's _nl_current_LC_CTYPE_used), which is its value wherever the executable loads.
#define BUSTER_LINK_SOURCE_NONE 0
#define BUSTER_LINK_SOURCE_PROGRAM 1
#define BUSTER_LINK_SOURCE_RUNTIME_SHARED 2
#define BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE 3
#define BUSTER_LINK_SOURCE_FOREIGN_OBJECT 4
#define BUSTER_LINK_SOURCE_FOREIGN_SHARED 5
#define BUSTER_LINK_SOURCE_LINKER 6
#define BUSTER_LINK_SOURCE_ABSOLUTE 7
#define BUSTER_LINK_SOURCE_COUNT 8

// Fixup kinds: how the instruction or the data reaches the target S, with addend A, from the field at P. G is the
// address of S's GOT slot. Each kind names the relocation an object gets and the value an executable gets (the table in
// docs/object-writer-and-linker.md, "Representation"):
//   - CALL: a branch's rel32 (call f, jmp f, jcc f): R_X86_64_PLT32; S + A - P.
//   - CALL_GOT: call or jmp [rip + slot] (ff 15, ff 25): R_X86_64_GOTPCRELX; G + A - P.
//   - LOAD_GOT: mov r64, [rip + slot], which has REX.W: R_X86_64_REX_GOTPCRELX; G + A - P, or S + A - P with the mov
//     rewritten to lea when S turns out to be local.
//   - GOTPCREL: another instruction that reads the slot (push [rip + slot]): R_X86_64_GOTPCREL; G + A - P. Linkers
//     never rewrite it.
//   - PC32: any other RIP-relative operand or rel32, or 4 bytes of data: R_X86_64_PC32; S + A - P, checked to fit in
//     int32.
//   - TPOFF32: a 32-bit displacement or immediate with S's offset from the end of the TLS block (local-exec, mov rax,
//     qword ptr fs:[x@tpoff]): R_X86_64_TPOFF32; that offset + A.
//   - GOTTPOFF: mov or add r64, [rip + slot] reading S's offset from the slot (initial-exec): R_X86_64_GOTTPOFF;
//     G + A - P, with a TPOFF64 for the slot.
//   - ABS64: 8 bytes of data, or a 64-bit immediate: R_X86_64_64; S + A, and in a PIE a relative relocation (a symbolic
//     one for an import).
//   - PC64: 8 bytes of data (unwind tables): R_X86_64_PC64; S + A - P.
//   - ABS32, ABS32S: a 32-bit immediate or displacement, or 4 bytes of data, non-PIE only: R_X86_64_32, R_X86_64_32S;
//     S + A, checked to fit when zero- or sign-extended.
//   - SIZE32, SIZE64: 4 or 8 bytes: R_X86_64_SIZE32, R_X86_64_SIZE64; S's size + A.
//   - TLSGD: the displacement of a general-dynamic access's lea rdi, [rip + x@tlsgd], in the 16 bytes of data16 lea
//     rdi, [rip + x@tlsgd]; data16 data16 rex.W call __tls_get_addr (or data16 rex.W call [rip + __tls_get_addr's
//     slot], a CALL_GOT or, as GNU as 2.42 writes it, a GOTPCREL), whose call is the next fixup: R_X86_64_TLSGD;
//     G + A - P for the pair of slots of x's module and offset.
//     An executable relaxes the 16 bytes to mov rax, fs:0; lea rax, [rax + x's offset from the end of the TLS block],
//     the call's fixup with them.
//   - TLSLD: the displacement of a local-dynamic access's lea rdi, [rip + x@tlsld], in the 12 bytes of lea rdi,
//     [rip + x@tlsld]; call __tls_get_addr (or the 13 with call [rip + slot]), whose call is the next fixup:
//     R_X86_64_TLSLD; G + A - P for the pair of slots of x's module. An executable relaxes them to data16 data16
//     data16 mov rax, fs:0 (or with four prefixes), the call's fixup with them.
//   - DTPOFF32: 32 bits of S's offset in its module's TLS block, after a TLSLD (lea rax, [rax + x@dtpoff]):
//     R_X86_64_DTPOFF32; that offset + A, which in an executable is TPOFF32's, from the end of the block.
// Only imported objects have the last three (libgcc.a's decimal floating point and split-stack members, and GCC 16's
// libgcc_eh.a's heap trampolines): the code generator makes local-exec and initial-exec accesses.
#define BUSTER_LINK_FIXUP_NONE 0
#define BUSTER_LINK_FIXUP_CALL 1
#define BUSTER_LINK_FIXUP_CALL_GOT 2
#define BUSTER_LINK_FIXUP_LOAD_GOT 3
#define BUSTER_LINK_FIXUP_GOTPCREL 4
#define BUSTER_LINK_FIXUP_PC32 5
#define BUSTER_LINK_FIXUP_TPOFF32 6
#define BUSTER_LINK_FIXUP_GOTTPOFF 7
#define BUSTER_LINK_FIXUP_ABS64 8
#define BUSTER_LINK_FIXUP_PC64 9
#define BUSTER_LINK_FIXUP_ABS32 10
#define BUSTER_LINK_FIXUP_ABS32S 11
#define BUSTER_LINK_FIXUP_SIZE32 12
#define BUSTER_LINK_FIXUP_SIZE64 13
#define BUSTER_LINK_FIXUP_TLSGD 14
#define BUSTER_LINK_FIXUP_TLSLD 15
#define BUSTER_LINK_FIXUP_DTPOFF32 16
#define BUSTER_LINK_FIXUP_KINDS 17
// A fixup's kind is its BUSTER_LINK_FIXUP_KIND bits, and BUSTER_LINK_FIXUP_WIDE says its addend is in the side table.
#define BUSTER_LINK_FIXUP_KIND 0x7f
#define BUSTER_LINK_FIXUP_WIDE 0x80

// The fields of an instruction a fixup can patch, as link_instruction_field classifies them: a branch's rel32, a
// RIP-relative displacement, any other 32-bit displacement or a 32-bit immediate, and a 64-bit immediate or address.
// Fields of 8 and 16 bits, and displacements that aren't 32-bit, are none of these.
typedef enum LinkField
{
    LINK_FIELD_NONE,
    LINK_FIELD_BRANCH32,
    LINK_FIELD_RIP32,
    LINK_FIELD_ABSOLUTE32,
    LINK_FIELD_ABSOLUTE64,
} LinkField;

// What a kind is: its name (for diagnostics), its field's width, whether its value is measured from the field (so that
// an instruction's addend loses the distance to its end), and the instruction fields it can patch (a bit per LinkField;
// none for PC64, which is only data). The table's rows are the kinds.
typedef struct LinkFixupKind LinkFixupKind;
struct LinkFixupKind
{
    const char* name;
    uint8_t width;
    bool pc_relative;
    uint8_t fields;
};

#define BUSTER_LINK_FIELD_BIT(field) (1u << (field))
#define BUSTER_LINK_FIELDS_BRANCH (BUSTER_LINK_FIELD_BIT(LINK_FIELD_BRANCH32))
#define BUSTER_LINK_FIELDS_RIP (BUSTER_LINK_FIELD_BIT(LINK_FIELD_RIP32))
#define BUSTER_LINK_FIELDS_ABSOLUTE32 (BUSTER_LINK_FIELD_BIT(LINK_FIELD_ABSOLUTE32))
#define BUSTER_LINK_FIELDS_ABSOLUTE64 (BUSTER_LINK_FIELD_BIT(LINK_FIELD_ABSOLUTE64))

static const LinkFixupKind link_fixup_kinds[BUSTER_LINK_FIXUP_KINDS] = {
    [BUSTER_LINK_FIXUP_NONE] = {
        .name = "none",
        .width = 0,
        .pc_relative = false,
        .fields = 0,
    },
    [BUSTER_LINK_FIXUP_CALL] = {
        .name = "call",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_BRANCH,
    },
    [BUSTER_LINK_FIXUP_CALL_GOT] = {
        .name = "call_got",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_LOAD_GOT] = {
        .name = "load_got",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_GOTPCREL] = {
        .name = "gotpcrel",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_PC32] = {
        .name = "pc32",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_BRANCH | BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_TPOFF32] = {
        .name = "tpoff32",
        .width = 4,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE32,
    },
    [BUSTER_LINK_FIXUP_GOTTPOFF] = {
        .name = "gottpoff",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_ABS64] = {
        .name = "abs64",
        .width = 8,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE64,
    },
    [BUSTER_LINK_FIXUP_PC64] = {
        .name = "pc64",
        .width = 8,
        .pc_relative = true,
        .fields = 0,
    },
    [BUSTER_LINK_FIXUP_ABS32] = {
        .name = "abs32",
        .width = 4,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE32,
    },
    [BUSTER_LINK_FIXUP_ABS32S] = {
        .name = "abs32s",
        .width = 4,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE32,
    },
    [BUSTER_LINK_FIXUP_SIZE32] = {
        .name = "size32",
        .width = 4,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE32,
    },
    [BUSTER_LINK_FIXUP_SIZE64] = {
        .name = "size64",
        .width = 8,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE64,
    },
    [BUSTER_LINK_FIXUP_TLSGD] = {
        .name = "tlsgd",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_TLSLD] = {
        .name = "tlsld",
        .width = 4,
        .pc_relative = true,
        .fields = BUSTER_LINK_FIELDS_RIP,
    },
    [BUSTER_LINK_FIXUP_DTPOFF32] = {
        .name = "dtpoff32",
        .width = 4,
        .pc_relative = false,
        .fields = BUSTER_LINK_FIELDS_ABSOLUTE32,
    },
};

typedef enum LinkError
{
    LINK_ERROR_NONE,
    // A table is at its capacity, the name arena can't hold a name, or the arena can't hold the tables.
    LINK_ERROR_FULL,
    // A record names an id that doesn't exist, or 0 where it needs one.
    LINK_ERROR_ID,
    // A record breaks one of the rules above: a class, alignment, size or bytes an atom can't have, flags or a source
    // that don't exist, a symbol outside its atom, defined twice or absolute in an atom, a fixup out of order,
    // overlapping another or outside its atom, a kind that doesn't exist, or a priority out of order, too high or for an
    // atom that isn't an init or fini array's.
    LINK_ERROR_RECORD,
    // A code fixup's field isn't one its kind can patch, or the references don't match the marked instructions.
    LINK_ERROR_FIELD,
    // link_check: a program symbol without an atom, a TLS kind whose target isn't a TLS symbol or another kind whose
    // target is one, a local symbol referred to from another unit, or a shared library's symbol referred to by a kind
    // that can't reach it.
    LINK_ERROR_BINDING,
} LinkError;

// The tables. Each column has room for `capacity` records, record 0 among them, of which `count` are in use.
typedef struct LinkNames LinkNames;
struct LinkNames
{
    const uint8_t** bytes;
    uint32_t* length;
    uint64_t* hash;
    uint32_t count;
    uint32_t capacity;
};

typedef struct LinkAtoms LinkAtoms;
struct LinkAtoms
{
    uint8_t* class;
    uint8_t* alignment_log2;
    uint32_t* unit;
    uint64_t* size;
    // Mutable, so that an executable's chunks can be patched where they are.
    uint8_t** bytes;
    // The atom's fixups: fixup_count of them from fixup_begin, or none (both 0).
    FixupId* fixup_begin;
    uint32_t* fixup_count;
    // After layout: an image offset for a loaded atom, a file offset otherwise.
    uint64_t* address;
    uint32_t count;
    uint32_t capacity;
};

typedef struct LinkSymbols LinkSymbols;
struct LinkSymbols
{
    NameId* name;
    AtomId* atom;
    uint64_t* value;
    uint64_t* size;
    uint8_t* flags;
    uint8_t* source;
    // The linker's: 0 until it assigns them.
    uint32_t* got_slot;
    uint32_t* dynamic_index;
    uint16_t* version;
    // After layout: what code that refers to the symbol uses (its address, its GOT slot's for an import, its offset in
    // the TLS block for a TLS variable).
    uint32_t* reference;
    uint32_t count;
    uint32_t capacity;
};

typedef struct LinkFixups LinkFixups;
struct LinkFixups
{
    uint32_t* offset;
    SymbolId* target;
    // Relative to the field, or with BUSTER_LINK_FIXUP_WIDE, the index of the addend in wide_addends.
    int32_t* addend;
    uint8_t* kind;
    uint32_t count;
    uint32_t capacity;
    // The side table, with room for wide_capacity addends, entry 0 among them, of which wide_count are in use.
    int64_t* wide_addends;
    uint32_t wide_count;
    uint32_t wide_capacity;
};

// The init and fini array atoms that have a priority (an object's .init_array.N and .fini_array.N, N from 0 to
// 65535), by atom in id order, entry 0 among them unused: the executable places them before those without one, in the
// order of their priorities and then of their ids, as GNU ld's and lld's SORT_BY_INIT_PRIORITY lay them out.
typedef struct LinkPriorities LinkPriorities;
struct LinkPriorities
{
    AtomId* atom;
    uint16_t* priority;
    uint32_t count;
    uint32_t capacity;
};

typedef struct Link Link;
struct Link
{
    LinkNames names;
    LinkAtoms atoms;
    LinkSymbols symbols;
    LinkFixups fixups;
    LinkPriorities priorities;
    // Where link_add_name copies names' bytes.
    Arena* name_arena;
    // The first failure, or LINK_ERROR_NONE.
    LinkError error;
};

// How many records of each table there is room for, besides record 0.
typedef struct LinkCapacity LinkCapacity;
struct LinkCapacity
{
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint32_t priorities;
};

// A symbolic field of a marked instruction, for link_add_code_fixups.
typedef struct LinkReference LinkReference;
struct LinkReference
{
    // The instruction's index among those given to x86_encode, and which of its fields this is (an X86FixupField).
    uint32_t instruction;
    uint8_t field;
    // BUSTER_LINK_FIXUP_*, without BUSTER_LINK_FIXUP_WIDE.
    uint8_t kind;
    SymbolId target;
    // What the field refers to is target + addend, as the source has it: for a PC-relative kind, before the distance to
    // the instruction's end is taken off.
    int64_t addend;
};

// The 64-bit hash of a name's bytes. It is part of the format from the runtime snapshot on (docs/object-writer-and-
// linker.md, "The runtime snapshot"), so link_hash_names computes the same function eight names at a time.
static __attribute__((unused)) uint64_t link_hash(const uint8_t* bytes, uint64_t length);
// hashes[i] = link_hash(names[i], lengths[i]) for `count` names, eight at a time in vectors.
static __attribute__((unused)) void link_hash_names(const uint8_t* const* names, const uint32_t* lengths, uint64_t count, uint64_t* hashes);
// At most how many bytes link_initialize pushes for `capacity`: what to reserve for the tables' arena.
static __attribute__((unused)) uint64_t link_capacity_bytes(LinkCapacity capacity);
// Pushes the columns of every table from `arena`, with room for `capacity`, and has names' bytes copied to `name_arena`.
// Returns false, with LINK_ERROR_FULL and every table without room, if the arena can't hold them or a capacity is
// UINT32_MAX (ids are 32-bit); the arena is then as it was.
static __attribute__((unused)) bool link_initialize(Link* link, Arena* arena, Arena* name_arena, LinkCapacity capacity);
// A copy of the name, with its hash: its id, or 0 if the table or the name arena is full or the name is empty.
static __attribute__((unused)) NameId link_add_name(Link* link, const uint8_t* bytes, uint32_t length);
// The same with the name's hash given, which the caller computed with link_hash or link_hash_names.
static __attribute__((unused)) NameId link_add_name_hashed(Link* link, const uint8_t* bytes, uint32_t length, uint64_t hash);
// An atom: its id, or 0. Its fixups are added after it, with link_add_fixup or link_add_code_fixups, and its address
// starts at 0.
static __attribute__((unused)) AtomId link_add_atom(Link* link, uint8_t class, uint8_t alignment_log2, uint64_t size, uint8_t* bytes, uint32_t unit);
// A symbol, defined in `atom` at `value` or (atom 0) defined later with link_define_symbol, elsewhere or nowhere: its id,
// or 0. The linker's columns start at 0.
static __attribute__((unused)) SymbolId link_add_symbol(Link* link, NameId name, AtomId atom, uint64_t value, uint64_t size, uint8_t flags, uint8_t source);
// Defines a symbol added without an atom (a function the code refers to before it is encoded). Returns false if it has
// an atom already, or the definition breaks the symbol rules.
static __attribute__((unused)) bool link_define_symbol(Link* link, SymbolId symbol, AtomId atom, uint64_t value, uint64_t size);
// A fixup of `atom`'s field at `offset`, of `kind` (without BUSTER_LINK_FIXUP_WIDE), which the addend gets if it doesn't
// fit in int32: its id, or 0. It must follow the atom's other fixups, in id and in offset.
static __attribute__((unused)) FixupId link_add_fixup(Link* link, AtomId atom, uint32_t offset, uint8_t kind, SymbolId target, int64_t addend);
// A fixup's addend, from the side table if it is wide.
static __attribute__((unused)) int64_t link_fixup_addend(const Link* link, FixupId fixup);
// Gives `atom`, an init or fini array atom after every atom that has one, a priority up to 65535. Returns false if the
// table is full, or the atom or the priority breaks those rules.
static __attribute__((unused)) bool link_add_priority(Link* link, AtomId atom, uint32_t priority);
// Adds the fixups of `atom`, whose bytes are x86_encode's output for `instructions` from the atom's start: one fixup
// per reference. fixup_ends are the ends x86_encode reported for the instructions marked with BUSTER_X86_FORM_FIXUP, of
// which there are `marked`. References are sorted by instruction, and an instruction's displacement comes before its
// immediate; every marked instruction has at least one, and every reference's instruction is marked. Returns false if
// they don't match, or if a field isn't one its kind can patch (link_instruction_field and LinkFixupKind.fields; and
// CALL_GOT only on call and jmp, LOAD_GOT only on mov r64 and GOTTPOFF only on mov and add r64, without a segment
// override, since linkers rewrite those instructions); the fixups added before the failure stay.
static __attribute__((unused)) bool link_add_code_fixups(Link* link, AtomId atom, const X86Instruction* instructions, uint64_t instruction_count, const uint32_t* fixup_ends,
                                                         uint64_t marked, const LinkReference* references, uint64_t reference_count);
// Checks what only the whole representation shows (see the rules above) and returns the first problem, which it also
// records in link->error, or LINK_ERROR_NONE.
static __attribute__((unused)) LinkError link_check(Link* link);

// Records the link's first failure.
static void link_fail(Link* link, LinkError error)
{
    if (link->error == LINK_ERROR_NONE)
    {
        link->error = error;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Names

// The name hash: eight bytes at a time, the last eight or fewer zero-extended (at least one word, so the empty name has
// one), each word mixed in with an XOR, a 64-bit multiplication and a shift that folds the product's high half into its
// low half; then SplitMix64's finalizer (THIRD_PARTY_NOTICES.md has its reference). The length is in the seed, so that
// names that differ only in trailing zero bytes differ. Each step is an AVX-512 instruction on eight 64-bit lanes
// (vpxorq, vpmullq, vpsrlq), which is what lets link_hash_names give each of eight names what link_hash gives it.
#define BUSTER_LINK_HASH_SEED 0x243F6A8885A308D3ull
#define BUSTER_LINK_HASH_MULTIPLIER 0x9E3779B97F4A7C15ull
#define BUSTER_LINK_HASH_FINISH0 0xbf58476d1ce4e5b9ull
#define BUSTER_LINK_HASH_FINISH1 0x94d049bb133111ebull
// link_hash_names' batch, and the longest name it hashes in vectors: a 64-byte load each. A longer one goes to
// link_hash.
#define BUSTER_LINK_HASH_BATCH 8
#define BUSTER_LINK_HASH_VECTOR_BYTES 64

static uint64_t link_hash_finish(uint64_t hash)
{
    uint64_t result = (hash ^ (hash >> 30)) * BUSTER_LINK_HASH_FINISH0;
    result = (result ^ (result >> 27)) * BUSTER_LINK_HASH_FINISH1;
    return result ^ (result >> 31);
}

// Every load is within the name.
static uint64_t link_hash(const uint8_t* bytes, uint64_t length)
{
    uint64_t hash = BUSTER_LINK_HASH_SEED ^ length;
    const uint64_t words = length == 0 ? 1 : (length + 7) / 8;
    for (uint64_t i = 0; i < words; i += 1)
    {
        const uint64_t offset = i * 8;
        const uint64_t take = length - offset < 8 ? length - offset : 8;
        uint64_t word = 0;
        if (take != 0)
        {
            memcpy(&word, bytes + offset, take);
        }
        hash = (hash ^ word) * BUSTER_LINK_HASH_MULTIPLIER;
        hash ^= hash >> 32;
    }
    return link_hash_finish(hash);
}

static __m512i link_hash_finish_vector(__m512i hash)
{
    __m512i result = _mm512_mullo_epi64(_mm512_xor_si512(hash, _mm512_srli_epi64(hash, 30)), _mm512_set1_epi64((long long)BUSTER_LINK_HASH_FINISH0));
    result = _mm512_mullo_epi64(_mm512_xor_si512(result, _mm512_srli_epi64(result, 27)), _mm512_set1_epi64((long long)BUSTER_LINK_HASH_FINISH1));
    return _mm512_xor_si512(result, _mm512_srli_epi64(result, 31));
}

// Transposes eight rows of eight 64-bit words, so that words[j] holds word j of rows 0 to 7: unpacks within 128-bit
// chunks, then two-input permutes of chunk pairs, then 256-bit halves (24 shuffles).
static void link_transpose_words(const __m512i* rows, __m512i* words)
{
    const __m512i t0 = _mm512_unpacklo_epi64(rows[0], rows[1]);
    const __m512i t1 = _mm512_unpackhi_epi64(rows[0], rows[1]);
    const __m512i t2 = _mm512_unpacklo_epi64(rows[2], rows[3]);
    const __m512i t3 = _mm512_unpackhi_epi64(rows[2], rows[3]);
    const __m512i t4 = _mm512_unpacklo_epi64(rows[4], rows[5]);
    const __m512i t5 = _mm512_unpackhi_epi64(rows[4], rows[5]);
    const __m512i t6 = _mm512_unpacklo_epi64(rows[6], rows[7]);
    const __m512i t7 = _mm512_unpackhi_epi64(rows[6], rows[7]);
    // Words 0, 1, 8, 9, 4, 5, 12, 13 and 2, 3, 10, 11, 6, 7, 14, 15 of the pair (_mm512_set_epi64 lists the last first).
    const __m512i even = _mm512_set_epi64(13, 12, 5, 4, 9, 8, 1, 0);
    const __m512i odd = _mm512_set_epi64(15, 14, 7, 6, 11, 10, 3, 2);
    const __m512i s0 = _mm512_permutex2var_epi64(t0, even, t2);
    const __m512i s1 = _mm512_permutex2var_epi64(t0, odd, t2);
    const __m512i s2 = _mm512_permutex2var_epi64(t1, even, t3);
    const __m512i s3 = _mm512_permutex2var_epi64(t1, odd, t3);
    const __m512i s4 = _mm512_permutex2var_epi64(t4, even, t6);
    const __m512i s5 = _mm512_permutex2var_epi64(t4, odd, t6);
    const __m512i s6 = _mm512_permutex2var_epi64(t5, even, t7);
    const __m512i s7 = _mm512_permutex2var_epi64(t5, odd, t7);
    words[0] = _mm512_shuffle_i64x2(s0, s4, 0x44);
    words[1] = _mm512_shuffle_i64x2(s2, s6, 0x44);
    words[2] = _mm512_shuffle_i64x2(s1, s5, 0x44);
    words[3] = _mm512_shuffle_i64x2(s3, s7, 0x44);
    words[4] = _mm512_shuffle_i64x2(s0, s4, 0xee);
    words[5] = _mm512_shuffle_i64x2(s2, s6, 0xee);
    words[6] = _mm512_shuffle_i64x2(s1, s5, 0xee);
    words[7] = _mm512_shuffle_i64x2(s3, s7, 0xee);
}

// The hashes of eight names of up to 64 bytes, rows[lane] holding a name's bytes and zeros past its length: the rows
// transposed so that a vector holds word j of the eight names, then the words mixed in lane by lane while a lane's name
// has them. Each lane's hash is link_hash's for its name; a longer name's is not, and is the caller's to replace.
static __m512i link_hash_rows(const __m512i* rows, __m512i lengths)
{
    const __m512i multiplier = _mm512_set1_epi64((long long)BUSTER_LINK_HASH_MULTIPLIER);
    __m512i words[BUSTER_LINK_HASH_BATCH];
    link_transpose_words(rows, words);
    const __m512i word_counts = _mm512_max_epu64(_mm512_srli_epi64(_mm512_add_epi64(lengths, _mm512_set1_epi64(7)), 3), _mm512_set1_epi64(1));
    const uint64_t most = _mm512_reduce_max_epu64(word_counts);
    const uint64_t steps = most < BUSTER_LINK_HASH_BATCH ? most : BUSTER_LINK_HASH_BATCH;
    __m512i hash = _mm512_xor_si512(_mm512_set1_epi64((long long)BUSTER_LINK_HASH_SEED), lengths);
    for (uint64_t j = 0; j < steps; j += 1)
    {
        const __mmask8 active = _mm512_cmpgt_epu64_mask(word_counts, _mm512_set1_epi64((long long)j));
        __m512i mixed = _mm512_mullo_epi64(_mm512_xor_si512(hash, words[j]), multiplier);
        mixed = _mm512_xor_si512(mixed, _mm512_srli_epi64(mixed, 32));
        hash = _mm512_mask_mov_epi64(hash, active, mixed);
    }
    return link_hash_finish_vector(hash);
}

// Each batch of eight: each name's first 64 bytes in a masked load (none past its length is read, so a name may end at
// a page's end), hashed by link_hash_rows; the last batch's missing lanes are empty names, whose hashes aren't stored.
// A name past 64 bytes goes to link_hash.
static void link_hash_names(const uint8_t* const* names, const uint32_t* lengths, uint64_t count, uint64_t* hashes)
{
    for (uint64_t i = 0; i < count; i += BUSTER_LINK_HASH_BATCH)
    {
        const uint32_t batch = count - i < BUSTER_LINK_HASH_BATCH ? (uint32_t)(count - i) : BUSTER_LINK_HASH_BATCH;
        __m512i rows[BUSTER_LINK_HASH_BATCH];
        uint32_t long_names = 0;
        for (uint32_t lane = 0; lane < BUSTER_LINK_HASH_BATCH; lane += 1)
        {
            const uint32_t length = lane < batch ? lengths[i + lane] : 0;
            const uint64_t loaded = length < BUSTER_LINK_HASH_VECTOR_BYTES ? length : BUSTER_LINK_HASH_VECTOR_BYTES;
            rows[lane] = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, (uint32_t)loaded), lane < batch ? names[i + lane] : 0);
            long_names |= (length > BUSTER_LINK_HASH_VECTOR_BYTES ? 1u : 0u) << lane;
        }
        const __m512i length_vector = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32((__mmask8)_bzhi_u32(0xff, batch), lengths + i));
        _mm512_mask_storeu_epi64(hashes + i, (__mmask8)_bzhi_u32(0xff, batch), link_hash_rows(rows, length_vector));
        for (uint32_t lane = 0; long_names != 0 && lane < batch; lane += 1)
        {
            if ((long_names >> lane & 1) != 0)
            {
                hashes[i + lane] = link_hash(names[i + lane], lengths[i + lane]);
            }
        }
    }
}

static NameId link_add_name(Link* link, const uint8_t* bytes, uint32_t length)
{
    return link_add_name_hashed(link, bytes, length, link_hash(bytes, length));
}

static NameId link_add_name_hashed(Link* link, const uint8_t* bytes, uint32_t length, uint64_t hash)
{
    LinkNames* names = &link->names;
    NameId result = 0;
    if (names->count == names->capacity)
    {
        link_fail(link, LINK_ERROR_FULL);
    }
    else if (length == 0)
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        uint8_t* copy = arena_push(link->name_arena, (uint64_t)length + 1, 1);
        if (copy == 0)
        {
            link_fail(link, LINK_ERROR_FULL);
        }
        else
        {
            memcpy(copy, bytes, length);
            copy[length] = 0;
            result = names->count;
            names->bytes[result] = copy;
            names->length[result] = length;
            names->hash[result] = hash;
            names->count += 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tables

// Each column starts on a cache line of its own.
#define BUSTER_LINK_COLUMN_ALIGNMENT 64

// Each table's bytes per record, over its columns, and its number of columns.
#define BUSTER_LINK_NAME_BYTES (sizeof(const uint8_t*) + sizeof(uint32_t) + sizeof(uint64_t))
#define BUSTER_LINK_NAME_COLUMNS 3
#define BUSTER_LINK_ATOM_BYTES (2 * sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint8_t*) + sizeof(FixupId) + sizeof(uint32_t) + sizeof(uint64_t))
#define BUSTER_LINK_ATOM_COLUMNS 8
#define BUSTER_LINK_SYMBOL_BYTES (sizeof(NameId) + sizeof(AtomId) + 2 * sizeof(uint64_t) + 2 * sizeof(uint8_t) + 2 * sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t))
#define BUSTER_LINK_SYMBOL_COLUMNS 10
#define BUSTER_LINK_FIXUP_BYTES (sizeof(uint32_t) + sizeof(SymbolId) + sizeof(int32_t) + sizeof(uint8_t))
#define BUSTER_LINK_FIXUP_COLUMNS 4
_Static_assert(BUSTER_LINK_FIXUP_BYTES == 13, "a fixup is 13 bytes");

static uint64_t link_capacity_bytes(LinkCapacity capacity)
{
    const uint64_t padding = BUSTER_LINK_COLUMN_ALIGNMENT - 1;
    uint64_t result = ((uint64_t)capacity.names + 1) * BUSTER_LINK_NAME_BYTES + BUSTER_LINK_NAME_COLUMNS * padding;
    result += ((uint64_t)capacity.atoms + 1) * BUSTER_LINK_ATOM_BYTES + BUSTER_LINK_ATOM_COLUMNS * padding;
    result += ((uint64_t)capacity.symbols + 1) * BUSTER_LINK_SYMBOL_BYTES + BUSTER_LINK_SYMBOL_COLUMNS * padding;
    result += ((uint64_t)capacity.fixups + 1) * BUSTER_LINK_FIXUP_BYTES + BUSTER_LINK_FIXUP_COLUMNS * padding;
    result += ((uint64_t)capacity.wide_addends + 1) * sizeof(int64_t) + padding;
    result += ((uint64_t)capacity.priorities + 1) * (sizeof(AtomId) + sizeof(uint16_t)) + 2 * padding;
    return result;
}

// A column of `records` elements of `element_bytes`, record 0 cleared, while every column before it has fit: 0, and
// *fits false, once one hasn't.
static void* link_push_column(Arena* arena, uint32_t records, uint64_t element_bytes, bool* fits)
{
    void* result = 0;
    if (*fits)
    {
        result = arena_push_array(arena, records, element_bytes, BUSTER_LINK_COLUMN_ALIGNMENT);
        *fits = result != 0;
    }
    if (result != 0)
    {
        memset(result, 0, element_bytes);
    }
    return result;
}

static bool link_initialize(Link* link, Arena* arena, Arena* name_arena, LinkCapacity capacity)
{
    const uint64_t length = arena->length;
    Link result = {
        .name_arena = name_arena,
        .error = LINK_ERROR_NONE,
    };
    bool fits = capacity.names < UINT32_MAX && capacity.atoms < UINT32_MAX && capacity.symbols < UINT32_MAX && capacity.fixups < UINT32_MAX &&
                capacity.wide_addends < UINT32_MAX && capacity.priorities < UINT32_MAX;
    if (fits)
    {
        const uint32_t names = capacity.names + 1;
        const uint32_t atoms = capacity.atoms + 1;
        const uint32_t symbols = capacity.symbols + 1;
        const uint32_t fixups = capacity.fixups + 1;
        const uint32_t wide_addends = capacity.wide_addends + 1;
        const uint32_t priorities = capacity.priorities + 1;
        result.names.bytes = link_push_column(arena, names, sizeof(*result.names.bytes), &fits);
        result.names.length = link_push_column(arena, names, sizeof(*result.names.length), &fits);
        result.names.hash = link_push_column(arena, names, sizeof(*result.names.hash), &fits);
        result.atoms.class = link_push_column(arena, atoms, sizeof(*result.atoms.class), &fits);
        result.atoms.alignment_log2 = link_push_column(arena, atoms, sizeof(*result.atoms.alignment_log2), &fits);
        result.atoms.unit = link_push_column(arena, atoms, sizeof(*result.atoms.unit), &fits);
        result.atoms.size = link_push_column(arena, atoms, sizeof(*result.atoms.size), &fits);
        result.atoms.bytes = link_push_column(arena, atoms, sizeof(*result.atoms.bytes), &fits);
        result.atoms.fixup_begin = link_push_column(arena, atoms, sizeof(*result.atoms.fixup_begin), &fits);
        result.atoms.fixup_count = link_push_column(arena, atoms, sizeof(*result.atoms.fixup_count), &fits);
        result.atoms.address = link_push_column(arena, atoms, sizeof(*result.atoms.address), &fits);
        result.symbols.name = link_push_column(arena, symbols, sizeof(*result.symbols.name), &fits);
        result.symbols.atom = link_push_column(arena, symbols, sizeof(*result.symbols.atom), &fits);
        result.symbols.value = link_push_column(arena, symbols, sizeof(*result.symbols.value), &fits);
        result.symbols.size = link_push_column(arena, symbols, sizeof(*result.symbols.size), &fits);
        result.symbols.flags = link_push_column(arena, symbols, sizeof(*result.symbols.flags), &fits);
        result.symbols.source = link_push_column(arena, symbols, sizeof(*result.symbols.source), &fits);
        result.symbols.got_slot = link_push_column(arena, symbols, sizeof(*result.symbols.got_slot), &fits);
        result.symbols.dynamic_index = link_push_column(arena, symbols, sizeof(*result.symbols.dynamic_index), &fits);
        result.symbols.version = link_push_column(arena, symbols, sizeof(*result.symbols.version), &fits);
        result.symbols.reference = link_push_column(arena, symbols, sizeof(*result.symbols.reference), &fits);
        result.fixups.offset = link_push_column(arena, fixups, sizeof(*result.fixups.offset), &fits);
        result.fixups.target = link_push_column(arena, fixups, sizeof(*result.fixups.target), &fits);
        result.fixups.addend = link_push_column(arena, fixups, sizeof(*result.fixups.addend), &fits);
        result.fixups.kind = link_push_column(arena, fixups, sizeof(*result.fixups.kind), &fits);
        result.fixups.wide_addends = link_push_column(arena, wide_addends, sizeof(*result.fixups.wide_addends), &fits);
        result.priorities.atom = link_push_column(arena, priorities, sizeof(*result.priorities.atom), &fits);
        result.priorities.priority = link_push_column(arena, priorities, sizeof(*result.priorities.priority), &fits);
        if (fits)
        {
            result.names.count = 1;
            result.names.capacity = names;
            result.atoms.count = 1;
            result.atoms.capacity = atoms;
            result.symbols.count = 1;
            result.symbols.capacity = symbols;
            result.fixups.count = 1;
            result.fixups.capacity = fixups;
            result.fixups.wide_count = 1;
            result.fixups.wide_capacity = wide_addends;
            result.priorities.count = 1;
            result.priorities.capacity = priorities;
        }
    }
    if (!fits)
    {
        arena_pop_to(arena, length);
        result = (Link){
            .name_arena = name_arena,
            .error = LINK_ERROR_FULL,
        };
    }
    *link = result;
    return fits;
}

// Whether atoms of the class have no bytes.
static bool link_class_is_zero_fill(uint8_t class)
{
    return class == BUSTER_LINK_CLASS_TBSS || class == BUSTER_LINK_CLASS_BSS;
}

static bool link_class_is_tls(uint8_t class)
{
    return class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_TBSS;
}

static AtomId link_add_atom(Link* link, uint8_t class, uint8_t alignment_log2, uint64_t size, uint8_t* bytes, uint32_t unit)
{
    LinkAtoms* atoms = &link->atoms;
    const bool bytes_valid = link_class_is_zero_fill(class) ? bytes == 0 : bytes != 0 || size == 0;
    AtomId result = 0;
    if (atoms->count == atoms->capacity)
    {
        link_fail(link, LINK_ERROR_FULL);
    }
    else if (class == BUSTER_LINK_CLASS_NONE || class >= BUSTER_LINK_CLASS_COUNT || alignment_log2 > BUSTER_LINK_MAX_ALIGNMENT_LOG2 || !bytes_valid)
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        result = atoms->count;
        atoms->class[result] = class;
        atoms->alignment_log2[result] = alignment_log2;
        atoms->unit[result] = unit;
        atoms->size[result] = size;
        atoms->bytes[result] = bytes;
        atoms->fixup_begin[result] = 0;
        atoms->fixup_count[result] = 0;
        atoms->address[result] = 0;
        atoms->count += 1;
    }
    return result;
}

// Flags whose type is one the representation has and whose binding exists.
static bool link_symbol_flags_are_valid(uint8_t flags)
{
    const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
    const bool type_valid = type == BUSTER_LINK_SYMBOL_NOTYPE || type == BUSTER_LINK_SYMBOL_OBJECT || type == BUSTER_LINK_SYMBOL_FUNCTION || type == BUSTER_LINK_SYMBOL_TLS ||
                            type == BUSTER_LINK_SYMBOL_IFUNC;
    return type_valid && (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_BINDING;
}

// Whether a symbol with `flags` can be defined in `atom`, which exists, at `value` with `size`: within the atom, and a
// TLS symbol exactly when the atom is a TLS one.
static bool link_symbol_fits(const Link* link, AtomId atom, uint64_t value, uint64_t size, uint8_t flags)
{
    const uint64_t atom_size = link->atoms.size[atom];
    const bool tls = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
    return value <= atom_size && size <= atom_size - value && tls == link_class_is_tls(link->atoms.class[atom]);
}

// Whether a symbol with `flags` can be absolute: in no atom, and neither a TLS variable nor an IFUNC, which need one.
static bool link_absolute_fits(AtomId atom, uint8_t flags)
{
    const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
    return atom == 0 && type != BUSTER_LINK_SYMBOL_TLS && type != BUSTER_LINK_SYMBOL_IFUNC;
}

static SymbolId link_add_symbol(Link* link, NameId name, AtomId atom, uint64_t value, uint64_t size, uint8_t flags, uint8_t source)
{
    LinkSymbols* symbols = &link->symbols;
    SymbolId result = 0;
    if (symbols->count == symbols->capacity)
    {
        link_fail(link, LINK_ERROR_FULL);
    }
    else if (name >= link->names.count || atom >= link->atoms.count)
    {
        link_fail(link, LINK_ERROR_ID);
    }
    else if (!link_symbol_flags_are_valid(flags) || source >= BUSTER_LINK_SOURCE_COUNT || (atom != 0 && !link_symbol_fits(link, atom, value, size, flags)) ||
             (source == BUSTER_LINK_SOURCE_ABSOLUTE && !link_absolute_fits(atom, flags)))
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        result = symbols->count;
        symbols->name[result] = name;
        symbols->atom[result] = atom;
        symbols->value[result] = value;
        symbols->size[result] = size;
        symbols->flags[result] = flags;
        symbols->source[result] = source;
        symbols->got_slot[result] = 0;
        symbols->dynamic_index[result] = 0;
        symbols->version[result] = 0;
        symbols->reference[result] = 0;
        symbols->count += 1;
    }
    return result;
}

static bool link_define_symbol(Link* link, SymbolId symbol, AtomId atom, uint64_t value, uint64_t size)
{
    LinkSymbols* symbols = &link->symbols;
    bool result = false;
    if (symbol == 0 || symbol >= symbols->count || atom == 0 || atom >= link->atoms.count)
    {
        link_fail(link, LINK_ERROR_ID);
    }
    else if (symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE || !link_symbol_fits(link, atom, value, size, symbols->flags[symbol]))
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        symbols->atom[symbol] = atom;
        symbols->value[symbol] = value;
        symbols->size[symbol] = size;
        result = true;
    }
    return result;
}

// Whether a fixup of `kind` at `offset` can follow the fixups of `atom`, which exists: a kind that exists, an atom with
// bytes whose field it lies within, and if the atom has fixups, a range that is the table's last and a field after its
// last fixup's.
static bool link_fixup_follows(const Link* link, AtomId atom, uint32_t offset, uint8_t kind)
{
    const LinkAtoms* atoms = &link->atoms;
    const uint32_t count = atoms->fixup_count[atom];
    bool result = kind != BUSTER_LINK_FIXUP_NONE && kind < BUSTER_LINK_FIXUP_KINDS && atoms->bytes[atom] != 0;
    if (result)
    {
        result = (uint64_t)offset + link_fixup_kinds[kind].width <= atoms->size[atom];
    }
    if (result && count != 0)
    {
        const FixupId last = atoms->fixup_begin[atom] + count - 1;
        const uint8_t last_kind = link->fixups.kind[last] & BUSTER_LINK_FIXUP_KIND;
        result = last + 1 == link->fixups.count && (uint64_t)link->fixups.offset[last] + link_fixup_kinds[last_kind].width <= offset;
    }
    return result;
}

static FixupId link_add_fixup(Link* link, AtomId atom, uint32_t offset, uint8_t kind, SymbolId target, int64_t addend)
{
    LinkFixups* fixups = &link->fixups;
    const bool wide = addend < INT32_MIN || addend > INT32_MAX;
    FixupId result = 0;
    if (fixups->count == fixups->capacity || (wide && fixups->wide_count == fixups->wide_capacity))
    {
        link_fail(link, LINK_ERROR_FULL);
    }
    else if (atom == 0 || atom >= link->atoms.count || target == 0 || target >= link->symbols.count)
    {
        link_fail(link, LINK_ERROR_ID);
    }
    else if (!link_fixup_follows(link, atom, offset, kind))
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        result = fixups->count;
        fixups->offset[result] = offset;
        fixups->target[result] = target;
        fixups->kind[result] = kind;
        fixups->addend[result] = (int32_t)addend;
        if (wide)
        {
            // The index, as the column's 32 bits.
            fixups->kind[result] = kind | BUSTER_LINK_FIXUP_WIDE;
            fixups->addend[result] = (int32_t)fixups->wide_count;
            fixups->wide_addends[fixups->wide_count] = addend;
            fixups->wide_count += 1;
        }
        if (link->atoms.fixup_count[atom] == 0)
        {
            link->atoms.fixup_begin[atom] = result;
        }
        link->atoms.fixup_count[atom] += 1;
        fixups->count += 1;
    }
    return result;
}

static int64_t link_fixup_addend(const Link* link, FixupId fixup)
{
    const int32_t addend = link->fixups.addend[fixup];
    int64_t result = addend;
    if ((link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0)
    {
        result = link->fixups.wide_addends[(uint32_t)addend];
    }
    return result;
}

// Whether atoms of the class are an init or a fini array's.
static bool link_class_is_array(uint8_t class)
{
    return class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY;
}

static bool link_add_priority(Link* link, AtomId atom, uint32_t priority)
{
    LinkPriorities* priorities = &link->priorities;
    const uint32_t count = priorities->count;
    bool result = false;
    if (count == priorities->capacity)
    {
        link_fail(link, LINK_ERROR_FULL);
    }
    else if (atom == 0 || atom >= link->atoms.count)
    {
        link_fail(link, LINK_ERROR_ID);
    }
    else if (!link_class_is_array(link->atoms.class[atom]) || priority > UINT16_MAX || (count > 1 && priorities->atom[count - 1] >= atom))
    {
        link_fail(link, LINK_ERROR_RECORD);
    }
    else
    {
        priorities->atom[count] = atom;
        priorities->priority[count] = (uint16_t)priority;
        priorities->count += 1;
        result = true;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Code

// What field `field` of an instruction is (LinkField), from its form and operands. A memory operand's displacement is
// 32-bit when it is RIP-relative, absolute or index-only; after a base, only when it doesn't fit in 8 bits in units of
// an EVEX form's N, since the encoder makes small ones 8-bit or leaves them out, as GNU as does with numbers. So a
// symbolic one there needs a placeholder that doesn't fit, such as INT32_MIN (x86_fixup_distance says the same).
static LinkField link_instruction_field(const X86Instruction* instruction, X86FixupField field)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint8_t mode = instruction->mode;
    const bool has_modrm = form->encoding == BUSTER_X86_ENCODING_R || (form->encoding & BUSTER_X86_ENCODING_DIGIT) != 0;
    const bool memory = has_modrm && (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0 && (mode & BUSTER_X86_MODE_MEMORY) != 0;
    const int32_t scale = form->disp8_scale;
    const int32_t scaled = instruction->displacement / scale;
    const bool short_displacement = instruction->displacement % scale == 0 && scaled >= -128 && scaled <= 127;
    LinkField result = LINK_FIELD_NONE;
    if (field == X86_FIXUP_DISPLACEMENT && form->relative_bytes == 4)
    {
        result = LINK_FIELD_BRANCH32;
    }
    else if (field == X86_FIXUP_DISPLACEMENT && form->relative_bytes == 0 && memory && (mode & BUSTER_X86_MODE_RIP) != 0)
    {
        result = LINK_FIELD_RIP32;
    }
    else if (field == X86_FIXUP_DISPLACEMENT && form->relative_bytes == 0 && memory && ((mode & BUSTER_X86_MODE_BASE) == 0 || !short_displacement))
    {
        result = LINK_FIELD_ABSOLUTE32;
    }
    else if (field == X86_FIXUP_IMMEDIATE && form->immediate_bytes == 4)
    {
        result = LINK_FIELD_ABSOLUTE32;
    }
    else if (field == X86_FIXUP_IMMEDIATE && form->immediate_bytes == 8)
    {
        result = LINK_FIELD_ABSOLUTE64;
    }
    return result;
}

// Whether `kind` can patch the instruction's field: one of the kind's fields and, for the kinds whose instructions
// linkers rewrite (call and jmp through the GOT into direct ones, mov from the GOT into lea, initial-exec into
// local-exec), one of those instructions.
static bool link_kind_fits(uint8_t kind, const X86Instruction* instruction, X86FixupField field)
{
    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
    bool result = kind < BUSTER_LINK_FIXUP_KINDS && (link_fixup_kinds[kind].fields & BUSTER_LINK_FIELD_BIT(link_instruction_field(instruction, field))) != 0;
    if (kind == BUSTER_LINK_FIXUP_CALL_GOT)
    {
        result = result && (form == X86_FORM_CALL_RM64 || form == X86_FORM_JMP_RM64);
    }
    else if (kind == BUSTER_LINK_FIXUP_LOAD_GOT)
    {
        result = result && form == X86_FORM_MOV_R64_RM64;
    }
    else if (kind == BUSTER_LINK_FIXUP_GOTTPOFF)
    {
        result = result && (form == X86_FORM_MOV_R64_RM64 || form == X86_FORM_ADD_R64_RM64);
    }
    else if (kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD || kind == BUSTER_LINK_FIXUP_DTPOFF32)
    {
        // Only imported objects have them (see the kinds above).
        result = false;
    }
    return result;
}

static bool link_add_code_fixups(Link* link, AtomId atom, const X86Instruction* instructions, uint64_t instruction_count, const uint32_t* fixup_ends,
                                 uint64_t marked, const LinkReference* references, uint64_t reference_count)
{
    bool result = atom != 0 && atom < link->atoms.count;
    if (!result)
    {
        link_fail(link, LINK_ERROR_ID);
    }
    // The previous reference's instruction and field, and its instruction's ordinal among the marked ones: one less
    // than the first's, before the first, so that it wraps to 0 there.
    uint64_t previous = UINT64_MAX;
    uint8_t previous_field = X86_FIXUP_IMMEDIATE;
    uint64_t ordinal = UINT64_MAX;
    for (uint64_t i = 0; result && i < reference_count; i += 1)
    {
        const LinkReference* reference = &references[i];
        const uint64_t index = reference->instruction;
        const bool next_instruction = previous == UINT64_MAX || index > previous;
        const bool next_field = index == previous && previous_field == X86_FIXUP_DISPLACEMENT && reference->field == X86_FIXUP_IMMEDIATE;
        ordinal += next_instruction ? 1 : 0;
        result = index < instruction_count && (next_instruction || next_field) && reference->field <= X86_FIXUP_IMMEDIATE && ordinal < marked &&
                 (instructions[index].form & BUSTER_X86_FORM_FIXUP) != 0 && link_kind_fits(reference->kind, &instructions[index], reference->field);
        uint32_t offset = 0;
        int64_t addend = reference->addend;
        if (result)
        {
            const uint32_t end = fixup_ends[ordinal];
            const uint32_t distance = x86_fixup_distance(instructions[index].form, reference->field);
            offset = end - distance;
            result = distance <= end && !(link_fixup_kinds[reference->kind].pc_relative && __builtin_sub_overflow(reference->addend, (int64_t)distance, &addend));
        }
        if (!result)
        {
            link_fail(link, LINK_ERROR_FIELD);
        }
        else
        {
            result = link_add_fixup(link, atom, offset, reference->kind, reference->target, addend) != 0;
        }
        if (result)
        {
            memset(link->atoms.bytes[atom] + offset, 0, link_fixup_kinds[reference->kind].width);
        }
        previous = index;
        previous_field = reference->field;
    }
    // Every marked instruction has a reference.
    if (result && ordinal + 1 != marked)
    {
        link_fail(link, LINK_ERROR_FIELD);
        result = false;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Checks

// The checks of link_check that the adds already make, again, for columns that lanes filled without them: every atom's
// fixups within the table and the atom, in order and apart, with kinds and targets that exist and wide addends in the
// side table, and every fixup in exactly one atom's range (counted in `owned`, a bit per fixup, which starts clear).
static bool link_check_fixups(const Link* link, AtomId atom, uint64_t* owned)
{
    const LinkFixups* fixups = &link->fixups;
    const FixupId begin = link->atoms.fixup_begin[atom];
    const uint32_t count = link->atoms.fixup_count[atom];
    bool result = count == 0 || (begin != 0 && begin <= fixups->count && count <= fixups->count - begin && link->atoms.bytes[atom] != 0);
    uint64_t next = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const FixupId fixup = begin + i;
        const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const bool wide = (fixups->kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0;
        const uint64_t offset = fixups->offset[fixup];
        const uint64_t bit = 1ull << (fixup % 64);
        result = kind != BUSTER_LINK_FIXUP_NONE && kind < BUSTER_LINK_FIXUP_KINDS && fixups->target[fixup] != 0 && fixups->target[fixup] < link->symbols.count &&
                 (!wide || ((uint32_t)fixups->addend[fixup] != 0 && (uint32_t)fixups->addend[fixup] < fixups->wide_count)) && offset >= next &&
                 offset + link_fixup_kinds[kind].width <= link->atoms.size[atom] && (owned[fixup / 64] & bit) == 0;
        if (result)
        {
            owned[fixup / 64] |= bit;
            next = offset + link_fixup_kinds[kind].width;
        }
    }
    return result;
}

// Whether a fixup of `kind` in `atom` can refer to `target`: a TLS kind to a TLS symbol and no other kind to one (but
// a size of either), a local symbol only from its own unit (or an absolute one or an interned STRINGS pool at unit
// zero, which are shared across units), except that terminal EH_FRAME pools at unit zero can refer to the locals of
// their combined units, and a shared library's
// symbol only through the GOT, by its absolute address (which the executable relocates when it loads), or for a function
// by a call (through a stub).
static bool link_check_target(const Link* link, AtomId atom, uint8_t kind, SymbolId target)
{
    const uint8_t flags = link->symbols.flags[target];
    const uint8_t source = link->symbols.source[target];
    const AtomId target_atom = link->symbols.atom[target];
    const bool tls_kind = kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD ||
                          kind == BUSTER_LINK_FIXUP_DTPOFF32;
    const bool size_kind = kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64;
    const bool tls_target = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
    const bool local = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
    const bool shared = source == BUSTER_LINK_SOURCE_RUNTIME_SHARED || source == BUSTER_LINK_SOURCE_FOREIGN_SHARED;
    const bool through_got = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
    const bool call = kind == BUSTER_LINK_FIXUP_CALL && (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
    const bool reaches_shared = through_got || kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_ABS64 || call;
    const bool absolute = source == BUSTER_LINK_SOURCE_ABSOLUTE;
    // Only a terminal EH pool as the source gains this allowance; targets in EH pools retain the ordinary rule.
    const bool unwind_pool = link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME && link->atoms.unit[atom] == 0;
    const bool pooled = target_atom != 0 && link->atoms.class[target_atom] == BUSTER_LINK_CLASS_STRINGS && link->atoms.unit[target_atom] == 0;
    return (size_kind || tls_kind == tls_target) && (!local || absolute || pooled || (target_atom != 0 && (unwind_pool || link->atoms.unit[target_atom] == link->atoms.unit[atom]))) &&
           (!shared || reaches_shared);
}

static LinkError link_check(Link* link)
{
    const LinkSymbols* symbols = &link->symbols;
    const LinkAtoms* atoms = &link->atoms;
    LinkError result = LINK_ERROR_NONE;
    for (SymbolId symbol = 1; result == LINK_ERROR_NONE && symbol < symbols->count; symbol += 1)
    {
        const uint8_t source = symbols->source[symbol];
        const AtomId atom = symbols->atom[symbol];
        const bool in_atom_source = source == BUSTER_LINK_SOURCE_PROGRAM || source == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE || source == BUSTER_LINK_SOURCE_FOREIGN_OBJECT;
        const bool absolute = source == BUSTER_LINK_SOURCE_ABSOLUTE;
        const bool local = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
        if (symbols->name[symbol] >= link->names.count || atom >= atoms->count || source >= BUSTER_LINK_SOURCE_COUNT)
        {
            result = LINK_ERROR_ID;
        }
        else if ((source == BUSTER_LINK_SOURCE_PROGRAM && atom == 0) || (!in_atom_source && atom != 0) || (local && !in_atom_source && !absolute))
        {
            result = LINK_ERROR_BINDING;
        }
        else if ((atom != 0 && !link_symbol_fits(link, atom, symbols->value[symbol], symbols->size[symbol], symbols->flags[symbol])) ||
                 (absolute && !link_absolute_fits(atom, symbols->flags[symbol])))
        {
            result = LINK_ERROR_RECORD;
        }
    }
    // The priorities: by atom in id order, each an init or fini array's.
    const LinkPriorities* priorities = &link->priorities;
    for (uint32_t entry = 1; result == LINK_ERROR_NONE && entry < priorities->count; entry += 1)
    {
        const AtomId atom = priorities->atom[entry];
        if (atom == 0 || atom >= atoms->count)
        {
            result = LINK_ERROR_ID;
        }
        else if (!link_class_is_array(atoms->class[atom]) || (entry > 1 && priorities->atom[entry - 1] >= atom))
        {
            result = LINK_ERROR_RECORD;
        }
    }

    Scratch scratch = scratch_begin(0);
    uint64_t* owned = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, link->fixups.count / 64 + 1);
    if (owned == 0)
    {
        result = result == LINK_ERROR_NONE ? LINK_ERROR_FULL : result;
    }
    else
    {
        memset(owned, 0, (link->fixups.count / 64 + 1) * sizeof(*owned));
    }
    uint64_t fixups = 0;
    for (AtomId atom = 1; result == LINK_ERROR_NONE && atom < atoms->count; atom += 1)
    {
        if (!link_check_fixups(link, atom, owned))
        {
            result = LINK_ERROR_RECORD;
        }
        const FixupId begin = atoms->fixup_begin[atom];
        for (uint32_t i = 0; result == LINK_ERROR_NONE && i < atoms->fixup_count[atom]; i += 1)
        {
            const FixupId fixup = begin + i;
            if (!link_check_target(link, atom, link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND, link->fixups.target[fixup]))
            {
                result = LINK_ERROR_BINDING;
            }
        }
        fixups += atoms->fixup_count[atom];
    }
    // Fixups in no atom's range.
    if (result == LINK_ERROR_NONE && fixups + 1 != link->fixups.count)
    {
        result = LINK_ERROR_RECORD;
    }
    scratch_end(scratch);
    if (result != LINK_ERROR_NONE)
    {
        link_fail(link, result);
    }
    return result;
}
