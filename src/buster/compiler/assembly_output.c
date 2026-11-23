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
//     of zeros and for zero-fill sections, `.string` and `.ascii` in string pools.
//   - Sections: each atom's (its descriptor's, or its class's object section), in atom order, aligned by `.p2align`
//     with int3 in code and zeros elsewhere, as the object writer fills its gaps.
// What the sink doesn't print yet it refuses (ASSEMBLY_UNSUPPORTED, `reason` naming it): thread-local storage, section
// groups, linked-order sections, common and unique symbols, code without records (inline assembly's), and fixup kinds
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

// A TEXT atom's instructions, as the emission made them: `count` records, sized (each branch's rel8 or rel32 form) and
// encoded into the atom's bytes, and its branches to its own records (`branches[i]` targets `targets[i]`, a record or
// `count` for the code's end).
typedef struct AssemblyCode AssemblyCode;
struct AssemblyCode
{
    const X86Instruction* records;
    const uint32_t* branches;
    const uint32_t* targets;
    uint32_t count;
    uint32_t branch_count;
};

// A unit of a link to print: its atoms (those of `unit`), each TEXT atom's code (indexed by AtomId, `code_count` entries),
// optional section descriptors (as ObjectOptions.atom_sections has them) and temporary symbols (as
// ObjectOptions.temporary_symbols), and the source's name for `.file`.
typedef struct AssemblyView AssemblyView;
struct AssemblyView
{
    const Link* link;
    const AssemblyCode* code;
    const ObjectAtomSection* sections;
    const uint8_t* temporary;
    String8 source_name;
    uint32_t unit;
    uint32_t code_count;
    uint32_t section_count;
    uint32_t temporary_count;
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

// Whether GNU as reads the bytes as a plain symbol name: a letter, `_`, `.` or `$` first, then those and digits. Any
// other name is written quoted (assembly_name).
static bool assembly_plain_name(const uint8_t* bytes, uint64_t length)
{
    bool plain = length != 0 && !(bytes[0] >= '0' && bytes[0] <= '9');
    for (uint64_t i = 0; plain && i < length; i += 1)
    {
        const uint8_t c = bytes[i];
        plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$';
    }
    return plain;
}

// What the sink calls each symbol: its own name (own[s] = 1) or BUSTER_ASSEMBLY_PREFIX "s" and its id (own[s] = 0). A
// symbol takes its own name where it has one, unless it is temporary and the name isn't a local label's (a temporary's
// own name would put it in the object's symbol table, where the object writer leaves it out).
typedef struct AssemblyNames AssemblyNames;
struct AssemblyNames
{
    uint8_t* own;
    // Each symbol's uses (assembly_print's `used`): 4 where an unwind table names its local label (AssemblyView.local_unwind).
    const uint8_t* used;
    // Each form row's assembly_ambiguous answer, as the walk finds it: the printing's other per-unit table.
    uint8_t* widths;
};

static void assembly_name(AssemblyText* text, const Link* link, const AssemblyNames* names, SymbolId symbol)
{
    if (names->own[symbol] != 0)
    {
        const NameId name = link->symbols.name[symbol];
        const uint8_t* bytes = link->names.bytes[name];
        const uint32_t length = link->names.length[name];
        if (assembly_plain_name(bytes, length))
        {
            assembly_bytes(text, bytes, length);
        }
        else
        {
            // GNU as's quoted name: a backslash before `"` and `\`, and every other byte as it is.
            assembly_append(text, "\"");
            uint32_t start = 0;
            for (uint32_t i = 0; i < length; i += 1)
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
    else
    {
        assembly_append(text, BUSTER_ASSEMBLY_PREFIX "s");
        assembly_unsigned(text, symbol);
    }
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
            uint32_t slot = (uint32_t)link->names.hash[name] & (slots - 1);
            bool placed = false;
            while (!placed && error == ASSEMBLY_OK)
            {
                const SymbolId other = table[slot];
                if (other == 0)
                {
                    table[slot] = s;
                    placed = true;
                }
                else
                {
                    const NameId other_name = link->symbols.name[other];
                    if (link->names.length[other_name] == length && memcmp(link->names.bytes[other_name], bytes, length) == 0)
                    {
                        error = ASSEMBLY_UNSUPPORTED;
                        result->reason = "two symbols of the same name, which GNU as would take for one";
                        result->symbol = s;
                    }
                    slot = (slot + 1) & (slots - 1);
                }
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
    const bool spelled = call || got || pc || absolute;
    if (spelled)
    {
        assembly_name(text, link, names, reference->target);
        assembly_append(text, call ? "@PLT" : got ? "@GOTPCREL" : "");
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
    // A record GNU as encodes otherwise: its bytes, but where a relocation is in them, which the sink refuses (below).
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
    // A form GNU as encodes otherwise with a relocation in it (`mov $symbol, %ecx` by C7, an MPX bnd branch to a
    // symbol), which no backend emits: refused, rather than printed as another encoding.
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
        };
    }
    return result;
}

static bool assembly_same_section(AssemblySection a, AssemblySection b)
{
    return a.name_length == b.name_length && memcmp(a.name, b.name, a.name_length) == 0 && a.flags == b.flags && a.entry_size == b.entry_size && a.type == b.type;
}

// `.section name,"flags",@type[,entsize]`, or false for a section type the sink doesn't spell.
static bool assembly_section_directive(AssemblyText* text, AssemblySection section)
{
    const char* type = section.type == SHT_PROGBITS ? "@progbits" : section.type == SHT_NOBITS ? "@nobits" : section.type == SHT_INIT_ARRAY ? "@init_array" :
                       section.type == SHT_FINI_ARRAY ? "@fini_array" : section.type == SHT_PREINIT_ARRAY ? "@preinit_array" : section.type == SHT_NOTE ? "@note" :
                       section.type == SHT_X86_64_UNWIND ? "@unwind" : 0;
    const uint64_t known = SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_MERGE | SHF_STRINGS | SHF_TLS | SHF_GNU_RETAIN;
    const bool spelled = type != 0 && (section.flags & ~known) == 0;
    if (spelled)
    {
        assembly_append(text, "\t.section ");
        if (assembly_plain_name((const uint8_t*)section.name, section.name_length))
        {
            assembly_bytes(text, section.name, section.name_length);
        }
        else
        {
            assembly_append(text, "\"");
            assembly_bytes(text, section.name, section.name_length);
            assembly_append(text, "\"");
        }
        assembly_append(text, ",\"");
        assembly_append(text, (section.flags & SHF_ALLOC) ? "a" : "");
        assembly_append(text, (section.flags & SHF_WRITE) ? "w" : "");
        assembly_append(text, (section.flags & SHF_EXECINSTR) ? "x" : "");
        assembly_append(text, (section.flags & SHF_MERGE) ? "M" : "");
        assembly_append(text, (section.flags & SHF_STRINGS) ? "S" : "");
        assembly_append(text, (section.flags & SHF_TLS) ? "T" : "");
        assembly_append(text, (section.flags & SHF_GNU_RETAIN) ? "R" : "");
        assembly_append(text, "\",");
        assembly_append(text, type);
        if (section.flags & SHF_MERGE)
        {
            assembly_append(text, ",");
            assembly_unsigned(text, section.entry_size);
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
    const char* visibility = visibilities[flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT];
    const char* binding_directive = binding == BUSTER_LINK_SYMBOL_WEAK ? "\t.weak " : binding == BUSTER_LINK_SYMBOL_GLOBAL && defined ? "\t.globl " : 0;
    const char* type_name = type == BUSTER_LINK_SYMBOL_FUNCTION ? "@function" : type == BUSTER_LINK_SYMBOL_OBJECT ? "@object" : type == BUSTER_LINK_SYMBOL_IFUNC ? "@gnu_indirect_function" : 0;
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
    if (type_name != 0 && defined)
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
        if (names->used != 0 && (names->used[symbol] & 4) != 0)
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

// The fixup kinds that are thread-local storage's.
static bool assembly_tls_kind(uint8_t kind)
{
    return kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD ||
           kind == BUSTER_LINK_FIXUP_DTPOFF32 || kind == BUSTER_LINK_FIXUP_TPOFF64 || kind == BUSTER_LINK_FIXUP_DTPOFF64;
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
            const LinkFixupKind* description = &link_fixup_kinds[kind];
            const uint32_t width = description->width;
            const int64_t addend = link_fixup_addend(link, fixup);
            const SymbolId target = link->fixups.target[fixup];
            const bool size_kind = kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64;
            const bool spelled = kind == BUSTER_LINK_FIXUP_ABS64 || kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64 || size_kind;
            bool zero = at + width <= size;
            for (uint32_t i = 0; zero && i < width; i += 1)
            {
                zero = bytes[at + i] == 0;
            }
            if (!spelled)
            {
                error = ASSEMBLY_UNSUPPORTED;
                *reason = assembly_tls_kind(kind) ? "thread-local storage" : "a relocation in data that -S doesn't print yet";
            }
            else if (!zero)
            {
                error = ASSEMBLY_INVALID;
                *reason = "a data fixup's field that isn't zero";
            }
            else
            {
                assembly_append(text, width == 8 ? "\t.quad " : "\t.long ");
                if (class == BUSTER_LINK_CLASS_EH_FRAME && kind == BUSTER_LINK_FIXUP_PC32 && names->used != 0 && (names->used[target] & 4) != 0)
                {
                    assembly_append(text, BUSTER_ASSEMBLY_PREFIX "l");
                    assembly_unsigned(text, target);
                }
                else
                {
                    assembly_name(text, link, names, target);
                }
                assembly_append(text, size_kind ? "@SIZE" : "");
                assembly_addend(text, addend);
                assembly_append(text, description->pc_relative ? "-.\n" : "\n");
                at += width;
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

// A TEXT atom's code: its records, each after the labels of its offset (its symbols' and, for a branch's target, the
// sink's own), with the atom's fixups as their records' references, then the labels at its end. The records must encode
// to the atom's bytes (with its fixups' fields zero, as link_add_code_fixups leaves them) and every fixup must be one of
// their fields. Returns the error, with `reason`.
static AssemblyError assembly_code(AssemblyText* text, const Link* link, const AssemblyNames* names, AtomId atom, const AssemblyCode* code, const SymbolId* defined,
                                   uint32_t begin, uint32_t end, Arena* scratch, const char** reason)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = code->count;
    const uint64_t size = link->atoms.size[atom];
    const uint8_t* atom_bytes = link->atoms.bytes[atom];
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* targets = offsets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    uint8_t* labeled = targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint8_t* encoded = labeled != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, BUSTER_X86_ENCODER_CAPACITY(count)) : 0;
    AssemblyError error = encoded != 0 ? ASSEMBLY_OK : ASSEMBLY_FULL;
    const int64_t length = encoded != 0 ? x86_encode(code->records, count, encoded, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0) : -1;
    if (error == ASSEMBLY_OK && ((uint64_t)length != size || atom_bytes == 0))
    {
        error = ASSEMBLY_INVALID;
        *reason = "instruction records that don't encode to their function's bytes";
    }
    if (error == ASSEMBLY_OK)
    {
        offsets[count] = (uint32_t)size;
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
        error = (uint64_t)offset + width <= size ? ASSEMBLY_OK : ASSEMBLY_INVALID;
        if (error == ASSEMBLY_OK)
        {
            memset(encoded + offset, 0, width);
        }
    }
    if (error == ASSEMBLY_OK && memcmp(encoded, atom_bytes, size) != 0)
    {
        error = ASSEMBLY_INVALID;
        *reason = "instruction records that don't encode to their function's bytes";
    }
    uint32_t cursor = begin;
    uint32_t next_fixup = 0;
    for (uint32_t r = 0; error == ASSEMBLY_OK && !text->full && r < count; r += 1)
    {
        const uint32_t offset = offsets[r];
        const uint32_t record_end = offsets[r + 1];
        if (cursor < end && link->symbols.value[defined[cursor]] < offset)
        {
            error = ASSEMBLY_INVALID;
            *reason = "a symbol inside an instruction";
        }
        assembly_labels(text, link, names, defined, end, &cursor, offset);
        if (labeled[r] != 0)
        {
            assembly_append(text, BUSTER_ASSEMBLY_PREFIX);
            assembly_unsigned(text, atom);
            assembly_append(text, ".");
            assembly_unsigned(text, r);
            assembly_append(text, ":\n");
        }
        // The record's fixups: on its displacement's field, then its immediate's, each at its distance from the end.
        const X86Instruction* record = &code->records[r];
        AssemblyReference displacement = { 0 };
        AssemblyReference immediate = { 0 };
        while (error == ASSEMBLY_OK && next_fixup < fixup_count && link->fixups.offset[fixup_begin + next_fixup] < record_end)
        {
            const FixupId fixup = fixup_begin + next_fixup;
            const uint32_t offset_in = link->fixups.offset[fixup];
            const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const uint32_t to_end = record_end - offset_in;
            const bool on_displacement = to_end == x86_fixup_distance(record->form, X86_FIXUP_DISPLACEMENT) && !displacement.present;
            const bool on_immediate = to_end == x86_fixup_distance(record->form, X86_FIXUP_IMMEDIATE) && !immediate.present;
            AssemblyReference* reference = on_displacement ? &displacement : on_immediate ? &immediate : 0;
            if (offset_in < offset || reference == 0)
            {
                error = ASSEMBLY_INVALID;
                *reason = "a fixup off its instruction's fields";
            }
            else if (assembly_tls_kind(kind))
            {
                error = ASSEMBLY_UNSUPPORTED;
                *reason = "thread-local storage";
            }
            else
            {
                // The source's addend: the field's, plus the distance from the field to the instruction's end for a
                // PC-relative kind, which the assembler takes off again.
                const int64_t addend = link_fixup_addend(link, fixup);
                *reference = (AssemblyReference){
                    .addend = link_fixup_kinds[kind].pc_relative ? addend + (int64_t)to_end : addend,
                    .target = link->fixups.target[fixup],
                    .kind = kind,
                    .present = true,
                };
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
            else if (!assembly_instruction(text, link, names, names->widths, record, encoded + offset, record_end - offset, &displacement, &immediate, atom, targets[r], reason))
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
        if (cursor != end || next_fixup != fixup_count)
        {
            error = ASSEMBLY_INVALID;
            *reason = cursor != end ? "a symbol inside an instruction" : "a fixup off its instruction's fields";
        }
    }
    arena_pop_to(scratch, mark);
    return error;
}

static AssemblyResult assembly_print(const AssemblyView* view, Arena* output, Arena* scratch)
{
    const Link* link = view->link;
    const uint64_t scratch_mark = scratch->length;
    const uint64_t output_mark = output->length;
    const uint32_t atom_count = link->atoms.count;
    const uint32_t symbol_count = link->symbols.count;
    const uint32_t unit = view->unit;
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
    }
    // What the sink doesn't print yet, by name, before any text.
    for (AtomId a = 1; result.error == ASSEMBLY_OK && a < atom_count; a += 1)
    {
        const uint8_t class = link->atoms.class[a];
        const bool mine = link->atoms.unit[a] == unit;
        const bool grouped = link->groups.count != 0 && a < link->groups.atom_count && link->groups.atom_groups[a] != 0;
        const bool linked = view->sections != 0 && a < view->section_count && view->sections[a].link != 0;
        const AssemblyCode* code = view->code != 0 && a < view->code_count ? &view->code[a] : 0;
        const bool uncoded = class == BUSTER_LINK_CLASS_TEXT && link->atoms.size[a] != 0 && (code == 0 || code->records == 0);
        const char* reason = !mine ? 0 : class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_TBSS ? "thread-local storage" :
                             grouped ? "section groups (COMDAT)" : linked ? "linked-order sections (patchable function entries' tables)" :
                             class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI ? "the C runtime's .init and .fini fragments" :
                             uncoded ? "code the emission made without instruction records" : 0;
        if (reason != 0)
        {
            result.error = ASSEMBLY_UNSUPPORTED;
            result.reason = reason;
            result.atom = a;
        }
    }
    for (SymbolId s = 1; result.error == ASSEMBLY_OK && s < symbol_count; s += 1)
    {
        const uint8_t flags = link->symbols.flags[s];
        const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
        const char* reason = used[s] == 0 ? 0 : type == BUSTER_LINK_SYMBOL_TLS ? "thread-local storage" : type == BUSTER_LINK_SYMBOL_COMMON ? "common symbols" :
                             (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_UNIQUE ? "unique symbols (STB_GNU_UNIQUE)" :
                             link->symbols.atom[s] == 0 && link->symbols.name[s] == 0 ? "a reference to an undefined symbol without a name" : 0;
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
    AssemblySection current = { .name = "" };
    for (AtomId a = 1; result.error == ASSEMBLY_OK && !text.full && a < atom_count; a += 1)
    {
        if (link->atoms.unit[a] == unit)
        {
            const AssemblySection section = assembly_section(view, a);
            const uint8_t class = link->atoms.class[a];
            if (!assembly_same_section(section, current))
            {
                result.error = assembly_section_directive(&text, section) ? ASSEMBLY_OK : ASSEMBLY_UNSUPPORTED;
                result.reason = result.error != ASSEMBLY_OK ? "a section type or flags -S doesn't print yet" : 0;
                current = section;
            }
            const uint32_t alignment = link->atoms.alignment_log2[a];
            if (result.error == ASSEMBLY_OK && alignment != 0)
            {
                assembly_append(&text, "\t.p2align ");
                assembly_unsigned(&text, alignment);
                assembly_append(&text, class == BUSTER_LINK_CLASS_TEXT ? ", 0xcc\n" : "\n");
            }
            const char* reason = 0;
            if (result.error == ASSEMBLY_OK && class == BUSTER_LINK_CLASS_TEXT && link->atoms.size[a] != 0)
            {
                result.error = assembly_code(&text, link, &names, a, &view->code[a], defined, defined_begin[a], defined_begin[a + 1], scratch, &reason);
            }
            else if (result.error == ASSEMBLY_OK)
            {
                result.error = assembly_data(&text, link, &names, a, defined, defined_begin[a], defined_begin[a + 1], &reason);
            }
            if (result.error == ASSEMBLY_OK)
            {
                assembly_sizes(&text, link, &names, defined, defined_begin[a], defined_begin[a + 1]);
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
