// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// External operands to selected encoder records (#221/#222). This is deliberately separate from X86Instruction:
// a mathematical immediate is not the same thing as bits in an encoding field. The catalogue supplies encodings,
// operand roles and widths; this consumer admits a documented subset whose immediate interpretation is known here.
// Initialize one caller-owned index for one X86Target, then probe a mnemonic bucket, never the whole catalogue
// per instruction. Target-ineligible forms are excluded at initialization, including from the scalar path.
// Candidate operand templates and immediate ranges are compared sixteen at a time, with an exact scalar twin.
// Both paths run x86_validate at the frontend boundary. No compiler expression optimization is done here.

#define BUSTER_X86_MATCH_BUCKETS 512
#define BUSTER_X86_MATCH_OPERANDS 4

typedef enum X86OperandKind
{
    X86_OPERAND_NONE,
    X86_OPERAND_REGISTER,
    X86_OPERAND_MEMORY,
    X86_OPERAND_IMMEDIATE,
} X86OperandKind;

typedef struct X86Operand X86Operand;
struct X86Operand
{
    __int128 value;
    uint16_t width;
    uint8_t kind;
    uint8_t class;
    uint8_t reg;
    uint8_t base;
    uint8_t index;
    uint8_t mode;
    uint8_t segment;
    bool symbolic;
};

typedef struct X86MatchRequest X86MatchRequest;
struct X86MatchRequest
{
    String8 mnemonic;
    X86Operand operands[BUSTER_X86_MATCH_OPERANDS];
    uint8_t count;
    uint8_t width;
    uint8_t decorations;
    uint8_t broadcast;
    bool rounding;
};

typedef struct X86MatchIndex X86MatchIndex;
struct X86MatchIndex
{
    // Open-addressed mnemonic dictionary. Each occupied slot owns exactly one canonical name's contiguous forms.
    // More slots than the admitted spelling lists can name; aliases movabs/pushw share mov/push's slot.
    String8 names[BUSTER_X86_MATCH_BUCKETS];
    uint32_t begin[BUSTER_X86_MATCH_BUCKETS + 1];
    uint32_t forms[X86_FORM_COUNT];
    uint32_t counts[X86_FORM_COUNT];
    // Per-operand matching templates, contiguous within each bucket: kinds in bits 0..3, register class in 4..7,
    // memory width in 8..15, role in 16..23, and a required register plus one in 24..31 (zero means any).
    uint32_t operands[4][X86_FORM_COUNT];
    uint8_t allowed[X86_FORM_COUNT];
    uint32_t broadcast[X86_FORM_COUNT];
    // Width (including implicit qword) in bits 0..7; spelling, family and EVEX predicates above it.
    uint32_t attributes[X86_FORM_COUNT];
    int64_t immediate_min[X86_FORM_COUNT];
    uint64_t immediate_max[X86_FORM_COUNT];
};

#define BUSTER_X86_MATCH_MOVABS (1u << 8)
#define BUSTER_X86_MATCH_PUSH_WORD (1u << 9)
#define BUSTER_X86_MATCH_EVEX (1u << 10)
#define BUSTER_X86_MATCH_ROUNDING (1u << 11)
#define BUSTER_X86_MATCH_FORBIDDEN (1u << 12)
#define BUSTER_X86_MATCH_PUSHW_NAME (1u << 13)

typedef struct X86Match X86Match;
struct X86Match
{
    X86Instruction instruction;
    uint32_t length;
    bool found;
    bool ambiguous;
};

enum
{
    X86_MATCH_RM = 1,
    X86_MATCH_REG,
    X86_MATCH_VVVV,
    X86_MATCH_IMM,
    X86_MATCH_REL,
    X86_MATCH_ACC,
    X86_MATCH_CL,
    X86_MATCH_ONE,
};

static uint32_t x86_match_roles(uint8_t syntax, uint8_t roles[4])
{
    uint32_t count = UINT32_MAX;
    switch (syntax)
    {
    case X86_SYNTAX_NONE:
        count = 0;
        break;
    case X86_SYNTAX_RM:
        roles[0] = X86_MATCH_RM;
        count = 1;
        break;
    case X86_SYNTAX_REG:
        roles[0] = X86_MATCH_REG;
        count = 1;
        break;
    case X86_SYNTAX_IMM:
        roles[0] = X86_MATCH_IMM;
        count = 1;
        break;
    case X86_SYNTAX_REL:
        roles[0] = X86_MATCH_REL;
        count = 1;
        break;
    case X86_SYNTAX_RM_REG:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_REG;
        count = 2;
        break;
    case X86_SYNTAX_REG_RM:
        roles[0] = X86_MATCH_REG;
        roles[1] = X86_MATCH_RM;
        count = 2;
        break;
    case X86_SYNTAX_RM_IMM:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_IMM;
        count = 2;
        break;
    case X86_SYNTAX_ACC_IMM:
        roles[0] = X86_MATCH_ACC;
        roles[1] = X86_MATCH_IMM;
        count = 2;
        break;
    case X86_SYNTAX_RM_CL:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_CL;
        count = 2;
        break;
    case X86_SYNTAX_RM_ONE:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_ONE;
        count = 2;
        break;
    case X86_SYNTAX_REG_RM_IMM:
        roles[0] = X86_MATCH_REG;
        roles[1] = X86_MATCH_RM;
        roles[2] = X86_MATCH_IMM;
        count = 3;
        break;
    case X86_SYNTAX_RM_REG_IMM:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_REG;
        roles[2] = X86_MATCH_IMM;
        count = 3;
        break;
    case X86_SYNTAX_RM_REG_CL:
        roles[0] = X86_MATCH_RM;
        roles[1] = X86_MATCH_REG;
        roles[2] = X86_MATCH_CL;
        count = 3;
        break;
    case X86_SYNTAX_REG_VVVV_RM:
        roles[0] = X86_MATCH_REG;
        roles[1] = X86_MATCH_VVVV;
        roles[2] = X86_MATCH_RM;
        count = 3;
        break;
    case X86_SYNTAX_REG_VVVV_RM_IMM:
        roles[0] = X86_MATCH_REG;
        roles[1] = X86_MATCH_VVVV;
        roles[2] = X86_MATCH_RM;
        roles[3] = X86_MATCH_IMM;
        count = 4;
        break;
    default:
        break;
    }
    return count;
}

static bool x86_match_name(String8 name, const char* text)
{
    return name.length == strlen(text) && memcmp(name.pointer, text, name.length) == 0;
}

static uint32_t x86_match_hash(String8 name)
{
    // The caller supplies the canonical spelling, including movabs -> mov and pushw -> push.
    uint32_t hash = 0;
    for (uint64_t i = 0; i < name.length; i += 1)
    {
        hash = hash * 33 + (uint8_t)name.pointer[i];
    }
    return hash & (BUSTER_X86_MATCH_BUCKETS - 1);
}

static String8 x86_match_canonical(String8 name)
{
    if (x86_match_name(name, "movabs"))
    {
        name = BUSTER_S8("mov");
    }
    else if (x86_match_name(name, "pushw"))
    {
        name = BUSTER_S8("push");
    }
    return name;
}

static uint32_t x86_match_bucket(const X86MatchIndex* index, String8 name)
{
    name = x86_match_canonical(name);
    uint32_t at = x86_match_hash(name);
    for (uint32_t probes = 0; probes < BUSTER_X86_MATCH_BUCKETS; probes += 1)
    {
        const String8 candidate = index->names[at];
        if (candidate.pointer == 0 || (candidate.length == name.length && memcmp(candidate.pointer, name.pointer, name.length) == 0))
        {
            break;
        }
        at = (at + 1) & (BUSTER_X86_MATCH_BUCKETS - 1);
    }
    return at;
}

// Syntax and immediate semantics understood by this frontend. ISA eligibility is a separate intersection with
// x86_target_forms at initialization. Adding an encoder form does not automatically admit its frontend syntax.
static bool x86_match_supported(const X86FormDescription* form)
{
    static const char* const integer[] =
        {
            "mov",
            "movabs",
            "lea",
            "movzx",
            "movsx",
            "movsxd",
            "add",
            "adc",
            "sub",
            "sbb",
            "and",
            "or",
            "xor",
            "cmp",
            "test",
            "inc",
            "dec",
            "neg",
            "not",
            "mul",
            "imul",
            "div",
            "idiv",
            "shl",
            "shr",
            "sar",
            "sal",
            "rol",
            "ror",
            "shld",
            "shrd",
            "push",
            "pushw",
            "pop",
            "call",
            "jmp",
            "ret",
            "leave",
            "nop",
            "int3",
            "ud2",
            "syscall",
            "clc",
            "stc",
            "cmc",
            "cld",
            "std",
            "cbw",
            "cwde",
            "cdqe",
            "cwd",
            "cdq",
            "cqo",
            "loop",
            "loope",
            "loopne",
            "jrcxz",
    };
    static const char* const sse[] =
        {
            "movaps",
            "movups",
            "movapd",
            "movupd",
            "movdqa",
            "movdqu",
            "addps",
            "addpd",
            "subps",
            "subpd",
            "mulps",
            "mulpd",
            "divps",
            "divpd",
            "xorps",
            "xorpd",
            "pxor",
            "paddd",
            "psubd",
    };
    static const char* const conditions[] =
        {
            "o",
            "no",
            "b",
            "ae",
            "e",
            "ne",
            "be",
            "a",
            "s",
            "ns",
            "p",
            "np",
            "l",
            "ge",
            "le",
            "g",
    };
    const char* name = form->mnemonic;
    bool allowed = false;
    if (form->family == BUSTER_X86_FAMILY_LEGACY)
    {
        for (uint32_t i = 0; !allowed && i < BUSTER_ARRAY_LENGTH(integer); i += 1)
        {
            allowed = name[0] == integer[i][0] && strcmp(name, integer[i]) == 0;
        }
        for (uint32_t i = 0; !allowed && i < BUSTER_ARRAY_LENGTH(sse); i += 1)
        {
            allowed = name[0] == sse[i][0] && strcmp(name, sse[i]) == 0;
        }
        const char* suffix = strncmp(name, "cmov", 4) == 0 ? name + 4 : strncmp(name, "set", 3) == 0 ? name + 3
                                                                    : name[0] == 'j'                 ? name + 1
                                                                                                     : "";
        for (uint32_t i = 0; !allowed && suffix[0] != 0 && i < BUSTER_ARRAY_LENGTH(conditions); i += 1)
        {
            allowed |= strcmp(suffix, conditions[i]) == 0;
        }
    }
    else if (form->family == BUSTER_X86_FAMILY_VEX && name[0] == 'v')
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sse); i += 1)
        {
            allowed |= name[1] == sse[i][0] && strcmp(name + 1, sse[i]) == 0;
        }
        allowed |= strcmp(name, "vzeroupper") == 0;
    }
    else if (form->family == BUSTER_X86_FAMILY_EVEX && form->vector_length == 2 && name[0] == 'v')
    {
        // 512-bit floating arithmetic/moves and dword integer arithmetic are AVX-512F. No implicit VL/BW/DQ.
        for (uint32_t i = 0; i < 14; i += 1)
        {
            allowed |= i != 4 && i != 5 && name[1] == sse[i][0] && strcmp(name + 1, sse[i]) == 0;
        }
        allowed |= strcmp(name, "vpaddd") == 0 || strcmp(name, "vpsubd") == 0 || strcmp(name, "vpxord") == 0;
        allowed |= strcmp(name, "vmovdqa32") == 0 || strcmp(name, "vmovdqu32") == 0;
    }
    // Near call/jump syntax must never select a far-pointer memory form merely because its memory width fits.
    allowed &= !((strcmp(name, "call") == 0 || strcmp(name, "jmp") == 0) && (form->encoding == BUSTER_X86_ENCODING_D3 || form->encoding == BUSTER_X86_ENCODING_D5));
    return allowed;
}

// Whether a form sign-extends its immediate into a wider operand: the imm8 of 83, 6B and 6A, and an imm32 into 64 bits
// (REX.W, or PUSH's default operand size). Such a field holds a signed value, which its range and its relocation (32S)
// follow.
static bool x86_match_sign_extends(const X86FormDescription* form)
{
    const uint32_t opcode = form->opcode & 0xff;
    return form->family == BUSTER_X86_FAMILY_LEGACY && (opcode == 0x83 || opcode == 0x6b || opcode == 0x6a || (form->immediate_bytes == 4 && (form->w || strcmp(form->mnemonic, "push") == 0)));
}

static void x86_match_initialize(X86MatchIndex* index, const X86Target* target)
{
    memset(index, 0, sizeof(*index));
    x86_target_forms(target, index->allowed);
    bool admitted[BUSTER_X86_FORM_LEGACY_MODES];
    uint16_t buckets[BUSTER_X86_FORM_LEGACY_MODES];
    for (uint32_t f = 0; f < BUSTER_X86_FORM_LEGACY_MODES; f += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[f];
        uint8_t roles[4];
        admitted[f] = index->allowed[f] && x86_match_supported(form) && x86_match_roles(form->syntax, roles) <= 4;
        if (admitted[f])
        {
            const String8 name = x86_match_canonical((String8){.pointer = (char*)form->mnemonic, .length = strlen(form->mnemonic)});
            const uint32_t bucket = x86_match_bucket(index, name);
            index->names[bucket] = name;
            buckets[f] = bucket;
            index->begin[bucket + 1] += 1;
        }
    }
    for (uint32_t b = 0; b < BUSTER_X86_MATCH_BUCKETS; b += 1)
    {
        index->begin[b + 1] += index->begin[b];
    }
    uint32_t cursors[BUSTER_X86_MATCH_BUCKETS];
    memcpy(cursors, index->begin, sizeof(cursors));
    for (uint32_t f = 0; f < BUSTER_X86_FORM_LEGACY_MODES; f += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[f];
        uint8_t roles[4];
        const uint32_t count = x86_match_roles(form->syntax, roles);
        if (admitted[f])
        {
            const uint32_t at = cursors[buckets[f]]++;
            index->forms[at] = f;
            index->counts[at] = count;
            index->broadcast[at] = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? 64 / form->disp8_scale : 0;
            const bool implicit_qword = form->operand_bytes == 0 && (strcmp(form->mnemonic, "call") == 0 || strcmp(form->mnemonic, "jmp") == 0 || strcmp(form->mnemonic, "ret") == 0 || strcmp(form->mnemonic, "leave") == 0);
            index->attributes[at] = (implicit_qword ? 8 : form->operand_bytes) |
                (strcmp(form->mnemonic, "movabs") == 0 ? BUSTER_X86_MATCH_MOVABS : 0) |
                (strcmp(form->mnemonic, "pushw") == 0 ? BUSTER_X86_MATCH_PUSHW_NAME : 0) |
                (form->operand_bytes == 2 && (strcmp(form->mnemonic, "pushw") == 0 || strcmp(form->mnemonic, "push") == 0) ? BUSTER_X86_MATCH_PUSH_WORD : 0) |
                (form->family == BUSTER_X86_FAMILY_EVEX ? BUSTER_X86_MATCH_EVEX : 0) |
                ((form->evex & BUSTER_X86_EVEX_ROUNDING) != 0 ? BUSTER_X86_MATCH_ROUNDING : 0) |
                ((form->evex & (BUSTER_X86_EVEX_SAE | BUSTER_X86_EVEX_ND | BUSTER_X86_EVEX_NF | BUSTER_X86_EVEX_SOURCE_CONDITION)) != 0 ? BUSTER_X86_MATCH_FORBIDDEN : 0);
            const uint32_t bytes = form->immediate_bytes;
            const bool extended = x86_match_sign_extends(form);
            index->immediate_min[at] = bytes != 0 ? (int64_t)(-((__int128)1 << (8 * bytes - 1))) : 0;
            index->immediate_max[at] = bytes != 0 ? (uint64_t)(((__int128)1 << (8 * bytes - (extended ? 1 : 0))) - 1) : 0;
            for (uint32_t operand = 0; operand < count; operand += 1)
            {
                const uint32_t role = roles[operand];
                const uint16_t descriptor = role == X86_MATCH_RM ? form->rm : role == X86_MATCH_REG ? form->reg
                                                                          : role == X86_MATCH_VVVV  ? form->vvvv
                                                                                                    : 0;
                uint32_t kinds = ((descriptor & BUSTER_X86_OPERAND_REGISTER) ? 1u << X86_OPERAND_REGISTER : 0) | ((descriptor & BUSTER_X86_OPERAND_MEMORY) ? 1u << X86_OPERAND_MEMORY : 0);
                uint32_t class = descriptor & BUSTER_X86_OPERAND_CLASS;
                uint32_t fixed = 0;
                if (role == X86_MATCH_IMM || role == X86_MATCH_REL || role == X86_MATCH_ONE)
                {
                    kinds = 1u << X86_OPERAND_IMMEDIATE;
                }
                else if (role == X86_MATCH_ACC || role == X86_MATCH_CL)
                {
                    kinds = 1u << X86_OPERAND_REGISTER;
                    class = role == X86_MATCH_CL ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8
                                                                       : form->operand_bytes == 2   ? BUSTER_X86_CLASS_GPR16
                                                                       : form->operand_bytes == 4   ? BUSTER_X86_CLASS_GPR32
                                                                                                    : BUSTER_X86_CLASS_GPR64;
                    fixed = role == X86_MATCH_CL ? 2 : 1;
                }
                const uint32_t width = (form->evex & BUSTER_X86_EVEX_BROADCAST) && role == X86_MATCH_RM ? form->disp8_scale : form->operand_bytes;
                index->operands[operand][at] = kinds | class << 4 | width << 8 | role << 16 | fixed << 24;
            }
        }
    }
}

static bool x86_match_immediate(const X86FormDescription* form, const X86Operand* operand)
{
    const uint32_t bytes = form->immediate_bytes;
    const bool extended = x86_match_sign_extends(form);
    const __int128 low = bytes != 0 ? -((__int128)1 << (bytes * 8 - 1)) : 0;
    const __int128 high = bytes != 0 ? (((__int128)1 << (bytes * 8 - (extended ? 1 : 0))) - 1) : 0;
    return operand->kind == X86_OPERAND_IMMEDIATE && bytes != 0 && (operand->symbolic ? bytes >= 4 : operand->value >= low && operand->value <= high);
}

static bool x86_match_operand(const X86Operand* operand, uint16_t descriptor, uint8_t width, X86Instruction* instruction, uint8_t role)
{
    bool result = false;
    if (operand->kind == X86_OPERAND_REGISTER)
    {
        result = (descriptor & BUSTER_X86_OPERAND_REGISTER) != 0 && operand->class == (descriptor & BUSTER_X86_OPERAND_CLASS);
        if (role == X86_MATCH_RM)
        {
            instruction->rm = operand->reg;
        }
        else if (role == X86_MATCH_REG)
        {
            instruction->reg = operand->reg;
        }
        else
        {
            instruction->vvvv = operand->reg;
        }
    }
    else if (role == X86_MATCH_RM && operand->kind == X86_OPERAND_MEMORY)
    {
        result = (descriptor & BUSTER_X86_OPERAND_MEMORY) != 0 && (operand->width == 0 || operand->width == width);
        result &= operand->symbolic || (operand->value >= INT32_MIN && operand->value <= INT32_MAX);
        instruction->rm = operand->base;
        instruction->index = operand->index;
        instruction->mode = operand->mode;
        instruction->displacement = operand->symbolic ? INT32_MIN : (int32_t)operand->value;
        instruction->form += operand->segment == 1 ? BUSTER_X86_FORM_FS : operand->segment == 2 ? BUSTER_X86_FORM_GS
                                                                                                : 0;
    }
    return result;
}

static bool x86_match_candidate(const X86MatchRequest* request, uint32_t f, X86Instruction* instruction)
{
    const X86FormDescription* form = &x86_form_descriptions[f];
    uint8_t roles[4];
    // The target-specific index contains only supported, eligible forms. pushw is also push's 16-bit register and
    // memory forms, as in GNU as.
    bool result = x86_match_name(request->mnemonic, form->mnemonic) || (x86_match_name(request->mnemonic, "mov") && strcmp(form->mnemonic, "movabs") == 0) || (x86_match_name(request->mnemonic, "pushw") && strcmp(form->mnemonic, "push") == 0 && form->operand_bytes == 2);
    result &= x86_match_roles(form->syntax, roles) == request->count;
    const bool implicit_qword = form->operand_bytes == 0 && (strcmp(form->mnemonic, "call") == 0 || strcmp(form->mnemonic, "jmp") == 0 || strcmp(form->mnemonic, "ret") == 0 || strcmp(form->mnemonic, "leave") == 0);
    result &= request->width == 0 || request->width == form->operand_bytes || (request->width == 8 && implicit_qword);
    result &= ((form->evex & BUSTER_X86_EVEX_BROADCAST) != 0) == (request->broadcast != 0);
    result &= ((form->evex & BUSTER_X86_EVEX_ROUNDING) != 0) == request->rounding;
    result &= (form->evex & (BUSTER_X86_EVEX_SAE | BUSTER_X86_EVEX_ND | BUSTER_X86_EVEX_NF | BUSTER_X86_EVEX_SOURCE_CONDITION)) == 0;
    if (request->decorations != 0 && form->family != BUSTER_X86_FAMILY_EVEX)
    {
        result = false;
    }
    *instruction = (X86Instruction){.form = f, .decorations = request->decorations};
    for (uint32_t i = 0; result && i < request->count; i += 1)
    {
        const X86Operand* operand = &request->operands[i];
        const uint8_t role = roles[i];
        switch (role)
        {
        case X86_MATCH_REG:
            result = x86_match_operand(operand, form->reg, form->operand_bytes, instruction, role);
            break;
        case X86_MATCH_RM:
            result = x86_match_operand(operand, form->rm, (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : form->operand_bytes, instruction, role);
            break;
        case X86_MATCH_VVVV:
            result = x86_match_operand(operand, form->vvvv, form->operand_bytes, instruction, role);
            break;
        case X86_MATCH_IMM:
        {
            result = x86_match_immediate(form, operand);
            instruction->immediate = operand->symbolic ? 0 : (int32_t)(uint64_t)operand->value;
            if (form->immediate_bytes == 8)
            {
                instruction->displacement = operand->symbolic ? 0 : (int32_t)((uint64_t)operand->value >> 32);
            }
        }
        break;
        case X86_MATCH_REL:
        {
            result = operand->kind == X86_OPERAND_IMMEDIATE && operand->symbolic;
            instruction->displacement = 0;
        }
        break;
        case X86_MATCH_ACC:
            result = operand->kind == X86_OPERAND_REGISTER && operand->class <= BUSTER_X86_CLASS_GPR64 && operand->reg == 0 && operand->width == form->operand_bytes;
            break;
        case X86_MATCH_CL:
            result = operand->kind == X86_OPERAND_REGISTER && operand->class == BUSTER_X86_CLASS_GPR8 && operand->reg == 1;
            break;
        case X86_MATCH_ONE:
            result = operand->kind == X86_OPERAND_IMMEDIATE && !operand->symbolic && operand->value == 1;
            break;
        default:
            result = false;
            break;
        }
    }
    if (result && request->broadcast != 0)
    {
        result = request->broadcast == (64u / form->disp8_scale);
    }
    result = result && x86_validate(instruction, 1) == 1;
    return result;
}

// Sixteen candidate forms of one mnemonic bucket. All operand kind/class/width, fixed-register, ISA, broadcast and
// immediate-extension predicates are evaluated in columns. Only survivors construct records and run the encoder's
// detailed legality boundary (high-byte/REX combinations, address and EVEX restrictions). The scalar path below does
// not use these templates: its independent semantic matcher is also their oracle.
static uint32_t x86_match_candidates_vector(const X86MatchIndex* index, const X86MatchRequest* request, uint32_t first, uint32_t count)
{
    const __mmask16 valid = (1u << count) - 1;
    __mmask16 mask = _mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, index->counts + first), _mm512_set1_epi32(request->count));
    mask &= _mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, index->broadcast + first), _mm512_set1_epi32(request->broadcast));
    const __m512i attributes = _mm512_maskz_loadu_epi32(valid, index->attributes + first);
    if (request->width != 0)
    {
        mask &= _mm512_cmpeq_epi32_mask(_mm512_and_epi32(attributes, _mm512_set1_epi32(255)), _mm512_set1_epi32(request->width));
    }
    const uint32_t spelling = x86_match_name(request->mnemonic, "movabs") ? BUSTER_X86_MATCH_MOVABS : x86_match_name(request->mnemonic, "pushw") ? BUSTER_X86_MATCH_PUSH_WORD : 0;
    const uint32_t required = spelling | (request->decorations != 0 ? BUSTER_X86_MATCH_EVEX : 0) | (request->rounding ? BUSTER_X86_MATCH_ROUNDING : 0);
    const uint32_t checked = required | BUSTER_X86_MATCH_ROUNDING | BUSTER_X86_MATCH_FORBIDDEN | (spelling == BUSTER_X86_MATCH_PUSH_WORD ? 0 : BUSTER_X86_MATCH_PUSHW_NAME);
    mask &= _mm512_cmpeq_epi32_mask(_mm512_and_epi32(attributes, _mm512_set1_epi32(checked)), _mm512_set1_epi32(required));
    for (uint32_t operand = 0; mask != 0 && operand < request->count; operand += 1)
    {
        const X86Operand* value = &request->operands[operand];
        const __m512i templates = _mm512_maskz_loadu_epi32(valid, index->operands[operand] + first);
        mask &= _mm512_test_epi32_mask(templates, _mm512_set1_epi32(1u << value->kind));
        const __m512i roles = _mm512_and_epi32(_mm512_srli_epi32(templates, 16), _mm512_set1_epi32(255));
        if (value->kind == X86_OPERAND_REGISTER)
        {
            const __m512i classes = _mm512_and_epi32(_mm512_srli_epi32(templates, 4), _mm512_set1_epi32(15));
            const __m512i fixed = _mm512_srli_epi32(templates, 24);
            const __mmask16 accumulator = _mm512_cmpeq_epi32_mask(roles, _mm512_set1_epi32(X86_MATCH_ACC));
            const __mmask16 class_match = _mm512_cmpeq_epi32_mask(classes, _mm512_set1_epi32(value->class));
            // The scalar accumulator rule sizes its implicit register from width, even for a malformed direct
            // request whose class disagrees. Keep that predicate exact; the parser itself emits consistent pairs.
            mask &= (accumulator & (value->class <= BUSTER_X86_CLASS_GPR64 ? 65535u : 0)) | (~accumulator & class_match);
            mask &= _mm512_cmpeq_epi32_mask(fixed, _mm512_setzero_si512()) | _mm512_cmpeq_epi32_mask(fixed, _mm512_set1_epi32(value->reg + 1));
            const __m512i widths = _mm512_and_epi32(_mm512_srli_epi32(templates, 8), _mm512_set1_epi32(255));
            mask &= ~accumulator | _mm512_cmpeq_epi32_mask(widths, _mm512_set1_epi32(value->width));
        }
        else if (value->kind == X86_OPERAND_MEMORY)
        {
            if (value->width != 0)
            {
                const __m512i widths = _mm512_and_epi32(_mm512_srli_epi32(templates, 8), _mm512_set1_epi32(255));
                mask &= _mm512_cmpeq_epi32_mask(widths, _mm512_set1_epi32(value->width));
            }
            if (!value->symbolic && (value->value < INT32_MIN || value->value > INT32_MAX))
            {
                mask = 0;
            }
        }
        else if (value->kind == X86_OPERAND_IMMEDIATE)
        {
            if (!value->symbolic && (value->value < INT64_MIN || value->value > (__int128)UINT64_MAX))
            {
                mask = 0;
            }
            const __mmask16 relative = _mm512_cmpeq_epi32_mask(roles, _mm512_set1_epi32(X86_MATCH_REL));
            const __mmask16 one = _mm512_cmpeq_epi32_mask(roles, _mm512_set1_epi32(X86_MATCH_ONE));
            uint32_t fits = 0;
            for (uint32_t half = 0; half < 2; half += 1)
            {
                const __mmask8 lanes = valid >> (half * 8);
                const __m512i low = _mm512_maskz_loadu_epi64(lanes, index->immediate_min + first + half * 8);
                const __m512i high = _mm512_maskz_loadu_epi64(lanes, index->immediate_max + first + half * 8);
                const __m512i bits = _mm512_set1_epi64((uint64_t)value->value);
                const __mmask8 accepted = value->symbolic ? _mm512_cmpge_epu64_mask(high, _mm512_set1_epi64(INT32_MAX)) : value->value < 0 ? _mm512_cmpge_epi64_mask(bits, low)
                                                                                                                                           : _mm512_cmple_epu64_mask(bits, high);
                fits |= (uint32_t)(accepted & lanes) << (half * 8);
            }
            fits &= ~(relative | one);
            fits |= value->symbolic ? relative : value->value == 1 ? one
                                                                   : 0;
            mask &= fits;
        }
    }
    return mask;
}

// Only the vector predicate's survivors reach here. The role columns replace decoding the syntax and repeating
// every operand/name comparison. Full encoder legality still runs before any untrusted record can be encoded.
// x86_match_candidate remains the independent scalar semantic matcher.
static bool x86_match_candidate_indexed(const X86MatchIndex* index, const X86MatchRequest* request, uint32_t at, X86Instruction* instruction)
{
    const uint32_t f = index->forms[at];
    *instruction = (X86Instruction){.form = f, .decorations = request->decorations};
    for (uint32_t i = 0; i < request->count; i += 1)
    {
        const X86Operand* operand = &request->operands[i];
        const uint32_t role = (index->operands[i][at] >> 16) & 255;
        if (role == X86_MATCH_RM)
        {
            if (operand->kind == X86_OPERAND_MEMORY)
            {
                instruction->rm = operand->base;
                instruction->index = operand->index;
                instruction->mode = operand->mode;
                instruction->displacement = operand->symbolic ? INT32_MIN : (int32_t)operand->value;
                instruction->form += operand->segment == 1 ? BUSTER_X86_FORM_FS : operand->segment == 2 ? BUSTER_X86_FORM_GS : 0;
            }
            else
            {
                instruction->rm = operand->reg;
            }
        }
        else if (role == X86_MATCH_REG)
        {
            instruction->reg = operand->reg;
        }
        else if (role == X86_MATCH_VVVV)
        {
            instruction->vvvv = operand->reg;
        }
        else if (role == X86_MATCH_IMM)
        {
            instruction->immediate = operand->symbolic ? 0 : (int32_t)(uint64_t)operand->value;
            if (x86_form_descriptions[f].immediate_bytes == 8)
            {
                instruction->displacement = operand->symbolic ? 0 : (int32_t)((uint64_t)operand->value >> 32);
            }
        }
    }
    return x86_validate(instruction, 1) == 1;
}

static X86Match x86_match(const X86MatchIndex* index, const X86MatchRequest* request, bool vectorized)
{
    X86Match result = {0};
    const uint32_t bucket = x86_match_bucket(index, request->mnemonic);
    uint32_t memory_width = 0;
    bool unsized_memory = false;
    for (uint32_t i = 0; i < request->count; i += 1)
    {
        unsized_memory |= request->operands[i].kind == X86_OPERAND_MEMORY && request->operands[i].width == 0;
    }
    for (uint32_t first = index->begin[bucket]; first < index->begin[bucket + 1]; first += 16)
    {
        const uint32_t left = index->begin[bucket + 1] - first;
        const uint32_t n = left < 16 ? left : 16;
        uint32_t mask = 0;
        if (vectorized)
        {
            mask = x86_match_candidates_vector(index, request, first, n);
        }
        else
        {
            for (uint32_t i = 0; i < n; i += 1)
            {
                mask |= (uint32_t)(index->counts[first + i] == request->count) << i;
            }
        }
        while (mask != 0)
        {
            const uint32_t at = first + __builtin_ctz(mask);
            mask &= mask - 1;
            X86Instruction instruction;
            const bool accepted = vectorized ? x86_match_candidate_indexed(index, request, at, &instruction) : x86_match_candidate(request, index->forms[at], &instruction);
            if (accepted)
            {
                const X86FormDescription* form = x86_form_description(instruction.form);
                if (unsized_memory)
                {
                    result.ambiguous |= memory_width != 0 && memory_width != form->operand_bytes;
                    memory_width = form->operand_bytes;
                }
                uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
                // The indexed builder has already crossed the complete x86_validate boundary. The scalar API
                // would run that same validation a second time merely to get this length.
                const int64_t length = vectorized ? encode_one(&instruction, bytes, X86_ENCODER_MODE_64) : x86_encode_scalar(&instruction, 1, bytes, sizeof(bytes), 0, 0);
                if (length > 0 && (!result.found || length < result.length))
                {
                    result.instruction = instruction;
                    result.length = (uint32_t)length;
                    result.found = true;
                }
            }
        }
    }
    result.found &= !result.ambiguous;
    return result;
}
