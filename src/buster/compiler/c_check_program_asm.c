// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147), as the lowering compiles it: each asm statement whose template assembles to instructions
// is a per-unit helper, a leaf function of its own records that the statement's code calls through Tier 0's ordinary
// CALL, as the atomic and floating-environment helpers are (c_check_program_helpers.c); a template of no instructions,
// the compiler barriers', is no code at all. Tier 0 doesn't see the asm: its registers stay out of Tier 0's depths and
// homes, the call saves the values live across it, and the helper has an unwind entry of its own.
//
// check_program_asm_plan plans every statement of the selected bodies before they are lowered, beside the builtin
// helpers, in this order (CheckProgramAsm):
//   - Placement. Each operand takes its constraint's first alternative (check_asm_constraint_parse): a general register
//     of a class or a fixed one, an XMM register for a float or a double, memory (an lvalue's: clang and the checker
//     refuse an rvalue there), an integer constant, or an output's register (a matching input). Every operand has a
//     register of its own but a matching pair, none a clobber, caller-saved ones first (check_asm_take).
//   - Substitution (check_asm_substitute): the template with each operand as GCC and clang print it in AT&T syntax: a
//     register by the operand's size or its modifier's (b h w k q), memory as (%reg), a constant as $value, and %%, %=,
//     %{ %| %} and {att|intel}'s first alternative. A basic asm statement's template is its text, as it is.
//   - Assembly: asm_assemble_code's records of the text, by the checker's path (the assembler's vector path or its
//     scalar twin), from a match index that admits every form the encoder has (an inline template's instructions are
//     its author's to choose, as GNU as and clang's integrated assembler take any).
//   - The helper (check_asm_helper_code): pushes of the callee-saved registers the statement touches, rsp aligned to 16
//     at the asm, the operands loaded from the operand block whose address comes in rdi, the records, the register
//     outputs stored through their addresses (which the block holds), and the epilogue: a frame unwind_tier0_frame
//     takes. Its symbol follows the builtin helpers', and check_program_emit publishes it as theirs.
// The lowering (check_lower_asm, c_check_program_lower.c) fills the block in clang's order: each output's address (with
// a `+` operand's value, loaded through it), then each input's value or a memory input's address, then the call.
//
// This is scalar: each statement is planned once per unit, and its template is a few instructions. The statements are
// rare (main.c's units have 58), and docs/c-inline-assembly.md ("Why it is scalar") measures what one costs a compile.
//
// What the lowering refuses, by name (CheckProgramAsm.reason): asm goto, flag outputs, the x87, MMX, mask and vector
// register classes and the other letters it doesn't place (A, p, Y, the floating constants), symbolic immediates and
// constants outside their letter's range, operands of types no general or XMM register holds by value, bit-fields,
// explicit register variables, clobbers of an operand's register, APX's registers, and templates whose instructions
// refer to symbols (the second part of #147's claim) or that the assembler refuses.
// Include after c_check_program_helpers.c, assembler.c, x86_target.c and tier0.c.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_INCLUDED

// An operand's place (CheckAsmPlace.kind).
#define BUSTER_CHECK_ASM_GENERAL 1
#define BUSTER_CHECK_ASM_VECTOR 2
#define BUSTER_CHECK_ASM_MEMORY 3
#define BUSTER_CHECK_ASM_CONSTANT 4

// The general registers but rsp, the ones the helper may take, and the callee-saved ones among them.
#define BUSTER_CHECK_ASM_GENERAL_ALL (0xffffu & ~(1u << X86_REGISTER_RSP))
#define BUSTER_CHECK_ASM_CALLEE_SAVED                                                                                   \
    (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R12 | 1u << X86_REGISTER_R13 | 1u << X86_REGISTER_R14 | \
     1u << X86_REGISTER_R15)
// The operand block's slots: 8 bytes each, a value's or an address's.
#define BUSTER_CHECK_ASM_SLOT_BYTES 8
// The most operands a statement the lowering takes may have: GCC takes 30.
#define BUSTER_CHECK_ASM_OPERANDS 64

// Where the lowering places an operand. `slot` is its block slot's offset (an output's or a memory operand's address,
// an input's value; BUSTER_CHECK_NONE for a constant), `value` a `+` register operand's value slot, `tied` a matching
// input's output (BUSTER_CHECK_NONE for none), `reg` the register (GENERAL's and VECTOR's, MEMORY's address's), and
// `size` the operand's bytes in a register (1, 2, 4 or 8: its type's).
typedef struct CheckAsmPlace CheckAsmPlace;
struct CheckAsmPlace
{
    int64_t constant;
    TypeId type;
    uint32_t slot;
    uint32_t value;
    uint32_t tied;
    uint8_t kind;
    uint8_t reg;
    uint8_t size;
    bool written;
    bool read;
};

// A statement's plan: its operands' places (output_count + input_count of them), its block's bytes (0 for none), its
// helper's index in CheckProgramHelpers.items (BUSTER_CHECK_NONE for a template of no instructions, which has none),
// and for a statement the lowering refuses, why (CheckProgramLowered.reason), reported where the statement is lowered.
typedef struct CheckProgramAsm CheckProgramAsm;
struct CheckProgramAsm
{
    CheckAsmPlace* places;
    const char* reason;
    uint32_t block;
    uint32_t helper;
    bool planned;
};

typedef struct CheckProgramAsms CheckProgramAsms;
struct CheckProgramAsms
{
    // input->asm_count of them, by statement (0 for a program without asm statements); a statement of no selected body
    // isn't planned.
    CheckProgramAsm* plans;
    CheckProgramStatus status;
    uint32_t token;
};

// A constraint's first alternative: the general registers it allows (a bit each), whether it allows an XMM register,
// memory or an integer constant (`ranged`: in [low, high], by its sign-extended value; `unsigned32`: Z's, by its
// zero-extended one, check_asm_constant_fits), the output a matching input is tied to (BUSTER_CHECK_NONE for none), and
// the first letter the lowering doesn't place, as the feature it names.
typedef struct CheckAsmConstraint CheckAsmConstraint;
struct CheckAsmConstraint
{
    int64_t low;
    int64_t high;
    const char* refused;
    uint32_t general;
    uint32_t tied;
    bool vector;
    bool memory;
    bool immediate;
    bool ranged;
    bool unsigned32;
};

static const char* const check_asm_registers[4][16] = {
    { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" },
    { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" },
    { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" },
    { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" },
};

// The general registers in the order an operand takes them: the caller-saved ones first, so that a template of them
// costs its helper no saves, then the callee-saved ones, which its prologue pushes.
static const uint8_t check_asm_order[15] = {
    X86_REGISTER_RAX,
    X86_REGISTER_RCX,
    X86_REGISTER_RDX,
    X86_REGISTER_RSI,
    X86_REGISTER_RDI,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
    X86_REGISTER_RBX,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
    X86_REGISTER_RBP,
};

// The register a clobber names: a general one's number (0 to 15), 16 plus an XMM, YMM or ZMM register's number, -1
// for one that needs nothing of the helper (rsp, the flags, memory, the x87's, MMX's and the opmasks, which its call
// clobbers anyway), or -2 for APX's general registers r16 to r31, which it doesn't take.
static int32_t check_asm_clobber(CheckAsmText text)
{
    if (text.length != 0 && (text.bytes[0] == '%' || text.bytes[0] == '#'))
    {
        text.bytes += 1;
        text.length -= 1;
    }
    int32_t result = -1;
    for (uint32_t size = 0; result == -1 && size < 4; size += 1)
    {
        for (uint32_t reg = 0; result == -1 && reg < 16; reg += 1)
        {
            const char* name = check_asm_registers[size][reg];
            if (text.length == strlen(name) && memcmp(text.bytes, name, text.length) == 0)
            {
                result = (int32_t)reg;
            }
        }
    }
    for (uint32_t high = 0; result == -1 && high < 4; high += 1)
    {
        static const char* const names[4] = { "ah", "ch", "dh", "bh" };
        if (text.length == 2 && memcmp(text.bytes, names[high], 2) == 0)
        {
            result = (int32_t)high;
        }
    }
    const bool vector = text.length >= 4 && (text.bytes[0] == 'x' || text.bytes[0] == 'y' || text.bytes[0] == 'z') && memcmp(text.bytes + 1, "mm", 2) == 0;
    const bool apx = result == -1 && text.length >= 3 && text.bytes[0] == 'r' && text.bytes[1] >= '1' && text.bytes[1] <= '3' && text.bytes[2] >= '0' && text.bytes[2] <= '9' &&
                     !(text.bytes[1] == '1' && text.bytes[2] <= '5');
    if (vector)
    {
        uint32_t number = 0;
        for (uint32_t at = 3; at < text.length && number < 64; at += 1)
        {
            number = number * 10 + (uint32_t)(text.bytes[at] - '0');
        }
        result = 16 + (int32_t)number;
    }
    else if (apx)
    {
        result = -2;
    }
    return result;
}

// Constraint `text`'s first alternative, for an output (after its = or +) or an input, of statement `statement`.
static CheckAsmConstraint check_asm_constraint_parse(const CheckInputAsm* statement, const InternTable* names, CheckAsmText text, bool output)
{
    CheckAsmConstraint result = { .tied = BUSTER_CHECK_NONE };
    for (uint32_t at = output ? 1 : 0; at < text.length && text.bytes[at] != ','; at += 1)
    {
        const uint8_t byte = text.bytes[at];
        int64_t low = 0;
        int64_t high = -1;
        if (byte >= '0' && byte <= '9')
        {
            uint32_t tied = 0;
            while (at < text.length && text.bytes[at] >= '0' && text.bytes[at] <= '9' && tied <= statement->output_count)
            {
                tied = tied * 10 + (uint32_t)(text.bytes[at++] - '0');
            }
            at -= 1;
            result.tied = tied;
        }
        else if (byte == '[')
        {
            const uint32_t begin = at + 1;
            while (at < text.length && text.bytes[at] != ']')
            {
                at += 1;
            }
            result.tied = check_asm_named(statement, names, text.bytes + begin, at - begin, false);
        }
        else if (byte == 'r' || byte == 'q' || byte == 'l' || byte == 'g' || byte == 'X')
        {
            result.general |= BUSTER_CHECK_ASM_GENERAL_ALL;
            result.memory |= byte == 'g' || byte == 'X';
            result.immediate |= (byte == 'g' || byte == 'X') && !output;
            if ((byte == 'g' || byte == 'X') && !output)
            {
                low = INT64_MIN;
                high = INT64_MAX;
            }
        }
        else if (byte == 'Q')
        {
            result.general |= 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RCX | 1u << X86_REGISTER_RDX;
        }
        else if (byte == 'R')
        {
            result.general |= 0xffu & ~(1u << X86_REGISTER_RSP);
        }
        else if (strchr("abcdSD", byte) != NULL)
        {
            static const char letters[] = "acdbSD";
            static const uint8_t registers[] = { X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RBX, X86_REGISTER_RSI, X86_REGISTER_RDI };
            result.general |= 1u << registers[strchr(letters, byte) - letters];
        }
        else if (byte == 'x' || byte == 'v')
        {
            result.vector = true;
        }
        else if (byte == 'm' || byte == 'o' || byte == 'V' || byte == '<' || byte == '>')
        {
            result.memory = true;
        }
        else if (byte == 'i' || byte == 'n')
        {
            low = INT64_MIN;
            high = INT64_MAX;
        }
        else if (byte == 'e')
        {
            low = INT32_MIN;
            high = INT32_MAX;
        }
        else if (byte == 'Z')
        {
            result.unsigned32 = true;
            result.immediate = true;
        }
        else if (strchr("IJKMNO", byte) != NULL)
        {
            static const char letters[] = "IJKMNO";
            static const int64_t lows[] = { 0, 0, -128, 0, 0, 0 };
            static const int64_t highs[] = { 31, 63, 127, 3, 255, 127 };
            low = lows[strchr(letters, byte) - letters];
            high = highs[strchr(letters, byte) - letters];
        }
        else if (byte == '&' || byte == '%' || byte == '?' || byte == '!' || byte == '*' || byte == '#' || byte == ' ' || byte == '\t')
        {
            // An early clobber is kept apart from the inputs anyway: every operand has a register of its own.
        }
        else if (result.refused == NULL)
        {
            result.refused = byte == '@'                                 ? "inline assembly flag outputs" :
                             byte == 'f' || byte == 't' || byte == 'u'   ? "inline assembly operands in x87 registers" :
                             byte == 'y'                                 ? "inline assembly operands in MMX registers" :
                             byte == 'k'                                 ? "inline assembly operands in opmask registers" :
                             byte == 'A'                                 ? "inline assembly operands in the rax and rdx pair" :
                                                                           "this inline assembly constraint";
        }
        if (high >= low)
        {
            result.low = result.ranged && result.low < low ? result.low : low;
            result.high = result.ranged && result.high > high ? result.high : high;
            result.immediate = true;
            result.ranged = true;
        }
    }
    return result;
}

// Whether `constraint` takes the integer constant `extended`, its type's `bits` sign-extended (as clang prints it), as
// clang checks it: a letter's range by that value (so "N"((unsigned char)255) is refused, as clang refuses it), and Z's
// by the zero-extended bits, as clang's backend checks Z (so "Z"(0xffffffffu) is taken, and printed $-1).
static bool check_asm_constant_fits(const CheckAsmConstraint* constraint, int64_t extended, uint32_t bits)
{
    const uint64_t zero_extended = bits >= 64 ? (uint64_t)extended : (uint64_t)extended & ((UINT64_C(1) << bits) - 1);
    return (constraint->ranged && extended >= constraint->low && extended <= constraint->high) || (constraint->unsigned32 && zero_extended <= UINT32_MAX);
}

// The type a matching input of `input`'s type takes its output's register in, for an output of `output`'s type, as GCC
// and clang place it: an integer or a pointer narrower than the output zero-extended from its own width (clang's zext,
// which the helper's narrower loads make too), so its unsigned type, whose value the output's conversion then widens;
// otherwise the output's own, a floating input converted as clang converts it.
static TypeId check_asm_tied_type(const TypeTable* types, TypeId input, TypeId output)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = check_action_integer_type(types, input, &width, &is_signed) || check_action_kind(types, input) == BUSTER_TYPE_KIND_POINTER;
    const uint64_t size = type_is_complete(types, input) ? type_size(types, input) : 0;
    const uint64_t output_size = type_is_complete(types, output) ? type_size(types, output) : 0;
    const TypeId widened = size == 1 ? BUSTER_TYPE_UCHAR : size == 2 ? BUSTER_TYPE_USHORT : size == 4 ? BUSTER_TYPE_UINT : output;
    return integer && size < output_size ? widened : output;
}

// A general register of `allowed` that no other operand and no clobber has (`taken`), in check_asm_order's order: its
// number, or -1 for none.
static int32_t check_asm_take(uint32_t allowed, uint32_t* taken)
{
    int32_t result = -1;
    for (uint32_t i = 0; result < 0 && i < BUSTER_ARRAY_LENGTH(check_asm_order); i += 1)
    {
        const uint32_t bit = 1u << check_asm_order[i];
        if ((allowed & bit) != 0 && (*taken & bit) == 0)
        {
            *taken |= bit;
            result = check_asm_order[i];
        }
    }
    return result;
}

// Appends `count` bytes to the text being written at the top of `scratch` (check_asm_substitute's).
static void check_asm_emit_bytes(Arena* scratch, const void* bytes, uint64_t count, bool* valid)
{
    uint8_t* at = *valid && count != 0 ? arena_push(scratch, count, 1) : 0;
    *valid &= count == 0 || at != 0;
    if (at != 0)
    {
        memcpy(at, bytes, count);
    }
}

static void check_asm_emit_text(Arena* scratch, const char* text, bool* valid)
{
    check_asm_emit_bytes(scratch, text, strlen(text), valid);
}

static void check_asm_emit_number(Arena* scratch, int64_t value, bool* valid)
{
    char digits[24];
    const int length = snprintf(digits, sizeof(digits), "%lld", (long long)value);
    check_asm_emit_bytes(scratch, digits, (uint64_t)length, valid);
}

// Operand `place`, as GCC and clang print it in AT&T syntax with `modifier` (0 for none): NULL, or the feature a
// modifier the lowering doesn't print names.
static const char* check_asm_print(Arena* scratch, const CheckAsmPlace* place, uint8_t modifier, bool* valid)
{
    const char* refused = NULL;
    if (place->kind == BUSTER_CHECK_ASM_GENERAL)
    {
        const uint32_t size = modifier == 'b' ? 1 : modifier == 'w' ? 2 : modifier == 'k' ? 4 : modifier == 'q' || modifier == 'V' || modifier == 'a' || modifier == 'A' ? 8 : place->size;
        const uint32_t row = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
        static const char* const high[4] = { "%ah", "%ch", "%dh", "%bh" };
        if (modifier == 'h' && place->reg < 4)
        {
            check_asm_emit_text(scratch, high[place->reg], valid);
        }
        else if (modifier == 0 || modifier == 'b' || modifier == 'w' || modifier == 'k' || modifier == 'q' || modifier == 'V' || modifier == 'a' || modifier == 'A')
        {
            check_asm_emit_text(scratch, modifier == 'a' ? "(%" : modifier == 'A' ? "*%" : modifier == 'V' ? "" : "%", valid);
            check_asm_emit_text(scratch, check_asm_registers[row][place->reg], valid);
            check_asm_emit_text(scratch, modifier == 'a' ? ")" : "", valid);
        }
        else if (modifier == 'z')
        {
            check_asm_emit_text(scratch, size == 1 ? "b" : size == 2 ? "w" : size == 4 ? "l" : "q", valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_VECTOR)
    {
        if (modifier == 0 || modifier == 'x' || modifier == 't' || modifier == 'g')
        {
            check_asm_emit_text(scratch, modifier == 't' ? "%ymm" : modifier == 'g' ? "%zmm" : "%xmm", valid);
            check_asm_emit_number(scratch, place->reg, valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_MEMORY)
    {
        if (modifier == 0 || modifier == 'H' || modifier == 'A')
        {
            check_asm_emit_text(scratch, modifier == 'H' ? "8(%" : modifier == 'A' ? "*(%" : "(%", valid);
            check_asm_emit_text(scratch, check_asm_registers[3][place->reg], valid);
            check_asm_emit_text(scratch, ")", valid);
        }
        else if (modifier == 'z' && (place->size == 1 || place->size == 2 || place->size == 4 || place->size == 8))
        {
            check_asm_emit_text(scratch, place->size == 1 ? "b" : place->size == 2 ? "w" : place->size == 4 ? "l" : "q", valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (modifier == 0 || modifier == 'c' || modifier == 'P' || modifier == 'p' || modifier == 'a' || modifier == 'n')
    {
        check_asm_emit_text(scratch, modifier == 0 ? "$" : "", valid);
        check_asm_emit_number(scratch, modifier == 'n' ? (int64_t)(0 - (uint64_t)place->constant) : place->constant, valid);
    }
    else
    {
        refused = "this inline assembly operand modifier";
    }
    return refused;
}

// Statement `statement`'s template with its operands substituted (as `places` has them), as AT&T syntax prints them,
// written at the top of `scratch` into `*text`, its basic template as it is: NULL, or the feature it refuses. `unique`
// is %='s number, the statement's own in the unit.
static const char* check_asm_substitute(const CheckInputAsm* statement, const CheckAsmPlace* places, const InternTable* names, uint32_t unique,
                                        Arena* scratch, String8* text, bool* valid)
{
    const CheckAsmText source = statement->template_text;
    const uint64_t begin = scratch->length;
    const uint32_t count = statement->output_count + statement->input_count;
    const char* refused = NULL;
    // 0 outside braces, 1 in the AT&T alternative, 2 in a later one, which isn't printed.
    uint32_t dialect = 0;
    for (uint32_t at = 0; *valid && refused == NULL && at < source.length; at += 1)
    {
        const uint8_t byte = source.bytes[at];
        if (statement->basic)
        {
            check_asm_emit_bytes(scratch, &byte, 1, valid);
        }
        else if (byte == '%' && at + 1 < source.length)
        {
            at += 1;
            const uint8_t next = source.bytes[at];
            if (next == '%' || next == '{' || next == '|' || next == '}')
            {
                if (dialect != 2)
                {
                    check_asm_emit_bytes(scratch, &next, 1, valid);
                }
            }
            else if (next == '=')
            {
                if (dialect != 2)
                {
                    check_asm_emit_number(scratch, unique, valid);
                }
            }
            else
            {
                uint8_t modifier = 0;
                if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'))
                {
                    modifier = next;
                    at += 1;
                }
                uint32_t operand = BUSTER_CHECK_NONE;
                if (at < source.length && source.bytes[at] == '[')
                {
                    const uint32_t name = at + 1;
                    while (at < source.length && source.bytes[at] != ']')
                    {
                        at += 1;
                    }
                    operand = check_asm_named(statement, names, source.bytes + name, at - name, modifier == 'l');
                }
                else
                {
                    operand = 0;
                    const uint32_t digits = at;
                    while (at < source.length && source.bytes[at] >= '0' && source.bytes[at] <= '9' && operand <= count)
                    {
                        operand = operand * 10 + (uint32_t)(source.bytes[at++] - '0');
                    }
                    operand = at > digits ? operand : BUSTER_CHECK_NONE;
                    at -= 1;
                }
                if (modifier == 'l' || operand >= count)
                {
                    // The checker took the template (check_asm_template), so this is an asm goto's label.
                    refused = "asm goto";
                }
                else if (dialect != 2)
                {
                    refused = check_asm_print(scratch, places + operand, modifier, valid);
                }
            }
        }
        else if (byte == '{' && dialect == 0)
        {
            dialect = 1;
        }
        else if (byte == '|' && dialect != 0)
        {
            dialect = 2;
        }
        else if (byte == '}' && dialect != 0)
        {
            dialect = 0;
        }
        else if (dialect != 2)
        {
            check_asm_emit_bytes(scratch, &byte, 1, valid);
        }
    }
    *text = (String8){ .pointer = (char*)scratch->base + begin, .length = scratch->length - begin };
    return refused;
}

// Whether the helper loads operand `place` before the template: a register operand the template reads (an input's
// value, a matching input's into its output's register, a `+` operand's) and a memory operand's address.
static bool check_asm_loads(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_MEMORY || ((place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR) && place->read);
}

// Whether the helper stores operand `place` after the template: a register output, through the address its slot holds.
static bool check_asm_stores(const CheckAsmPlace* place)
{
    return place->written && (place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR);
}

// The helper's records around the template's (`code`, which refers to no symbol): the pushes of `saved`'s callee-saved
// registers, the reservation that aligns rsp to 16 and keeps the block's address (`block`: whether the helper has an
// operand block, in rdi), the loads, the template's records, the stores of the register outputs (through `stores`'
// two registers) and the epilogue, with the template's branches in the helper's indices: the frame unwind_tier0_frame
// takes (Tier0Code.frame_saved and frame_size). A failure is TIER0_ERROR_MEMORY.
static Tier0Code check_asm_helper_code(const CheckAsmPlace* places, uint32_t count, const AsmCode* code, uint32_t saved, const uint8_t stores[2],
                                       bool block, Arena* output)
{
    Tier0Code result = { .error = TIER0_ERROR_MEMORY };
    uint32_t pushes = 0;
    uint32_t loads = 0;
    uint32_t stored = 0;
    for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
    {
        pushes += (saved >> tier0_callee_saved[r] & 1) != 0;
    }
    for (uint32_t k = 0; k < count; k += 1)
    {
        loads += check_asm_loads(places + k);
        stored += check_asm_stores(places + k);
    }
    // At the call, rsp is 8 past a multiple of 16; the pushes and the reservation (the block's address's slot) leave it
    // at one at the template, as clang -O0's functions have it in their bodies.
    const uint32_t frame = pushes % 2 == 0 ? 8 : 16;
    const uint64_t records = 2 * (uint64_t)pushes + (block ? 1 : 0) + loads + code->count + (stored != 0 ? 1 + 2 * (uint64_t)stored : 0) + 3;
    X86Instruction* written = records <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, records) : 0;
    uint32_t* branches = code->branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, code->branch_count) : 0;
    uint32_t* targets = code->branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, code->branch_count) : 0;
    if (written != 0 && (code->branch_count == 0 || (branches != 0 && targets != 0)))
    {
        uint32_t at = 0;
        uint32_t frame_saved = 0;
        for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
        {
            if ((saved >> tier0_callee_saved[r] & 1) != 0)
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = tier0_callee_saved[r] };
                frame_saved |= 1u << r;
            }
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
        if (block)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
        }
        // The loads, from the block rdi holds: the one into rdi last. Only a matching pair shares a register, and of it only
        // the input loads, so one load at most goes into rdi.
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                const bool into_rdi = place->kind != BUSTER_CHECK_ASM_VECTOR && place->reg == X86_REGISTER_RDI;
                if (check_asm_loads(place) && into_rdi == (pass == 1))
                {
                    // A `+` register operand's value is in its value slot; an input's value and a memory operand's address in
                    // its slot.
                    const uint32_t slot = place->written && place->kind != BUSTER_CHECK_ASM_MEMORY ? place->value : place->slot;
                    uint32_t form = 0;
                    if (place->kind == BUSTER_CHECK_ASM_VECTOR)
                    {
                        form = place->size == 4 ? X86_FORM_MOVD_X_RM32 : X86_FORM_MOVQ_X_XM64;
                    }
                    else if (place->kind == BUSTER_CHECK_ASM_MEMORY)
                    {
                        form = X86_FORM_MOV_R64_RM64;
                    }
                    else
                    {
                        // A narrower value zero-extended: the register's other bits are the template's to ignore, as GCC's
                        // and clang's leave them unspecified.
                        form = place->size == 1 ? X86_FORM_MOVZX_R32_RM8 : place->size == 2 ? X86_FORM_MOVZX_R32_RM16 :
                               place->size == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
                    }
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = place->reg, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)slot };
                }
            }
        }
        const uint32_t base = at;
        if (code->count != 0)
        {
            memcpy(written + at, code->records, (uint64_t)code->count * sizeof(X86Instruction));
        }
        at += code->count;
        for (uint32_t b = 0; b < code->branch_count; b += 1)
        {
            branches[b] = base + code->branches[b];
            targets[b] = base + code->targets[b];
        }
        if (stored != 0)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                const uint32_t form = place->kind == BUSTER_CHECK_ASM_VECTOR ? (place->size == 4 ? X86_FORM_MOVD_RM32_X : X86_FORM_MOVQ_XM64_X) :
                                      place->size == 1 ? X86_FORM_MOV_RM8_R8 : place->size == 2 ? X86_FORM_MOV_RM16_R16 :
                                      place->size == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = place->reg, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
        for (uint32_t r = BUSTER_TIER0_CALLEE_SAVED; r-- > 0;)
        {
            if ((saved >> tier0_callee_saved[r] & 1) != 0)
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = tier0_callee_saved[r] };
            }
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_RET };
        result = (Tier0Code){
            .records = written,
            .count = at,
            .branches = branches,
            .targets = targets,
            .branch_count = code->branch_count,
            .frame_saved = frame_saved,
            .frame_size = frame,
            .error = TIER0_OK,
        };
    }
    return result;
}

// The index the templates' instructions are matched against: every form the assembler's syntax takes, whatever the
// unit's target, as GNU as and clang's integrated assembler take a template's instructions. Made on `scratch` once per
// lowering of a unit with asm statements (the assembler guide measures its initialization), or 0 without room.
static const X86MatchIndex* check_asm_matches(Arena* scratch)
{
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(scratch, X86MatchIndex, 1);
    if (index != 0)
    {
        const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
        if (!x86_encoder_initialized)
        {
            x86_encoder_initialize();
        }
        x86_match_initialize(index, &everything);
    }
    return index;
}

// Whether operand expression `node` names an explicit register variable (`register T x asm("reg")`), whose register
// GCC and clang give the operand: the lowering doesn't place one there yet. An object's id is its first declaration's
// token plus one, which has its attributes.
static bool check_asm_register_variable(const CheckProgram* program, uint32_t node)
{
    const CheckInput* input = program->input;
    const CheckValue* value = node < program->stream->count ? program->stream->values + node : 0;
    const uint32_t declaration = value != 0 && (value->flags & BUSTER_CHECK_REGISTER) != 0 && value->object != 0 && value->object <= input->token_count ?
                                 value->object - 1 : BUSTER_CHECK_NONE;
    const CheckInputAttributes* attributes = declaration < input->token_count && input->registers[declaration] ? input->attributes[declaration] : 0;
    bool labelled = false;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        labelled |= attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL;
    }
    return labelled;
}

// Plans statement `s` (its operands from `operands`, check_asms_run's) into `plan`: its places, and its helper's code
// into `helper`, or the reason the lowering refuses it, its template matched against `index`. False only for an arena
// without room, with both arenas then as they were.
static bool check_program_asm_plan_one(const CheckProgram* program, uint32_t s, const CheckAsmCheckedOperand* operands, const X86MatchIndex* index,
                                       bool scalar, CheckProgramAsm* plan, CheckProgramHelper* helper, Arena* output, Arena* scratch)
{
    const CheckInput* input = program->input;
    const CheckInputAsm* statement = input->asm_statements + s;
    const TypeTable* types = program->types;
    const uint32_t count = statement->output_count + statement->input_count;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *plan = (CheckProgramAsm){ .helper = BUSTER_CHECK_NONE, .planned = true };
    CheckAsmPlace* places = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckAsmPlace, count) : 0;
    CheckAsmConstraint* constraints = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckAsmConstraint, count) : 0;
    bool valid = count == 0 || (places != 0 && constraints != 0);
    const char* reason = statement->is_goto ? "asm goto" : count > BUSTER_CHECK_ASM_OPERANDS ? "inline assembly statements of more than 64 operands" : NULL;
    // The registers no operand may take: the clobbers' (and rsp), and the XMM ones the clobbers name.
    uint32_t taken = 1u << X86_REGISTER_RSP;
    uint32_t vectors = 0;
    for (uint32_t c = 0; valid && reason == NULL && c < statement->clobber_count; c += 1)
    {
        const int32_t reg = check_asm_clobber(statement->clobbers[c]);
        if (reg == -2)
        {
            reason = "APX's registers in inline assembly";
        }
        else if (reg >= 0 && reg < 16)
        {
            taken |= 1u << reg;
        }
        else if (reg >= 16 && reg < 48)
        {
            vectors |= reg < 32 ? 1u << (reg - 16) : 0;
        }
    }
    // Each operand's constraint and type, and its kind of place.
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        const bool output = k < statement->output_count;
        const CheckAsmCheckedOperand* checked = operands + k;
        const CheckValue* value = &checked->value;
        // An input's value is its conversion's; an output's and a memory operand's, its lvalue's.
        const TypeId type = output ? value->type : checked->conversion.type != 0 ? checked->conversion.type : value->type;
        const uint8_t kind = check_action_kind(types, type);
        const uint64_t size = type_is_complete(types, type) ? type_size(types, type) : 0;
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = check_action_integer_type(types, type, &width, &is_signed) || kind == BUSTER_TYPE_KIND_POINTER;
        const bool floating = kind == BUSTER_TYPE_KIND_FLOATING && (size == 4 || size == 8);
        const bool registered = (integer || floating) && (size == 1 || size == 2 || size == 4 || size == 8);
        constraints[k] = check_asm_constraint_parse(statement, program->identifiers, statement->operands[k].constraint, output);
        const CheckAsmConstraint* constraint = constraints + k;
        CheckAsmPlace* place = places + k;
        *place = (CheckAsmPlace){
            .type = type,
            .slot = BUSTER_CHECK_NONE,
            .value = BUSTER_CHECK_NONE,
            .tied = output ? BUSTER_CHECK_NONE : constraint->tied,
            .size = registered ? (uint8_t)size : 0,
            .written = output,
            .read = !output || (statement->operands[k].flags & CHECK_ASM_READ) != 0,
        };
        const bool constant = !output && value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE &&
                              (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && integer;
        if (constraint->refused != NULL)
        {
            reason = constraint->refused;
        }
        else if ((value->flags & BUSTER_CHECK_BIT_FIELD) != 0 || (program->stream->values[checked->node].flags & BUSTER_CHECK_BIT_FIELD) != 0)
        {
            reason = "bit-fields as inline assembly operands";
        }
        else if (check_asm_register_variable(program, checked->node))
        {
            reason = "explicit register variables as inline assembly operands";
        }
        else if (place->tied != BUSTER_CHECK_NONE)
        {
            // A matching input: its output's place, below.
        }
        else if (constraint->immediate && constant)
        {
            // An integer constant as clang prints it: its type's bits, sign-extended.
            const uint32_t bits = width != 0 ? width : 64;
            const uint64_t raw = (uint64_t)value->integer;
            const int64_t extended = bits >= 64 ? (int64_t)raw : (int64_t)(raw << (64 - bits)) >> (64 - bits);
            place->kind = BUSTER_CHECK_ASM_CONSTANT;
            place->constant = extended;
            reason = check_asm_constant_fits(constraint, extended, bits) ? NULL : "inline assembly constants outside their constraint's range";
        }
        else if (constraint->general != 0 && registered)
        {
            place->kind = BUSTER_CHECK_ASM_GENERAL;
        }
        else if (constraint->vector && floating)
        {
            place->kind = BUSTER_CHECK_ASM_VECTOR;
        }
        else if (constraint->memory && (output || value->category == CHECK_LVALUE))
        {
            place->kind = BUSTER_CHECK_ASM_MEMORY;
        }
        else
        {
            reason = constraint->immediate && !constraint->memory && constraint->general == 0 && !constraint->vector ?
                     "inline assembly immediates that aren't integer constants" : "inline assembly operands of this type in their constraint's registers";
        }
    }
    // The fixed registers first, then the outputs' registers of a class, the inputs', the matching inputs (their outputs'),
    // and the memory operands' addresses; the XMM registers apart. An input fixed to an output's fixed register (as
    // "=a"(r) : "a"(n)) shares it, as GCC and clang let it, unless the output is early-clobbered or read too: it is that
    // output's matching input then.
    for (uint32_t pass = 0; valid && reason == NULL && pass < 4; pass += 1)
    {
        for (uint32_t k = 0; reason == NULL && k < count; k += 1)
        {
            CheckAsmPlace* place = places + k;
            const uint32_t allowed = constraints[k].general;
            const bool fixed = (allowed & (allowed - 1)) == 0;
            const bool output = k < statement->output_count;
            int32_t reg = -1;
            uint32_t sharing = BUSTER_CHECK_NONE;
            for (uint32_t o = 0; pass == 0 && !output && place->kind == BUSTER_CHECK_ASM_GENERAL && fixed && o < statement->output_count; o += 1)
            {
                const bool same = places[o].kind == BUSTER_CHECK_ASM_GENERAL && places[o].tied == BUSTER_CHECK_NONE && constraints[o].general == allowed;
                sharing = same && (statement->operands[o].flags & (CHECK_ASM_EARLY | CHECK_ASM_READ)) == 0 ? o : sharing;
            }
            if (sharing != BUSTER_CHECK_NONE && place->tied == BUSTER_CHECK_NONE)
            {
                place->tied = sharing;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && fixed)
            {
                reg = (taken & allowed) == 0 ? check_asm_take(allowed, &taken) : -2;
            }
            else if (pass == 1 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && !fixed && output)
            {
                reg = check_asm_take(allowed, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 2 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && !fixed && !output)
            {
                reg = check_asm_take(allowed, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_MEMORY)
            {
                reg = check_asm_take(BUSTER_CHECK_ASM_GENERAL_ALL, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_VECTOR && place->tied == BUSTER_CHECK_NONE)
            {
                const uint32_t free = ~vectors & 0xffffu;
                reg = free != 0 ? __builtin_ctz(free) : -3;
                vectors |= free != 0 ? 1u << reg : 0;
            }
            else if (pass == 3 && place->tied != BUSTER_CHECK_NONE)
            {
                // A matching input takes its output's register, which a register output of its own has (the checker
                // checked the match's types alike); a `+` output reads its own value instead.
                const CheckAsmPlace* target = place->tied < statement->output_count ? places + place->tied : 0;
                if (target == 0 || (target->kind != BUSTER_CHECK_ASM_GENERAL && target->kind != BUSTER_CHECK_ASM_VECTOR) || target->read)
                {
                    reason = "inline assembly inputs matching an output that isn't a register's of its own";
                }
                else
                {
                    place->kind = target->kind;
                    place->reg = target->reg;
                    place->size = place->size != 0 ? place->size : target->size;
                }
            }
            if (reg >= 0)
            {
                place->reg = (uint8_t)reg;
            }
            else if (reg == -2)
            {
                reason = "inline assembly operands whose fixed register is another operand's or a clobber";
            }
            else if (reg == -3)
            {
                reason = "inline assembly operands past the registers left";
            }
        }
    }
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        // A matching input of a narrower register class than its output's (the checker matched their kinds).
        reason = places[k].kind != BUSTER_CHECK_ASM_CONSTANT && places[k].kind != BUSTER_CHECK_ASM_MEMORY && places[k].size == 0 ?
                 "inline assembly operands of this type in their constraint's registers" : NULL;
    }
    // The operand block: each register output's address (and a `+` one's value), each memory operand's address and each
    // input's value, 8 bytes each.
    uint32_t block = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        CheckAsmPlace* place = places + k;
        if (place->kind != BUSTER_CHECK_ASM_CONSTANT)
        {
            place->slot = block;
            block += BUSTER_CHECK_ASM_SLOT_BYTES;
        }
        if (place->written && place->read && place->kind != BUSTER_CHECK_ASM_MEMORY)
        {
            place->value = block;
            block += BUSTER_CHECK_ASM_SLOT_BYTES;
        }
    }
    // The two registers the stores take after the template: none an output's, whose value they would lose.
    uint8_t stores[2] = { X86_REGISTER_RAX, X86_REGISTER_RAX };
    uint32_t avoided = 1u << X86_REGISTER_RSP;
    uint32_t outputs = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        outputs += check_asm_stores(places + k);
        avoided |= check_asm_stores(places + k) && places[k].kind == BUSTER_CHECK_ASM_GENERAL ? 1u << places[k].reg : 0;
    }
    for (uint32_t i = 0; valid && reason == NULL && outputs != 0 && i < 2; i += 1)
    {
        const int32_t reg = check_asm_take(BUSTER_CHECK_ASM_GENERAL_ALL, &avoided);
        stores[i] = reg >= 0 ? (uint8_t)reg : X86_REGISTER_RAX;
        reason = reg < 0 ? "inline assembly operands past the registers left" : NULL;
        taken |= reg >= 0 ? 1u << reg : 0;
    }
    // The template, its operands substituted, assembled by the checker's path.
    String8 text = { 0 };
    if (valid && reason == NULL)
    {
        reason = check_asm_substitute(statement, places, program->identifiers, s, scratch, &text, &valid);
    }
    AsmCode code = { 0 };
    if (valid && reason == NULL)
    {
        code = asm_assemble_code(text, index, (AsmOptions){ .vectorized = !scalar }, output, scratch);
        reason = code.error != NULL && strstr(code.error, "arena exhausted") == NULL ? code.error : NULL;
        valid = code.error == NULL || reason != NULL;
        reason = reason == NULL && code.reference_count != 0 ? "inline assembly that refers to symbols" : reason;
    }
    if (valid && reason == NULL && code.count != 0)
    {
        // The callee-saved registers the statement touches: its clobbers', its operands' and the stores'.
        helper->code = check_asm_helper_code(places, count, &code, taken & BUSTER_CHECK_ASM_CALLEE_SAVED, stores, block != 0, output);
        valid = helper->code.error == TIER0_OK;
        helper->id = CBUILTIN_NONE;
        helper->assembly = s + 1;
        helper->parameter_count = block != 0 ? 1 : 0;
        helper->parameter_types[0] = block != 0 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID;
        helper->parameter_types[1] = BUSTER_TYPE_VOID;
        helper->parameter_types[2] = BUSTER_TYPE_VOID;
        helper->result_type = BUSTER_TYPE_VOID;
        helper->compiler_barrier = true;
        plan->helper = 0;
    }
    plan->places = places;
    plan->block = block;
    plan->reason = reason;
    if (!valid)
    {
        arena_pop_to(output, output_mark);
        *plan = (CheckProgramAsm){ 0 };
    }
    else if (reason != NULL)
    {
        // A refusal keeps nothing but itself.
        arena_pop_to(output, output_mark);
        plan->places = 0;
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

// Plans every asm statement of the program's bodies (CheckProgramAsms), appending a helper for each whose template
// assembles to instructions to `helpers`, after the builtin ones, with the next symbols. False for an arena without
// room or an inconsistent input; the refusals are the plans' own, which the lowering reports in its order.
static bool check_program_asm_plan(CheckProgramAsms* result, CheckProgramHelpers* helpers, const CheckProgram* program, bool scalar, Arena* output, Arena* scratch)
{
    *result = (CheckProgramAsms){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    const CheckInput* input = program->input;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    // A body's controls: its statements' (entry_count of them), then its statement expressions' (to count). A unit
    // without asm statements, the usual one, has none to walk for.
    uint32_t statements = 0;
    for (uint32_t b = 0; input->asm_count != 0 && b < program->body_count; b += 1)
    {
        for (uint32_t i = 0; i < program->bodies[b].count; i += 1)
        {
            statements += program->controls[program->bodies[b].first + i].kind == CHECK_CONTROL_ASM;
        }
    }
    const CheckAsms* asms = program->asms;
    bool valid = statements == 0 || (asms != 0 && asms->status == CHECK_ASM_OK && input->asm_count != 0);
    result->plans = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAsm, input->asm_count) : 0;
    uint32_t* firsts = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->asm_count + 1) : 0;
    CheckProgramHelper* added = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelper, statements) : 0;
    const X86MatchIndex* index = added != 0 ? check_asm_matches(scratch) : 0;
    uint32_t count = 0;
    if (statements != 0 && (result->plans == 0 || firsts == 0 || added == 0 || index == 0))
    {
        result->status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        valid = false;
    }
    if (valid && statements != 0)
    {
        memset(result->plans, 0, (uint64_t)input->asm_count * sizeof(CheckProgramAsm));
        uint64_t first = 0;
        for (uint32_t s = 0; s < input->asm_count; s += 1)
        {
            firsts[s] = (uint32_t)first;
            first += (uint64_t)input->asm_statements[s].output_count + input->asm_statements[s].input_count;
        }
        valid = first == asms->count;
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    }
    for (uint32_t b = 0; valid && statements != 0 && b < program->body_count; b += 1)
    {
        for (uint32_t i = 0; valid && i < program->bodies[b].count; i += 1)
        {
            const CheckControl control = program->controls[program->bodies[b].first + i];
            const uint32_t s = control.target;
            if (control.kind == CHECK_CONTROL_ASM && (s >= input->asm_count || result->plans[s].planned))
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = control.token;
                valid = false;
            }
            else if (control.kind == CHECK_CONTROL_ASM)
            {
                CheckProgramHelper* helper = added + count;
                *helper = (CheckProgramHelper){ 0 };
                valid = check_program_asm_plan_one(program, s, asms->operands + firsts[s], index, scalar, result->plans + s, helper, output, scratch);
                if (valid && result->plans[s].helper != BUSTER_CHECK_NONE)
                {
                    result->plans[s].helper = helpers->count + count;
                    count += 1;
                }
                result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
                result->token = valid ? BUSTER_CHECK_NONE : control.token;
            }
        }
    }
    if (valid && count != 0)
    {
        CheckProgramHelper* items = (uint64_t)program->symbol_count + helpers->count + count < UINT32_MAX ?
                                    BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelper, (uint64_t)helpers->count + count) : 0;
        valid = items != 0;
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
        if (valid)
        {
            if (helpers->count != 0)
            {
                memcpy(items, helpers->items, (uint64_t)helpers->count * sizeof(CheckProgramHelper));
            }
            memcpy(items + helpers->count, added, (uint64_t)count * sizeof(CheckProgramHelper));
            for (uint32_t k = 0; k < count; k += 1)
            {
                items[helpers->count + k].symbol = program->symbol_count + 1 + helpers->count + k;
            }
            helpers->items = items;
            helpers->count += count;
        }
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckProgramAsms){ .status = status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

#endif
