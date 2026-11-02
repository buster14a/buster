// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1134's round trip: every form the assembler's syntax admits (x86_match_supported, intersected with a target of every
// feature) as random records of x86_random_instruction, each printed in Intel syntax by the encoder tests' GNU as printer
// (x86_print_instruction, without the pseudo-prefixes that pick one encoding of an instruction) and in AT&T syntax by
// this file's printer, then assembled by both of the matcher's paths. Each must assemble, both paths into the same
// record, both syntaxes into the same bytes, and that record must be the original instruction: the same mnemonic, the
// same operands in Intel order (registers, memory, the immediate's value at the operation's width), the same
// decorations, and the matcher's choice among its encodings: a form of no higher rank (legacy or VEX, then EVEX, then a
// VEX form that yields to its EVEX siblings: X86Match.rank) and, of the original's rank, no longer. With --cross-check, GNU as assembles both syntaxes'
// lines, each in a 16-byte slot of its own, and its bytes must be the matcher's, but where GNU as would choose another
// encoding of the same instruction (x86_assembler_prefers_another_form), which is counted. Coverage grows with the form
// table: a form the syntax admits is a form this test round-trips, by construction.
//
// The forms left out, each by name: a branch's (REL), whose destination is a label, which the branch fixtures test;
// embedded rounding's ({rn-sae}), which the parser has no syntax for.

#include <stdarg.h>

#define BUSTER_ASM_ROUND_TRIP_VARIANTS 4

typedef struct AsmRoundTripOperand AsmRoundTripOperand;
struct AsmRoundTripOperand
{
    uint64_t value;
    // An immediate as the Intel printer writes it: its field's bits as the record holds them.
    int64_t written;
    int32_t displacement;
    uint16_t width;
    uint8_t kind;
    uint8_t class;
    uint8_t reg;
    uint8_t mode;
    uint8_t index;
    uint8_t segment;
};

// Whether the round trip leaves the form out (the header's list).
static bool asm_round_trip_skipped(const X86FormDescription* form)
{
    return form->syntax == X86_SYNTAX_REL || (form->evex & BUSTER_X86_EVEX_ROUNDING) != 0;
}

// A record's operands in Intel order, by its form's roles: a register (its class and number), a memory operand (its
// width, mode, base, index, displacement and segment) or an immediate (its value at the operation's width for a value,
// or its field's bits). Returns the count.
static uint32_t asm_round_trip_operands(const X86Instruction* record, AsmRoundTripOperand operands[4])
{
    const X86FormDescription* form = x86_form_description(record->form);
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    uint8_t roles[4];
    const uint32_t count = x86_match_roles(form->syntax, roles);
    for (uint32_t i = 0; i < count; i += 1)
    {
        AsmRoundTripOperand* operand = &operands[i];
        *operand = (AsmRoundTripOperand){ .kind = X86_OPERAND_REGISTER };
        switch (roles[i])
        {
        case X86_MATCH_RM:
            if (record->mode & BUSTER_X86_MODE_MEMORY)
            {
                operand->kind = X86_OPERAND_MEMORY;
                operand->width = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : form->operand_bytes;
                operand->mode = record->mode;
                operand->reg = (record->mode & BUSTER_X86_MODE_BASE) ? record->rm : 0;
                operand->index = (record->mode & BUSTER_X86_MODE_INDEX) ? record->index : 0;
                operand->displacement = record->displacement;
                operand->segment = (uint8_t)x86_form_segment(record->form);
            }
            else
            {
                operand->class = form->rm & BUSTER_X86_OPERAND_CLASS;
                operand->reg = record->rm;
            }
            break;
        case X86_MATCH_REG:
            operand->class = form->reg & BUSTER_X86_OPERAND_CLASS;
            operand->reg = record->reg;
            break;
        case X86_MATCH_VVVV:
            operand->class = form->vvvv & BUSTER_X86_OPERAND_CLASS;
            operand->reg = record->vvvv;
            break;
        case X86_MATCH_IS4:
            operand->class = form->reg & BUSTER_X86_OPERAND_CLASS;
            operand->reg = (uint8_t)((uint32_t)record->immediate >> 4);
            break;
        case X86_MATCH_XMM0:
            operand->class = BUSTER_X86_CLASS_XMM;
            break;
        case X86_MATCH_ST0:
            // The x87's st(0) beside an st(i) (#147).
            operand->class = BUSTER_X86_CLASS_ST;
            break;
        case X86_MATCH_CL:
            operand->class = BUSTER_X86_CLASS_GPR8;
            operand->reg = 1;
            break;
        case X86_MATCH_ACC:
            operand->class = form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 2 ? BUSTER_X86_CLASS_GPR16 : form->operand_bytes == 4 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
            break;
        case X86_MATCH_ONE:
            operand->kind = X86_OPERAND_IMMEDIATE;
            operand->value = 1;
            operand->written = 1;
            break;
        default:
        {
            operand->kind = X86_OPERAND_IMMEDIATE;
            const int32_t high = form->immediate_bytes == 8 ? record->displacement : 0;
            const bool value = x86_match_value(row);
            const uint64_t field = ((uint64_t)(uint32_t)record->immediate | (uint64_t)(uint32_t)high << 32) & x86_width_mask(form->immediate_bytes);
            operand->value = value ? x86_immediate_value(row, record->immediate, high) : field;
            operand->width = (uint16_t)(value ? x86_effects(row)->width : form->immediate_bytes);
            operand->written = form->immediate_bytes == 8 ? (int64_t)field : record->immediate;
            break;
        }
        }
    }
    return count;
}

// A form's rank in the matcher's choice (X86Match.rank).
static uint32_t asm_round_trip_rank(uint32_t row)
{
    return x86_match_yields(row) ? 2 : x86_form_descriptions[row].family == BUSTER_X86_FAMILY_EVEX ? 1 : 0;
}

// Whether two records are one instruction (the header's oracle): the same mnemonic, operands, decorations and broadcast.
static bool asm_round_trip_same(const X86Instruction* original, const X86Instruction* matched)
{
    const X86FormDescription* a = x86_form_description(original->form);
    const X86FormDescription* b = x86_form_description(matched->form);
    AsmRoundTripOperand left[4];
    AsmRoundTripOperand right[4];
    const uint32_t count = asm_round_trip_operands(original, left);
    bool same = strcmp(a->mnemonic, b->mnemonic) == 0 && count == asm_round_trip_operands(matched, right);
    same &= (original->decorations & (BUSTER_X86_DECORATION_MASK | BUSTER_X86_DECORATION_ZEROING)) == (matched->decorations & (BUSTER_X86_DECORATION_MASK | BUSTER_X86_DECORATION_ZEROING));
    same &= ((a->evex ^ b->evex) & BUSTER_X86_EVEX_BROADCAST) == 0;
    for (uint32_t i = 0; same && i < count; i += 1)
    {
        const AsmRoundTripOperand* l = &left[i];
        const AsmRoundTripOperand* r = &right[i];
        same = l->kind == r->kind && l->value == r->value && l->displacement == r->displacement && l->width == r->width && l->class == r->class && l->reg == r->reg &&
               l->mode == r->mode && l->index == r->index && l->segment == r->segment;
    }
    return same;
}

// The record's Intel spelling: x86_print_instruction's, without its leading pseudo-prefixes ({load}, {store}, {vex},
// {evex}, {nf}, {disp32}). `gnu` keeps an absolute address's ds:, which GNU as reads and this assembler doesn't (it
// reads the address, with its width, as memory). Returns the length, or 0 if it doesn't fit.
static uint32_t asm_round_trip_intel(const X86Instruction* record, char* text, uint32_t capacity, bool gnu)
{
    char raw[512];
    FILE* file = fmemopen(raw, sizeof(raw), "w");
    uint32_t length = 0;
    if (file != 0)
    {
        x86_print_instruction(file, record, 0);
        const long written = ftell(file);
        fclose(file);
        uint32_t at = 0;
        while (written > 0 && at < (uint32_t)written && raw[at] == '{')
        {
            while (at < (uint32_t)written && raw[at] != ' ')
            {
                at += 1;
            }
            at += at < (uint32_t)written;
        }
        for (; written > 0 && at < (uint32_t)written && length + 2 < capacity; at += 1)
        {
            const bool data_segment = !gnu && at + 2 < (uint32_t)written && raw[at] == 'd' && raw[at + 1] == 's' && raw[at + 2] == ':';
            if (data_segment)
            {
                // An absolute address: [N], which says memory without a width too.
                text[length++] = '[';
                at += 3;
                while (at < (uint32_t)written && length + 2 < capacity && (raw[at] == '-' || (raw[at] >= '0' && raw[at] <= '9')))
                {
                    text[length++] = raw[at++];
                }
                text[length++] = ']';
                at -= 1;
            }
            else
            {
                text[length++] = raw[at];
            }
        }
        text[length] = 0;
    }
    return length;
}

// Text appended to `text` at `*length`, as far as `capacity` allows.
static __attribute__((format(printf, 4, 5))) void asm_round_trip_append(char* text, uint32_t capacity, uint32_t* length, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = *length < capacity ? vsnprintf(text + *length, capacity - *length, format, arguments) : 0;
    va_end(arguments);
    *length += written > 0 ? (uint32_t)written : 0;
}

static void asm_round_trip_att_operand(const X86Instruction* record, const AsmRoundTripOperand* operand, uint16_t descriptor, char* text, uint32_t capacity, uint32_t* length)
{
    const X86FormDescription* form = x86_form_description(record->form);
    // A near branch's register or memory operand is its destination's place: AT&T's indirection, `*`.
    const uint64_t length_name = strlen(form->mnemonic);
    const bool branch = form->syntax == X86_SYNTAX_RM && ((length_name >= 3 && strcmp(form->mnemonic + length_name - 3, "jmp") == 0) || (length_name >= 4 && strcmp(form->mnemonic + length_name - 4, "call") == 0));
    asm_round_trip_append(text, capacity, length, "%s", branch && operand->kind != X86_OPERAND_IMMEDIATE ? "*" : "");
    if (operand->kind == X86_OPERAND_IMMEDIATE)
    {
        // As the Intel printer writes it (GNU as takes a bit offset's imm8 unsigned).
        asm_round_trip_append(text, capacity, length, "$%lld", (long long)operand->written);
    }
    else if (operand->kind == X86_OPERAND_MEMORY)
    {
        static const char* const segments[] = { "", "%fs:", "%gs:" };
        asm_round_trip_append(text, capacity, length, "%s", segments[operand->segment % 3]);
        const bool base = (operand->mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = (operand->mode & BUSTER_X86_MODE_INDEX) != 0;
        const bool rip = (operand->mode & BUSTER_X86_MODE_RIP) != 0;
        asm_round_trip_append(text, capacity, length, "%d", operand->displacement);
        if (base || index || rip)
        {
            asm_round_trip_append(text, capacity, length, "(");
            if (rip)
            {
                asm_round_trip_append(text, capacity, length, "%%rip");
            }
            if (base)
            {
                asm_round_trip_append(text, capacity, length, "%%%s", x86_gpr64_names[operand->reg & BUSTER_X86_REGISTER_NUMBER]);
            }
            if (index)
            {
                asm_round_trip_append(text, capacity, length, ",%%%s,%u", x86_gpr64_names[operand->index & BUSTER_X86_REGISTER_NUMBER], 1u << (operand->mode & BUSTER_X86_MODE_SCALE));
            }
            asm_round_trip_append(text, capacity, length, ")");
        }
        if (form->evex & BUSTER_X86_EVEX_BROADCAST)
        {
            asm_round_trip_append(text, capacity, length, "{1to%u}", (uint32_t)form->operand_bytes / form->disp8_scale);
        }
    }
    else
    {
        const uint16_t named = descriptor != 0 ? descriptor : (uint16_t)(operand->class | BUSTER_X86_OPERAND_REGISTER);
        asm_round_trip_append(text, capacity, length, "%%%s", x86_register_name(operand->reg, named));
    }
}

// The AT&T mnemonic of a form, with a suffix: `suffix` 0 for none, or the letters a source writes (b, w, l, q, s, t, ll,
// x, y, z). The extension moves are AT&T's own names (movzbl, movslq).
static void asm_round_trip_att_mnemonic(const X86FormDescription* form, const char* suffix, char* text, uint32_t capacity, uint32_t* length)
{
    const bool zero = strcmp(form->mnemonic, "movzx") == 0;
    const bool sign = strcmp(form->mnemonic, "movsx") == 0 || strcmp(form->mnemonic, "movsxd") == 0;
    if (zero || sign)
    {
        static const char letters[] = "?bw?l???q";
        const uint32_t destination = 1u << ((form->reg & BUSTER_X86_OPERAND_CLASS) - BUSTER_X86_CLASS_GPR8);
        asm_round_trip_append(text, capacity, length, "mov%c%c%c", zero ? 'z' : 's', letters[form->operand_bytes & 15], letters[destination & 15]);
    }
    else
    {
        // GNU as's AT&T names: the x87's 16-bit environment and state forms' s (fnsaves), the dword string instructions'
        // l (rep stosl), and the far returns', iret's and sysret's.
        static const char* const names[][2] = { { "iretd", "iretl" }, { "sysretd", "sysretl" }, { "retfw", "lretw" }, { "retfd", "lretl" }, { "retfq", "lretq" } };
        const char* mnemonic = form->mnemonic;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            mnemonic = strcmp(mnemonic, names[i][0]) == 0 ? names[i][1] : mnemonic;
        }
        const uint64_t name = strlen(mnemonic);
        const bool environment = mnemonic[0] == 'f' && name > 1 && mnemonic[name - 1] == 'w' && form->operand_bytes != 2;
        const char* base = name >= 5 ? mnemonic + name - 5 : "";
        const bool string = form->syntax == X86_SYNTAX_NONE && name >= 4 && mnemonic[name - 1] == 'd' &&
                            (strncmp(base, "movs", 4) == 0 || strncmp(base, "cmps", 4) == 0 || strncmp(base, "lods", 4) == 0 || strncmp(base, "stos", 4) == 0 ||
                             strncmp(base, "scas", 4) == 0 || strncmp(base, "outs", 4) == 0 || strncmp(mnemonic + name - 4, "ins", 3) == 0);
        asm_round_trip_append(text, capacity, length, "%.*s%s%s", (int)(name - (environment || string)), mnemonic, environment ? "s" : string ? "l" : "", suffix != 0 ? suffix : "");
    }
}

// The AT&T mnemonic of an x87 subtraction or division whose destination is st(i) (#147): the System V assembler's
// reversed one, which GNU as reads `fsub %st, %st(1)` as (Intel's fsubr st(1), st(0)) and objdump prints Intel's as,
// for a popping form always and another where st(i) isn't st(0) (asm_x87_operands' rule). NULL for any other record.
static const char* asm_round_trip_x87_reversed(const X86FormDescription* form, const X86Instruction* record)
{
    static const char* const reversed[][2] = {
        { "fsub", "fsubr" }, { "fsubr", "fsub" }, { "fdiv", "fdivr" }, { "fdivr", "fdiv" },
        { "fsubp", "fsubrp" }, { "fsubrp", "fsubp" }, { "fdivp", "fdivrp" }, { "fdivrp", "fdivp" },
    };
    const char* result = NULL;
    for (uint32_t i = 0; form->syntax == X86_SYNTAX_RM_ST && result == NULL && i < BUSTER_ARRAY_LENGTH(reversed); i += 1)
    {
        const bool popping = i >= 4;
        result = strcmp(form->mnemonic, reversed[i][0]) == 0 && (popping || record->rm != 0) ? reversed[i][1] : NULL;
    }
    return result;
}

// The record in AT&T syntax: its mnemonic (and `suffix`), then its operands in reverse order, the destination's mask and
// zeroing after it.
static uint32_t asm_round_trip_att(const X86Instruction* record, const char* suffix, char* text, uint32_t capacity)
{
    const X86FormDescription* form = x86_form_description(record->form);
    AsmRoundTripOperand operands[4];
    uint8_t roles[4];
    const uint32_t count = asm_round_trip_operands(record, operands);
    x86_match_roles(form->syntax, roles);
    uint32_t length = 0;
    const char* reversed = asm_round_trip_x87_reversed(form, record);
    if (reversed != NULL)
    {
        asm_round_trip_append(text, capacity, &length, "%s", reversed);
    }
    else
    {
        asm_round_trip_att_mnemonic(form, suffix, text, capacity, &length);
    }
    for (uint32_t i = count; i-- > 0;)
    {
        asm_round_trip_append(text, capacity, &length, i + 1 == count ? " " : ",");
        const uint16_t descriptor = roles[i] == X86_MATCH_RM ? form->rm : roles[i] == X86_MATCH_REG || roles[i] == X86_MATCH_IS4 ? form->reg
                                    : roles[i] == X86_MATCH_VVVV                                                                 ? form->vvvv
                                                                                                                                 : 0;
        asm_round_trip_att_operand(record, &operands[i], descriptor, text, capacity, &length);
        if (i == 0 && (record->decorations & BUSTER_X86_DECORATION_MASK))
        {
            asm_round_trip_append(text, capacity, &length, "{%%k%u}", record->decorations & BUSTER_X86_DECORATION_MASK);
        }
        if (i == 0 && (record->decorations & BUSTER_X86_DECORATION_ZEROING))
        {
            asm_round_trip_append(text, capacity, &length, "{z}");
        }
    }
    return length < capacity ? length : 0;
}

// The suffixes an AT&T spelling of the form may take, in the order the round trip tries them (none first): the integer
// width's (b, w, l, q) of its AT&T suffix width (x86_match_suffix_width), the x87's of its memory (s, l, t, ll), and a
// vector memory operand's (x, y, z).
static uint32_t asm_round_trip_suffixes(const X86FormDescription* form, const char* suffixes[4])
{
    uint32_t count = 0;
    suffixes[count++] = 0;
    const uint32_t width = x86_match_suffix_width(form);
    const uint32_t rm = form->rm & BUSTER_X86_OPERAND_CLASS;
    if (form->mnemonic[0] == 'f' && (form->rm & BUSTER_X86_OPERAND_MEMORY) && rm == BUSTER_X86_CLASS_NONE)
    {
        const bool integer = form->mnemonic[1] == 'i';
        suffixes[count++] = integer ? (width == 2 ? "s" : width == 4 ? "l" : width == 8 ? "ll" : 0) : (width == 4 ? "s" : width == 8 ? "l" : width == 10 ? "t" : 0);
    }
    else if (rm >= BUSTER_X86_CLASS_XMM && rm <= BUSTER_X86_CLASS_ZMM && (form->rm & BUSTER_X86_OPERAND_MEMORY))
    {
        suffixes[count++] = width == 16 ? "x" : width == 32 ? "y" : width == 64 ? "z" : 0;
    }
    else
    {
        suffixes[count++] = width == 1 ? "b" : width == 2 ? "w" : width == 4 ? "l" : width == 8 ? "q" : 0;
    }
    count -= suffixes[count - 1] == 0 && count > 1;
    return count;
}

// One source of one instruction by both paths (asm_assemble_code): its record, or false if either path refuses it or
// they differ.
static bool asm_round_trip_assemble(AsmTest* test, const char* source, bool intel, X86Instruction* record, bool* differ)
{
    const String8 text = { .pointer = (char*)source, .length = strlen(source) };
    test->scalar.length = 0;
    test->vector.length = 0;
    const AsmCode scalar = asm_assemble_code(text, test->index, (AsmOptions){ .intel = intel }, &test->scalar, &test->vector);
    const AsmCode vector = asm_assemble_code(text, test->index, (AsmOptions){ .vectorized = true, .intel = intel }, &test->vector, &test->scalar);
    *differ = (scalar.error == 0) != (vector.error == 0) || scalar.count != vector.count ||
              (scalar.count == 1 && memcmp(scalar.records, vector.records, sizeof(X86Instruction)) != 0);
    const bool assembled = scalar.error == 0 && vector.error == 0 && scalar.count == 1 && vector.count == 1;
    if (assembled)
    {
        *record = scalar.records[0];
    }
    return assembled && !*differ;
}

// A failure's details, the first 16.
static void asm_round_trip_fail(AsmTest* test, const char* what, const char* source, const X86Instruction* original)
{
    char detail[640];
    const X86FormDescription* form = x86_form_description(original->form);
    snprintf(detail, sizeof(detail), "%s (form %u %s): %s", what, (uint32_t)(form - x86_form_descriptions), form->mnemonic, source);
    asm_test_check(test, false, "round trip", detail);
}

typedef struct AsmRoundTripLine AsmRoundTripLine;
struct AsmRoundTripLine
{
    X86Instruction original;
    X86Instruction matched;
};

// The round trip of every admitted form, `variants` records each; the lines it assembled go to `lines` (and their Intel
// and AT&T sources to `intel` and `att`, for GNU as), up to `capacity`. Returns the count of lines.
static uint32_t asm_round_trip(AsmTest* test, uint64_t* random, uint32_t variants, AsmRoundTripLine* lines, FILE* intel, FILE* att, uint32_t capacity)
{
    const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
    x86_match_initialize(test->index, &everything);
    test->architecture = X86_ARCH_OTHER;
    const X86EncoderMode mode = x86_test_mode;
    x86_test_mode = X86_ENCODER_MODE_64;
    uint32_t forms = 0;
    uint32_t skipped = 0;
    uint32_t suffixed = 0;
    uint32_t count = 0;
    for (uint32_t at = 0; at < test->index->begin[BUSTER_X86_MATCH_BUCKETS]; at += 1)
    {
        const uint32_t row = test->index->forms[at];
        const X86FormDescription* form = &x86_form_descriptions[row];
        if (asm_round_trip_skipped(form))
        {
            skipped += 1;
            continue;
        }
        forms += 1;
        for (uint32_t variant = 0; variant < variants; variant += 1)
        {
            const X86Instruction original = x86_random_instruction(random, (uint16_t)row, true);
            uint8_t original_bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
            const int64_t original_length = x86_encode_scalar(&original, 1, original_bytes, sizeof(original_bytes), 0, 0);
            char source[640];
            memcpy(source, ".intel_syntax noprefix\n", 23);
            const uint32_t length = asm_round_trip_intel(&original, source + 23, sizeof(source) - 23, false);
            X86Instruction matched;
            bool differ = false;
            const bool assembled = length != 0 && original_length > 0 && asm_round_trip_assemble(test, source, true, &matched, &differ);
            uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
            const int64_t matched_length = assembled ? x86_encode_scalar(&matched, 1, bytes, sizeof(bytes), 0, 0) : 0;
            // The matcher's choice among the forms of the instruction: a lower rank (legacy or VEX, EVEX, then a VEX form
            // that yields), and the shortest of its rank.
            const uint32_t matched_row = (uint32_t)(x86_form_description(matched.form) - x86_form_descriptions);
            const uint32_t original_rank = asm_round_trip_rank(row);
            const uint32_t matched_rank = asm_round_trip_rank(matched_row);
            const bool shortest = matched_length > 0 && (matched_rank < original_rank || (matched_rank == original_rank && matched_length <= original_length));
            if (!assembled || !asm_round_trip_same(&original, &matched) || !shortest)
            {
                asm_round_trip_fail(test, differ ? "Intel: the paths differ" : !assembled ? "Intel: not assembled" : !shortest ? "Intel: a longer encoding" : "Intel: another instruction",
                                    source + 23, &original);
                continue;
            }
            test->checks += 1;
            // AT&T: without a suffix, then with the form's, where the operands leave its width open.
            const char* suffixes[4];
            const uint32_t tries = asm_round_trip_suffixes(form, suffixes);
            char att_source[640];
            bool att_done = false;
            X86Instruction att_matched = { 0 };
            for (uint32_t t = 0; !att_done && t < tries; t += 1)
            {
                att_done = asm_round_trip_att(&original, suffixes[t], att_source, sizeof(att_source)) != 0 && asm_round_trip_assemble(test, att_source, false, &att_matched, &differ);
                suffixed += att_done && t != 0;
            }
            // The same instruction as Intel's, in the same bytes (a control immediate's field may hold its bits either signed or
            // not, as the source writes them).
            uint8_t att_bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
            const int64_t att_length = att_done ? x86_encode_scalar(&att_matched, 1, att_bytes, sizeof(att_bytes), 0, 0) : 0;
            const bool att_same = att_done && asm_round_trip_same(&original, &att_matched) && att_length == matched_length && memcmp(att_bytes, bytes, (size_t)att_length) == 0;
            if (!att_same)
            {
                asm_round_trip_fail(test, differ ? "AT&T: the paths differ" : !att_done ? "AT&T: not assembled" : "AT&T: another encoding than Intel's", att_source, &original);
                continue;
            }
            test->checks += 1;
            if (count < capacity && intel != 0 && att != 0)
            {
                char gnu[640];
                asm_round_trip_intel(&original, gnu, sizeof(gnu), true);
                fprintf(intel, "%s\n.p2align 4, 0xcc\n", gnu);
                fprintf(att, "%s\n.p2align 4, 0xcc\n", att_source);
                lines[count++] = (AsmRoundTripLine){ .original = original, .matched = matched };
            }
        }
    }
    x86_test_mode = mode;
    printf("assembler round trip: %u forms, %u records each, in Intel and AT&T syntax by both paths (%u AT&T spellings with a suffix); %u forms "
           "left out (branches and embedded rounding)\n",
           forms, variants, suffixed, skipped);
    asm_test_check(test, forms > 4000, "round trip covers the admitted forms", 0);
    return count;
}

// GNU as's bytes of a round trip's source, read from the .text of the object it writes, or an empty slice.
static String8 asm_round_trip_gnu(AsmTest* test, const char* directory, const char* name, FILE* source)
{
    char assembly[PATH_MAX];
    char object[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(assembly, sizeof(assembly), "%s/%s.s", directory, name);
    snprintf(object, sizeof(object), "%s/%s.o", directory, name);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    String8 bytes = { 0 };
    const long size = ftell(source);
    char* text = size > 0 ? arena_push(&test->vector, (uint64_t)size, 1) : 0;
    rewind(source);
    bool okay = text != 0 && fread(text, 1, (size_t)size, source) == (size_t)size && object_write_file(assembly, (const uint8_t*)text, (uint64_t)size) == OBJECT_ERROR_NONE &&
                oracle_epoch_file(assembly);
    char* arguments[] = { "as", "--64", "-o", object, assembly, 0 };
    okay = okay && asm_test_tool(arguments, object, output, errors) == 0;
    if (okay)
    {
        const String8 file = object_test_read_file(&test->vector, object);
        ImportElf elf;
        okay = import_elf(&elf, file, ET_REL).error == IMPORT_ERROR_NONE;
        for (uint32_t index = 1; okay && index < elf.section_count && bytes.length == 0; index += 1)
        {
            const Elf64_Shdr header = import_section_header(&elf, index);
            String8 section = { 0 };
            if (import_section_name(&elf, &header, &section) && x86_match_name(section, ".text") && import_within(file.length, header.sh_offset, header.sh_size))
            {
                bytes = (String8){ .pointer = file.pointer + header.sh_offset, .length = header.sh_size };
            }
        }
    }
    char diagnostics[1024] = { 0 };
    FILE* messages = okay && bytes.length != 0 ? 0 : fopen(errors, "r");
    if (messages != 0)
    {
        const size_t read = fread(diagnostics, 1, sizeof(diagnostics) - 1, messages);
        diagnostics[read] = 0;
        fclose(messages);
    }
    asm_test_check(test, okay && bytes.length != 0, "round trip: GNU as assembles every line", diagnostics[0] != 0 ? diagnostics : name);
    unlink(assembly);
    unlink(object);
    return bytes;
}

// --cross-check: GNU as's bytes of each line, in its 16-byte slot, against the matcher's record's; a line where GNU as
// would choose another encoding of the same instruction (x86_assembler_prefers_another_form, the encoder tests' list)
// is counted instead.
static void asm_round_trip_cross_check(AsmTest* test, const char* directory, uint64_t* random)
{
    const uint32_t capacity = 32768;
    FILE* intel = tmpfile();
    FILE* att = tmpfile();
    bool okay = intel != 0 && att != 0;
    if (okay)
    {
        // MPX's bnd prefix (the bnd call and jmp forms), which binutils 2.46 takes only where .arch enables MPX, as the
        // encoder tests' --print-assembly does.
        fputs(".intel_syntax noprefix\n.arch .mpx\n.text\n", intel);
        fputs(".arch .mpx\n.text\n", att);
        // The lines are the round trip's own, kept outside the arenas the paths reset for each line.
        Arena kept = arena_reserve((uint64_t)capacity * sizeof(AsmRoundTripLine));
        AsmRoundTripLine* copies = BUSTER_ARENA_PUSH_ARRAY(&kept, AsmRoundTripLine, capacity);
        const uint32_t count = copies != 0 ? asm_round_trip(test, random, 2, copies, intel, att, capacity) : 0;
        uint32_t compared = 0;
        uint32_t preferred = 0;
        const char* names[] = { "round-trip-intel", "round-trip-att" };
        FILE* files[] = { intel, att };
        for (uint32_t syntax = 0; syntax < 2; syntax += 1)
        {
            test->vector.length = 0;
            const String8 bytes = asm_round_trip_gnu(test, directory, names[syntax], files[syntax]);
            for (uint32_t i = 0; bytes.length != 0 && i < count; i += 1)
            {
                uint8_t ours[BUSTER_X86_ENCODER_CAPACITY(1)];
                const int64_t length = x86_encode_scalar(&copies[i].matched, 1, ours, sizeof(ours), 0, 0);
                const bool inside = (uint64_t)i * 16 + 16 <= bytes.length;
                const uint8_t* theirs = (const uint8_t*)bytes.pointer + (uint64_t)i * 16;
                bool same = inside && length > 0 && memcmp(ours, theirs, (size_t)length) == 0;
                for (int64_t k = length; same && k < 16; k += 1)
                {
                    same = theirs[k] == 0xcc;
                }
                if (!same && x86_assembler_prefers_another_form(&copies[i].matched))
                {
                    preferred += 1;
                }
                else
                {
                    char line[640];
                    if (syntax == 0)
                    {
                        asm_round_trip_intel(&copies[i].original, line, sizeof(line), true);
                    }
                    else
                    {
                        snprintf(line, sizeof(line), "line %u of the AT&T source", i);
                    }
                    if (!same)
                    {
                        asm_round_trip_fail(test, syntax == 0 ? "GNU as encodes the Intel line otherwise" : "GNU as encodes the AT&T line otherwise", line, &copies[i].original);
                    }
                    compared += same;
                }
            }
        }
        printf("assembler round trip against GNU as: %u lines in each syntax, %u the same bytes, %u where GNU as chooses another encoding of the same instruction\n",
               count, compared, preferred);
        arena_release(&kept);
    }
    asm_test_check(test, okay, "round trip cross-check setup", 0);
    if (intel != 0)
    {
        fclose(intel);
    }
    if (att != 0)
    {
        fclose(att);
    }
}

// #1134's instructions: the 47 the issue tried in inline assembly's AT&T spellings (12 admitted before it, 35 refused),
// #1115's scalar SSE, conversions and one-operand shift, the AT&T suffixes' widths (x87's, vectors' and a GPR
// destination's), the condition aliases and the other new operand orders. Each assembles on znver5 by both paths, and
// with --cross-check into GNU as's bytes.
static const char asm_round_trip_issue_att[] =
    "issue_att:\n"
    "rep movsb\nrep stosb\nrep movsq\nlea 8(%rax,%rbx,4), %rcx\nmovzbl %al, %ecx\nmovsbq %al, %rcx\ncmovne %eax, %ecx\nxchg %eax, %ecx\n"
    "lock xaddl %eax, (%rdi)\nsete %al\nimul $7, %eax, %ecx\nmovq %rax, %xmm0\n"
    "bt %eax, %ecx\nbts $3, %rcx\nbtr %rax, (%rdi)\npopcnt %rax, %rcx\ncrc32q %rax, %rcx\ncrc32b %al, %ecx\nrdrand %eax\nrdseed %rax\n"
    "cmpxchg16b (%rdi)\nlock cmpxchg16b (%rdi)\nprefetcht0 (%rax)\nprefetchnta 64(%rax)\nmovnti %eax, (%rdi)\nclflushopt (%rdi)\n"
    "movbe (%rdi), %eax\nadcx %rax, %rcx\nadox %eax, %ecx\nmulx %rax, %rcx, %rdx\nshlx %rax, %rcx, %rdx\nandn %rax, %rcx, %rdx\n"
    "pdep %rax, %rcx, %rdx\naesenc %xmm1, %xmm0\npclmulqdq $0x11, %xmm1, %xmm0\npshufb %xmm1, %xmm0\nsha256rnds2 %xmm0, %xmm1, %xmm2\n"
    "cvtsd2ss %xmm1, %xmm0\nmovd %eax, %xmm0\npshufd $0x1b, %xmm1, %xmm0\npextrd $1, %xmm0, %eax\npinsrq $1, %rax, %xmm0\n"
    "vpbroadcastd %xmm0, %ymm1\nvpermq $0x4e, %ymm1, %ymm2\nvpshufb %ymm1, %ymm2, %ymm3\nvpternlogd $0xff, %zmm1, %zmm2, %zmm3\n"
    "vpcompressd %zmm1, %zmm2{%k1}\n"
    "movss (%rax), %xmm0\nmovsd %xmm1, %xmm0\naddsd %xmm1, %xmm0\nucomisd %xmm1, %xmm0\ncvtsi2sdl (%r8), %xmm1\ncvtsi2sdl -52(%rbp), %xmm0\n"
    "cvtsi2sdq %rax, %xmm0\ncvtsi2ssl %eax, %xmm1\nvcvtsi2sdq %rax, %xmm1, %xmm2\nvmovq %xmm0, %rax\nshrl %ecx\nsarq (%rax)\nroll %eax\n"
    "cvtss2siq (%rax), %rax\ncvtsd2sil %xmm0, %eax\nvcvtpd2dqx (%rax), %xmm0\nvcvtpd2dqy (%rax), %xmm0\nfldt (%rax)\nfistpll (%rax)\n"
    "filds (%rax)\nfadds (%rax)\nfaddl (%rax)\nfiaddl (%rax)\nfnsaves (%rax)\nsetz %al\ncmovzl %eax, %ecx\nvaddps (%rax){1to8}, %ymm1, %ymm2\n"
    "vaddpd 8(%rax){1to2}, %xmm1, %xmm2{%k1}{z}\nvblendvps %xmm4, %xmm3, %xmm2, %xmm1\nblendvps %xmm0, %xmm1, %xmm2\nvpsrld $3, %ymm1, %ymm2\n"
    "vmaskmovps %xmm1, %xmm2, (%rax)\nbextr %eax, %ecx, %edx\nblsr %eax, %ecx\nxchg %eax, %eax\nxchg %rcx, %rax\nstosl\nrep stosl\nlretq\n";
static const char asm_round_trip_issue_intel[] =
    ".intel_syntax noprefix\nissue_intel:\n"
    "bt ecx, eax\nbts rcx, 3\npopcnt rcx, rax\ncrc32 ecx, byte ptr [rax]\nrdrand eax\nmovbe eax, dword ptr [rdi]\nadcx rcx, rax\n"
    "mulx rdx, rcx, rax\nshlx rdx, rcx, rax\nsha256rnds2 xmm2, xmm1, xmm0\nvpternlogd zmm3, zmm2, zmm1, 0xff\nvpcompressd zmmword ptr [rax]{k1}, zmm1\n"
    "cvtsi2sd xmm0, dword ptr [rbp-52]\nshr ecx\nfld tbyte ptr [rax]\nfild word ptr [rax]\nlfs ebx, fword ptr [rax]\nmovsx r8, r8d\n"
    "vaddps ymm2, ymm1, dword ptr [rax]{1to8}\nvblendvps xmm1, xmm2, xmm3, xmm4\nvaddpd ymm9, ymm4, ymmword ptr [rsp+rcx*4+256]\n";

// The checks of #1134's diagnostics, choices and fixtures.
static void asm_round_trip_fixtures(AsmTest* test, const char* directory, bool cross_check)
{
    asm_test_pair(test, (String8){ .pointer = (char*)asm_round_trip_issue_att, .length = sizeof(asm_round_trip_issue_att) - 1 }, 1, X86_ARCH_ZNVER5);
    asm_test_pair(test, (String8){ .pointer = (char*)asm_round_trip_issue_intel, .length = sizeof(asm_round_trip_issue_intel) - 1 }, 1, X86_ARCH_ZNVER5);
    if (cross_check)
    {
        asm_test_oracle(test, asm_round_trip_issue_att, directory, X86_ARCH_ZNVER5);
        asm_test_oracle(test, asm_round_trip_issue_intel, directory, X86_ARCH_ZNVER5);
    }
    // Each diagnostic names the instruction as the source writes it, and says why: an instruction the assembler doesn't
    // have, or whose forms no target allows (AVX512-FP16's, which the target table doesn't classify); one whose features
    // the target lacks; operands no form takes, on any target or on this one; and an AT&T suffix the registers disagree
    // with.
    static const struct
    {
        const char* source;
        const char* error;
        const char* name;
    } diagnostics[] = {
        { "vpfoo %xmm0, %xmm1\n", "instruction not supported", "vpfoo" },
        { "vaddph %zmm1, %zmm2, %zmm3\n", "instruction not supported", "vaddph" },
        { "vpternlogd $1, %zmm1, %zmm2, %zmm3\n", "instruction requires ISA features the target lacks (see -march)", "vpternlogd" },
        { "popcnt %xmm0, %eax\n", "no form of the instruction takes these operands", "popcnt" },
        { "vpaddd %zmm1, %zmm2, %zmm3\n", "no form of the instruction takes these operands on this target", "vpaddd" },
        { "addl %rax, %rbx\n", "mnemonic suffix and register width disagree", "addl" },
        { "crc32w %al, %eax\n", "mnemonic suffix and register width disagree", "crc32w" },
        { ".intel_syntax noprefix\nnop\nvpfoo xmm0, xmm1\n", "instruction not supported", "vpfoo" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(diagnostics); i += 1)
    {
        const AsmResult result = asm_test_pair(test, (String8){ .pointer = (char*)diagnostics[i].source, .length = strlen(diagnostics[i].source) }, 0, X86_ARCH_V3);
        const bool named = result.error != 0 && strcmp(result.error, diagnostics[i].error) == 0 && x86_match_name(result.error_name, diagnostics[i].name);
        asm_test_check(test, named, "a diagnostic names the instruction and why", diagnostics[i].source);
    }
    // The choices among an instruction's encodings, with every feature (the C lowering's index): VEX's before EVEX's though
    // EVEX's compressed displacement is shorter (an AVX instruction isn't AVX-512's), EVEX's vpdpbusd before AVX-VNNI's,
    // which a processor with AVX512-VNNI may lack, and VEX's where the target has no EVEX one; the accumulator's xchg
    // (90+r), but 87's for EAX with itself (90 is NOP, which keeps RAX's upper half), and 48 90 for RAX with itself (a
    // NOP either way, which GNU as writes 90); and a shift of one operand, D1's.
    const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
    const X86Target vnni = { .features = BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(AVXVNNI), .architecture = X86_ARCH_OTHER };
    static const struct
    {
        const char* source;
        bool everything;
        uint8_t length;
        uint8_t bytes[10];
    } choices[] = {
        { "vaddpd 256(%rsp,%rcx,4), %ymm4, %ymm9\n", true, 9, { 0xc5, 0x5d, 0x58, 0x8c, 0x8c, 0x00, 0x01, 0x00, 0x00 } },
        { "xchg %rcx, %rax\n", true, 2, { 0x48, 0x91 } },
        { "vpdpbusd %xmm1, %xmm2, %xmm3\n", true, 6, { 0x62, 0xf2, 0x6d, 0x08, 0x50, 0xd9 } },
        { "vpdpbusd %xmm1, %xmm2, %xmm3\n", false, 5, { 0xc4, 0xe2, 0x69, 0x50, 0xd9 } },
        { "xchg %eax, %eax\n", true, 2, { 0x87, 0xc0 } },
        { "xchg %rax, %rax\n", true, 2, { 0x48, 0x90 } },
        { "xchg %ax, %ax\n", true, 2, { 0x66, 0x90 } },
        { "xchg %ecx, %eax\n", true, 1, { 0x91 } },
        { "shrl %ecx\n", true, 2, { 0xd1, 0xe9 } },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(choices); i += 1)
    {
        x86_match_initialize(test->index, choices[i].everything ? &everything : &vnni);
        test->architecture = X86_ARCH_OTHER;
        X86Instruction record = { 0 };
        bool differ = false;
        const bool assembled = asm_round_trip_assemble(test, choices[i].source, false, &record, &differ);
        uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
        const int64_t length = assembled ? x86_encode_scalar(&record, 1, bytes, sizeof(bytes), 0, 0) : 0;
        asm_test_check(test, length == choices[i].length && memcmp(bytes, choices[i].bytes, choices[i].length) == 0, "the matcher's choice among an instruction's encodings", choices[i].source);
    }
}
