// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Formatting plans for validated decoder records. The independent scalar formatter above this include retains
// the original decisions and bounded fragment writer. Plans resolve catalogue-only choices once, while register
// tokens carry their byte length. A proved per-form bound admits direct stores; smaller buffers use the scalar
// writer. No allocation or variable-length string scan occurs while formatting a record.

#define BUSTER_X86_TEXT_EVEX 1u
#define BUSTER_X86_TEXT_VEX 2u
#define BUSTER_X86_TEXT_DIRECTION 4u
#define BUSTER_X86_TEXT_STORE 8u
#define BUSTER_X86_TEXT_DISP32 16u
#define BUSTER_X86_TEXT_ADDRESS_REGISTER 32u
#define BUSTER_X86_TEXT_ROUNDING 64u
#define BUSTER_X86_TEXT_ROUNDING_MODE 128u
#define BUSTER_X86_TEXT_SIB 256u
#define BUSTER_X86_TEXT_VSIB 512u

typedef struct X86DisassemblyPlan X86DisassemblyPlan;
struct X86DisassemblyPlan
{
    const char* mnemonic;
    uint64_t operands;
    uint64_t memory;
    uint16_t flags;
    uint16_t bound;
    uint8_t mnemonic_length;
    uint8_t reg;
    uint8_t rm;
    uint8_t vvvv;
    uint8_t accumulator;
    uint8_t immediate;
    uint8_t broadcast;
};

static X86DisassemblyPlan x86_disassembly_plans[X86_FORM_COUNT];
static uint64_t x86_disassembly_registers[16][64];

static uint64_t x86_disassembly_token(const char* bytes, uint32_t length)
{
    uint64_t token = (uint64_t)length << 56;
    memcpy(&token, bytes, length);
    return token;
}

static void x86_disassembly_initialize(void)
{
    static const char* const syntax[] =
    {
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
    for (uint32_t kind = 1; kind < 16; kind += 1)
    {
        for (uint32_t number = 0; number < 32; number += 1)
        {
            if (kind != BUSTER_X86_CLASS_SEGMENT || number < 6)
            {
                char spelling[16];
                X86DisassemblyText text = {.data = spelling, .capacity = sizeof(spelling)};
                x86_disassembly_register(&text, number, kind);
                x86_disassembly_registers[kind][number] = x86_disassembly_token(spelling, (uint32_t)text.length);
                if (kind <= BUSTER_X86_CLASS_GPR64)
                {
                    text.length = 0;
                    x86_disassembly_register(&text, number | BUSTER_X86_REGISTER_HIGH_BYTE, kind);
                    x86_disassembly_registers[kind][number + 32] = x86_disassembly_token(spelling, (uint32_t)text.length);
                }
            }
        }
    }
    for (uint32_t row = 1; row < X86_FORM_COUNT; row += 1)
    {
        if (x86_decode_form_supported(row))
        {
            const X86FormDescription* form = &x86_form_descriptions[row];
            const char* operands = form->syntax < BUSTER_ARRAY_LENGTH(syntax) ? syntax[form->syntax] : 0;
            const uint32_t mnemonic_length = (uint32_t)strlen(form->mnemonic);
            const uint32_t count = operands != 0 ? (uint32_t)strlen(operands) : 8;
            X86DisassemblyPlan* plan = &x86_disassembly_plans[row];
            if (mnemonic_length < 256 && count < 8)
            {
                plan->mnemonic = form->mnemonic;
                plan->mnemonic_length = (uint8_t)mnemonic_length;
                memcpy(&plan->operands, operands, count);
                // Prefixes total <=31 bytes; each operand, including memory and a separator, <=66. Decorations
                // and rounding total <=32. Eight extra bytes permit a packed token store past its logical end.
                // Hex stores mask out leading and trailing bytes. The register/memory/number bounds follow
                // their validated fields; the largest memory spelling is below the 64-byte operand allowance.
                plan->bound = (uint16_t)(mnemonic_length + 31 + count * 66 + 32 + 8);
                plan->reg = form->reg & BUSTER_X86_OPERAND_CLASS;
                plan->rm = form->rm & BUSTER_X86_OPERAND_CLASS;
                plan->vvvv = form->vvvv & BUSTER_X86_OPERAND_CLASS;
                plan->accumulator = form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 2 ? BUSTER_X86_CLASS_GPR16 : form->operand_bytes == 4 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
                plan->immediate = form->immediate_bytes == 8 ? 2 : form->immediate_bytes == 1 && ((uint8_t)form->opcode == 0x83 || (uint8_t)form->opcode == 0x6b || (uint8_t)form->opcode == 0x6a) ? 1 : form->family != BUSTER_X86_FAMILY_LEGACY && x86_opcode_map(form) == 7 ? 3 : 0;
                const uint32_t bytes = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : form->operand_bytes;
                const char* size = bytes == 1 ? "byte" : bytes == 2 ? "word" : bytes == 4 ? "dword" : bytes == 6 ? "fword" : bytes == 8 ? "qword" : bytes == 10 ? "tbyte" : bytes == 16 ? "xmmword" : bytes == 32 ? "ymmword" : bytes == 64 ? "zmmword" : "";
                plan->memory = x86_disassembly_token(size, (uint32_t)strlen(size));
                plan->broadcast = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->operand_bytes / form->disp8_scale : 0;
                plan->flags = form->family == BUSTER_X86_FAMILY_EVEX ? BUSTER_X86_TEXT_EVEX : form->family == BUSTER_X86_FAMILY_VEX && x86_opcode_map(form) < 8 ? BUSTER_X86_TEXT_VEX : 0;
                const bool direction = form->reg != BUSTER_X86_OPERAND_NONE && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_VVVV_RM);
                const bool swapped = form->w && form->syntax == X86_SYNTAX_REG_VVVV_RM && x86_opcode_map(form) >= 8;
                plan->flags |= direction ? BUSTER_X86_TEXT_DIRECTION : 0;
                plan->flags |= form->syntax == X86_SYNTAX_RM_REG || swapped ? BUSTER_X86_TEXT_STORE : 0;
                plan->flags |= form->relative_bytes >= 2 && form->opcode != 0xe8 ? BUSTER_X86_TEXT_DISP32 : 0;
                plan->flags |= row == X86_FORM_MOVDIR64B_R64_M512 || row == X86_FORM_ENQCMD_R64_M512 || row == X86_FORM_ENQCMDS_R64_M512 || row == X86_FORM_UMONITOR_R64 ? BUSTER_X86_TEXT_ADDRESS_REGISTER : 0;
                plan->flags |= (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) ? BUSTER_X86_TEXT_ROUNDING : 0;
                plan->flags |= (form->evex & BUSTER_X86_EVEX_ROUNDING) ? BUSTER_X86_TEXT_ROUNDING_MODE : 0;
                plan->flags |= (form->rm & BUSTER_X86_OPERAND_SIB) ? BUSTER_X86_TEXT_SIB : 0;
                plan->flags |= (form->rm & BUSTER_X86_OPERAND_VSIB) ? BUSTER_X86_TEXT_VSIB : 0;
            }
        }
    }
}

static char* x86_disassembly_write_token(char* output, uint64_t token)
{
    memcpy(output, &token, sizeof(token));
    return output + (token >> 56);
}

static char* x86_disassembly_write_register(char* output, uint32_t reg, uint32_t kind)
{
    return x86_disassembly_write_token(output, x86_disassembly_registers[kind][(reg & 31) | ((reg & BUSTER_X86_REGISTER_HIGH_BYTE) >> 1)]);
}

static char* x86_disassembly_write_unsigned(char* output, uint64_t value)
{
    static const char pairs[] = "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
    char digits[20];
    uint32_t at = sizeof(digits);
    while (value >= 100)
    {
        const uint64_t next = value / 100;
        at -= 2;
        memcpy(digits + at, pairs + (value - next * 100) * 2, 2);
        value = next;
    }
    if (value >= 10)
    {
        at -= 2;
        memcpy(digits + at, pairs + value * 2, 2);
    }
    else
    {
        at -= 1;
        digits[at] = (char)('0' + value);
    }
    const uint32_t length = sizeof(digits) - at;
    memcpy(output, digits + at, length);
    return output + length;
}

static char* x86_disassembly_write_signed(char* output, int64_t value, bool plus)
{
    if (value < 0 || plus)
    {
        *output = value < 0 ? '-' : '+';
        output += 1;
    }
    return x86_disassembly_write_unsigned(output, value < 0 ? 0 - (uint64_t)value : (uint64_t)value);
}

static char* x86_disassembly_write_hex(char* output, uint64_t value)
{
#if !BUSTER_HOST_VECTOR
    static const char hex[] = "0123456789abcdef";
    const uint32_t first = (uint32_t)__builtin_clzll(value | 1) / 4;
    for (uint32_t i = first; i < 16; i += 1)
    {
        output[i - first] = hex[(value >> ((15 - i) * 4)) & 15];
    }
    return output + 16 - first;
#else
    // Convert all sixteen nibbles together. VPSHUFB XMM's measured Zen 5 register latency is two cycles; using
    // a 128-bit register avoids wide temporaries for this single immediate and needs no gather or compress.
    const __m128i reverse = _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m128i bytes = _mm_shuffle_epi8(_mm_cvtsi64_si128((long long)value), reverse);
    const __m128i nibbles = _mm_and_si128(_mm_unpacklo_epi8(_mm_srli_epi16(bytes, 4), bytes), _mm_set1_epi8(15));
    const __m128i alphabet = _mm_setr_epi8('0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f');
    const __m128i digits = _mm_shuffle_epi8(alphabet, nibbles);
    const uint32_t first = (uint32_t)__builtin_clzll(value | 1) / 4;
    const __m128i positions = _mm_add_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), _mm_set1_epi8((char)first));
    const uint32_t length = 16 - first;
    _mm_mask_storeu_epi8(output, (uint16_t)((1u << length) - 1), _mm_shuffle_epi8(digits, positions));
    return output + length;
#endif
}

static char* x86_disassembly_write_memory(char* output, const X86Decoded* decoded, const X86DisassemblyPlan* plan)
{
    const X86Instruction* instruction = &decoded->instruction;
    if (plan->memory != 0)
    {
        output = x86_disassembly_write_token(output, plan->memory);
        memcpy(output, " ptr ", 5);
        output += 5;
    }
    const uint32_t segment = x86_form_segment(instruction->form);
    if (segment != 0)
    {
        memcpy(output, segment == 1 ? "fs:" : "gs:", 3);
        output += 3;
    }
    const bool hidden_index = (plan->flags & BUSTER_X86_TEXT_SIB) && instruction->index == 4;
    const bool absolute = !(instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_RIP)) && (!(instruction->mode & BUSTER_X86_MODE_INDEX) || hidden_index);
    if (absolute && segment == 0)
    {
        memcpy(output, "ds:", 3);
        output += 3;
    }
    if (!absolute)
    {
        *output = '[';
        output += 1;
    }
    bool part = false;
    const uint32_t address_class = decoded->address32 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
    if (instruction->mode & BUSTER_X86_MODE_RIP)
    {
        memcpy(output, decoded->address32 ? "eip" : "rip", 3);
        output += 3;
        part = true;
    }
    if (instruction->mode & BUSTER_X86_MODE_BASE)
    {
        output = x86_disassembly_write_register(output, instruction->rm, address_class);
        part = true;
    }
    if ((instruction->mode & BUSTER_X86_MODE_INDEX) && !hidden_index)
    {
        if (part)
        {
            *output = '+';
            output += 1;
        }
        output = x86_disassembly_write_register(output, instruction->index, (plan->flags & BUSTER_X86_TEXT_VSIB) ? plan->rm : address_class);
        *output = '*';
        output[1] = (char)('0' + (1u << (instruction->mode & BUSTER_X86_MODE_SCALE)));
        output += 2;
        part = true;
    }
    if (!part || instruction->displacement != 0)
    {
        output = x86_disassembly_write_signed(output, instruction->displacement, part);
    }
    if (!absolute)
    {
        *output = ']';
        output += 1;
    }
    if (plan->broadcast != 0)
    {
        memcpy(output, "{1to", 4);
        output = x86_disassembly_write_unsigned(output + 4, plan->broadcast);
        *output = '}';
        output += 1;
    }
    return output;
}

static int64_t x86_disassembly_format(const X86Decoded* decoded, char* output, uint64_t capacity)
{
    const X86Instruction* instruction = &decoded->instruction;
    const X86DisassemblyPlan* plan = &x86_disassembly_plans[x86_form_description(instruction->form) - x86_form_descriptions];
    int64_t length;
    if (output == 0 || plan->bound == 0 || capacity < plan->bound)
    {
        length = x86_disassembly_format_scalar(decoded, output, capacity);
    }
    else
    {
        char* at = output;
        const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
        if (plan->flags & (BUSTER_X86_TEXT_EVEX | BUSTER_X86_TEXT_VEX))
        {
            const uint32_t count = (plan->flags & BUSTER_X86_TEXT_EVEX) ? 7 : 6;
            memcpy(at, (plan->flags & BUSTER_X86_TEXT_EVEX) ? "{evex} " : "{vex} ", count);
            at += count;
        }
        if (!memory && (plan->flags & BUSTER_X86_TEXT_DIRECTION))
        {
            const uint32_t count = (plan->flags & BUSTER_X86_TEXT_STORE) ? 8 : 7;
            memcpy(at, (plan->flags & BUSTER_X86_TEXT_STORE) ? "{store} " : "{load} ", count);
            at += count;
        }
        if (plan->flags & BUSTER_X86_TEXT_DISP32)
        {
            memcpy(at, "{disp32} ", 9);
            at += 9;
        }
        const bool hidden_index = (plan->flags & BUSTER_X86_TEXT_SIB) && instruction->index == 4;
        const bool general_index = (instruction->mode & BUSTER_X86_MODE_INDEX) && !hidden_index && !(plan->flags & BUSTER_X86_TEXT_VSIB);
        const bool address_register = (plan->flags & BUSTER_X86_TEXT_ADDRESS_REGISTER) != 0;
        const bool shown_address = address_register || (instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_RIP)) || general_index;
        if (decoded->address32 && !shown_address)
        {
            memcpy(at, "addr32 ", 7);
            at += 7;
        }
        memcpy(at, plan->mnemonic, plan->mnemonic_length);
        at += plan->mnemonic_length;
        static const char rounding_names[4][8] = {"{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}"};
        const bool rounding_mode = (plan->flags & BUSTER_X86_TEXT_ROUNDING_MODE) != 0;
        const uint32_t rounding_length = rounding_mode ? 8 : 5;
        const char* rounding_name = rounding_mode ? rounding_names[(instruction->decorations >> 5) & 3] : "{sae}";
        bool rounding = (plan->flags & BUSTER_X86_TEXT_ROUNDING) != 0;
        uint32_t operand_index = 0;
        for (uint64_t operands = plan->operands; operands != 0; operands >>= 8)
        {
            if (operand_index != 0)
            {
                *at = ',';
                at += 1;
            }
            *at = ' ';
            at += 1;
            const char operand = (char)operands;
            if (operand == 'i' && rounding)
            {
                memcpy(at, rounding_name, rounding_length);
                memcpy(at + rounding_length, ", ", 2);
                at += rounding_length + 2;
                rounding = false;
            }
            switch (operand)
            {
                case 'r': at = x86_disassembly_write_register(at, instruction->reg, decoded->address32 && address_register ? BUSTER_X86_CLASS_GPR32 : plan->reg); break;
                case 'v': at = x86_disassembly_write_register(at, instruction->vvvv, plan->vvvv); break;
                case 'm': at = memory ? x86_disassembly_write_memory(at, decoded, plan) : x86_disassembly_write_register(at, instruction->rm, decoded->address32 && address_register ? BUSTER_X86_CLASS_GPR32 : plan->rm); break;
                case 'i':
                    if (plan->immediate == 2)
                    {
                        memcpy(at, "0x", 2);
                        at = x86_disassembly_write_hex(at + 2, (uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32));
                    }
                    else if (plan->immediate == 3)
                    {
                        at = x86_disassembly_write_unsigned(at, (uint32_t)instruction->immediate);
                    }
                    else
                    {
                        at = x86_disassembly_write_signed(at, plan->immediate == 1 ? (int8_t)instruction->immediate : instruction->immediate, false);
                    }
                    break;
                case 't': *at = '.'; at = x86_disassembly_write_signed(at + 1, (int64_t)instruction->displacement + decoded->length, true); break;
                case 'a': at = x86_disassembly_write_register(at, 0, plan->accumulator); break;
                case 'o': memcpy(at, "ds:0x", 5); at = x86_disassembly_write_hex(at + 5, (uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32)); break;
                case 'l': at = x86_disassembly_write_unsigned(at, (uint32_t)instruction->immediate & 65535); break;
                case 'h': at = x86_disassembly_write_unsigned(at, (uint32_t)instruction->immediate >> 16); break;
                case 'j': at = x86_disassembly_write_unsigned(at, (uint32_t)instruction->immediate & 255); break;
                case 'k': at = x86_disassembly_write_unsigned(at, ((uint32_t)instruction->immediate >> 8) & 255); break;
                case '4': at = x86_disassembly_write_register(at, (uint32_t)instruction->immediate >> 4, plan->reg); break;
                case 'z': at = x86_disassembly_write_unsigned(at, (uint32_t)instruction->immediate & 15); break;
                case 'c': memcpy(at, "cl", 2); at += 2; break;
                case 'd': memcpy(at, "dx", 2); at += 2; break;
                case 's': memcpy(at, "st(0)", 5); at += 5; break;
                case 'x': memcpy(at, "xmm0", 4); at += 4; break;
                case 'q': memcpy(at, "[rcx]", 5); at += 5; break;
                default: *at = '1'; at += 1; break;
            }
            if (operand_index == 0 && (instruction->decorations & BUSTER_X86_DECORATION_MASK))
            {
                memcpy(at, "{k0}", 4);
                at[2] = (char)('0' + (instruction->decorations & BUSTER_X86_DECORATION_MASK));
                at += 4;
            }
            if (operand_index == 0 && (instruction->decorations & BUSTER_X86_DECORATION_ZEROING))
            {
                memcpy(at, "{z}", 3);
                at += 3;
            }
            operand_index += 1;
        }
        if (rounding)
        {
            memcpy(at, ", ", 2);
            memcpy(at + 2, rounding_name, rounding_length);
            at += rounding_length + 2;
        }
        *at = 0;
        length = at - output;
    }
    return length;
}

// Listing prefixes occupy exactly 56 bytes: eight address digits, two spaces, fifteen three-byte hex slots and
// one final space. Only decoded.length input bytes are read. The caller's source starts at decoded.offset zero;
// base adjusts the displayed address when a command decodes a later chunk of the same bounded source.
static int64_t x86_disassembly_listing(const X86Decoded* decoded, const uint8_t* source, uint32_t base, char* output, uint64_t capacity, bool scalar)
{
    (void)scalar;

    int64_t length = -1;
    if (output != 0 && capacity >= 56)
    {
        const uint32_t address = base + decoded->offset;
        static const char hex[] = "0123456789abcdef";
        if (BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t i = 0; i < 8; i += 1)
            {
                output[i] = hex[(address >> (28 - i * 4)) & 15];
            }
            memset(output + 8, ' ', 48);
            for (uint32_t i = 0; i < decoded->length; i += 1)
            {
                const uint8_t byte = source[decoded->offset + i];
                output[10 + i * 3] = hex[byte >> 4];
                output[11 + i * 3] = hex[byte & 15];
            }
        }
        else
        {
            const __m128i alphabet = _mm_loadu_si128((const __m128i*)hex);
            const __m128i reverse = _mm_setr_epi8(3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0);
            const __m128i address_bytes = _mm_shuffle_epi8(_mm_cvtsi32_si128((int)address), reverse);
            const __m128i address_nibbles = _mm_and_si128(_mm_unpacklo_epi8(_mm_srli_epi16(address_bytes, 4), address_bytes), _mm_set1_epi8(15));
            _mm_storel_epi64((__m128i*)output, _mm_shuffle_epi8(alphabet, address_nibbles));
            memcpy(output + 8, "  ", 2);
            const __m128i bytes = _mm_maskz_loadu_epi8((uint16_t)((1u << decoded->length) - 1), source + decoded->offset);
            const __m256i wide = _mm256_cvtepu8_epi16(bytes);
            const __m256i nibbles = _mm256_or_si256(_mm256_srli_epi16(wide, 4), _mm256_slli_epi16(_mm256_and_si256(wide, _mm256_set1_epi16(15)), 8));
            const __m256i digits = _mm256_shuffle_epi8(_mm256_broadcastsi128_si256(alphabet), nibbles);
            const __m512i table = _mm512_inserti64x4(_mm512_set1_epi8(' '), digits, 0);
            const __m512i positions = _mm512_setr_epi64(0x0504200302200100ull, 0x0a20090820070620ull, 0x200f0e200d0c200bull, 0x1514201312201110ull,
                                                       0x1a20191820171620ull, 0x202020201d1c201bull, 0x2020202020202020ull, 0x2020202020202020ull);
            const __m512i spaced = _mm512_permutexvar_epi8(positions, table);
            const uint64_t active = (1ull << (decoded->length * 3)) - 1;
            const __m512i prefix = _mm512_mask_mov_epi8(_mm512_set1_epi8(' '), active, spaced);
            _mm512_mask_storeu_epi8(output + 10, (1ull << 46) - 1, prefix);
        }
        const int64_t body = BUSTER_SCALAR_PATH(scalar) ? x86_disassembly_format_scalar(decoded, output + 56, capacity - 56) : x86_disassembly_format(decoded, output + 56, capacity - 56);
        length = body < 0 ? -1 : 56 + body;
    }
    return length;
}
