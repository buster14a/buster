// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The assembly sink (#1362): a unit of the link representation (link.c) as GNU as's AT&T assembly, which -S writes in
// place of -c's object. It prints from the symbolic emission view (AssemblyView), not from the object: the link's atoms,
// symbols and fixups, which hold every relocation's target and addend symbolically, and each TEXT atom's instruction
// records as the emission sized and encoded them (AssemblyCode, its branch columns among them). The object writer stays
// the control: the text describes the object -c writes, and an assembler that keeps the instructions' forms makes the
// same sections, symbols and relocations of it (docs/commands/driver.md's "-S" has the contract and the tests).
//
// The contracts:
//   - Instructions: each record in AT&T syntax, with the pseudo-prefixes GNU as takes to pick the record's own encoding
//     where another one is its default ({load} and {store} between registers, {evex}, {vex}, {disp32} on a rel32
//     branch to a label), and a size suffix on integer instructions. A record GNU as encodes otherwise whatever it is
//     written as (assembly_escaped: a shorter immediate or accumulator form, and the others GNU as prefers) is its bytes,
//     `.byte`, after a comment with its spelling: never another encoding.
//   - Relocations: a fixup is its target's name and its addend as the source writes it, never a field's bytes. A
//     PC-relative field's addend is the link's (relative to the field) plus the distance from the field to the
//     instruction's end (x86_fixup_distance), which GNU as takes off again; a call's is `name@PLT`, a GOT slot's
//     `name@GOTPCREL`, data's `.quad`, `.long` and `name - .`.
//   - Labels: a symbol at its offset, under its own name where the object has one (quoted where GNU as's plain names
//     don't spell it), and an unnamed or temporary one, and each branch's target, under a local `.Lbuster.` name of
//     its own, which no object keeps.
//   - Data: rows of `.byte` (sixteen bytes a row, formatted by an AVX-512 kernel: assembly_byte_row), `.zero` for runs
//     of zeros and for zero-fill sections, `.string` and `.ascii` in string pools. A common symbol is a `.comm`
//     with its size and byte alignment. Its atom occupies no space in a section.
//   - Sections: each atom's (its descriptor's, or its class's object section), in atom order, aligned by `.p2align`
//     with int3 in code and zeros elsewhere, as the object writer fills its gaps. Linked-order entries name a label in
//     their associated section and have a unique number so separate entries can share a section name.
// What the sink doesn't print yet it refuses (ASSEMBLY_UNSUPPORTED, `reason` naming it): unknown section descriptors,
// code without retained records, unique symbols whose type GNU as cannot represent, and fixup kinds
// that GNU as can't spell where they are. A view that contradicts itself (records that don't encode to their atom's
// bytes, a fixup off every field) is ASSEMBLY_INVALID: an internal error, never printed around.
//
// The writer is scalar per record and per symbol: formatting is variable-length text, and the sink is -S's alone
// (a compile without -S runs none of it). The bulk of a large unit's text, its data's rows, goes through the vector
// kernel; the per-record formatting is independent across functions, which a later stage can cut into chunks on lanes
// by prefix sums of their lengths.
//
// Layout: the API (AssemblyCode, AssemblyView, AssemblyError, AssemblyResult, assembly_print); the text (AssemblyText
// and its appenders); names (assembly_plain_name, AssemblyNames, assembly_names, assembly_name); instructions
// (assembly_mnemonic, assembly_memory, assembly_escaped, assembly_instruction); data (assembly_byte_row,
// assembly_data); and the unit (assembly_section, assembly_attributes, assembly_code, assembly_print).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <elf.h>

#include "host_isa.h"

// ---------------------------------------------------------------------------------------------------------------------
// API

// The prefix of every name the sink makes (a branch's target, an unnamed or temporary symbol): a local label of GNU as's
// that no C identifier spells. A link with a name of this prefix is refused rather than printed ambiguously.
#define BUSTER_ASSEMBLY_PREFIX ".Lbuster."

typedef enum AssemblyError
{
    ASSEMBLY_OK,
    // The output arena can't hold the text, or the scratch arena the tables.
    ASSEMBLY_FULL,
    // A feature the sink doesn't print yet: AssemblyResult.reason names it.
    ASSEMBLY_UNSUPPORTED,
    // The view contradicts itself: AssemblyResult.reason says how.
    ASSEMBLY_INVALID,
} AssemblyError;

// Substituted inline text and the range it occupies in its atom. Extraction-only marker lines
// have zero width. A directive assembled in a helper occupies its actual byte range.
typedef struct AssemblyInline AssemblyInline;
struct AssemblyInline
{
    String8 text;
    uint32_t offset;
    uint32_t size;
    uint32_t statement;
};

// A TEXT atom's instructions, as the emission made them: `count` records, sized (each branch's rel8 or rel32 form) and
// encoded into the atom's bytes, and its branches to its own records (`branches[i]` targets `targets[i]`, a record or
// `count` for the code's end).
typedef struct AssemblyCode AssemblyCode;
struct AssemblyCode
{
    const X86Instruction* records;
    // A full assembly unit's records may have data between them. Non-null offsets select the mixed-code writer.
    const uint32_t* offsets;
    const uint32_t* branches;
    const uint32_t* targets;
    uint32_t count;
    uint32_t branch_count;
    // The patchable-entry bytes in the atom before its first instruction record; among them, indirect branch tracking's
    // endbr64 (#3570), at its offset plus one (0 for none), which is printed as the instruction.
    uint32_t sled;
    uint32_t endbr;
    AssemblyInline* templates;
    uint32_t template_count;
    // Final frame rows, relative to the function entry inside its atom. Only -S retains them; the generated table is
    // replaced by CFI directives whose locations follow the printed instructions, including any assembler relaxation.
    const EmissionFrameRow* frame_rows;
    uint32_t frame_count;
    uint32_t frame_offset;
    uint32_t frame_size;
    bool frame_enabled;
};

// A unit of a link to print: its atoms (those of `unit`), each TEXT atom's code (indexed by AtomId, `code_count` entries),
// optional section descriptors (as ObjectOptions.atom_sections has them) and temporary symbols (as
// ObjectOptions.temporary_symbols), and the source's name for `.file`.
typedef struct AssemblyView AssemblyView;
struct AssemblyView
{
    const Link* link;
    const AssemblyCode* code;
    const ObjectDebug* debug;
    const ObjectAtomSection* sections;
    const uint8_t* temporary;
    String8 source_name;
    uint32_t unit;
    uint32_t code_count;
    uint32_t section_count;
    uint32_t temporary_count;
    // The generated unwind table whose functions publish frame rows in `code`. Other source-supplied tables remain.
    AtomId cfi_atom;
    // Position-independent code's (#2101, ObjectOptions.local_unwind): an unwind table's PC32 to a global or weak symbol
    // the unit defines names a local label of the sink's at the symbol (`.Lbuster.l` and the symbol's number), as the
    // object refers to it through its section's symbol.
    bool local_unwind;
};

// The text, `length` bytes at `bytes` on the output arena, or why there is none; with the atom and symbol it stopped at
// (0 for none).
typedef struct AssemblyResult AssemblyResult;
struct AssemblyResult
{
    uint8_t* bytes;
    uint64_t length;
    const char* reason;
    AtomId atom;
    SymbolId symbol;
    AssemblyError error;
};

// The unit as GNU as source, pushed on `output`, an arena nothing else pushes on meanwhile (the text is one run of its
// bytes), with what printing needs from `scratch` (another arena), which it leaves as it was. On failure `output` is as
// it was too.
static __attribute__((unused)) AssemblyResult assembly_print(const AssemblyView* view, Arena* output, Arena* scratch);

// ---------------------------------------------------------------------------------------------------------------------
// Text

typedef struct AssemblyText AssemblyText;
struct AssemblyText
{
    Arena* arena;
    bool full;
};

static void assembly_bytes(AssemblyText* text, const void* bytes, uint64_t length)
{
    if (!text->full && length != 0)
    {
        uint8_t* place = arena_push(text->arena, length, 1);
        text->full = place == 0;
        if (place != 0)
        {
            memcpy(place, bytes, length);
        }
    }
}

static void assembly_append(AssemblyText* text, const char* literal)
{
    assembly_bytes(text, literal, strlen(literal));
}

static void assembly_unsigned(AssemblyText* text, uint64_t value)
{
    char digits[20];
    uint32_t at = sizeof(digits);
    do
    {
        at -= 1;
        digits[at] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    assembly_bytes(text, digits + at, sizeof(digits) - at);
}

static void assembly_signed(AssemblyText* text, int64_t value)
{
    if (value < 0)
    {
        assembly_append(text, "-");
    }
    assembly_unsigned(text, value < 0 ? 0 - (uint64_t)value : (uint64_t)value);
}

static void assembly_hexadecimal(AssemblyText* text, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[18];
    uint32_t at = sizeof(buffer);
    do
    {
        at -= 1;
        buffer[at] = digits[value & 15];
        value >>= 4;
    } while (value != 0);
    at -= 2;
    buffer[at] = '0';
    buffer[at + 1] = 'x';
    assembly_bytes(text, buffer + at, sizeof(buffer) - at);
}

// A byte as the data rows spell it: 0x and two digits.
static void assembly_byte(AssemblyText* text, uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    const char bytes[4] = { '0', 'x', digits[value >> 4], digits[value & 15] };
    assembly_bytes(text, bytes, sizeof(bytes));
}

// An addend after a name: nothing for 0, else +N or -N.
static void assembly_addend(AssemblyText* text, int64_t addend)
{
    if (addend > 0)
    {
        assembly_append(text, "+");
    }
    if (addend != 0)
    {
        assembly_signed(text, addend);
    }
}

// A register's AT&T name: %, then the disassembler's name of it (x86_disassembly_register), but %st for st(0).
static void assembly_register(AssemblyText* text, uint32_t reg, uint32_t operand)
{
    char bytes[32];
    X86DisassemblyText name = {
        .data = bytes,
        .capacity = sizeof(bytes),
    };
    x86_disassembly_register(&name, reg, operand);
    const bool st0 = (operand & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_ST && (reg & BUSTER_X86_REGISTER_NUMBER) == 0;
    assembly_append(text, "%");
    assembly_bytes(text, st0 ? "st" : bytes, st0 ? 2 : name.length);
}

// ---------------------------------------------------------------------------------------------------------------------
// Names

// Both assemblers read these plain names. GNU as also accepts `$`, which Buster's lexer splits out of section names.
static bool assembly_plain_name(const uint8_t* bytes, uint64_t length, bool quote_dollar)
{
    bool plain = length != 0 && !(bytes[0] >= '0' && bytes[0] <= '9');
    for (uint64_t i = 0; plain && i < length; i += 1)
    {
        const uint8_t c = bytes[i];
        plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || (c == '$' && !quote_dollar);
    }
    return plain;
}

static void assembly_identifier(AssemblyText* text, const uint8_t* bytes, uint64_t length, bool quote_dollar)
{
    if (assembly_plain_name(bytes, length, quote_dollar))
    {
        assembly_bytes(text, bytes, length);
    }
    else
    {
        // GNU as's quoted name: a backslash before `"` and `\`, and every other byte as it is.
        assembly_append(text, "\"");
        uint64_t start = 0;
        for (uint64_t i = 0; i < length; i += 1)
        {
            if (bytes[i] == '"' || bytes[i] == '\\')
            {
                assembly_bytes(text, bytes + start, i - start);
                assembly_append(text, "\\");
                start = i;
            }
        }
        assembly_bytes(text, bytes + start, length - start);
        assembly_append(text, "\"");
    }
}

// What the sink calls each symbol: its own name (own[s] = 1) or BUSTER_ASSEMBLY_PREFIX "s" and its id (own[s] = 0). A
// symbol takes its own name where it has one, unless it is temporary and the name isn't a local label's (a temporary's
// own name would put it in the object's symbol table, where the object writer leaves it out).
typedef struct AssemblyNames AssemblyNames;
struct AssemblyNames
{
    uint8_t* own;
    const SymbolId* slots;
    uint32_t mask;
    // Each symbol's uses (assembly_print's `used`): 4 for a PIC unwind reference, 8 for DWARF, both naming the local definition.
    const uint8_t* used;
    // Each form row's assembly_ambiguous answer, as the walk finds it: the printing's other per-unit table.
    uint8_t* widths;
};

static void assembly_name(AssemblyText* text, const Link* link, const AssemblyNames* names, SymbolId symbol)
{
    if (names->own[symbol] != 0)
    {
        const NameId name = link->symbols.name[symbol];
        assembly_identifier(text, link->names.bytes[name], link->names.length[name], false);
    }
    else
    {
        assembly_append(text, BUSTER_ASSEMBLY_PREFIX "s");
        assembly_unsigned(text, symbol);
    }
}

// The slot for a name the sink spells explicitly, or the empty slot where it would go.
static uint32_t assembly_name_slot(const AssemblyNames* names, const Link* link, const uint8_t* bytes, uint64_t length, uint64_t hash)
{
    uint32_t slot = (uint32_t)hash & names->mask;
    while (names->slots[slot] != 0)
    {
        const NameId other = link->symbols.name[names->slots[slot]];
        if (link->names.length[other] == length && memcmp(link->names.bytes[other], bytes, length) == 0)
        {
            break;
        }
        slot = (slot + 1) & names->mask;
    }
    return slot;
}

// Fills `names` for the view's symbols, from `scratch`, and checks that each own name the text spells is one symbol's:
// two symbols the unit names alike (each it defines or refers to) would be one symbol to GNU as. Returns the error, with
// the symbol in `result`.
static AssemblyError assembly_names(AssemblyNames* names, const AssemblyView* view, const uint8_t* used, Arena* scratch, AssemblyResult* result)
{
    const Link* link = view->link;
    const uint32_t count = link->symbols.count;
    names->own = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count);
    names->widths = names->own != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, X86_FORM_COUNT) : 0;
    if (names->widths != 0)
    {
        memset(names->widths, 0, X86_FORM_COUNT);
    }
    uint32_t slots = 16;
    while (slots < 2 * (uint64_t)count && slots < (UINT32_C(1) << 31))
    {
        slots *= 2;
    }
    SymbolId* table = names->widths != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, slots) : 0;
    AssemblyError error = table != 0 ? ASSEMBLY_OK : ASSEMBLY_FULL;
    if (table != 0)
    {
        memset(table, 0, (uint64_t)slots * sizeof(*table));
        names->slots = table;
        names->mask = slots - 1;
        names->own[0] = 0;
    }
    const uint64_t prefix_length = sizeof(BUSTER_ASSEMBLY_PREFIX) - 1;
    for (SymbolId s = 1; error == ASSEMBLY_OK && s < count; s += 1)
    {
        const NameId name = link->symbols.name[s];
        const uint8_t* bytes = name != 0 ? link->names.bytes[name] : 0;
        const uint32_t length = name != 0 ? link->names.length[name] : 0;
        const bool temporary = view->temporary != 0 && s < view->temporary_count && view->temporary[s] != 0;
        const bool local_label = length >= 2 && bytes[0] == '.' && bytes[1] == 'L';
        names->own[s] = (uint8_t)(name != 0 && (!temporary || local_label));
        if (name != 0 && length >= prefix_length && memcmp(bytes, BUSTER_ASSEMBLY_PREFIX, prefix_length) == 0)
        {
            error = ASSEMBLY_UNSUPPORTED;
            result->reason = "a symbol whose name starts with " BUSTER_ASSEMBLY_PREFIX ", which -S's own labels use";
            result->symbol = s;
        }
        if (error == ASSEMBLY_OK && names->own[s] != 0 && used[s] != 0)
        {
            const uint32_t slot = assembly_name_slot(names, link, bytes, length, link->names.hash[name]);
            if (table[slot] == 0)
            {
                table[slot] = s;
            }
            else
            {
                error = ASSEMBLY_UNSUPPORTED;
                result->reason = "two symbols of the same name, which GNU as would take for one";
                result->symbol = s;
            }
        }
    }
    return error;
}

// ---------------------------------------------------------------------------------------------------------------------
// Instructions

// The AT&T mnemonic of a record's form, and whether it takes an integer size suffix (assembly_suffix): the extension
// moves' own names (movzbl, movslq), the conversions' (cltq), the far returns' (lretq), the x87's reversed subtractions
// and divisions of the System V assembler (GNU as reads `fsub %st, %st(1)` as Intel's fsubr st(1), st(0)), and the
// string instructions' dword l (movsl).
static void assembly_mnemonic(AssemblyText* text, const X86FormDescription* form, const X86Instruction* record, bool* suffix)
{
    const char* mnemonic = form->mnemonic;
    const bool zero = strcmp(mnemonic, "movzx") == 0;
    const bool sign = strcmp(mnemonic, "movsx") == 0 || strcmp(mnemonic, "movsxd") == 0;
    *suffix = !zero && !sign;
    if (zero || sign)
    {
        static const char letters[] = "?bw?l???q";
        const uint32_t destination = 1u << ((form->reg & BUSTER_X86_OPERAND_CLASS) - BUSTER_X86_CLASS_GPR8);
        const char name[] = { 'm', 'o', 'v', zero ? 'z' : 's', letters[form->operand_bytes & 15], letters[destination & 15], 0 };
        assembly_append(text, name);
    }
    else
    {
        static const char* const renamed[][2] = {
            { "cbw", "cbtw" },
            { "cwde", "cwtl" },
            { "cdqe", "cltq" },
            { "cwd", "cwtd" },
            { "cdq", "cltd" },
            { "cqo", "cqto" },
            { "iretd", "iretl" },
            { "sysretd", "sysretl" },
            { "retfw", "lretw" },
            { "retfd", "lretl" },
            { "retfq", "lretq" },
        };
        static const char* const reversed[][2] = {
            { "fsub", "fsubr" },
            { "fsubr", "fsub" },
            { "fdiv", "fdivr" },
            { "fdivr", "fdiv" },
            { "fsubp", "fsubrp" },
            { "fsubrp", "fsubp" },
            { "fdivp", "fdivrp" },
            { "fdivrp", "fdivp" },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(renamed); i += 1)
        {
            const bool match = strcmp(mnemonic, renamed[i][0]) == 0;
            mnemonic = match ? renamed[i][1] : mnemonic;
            *suffix = *suffix && !match;
        }
        for (uint32_t i = 0; form->syntax == X86_SYNTAX_RM_ST && i < BUSTER_ARRAY_LENGTH(reversed); i += 1)
        {
            const bool popping = i >= 4;
            mnemonic = strcmp(form->mnemonic, reversed[i][0]) == 0 && (popping || record->rm != 0) ? reversed[i][1] : mnemonic;
        }
        const uint64_t length = strlen(mnemonic);
        // The x87's 16-bit environment and state forms (fnstenvw, frstorw): AT&T's s for the w (fnstenvs).
        const bool environment = mnemonic[0] == 'f' && length > 1 && mnemonic[length - 1] == 'w' && form->operand_bytes != 2 && form->family == BUSTER_X86_FAMILY_LEGACY;
        const char* tail = length >= 5 ? mnemonic + length - 5 : "";
        const bool string = form->syntax == X86_SYNTAX_NONE && length >= 4 && mnemonic[length - 1] == 'd' &&
                            (strncmp(tail, "movs", 4) == 0 || strncmp(tail, "cmps", 4) == 0 || strncmp(tail, "lods", 4) == 0 || strncmp(tail, "stos", 4) == 0 ||
                             strncmp(tail, "scas", 4) == 0 || strncmp(tail, "outs", 4) == 0 || strncmp(mnemonic + length - 4, "ins", 3) == 0);
        assembly_bytes(text, mnemonic, length - (string || environment ? 1 : 0));
        assembly_append(text, string ? "l" : environment ? "s" : "");
        *suffix = *suffix && !string && !environment;
    }
}

// Whether a register operand's class is a general register's (or the form has no such operand).
static bool assembly_general(uint16_t operand)
{
    const uint32_t kind = operand & BUSTER_X86_OPERAND_CLASS;
    return kind == BUSTER_X86_CLASS_NONE || (kind >= BUSTER_X86_CLASS_GPR8 && kind <= BUSTER_X86_CLASS_GPR64);
}

// Whether GNU as needs a size suffix to read the record as its own form (`widths`, a byte per form row: 0 not looked at
// yet, 1 no, 2 yes): its operands leave the width open, which they do where another form of its mnemonic takes the same
// syntax and the same registers but a memory operand (or, without a ModRM operand, an immediate) of another width. A
// register operand sizes the instruction, and an instruction of one width (prefetch, ldmxcsr) takes no suffix.
static bool assembly_ambiguous(const X86Instruction* record, const X86FormDescription* form, uint8_t* widths)
{
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool immediate_only = form->rm == BUSTER_X86_OPERAND_NONE && form->reg == BUSTER_X86_OPERAND_NONE && form->immediate_bytes != 0 &&
                                form->syntax != X86_SYNTAX_ACC_IMM && form->syntax != X86_SYNTAX_IMM_ACC;
    const bool broadcast = (form->evex & BUSTER_X86_EVEX_BROADCAST) != 0;
    if (widths[row] == 0)
    {
        const uint32_t width = x86_match_suffix_width(form);
        bool other = false;
        for (uint32_t r = 0; !other && r < X86_FORM_COUNT; r += 1)
        {
            const X86FormDescription* candidate = &x86_form_descriptions[r];
            other = candidate->syntax == form->syntax && (candidate->reg & BUSTER_X86_OPERAND_CLASS) == (form->reg & BUSTER_X86_OPERAND_CLASS) &&
                    (candidate->vvvv & BUSTER_X86_OPERAND_CLASS) == (form->vvvv & BUSTER_X86_OPERAND_CLASS) &&
                    (form->rm == BUSTER_X86_OPERAND_NONE || (candidate->rm & BUSTER_X86_OPERAND_MEMORY) != 0) && x86_match_suffix_width(candidate) != width &&
                    strcmp(candidate->mnemonic, form->mnemonic) == 0;
        }
        widths[row] = other ? 2 : 1;
    }
    return widths[row] == 2 && (memory || immediate_only) && !broadcast;
}

// The AT&T size suffix of a record that needs one (assembly_ambiguous; 0 for none): b, w, l or q by an integer
// operation's width (the memory's, or a general register's side of a conversion: cvtsi2sdl), the x87's memory's s, l, t
// or ll, and a vector memory operand's x, y or z.
static const char* assembly_suffix(const X86Instruction* record, const X86FormDescription* form, uint8_t* widths)
{
    const uint32_t width = x86_match_suffix_width(form);
    const uint32_t rm = form->rm & BUSTER_X86_OPERAND_CLASS;
    const char* result = 0;
    if (!assembly_ambiguous(record, form, widths))
    {
        result = 0;
    }
    else if (form->family == BUSTER_X86_FAMILY_LEGACY && form->mnemonic[0] == 'f' && rm == BUSTER_X86_CLASS_NONE)
    {
        const bool integer = form->mnemonic[1] == 'i';
        result = integer ? (width == 2 ? "s" : width == 4 ? "l" : width == 8 ? "ll" : 0) : (width == 4 ? "s" : width == 8 ? "l" : width == 10 ? "t" : 0);
    }
    else if (rm >= BUSTER_X86_CLASS_XMM && rm <= BUSTER_X86_CLASS_ZMM)
    {
        result = width == 16 ? "x" : width == 32 ? "y" : width == 64 ? "z" : 0;
    }
    else
    {
        result = width == 1 ? "b" : width == 2 ? "w" : width == 4 ? "l" : width == 8 ? "q" : 0;
    }
    return result;
}

// What a fixup on a record's field prints: its kind, target, and the addend as the source writes it.
typedef struct AssemblyReference AssemblyReference;
struct AssemblyReference
{
    int64_t addend;
    SymbolId target;
    uint8_t kind;
    bool present;
};

// A reference's expression: the target's name, its modifier (@PLT for a call, @GOTPCREL for a GOT slot) and its
// addend. False for a kind the sink doesn't spell in this field.
static bool assembly_reference(AssemblyText* text, const Link* link, const AssemblyNames* names, const AssemblyReference* reference, bool relative, bool rip, bool memory)
{
    const uint8_t kind = reference->kind;
    const bool call = kind == BUSTER_LINK_FIXUP_CALL && relative;
    const bool got = rip && (kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL);
    const bool pc = kind == BUSTER_LINK_FIXUP_PC32 && (relative || rip);
    // A displacement's absolute address is sign-extended (R_X86_64_32S, as GNU as writes one); an immediate's is any of
    // the three, by its field's width and extension, which GNU as reads from the instruction.
    const bool absolute = !relative && !rip && (kind == BUSTER_LINK_FIXUP_ABS32S || (!memory && (kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS64)));
    const bool tls_offset = !relative && !rip && (kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_DTPOFF32 ||
                                                (!memory && (kind == BUSTER_LINK_FIXUP_TPOFF64 || kind == BUSTER_LINK_FIXUP_DTPOFF64)));
    const bool tls_address = rip && (kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD);
    const bool spelled = call || got || pc || absolute || tls_offset || tls_address;
    if (spelled)
    {
        assembly_name(text, link, names, reference->target);
        const char* modifier = call ? "@PLT" : got ? "@GOTPCREL" :
                               kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_TPOFF64 ? "@TPOFF" :
                               kind == BUSTER_LINK_FIXUP_DTPOFF32 || kind == BUSTER_LINK_FIXUP_DTPOFF64 ? "@DTPOFF" :
                               kind == BUSTER_LINK_FIXUP_GOTTPOFF ? "@GOTTPOFF" : kind == BUSTER_LINK_FIXUP_TLSGD ? "@TLSGD" :
                               kind == BUSTER_LINK_FIXUP_TLSLD ? "@TLSLD" : "";
        assembly_append(text, modifier);
        assembly_addend(text, reference->addend);
    }
    return spelled;
}

// A memory operand in AT&T syntax: the segment, the displacement (or the reference's expression), then (base, index,
// scale) or (%rip); a broadcast's {1toN} after it.
static bool assembly_memory(AssemblyText* text, const Link* link, const AssemblyNames* names, const X86Instruction* record, const X86FormDescription* form,
                            const AssemblyReference* reference)
{
    const uint32_t segment = x86_form_segment(record->form);
    if (segment != 0)
    {
        assembly_append(text, segment == BUSTER_X86_SEGMENT_FS ? "%fs:" : segment == BUSTER_X86_SEGMENT_GS ? "%gs:" : "%ss:");
    }
    const bool rip = (record->mode & BUSTER_X86_MODE_RIP) != 0;
    const bool base = (record->mode & BUSTER_X86_MODE_BASE) != 0;
    const bool hidden_index = (form->rm & BUSTER_X86_OPERAND_SIB) && record->index == 4;
    const bool index = (record->mode & BUSTER_X86_MODE_INDEX) != 0 && !hidden_index;
    bool spelled = true;
    if (reference->present)
    {
        spelled = assembly_reference(text, link, names, reference, false, rip, true);
    }
    else if (record->displacement != 0 || (!base && !index && !rip))
    {
        assembly_signed(text, record->displacement);
    }
    if (rip)
    {
        assembly_append(text, "(%rip)");
    }
    else if (base || index)
    {
        assembly_append(text, "(");
        if (base)
        {
            assembly_register(text, record->rm, BUSTER_X86_CLASS_GPR64);
        }
        if (index)
        {
            assembly_append(text, ",");
            assembly_register(text, record->index, (form->rm & BUSTER_X86_OPERAND_VSIB) ? form->rm : BUSTER_X86_CLASS_GPR64);
            assembly_append(text, ",");
            assembly_unsigned(text, 1u << (record->mode & BUSTER_X86_MODE_SCALE));
        }
        assembly_append(text, ")");
    }
    if (form->evex & BUSTER_X86_EVEX_BROADCAST)
    {
        assembly_append(text, "{1to");
        assembly_unsigned(text, form->operand_bytes / form->disp8_scale);
        assembly_append(text, "}");
    }
    return spelled;
}

// The form's last opcode byte.
static uint32_t assembly_opcode(const X86FormDescription* form)
{
    return form->opcode & 0xff;
}

// Whether GNU as encodes the record's instruction with another form whatever it is written as, and no pseudo-prefix
// asks for the record's (the encoder tests' x86_assembler_prefers_another_form, in 64-bit mode): MOVQ's W1 forms with
// memory (GNU as's are F3 0F 7E and 66 0F D6, and MMX's 0F 6F and 0F 7F); the accumulator's short forms of 80, 81,
// F6/F7 /0 and 87; B0+r and B8+r for MOV r, imm; the imm8 forms of 81, 69 and 68 and of the accumulator's arithmetic; D0
// and D1 for a shift by 1; INT3 for INT 3; FMA4's and XOP's other W between registers; 50+r and 58+r for PUSH and POP
// of a register; XCHG EAX, EAX's and RAX's 90 forms; PREFETCHIT0 and 1 off RIP; LEA with a segment override; the x87's
// DC between st(0) and st(0); and an index scaled by 1 without a base, which GNU as makes the base. And MPX's
// bnd-prefixed branches (notrack's too), which GNU as reads only after `.arch .mpx` since binutils 2.46. The sink prints
// such a record's bytes. `symbolic` says the immediate is a relocation's, whose value GNU as can't shorten: the rules of
// its value don't apply.
static bool assembly_escaped(const X86Instruction* record, const X86FormDescription* form, bool symbolic)
{
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool register_direct = !memory && form->rm != BUSTER_X86_OPERAND_NONE;
    const bool legacy = form->family == BUSTER_X86_FAMILY_LEGACY;
    const uint32_t opcode = assembly_opcode(form);
    const bool one = form->opcode_bytes == 1;
    const int32_t immediate = form->immediate_bytes == 1 ? (int8_t)record->immediate : form->immediate_bytes == 2 ? (int16_t)record->immediate : record->immediate;
    const bool fits8 = !symbolic && immediate >= -128 && immediate <= 127;
    const bool movq = row == X86_FORM_MOVQ_X_RM64 || row == X86_FORM_MOVQ_RM64_X || row == X86_FORM_VMOVQ_X_RM64 || row == X86_FORM_VMOVQ_RM64_X ||
                      row == X86_FORM_VMOVQ_X_XM64_EVEX || row == X86_FORM_VMOVQ_XM64_X_EVEX || row == X86_FORM_MOVQ_MM_RM64 || row == X86_FORM_MOVQ_RM64_MM;
    const bool index_only = (record->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX)) == BUSTER_X86_MODE_INDEX && (record->mode & BUSTER_X86_MODE_SCALE) == 0 &&
                            !(form->rm & (BUSTER_X86_OPERAND_VSIB | BUSTER_X86_OPERAND_SIB)) && (form->reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_BND;
    bool result = memory && movq;
    result |= legacy && one && register_direct && record->rm == 0 && (opcode == 0x80 || opcode == 0x81 || ((opcode == 0xf6 || opcode == 0xf7) && form->encoding == BUSTER_X86_ENCODING_D0));
    result |= legacy && one && register_direct && opcode == 0x87 && (record->rm == 0 || record->reg == 0);
    result |= legacy && one && register_direct && (opcode == 0xc6 || opcode == 0xc7) && !form->w;
    result |= legacy && one && (opcode == 0x81 || opcode == 0x69 || opcode == 0x68) && form->immediate_bytes != 1 && fits8;
    result |= legacy && one && (opcode & 0xc7) == 0x05 && form->syntax == X86_SYNTAX_ACC_IMM && fits8;
    result |= legacy && one && (opcode == 0xc0 || opcode == 0xc1) && !symbolic && record->immediate == 1;
    result |= legacy && one && opcode == 0xcd && !symbolic && record->immediate == 3;
    result |= register_direct && form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4 && form->opcode >= 0x0f3a5c;
    result |= register_direct && form->w && ((form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM && x86_opcode_map(form) >= 8) || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM);
    result |= legacy && one && register_direct && (opcode == 0xff || opcode == 0x8f) && (form->encoding == BUSTER_X86_ENCODING_D6 || opcode == 0x8f);
    result |= (row == X86_FORM_XCHG_R32_EAX || row == X86_FORM_XCHG_R64_RAX) && record->rm == 0;
    result |= (row == X86_FORM_PREFETCHIT0_M8 || row == X86_FORM_PREFETCHIT1_M8) && !(record->mode & BUSTER_X86_MODE_RIP);
    result |= legacy && one && opcode == 0x8d && x86_form_segment(record->form) != 0;
    result |= legacy && one && register_direct && opcode == 0xdc && record->rm == 0;
    result |= memory && index_only;
    result |= strncmp(form->mnemonic, "bnd ", 4) == 0 || strstr(form->mnemonic, " bnd ") != 0;
    return result;
}

// Whether a register-to-register record of a form GNU as encodes in both directions needs {load} or {store} to keep
// its own (0 for neither): the integer instructions' load direction (03, 0B ... 3B, 8A and 8B), whose store
// direction is GNU as's default, and the vector moves' either direction, whose default varies (and VEX's swaps for a
// two-byte prefix).
static const char* assembly_direction(const X86Instruction* record, const X86FormDescription* form)
{
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool both = !memory && form->reg != BUSTER_X86_OPERAND_NONE && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_REG_RM);
    const bool integer = assembly_general(form->reg) && assembly_general(form->rm);
    const uint32_t opcode = assembly_opcode(form);
    const bool alu = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1 && ((opcode < 0x40 && (opcode & 7) <= 3) || (opcode >= 0x88 && opcode <= 0x8b));
    const char* name = form->mnemonic;
    const bool move = strncmp(name, "mov", 3) == 0 || strncmp(name, "vmov", 4) == 0;
    const char* result = 0;
    if (both && integer && alu && form->syntax == X86_SYNTAX_REG_RM)
    {
        result = "{load} ";
    }
    else if (both && !integer && move)
    {
        result = form->syntax == X86_SYNTAX_REG_RM ? "{load} " : "{store} ";
    }
    return result;
}

// Whether an EVEX record could be VEX's, which GNU as prefers without {evex}: no mask, zeroing, broadcast or embedded
// rounding, at most 256 bits, and registers below 16.
static bool assembly_evex_needed(const X86Instruction* record, const X86FormDescription* form)
{
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool decorated = (record->decorations & (BUSTER_X86_DECORATION_MASK | BUSTER_X86_DECORATION_ZEROING)) != 0 ||
                           (form->evex & (BUSTER_X86_EVEX_BROADCAST | BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
    const bool high = (record->reg & BUSTER_X86_REGISTER_NUMBER) >= 16 || (record->vvvv & BUSTER_X86_REGISTER_NUMBER) >= 16 ||
                      (!memory && (record->rm & BUSTER_X86_REGISTER_NUMBER) >= 16) || (memory && (record->index & BUSTER_X86_REGISTER_NUMBER) >= 16);
    return form->family == BUSTER_X86_FAMILY_EVEX && x86_opcode_map(form) != 4 && !decorated && !high && form->vector_length < 2;
}

// The Intel order of a form's operands as the disassembler's formatter spells them (x86_disassembly_format_scalar):
// r reg, v vvvv, m rm, i immediate, t a branch's target, a the accumulator, o a moffs, l and h ENTER's, j and k
// EXTRQ's, 4 an /is4 register, z its selector, c CL, d DX, s st(0), x XMM0, q [rcx] and 1 the shifts' 1.
static const char* assembly_operands(uint8_t syntax)
{
    static const char* const operands[] = {
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
    return syntax < BUSTER_ARRAY_LENGTH(operands) ? operands[syntax] : 0;
}

// An immediate's value as the instruction takes it: a sign-extended imm8's (83, 6B, 6A) signed, another imm8's unsigned,
// and a wider one's as the record holds it (a sign-extended imm32's signed; a 64-bit immediate whole).
static void assembly_immediate(AssemblyText* text, const X86Instruction* record, const X86FormDescription* form)
{
    const uint32_t opcode = assembly_opcode(form);
    const bool extended = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1 && (opcode == 0x83 || opcode == 0x6b || opcode == 0x6a);
    assembly_append(text, "$");
    if (form->immediate_bytes == 8)
    {
        assembly_hexadecimal(text, (uint32_t)record->immediate | ((uint64_t)(uint32_t)record->displacement << 32));
    }
    else if (form->immediate_bytes == 1 && extended)
    {
        assembly_signed(text, (int8_t)record->immediate);
    }
    else if (form->immediate_bytes == 1)
    {
        assembly_unsigned(text, (uint8_t)record->immediate);
    }
    else
    {
        assembly_signed(text, record->immediate);
    }
}

// One record, a line of its own: the pseudo-prefixes, the mnemonic and suffix, and the operands in AT&T order (the
// Intel order reversed, the destination's mask and zeroing after it, embedded rounding after the immediate or first),
// with its references (`displacement` and `immediate`, `present` or not) and its branch's target label (`target`, a
// record of its atom, or UINT32_MAX). An escaped record (assembly_escaped) is its `length` bytes, `encoded`. Returns
// false, with `reason`, for a reference the sink can't spell there.
static bool assembly_instruction(AssemblyText* text, const Link* link, const AssemblyNames* names, uint8_t* widths, const X86Instruction* record, const uint8_t* encoded,
                                 uint32_t length, const AssemblyReference* displacement, const AssemblyReference* immediate, AtomId atom, uint32_t target, const char** reason)
{
    const X86FormDescription* form = x86_form_description(record->form);
    const char* operands = assembly_operands(form->syntax);
    const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0;
    // The callers split a relocated record GNU as encodes otherwise into bytes and symbolic fields before this point.
    const bool otherwise = assembly_escaped(record, form, immediate->present);
    const bool referenced = displacement->present || immediate->present;
    const bool escaped = otherwise && !referenced;
    bool spelled = operands != 0;
    if (escaped)
    {
        assembly_append(text, "\t# ");
        assembly_append(text, form->mnemonic);
        assembly_append(text, " (GNU as would encode it otherwise)\n\t.byte ");
        for (uint32_t i = 0; i < length; i += 1)
        {
            assembly_append(text, i == 0 ? "" : ",");
            assembly_byte(text, encoded[i]);
        }
    }
    else if (spelled)
    {
        assembly_append(text, "\t");
        const char* direction = assembly_direction(record, form);
        assembly_append(text, direction != 0 ? direction : "");
        assembly_append(text, assembly_evex_needed(record, form) ? "{evex} " : "");
        assembly_append(text, form->family == BUSTER_X86_FAMILY_VEX && x86_match_yields((uint32_t)(form - x86_form_descriptions)) ? "{vex} " : "");
        const bool near = form->syntax == X86_SYNTAX_REL && form->relative_bytes >= 2 && assembly_opcode(form) != 0xe8 && target != UINT32_MAX;
        assembly_append(text, near || (record->mode & BUSTER_X86_MODE_DISP32) ? "{disp32} " : "");
        bool suffixed = false;
        assembly_mnemonic(text, form, record, &suffixed);
        const char* suffix = suffixed ? assembly_suffix(record, form, widths) : 0;
        assembly_append(text, suffix != 0 ? suffix : "");
        const uint64_t name_length = strlen(form->mnemonic);
        const bool indirect = form->syntax == X86_SYNTAX_RM && ((name_length >= 3 && strcmp(form->mnemonic + name_length - 3, "jmp") == 0) ||
                                                                (name_length >= 4 && strcmp(form->mnemonic + name_length - 4, "call") == 0));
        const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
        static const char* const rounding_names[4] = { "{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}" };
        const char* rounding_name = (form->evex & BUSTER_X86_EVEX_ROUNDING) ? rounding_names[(record->decorations >> 5) & 3] : "{sae}";
        const bool has_immediate = strchr(operands, 'i') != 0;
        // A conversion from a general register takes its rounding after that source (vcvtsi2sd %rax, {rn-sae}, ...),
        // as GNU as reads it; any other form first, or after its immediate.
        const uint32_t rm_class = form->rm & BUSTER_X86_OPERAND_CLASS;
        const bool after_source = rounding && !has_immediate && rm_class >= BUSTER_X86_CLASS_GPR8 && rm_class <= BUSTER_X86_CLASS_GPR64;
        const uint32_t count = (uint32_t)strlen(operands);
        const uint32_t accumulator = form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 2 ? BUSTER_X86_CLASS_GPR16 : form->operand_bytes == 4 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
        bool first = true;
        if (rounding && !has_immediate && !after_source)
        {
            assembly_append(text, " ");
            assembly_append(text, rounding_name);
            first = false;
        }
        for (uint32_t k = count; spelled && k-- > 0;)
        {
            const char letter = operands[k];
            // The shifts' 1 is implicit in AT&T's spelling too (shll %eax), which GNU as encodes as D1.
            if (letter != '1')
            {
                assembly_append(text, first ? " " : ", ");
                first = false;
            }
            switch (letter)
            {
            case 'r':
                assembly_register(text, record->reg, form->reg);
                break;
            case 'v':
                assembly_register(text, record->vvvv, form->vvvv);
                break;
            case 'm':
                assembly_append(text, indirect ? "*" : "");
                if (memory)
                {
                    spelled = assembly_memory(text, link, names, record, form, displacement);
                }
                else
                {
                    assembly_register(text, record->rm, form->rm);
                }
                if (after_source)
                {
                    assembly_append(text, ", ");
                    assembly_append(text, rounding_name);
                }
                break;
            case 'i':
                if (immediate->present)
                {
                    assembly_append(text, "$");
                    spelled = assembly_reference(text, link, names, immediate, false, false, false);
                }
                else
                {
                    assembly_immediate(text, record, form);
                }
                if (rounding)
                {
                    assembly_append(text, ", ");
                    assembly_append(text, rounding_name);
                }
                break;
            case 't':
                if (displacement->present)
                {
                    spelled = assembly_reference(text, link, names, displacement, true, false, false);
                }
                else
                {
                    spelled = target != UINT32_MAX;
                    assembly_append(text, BUSTER_ASSEMBLY_PREFIX);
                    assembly_unsigned(text, atom);
                    assembly_append(text, ".");
                    assembly_unsigned(text, target);
                }
                break;
            case 'a':
                assembly_register(text, 0, accumulator);
                break;
            case 'o':
                spelled = !immediate->present;
                assembly_hexadecimal(text, (uint32_t)record->immediate | ((uint64_t)(uint32_t)record->displacement << 32));
                break;
            case 'l':
                assembly_append(text, "$");
                assembly_unsigned(text, (uint32_t)record->immediate & 65535);
                break;
            case 'h':
                assembly_append(text, "$");
                assembly_unsigned(text, (uint32_t)record->immediate >> 16);
                break;
            case 'j':
                assembly_append(text, "$");
                assembly_unsigned(text, (uint32_t)record->immediate & 255);
                break;
            case 'k':
                assembly_append(text, "$");
                assembly_unsigned(text, ((uint32_t)record->immediate >> 8) & 255);
                break;
            case '4':
                assembly_register(text, (uint32_t)record->immediate >> 4, form->reg);
                break;
            case 'z':
                assembly_append(text, "$");
                assembly_unsigned(text, (uint32_t)record->immediate & 15);
                break;
            case 'c':
                assembly_append(text, "%cl");
                break;
            case 'd':
                assembly_append(text, "%dx");
                break;
            case 's':
                assembly_append(text, "%st");
                break;
            case 'x':
                assembly_append(text, "%xmm0");
                break;
            case 'q':
                assembly_append(text, "(%rcx)");
                break;
            default:
                break;
            }
            if (k == 0 && (record->decorations & BUSTER_X86_DECORATION_MASK))
            {
                assembly_append(text, "{%k");
                assembly_unsigned(text, record->decorations & BUSTER_X86_DECORATION_MASK);
                assembly_append(text, "}");
            }
            if (k == 0 && (record->decorations & BUSTER_X86_DECORATION_ZEROING))
            {
                assembly_append(text, "{z}");
            }
        }
    }
    // A relocated form whose spelling changes its encoding belongs to assembly_escaped_fields in the caller.
    const bool unescaped = otherwise && referenced;
    spelled = spelled && !unescaped;
    if (!spelled)
    {
        *reason = operands == 0 ? "an instruction form without an AT&T spelling" : unescaped ? "a relocation on an instruction GNU as encodes otherwise (MPX's bnd branches, or a shorter form)" :
                                                                                               "a relocation the instruction's operand can't spell";
    }
    assembly_append(text, "\n");
    return spelled;
}

// ---------------------------------------------------------------------------------------------------------------------
// Data

// Sixteen bytes as one line, "\t.byte 0x.., ... 0x..\n" (87 bytes): each byte's two hexadecimal digits by a table
// permute (vpermb of the nibbles into "0123456789abcdef"), and the line's 80 bytes after "\t.byte " gathered from the
// digits and the constant characters by two permutes of two vectors (vpermt2b), each output byte from its place's index.
#define BUSTER_ASSEMBLY_ROW_BYTES 16
#define BUSTER_ASSEMBLY_ROW_TEXT (7 + 5 * BUSTER_ASSEMBLY_ROW_BYTES)

// Place j of the line's 80 bytes after "\t.byte " (5 a byte: '0', 'x', the high digit, the low one and ',', the last
// ',' a '\n') takes the source vector's byte at this index: the high digits are at 0 to 15, the low ones at 16 to 31,
// and '0', 'x', ',' and '\n' at 32 to 35.
static const uint8_t assembly_row_index[80] = {
    32, 33, 0, 16, 34, 32, 33, 1, 17, 34, 32, 33, 2, 18, 34, 32, 33, 3, 19, 34,
    32, 33, 4, 20, 34, 32, 33, 5, 21, 34, 32, 33, 6, 22, 34, 32, 33, 7, 23, 34,
    32, 33, 8, 24, 34, 32, 33, 9, 25, 34, 32, 33, 10, 26, 34, 32, 33, 11, 27, 34,
    32, 33, 12, 28, 34, 32, 33, 13, 29, 34, 32, 33, 14, 30, 34, 32, 33, 15, 31, 35,
};

static void assembly_byte_row(uint8_t* output, const uint8_t* input)
{
    // The characters: the digits at 0 to 15, then '0', 'x', ',' and '\n' at 16 to 19.
    const __m512i characters = _mm512_setr_epi32(0x33323130, 0x37363534, 0x62613938, 0x66656463, 0x0a2c7830, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m128i bytes = _mm_loadu_si128((const __m128i*)input);
    const __m128i high = _mm_and_si128(_mm_srli_epi16(bytes, 4), _mm_set1_epi8(15));
    const __m128i low = _mm_and_si128(bytes, _mm_set1_epi8(15));
    // Each source byte's character: the nibbles' digits, and the four constants (16 to 19) at 32 to 35.
    const __m512i nibbles = _mm512_inserti64x2(_mm512_inserti64x2(_mm512_castsi128_si512(high), low, 1), _mm_setr_epi8(16, 17, 18, 19, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), 2);
    const __m512i source = _mm512_permutexvar_epi8(nibbles, characters);
    const __m512i first = _mm512_permutexvar_epi8(_mm512_loadu_si512(assembly_row_index), source);
    const __m512i second = _mm512_permutexvar_epi8(_mm512_castsi128_si512(_mm_loadu_si128((const __m128i*)(assembly_row_index + 64))), source);
    memcpy(output, "\t.byte ", 7);
    _mm512_storeu_si512(output + 7, first);
    _mm_storeu_si128((__m128i*)(output + 7 + 64), _mm512_castsi512_si128(second));
}

// Bytes `length` at `bytes`: whole rows by the kernel, a last partial row by the same characters, and runs of 16 or
// more zeros as `.zero`.
static void assembly_plain_bytes(AssemblyText* text, const uint8_t* bytes, uint64_t length)
{
    uint64_t at = 0;
    while (!text->full && at < length)
    {
        uint64_t zeros = 0;
        while (at + zeros < length && bytes[at + zeros] == 0)
        {
            zeros += 1;
        }
        if (zeros >= BUSTER_ASSEMBLY_ROW_BYTES)
        {
            assembly_append(text, "\t.zero ");
            assembly_unsigned(text, zeros);
            assembly_append(text, "\n");
            at += zeros;
        }
        else if (length - at >= BUSTER_ASSEMBLY_ROW_BYTES)
        {
            uint8_t* place = arena_push(text->arena, BUSTER_ASSEMBLY_ROW_TEXT, 1);
            text->full = place == 0;
            if (place != 0)
            {
                assembly_byte_row(place, bytes + at);
            }
            at += BUSTER_ASSEMBLY_ROW_BYTES;
        }
        else
        {
            assembly_append(text, "\t.byte ");
            for (uint64_t i = at; i < length; i += 1)
            {
                assembly_append(text, i == at ? "" : ",");
                assembly_byte(text, bytes[i]);
            }
            assembly_append(text, "\n");
            at = length;
        }
    }
}

// A string pool's bytes: each NUL-terminated string as `.string`, a tail without one as `.ascii`, C's escapes for
// quotes, backslashes and bytes outside printable ASCII.
static void assembly_strings(AssemblyText* text, const uint8_t* bytes, uint64_t length)
{
    uint64_t at = 0;
    while (!text->full && at < length)
    {
        uint64_t end = at;
        while (end < length && bytes[end] != 0)
        {
            end += 1;
        }
        const bool terminated = end < length;
        assembly_append(text, terminated ? "\t.string \"" : "\t.ascii \"");
        for (uint64_t i = at; i < end; i += 1)
        {
            const uint8_t c = bytes[i];
            if (c == '"' || c == '\\')
            {
                const char escaped[2] = { '\\', (char)c };
                assembly_bytes(text, escaped, 2);
            }
            else if (c >= 32 && c < 127)
            {
                assembly_bytes(text, &c, 1);
            }
            else
            {
                const char octal[4] = { '\\', (char)('0' + (c >> 6)), (char)('0' + ((c >> 3) & 7)), (char)('0' + (c & 7)) };
                assembly_bytes(text, octal, 4);
            }
        }
        assembly_append(text, "\"\n");
        at = terminated ? end + 1 : end;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The unit

// An atom's section, as the object writer gives it one: its descriptor's (a named section), or its class's.
typedef struct AssemblySection AssemblySection;
struct AssemblySection
{
    const char* name;
    uint64_t name_length;
    uint64_t flags;
    uint64_t entry_size;
    uint32_t type;
    AtomId link;
    uint32_t identity;
    uint32_t group;
    uint8_t class;
};

static AssemblySection assembly_section(const AssemblyView* view, AtomId atom)
{
    const uint8_t class = view->link->atoms.class[atom];
    const ObjectAtomSection* descriptor = view->sections != 0 && atom < view->section_count ? &view->sections[atom] : 0;
    AssemblySection result = {
        .name = object_class_sections[class].name,
        .name_length = object_class_sections[class].name_length,
        .flags = object_class_sections[class].flags,
        .entry_size = object_class_sections[class].entry_size,
        .type = object_class_sections[class].type,
    };
    if (descriptor != 0 && descriptor->name.length != 0)
    {
        result = (AssemblySection){
            .name = descriptor->name.pointer,
            .name_length = descriptor->name.length,
            .flags = descriptor->flags,
            .entry_size = descriptor->entry_size,
            .type = descriptor->type,
            .link = descriptor->link,
            .identity = descriptor->identity,
        };
    }
    result.class = class;
    const LinkGroups groups = view->link->groups;
    result.group = groups.count != 0 && atom < groups.atom_count ? groups.atom_groups[atom] : 0;
    result.flags |= result.group != 0 ? SHF_GROUP : 0;
    return result;
}

static bool assembly_same_section(AssemblySection a, AssemblySection b)
{
    return a.name_length == b.name_length && memcmp(a.name, b.name, a.name_length) == 0 && a.flags == b.flags && a.entry_size == b.entry_size &&
           a.type == b.type && a.link == b.link && a.identity == b.identity && a.group == b.group && a.class == b.class;
}

// A stable GNU unique ID for a descriptor the assembler must not merge with another of the same name. The first
// atom of an equal descriptor owns the ID, so revisiting it joins its section, as the object writer does.
static uint32_t assembly_section_identity(const AssemblyView* view, AssemblySection section, AtomId atom, AtomId* slots, uint64_t mask)
{
    const uint64_t hash = link_hash((const uint8_t*)section.name, section.name_length) ^ section.flags ^ section.entry_size ^
                          ((uint64_t)section.type << 32) ^ ((uint64_t)section.link * UINT64_C(0xc2b2ae3d27d4eb4f)) ^
                          ((uint64_t)section.identity * UINT64_C(0x165667b19e3779f9)) ^ ((uint64_t)section.group * UINT64_C(0x9e3779b97f4a7c15)) ^
                          ((uint64_t)section.class * UINT64_C(0x27d4eb2f165667c5));
    uint64_t slot = hash & mask;
    while (slots[slot] != 0 && !assembly_same_section(section, assembly_section(view, slots[slot])))
    {
        slot = (slot + 1) & mask;
    }
    if (slots[slot] == 0)
    {
        slots[slot] = atom;
    }
    return slots[slot];
}

// `.section name,"flags",@type[,entsize][,label][,signature[,comdat]][,unique,id]`.
static bool assembly_section_directive(AssemblyText* text, AssemblySection section, SymbolId label, uint32_t identity, const Link* link, const AssemblyNames* names)
{
    const char* type = section.type == SHT_PROGBITS ? "@progbits" : section.type == SHT_NOBITS ? "@nobits" : section.type == SHT_INIT_ARRAY ? "@init_array" :
                       section.type == SHT_FINI_ARRAY ? "@fini_array" : section.type == SHT_PREINIT_ARRAY ? "@preinit_array" : section.type == SHT_NOTE ? "@note" :
                       section.type == SHT_X86_64_UNWIND ? "@unwind" : 0;
    const uint64_t known = SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_MERGE | SHF_STRINGS | SHF_TLS | SHF_GNU_RETAIN | SHF_LINK_ORDER | SHF_GROUP;
    const bool spelled = type != 0 && (section.flags & ~known) == 0 &&
                         ((section.flags & SHF_GROUP) != 0) == (section.group != 0) &&
                         (section.link == 0 || (section.type == SHT_PROGBITS && (section.flags & ~(SHF_GROUP | SHF_GNU_RETAIN)) == (SHF_ALLOC | SHF_WRITE | SHF_LINK_ORDER) && section.entry_size == 0));
    if (spelled)
    {
        assembly_append(text, "\t.section ");
        assembly_identifier(text, (const uint8_t*)section.name, section.name_length, true);
        assembly_append(text, ",\"");
        assembly_append(text, (section.flags & SHF_ALLOC) ? "a" : "");
        assembly_append(text, (section.flags & SHF_WRITE) ? "w" : "");
        assembly_append(text, (section.flags & SHF_EXECINSTR) ? "x" : "");
        assembly_append(text, (section.flags & SHF_MERGE) ? "M" : "");
        assembly_append(text, (section.flags & SHF_STRINGS) ? "S" : "");
        assembly_append(text, (section.flags & SHF_TLS) ? "T" : "");
        assembly_append(text, (section.flags & SHF_GNU_RETAIN) ? "R" : "");
        assembly_append(text, (section.flags & SHF_LINK_ORDER) ? "o" : "");
        assembly_append(text, (section.flags & SHF_GROUP) ? "G" : "");
        assembly_append(text, "\",");
        assembly_append(text, type);
        if (section.flags & SHF_MERGE)
        {
            assembly_append(text, ",");
            assembly_unsigned(text, section.entry_size);
        }
        if (label != 0)
        {
            assembly_append(text, ",");
            assembly_name(text, link, names, label);
        }
        if (section.group != 0)
        {
            const LinkGroup group = link->groups.data[section.group];
            assembly_append(text, ",");
            assembly_identifier(text, link->names.bytes[group.name], link->names.length[group.name], true);
            assembly_append(text, (group.flags & BUSTER_LINK_GROUP_COMDAT) != 0 ? ",comdat" : identity != 0 ? "," : "");
        }
        if (identity != 0)
        {
            assembly_append(text, ",unique,");
            assembly_unsigned(text, identity);
        }
        assembly_append(text, "\n");
    }
    return spelled;
}

// A symbol's binding, visibility and type, before its label (a definition) or anywhere (a symbol the unit refers to
// without defining it: its weak binding and visibility, which GNU as's undefined symbol keeps, as the object writer's
// does).
static void assembly_attributes(AssemblyText* text, const Link* link, const AssemblyNames* names, SymbolId symbol, bool defined)
{
    static const char* const visibilities[] = { 0, "\t.internal ", "\t.hidden ", "\t.protected " };
    const uint8_t flags = link->symbols.flags[symbol];
    const uint8_t binding = flags & BUSTER_LINK_SYMBOL_BINDING;
    const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
    const AtomId atom = link->symbols.atom[symbol];
    const bool tls = type == BUSTER_LINK_SYMBOL_TLS || (atom != 0 && atom < link->atoms.count && link_class_is_tls(link->atoms.class[atom]));
    const bool unique = binding == BUSTER_LINK_SYMBOL_UNIQUE;
    const char* visibility = visibilities[flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT];
    const char* binding_directive = binding == BUSTER_LINK_SYMBOL_WEAK ? "\t.weak " : binding == BUSTER_LINK_SYMBOL_GLOBAL && defined ? "\t.globl " : 0;
    // GNU's unique type supplies both the binding and object type. A TLS section promotes it to TLS; a second .type
    // would clear the unique binding. Ordinary undefined TLS references also need their explicit TLS type.
    const char* type_name = unique ? "@gnu_unique_object" : tls ? "@tls_object" : type == BUSTER_LINK_SYMBOL_FUNCTION ? "@function" :
                            type == BUSTER_LINK_SYMBOL_OBJECT ? "@object" : type == BUSTER_LINK_SYMBOL_IFUNC ? "@gnu_indirect_function" : 0;
    if (binding_directive != 0)
    {
        assembly_append(text, binding_directive);
        assembly_name(text, link, names, symbol);
        assembly_append(text, "\n");
    }
    if (visibility != 0)
    {
        assembly_append(text, visibility);
        assembly_name(text, link, names, symbol);
        assembly_append(text, "\n");
    }
    if (type_name != 0 && (defined || tls))
    {
        assembly_append(text, "\t.type ");
        assembly_name(text, link, names, symbol);
        assembly_append(text, ", ");
        assembly_append(text, type_name);
        assembly_append(text, "\n");
    }
}

// The labels of the atom's symbols at `offset` (`defined[*cursor]` onward, by value), each after its attributes.
static void assembly_labels(AssemblyText* text, const Link* link, const AssemblyNames* names, const SymbolId* defined, uint32_t end, uint32_t* cursor, uint64_t offset)
{
    while (*cursor < end && link->symbols.value[defined[*cursor]] == offset)
    {
        const SymbolId symbol = defined[*cursor];
        assembly_attributes(text, link, names, symbol, true);
        assembly_name(text, link, names, symbol);
        assembly_append(text, ":\n");
        if (names->used != 0 && (names->used[symbol] & (4 | 8)) != 0)
        {
            assembly_append(text, BUSTER_ASSEMBLY_PREFIX "l");
            assembly_unsigned(text, symbol);
            assembly_append(text, ":\n");
        }
        *cursor += 1;
    }
}

// The `.size` of the atom's named symbols, after its contents.
static void assembly_sizes(AssemblyText* text, const Link* link, const AssemblyNames* names, const SymbolId* defined, uint32_t begin, uint32_t end)
{
    for (uint32_t i = begin; i < end; i += 1)
    {
        const SymbolId symbol = defined[i];
        const uint8_t type = link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE;
        if (type == BUSTER_LINK_SYMBOL_FUNCTION || type == BUSTER_LINK_SYMBOL_OBJECT || type == BUSTER_LINK_SYMBOL_IFUNC || link->symbols.size[symbol] != 0)
        {
            assembly_append(text, "\t.size ");
            assembly_name(text, link, names, symbol);
            assembly_append(text, ", ");
            assembly_unsigned(text, link->symbols.size[symbol]);
            assembly_append(text, "\n");
        }
    }
}

// One data field, including its symbolic relocation. Explicit .reloc keeps relocation kinds that a normal
// .long/.quad expression cannot select; its addend is already relative to the field, without instruction bias.
static AssemblyError assembly_data_fixup(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, FixupId fixup, const char** reason)
{
    const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
    const uint32_t offset = link->fixups.offset[fixup];
    const uint64_t size = link->atoms.size[atom];
    const uint8_t* bytes = link->atoms.bytes[atom];
    const LinkFixupKind* description = &link_fixup_kinds[kind];
    const uint32_t width = description->width;
    const int64_t addend = link_fixup_addend(link, fixup);
    const SymbolId target = link->fixups.target[fixup];
    const bool tls_offset = kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_DTPOFF32 ||
                            kind == BUSTER_LINK_FIXUP_TPOFF64 || kind == BUSTER_LINK_FIXUP_DTPOFF64;
    const bool expression = kind == BUSTER_LINK_FIXUP_ABS64 || kind == BUSTER_LINK_FIXUP_ABS32 ||
                            kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64 || tls_offset;
    bool zero = bytes != NULL && (uint64_t)offset + width <= size && (width == 4 || width == 8);
    for (uint32_t i = 0; zero && i < width; i += 1)
    {
        zero = bytes[offset + i] == 0;
    }
    const AssemblyError error = zero ? ASSEMBLY_OK : ASSEMBLY_INVALID;
    if (!zero)
    {
        *reason = "a data fixup's field that isn't zero";
    }
    else
    {
        if (!expression)
        {
            assembly_append(text, "\t.reloc ., ");
            assembly_append(text, object_relocation_names[kind]);
            assembly_append(text, ", ");
        }
        else
        {
            assembly_append(text, width == 8 ? "\t.quad " : "\t.long ");
        }
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME && kind == BUSTER_LINK_FIXUP_PC32 && names->used != NULL && (names->used[target] & 4) != 0)
        {
            assembly_append(text, BUSTER_ASSEMBLY_PREFIX "l");
            assembly_unsigned(text, target);
        }
        else
        {
            assembly_name(text, link, names, target);
        }
        const char* modifier = kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_TPOFF64 ? "@TPOFF" :
                               kind == BUSTER_LINK_FIXUP_DTPOFF32 || kind == BUSTER_LINK_FIXUP_DTPOFF64 ? "@DTPOFF" : "";
        assembly_append(text, expression ? modifier : "");
        assembly_addend(text, addend);
        assembly_append(text, expression && description->pc_relative ? "-.\n" : "\n");
        if (!expression)
        {
            assembly_append(text, width == 8 ? "\t.quad 0\n" : "\t.long 0\n");
        }
    }
    return error;
}

// A data atom's contents: its bytes (zero-fill's `.zero`), with each symbol's label at its offset and each fixup's
// directive at its field (whose bytes, zero in the object, it prints in their place). Returns the error, with `reason`.
static AssemblyError assembly_data(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, const SymbolId* defined, uint32_t begin, uint32_t end,
                                   const char** reason)
{
    const uint8_t class = link->atoms.class[atom];
    const uint8_t* bytes = link->atoms.bytes[atom];
    const uint64_t size = link->atoms.size[atom];
    const uint32_t fixup_count = link->atoms.fixup_count[atom];
    const FixupId fixup_begin = link->atoms.fixup_begin[atom];
    const bool zero_fill = object_class_is_zero_fill(class) || bytes == 0;
    uint32_t cursor = begin;
    uint32_t next_fixup = 0;
    uint64_t at = 0;
    AssemblyError error = zero_fill && fixup_count != 0 ? ASSEMBLY_INVALID : ASSEMBLY_OK;
    *reason = error != ASSEMBLY_OK ? "a fixup in zero-fill data" : *reason;
    while (error == ASSEMBLY_OK && !text->full && at <= size)
    {
        assembly_labels(text, link, names, defined, end, &cursor, at);
        const uint64_t symbol_at = cursor < end ? link->symbols.value[defined[cursor]] : size;
        const FixupId fixup = next_fixup < fixup_count ? fixup_begin + next_fixup : 0;
        const uint64_t fixup_at = fixup != 0 ? link->fixups.offset[fixup] : size;
        const uint64_t stop = symbol_at < fixup_at ? symbol_at : fixup_at;
        if (stop < at)
        {
            error = ASSEMBLY_INVALID;
            *reason = "a fixup or a symbol inside another fixup's field";
        }
        else if (stop > at)
        {
            if (zero_fill)
            {
                assembly_append(text, "\t.zero ");
                assembly_unsigned(text, stop - at);
                assembly_append(text, "\n");
            }
            else if (class == BUSTER_LINK_CLASS_STRINGS)
            {
                assembly_strings(text, bytes + at, stop - at);
            }
            else
            {
                assembly_plain_bytes(text, bytes + at, stop - at);
            }
            at = stop;
        }
        else if (fixup != 0 && fixup_at == at)
        {
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            error = assembly_data_fixup(text, link, names, atom, fixup, reason);
            if (error == ASSEMBLY_OK)
            {
                at += link_fixup_kinds[kind].width;
                next_fixup += 1;
            }
        }
        else
        {
            // The end, with its labels printed: every symbol of the atom's must have been.
            error = cursor == end ? ASSEMBLY_OK : ASSEMBLY_INVALID;
            *reason = error != ASSEMBLY_OK ? "a symbol past its atom's end" : *reason;
            at = size + 1;
        }
    }
    return error;
}

static void assembly_inline_text(AssemblyText* text, const AssemblyInline* piece)
{
    assembly_append(text, "#APP\n");
    assembly_bytes(text, piece->text.pointer, piece->text.length);
    assembly_append(text, "\n#NO_APP\n");
}

// .cfi_escape retains a row whose 64-bit offset exceeds the assembler's named directives' signed 32-bit operands.
// Every byte is part of one frame instruction; its location is still the symbolic boundary printed by the caller.
static void assembly_frame_leb(AssemblyText* text, uint64_t bits, bool sign)
{
    bool more = true;
    while (more)
    {
        uint8_t byte = (uint8_t)(bits & 0x7f);
        bits = sign ? (uint64_t)((int64_t)bits >> 7) : bits >> 7;
        more = sign ? !((bits == 0 && (byte & 0x40) == 0) || (bits == UINT64_MAX && (byte & 0x40) != 0)) : bits != 0;
        byte |= more ? 0x80 : 0;
        assembly_append(text, ", ");
        assembly_byte(text, byte);
    }
}

// CFI has the same symbolic instruction boundaries as the generated code. The object writer's CIE explicitly keeps
// the six callee-saved registers in themselves; spell that baseline in each FDE and restore its rules explicitly,
// because GNU as's standard CIE supplies only the CFA and return-address rules.
static bool assembly_frame_at(AssemblyText* text, const AssemblyCode* code, uint64_t at, uint32_t* cursor, const char** reason)
{
    bool valid = !code->frame_enabled || code->frame_count == 0 || code->frame_rows != NULL;
    if (code->frame_enabled)
    {
        const uint64_t start = code->frame_offset;
        const uint64_t end = start + code->frame_size;
        if (valid && at == start)
        {
            assembly_append(text, "\t.cfi_startproc\n\t.cfi_same_value 3\n\t.cfi_same_value 6\n\t.cfi_same_value 12\n\t.cfi_same_value 13\n\t.cfi_same_value 14\n\t.cfi_same_value 15\n");
        }
        while (valid && *cursor < code->frame_count && (uint64_t)code->frame_rows[*cursor].offset + start <= at)
        {
            const EmissionFrameRow row = code->frame_rows[*cursor];
            valid = (uint64_t)row.offset + start == at && row.offset < code->frame_size;
            if (valid)
            {
                const bool same = row.reg == 3 || row.reg == 6 || (row.reg >= 12 && row.reg <= 15);
                const char* directive = row.kind == EMISSION_FRAME_CFA ? "\t.cfi_def_cfa " :
                                        row.kind == EMISSION_FRAME_CFA_OFFSET ? "\t.cfi_def_cfa_offset " :
                                        row.kind == EMISSION_FRAME_OFFSET ? "\t.cfi_offset " :
                                        row.kind == EMISSION_FRAME_REGISTER ? "\t.cfi_register " :
                                        row.kind == EMISSION_FRAME_RESTORE ? (same ? "\t.cfi_same_value " : "\t.cfi_undefined ") :
                                        row.kind == EMISSION_FRAME_SAME_VALUE ? "\t.cfi_same_value " :
                                        row.kind == EMISSION_FRAME_UNDEFINED ? "\t.cfi_undefined " :
                                        row.kind == EMISSION_FRAME_REMEMBER_STATE ? "\t.cfi_remember_state" :
                                        row.kind == EMISSION_FRAME_RESTORE_STATE ? "\t.cfi_restore_state" : NULL;
                valid = directive != NULL;
                if (valid)
                {
                    const bool wide = (row.value < INT32_MIN || row.value > INT32_MAX) &&
                                      (row.kind == EMISSION_FRAME_CFA || row.kind == EMISSION_FRAME_CFA_OFFSET || row.kind == EMISSION_FRAME_OFFSET);
                    if (wide)
                    {
                        assembly_append(text, "\t.cfi_escape ");
                        assembly_byte(text, row.kind == EMISSION_FRAME_CFA ? 0x0c : row.kind == EMISSION_FRAME_CFA_OFFSET ? 0x0e : 0x11);
                        if (row.kind != EMISSION_FRAME_CFA_OFFSET)
                        {
                            assembly_frame_leb(text, row.reg, false);
                        }
                        const int64_t value = row.kind == EMISSION_FRAME_OFFSET ? -(row.value / 8) : row.value;
                        assembly_frame_leb(text, (uint64_t)value, row.kind == EMISSION_FRAME_OFFSET);
                    }
                    else
                    {
                        assembly_append(text, directive);
                    }
                    const bool operand = row.kind < EMISSION_FRAME_REMEMBER_STATE;
                    const bool pair = row.kind == EMISSION_FRAME_CFA || row.kind == EMISSION_FRAME_OFFSET || row.kind == EMISSION_FRAME_REGISTER;
                    if (!wide && operand && row.kind != EMISSION_FRAME_CFA_OFFSET)
                    {
                        assembly_unsigned(text, row.reg);
                    }
                    if (!wide && pair)
                    {
                        assembly_append(text, ", ");
                    }
                    if (!wide && (pair || row.kind == EMISSION_FRAME_CFA_OFFSET))
                    {
                        assembly_signed(text, row.value);
                    }
                    assembly_append(text, "\n");
                    *cursor += 1;
                }
            }
        }
        valid = valid && (at != end || *cursor == code->frame_count);
        if (valid && at == end)
        {
            assembly_append(text, "\t.cfi_endproc\n");
        }
        *reason = valid ? *reason : "an unwind row outside an instruction boundary";
    }
    return valid;
}

// Byte spans outside an instruction (patchable entry, inline helper wrappers, sparse assembly fragments) can contain
// frame boundaries too. The caller prints the directives at `begin`; this splits only at boundaries strictly inside.
static uint64_t assembly_frame_next(const AssemblyCode* code, uint64_t begin, uint64_t end, uint32_t cursor)
{
    uint64_t next = end;
    if (code->frame_enabled)
    {
        const uint64_t start = code->frame_offset;
        const uint64_t finish = start + code->frame_size;
        next = start > begin && start < next ? start : next;
        next = finish > begin && finish < next ? finish : next;
        if (cursor < code->frame_count)
        {
            const uint64_t row = start + code->frame_rows[cursor].offset;
            next = row > begin && row < next ? row : next;
        }
    }
    return next;
}

static bool assembly_frame_valid(const AssemblyCode* code, uint64_t size)
{
    bool valid = !code->frame_enabled || ((uint64_t)code->frame_offset + code->frame_size <= size && (code->frame_count == 0 || code->frame_rows != NULL));
    uint32_t previous = 0;
    for (uint32_t r = 0; valid && code->frame_enabled && r < code->frame_count; r += 1)
    {
        const EmissionFrameRow row = code->frame_rows[r];
        valid = row.offset >= previous && row.offset < code->frame_size && row.kind <= EMISSION_FRAME_RESTORE_STATE && row.reserved == 0;
        previous = row.offset;
    }
    return valid;
}

// Only the compiler-generated table can be replaced. Prove that it is an otherwise unnamed unwind atom and that
// every relocated FDE range belongs to exactly one published frame, with no uncovered frame or extra target.
static bool assembly_frame_table_valid(const AssemblyView* view, const uint32_t* defined_begin, uint32_t* covered)
{
    const Link* link = view->link;
    const AtomId atom = view->cfi_atom;
    bool valid = atom < link->atoms.count && link->atoms.unit[atom] == view->unit && link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME &&
                 link->atoms.bytes[atom] != NULL && link->atoms.fixup_count[atom] != 0 && defined_begin[atom] == defined_begin[atom + 1] &&
                 (atom >= link->groups.atom_count || link->groups.atom_groups[atom] == 0);
    memset(covered, 0, (uint64_t)link->atoms.count * sizeof(*covered));
    const FixupId begin = valid ? link->atoms.fixup_begin[atom] : 0;
    const uint32_t count = valid ? link->atoms.fixup_count[atom] : 0;
    for (uint32_t f = 0; valid && f < count; f += 1)
    {
        const FixupId fixup = begin + f;
        const uint64_t offset = link->fixups.offset[fixup];
        const SymbolId symbol = link->fixups.target[fixup];
        const AtomId target = symbol != 0 && symbol < link->symbols.count ? link->symbols.atom[symbol] : 0;
        const AssemblyCode* code = view->code != NULL && target < view->code_count ? &view->code[target] : NULL;
        valid = (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_PC32 && link_fixup_addend(link, fixup) == 0 &&
                offset <= link->atoms.size[atom] && 8 <= link->atoms.size[atom] - offset && target != 0 && target < link->atoms.count &&
                link->atoms.unit[target] == view->unit && link->atoms.class[target] == BUSTER_LINK_CLASS_TEXT && code != NULL && code->frame_enabled &&
                code->frame_offset == link->symbols.value[symbol] && covered[target] == 0;
        if (valid)
        {
            uint32_t range = 0;
            memcpy(&range, link->atoms.bytes[atom] + offset + 4, sizeof(range));
            valid = range == code->frame_size;
            covered[target] = 1;
        }
    }
    for (AtomId a = 1; valid && a < link->atoms.count; a += 1)
    {
        if (link->atoms.unit[a] == view->unit && view->code != NULL && a < view->code_count && view->code[a].frame_enabled)
        {
            valid = covered[a] != 0;
        }
    }
    return valid;
}

// A standalone string directive uses the same helper and byte range as direct object emission.
static AssemblyError assembly_inline_helper(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, const AssemblyCode* code,
                                            const SymbolId* defined, uint32_t begin, uint32_t end, const char** reason)
{
    const AssemblyInline* piece = code->templates;
    const uint64_t size = link->atoms.size[atom];
    const uint64_t tail = (uint64_t)piece->offset + piece->size;
    uint32_t cursor = begin;
    assembly_labels(text, link, names, defined, end, &cursor, 0);
    bool valid = tail <= size && link->atoms.fixup_count[atom] == 0 && (cursor == end || link->symbols.value[defined[cursor]] == size);
    if (valid)
    {
        const uint8_t* bytes = link->atoms.bytes[atom];
        uint32_t frame = 0;
        for (uint64_t at = 0; valid && at < piece->offset;)
        {
            valid = assembly_frame_at(text, code, at, &frame, reason);
            const uint64_t next = assembly_frame_next(code, at, piece->offset, frame);
            assembly_plain_bytes(text, bytes + at, next - at);
            at = next;
        }
        valid = valid && assembly_frame_at(text, code, piece->offset, &frame, reason);
        if (valid)
        {
            assembly_inline_text(text, piece);
        }
        for (uint64_t at = tail; valid && at < size;)
        {
            valid = (at == piece->offset && piece->size == 0) || assembly_frame_at(text, code, at, &frame, reason);
            const uint64_t next = assembly_frame_next(code, at, size, frame);
            assembly_plain_bytes(text, bytes + at, next - at);
            at = next;
        }
        assembly_labels(text, link, names, defined, end, &cursor, size);
        valid = valid && (size == piece->offset ? true : assembly_frame_at(text, code, size, &frame, reason));
    }
    else
    {
        *reason = "an inconsistent inline assembly range";
    }
    return valid ? ASSEMBLY_OK : ASSEMBLY_INVALID;
}

static int assembly_offset_order(const void* left, const void* right)
{
    const uint32_t a = *(const uint32_t*)left;
    const uint32_t b = *(const uint32_t*)right;
    return (a > b) - (a < b);
}

// A retained instruction whose spelling changes its encoding or needs an overflowing source addend: keep its bytes
// around each symbolic field. The fixups remain the Link's, including their field-relative addends.
static AssemblyError assembly_escaped_fields(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom,
                                            const X86FormDescription* form, uint64_t at, uint64_t finish, FixupId begin, FixupId end,
                                            const char** reason)
{
    const uint8_t* bytes = link->atoms.bytes[atom];
    AssemblyError error = ASSEMBLY_OK;
    assembly_append(text, "\t# ");
    assembly_append(text, form->mnemonic);
    assembly_append(text, " (selected bytes with symbolic fields)\n");
    for (FixupId fixup = begin; error == ASSEMBLY_OK && !text->full && fixup < end; fixup += 1)
    {
        const uint64_t offset = link->fixups.offset[fixup];
        const uint32_t width = link_fixup_kinds[link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND].width;
        if (offset < at || offset + width > finish)
        {
            error = ASSEMBLY_INVALID;
            *reason = "overlapping or out-of-range fields in an escaped instruction";
        }
        else
        {
            assembly_plain_bytes(text, bytes + at, offset - at);
            error = assembly_data_fixup(text, link, names, atom, fixup, reason);
            at = offset + width;
        }
    }
    if (error == ASSEMBLY_OK)
    {
        assembly_plain_bytes(text, bytes + at, finish - at);
    }
    return error;
}

// Full assembly units retain their selected instruction records at byte offsets, with directives' bytes between
// them. Keep that fixed layout: labels, data fixups, CFI and original marker ranges share the same byte boundaries.
static AssemblyError assembly_mixed_code(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, const AssemblyCode* code,
                                         const SymbolId* defined, uint32_t begin, uint32_t end, Arena* scratch, const char** reason)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = code->count;
    const uint64_t size = link->atoms.size[atom];
    const uint8_t* bytes = link->atoms.bytes[atom];
    uint32_t* encoded_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* targets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    uint8_t* encoded = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, BUSTER_X86_ENCODER_CAPACITY(count));
    AssemblyError error = encoded_offsets != NULL && targets != NULL && encoded != NULL ? ASSEMBLY_OK : ASSEMBLY_FULL;
    const bool shape = code->offsets != NULL && (count == 0 || code->records != NULL) && (size == 0 || bytes != NULL) && size <= UINT32_MAX;
    const int64_t length = error == ASSEMBLY_OK && shape ? x86_encode(code->records, count, encoded, BUSTER_X86_ENCODER_CAPACITY(count), encoded_offsets, NULL) : -1;
    if (error == ASSEMBLY_OK && (length < 0 || (uint64_t)length > UINT32_MAX))
    {
        error = ASSEMBLY_INVALID;
        *reason = "invalid mixed instruction records";
    }
    uint32_t target_count = 0;
    const FixupId fixup_begin = link->atoms.fixup_begin[atom];
    const uint32_t fixup_count = link->atoms.fixup_count[atom];
    uint32_t branch_fixup = 0;
    uint64_t previous = 0;
    if (error == ASSEMBLY_OK)
    {
        encoded_offsets[count] = (uint32_t)length;
    }
    for (uint32_t r = 0; error == ASSEMBLY_OK && r < count; r += 1)
    {
        const uint32_t start = code->offsets[r];
        const uint32_t width = encoded_offsets[r + 1] - encoded_offsets[r];
        const uint64_t finish = (uint64_t)start + width;
        if (start < previous || finish > size || width == 0)
        {
            error = ASSEMBLY_INVALID;
            *reason = "overlapping or out-of-range mixed instruction records";
        }
        else
        {
            const X86Instruction* record = &code->records[r];
            const int64_t target = (int64_t)finish + record->displacement;
            while (branch_fixup < fixup_count && link->fixups.offset[fixup_begin + branch_fixup] < start)
            {
                branch_fixup += 1;
            }
            const bool symbolic = branch_fixup < fixup_count && link->fixups.offset[fixup_begin + branch_fixup] < finish;
            if (!symbolic && x86_form_description(record->form)->syntax == X86_SYNTAX_REL && target >= 0 && (uint64_t)target <= size)
            {
                targets[target_count++] = (uint32_t)target;
            }
            previous = finish;
        }
    }
    if (error == ASSEMBLY_OK && target_count > 1)
    {
        qsort(targets, target_count, sizeof(uint32_t), assembly_offset_order);
    }
    uint32_t next_fixup = 0;
    uint32_t next_record = 0;
    uint32_t next_target = 0;
    uint32_t cursor = begin;
    uint32_t frame_cursor = 0;
    uint32_t piece = 0;
    uint64_t at = 0;
    while (error == ASSEMBLY_OK && !text->full && at <= size)
    {
        const uint64_t symbol_at = cursor < end ? link->symbols.value[defined[cursor]] : size;
        const uint64_t target_at = next_target < target_count ? targets[next_target] : size;
        const uint64_t record_at = next_record < count ? code->offsets[next_record] : size;
        const uint64_t fixup_at = next_fixup < fixup_count ? link->fixups.offset[fixup_begin + next_fixup] : size;
        const uint64_t piece_at = piece < code->template_count ? code->templates[piece].offset : size;
        if (symbol_at < at || target_at < at || record_at < at || fixup_at < at || piece_at < at)
        {
            error = ASSEMBLY_INVALID;
            *reason = "a mixed-code label, record or fixup inside another field";
            break;
        }
        assembly_labels(text, link, names, defined, end, &cursor, at);
        if (next_target < target_count && targets[next_target] == at)
        {
            assembly_append(text, BUSTER_ASSEMBLY_PREFIX);
            assembly_unsigned(text, atom);
            assembly_append(text, ".");
            assembly_unsigned(text, at);
            assembly_append(text, ":\n");
            while (next_target < target_count && targets[next_target] == at)
            {
                next_target += 1;
            }
        }
        if (!assembly_frame_at(text, code, at, &frame_cursor, reason))
        {
            error = ASSEMBLY_INVALID;
            break;
        }
        bool placed = false;
        while (piece < code->template_count && code->templates[piece].offset == at)
        {
            const AssemblyInline* source = &code->templates[piece++];
            const uint64_t finish = at + source->size;
            if (finish > size || (source->size != 0 && ((next_record < count && record_at < finish) || (next_fixup < fixup_count && fixup_at < finish) ||
                (cursor < end && link->symbols.value[defined[cursor]] < finish) || (next_target < target_count && targets[next_target] < finish) ||
                assembly_frame_next(code, at, finish, frame_cursor) != finish)))
            {
                error = ASSEMBLY_INVALID;
                *reason = "an inconsistent inline assembly range in mixed code";
                break;
            }
            assembly_inline_text(text, source);
            if (source->size != 0)
            {
                at = finish;
                placed = true;
                break;
            }
        }
        if (error != ASSEMBLY_OK)
        {
            break;
        }
        if (placed)
        {
            continue;
        }
        if (at == size)
        {
            error = cursor == end && next_record == count && next_fixup == fixup_count && piece == code->template_count && frame_cursor == code->frame_count ?
                    ASSEMBLY_OK : ASSEMBLY_INVALID;
            *reason = error == ASSEMBLY_OK ? *reason : "mixed-code metadata past its atom's end";
            break;
        }
        if (next_record < count && record_at == at)
        {
            const uint32_t r = next_record++;
            const X86Instruction* record = &code->records[r];
            const uint32_t width = encoded_offsets[r + 1] - encoded_offsets[r];
            const uint32_t finish = (uint32_t)at + width;
            const uint32_t first_fixup = next_fixup;
            uint8_t* instruction_bytes = encoded + encoded_offsets[r];
            AssemblyReference displacement = { 0 };
            AssemblyReference immediate = { 0 };
            uint64_t field_end = at;
            bool escaped_addend = false;
            if (assembly_frame_next(code, at, finish, frame_cursor) != finish)
            {
                error = ASSEMBLY_INVALID;
                *reason = "an unwind boundary inside a mixed instruction";
            }
            else if ((cursor < end && link->symbols.value[defined[cursor]] < finish) ||
                     (next_target < target_count && targets[next_target] < finish) ||
                     (piece < code->template_count && code->templates[piece].offset < finish))
            {
                error = ASSEMBLY_INVALID;
                *reason = "a label or inline boundary inside a mixed instruction";
            }
            while (error == ASSEMBLY_OK && next_fixup < fixup_count && link->fixups.offset[fixup_begin + next_fixup] < finish)
            {
                const FixupId fixup = fixup_begin + next_fixup;
                const uint32_t offset = link->fixups.offset[fixup];
                const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
                const uint32_t field_width = link_fixup_kinds[kind].width;
                const uint32_t distance = finish - offset;
                const bool on_displacement = distance == x86_fixup_distance(record->form, X86_FIXUP_DISPLACEMENT) && !displacement.present;
                const bool on_immediate = distance == x86_fixup_distance(record->form, X86_FIXUP_IMMEDIATE) && !immediate.present;
                AssemblyReference* reference = on_displacement ? &displacement : on_immediate ? &immediate : NULL;
                if (offset < field_end || (uint64_t)offset + field_width > finish || reference == NULL)
                {
                    error = ASSEMBLY_INVALID;
                    *reason = "a fixup off its mixed instruction's fields";
                }
                else
                {
                    const int64_t addend = link_fixup_addend(link, fixup);
                    int64_t source_addend = addend;
                    const bool overflow = link_fixup_kinds[kind].pc_relative && __builtin_add_overflow(addend, (int64_t)distance, &source_addend);
                    escaped_addend = escaped_addend || overflow;
                    *reference = (AssemblyReference){
                        .addend = overflow ? addend : source_addend,
                        .target = link->fixups.target[fixup],
                        .kind = kind,
                        .present = true,
                    };
                    memset(instruction_bytes + offset - at, 0, field_width);
                    field_end = (uint64_t)offset + field_width;
                    next_fixup += 1;
                }
            }
            if (error == ASSEMBLY_OK && memcmp(instruction_bytes, bytes + at, width) != 0)
            {
                error = ASSEMBLY_INVALID;
                *reason = "mixed instruction records that don't encode to their atom's bytes";
            }
            if (error == ASSEMBLY_OK)
            {
                const X86FormDescription* form = x86_form_description(record->form);
                const bool branch = form->syntax == X86_SYNTAX_REL;
                const int64_t target = (int64_t)finish + record->displacement;
                if (escaped_addend || (first_fixup != next_fixup && assembly_escaped(record, form, immediate.present)))
                {
                    error = assembly_escaped_fields(text, link, names, atom, form, at, finish,
                                                    fixup_begin + first_fixup, fixup_begin + next_fixup, reason);
                }
                else if (branch && !displacement.present && (target < 0 || (uint64_t)target > size))
                {
                    // A literal relative displacement may intentionally leave the atom. Preserve its selected bytes.
                    assembly_plain_bytes(text, bytes + at, width);
                }
                else if (!assembly_instruction(text, link, names, names->widths, record, instruction_bytes, width, &displacement, &immediate, atom,
                                               (uint32_t)target, reason))
                {
                    error = ASSEMBLY_UNSUPPORTED;
                }
            }
            at = finish;
        }
        else if (next_fixup < fixup_count && fixup_at == at)
        {
            const FixupId fixup = fixup_begin + next_fixup++;
            const uint32_t width = link_fixup_kinds[link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND].width;
            if (assembly_frame_next(code, at, at + width, frame_cursor) != at + width)
            {
                error = ASSEMBLY_INVALID;
                *reason = "an unwind boundary inside a mixed data fixup";
            }
            else
            {
                error = assembly_data_fixup(text, link, names, atom, fixup, reason);
            }
            at += width;
        }
        else
        {
            uint64_t stop = record_at < fixup_at ? record_at : fixup_at;
            const uint64_t next_symbol = cursor < end ? link->symbols.value[defined[cursor]] : size;
            const uint64_t next_label = next_target < target_count ? targets[next_target] : size;
            const uint64_t next_piece = piece < code->template_count ? code->templates[piece].offset : size;
            stop = next_symbol < stop ? next_symbol : stop;
            stop = next_label < stop ? next_label : stop;
            stop = next_piece < stop ? next_piece : stop;
            stop = assembly_frame_next(code, at, stop, frame_cursor);
            assembly_plain_bytes(text, bytes + at, stop - at);
            at = stop;
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

// A TEXT atom's code: its records, each after the labels of its offset (its symbols' and, for a branch's target, the
// sink's own), with the atom's fixups as their records' references, then the labels at its end. The records must encode
// to the atom's bytes (with its fixups' fields zero, as link_add_code_fixups leaves them) and every fixup must be one of
// their fields. Returns the error, with `reason`.
static AssemblyError assembly_code(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, const AssemblyCode* code, const SymbolId* defined,
                                   uint32_t begin, uint32_t end, Arena* scratch, const char** reason)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = code->count;
    const uint32_t sled = code->sled;
    const uint64_t size = link->atoms.size[atom];
    const uint8_t* atom_bytes = link->atoms.bytes[atom];
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* targets = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* labeled = targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* encoded = labeled != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, BUSTER_X86_ENCODER_CAPACITY(count)) : 0;
    AssemblyError error = encoded != 0 ? ASSEMBLY_OK : ASSEMBLY_FULL;
    const int64_t length = encoded != 0 ? x86_encode(code->records, count, encoded, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0) : -1;
    if (error == ASSEMBLY_OK && (length < 0 || (uint64_t)length + sled != size || size > UINT32_MAX || atom_bytes == 0))
    {
        error = ASSEMBLY_INVALID;
        *reason = "instruction records that don't encode to their function's bytes";
    }
    if (error == ASSEMBLY_OK)
    {
        offsets[count] = (uint32_t)length;
        memset(labeled, 0, (uint64_t)count + 1);
        for (uint32_t r = 0; r <= count; r += 1)
        {
            targets[r] = UINT32_MAX;
        }
        for (uint32_t b = 0; error == ASSEMBLY_OK && b < code->branch_count; b += 1)
        {
            const uint32_t branch = code->branches[b];
            const uint32_t target = code->targets[b];
            error = branch < count && target <= count ? ASSEMBLY_OK : ASSEMBLY_INVALID;
            if (error == ASSEMBLY_OK)
            {
                targets[branch] = target;
                labeled[target] = 1;
            }
        }
        *reason = error != ASSEMBLY_OK ? "a branch outside its function's records" : *reason;
    }
    // The fixups' fields are zero in the atom: zero them in the records' bytes too, and compare.
    const uint32_t fixup_count = link->atoms.fixup_count[atom];
    const FixupId fixup_begin = link->atoms.fixup_begin[atom];
    for (uint32_t f = 0; error == ASSEMBLY_OK && f < fixup_count; f += 1)
    {
        const FixupId fixup = fixup_begin + f;
        const uint32_t offset = link->fixups.offset[fixup];
        const uint32_t width = link_fixup_kinds[link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND].width;
        error = offset >= sled && (uint64_t)offset + width <= size ? ASSEMBLY_OK : ASSEMBLY_INVALID;
        if (error == ASSEMBLY_OK)
        {
            memset(encoded + offset - sled, 0, width);
        }
    }
    if (error == ASSEMBLY_OK && memcmp(encoded, atom_bytes + sled, size - sled) != 0)
    {
        error = ASSEMBLY_INVALID;
        *reason = "instruction records that don't encode to their function's bytes";
    }
    uint32_t cursor = begin;
    uint32_t frame = 0;
    // The function symbol can lie inside the sled, while the table refers to its start.
    for (uint32_t at = 0; error == ASSEMBLY_OK && !text->full && at < sled;)
    {
        assembly_labels(text, link, names, defined, end, &cursor, at);
        error = assembly_frame_at(text, code, at, &frame, reason) ? error : ASSEMBLY_INVALID;
        const uint64_t next_label = cursor < end ? link->symbols.value[defined[cursor]] : sled;
        const uint32_t marked = code->endbr != 0 ? code->endbr - 1 : UINT32_MAX;
        const uint32_t until = next_label < sled ? (uint32_t)next_label : sled;
        const uint32_t limit = at < marked && marked < until ? marked : until;
        const uint32_t stop = (uint32_t)assembly_frame_next(code, at, limit, frame);
        if (at == marked)
        {
            if (assembly_frame_next(code, at, at + 4, frame) != at + 4)
            {
                error = ASSEMBLY_INVALID;
                *reason = "an unwind boundary inside an instruction";
            }
            else
            {
                assembly_append(text, "\tendbr64\n");
            }
        }
        else
        {
            assembly_plain_bytes(text, atom_bytes + at, stop - at);
        }
        at = at == marked ? at + 4 : stop;
    }
    uint32_t next_fixup = 0;
    uint32_t piece = 0;
    for (uint32_t r = 0; error == ASSEMBLY_OK && !text->full && r < count; r += 1)
    {
        const uint32_t offset = offsets[r];
        const uint32_t at = offset + sled;
        const uint32_t record_end = offsets[r + 1] + sled;
        if (cursor < end && link->symbols.value[defined[cursor]] < at)
        {
            error = ASSEMBLY_INVALID;
            *reason = "a symbol inside an instruction";
        }
        assembly_labels(text, link, names, defined, end, &cursor, at);
        error = assembly_frame_at(text, code, at, &frame, reason) ? error : ASSEMBLY_INVALID;
        if (error == ASSEMBLY_OK && assembly_frame_next(code, at, record_end, frame) != record_end)
        {
            error = ASSEMBLY_INVALID;
            *reason = "an unwind boundary inside an instruction";
        }
        if (labeled[r] != 0)
        {
            assembly_append(text, BUSTER_ASSEMBLY_PREFIX);
            assembly_unsigned(text, atom);
            assembly_append(text, ".");
            assembly_unsigned(text, r);
            assembly_append(text, ":\n");
        }
        while (piece < code->template_count && code->templates[piece].offset == at)
        {
            assembly_inline_text(text, &code->templates[piece++]);
        }
        // The record's fixups: on its displacement's field, then its immediate's, each at its distance from the end.
        const X86Instruction* record = &code->records[r];
        AssemblyReference displacement = { 0 };
        AssemblyReference immediate = { 0 };
        const uint32_t first_fixup = next_fixup;
        uint64_t field_end = at;
        bool escaped_addend = false;
        if (error == ASSEMBLY_OK && cursor < end && link->symbols.value[defined[cursor]] < record_end)
        {
            error = ASSEMBLY_INVALID;
            *reason = "a symbol inside an instruction";
        }
        else if (error == ASSEMBLY_OK && piece < code->template_count && code->templates[piece].offset < record_end)
        {
            error = ASSEMBLY_INVALID;
            *reason = "an inline assembly marker outside an instruction boundary";
        }
        while (error == ASSEMBLY_OK && next_fixup < fixup_count && link->fixups.offset[fixup_begin + next_fixup] < record_end)
        {
            const FixupId fixup = fixup_begin + next_fixup;
            const uint32_t offset_in = link->fixups.offset[fixup];
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const uint32_t field_width = link_fixup_kinds[kind].width;
            const uint32_t to_end = record_end - offset_in;
            const bool on_displacement = to_end == x86_fixup_distance(record->form, X86_FIXUP_DISPLACEMENT) && !displacement.present;
            const bool on_immediate = to_end == x86_fixup_distance(record->form, X86_FIXUP_IMMEDIATE) && !immediate.present;
            AssemblyReference* reference = on_displacement ? &displacement : on_immediate ? &immediate : 0;
            if (offset_in < field_end || (uint64_t)offset_in + field_width > record_end || reference == 0)
            {
                error = ASSEMBLY_INVALID;
                *reason = "a fixup off its instruction's fields";
            }
            else
            {
                // The source's addend: the field's, plus the distance from the field to the instruction's end for a
                // PC-relative kind, which the assembler takes off again.
                const int64_t addend = link_fixup_addend(link, fixup);
                int64_t source_addend = addend;
                const bool overflow = link_fixup_kinds[kind].pc_relative && __builtin_add_overflow(addend, (int64_t)to_end, &source_addend);
                escaped_addend = escaped_addend || overflow;
                *reference = (AssemblyReference){
                    .addend = overflow ? addend : source_addend,
                    .target = link->fixups.target[fixup],
                    .kind = kind,
                    .present = true,
                };
                field_end = (uint64_t)offset_in + field_width;
                next_fixup += 1;
            }
        }
        if (error == ASSEMBLY_OK)
        {
            const X86FormDescription* form = x86_form_description(record->form);
            const bool branch = form->syntax == X86_SYNTAX_REL;
            if (branch && !displacement.present && targets[r] == UINT32_MAX)
            {
                error = ASSEMBLY_INVALID;
                *reason = "a relative branch without a target";
            }
            else if (escaped_addend || (first_fixup != next_fixup && assembly_escaped(record, form, immediate.present)))
            {
                error = assembly_escaped_fields(text, link, names, atom, form, at, record_end,
                                                fixup_begin + first_fixup, fixup_begin + next_fixup, reason);
            }
            else if (!assembly_instruction(text, link, names, names->widths, record, encoded + offset, record_end - at, &displacement, &immediate, atom, targets[r], reason))
            {
                error = ASSEMBLY_UNSUPPORTED;
            }
        }
    }
    if (error == ASSEMBLY_OK && count < UINT32_MAX && labeled[count] != 0)
    {
        assembly_append(text, BUSTER_ASSEMBLY_PREFIX);
        assembly_unsigned(text, atom);
        assembly_append(text, ".");
        assembly_unsigned(text, count);
        assembly_append(text, ":\n");
    }
    if (error == ASSEMBLY_OK)
    {
        assembly_labels(text, link, names, defined, end, &cursor, size);
        error = assembly_frame_at(text, code, size, &frame, reason) ? error : ASSEMBLY_INVALID;
        while (piece < code->template_count && code->templates[piece].offset == size)
        {
            assembly_inline_text(text, &code->templates[piece++]);
        }
        if (piece != code->template_count)
        {
            error = ASSEMBLY_INVALID;
            *reason = "an inline assembly marker outside an instruction boundary";
        }
        if (cursor != end || next_fixup != fixup_count)
        {
            error = ASSEMBLY_INVALID;
            *reason = cursor != end ? "a symbol inside an instruction" : "a fixup off its instruction's fields";
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

// Emit the bytes up to a relocated DWARF field, then its symbolic value. A definition address uses
// a local label even for a preemptible symbol. The debugger needs the address of this unit's own definition.
static void assembly_debug_field(AssemblyText* text, String8 section, uint64_t* cursor, uint64_t offset, SymbolId symbol, uint32_t target_section, uint64_t addend)
{
    assembly_plain_bytes(text, (const uint8_t*)section.pointer + *cursor, offset - *cursor);
    if (symbol != 0)
    {
        assembly_append(text, "\t.quad " BUSTER_ASSEMBLY_PREFIX "l");
        assembly_unsigned(text, symbol);
        *cursor = offset + 8;
    }
    else
    {
        uint32_t value = 0;
        memcpy(&value, section.pointer + offset, 4);
        addend = value;
        assembly_append(text, "\t.long " BUSTER_ASSEMBLY_PREFIX "d");
        assembly_unsigned(text, target_section);
        *cursor = offset + 4;
    }
    if (addend != 0)
    {
        assembly_append(text, "+");
        assembly_unsigned(text, addend);
    }
    assembly_append(text, "\n");
}

// The same ordered section contributions and relocation columns as object_fill_debug. The three
// .debug_info columns are each sorted by offset. Walk them together so unrelocated bytes are copied once.
static void assembly_debug(AssemblyText* text, const ObjectDebug* debug)
{
    const uint32_t units = debug->unit_count != 0 ? debug->unit_count : 1;
    for (uint32_t s = 0; !text->full && s < BUSTER_OBJECT_DEBUG_SECTIONS; s += 1)
    {
        const String8 section = debug->sections[s];
        if (section.length == 0)
        {
            continue;
        }
        assembly_append(text, "\t.section ");
        assembly_append(text, object_debug_sections[s]);
        assembly_append(text, ",\"\",@progbits\n" BUSTER_ASSEMBLY_PREFIX "d");
        assembly_unsigned(text, s);
        assembly_append(text, ":\n");
        uint64_t cursor = 0;
        if (s == BUSTER_OBJECT_DEBUG_INFO)
        {
            const uint64_t core_count = (uint64_t)units * BUSTER_OBJECT_DEBUG_FIELDS;
            uint64_t core = 0;
            uint32_t field = 0;
            uint32_t address = 0;
            while (!text->full && (core < core_count || field < debug->extra.field_count || address < debug->extra.address_count))
            {
                const uint32_t c = core < core_count ? debug->fields[core] : UINT32_MAX;
                const uint32_t f = field < debug->extra.field_count ? debug->extra.fields[field].offset : UINT32_MAX;
                const uint32_t a = address < debug->extra.address_count ? debug->extra.addresses[address].offset : UINT32_MAX;
                if (c < f && c < a)
                {
                    assembly_debug_field(text, section, &cursor, c, 0, core % BUSTER_OBJECT_DEBUG_FIELDS + 1, 0);
                    core += 1;
                }
                else if (f < a)
                {
                    assembly_debug_field(text, section, &cursor, f, 0, debug->extra.fields[field].section, 0);
                    field += 1;
                }
                else
                {
                    const NativeDebugAddress item = debug->extra.addresses[address];
                    assembly_debug_field(text, section, &cursor, a, item.symbol, 0, item.addend);
                    address += 1;
                }
            }
        }
        else if (s == BUSTER_OBJECT_DEBUG_LINE || s == BUSTER_OBJECT_DEBUG_ADDR)
        {
            uint32_t function = 0;
            uint64_t slot = 0;
            for (uint32_t u = 0; !text->full && u < units; u += 1)
            {
                slot += 8; // DWARF 5 contribution header in .debug_addr.
                const uint32_t count = object_debug_unit_functions(debug, u);
                for (uint32_t i = 0; !text->full && i < count; i += 1)
                {
                    const uint64_t at = s == BUSTER_OBJECT_DEBUG_ADDR ? slot : debug->line_addresses[function];
                    assembly_debug_field(text, section, &cursor, at, debug->functions[function], 0, 0);
                    function += 1;
                    slot += 8;
                }
            }
        }
        assembly_plain_bytes(text, (const uint8_t*)section.pointer + cursor, section.length - cursor);
    }
}

static AssemblyResult assembly_print(const AssemblyView* view, Arena* output, Arena* scratch)
{
    const Link* link = view->link;
    const uint64_t scratch_mark = scratch->length;
    const uint64_t output_mark = output->length;
    const uint32_t atom_count = link->atoms.count;
    const uint32_t symbol_count = link->symbols.count;
    const uint32_t unit = view->unit;
    bool frames = false;
    AssemblyResult result = { .error = ASSEMBLY_OK };
    AssemblyText text = {
        .arena = output,
    };
    // The symbols each atom of the unit defines, by atom and then by value (a counting sort, then an insertion sort of
    // each atom's few), and the symbols the text spells: those, and those the unit's fixups refer to.
    uint32_t* defined_begin = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)atom_count + 1);
    uint32_t* next = defined_begin != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)atom_count + 1) : 0;
    SymbolId* defined = next != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, symbol_count) : 0;
    uint8_t* used = defined != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbol_count) : 0;
    result.error = used != 0 ? ASSEMBLY_OK : ASSEMBLY_FULL;
    if (result.error == ASSEMBLY_OK && !link_groups_valid(link))
    {
        result.error = ASSEMBLY_INVALID;
        result.reason = "invalid section groups";
    }
    if (result.error == ASSEMBLY_OK)
    {
        memset(defined_begin, 0, ((uint64_t)atom_count + 1) * sizeof(*defined_begin));
        memset(used, 0, symbol_count);
        for (SymbolId s = 1; s < symbol_count; s += 1)
        {
            const AtomId atom = link->symbols.atom[s];
            const bool mine = atom != 0 && atom < atom_count && link->atoms.unit[atom] == unit;
            defined_begin[atom + 1] += mine ? 1 : 0;
            used[s] = (uint8_t)mine;
        }
        for (AtomId a = 1; a <= atom_count; a += 1)
        {
            defined_begin[a] += defined_begin[a - 1];
            next[a - 1] = defined_begin[a - 1];
        }
        for (SymbolId s = 1; s < symbol_count; s += 1)
        {
            const AtomId atom = link->symbols.atom[s];
            if (used[s] != 0)
            {
                defined[next[atom]] = s;
                next[atom] += 1;
            }
        }
        for (AtomId a = 1; a < atom_count; a += 1)
        {
            for (uint32_t i = defined_begin[a] + 1; i < defined_begin[a + 1]; i += 1)
            {
                const SymbolId symbol = defined[i];
                uint32_t j = i;
                while (j > defined_begin[a] && link->symbols.value[defined[j - 1]] > link->symbols.value[symbol])
                {
                    defined[j] = defined[j - 1];
                    j -= 1;
                }
                defined[j] = symbol;
            }
        }
        for (AtomId a = 1; a < atom_count; a += 1)
        {
            for (uint32_t f = 0; link->atoms.unit[a] == unit && f < link->atoms.fixup_count[a]; f += 1)
            {
                const FixupId fixup = link->atoms.fixup_begin[a] + f;
                const SymbolId target = link->fixups.target[fixup];
                // An unwind table's PC32 to a global or weak symbol the unit defines, under AssemblyView.local_unwind.
                const bool local = view->local_unwind && link->atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME &&
                                   (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_PC32 && (used[target] & 1) != 0 &&
                                   (link->symbols.flags[target] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
                used[target] |= (uint8_t)(local ? 6 : 2);
            }
        }
        for (uint32_t g = 1; g < link->groups.count; g += 1)
        {
            const LinkGroup group = link->groups.data[g];
            if (group.unit == unit && group.signature != 0)
            {
                used[group.signature] |= 2;
            }
        }
    }
    const ObjectDebug* debug = view->debug != NULL && !object_debug_absent(view->debug) ? view->debug : NULL;
    if (result.error == ASSEMBLY_OK && debug != NULL)
    {
        if (object_debug_valid(debug, link, unit))
        {
            for (uint32_t i = 0; i < debug->function_count; i += 1)
            {
                used[debug->functions[i]] |= 8;
            }
            for (uint32_t i = 0; i < debug->extra.address_count; i += 1)
            {
                used[debug->extra.addresses[i].symbol] |= 8;
            }
        }
        else
        {
            result.error = ASSEMBLY_INVALID;
            result.reason = "invalid debug information";
        }
    }
    // What the sink doesn't print yet, by name, before any text.
    for (AtomId a = 1; result.error == ASSEMBLY_OK && a < atom_count; a += 1)
    {
        const uint8_t class = link->atoms.class[a];
        const bool mine = link->atoms.unit[a] == unit;
        const AssemblyCode* code = view->code != 0 && a < view->code_count ? &view->code[a] : 0;
        frames = frames || (mine && code != NULL && code->frame_enabled);
        const bool uncoded = class == BUSTER_LINK_CLASS_TEXT && link->atoms.size[a] != 0 && (code == 0 || (code->records == 0 && code->template_count != 1));
        const char* reason = !mine ? 0 :
                             (class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI) && (code == NULL || code->offsets == NULL) ?
                                 "the C runtime's .init and .fini fragments" :
                             uncoded ? "code the emission made without instruction records" : 0;
        if (mine && code != NULL && !assembly_frame_valid(code, link->atoms.size[a]))
        {
            result.error = ASSEMBLY_INVALID;
            result.reason = "an invalid unwind frame range";
            result.atom = a;
        }
        else if (reason != 0)
        {
            result.error = ASSEMBLY_UNSUPPORTED;
            result.reason = reason;
            result.atom = a;
        }
    }
    if (result.error == ASSEMBLY_OK && view->cfi_atom != 0 && !assembly_frame_table_valid(view, defined_begin, next))
    {
        result.error = ASSEMBLY_INVALID;
        result.reason = "an invalid replacement unwind table";
        result.atom = view->cfi_atom;
    }
    for (SymbolId s = 1; result.error == ASSEMBLY_OK && s < symbol_count; s += 1)
    {
        const uint8_t flags = link->symbols.flags[s];
        const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
        const AtomId atom = link->symbols.atom[s];
        const bool common = type == BUSTER_LINK_SYMBOL_COMMON && (used[s] & 1) != 0;
        const char* reason = used[s] == 0 ? 0 : common && (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_GLOBAL ? "a non-global common definition" :
                             common && defined_begin[atom + 1] - defined_begin[atom] != 1 ? "a common definition sharing an atom with another symbol" :
                             (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_UNIQUE && atom == 0 ? "undefined unique symbols" :
                             (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_UNIQUE && type != BUSTER_LINK_SYMBOL_OBJECT && type != BUSTER_LINK_SYMBOL_TLS ?
                                 "unique symbols without object or TLS type" :
                             atom == 0 && link->symbols.name[s] == 0 ? "a reference to an undefined symbol without a name" : 0;
        if (reason != 0)
        {
            result.error = ASSEMBLY_UNSUPPORTED;
            result.reason = reason;
            result.symbol = s;
        }
    }
    AssemblyNames names = { 0 };
    if (result.error == ASSEMBLY_OK)
    {
        result.error = assembly_names(&names, view, used, scratch, &result);
        names.used = used;
    }
    if (result.error == ASSEMBLY_OK)
    {
        assembly_append(&text, "\t.file \"");
        for (uint64_t i = 0; i < view->source_name.length; i += 1)
        {
            const char c = view->source_name.pointer[i];
            const char escaped[2] = { '\\', c };
            assembly_bytes(&text, c == '"' || c == '\\' ? escaped : escaped + 1, c == '"' || c == '\\' ? 2 : 1);
        }
        assembly_append(&text, "\"\n");
    }
    // GNU as otherwise replaces an unused weak undefined signature with a local symbol in the group section. A
    // cancelled expression is an ordinary use without bytes or relocations, preserving the writer's signature.
    for (uint32_t g = 1; result.error == ASSEMBLY_OK && g < link->groups.count; g += 1)
    {
        const LinkGroup group = link->groups.data[g];
        const SymbolId signature = group.signature;
        if (group.unit == unit && signature != 0 && link->symbols.atom[signature] == 0)
        {
            assembly_append(&text, "\t.size ");
            assembly_name(&text, link, &names, signature);
            assembly_append(&text, ", ");
            assembly_name(&text, link, &names, signature);
            assembly_append(&text, "-");
            assembly_name(&text, link, &names, signature);
            assembly_append(&text, "\n");
        }
    }
    AssemblySection current = { .name = "" };
    if (result.error == ASSEMBLY_OK && frames)
    {
        assembly_append(&text, "\t.section .eh_frame,\"a\",@unwind\n\t.p2align 3\n\t.cfi_sections .eh_frame\n");
    }
    AtomId* section_slots = NULL;
    uint64_t section_mask = 0;
    for (AtomId a = 1; result.error == ASSEMBLY_OK && !text.full && a < atom_count; a += 1)
    {
        if (link->atoms.unit[a] == unit && a != view->cfi_atom)
        {
            const uint32_t begin = defined_begin[a];
            const uint32_t end = defined_begin[a + 1];
            if (begin < end && (link->symbols.flags[defined[begin]] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON)
            {
                const SymbolId symbol = defined[begin];
                // .comm supplies global binding and object type. Keep its visibility, and leave the current section alone.
                assembly_attributes(&text, link, &names, symbol, false);
                assembly_append(&text, "\t.comm ");
                assembly_name(&text, link, &names, symbol);
                assembly_append(&text, ", ");
                assembly_unsigned(&text, link->symbols.size[symbol]);
                assembly_append(&text, ", ");
                assembly_unsigned(&text, UINT64_C(1) << link->atoms.alignment_log2[a]);
                assembly_append(&text, "\n");
                continue;
            }
            const AssemblySection section = assembly_section(view, a);
            const uint8_t class = link->atoms.class[a];
            const AtomId associated = section.link;
            const bool ordered = (section.flags & SHF_LINK_ORDER) != 0;
            // A nondefault descriptor may reuse another section's name with different flags or type, including an
            // assembler's built-in .data or .text even without ordinary atoms. Give it a stable identity; explicit
            // descriptors equal to their class's default still join that section, as the object writer does.
            const ObjectAtomSection* descriptor = view->sections != NULL && a < view->section_count ? &view->sections[a] : NULL;
            const bool unique_section = ordered || section.identity != 0 || (descriptor != NULL && !object_section_is_default(descriptor, class));
            if (unique_section && section_slots == NULL)
            {
                uint64_t capacity = 16;
                while (capacity < 2 * (uint64_t)atom_count)
                {
                    capacity *= 2;
                }
                section_slots = BUSTER_ARENA_PUSH_ARRAY(scratch, AtomId, capacity);
                if (section_slots != NULL)
                {
                    memset(section_slots, 0, capacity * sizeof(*section_slots));
                    section_mask = capacity - 1;
                }
                else
                {
                    result.error = ASSEMBLY_FULL;
                }
            }
            const uint32_t identity = result.error == ASSEMBLY_OK && unique_section ? assembly_section_identity(view, section, a, section_slots, section_mask) : 0;
            SymbolId label = 0;
            if (ordered && associated != 0 && associated < atom_count && link->atoms.unit[associated] == unit && link->atoms.class[associated] < class &&
                defined_begin[associated] < defined_begin[associated + 1])
            {
                label = defined[defined_begin[associated]];
            }
            if (result.error == ASSEMBLY_OK && (ordered != (label != 0) || ordered != (associated != 0)))
            {
                result.error = ASSEMBLY_INVALID;
                result.reason = "a linked-order section without an associated label in its unit";
            }
            else if (result.error == ASSEMBLY_OK && (ordered || !assembly_same_section(section, current)))
            {
                result.error = assembly_section_directive(&text, section, label, identity, link, &names) ? ASSEMBLY_OK : ASSEMBLY_UNSUPPORTED;
                result.reason = result.error != ASSEMBLY_OK ? "a section type or flags -S doesn't print yet" : 0;
                current = section;
            }
            // A bare reference to this spelling could otherwise select the table rather than the undefined global.
            if (result.error == ASSEMBLY_OK && ordered)
            {
                const uint8_t* bytes = (const uint8_t*)section.name;
                const SymbolId symbol = names.slots[assembly_name_slot(&names, link, bytes, section.name_length, link_hash(bytes, section.name_length))];
                if (symbol != 0 && used[symbol] == 2 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL)
                {
                    assembly_append(&text, "\t.globl ");
                    assembly_name(&text, link, &names, symbol);
                    assembly_append(&text, "\n");
                }
            }
            const uint32_t alignment = link->atoms.alignment_log2[a];
            if (result.error == ASSEMBLY_OK && alignment != 0)
            {
                assembly_append(&text, "\t.p2align ");
                assembly_unsigned(&text, alignment);
                assembly_append(&text, class == BUSTER_LINK_CLASS_TEXT ? ", 0xcc\n" : "\n");
            }
            const char* reason = 0;
            const AssemblyCode* code = view->code != NULL && a < view->code_count ? &view->code[a] : NULL;
            const bool executable = class == BUSTER_LINK_CLASS_TEXT ||
                                    ((class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI) && code != NULL && code->offsets != NULL);
            if (result.error == ASSEMBLY_OK && executable && link->atoms.size[a] != 0)
            {
                result.error = code->offsets != NULL ? assembly_mixed_code(&text, link, &names, a, code, defined, begin, end, scratch, &reason) :
                               code->records == NULL ? assembly_inline_helper(&text, link, &names, a, code, defined, begin, end, &reason) :
                                                       assembly_code(&text, link, &names, a, code, defined, begin, end, scratch, &reason);
            }
            else if (result.error == ASSEMBLY_OK)
            {
                result.error = assembly_data(&text, link, &names, a, defined, begin, end, &reason);
                if (result.error == ASSEMBLY_OK && class == BUSTER_LINK_CLASS_TEXT && view->code != NULL && a < view->code_count && view->code[a].frame_enabled)
                {
                    uint32_t frame = 0;
                    result.error = assembly_frame_at(&text, &view->code[a], 0, &frame, &reason) ? ASSEMBLY_OK : ASSEMBLY_INVALID;
                }
            }
            if (result.error == ASSEMBLY_OK)
            {
                assembly_sizes(&text, link, &names, defined, begin, end);
            }
            else
            {
                result.reason = result.reason != 0 ? result.reason : reason;
                result.atom = a;
            }
        }
    }
    // The symbols the unit refers to without defining: weak bindings and visibilities, which the object keeps.
    for (SymbolId s = 1; result.error == ASSEMBLY_OK && s < symbol_count; s += 1)
    {
        if (used[s] == 2)
        {
            assembly_attributes(&text, link, &names, s, false);
        }
    }
    if (result.error == ASSEMBLY_OK && debug != NULL)
    {
        assembly_debug(&text, debug);
    }
    if (result.error == ASSEMBLY_OK)
    {
        assembly_append(&text, "\t.section .note.GNU-stack,\"\",@progbits\n");
    }
    // Text that ran out of room stopped its walks early, which can look like a contradiction: room is the failure.
    result.error = text.full ? ASSEMBLY_FULL : result.error;
    result.reason = text.full ? 0 : result.reason;
    if (result.error == ASSEMBLY_OK)
    {
        result.bytes = output->base + output_mark;
        result.length = output->length - output_mark;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return result;
}
