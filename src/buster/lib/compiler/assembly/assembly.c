#include <buster/lib/compiler/assembly/assembly.h>

#include <buster/lib/string.h>

typedef enum AssemblyOpcode
{
    ASSEMBLY_OPCODE_X86_NOP,
    ASSEMBLY_OPCODE_X86_RET,
    ASSEMBLY_OPCODE_X86_INT3,
    ASSEMBLY_OPCODE_X86_CALL,
    ASSEMBLY_OPCODE_X86_JMP,
    ASSEMBLY_OPCODE_X86_MOV,
    ASSEMBLY_OPCODE_X86_ADD,
    ASSEMBLY_OPCODE_X86_ADC,
    ASSEMBLY_OPCODE_X86_SUB,
    ASSEMBLY_OPCODE_X86_SBB,
    ASSEMBLY_OPCODE_X86_AND,
    ASSEMBLY_OPCODE_X86_OR,
    ASSEMBLY_OPCODE_X86_XOR,
    ASSEMBLY_OPCODE_X86_CMP,
    ASSEMBLY_OPCODE_X86_TEST,
    ASSEMBLY_OPCODE_X86_IMUL,
    ASSEMBLY_OPCODE_X86_MUL,
    ASSEMBLY_OPCODE_X86_DIV,
    ASSEMBLY_OPCODE_X86_IDIV,
    ASSEMBLY_OPCODE_X86_PUSH,
    ASSEMBLY_OPCODE_X86_POP,
    ASSEMBLY_OPCODE_X86_INC,
    ASSEMBLY_OPCODE_X86_DEC,
    ASSEMBLY_OPCODE_X86_NEG,
    ASSEMBLY_OPCODE_X86_NOT,
    ASSEMBLY_OPCODE_X86_SHL,
    ASSEMBLY_OPCODE_X86_SHR,
    ASSEMBLY_OPCODE_X86_SAR,
    ASSEMBLY_OPCODE_X86_LEA,
    ASSEMBLY_OPCODE_X86_MOVZX,
    ASSEMBLY_OPCODE_X86_MOVSX,
    ASSEMBLY_OPCODE_X86_MOVSXD,
    ASSEMBLY_OPCODE_X86_ROL,
    ASSEMBLY_OPCODE_X86_ROR,
    ASSEMBLY_OPCODE_X86_RCL,
    ASSEMBLY_OPCODE_X86_RCR,
    ASSEMBLY_OPCODE_X86_SHLD,
    ASSEMBLY_OPCODE_X86_SHRD,
    ASSEMBLY_OPCODE_X86_BSF,
    ASSEMBLY_OPCODE_X86_BSR,
    ASSEMBLY_OPCODE_X86_BSWAP,
    ASSEMBLY_OPCODE_X86_BT,
    ASSEMBLY_OPCODE_X86_BTC,
    ASSEMBLY_OPCODE_X86_BTR,
    ASSEMBLY_OPCODE_X86_BTS,
    ASSEMBLY_OPCODE_X86_XCHG,
    ASSEMBLY_OPCODE_X86_XADD,
    ASSEMBLY_OPCODE_X86_CMPXCHG,
    ASSEMBLY_OPCODE_X86_CMPXCHG8B,
    ASSEMBLY_OPCODE_X86_CMPXCHG16B,
    ASSEMBLY_OPCODE_X86_POPCNT,
    ASSEMBLY_OPCODE_X86_LZCNT,
    ASSEMBLY_OPCODE_X86_TZCNT,
    ASSEMBLY_OPCODE_X86_CBW,
    ASSEMBLY_OPCODE_X86_CWDE,
    ASSEMBLY_OPCODE_X86_CDQE,
    ASSEMBLY_OPCODE_X86_CWD,
    ASSEMBLY_OPCODE_X86_CDQ,
    ASSEMBLY_OPCODE_X86_CQO,
    ASSEMBLY_OPCODE_X86_JCC,
    ASSEMBLY_OPCODE_X86_SETCC,
    ASSEMBLY_OPCODE_X86_CMOVCC,
    ASSEMBLY_OPCODE_X86_MOVAPS,
    ASSEMBLY_OPCODE_X86_MOVUPS,
    ASSEMBLY_OPCODE_X86_MOVAPD,
    ASSEMBLY_OPCODE_X86_MOVUPD,
    ASSEMBLY_OPCODE_X86_MOVDQA,
    ASSEMBLY_OPCODE_X86_MOVDQU,
    ASSEMBLY_OPCODE_X86_XORPS,
    ASSEMBLY_OPCODE_X86_XORPD,
    ASSEMBLY_OPCODE_X86_PXOR,
    ASSEMBLY_OPCODE_X86_ADDPS,
    ASSEMBLY_OPCODE_X86_ADDPD,
    ASSEMBLY_OPCODE_X86_ADDSS,
    ASSEMBLY_OPCODE_X86_ADDSD,
    ASSEMBLY_OPCODE_X86_SUBPS,
    ASSEMBLY_OPCODE_X86_SUBPD,
    ASSEMBLY_OPCODE_X86_MULPS,
    ASSEMBLY_OPCODE_X86_MULPD,
    ASSEMBLY_OPCODE_X86_DIVPS,
    ASSEMBLY_OPCODE_X86_DIVPD,
    ASSEMBLY_OPCODE_X86_VMOVAPS,
    ASSEMBLY_OPCODE_X86_VMOVUPS,
    ASSEMBLY_OPCODE_X86_VMOVAPD,
    ASSEMBLY_OPCODE_X86_VMOVUPD,
    ASSEMBLY_OPCODE_X86_VXORPS,
    ASSEMBLY_OPCODE_X86_VXORPD,
    ASSEMBLY_OPCODE_X86_VADDPS,
    ASSEMBLY_OPCODE_X86_VADDPD,
    ASSEMBLY_OPCODE_X86_VADDSS,
    ASSEMBLY_OPCODE_X86_VADDSD,
    ASSEMBLY_OPCODE_X86_VSUBPS,
    ASSEMBLY_OPCODE_X86_VSUBPD,
    ASSEMBLY_OPCODE_X86_VMULPS,
    ASSEMBLY_OPCODE_X86_VMULPD,
    ASSEMBLY_OPCODE_X86_VDIVPS,
    ASSEMBLY_OPCODE_X86_VDIVPD,
    ASSEMBLY_OPCODE_X86_VMOVDQA,
    ASSEMBLY_OPCODE_X86_VMOVDQU,
    ASSEMBLY_OPCODE_X86_VPADDB,
    ASSEMBLY_OPCODE_X86_VPADDW,
    ASSEMBLY_OPCODE_X86_VPADDD,
    ASSEMBLY_OPCODE_X86_VPADDQ,
    ASSEMBLY_OPCODE_X86_VPSUBB,
    ASSEMBLY_OPCODE_X86_VPSUBW,
    ASSEMBLY_OPCODE_X86_VPSUBD,
    ASSEMBLY_OPCODE_X86_VPSUBQ,
    ASSEMBLY_OPCODE_X86_VPAND,
    ASSEMBLY_OPCODE_X86_VPOR,
    ASSEMBLY_OPCODE_X86_VPXOR,
    ASSEMBLY_OPCODE_X86_VPCMPEQB,
    ASSEMBLY_OPCODE_X86_VPCMPEQW,
    ASSEMBLY_OPCODE_X86_VPCMPEQD,
    ASSEMBLY_OPCODE_X86_VPCMPEQQ,
    ASSEMBLY_OPCODE_X86_VPCMPGTB,
    ASSEMBLY_OPCODE_X86_VPCMPGTW,
    ASSEMBLY_OPCODE_X86_VPCMPGTD,
    ASSEMBLY_OPCODE_X86_VPCMPGTQ,
    ASSEMBLY_OPCODE_X86_VPMULLW,
    ASSEMBLY_OPCODE_X86_VPMULLD,
    ASSEMBLY_OPCODE_X86_VMOVDQA32,
    ASSEMBLY_OPCODE_X86_VMOVDQU32,
    ASSEMBLY_OPCODE_X86_VMOVDQA64,
    ASSEMBLY_OPCODE_X86_VMOVDQU64,
    ASSEMBLY_OPCODE_X86_VMOVDQU8,
    ASSEMBLY_OPCODE_X86_VMOVDQU16,
    ASSEMBLY_OPCODE_X86_VCMPPS,
    ASSEMBLY_OPCODE_X86_VCMPPD,
    ASSEMBLY_OPCODE_X86_VPCMPD,
    ASSEMBLY_OPCODE_X86_VPCMPQ,
    ASSEMBLY_OPCODE_X86_VPCMPB,
    ASSEMBLY_OPCODE_X86_VPCMPW,
    ASSEMBLY_OPCODE_X86_VPCMPUD,
    ASSEMBLY_OPCODE_X86_VPCMPUQ,
    ASSEMBLY_OPCODE_X86_VRNDSCALEPS,
    ASSEMBLY_OPCODE_X86_VRNDSCALEPD,
    ASSEMBLY_OPCODE_X86_KMOVW,
    ASSEMBLY_OPCODE_X86_KMOVD,
    ASSEMBLY_OPCODE_X86_KMOVQ,
    ASSEMBLY_OPCODE_X86_KADDW,
    ASSEMBLY_OPCODE_X86_KANDW,
    ASSEMBLY_OPCODE_X86_KORW,
    ASSEMBLY_OPCODE_X86_KXORW,
    ASSEMBLY_OPCODE_X86_KNOTW,
    ASSEMBLY_OPCODE_X86_KORTESTW,
    ASSEMBLY_OPCODE_X86_APX_PUSH2,
    ASSEMBLY_OPCODE_X86_APX_POP2,
    ASSEMBLY_OPCODE_X86_LDTILECFG,
    ASSEMBLY_OPCODE_X86_STTILECFG,
    ASSEMBLY_OPCODE_X86_TILELOADD,
    ASSEMBLY_OPCODE_X86_TILELOADDT1,
    ASSEMBLY_OPCODE_X86_TILESTORED,
    ASSEMBLY_OPCODE_X86_TILEZERO,
    ASSEMBLY_OPCODE_X86_TDPBF16PS,
    ASSEMBLY_OPCODE_X86_TDPBSSD,
    ASSEMBLY_OPCODE_X86_TDPBSUD,
    ASSEMBLY_OPCODE_X86_TDPBUSD,
    ASSEMBLY_OPCODE_X86_TDPBUUD,
    ASSEMBLY_OPCODE_X86_EMMS,
    ASSEMBLY_OPCODE_X86_MOVQ_MMX,
    ASSEMBLY_OPCODE_X86_PADDB_MMX,
    ASSEMBLY_OPCODE_X86_PADDW_MMX,
    ASSEMBLY_OPCODE_X86_PADDD_MMX,
    ASSEMBLY_OPCODE_X86_PADDQ_MMX,
    ASSEMBLY_OPCODE_X86_PSUBB_MMX,
    ASSEMBLY_OPCODE_X86_PSUBW_MMX,
    ASSEMBLY_OPCODE_X86_PSUBD_MMX,
    ASSEMBLY_OPCODE_X86_PSUBQ_MMX,
    ASSEMBLY_OPCODE_X86_PAND_MMX,
    ASSEMBLY_OPCODE_X86_POR_MMX,
    ASSEMBLY_OPCODE_X86_PXOR_MMX,
    ASSEMBLY_OPCODE_X86_PCMPEQB_MMX,
    ASSEMBLY_OPCODE_X86_PCMPEQW_MMX,
    ASSEMBLY_OPCODE_X86_PCMPEQD_MMX,
    ASSEMBLY_OPCODE_X86_PCMPGTB_MMX,
    ASSEMBLY_OPCODE_X86_PCMPGTW_MMX,
    ASSEMBLY_OPCODE_X86_PCMPGTD_MMX,
    ASSEMBLY_OPCODE_X86_PMULLW_MMX,
    ASSEMBLY_OPCODE_X86_FLD,
    ASSEMBLY_OPCODE_X86_FST,
    ASSEMBLY_OPCODE_X86_FSTP,
    ASSEMBLY_OPCODE_X86_FILD,
    ASSEMBLY_OPCODE_X86_FIST,
    ASSEMBLY_OPCODE_X86_FISTP,
    ASSEMBLY_OPCODE_X86_FISTTP,
    ASSEMBLY_OPCODE_X86_FADD,
    ASSEMBLY_OPCODE_X86_FMUL,
    ASSEMBLY_OPCODE_X86_FSUB,
    ASSEMBLY_OPCODE_X86_FSUBR,
    ASSEMBLY_OPCODE_X86_FDIV,
    ASSEMBLY_OPCODE_X86_FDIVR,
    ASSEMBLY_OPCODE_X86_FADDP,
    ASSEMBLY_OPCODE_X86_FMULP,
    ASSEMBLY_OPCODE_X86_FSUBP,
    ASSEMBLY_OPCODE_X86_FSUBRP,
    ASSEMBLY_OPCODE_X86_FDIVP,
    ASSEMBLY_OPCODE_X86_FDIVRP,
    ASSEMBLY_OPCODE_X86_FXCH,
    ASSEMBLY_OPCODE_X86_FCOM,
    ASSEMBLY_OPCODE_X86_FCOMP,
    ASSEMBLY_OPCODE_X86_FCOMPP,
    ASSEMBLY_OPCODE_X86_FUCOM,
    ASSEMBLY_OPCODE_X86_FUCOMP,
    ASSEMBLY_OPCODE_X86_FUCOMPP,
    ASSEMBLY_OPCODE_X86_FCOMI,
    ASSEMBLY_OPCODE_X86_FCOMIP,
    ASSEMBLY_OPCODE_X86_FUCOMI,
    ASSEMBLY_OPCODE_X86_FUCOMIP,
    ASSEMBLY_OPCODE_X86_FCMOVCC,
    ASSEMBLY_OPCODE_X86_FIADD,
    ASSEMBLY_OPCODE_X86_FIMUL,
    ASSEMBLY_OPCODE_X86_FISUB,
    ASSEMBLY_OPCODE_X86_FISUBR,
    ASSEMBLY_OPCODE_X86_FIDIV,
    ASSEMBLY_OPCODE_X86_FIDIVR,
    ASSEMBLY_OPCODE_X86_FBLD,
    ASSEMBLY_OPCODE_X86_FBSTP,
    ASSEMBLY_OPCODE_X86_FLDCW,
    ASSEMBLY_OPCODE_X86_FNSTCW,
    ASSEMBLY_OPCODE_X86_FSTCW,
    ASSEMBLY_OPCODE_X86_FLDENV,
    ASSEMBLY_OPCODE_X86_FNSTENV,
    ASSEMBLY_OPCODE_X86_FSTENV,
    ASSEMBLY_OPCODE_X86_FRSTOR,
    ASSEMBLY_OPCODE_X86_FNSAVE,
    ASSEMBLY_OPCODE_X86_FSAVE,
    ASSEMBLY_OPCODE_X86_FNSTSW,
    ASSEMBLY_OPCODE_X86_FSTSW,
    ASSEMBLY_OPCODE_X86_FFREE,
    ASSEMBLY_OPCODE_X86_FFREEP,
    ASSEMBLY_OPCODE_X86_FINCSTP,
    ASSEMBLY_OPCODE_X86_FDECSTP,
    ASSEMBLY_OPCODE_X86_F2XM1,
    ASSEMBLY_OPCODE_X86_FABS,
    ASSEMBLY_OPCODE_X86_FCHS,
    ASSEMBLY_OPCODE_X86_FLD1,
    ASSEMBLY_OPCODE_X86_FLDZ,
    ASSEMBLY_OPCODE_X86_FLDPI,
    ASSEMBLY_OPCODE_X86_FLDL2E,
    ASSEMBLY_OPCODE_X86_FLDL2T,
    ASSEMBLY_OPCODE_X86_FLDLG2,
    ASSEMBLY_OPCODE_X86_FLDLN2,
    ASSEMBLY_OPCODE_X86_FSQRT,
    ASSEMBLY_OPCODE_X86_FSIN,
    ASSEMBLY_OPCODE_X86_FCOS,
    ASSEMBLY_OPCODE_X86_FSINCOS,
    ASSEMBLY_OPCODE_X86_FPTAN,
    ASSEMBLY_OPCODE_X86_FPATAN,
    ASSEMBLY_OPCODE_X86_FYL2X,
    ASSEMBLY_OPCODE_X86_FYL2XP1,
    ASSEMBLY_OPCODE_X86_FRNDINT,
    ASSEMBLY_OPCODE_X86_FSCALE,
    ASSEMBLY_OPCODE_X86_FPREM,
    ASSEMBLY_OPCODE_X86_FPREM1,
    ASSEMBLY_OPCODE_X86_FXTRACT,
    ASSEMBLY_OPCODE_X86_FTST,
    ASSEMBLY_OPCODE_X86_FXAM,
    ASSEMBLY_OPCODE_X86_FNOP,
    ASSEMBLY_OPCODE_X86_FINIT,
    ASSEMBLY_OPCODE_X86_FNINIT,
    ASSEMBLY_OPCODE_X86_FCLEX,
    ASSEMBLY_OPCODE_X86_FNCLEX,
    ASSEMBLY_OPCODE_X86_FWAIT,
    ASSEMBLY_OPCODE_AARCH64_NOP,
    ASSEMBLY_OPCODE_AARCH64_RET,
    ASSEMBLY_OPCODE_AARCH64_B,
    ASSEMBLY_OPCODE_AARCH64_BL,
    ASSEMBLY_OPCODE_COUNT,
} AssemblyOpcode;

typedef enum AssemblyOperandKind
{
    ASSEMBLY_OPERAND_NONE,
    ASSEMBLY_OPERAND_REGISTER,
    ASSEMBLY_OPERAND_MEMORY,
    ASSEMBLY_OPERAND_EXPRESSION,
    ASSEMBLY_OPERAND_COUNT,
} AssemblyOperandKind;

typedef struct AssemblyExpression AssemblyExpression;
struct AssemblyExpression
{
    s64 addend;
    u32 symbol;
    bool has_symbol;
    u8 reserved[3];
};

typedef struct AssemblyRegister AssemblyRegister;
typedef enum AssemblyRegisterClass
{
    ASSEMBLY_REGISTER_GPR,
    ASSEMBLY_REGISTER_XMM,
    ASSEMBLY_REGISTER_YMM,
    ASSEMBLY_REGISTER_ZMM,
    ASSEMBLY_REGISTER_OPMASK,
    ASSEMBLY_REGISTER_TILE,
    ASSEMBLY_REGISTER_MMX,
    ASSEMBLY_REGISTER_X87,
    ASSEMBLY_REGISTER_CLASS_COUNT,
} AssemblyRegisterClass;

struct AssemblyRegister
{
    u8 index;
    u16 width;
    AssemblyRegisterClass class;
    bool high_byte;
};

typedef struct AssemblyMemory AssemblyMemory;
struct AssemblyMemory
{
    AssemblyExpression displacement;
    AssemblyRegister base;
    AssemblyRegister index;
    u8 scale;
    u16 width;
    bool has_base;
    bool has_index;
    bool rip_relative;
    u8 reserved;
};

typedef struct AssemblyOperand AssemblyOperand;
struct AssemblyOperand
{
    AssemblyExpression expression;
    AssemblyMemory memory;
    AssemblyRegister reg;
    AssemblyOperandKind kind;
    u8 mask;
    u8 broadcast;
    u8 rounding;
    bool has_mask;
    bool zeroing;
    bool sae;
};

typedef struct AssemblyInstruction AssemblyInstruction;
struct AssemblyInstruction
{
    AssemblyOperand operands[4];
    u64 offset;
    u32 line;
    u32 column;
    u32 size;
    AssemblyOpcode opcode;
    u8 operand_count;
    u16 width;
    u8 condition;
    bool lock_prefix;
    bool no_flags;
    bool evex;
    u8 rip_relocation_trailing;
};

typedef struct AssemblyBuilder AssemblyBuilder;
struct AssemblyBuilder
{
    Arena* arena;
    AssemblyEncodeResult result;
    AssemblyInstruction* instructions;
    u32 instruction_count;
    u32 instruction_capacity;
    u32 symbol_capacity;
    u32 relocation_capacity;
    u32 diagnostic_capacity;
    u64 output_capacity;
    u64 output_count;
};

BUSTER_GLOBAL_LOCAL bool assembly_space(char8 value)
{
    return value == ' ' || value == '\t' || value == '\r';
}

BUSTER_GLOBAL_LOCAL String8 assembly_trim(String8 string)
{
    u64 start = 0;
    while (start < string.length && assembly_space(string.pointer[start]))
    {
        start += 1;
    }
    u64 end = string.length;
    while (end > start && assembly_space(string.pointer[end - 1]))
    {
        end -= 1;
    }
    return (String8){.pointer = string.pointer + start, .length = end - start};
}

BUSTER_GLOBAL_LOCAL bool assembly_character_identifier_start(char8 value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_' || value == '.' || value == '$';
}

BUSTER_GLOBAL_LOCAL bool assembly_character_identifier(char8 value)
{
    return assembly_character_identifier_start(value) || (value >= '0' && value <= '9');
}

BUSTER_GLOBAL_LOCAL bool assembly_identifier(String8 string)
{
    if (!string.length || !assembly_character_identifier_start(string.pointer[0]))
    {
        return false;
    }
    for (u64 index = 1; index < string.length; index += 1)
    {
        if (!assembly_character_identifier(string.pointer[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL char8 assembly_ascii_lower(char8 value)
{
    return value >= 'A' && value <= 'Z' ? (char8)(value + ('a' - 'A')) : value;
}

BUSTER_GLOBAL_LOCAL bool assembly_word_equal(String8 left, String8 right)
{
    if (left.length != right.length)
    {
        return false;
    }
    for (u64 index = 0; index < left.length; index += 1)
    {
        if (assembly_ascii_lower(left.pointer[index]) != assembly_ascii_lower(right.pointer[index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void assembly_diagnostic(AssemblyBuilder* builder, AssemblyDiagnosticKind kind, u32 line, u32 column, u32 length, String8 message)
{
    if (builder->result.diagnostic_count >= builder->diagnostic_capacity)
    {
        return;
    }
    builder->result.diagnostics[builder->result.diagnostic_count++] = (AssemblyDiagnostic){
        .message = message,
        .line = line,
        .column = column,
        .length = length,
        .kind = kind,
    };
}

BUSTER_GLOBAL_LOCAL u32 assembly_symbol_find(AssemblyBuilder* builder, String8 name)
{
    for (u32 symbol_index = 0; symbol_index < builder->result.symbol_count; symbol_index += 1)
    {
        if (string_equal(builder->result.symbols[symbol_index].name, name))
        {
            return symbol_index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 assembly_symbol_intern(AssemblyBuilder* builder, String8 name)
{
    u32 existing = assembly_symbol_find(builder, name);
    if (existing != UINT32_MAX)
    {
        return existing;
    }
    if (builder->result.symbol_count >= builder->symbol_capacity)
    {
        return UINT32_MAX;
    }
    u32 result = builder->result.symbol_count++;
    builder->result.symbols[result].name = string_duplicate_arena(builder->arena, name, false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool assembly_parse_s64(String8 string, s64* result)
{
    string = assembly_trim(string);
    if (!string.length || !result)
    {
        return false;
    }
    bool negative = string.pointer[0] == '-';
    u64 cursor = negative || string.pointer[0] == '+' ? 1 : 0;
    u32 base = 10;
    if (cursor + 2 <= string.length && string.pointer[cursor] == '0' &&
        (string.pointer[cursor + 1] == 'x' || string.pointer[cursor + 1] == 'X'))
    {
        base = 16;
        cursor += 2;
    }
    if (cursor == string.length)
    {
        return false;
    }
    u64 value = 0;
    u64 limit = negative ? (u64)INT64_MAX + 1 : (u64)INT64_MAX;
    for (; cursor < string.length; cursor += 1)
    {
        char8 character = string.pointer[cursor];
        u32 digit = character >= '0' && character <= '9' ? (u32)(character - '0')
                    : character >= 'a' && character <= 'f' ? (u32)(character - 'a') + 10
                    : character >= 'A' && character <= 'F' ? (u32)(character - 'A') + 10
                                                            : UINT32_MAX;
        if (digit >= base || value > (limit - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
    }
    if (negative)
    {
        *result = value == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)value;
    }
    else
    {
        *result = (s64)value;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_expression_parse(AssemblyBuilder* builder, String8 text, AssemblyExpression* result)
{
    text = assembly_trim(text);
    if (!text.length || !result)
    {
        return false;
    }
    s64 integer = 0;
    if (assembly_parse_s64(text, &integer))
    {
        result->addend = integer;
        return true;
    }
    u64 operator_index = text.length;
    for (u64 index = 1; index < text.length; index += 1)
    {
        if (text.pointer[index] == '+' || text.pointer[index] == '-')
        {
            operator_index = index;
            break;
        }
    }
    String8 name = assembly_trim((String8){.pointer = text.pointer, .length = operator_index});
    if (!assembly_identifier(name))
    {
        return false;
    }
    if (operator_index < text.length)
    {
        String8 addend = assembly_trim((String8){.pointer = text.pointer + operator_index + 1, .length = text.length - operator_index - 1});
        if (!assembly_parse_s64(addend, &integer))
        {
            return false;
        }
        if (text.pointer[operator_index] == '-')
        {
            if (integer == INT64_MIN)
            {
                return false;
            }
            integer = -integer;
        }
    }
    u32 symbol = assembly_symbol_intern(builder, name);
    if (symbol == UINT32_MAX)
    {
        return false;
    }
    *result = (AssemblyExpression){
        .addend = integer,
        .symbol = symbol,
        .has_symbol = true,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_numbered_register_parse(String8 text, String8 prefix, u32 maximum, u16 width,
                                                          AssemblyRegisterClass class, AssemblyRegister* result)
{
    if (text.length <= prefix.length || !assembly_word_equal(string_slice(text, 0, prefix.length), prefix))
    {
        return false;
    }
    u32 value = 0;
    for (u64 index = prefix.length; index < text.length; index += 1)
    {
        char8 character = text.pointer[index];
        if (character < '0' || character > '9' || value > (UINT32_MAX - (u32)(character - '0')) / 10)
        {
            return false;
        }
        value = value * 10 + (u32)(character - '0');
    }
    if (value > maximum)
    {
        return false;
    }
    *result = (AssemblyRegister){.index = (u8)value, .width = width, .class = class};
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_extended_gpr_parse(String8 text, AssemblyRegister* result)
{
    if (text.length < 3 || assembly_ascii_lower(text.pointer[0]) != 'r')
    {
        return false;
    }
    u64 suffix_start = text.length;
    char8 suffix = 0;
    if (text.length && (assembly_ascii_lower(text.pointer[text.length - 1]) == 'b' ||
                        assembly_ascii_lower(text.pointer[text.length - 1]) == 'w' ||
                        assembly_ascii_lower(text.pointer[text.length - 1]) == 'd'))
    {
        suffix_start -= 1;
        suffix = assembly_ascii_lower(text.pointer[text.length - 1]);
    }
    if (suffix_start <= 1)
    {
        return false;
    }
    u32 value = 0;
    for (u64 index = 1; index < suffix_start; index += 1)
    {
        char8 character = text.pointer[index];
        if (character < '0' || character > '9' || value > (UINT32_MAX - (u32)(character - '0')) / 10)
        {
            return false;
        }
        value = value * 10 + (u32)(character - '0');
    }
    if (value < 16 || value > 31)
    {
        return false;
    }
    u16 width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'd' ? 32 : 64;
    *result = (AssemblyRegister){.index = (u8)value, .width = width, .class = ASSEMBLY_REGISTER_GPR};
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_register_parse(String8 text, AssemblySyntax syntax, AssemblyRegister* result)
{
    text = assembly_trim(text);
    if (syntax == ASSEMBLY_SYNTAX_ATT)
    {
        if (!text.length || text.pointer[0] != '%')
        {
            return false;
        }
        text.pointer += 1;
        text.length -= 1;
    }
    else if (text.length && text.pointer[0] == '%')
    {
        return false;
    }

    if (assembly_extended_gpr_parse(text, result))
    {
        return true;
    }

    static String8 const names_64[] = {
        S8_INITIALIZER("rax"), S8_INITIALIZER("rcx"), S8_INITIALIZER("rdx"), S8_INITIALIZER("rbx"),
        S8_INITIALIZER("rsp"), S8_INITIALIZER("rbp"), S8_INITIALIZER("rsi"), S8_INITIALIZER("rdi"),
        S8_INITIALIZER("r8"), S8_INITIALIZER("r9"), S8_INITIALIZER("r10"), S8_INITIALIZER("r11"),
        S8_INITIALIZER("r12"), S8_INITIALIZER("r13"), S8_INITIALIZER("r14"), S8_INITIALIZER("r15"),
    };
    static String8 const names_32[] = {
        S8_INITIALIZER("eax"), S8_INITIALIZER("ecx"), S8_INITIALIZER("edx"), S8_INITIALIZER("ebx"),
        S8_INITIALIZER("esp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("esi"), S8_INITIALIZER("edi"),
        S8_INITIALIZER("r8d"), S8_INITIALIZER("r9d"), S8_INITIALIZER("r10d"), S8_INITIALIZER("r11d"),
        S8_INITIALIZER("r12d"), S8_INITIALIZER("r13d"), S8_INITIALIZER("r14d"), S8_INITIALIZER("r15d"),
    };
    static String8 const names_16[] = {
        S8_INITIALIZER("ax"), S8_INITIALIZER("cx"), S8_INITIALIZER("dx"), S8_INITIALIZER("bx"),
        S8_INITIALIZER("sp"), S8_INITIALIZER("bp"), S8_INITIALIZER("si"), S8_INITIALIZER("di"),
        S8_INITIALIZER("r8w"), S8_INITIALIZER("r9w"), S8_INITIALIZER("r10w"), S8_INITIALIZER("r11w"),
        S8_INITIALIZER("r12w"), S8_INITIALIZER("r13w"), S8_INITIALIZER("r14w"), S8_INITIALIZER("r15w"),
    };
    static String8 const names_high_8[] = {
        S8_INITIALIZER("ah"), S8_INITIALIZER("ch"), S8_INITIALIZER("dh"), S8_INITIALIZER("bh"),
    };
    static String8 const names_8[] = {
        S8_INITIALIZER("al"), S8_INITIALIZER("cl"), S8_INITIALIZER("dl"), S8_INITIALIZER("bl"),
        S8_INITIALIZER("spl"), S8_INITIALIZER("bpl"), S8_INITIALIZER("sil"), S8_INITIALIZER("dil"),
        S8_INITIALIZER("r8b"), S8_INITIALIZER("r9b"), S8_INITIALIZER("r10b"), S8_INITIALIZER("r11b"),
        S8_INITIALIZER("r12b"), S8_INITIALIZER("r13b"), S8_INITIALIZER("r14b"), S8_INITIALIZER("r15b"),
    };
    static const struct
    {
        String8 const* names;
        u16 width;
    } groups[] = {
        {names_64, 64},
        {names_32, 32},
        {names_16, 16},
        {names_8, 8},
    };
    for (u32 group_index = 0; group_index < BUSTER_ARRAY_LENGTH(groups); group_index += 1)
    {
        for (u32 register_index = 0; register_index < 16; register_index += 1)
        {
            if (assembly_word_equal(text, groups[group_index].names[register_index]))
            {
                *result = (AssemblyRegister){.index = (u8)register_index, .width = groups[group_index].width};
                return true;
            }
        }
    }
    for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(names_high_8); register_index += 1)
    {
        if (assembly_word_equal(text, names_high_8[register_index]))
        {
            *result = (AssemblyRegister){.index = (u8)(register_index + 4), .width = 8, .high_byte = true};
            return true;
        }
    }
    static String8 const names_xmm[] = {
        S8_INITIALIZER("xmm0"), S8_INITIALIZER("xmm1"), S8_INITIALIZER("xmm2"), S8_INITIALIZER("xmm3"),
        S8_INITIALIZER("xmm4"), S8_INITIALIZER("xmm5"), S8_INITIALIZER("xmm6"), S8_INITIALIZER("xmm7"),
        S8_INITIALIZER("xmm8"), S8_INITIALIZER("xmm9"), S8_INITIALIZER("xmm10"), S8_INITIALIZER("xmm11"),
        S8_INITIALIZER("xmm12"), S8_INITIALIZER("xmm13"), S8_INITIALIZER("xmm14"), S8_INITIALIZER("xmm15"),
    };
    for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(names_xmm); register_index += 1)
    {
        if (assembly_word_equal(text, names_xmm[register_index]))
        {
            *result = (AssemblyRegister){.index = (u8)register_index, .width = 128, .class = ASSEMBLY_REGISTER_XMM};
            return true;
        }
    }
    static String8 const names_ymm[] = {
        S8_INITIALIZER("ymm0"), S8_INITIALIZER("ymm1"), S8_INITIALIZER("ymm2"), S8_INITIALIZER("ymm3"),
        S8_INITIALIZER("ymm4"), S8_INITIALIZER("ymm5"), S8_INITIALIZER("ymm6"), S8_INITIALIZER("ymm7"),
        S8_INITIALIZER("ymm8"), S8_INITIALIZER("ymm9"), S8_INITIALIZER("ymm10"), S8_INITIALIZER("ymm11"),
        S8_INITIALIZER("ymm12"), S8_INITIALIZER("ymm13"), S8_INITIALIZER("ymm14"), S8_INITIALIZER("ymm15"),
    };
    for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(names_ymm); register_index += 1)
    {
        if (assembly_word_equal(text, names_ymm[register_index]))
        {
            *result = (AssemblyRegister){.index = (u8)register_index, .width = 256, .class = ASSEMBLY_REGISTER_YMM};
            return true;
        }
    }
    if (assembly_numbered_register_parse(text, S8("xmm"), 31, 128, ASSEMBLY_REGISTER_XMM, result) ||
        assembly_numbered_register_parse(text, S8("ymm"), 31, 256, ASSEMBLY_REGISTER_YMM, result) ||
        assembly_numbered_register_parse(text, S8("zmm"), 31, 512, ASSEMBLY_REGISTER_ZMM, result) ||
        assembly_numbered_register_parse(text, S8("k"), 7, 64, ASSEMBLY_REGISTER_OPMASK, result) ||
        assembly_numbered_register_parse(text, S8("tmm"), 7, 0, ASSEMBLY_REGISTER_TILE, result))
    {
        return true;
    }
    static String8 const names_mmx[] = {
        S8_INITIALIZER("mm0"), S8_INITIALIZER("mm1"), S8_INITIALIZER("mm2"), S8_INITIALIZER("mm3"),
        S8_INITIALIZER("mm4"), S8_INITIALIZER("mm5"), S8_INITIALIZER("mm6"), S8_INITIALIZER("mm7"),
    };
    for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(names_mmx); register_index += 1)
    {
        if (assembly_word_equal(text, names_mmx[register_index]))
        {
            *result = (AssemblyRegister){.index = (u8)register_index, .width = 64, .class = ASSEMBLY_REGISTER_MMX};
            return true;
        }
    }
    if (assembly_word_equal(text, S8("st")))
    {
        *result = (AssemblyRegister){.width = 80, .class = ASSEMBLY_REGISTER_X87};
        return true;
    }
    if (text.length == 5 && assembly_ascii_lower(text.pointer[0]) == 's' && assembly_ascii_lower(text.pointer[1]) == 't' &&
        text.pointer[2] == '(' && text.pointer[4] == ')' && text.pointer[3] >= '0' && text.pointer[3] <= '7')
    {
        *result = (AssemblyRegister){.index = (u8)(text.pointer[3] - '0'), .width = 80, .class = ASSEMBLY_REGISTER_X87};
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_rip_parse(String8 text, AssemblySyntax syntax)
{
    text = assembly_trim(text);
    if (syntax == ASSEMBLY_SYNTAX_ATT)
    {
        if (!text.length || text.pointer[0] != '%')
        {
            return false;
        }
        text.pointer += 1;
        text.length -= 1;
    }
    return assembly_word_equal(text, S8("rip"));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_scale_parse(String8 text, u8* result)
{
    s64 value = 0;
    if (!assembly_parse_s64(text, &value) || (value != 1 && value != 2 && value != 4 && value != 8))
    {
        return false;
    }
    *result = (u8)value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_expression_merge(AssemblyBuilder* builder, AssemblyExpression* destination, String8 text, bool subtract)
{
    AssemblyExpression expression = {0};
    if (!assembly_expression_parse(builder, text, &expression) || (destination->has_symbol && expression.has_symbol))
    {
        return false;
    }
    if (subtract && expression.has_symbol)
    {
        return false;
    }
    if ((!subtract && expression.addend > 0 && destination->addend > INT64_MAX - expression.addend) ||
        (!subtract && expression.addend < 0 && destination->addend < INT64_MIN - expression.addend) ||
        (subtract && expression.addend < 0 && destination->addend > INT64_MAX + expression.addend) ||
        (subtract && expression.addend > 0 && destination->addend < INT64_MIN + expression.addend))
    {
        return false;
    }
    if (subtract && expression.addend == INT64_MIN)
    {
        destination->addend += INT64_MAX;
        destination->addend += 1;
    }
    else
    {
        destination->addend += subtract ? -expression.addend : expression.addend;
    }
    if (expression.has_symbol)
    {
        destination->has_symbol = true;
        destination->symbol = expression.symbol;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_parse_intel(AssemblyBuilder* builder, String8 text, AssemblyMemory* result)
{
    text = assembly_trim(text);
    static const struct
    {
        String8 prefix;
        u16 width;
    } qualifiers[] = {
        {S8_INITIALIZER("byte ptr"), 8},
        {S8_INITIALIZER("word ptr"), 16},
        {S8_INITIALIZER("dword ptr"), 32},
        {S8_INITIALIZER("qword ptr"), 64},
        {S8_INITIALIZER("tbyte ptr"), 80},
        {S8_INITIALIZER("xmmword ptr"), 128},
        {S8_INITIALIZER("ymmword ptr"), 256},
        {S8_INITIALIZER("zmmword ptr"), 512},
    };
    for (u32 qualifier_index = 0; qualifier_index < BUSTER_ARRAY_LENGTH(qualifiers); qualifier_index += 1)
    {
        String8 prefix = qualifiers[qualifier_index].prefix;
        if (text.length > prefix.length && assembly_word_equal(string_slice(text, 0, prefix.length), prefix) &&
            assembly_space(text.pointer[prefix.length]))
        {
            result->width = qualifiers[qualifier_index].width;
            text = assembly_trim(string_slice(text, prefix.length, text.length));
            break;
        }
    }
    if (text.length < 2 || text.pointer[0] != '[' || text.pointer[text.length - 1] != ']')
    {
        return false;
    }
    text = assembly_trim(string_slice(text, 1, text.length - 1));
    if (!text.length)
    {
        return false;
    }
    result->scale = 1;
    u64 cursor = 0;
    bool subtract = false;
    while (cursor < text.length)
    {
        while (cursor < text.length && assembly_space(text.pointer[cursor]))
        {
            cursor += 1;
        }
        if (cursor < text.length && (text.pointer[cursor] == '+' || text.pointer[cursor] == '-'))
        {
            subtract = text.pointer[cursor] == '-';
            cursor += 1;
        }
        u64 end = cursor;
        while (end < text.length && text.pointer[end] != '+' && text.pointer[end] != '-')
        {
            end += 1;
        }
        String8 term = assembly_trim(string_slice(text, cursor, end));
        if (!term.length)
        {
            return false;
        }
        u64 star = string_first_code_unit(term, '*');
        AssemblyRegister reg = {0};
        if (star < term.length)
        {
            String8 register_text = assembly_trim(string_slice(term, 0, star));
            String8 scale_text = assembly_trim(string_slice(term, star + 1, term.length));
            if (subtract || result->has_index || !assembly_register_parse(register_text, ASSEMBLY_SYNTAX_INTEL, &reg) || reg.width != 64 ||
                !assembly_x86_scale_parse(scale_text, &result->scale) || reg.index == 4)
            {
                return false;
            }
            result->index = reg;
            result->has_index = true;
        }
        else if (assembly_x86_rip_parse(term, ASSEMBLY_SYNTAX_INTEL))
        {
            if (subtract || result->rip_relative || result->has_base || result->has_index)
            {
                return false;
            }
            result->rip_relative = true;
        }
        else if (assembly_register_parse(term, ASSEMBLY_SYNTAX_INTEL, &reg))
        {
            if (subtract || reg.width != 64)
            {
                return false;
            }
            if (!result->has_base)
            {
                result->base = reg;
                result->has_base = true;
            }
            else if (!result->has_index && reg.index != 4)
            {
                result->index = reg;
                result->has_index = true;
            }
            else
            {
                return false;
            }
        }
        else if (!assembly_expression_merge(builder, &result->displacement, term, subtract))
        {
            return false;
        }
        cursor = end;
        subtract = false;
    }
    return !result->rip_relative || (!result->has_base && !result->has_index);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_parse_att(AssemblyBuilder* builder, String8 text, AssemblyMemory* result)
{
    text = assembly_trim(text);
    u64 open = string_first_code_unit(text, '(');
    if (open == text.length || text.length < open + 2 || text.pointer[text.length - 1] != ')')
    {
        return false;
    }
    String8 displacement = assembly_trim(string_slice(text, 0, open));
    if (displacement.length && !assembly_expression_parse(builder, displacement, &result->displacement))
    {
        return false;
    }
    String8 address = string_slice(text, open + 1, text.length - 1);
    String8 fields[3] = {0};
    u32 field_count = 0;
    u64 field_start = 0;
    for (;;)
    {
        u64 field_end = field_start;
        while (field_end < address.length && address.pointer[field_end] != ',')
        {
            field_end += 1;
        }
        if (field_count >= BUSTER_ARRAY_LENGTH(fields))
        {
            return false;
        }
        fields[field_count++] = assembly_trim(string_slice(address, field_start, field_end));
        if (field_end == address.length)
        {
            break;
        }
        field_start = field_end + 1;
    }
    if (fields[0].length)
    {
        if (assembly_x86_rip_parse(fields[0], ASSEMBLY_SYNTAX_ATT))
        {
            result->rip_relative = true;
        }
        else if (!assembly_register_parse(fields[0], ASSEMBLY_SYNTAX_ATT, &result->base) || result->base.width != 64)
        {
            return false;
        }
        else
        {
            result->has_base = true;
        }
    }
    result->scale = 1;
    if (field_count >= 2 && fields[1].length)
    {
        if (result->rip_relative || !assembly_register_parse(fields[1], ASSEMBLY_SYNTAX_ATT, &result->index) ||
            result->index.width != 64 || result->index.index == 4)
        {
            return false;
        }
        result->has_index = true;
    }
    if (field_count == 3 && fields[2].length && (!result->has_index || !assembly_x86_scale_parse(fields[2], &result->scale)))
    {
        return false;
    }
    return result->rip_relative || result->has_base || result->has_index;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_parse(AssemblyBuilder* builder, String8 text, AssemblySyntax syntax, AssemblyMemory* result)
{
    *result = (AssemblyMemory){0};
    return syntax == ASSEMBLY_SYNTAX_ATT ? assembly_x86_memory_parse_att(builder, text, result)
                                         : assembly_x86_memory_parse_intel(builder, text, result);
}

typedef struct AssemblyInstructionInfo AssemblyInstructionInfo;
struct AssemblyInstructionInfo
{
    AssemblyOpcode opcode;
    u8 operand_count;
    u8 suffix_width;
    u8 source_width;
    u8 condition;
    bool no_flags;
};

BUSTER_GLOBAL_LOCAL bool assembly_x86_condition_parse(String8 name, u8* result)
{
    static const struct
    {
        String8 name;
        u8 condition;
    } conditions[] = {
        {S8_INITIALIZER("o"), 0},   {S8_INITIALIZER("no"), 1},  {S8_INITIALIZER("b"), 2},   {S8_INITIALIZER("c"), 2},
        {S8_INITIALIZER("nae"), 2}, {S8_INITIALIZER("ae"), 3},  {S8_INITIALIZER("nb"), 3},  {S8_INITIALIZER("nc"), 3},
        {S8_INITIALIZER("e"), 4},   {S8_INITIALIZER("z"), 4},   {S8_INITIALIZER("ne"), 5},  {S8_INITIALIZER("nz"), 5},
        {S8_INITIALIZER("be"), 6},  {S8_INITIALIZER("na"), 6},  {S8_INITIALIZER("a"), 7},   {S8_INITIALIZER("nbe"), 7},
        {S8_INITIALIZER("s"), 8},   {S8_INITIALIZER("ns"), 9},  {S8_INITIALIZER("p"), 10},  {S8_INITIALIZER("pe"), 10},
        {S8_INITIALIZER("np"), 11}, {S8_INITIALIZER("po"), 11}, {S8_INITIALIZER("l"), 12},  {S8_INITIALIZER("nge"), 12},
        {S8_INITIALIZER("ge"), 13}, {S8_INITIALIZER("nl"), 13}, {S8_INITIALIZER("le"), 14}, {S8_INITIALIZER("ng"), 14},
        {S8_INITIALIZER("g"), 15},  {S8_INITIALIZER("nle"), 15},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(conditions); index += 1)
    {
        if (assembly_word_equal(name, conditions[index].name))
        {
            *result = conditions[index].condition;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_lookup_exact(String8 mnemonic, AssemblyInstructionInfo* result)
{
    static const struct
    {
        String8 name;
        AssemblyOpcode opcode;
        u8 operand_count;
    } instructions[] = {
        {S8_INITIALIZER("nop"), ASSEMBLY_OPCODE_X86_NOP, 0},       {S8_INITIALIZER("ret"), ASSEMBLY_OPCODE_X86_RET, 0},
        {S8_INITIALIZER("int3"), ASSEMBLY_OPCODE_X86_INT3, 0},    {S8_INITIALIZER("call"), ASSEMBLY_OPCODE_X86_CALL, 1},
        {S8_INITIALIZER("jmp"), ASSEMBLY_OPCODE_X86_JMP, 1},      {S8_INITIALIZER("mov"), ASSEMBLY_OPCODE_X86_MOV, 2},
        {S8_INITIALIZER("add"), ASSEMBLY_OPCODE_X86_ADD, 2},      {S8_INITIALIZER("adc"), ASSEMBLY_OPCODE_X86_ADC, 2},
        {S8_INITIALIZER("sub"), ASSEMBLY_OPCODE_X86_SUB, 2},      {S8_INITIALIZER("sbb"), ASSEMBLY_OPCODE_X86_SBB, 2},
        {S8_INITIALIZER("and"), ASSEMBLY_OPCODE_X86_AND, 2},      {S8_INITIALIZER("or"), ASSEMBLY_OPCODE_X86_OR, 2},
        {S8_INITIALIZER("xor"), ASSEMBLY_OPCODE_X86_XOR, 2},      {S8_INITIALIZER("cmp"), ASSEMBLY_OPCODE_X86_CMP, 2},
        {S8_INITIALIZER("test"), ASSEMBLY_OPCODE_X86_TEST, 2},    {S8_INITIALIZER("imul"), ASSEMBLY_OPCODE_X86_IMUL, 2},
        {S8_INITIALIZER("mul"), ASSEMBLY_OPCODE_X86_MUL, 1},      {S8_INITIALIZER("div"), ASSEMBLY_OPCODE_X86_DIV, 1},
        {S8_INITIALIZER("idiv"), ASSEMBLY_OPCODE_X86_IDIV, 1},
        {S8_INITIALIZER("push"), ASSEMBLY_OPCODE_X86_PUSH, 1},    {S8_INITIALIZER("pop"), ASSEMBLY_OPCODE_X86_POP, 1},
        {S8_INITIALIZER("inc"), ASSEMBLY_OPCODE_X86_INC, 1},      {S8_INITIALIZER("dec"), ASSEMBLY_OPCODE_X86_DEC, 1},
        {S8_INITIALIZER("neg"), ASSEMBLY_OPCODE_X86_NEG, 1},      {S8_INITIALIZER("not"), ASSEMBLY_OPCODE_X86_NOT, 1},
        {S8_INITIALIZER("shl"), ASSEMBLY_OPCODE_X86_SHL, 2},      {S8_INITIALIZER("sal"), ASSEMBLY_OPCODE_X86_SHL, 2},
        {S8_INITIALIZER("shr"), ASSEMBLY_OPCODE_X86_SHR, 2},      {S8_INITIALIZER("sar"), ASSEMBLY_OPCODE_X86_SAR, 2},
        {S8_INITIALIZER("lea"), ASSEMBLY_OPCODE_X86_LEA, 2},      {S8_INITIALIZER("movzx"), ASSEMBLY_OPCODE_X86_MOVZX, 2},
        {S8_INITIALIZER("movsx"), ASSEMBLY_OPCODE_X86_MOVSX, 2},  {S8_INITIALIZER("movsxd"), ASSEMBLY_OPCODE_X86_MOVSXD, 2},
        {S8_INITIALIZER("rol"), ASSEMBLY_OPCODE_X86_ROL, 2},      {S8_INITIALIZER("ror"), ASSEMBLY_OPCODE_X86_ROR, 2},
        {S8_INITIALIZER("rcl"), ASSEMBLY_OPCODE_X86_RCL, 2},      {S8_INITIALIZER("rcr"), ASSEMBLY_OPCODE_X86_RCR, 2},
        {S8_INITIALIZER("shld"), ASSEMBLY_OPCODE_X86_SHLD, 3},    {S8_INITIALIZER("shrd"), ASSEMBLY_OPCODE_X86_SHRD, 3},
        {S8_INITIALIZER("bsf"), ASSEMBLY_OPCODE_X86_BSF, 2},      {S8_INITIALIZER("bsr"), ASSEMBLY_OPCODE_X86_BSR, 2},
        {S8_INITIALIZER("bswap"), ASSEMBLY_OPCODE_X86_BSWAP, 1},
        {S8_INITIALIZER("bt"), ASSEMBLY_OPCODE_X86_BT, 2},        {S8_INITIALIZER("btc"), ASSEMBLY_OPCODE_X86_BTC, 2},
        {S8_INITIALIZER("btr"), ASSEMBLY_OPCODE_X86_BTR, 2},      {S8_INITIALIZER("bts"), ASSEMBLY_OPCODE_X86_BTS, 2},
        {S8_INITIALIZER("xchg"), ASSEMBLY_OPCODE_X86_XCHG, 2},    {S8_INITIALIZER("xadd"), ASSEMBLY_OPCODE_X86_XADD, 2},
        {S8_INITIALIZER("cmpxchg"), ASSEMBLY_OPCODE_X86_CMPXCHG, 2},
        {S8_INITIALIZER("cmpxchg8b"), ASSEMBLY_OPCODE_X86_CMPXCHG8B, 1},
        {S8_INITIALIZER("cmpxchg16b"), ASSEMBLY_OPCODE_X86_CMPXCHG16B, 1},
        {S8_INITIALIZER("popcnt"), ASSEMBLY_OPCODE_X86_POPCNT, 2},
        {S8_INITIALIZER("lzcnt"), ASSEMBLY_OPCODE_X86_LZCNT, 2},
        {S8_INITIALIZER("tzcnt"), ASSEMBLY_OPCODE_X86_TZCNT, 2},
        {S8_INITIALIZER("cbw"), ASSEMBLY_OPCODE_X86_CBW, 0},      {S8_INITIALIZER("cwde"), ASSEMBLY_OPCODE_X86_CWDE, 0},
        {S8_INITIALIZER("cdqe"), ASSEMBLY_OPCODE_X86_CDQE, 0},    {S8_INITIALIZER("cbtw"), ASSEMBLY_OPCODE_X86_CBW, 0},
        {S8_INITIALIZER("cwtl"), ASSEMBLY_OPCODE_X86_CWDE, 0},    {S8_INITIALIZER("cltq"), ASSEMBLY_OPCODE_X86_CDQE, 0},
        {S8_INITIALIZER("cwd"), ASSEMBLY_OPCODE_X86_CWD, 0},      {S8_INITIALIZER("cdq"), ASSEMBLY_OPCODE_X86_CDQ, 0},
        {S8_INITIALIZER("cqo"), ASSEMBLY_OPCODE_X86_CQO, 0},      {S8_INITIALIZER("cwtd"), ASSEMBLY_OPCODE_X86_CWD, 0},
        {S8_INITIALIZER("cltd"), ASSEMBLY_OPCODE_X86_CDQ, 0},     {S8_INITIALIZER("cqto"), ASSEMBLY_OPCODE_X86_CQO, 0},
        {S8_INITIALIZER("movaps"), ASSEMBLY_OPCODE_X86_MOVAPS, 2}, {S8_INITIALIZER("movups"), ASSEMBLY_OPCODE_X86_MOVUPS, 2},
        {S8_INITIALIZER("movapd"), ASSEMBLY_OPCODE_X86_MOVAPD, 2}, {S8_INITIALIZER("movupd"), ASSEMBLY_OPCODE_X86_MOVUPD, 2},
        {S8_INITIALIZER("movdqa"), ASSEMBLY_OPCODE_X86_MOVDQA, 2}, {S8_INITIALIZER("movdqu"), ASSEMBLY_OPCODE_X86_MOVDQU, 2},
        {S8_INITIALIZER("xorps"), ASSEMBLY_OPCODE_X86_XORPS, 2},   {S8_INITIALIZER("xorpd"), ASSEMBLY_OPCODE_X86_XORPD, 2},
        {S8_INITIALIZER("pxor"), ASSEMBLY_OPCODE_X86_PXOR_MMX, 2}, {S8_INITIALIZER("addps"), ASSEMBLY_OPCODE_X86_ADDPS, 2},
        {S8_INITIALIZER("addpd"), ASSEMBLY_OPCODE_X86_ADDPD, 2},   {S8_INITIALIZER("addss"), ASSEMBLY_OPCODE_X86_ADDSS, 2},
        {S8_INITIALIZER("addsd"), ASSEMBLY_OPCODE_X86_ADDSD, 2},   {S8_INITIALIZER("subps"), ASSEMBLY_OPCODE_X86_SUBPS, 2},
        {S8_INITIALIZER("subpd"), ASSEMBLY_OPCODE_X86_SUBPD, 2},   {S8_INITIALIZER("mulps"), ASSEMBLY_OPCODE_X86_MULPS, 2},
        {S8_INITIALIZER("mulpd"), ASSEMBLY_OPCODE_X86_MULPD, 2},   {S8_INITIALIZER("divps"), ASSEMBLY_OPCODE_X86_DIVPS, 2},
        {S8_INITIALIZER("divpd"), ASSEMBLY_OPCODE_X86_DIVPD, 2},
        {S8_INITIALIZER("vmovaps"), ASSEMBLY_OPCODE_X86_VMOVAPS, 2}, {S8_INITIALIZER("vmovups"), ASSEMBLY_OPCODE_X86_VMOVUPS, 2},
        {S8_INITIALIZER("vmovapd"), ASSEMBLY_OPCODE_X86_VMOVAPD, 2}, {S8_INITIALIZER("vmovupd"), ASSEMBLY_OPCODE_X86_VMOVUPD, 2},
        {S8_INITIALIZER("vxorps"), ASSEMBLY_OPCODE_X86_VXORPS, 3},   {S8_INITIALIZER("vxorpd"), ASSEMBLY_OPCODE_X86_VXORPD, 3},
        {S8_INITIALIZER("vaddps"), ASSEMBLY_OPCODE_X86_VADDPS, 3},   {S8_INITIALIZER("vaddpd"), ASSEMBLY_OPCODE_X86_VADDPD, 3},
        {S8_INITIALIZER("vaddss"), ASSEMBLY_OPCODE_X86_VADDSS, 3},   {S8_INITIALIZER("vaddsd"), ASSEMBLY_OPCODE_X86_VADDSD, 3},
        {S8_INITIALIZER("vsubps"), ASSEMBLY_OPCODE_X86_VSUBPS, 3},   {S8_INITIALIZER("vsubpd"), ASSEMBLY_OPCODE_X86_VSUBPD, 3},
        {S8_INITIALIZER("vmulps"), ASSEMBLY_OPCODE_X86_VMULPS, 3},   {S8_INITIALIZER("vmulpd"), ASSEMBLY_OPCODE_X86_VMULPD, 3},
        {S8_INITIALIZER("vdivps"), ASSEMBLY_OPCODE_X86_VDIVPS, 3},   {S8_INITIALIZER("vdivpd"), ASSEMBLY_OPCODE_X86_VDIVPD, 3},
        {S8_INITIALIZER("vmovdqa"), ASSEMBLY_OPCODE_X86_VMOVDQA, 2}, {S8_INITIALIZER("vmovdqu"), ASSEMBLY_OPCODE_X86_VMOVDQU, 2},
        {S8_INITIALIZER("vpaddb"), ASSEMBLY_OPCODE_X86_VPADDB, 3},   {S8_INITIALIZER("vpaddw"), ASSEMBLY_OPCODE_X86_VPADDW, 3},
        {S8_INITIALIZER("vpaddd"), ASSEMBLY_OPCODE_X86_VPADDD, 3},   {S8_INITIALIZER("vpaddq"), ASSEMBLY_OPCODE_X86_VPADDQ, 3},
        {S8_INITIALIZER("vpsubb"), ASSEMBLY_OPCODE_X86_VPSUBB, 3},   {S8_INITIALIZER("vpsubw"), ASSEMBLY_OPCODE_X86_VPSUBW, 3},
        {S8_INITIALIZER("vpsubd"), ASSEMBLY_OPCODE_X86_VPSUBD, 3},   {S8_INITIALIZER("vpsubq"), ASSEMBLY_OPCODE_X86_VPSUBQ, 3},
        {S8_INITIALIZER("vpand"), ASSEMBLY_OPCODE_X86_VPAND, 3},     {S8_INITIALIZER("vpor"), ASSEMBLY_OPCODE_X86_VPOR, 3},
        {S8_INITIALIZER("vpxor"), ASSEMBLY_OPCODE_X86_VPXOR, 3},     {S8_INITIALIZER("vpcmpeqb"), ASSEMBLY_OPCODE_X86_VPCMPEQB, 3},
        {S8_INITIALIZER("vpcmpeqw"), ASSEMBLY_OPCODE_X86_VPCMPEQW, 3}, {S8_INITIALIZER("vpcmpeqd"), ASSEMBLY_OPCODE_X86_VPCMPEQD, 3},
        {S8_INITIALIZER("vpcmpeqq"), ASSEMBLY_OPCODE_X86_VPCMPEQQ, 3}, {S8_INITIALIZER("vpcmpgtb"), ASSEMBLY_OPCODE_X86_VPCMPGTB, 3},
        {S8_INITIALIZER("vpcmpgtw"), ASSEMBLY_OPCODE_X86_VPCMPGTW, 3}, {S8_INITIALIZER("vpcmpgtd"), ASSEMBLY_OPCODE_X86_VPCMPGTD, 3},
        {S8_INITIALIZER("vpcmpgtq"), ASSEMBLY_OPCODE_X86_VPCMPGTQ, 3}, {S8_INITIALIZER("vpmullw"), ASSEMBLY_OPCODE_X86_VPMULLW, 3},
        {S8_INITIALIZER("vpmulld"), ASSEMBLY_OPCODE_X86_VPMULLD, 3},
        {S8_INITIALIZER("vmovdqa32"), ASSEMBLY_OPCODE_X86_VMOVDQA32, 2},
        {S8_INITIALIZER("vmovdqu32"), ASSEMBLY_OPCODE_X86_VMOVDQU32, 2},
        {S8_INITIALIZER("vmovdqa64"), ASSEMBLY_OPCODE_X86_VMOVDQA64, 2},
        {S8_INITIALIZER("vmovdqu64"), ASSEMBLY_OPCODE_X86_VMOVDQU64, 2},
        {S8_INITIALIZER("vmovdqu8"), ASSEMBLY_OPCODE_X86_VMOVDQU8, 2},
        {S8_INITIALIZER("vmovdqu16"), ASSEMBLY_OPCODE_X86_VMOVDQU16, 2},
        {S8_INITIALIZER("vcmpps"), ASSEMBLY_OPCODE_X86_VCMPPS, 4},
        {S8_INITIALIZER("vcmppd"), ASSEMBLY_OPCODE_X86_VCMPPD, 4},
        {S8_INITIALIZER("vpcmpd"), ASSEMBLY_OPCODE_X86_VPCMPD, 4},
        {S8_INITIALIZER("vpcmpq"), ASSEMBLY_OPCODE_X86_VPCMPQ, 4},
        {S8_INITIALIZER("vpcmpb"), ASSEMBLY_OPCODE_X86_VPCMPB, 4},
        {S8_INITIALIZER("vpcmpw"), ASSEMBLY_OPCODE_X86_VPCMPW, 4},
        {S8_INITIALIZER("vpcmpud"), ASSEMBLY_OPCODE_X86_VPCMPUD, 4},
        {S8_INITIALIZER("vpcmpuq"), ASSEMBLY_OPCODE_X86_VPCMPUQ, 4},
        {S8_INITIALIZER("vrndscaleps"), ASSEMBLY_OPCODE_X86_VRNDSCALEPS, 3},
        {S8_INITIALIZER("vrndscalepd"), ASSEMBLY_OPCODE_X86_VRNDSCALEPD, 3},
        {S8_INITIALIZER("kmovw"), ASSEMBLY_OPCODE_X86_KMOVW, 2},
        {S8_INITIALIZER("kmovd"), ASSEMBLY_OPCODE_X86_KMOVD, 2},
        {S8_INITIALIZER("kmovq"), ASSEMBLY_OPCODE_X86_KMOVQ, 2},
        {S8_INITIALIZER("kaddw"), ASSEMBLY_OPCODE_X86_KADDW, 3},
        {S8_INITIALIZER("kandw"), ASSEMBLY_OPCODE_X86_KANDW, 3},
        {S8_INITIALIZER("korw"), ASSEMBLY_OPCODE_X86_KORW, 3},
        {S8_INITIALIZER("kxorw"), ASSEMBLY_OPCODE_X86_KXORW, 3},
        {S8_INITIALIZER("knotw"), ASSEMBLY_OPCODE_X86_KNOTW, 2},
        {S8_INITIALIZER("kortestw"), ASSEMBLY_OPCODE_X86_KORTESTW, 2},
        {S8_INITIALIZER("push2"), ASSEMBLY_OPCODE_X86_APX_PUSH2, 2},
        {S8_INITIALIZER("pop2"), ASSEMBLY_OPCODE_X86_APX_POP2, 2},
        {S8_INITIALIZER("ldtilecfg"), ASSEMBLY_OPCODE_X86_LDTILECFG, 1},
        {S8_INITIALIZER("sttilecfg"), ASSEMBLY_OPCODE_X86_STTILECFG, 1},
        {S8_INITIALIZER("tileloadd"), ASSEMBLY_OPCODE_X86_TILELOADD, 2},
        {S8_INITIALIZER("tileloaddt1"), ASSEMBLY_OPCODE_X86_TILELOADDT1, 2},
        {S8_INITIALIZER("tilestored"), ASSEMBLY_OPCODE_X86_TILESTORED, 2},
        {S8_INITIALIZER("tilezero"), ASSEMBLY_OPCODE_X86_TILEZERO, 1},
        {S8_INITIALIZER("tdpbf16ps"), ASSEMBLY_OPCODE_X86_TDPBF16PS, 3},
        {S8_INITIALIZER("tdpbssd"), ASSEMBLY_OPCODE_X86_TDPBSSD, 3},
        {S8_INITIALIZER("tdpbsud"), ASSEMBLY_OPCODE_X86_TDPBSUD, 3},
        {S8_INITIALIZER("tdpbusd"), ASSEMBLY_OPCODE_X86_TDPBUSD, 3},
        {S8_INITIALIZER("tdpbuud"), ASSEMBLY_OPCODE_X86_TDPBUUD, 3},
        {S8_INITIALIZER("emms"), ASSEMBLY_OPCODE_X86_EMMS, 0},
        {S8_INITIALIZER("paddb"), ASSEMBLY_OPCODE_X86_PADDB_MMX, 2}, {S8_INITIALIZER("paddw"), ASSEMBLY_OPCODE_X86_PADDW_MMX, 2},
        {S8_INITIALIZER("paddd"), ASSEMBLY_OPCODE_X86_PADDD_MMX, 2}, {S8_INITIALIZER("paddq"), ASSEMBLY_OPCODE_X86_PADDQ_MMX, 2},
        {S8_INITIALIZER("psubb"), ASSEMBLY_OPCODE_X86_PSUBB_MMX, 2}, {S8_INITIALIZER("psubw"), ASSEMBLY_OPCODE_X86_PSUBW_MMX, 2},
        {S8_INITIALIZER("psubd"), ASSEMBLY_OPCODE_X86_PSUBD_MMX, 2}, {S8_INITIALIZER("psubq"), ASSEMBLY_OPCODE_X86_PSUBQ_MMX, 2},
        {S8_INITIALIZER("pand"), ASSEMBLY_OPCODE_X86_PAND_MMX, 2},   {S8_INITIALIZER("por"), ASSEMBLY_OPCODE_X86_POR_MMX, 2},
        {S8_INITIALIZER("pcmpeqb"), ASSEMBLY_OPCODE_X86_PCMPEQB_MMX, 2},
        {S8_INITIALIZER("pcmpeqw"), ASSEMBLY_OPCODE_X86_PCMPEQW_MMX, 2},
        {S8_INITIALIZER("pcmpeqd"), ASSEMBLY_OPCODE_X86_PCMPEQD_MMX, 2},
        {S8_INITIALIZER("pcmpgtb"), ASSEMBLY_OPCODE_X86_PCMPGTB_MMX, 2},
        {S8_INITIALIZER("pcmpgtw"), ASSEMBLY_OPCODE_X86_PCMPGTW_MMX, 2},
        {S8_INITIALIZER("pcmpgtd"), ASSEMBLY_OPCODE_X86_PCMPGTD_MMX, 2},
        {S8_INITIALIZER("pmullw"), ASSEMBLY_OPCODE_X86_PMULLW_MMX, 2},
        {S8_INITIALIZER("fld"), ASSEMBLY_OPCODE_X86_FLD, 1},       {S8_INITIALIZER("fst"), ASSEMBLY_OPCODE_X86_FST, 1},
        {S8_INITIALIZER("fstp"), ASSEMBLY_OPCODE_X86_FSTP, 1},     {S8_INITIALIZER("fild"), ASSEMBLY_OPCODE_X86_FILD, 1},
        {S8_INITIALIZER("fist"), ASSEMBLY_OPCODE_X86_FIST, 1},     {S8_INITIALIZER("fistp"), ASSEMBLY_OPCODE_X86_FISTP, 1},
        {S8_INITIALIZER("fisttp"), ASSEMBLY_OPCODE_X86_FISTTP, 1},
        {S8_INITIALIZER("fadd"), ASSEMBLY_OPCODE_X86_FADD, 2},     {S8_INITIALIZER("fmul"), ASSEMBLY_OPCODE_X86_FMUL, 2},
        {S8_INITIALIZER("fsub"), ASSEMBLY_OPCODE_X86_FSUB, 2},     {S8_INITIALIZER("fsubr"), ASSEMBLY_OPCODE_X86_FSUBR, 2},
        {S8_INITIALIZER("fdiv"), ASSEMBLY_OPCODE_X86_FDIV, 2},     {S8_INITIALIZER("fdivr"), ASSEMBLY_OPCODE_X86_FDIVR, 2},
        {S8_INITIALIZER("faddp"), ASSEMBLY_OPCODE_X86_FADDP, 2},   {S8_INITIALIZER("fmulp"), ASSEMBLY_OPCODE_X86_FMULP, 2},
        {S8_INITIALIZER("fsubp"), ASSEMBLY_OPCODE_X86_FSUBP, 2},   {S8_INITIALIZER("fsubrp"), ASSEMBLY_OPCODE_X86_FSUBRP, 2},
        {S8_INITIALIZER("fdivp"), ASSEMBLY_OPCODE_X86_FDIVP, 2},   {S8_INITIALIZER("fdivrp"), ASSEMBLY_OPCODE_X86_FDIVRP, 2},
        {S8_INITIALIZER("fxch"), ASSEMBLY_OPCODE_X86_FXCH, 1},     {S8_INITIALIZER("f2xm1"), ASSEMBLY_OPCODE_X86_F2XM1, 0},
        {S8_INITIALIZER("fcom"), ASSEMBLY_OPCODE_X86_FCOM, 1},     {S8_INITIALIZER("fcomp"), ASSEMBLY_OPCODE_X86_FCOMP, 1},
        {S8_INITIALIZER("fcompp"), ASSEMBLY_OPCODE_X86_FCOMPP, 0}, {S8_INITIALIZER("fucom"), ASSEMBLY_OPCODE_X86_FUCOM, 1},
        {S8_INITIALIZER("fucomp"), ASSEMBLY_OPCODE_X86_FUCOMP, 1}, {S8_INITIALIZER("fucompp"), ASSEMBLY_OPCODE_X86_FUCOMPP, 0},
        {S8_INITIALIZER("fcomi"), ASSEMBLY_OPCODE_X86_FCOMI, 2},   {S8_INITIALIZER("fcomip"), ASSEMBLY_OPCODE_X86_FCOMIP, 2},
        {S8_INITIALIZER("fcompi"), ASSEMBLY_OPCODE_X86_FCOMIP, 2}, {S8_INITIALIZER("fucomi"), ASSEMBLY_OPCODE_X86_FUCOMI, 2},
        {S8_INITIALIZER("fucomip"), ASSEMBLY_OPCODE_X86_FUCOMIP, 2}, {S8_INITIALIZER("fucompi"), ASSEMBLY_OPCODE_X86_FUCOMIP, 2},
        {S8_INITIALIZER("fiadd"), ASSEMBLY_OPCODE_X86_FIADD, 1},   {S8_INITIALIZER("fimul"), ASSEMBLY_OPCODE_X86_FIMUL, 1},
        {S8_INITIALIZER("fisub"), ASSEMBLY_OPCODE_X86_FISUB, 1},   {S8_INITIALIZER("fisubr"), ASSEMBLY_OPCODE_X86_FISUBR, 1},
        {S8_INITIALIZER("fidiv"), ASSEMBLY_OPCODE_X86_FIDIV, 1},   {S8_INITIALIZER("fidivr"), ASSEMBLY_OPCODE_X86_FIDIVR, 1},
        {S8_INITIALIZER("fbld"), ASSEMBLY_OPCODE_X86_FBLD, 1},     {S8_INITIALIZER("fbstp"), ASSEMBLY_OPCODE_X86_FBSTP, 1},
        {S8_INITIALIZER("fldcw"), ASSEMBLY_OPCODE_X86_FLDCW, 1},   {S8_INITIALIZER("fnstcw"), ASSEMBLY_OPCODE_X86_FNSTCW, 1},
        {S8_INITIALIZER("fstcw"), ASSEMBLY_OPCODE_X86_FSTCW, 1},   {S8_INITIALIZER("fldenv"), ASSEMBLY_OPCODE_X86_FLDENV, 1},
        {S8_INITIALIZER("fnstenv"), ASSEMBLY_OPCODE_X86_FNSTENV, 1}, {S8_INITIALIZER("fstenv"), ASSEMBLY_OPCODE_X86_FSTENV, 1},
        {S8_INITIALIZER("frstor"), ASSEMBLY_OPCODE_X86_FRSTOR, 1}, {S8_INITIALIZER("fnsave"), ASSEMBLY_OPCODE_X86_FNSAVE, 1},
        {S8_INITIALIZER("fsave"), ASSEMBLY_OPCODE_X86_FSAVE, 1},   {S8_INITIALIZER("fnstsw"), ASSEMBLY_OPCODE_X86_FNSTSW, 1},
        {S8_INITIALIZER("fstsw"), ASSEMBLY_OPCODE_X86_FSTSW, 1},   {S8_INITIALIZER("ffree"), ASSEMBLY_OPCODE_X86_FFREE, 1},
        {S8_INITIALIZER("ffreep"), ASSEMBLY_OPCODE_X86_FFREEP, 1}, {S8_INITIALIZER("fincstp"), ASSEMBLY_OPCODE_X86_FINCSTP, 0},
        {S8_INITIALIZER("fdecstp"), ASSEMBLY_OPCODE_X86_FDECSTP, 0},
        {S8_INITIALIZER("fabs"), ASSEMBLY_OPCODE_X86_FABS, 0},     {S8_INITIALIZER("fchs"), ASSEMBLY_OPCODE_X86_FCHS, 0},
        {S8_INITIALIZER("fld1"), ASSEMBLY_OPCODE_X86_FLD1, 0},     {S8_INITIALIZER("fldz"), ASSEMBLY_OPCODE_X86_FLDZ, 0},
        {S8_INITIALIZER("fldpi"), ASSEMBLY_OPCODE_X86_FLDPI, 0},   {S8_INITIALIZER("fldl2e"), ASSEMBLY_OPCODE_X86_FLDL2E, 0},
        {S8_INITIALIZER("fldl2t"), ASSEMBLY_OPCODE_X86_FLDL2T, 0}, {S8_INITIALIZER("fldlg2"), ASSEMBLY_OPCODE_X86_FLDLG2, 0},
        {S8_INITIALIZER("fldln2"), ASSEMBLY_OPCODE_X86_FLDLN2, 0}, {S8_INITIALIZER("fsqrt"), ASSEMBLY_OPCODE_X86_FSQRT, 0},
        {S8_INITIALIZER("fsin"), ASSEMBLY_OPCODE_X86_FSIN, 0},     {S8_INITIALIZER("fcos"), ASSEMBLY_OPCODE_X86_FCOS, 0},
        {S8_INITIALIZER("fsincos"), ASSEMBLY_OPCODE_X86_FSINCOS, 0}, {S8_INITIALIZER("fptan"), ASSEMBLY_OPCODE_X86_FPTAN, 0},
        {S8_INITIALIZER("fpatan"), ASSEMBLY_OPCODE_X86_FPATAN, 0}, {S8_INITIALIZER("fyl2x"), ASSEMBLY_OPCODE_X86_FYL2X, 0},
        {S8_INITIALIZER("fyl2xp1"), ASSEMBLY_OPCODE_X86_FYL2XP1, 0}, {S8_INITIALIZER("frndint"), ASSEMBLY_OPCODE_X86_FRNDINT, 0},
        {S8_INITIALIZER("fscale"), ASSEMBLY_OPCODE_X86_FSCALE, 0}, {S8_INITIALIZER("fprem"), ASSEMBLY_OPCODE_X86_FPREM, 0},
        {S8_INITIALIZER("fprem1"), ASSEMBLY_OPCODE_X86_FPREM1, 0}, {S8_INITIALIZER("fxtract"), ASSEMBLY_OPCODE_X86_FXTRACT, 0},
        {S8_INITIALIZER("ftst"), ASSEMBLY_OPCODE_X86_FTST, 0},     {S8_INITIALIZER("fxam"), ASSEMBLY_OPCODE_X86_FXAM, 0},
        {S8_INITIALIZER("fnop"), ASSEMBLY_OPCODE_X86_FNOP, 0},     {S8_INITIALIZER("finit"), ASSEMBLY_OPCODE_X86_FINIT, 0},
        {S8_INITIALIZER("fninit"), ASSEMBLY_OPCODE_X86_FNINIT, 0}, {S8_INITIALIZER("fclex"), ASSEMBLY_OPCODE_X86_FCLEX, 0},
        {S8_INITIALIZER("fnclex"), ASSEMBLY_OPCODE_X86_FNCLEX, 0}, {S8_INITIALIZER("fwait"), ASSEMBLY_OPCODE_X86_FWAIT, 0},
        {S8_INITIALIZER("wait"), ASSEMBLY_OPCODE_X86_FWAIT, 0},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
    {
        if (assembly_word_equal(mnemonic, instructions[index].name))
        {
            *result = (AssemblyInstructionInfo){.opcode = instructions[index].opcode, .operand_count = instructions[index].operand_count};
            return true;
        }
    }
    static String8 const x87_move_conditions[] = {
        S8_INITIALIZER("b"), S8_INITIALIZER("e"), S8_INITIALIZER("be"), S8_INITIALIZER("u"),
        S8_INITIALIZER("nb"), S8_INITIALIZER("ne"), S8_INITIALIZER("nbe"), S8_INITIALIZER("nu"),
    };
    String8 fcmov_prefix = S8("fcmov");
    if (mnemonic.length > fcmov_prefix.length &&
        assembly_word_equal(string_slice(mnemonic, 0, fcmov_prefix.length), fcmov_prefix))
    {
        String8 condition = string_slice(mnemonic, fcmov_prefix.length, mnemonic.length);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(x87_move_conditions); index += 1)
        {
            if (assembly_word_equal(condition, x87_move_conditions[index]))
            {
                *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_X86_FCMOVCC, .operand_count = 2, .condition = (u8)index};
                return true;
            }
        }
    }
    static const struct
    {
        String8 prefix;
        AssemblyOpcode opcode;
        u8 operand_count;
    } condition_families[] = {
        {S8_INITIALIZER("cmov"), ASSEMBLY_OPCODE_X86_CMOVCC, 2},
        {S8_INITIALIZER("set"), ASSEMBLY_OPCODE_X86_SETCC, 1},
        {S8_INITIALIZER("j"), ASSEMBLY_OPCODE_X86_JCC, 1},
    };
    for (u32 family_index = 0; family_index < BUSTER_ARRAY_LENGTH(condition_families); family_index += 1)
    {
        String8 prefix = condition_families[family_index].prefix;
        if (mnemonic.length > prefix.length && assembly_word_equal(string_slice(mnemonic, 0, prefix.length), prefix) &&
            assembly_x86_condition_parse(string_slice(mnemonic, prefix.length, mnemonic.length), &result->condition))
        {
            result->opcode = condition_families[family_index].opcode;
            result->operand_count = condition_families[family_index].operand_count;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_instruction_lookup(Target target, AssemblySyntax syntax, String8 mnemonic, AssemblyInstructionInfo* result)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (syntax == ASSEMBLY_SYNTAX_ATT)
        {
            static const struct
            {
                String8 name;
                AssemblyOpcode opcode;
                u8 width;
            } x87_memory_instructions[] = {
                {S8_INITIALIZER("flds"), ASSEMBLY_OPCODE_X86_FLD, 32},     {S8_INITIALIZER("fldl"), ASSEMBLY_OPCODE_X86_FLD, 64},
                {S8_INITIALIZER("fldt"), ASSEMBLY_OPCODE_X86_FLD, 80},     {S8_INITIALIZER("fsts"), ASSEMBLY_OPCODE_X86_FST, 32},
                {S8_INITIALIZER("fstl"), ASSEMBLY_OPCODE_X86_FST, 64},     {S8_INITIALIZER("fstps"), ASSEMBLY_OPCODE_X86_FSTP, 32},
                {S8_INITIALIZER("fstpl"), ASSEMBLY_OPCODE_X86_FSTP, 64},   {S8_INITIALIZER("fstpt"), ASSEMBLY_OPCODE_X86_FSTP, 80},
                {S8_INITIALIZER("filds"), ASSEMBLY_OPCODE_X86_FILD, 16},   {S8_INITIALIZER("fildl"), ASSEMBLY_OPCODE_X86_FILD, 32},
                {S8_INITIALIZER("fildq"), ASSEMBLY_OPCODE_X86_FILD, 64},   {S8_INITIALIZER("fildll"), ASSEMBLY_OPCODE_X86_FILD, 64},
                {S8_INITIALIZER("fists"), ASSEMBLY_OPCODE_X86_FIST, 16},   {S8_INITIALIZER("fistl"), ASSEMBLY_OPCODE_X86_FIST, 32},
                {S8_INITIALIZER("fistps"), ASSEMBLY_OPCODE_X86_FISTP, 16}, {S8_INITIALIZER("fistpl"), ASSEMBLY_OPCODE_X86_FISTP, 32},
                {S8_INITIALIZER("fistpq"), ASSEMBLY_OPCODE_X86_FISTP, 64}, {S8_INITIALIZER("fistpll"), ASSEMBLY_OPCODE_X86_FISTP, 64},
                {S8_INITIALIZER("fisttps"), ASSEMBLY_OPCODE_X86_FISTTP, 16}, {S8_INITIALIZER("fisttpl"), ASSEMBLY_OPCODE_X86_FISTTP, 32},
                {S8_INITIALIZER("fisttpq"), ASSEMBLY_OPCODE_X86_FISTTP, 64}, {S8_INITIALIZER("fisttpll"), ASSEMBLY_OPCODE_X86_FISTTP, 64},
                {S8_INITIALIZER("fadds"), ASSEMBLY_OPCODE_X86_FADD, 32},   {S8_INITIALIZER("faddl"), ASSEMBLY_OPCODE_X86_FADD, 64},
                {S8_INITIALIZER("fmuls"), ASSEMBLY_OPCODE_X86_FMUL, 32},   {S8_INITIALIZER("fmull"), ASSEMBLY_OPCODE_X86_FMUL, 64},
                {S8_INITIALIZER("fsubs"), ASSEMBLY_OPCODE_X86_FSUB, 32},   {S8_INITIALIZER("fsubl"), ASSEMBLY_OPCODE_X86_FSUB, 64},
                {S8_INITIALIZER("fsubrs"), ASSEMBLY_OPCODE_X86_FSUBR, 32}, {S8_INITIALIZER("fsubrl"), ASSEMBLY_OPCODE_X86_FSUBR, 64},
                {S8_INITIALIZER("fdivs"), ASSEMBLY_OPCODE_X86_FDIV, 32},   {S8_INITIALIZER("fdivl"), ASSEMBLY_OPCODE_X86_FDIV, 64},
                {S8_INITIALIZER("fdivrs"), ASSEMBLY_OPCODE_X86_FDIVR, 32}, {S8_INITIALIZER("fdivrl"), ASSEMBLY_OPCODE_X86_FDIVR, 64},
                {S8_INITIALIZER("fcoms"), ASSEMBLY_OPCODE_X86_FCOM, 32},   {S8_INITIALIZER("fcoml"), ASSEMBLY_OPCODE_X86_FCOM, 64},
                {S8_INITIALIZER("fcomps"), ASSEMBLY_OPCODE_X86_FCOMP, 32}, {S8_INITIALIZER("fcompl"), ASSEMBLY_OPCODE_X86_FCOMP, 64},
                {S8_INITIALIZER("fiadds"), ASSEMBLY_OPCODE_X86_FIADD, 16}, {S8_INITIALIZER("fiaddl"), ASSEMBLY_OPCODE_X86_FIADD, 32},
                {S8_INITIALIZER("fimuls"), ASSEMBLY_OPCODE_X86_FIMUL, 16}, {S8_INITIALIZER("fimull"), ASSEMBLY_OPCODE_X86_FIMUL, 32},
                {S8_INITIALIZER("fisubs"), ASSEMBLY_OPCODE_X86_FISUB, 16}, {S8_INITIALIZER("fisubl"), ASSEMBLY_OPCODE_X86_FISUB, 32},
                {S8_INITIALIZER("fisubrs"), ASSEMBLY_OPCODE_X86_FISUBR, 16}, {S8_INITIALIZER("fisubrl"), ASSEMBLY_OPCODE_X86_FISUBR, 32},
                {S8_INITIALIZER("fidivs"), ASSEMBLY_OPCODE_X86_FIDIV, 16}, {S8_INITIALIZER("fidivl"), ASSEMBLY_OPCODE_X86_FIDIV, 32},
                {S8_INITIALIZER("fidivrs"), ASSEMBLY_OPCODE_X86_FIDIVR, 16}, {S8_INITIALIZER("fidivrl"), ASSEMBLY_OPCODE_X86_FIDIVR, 32},
            };
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(x87_memory_instructions); index += 1)
            {
                if (assembly_word_equal(mnemonic, x87_memory_instructions[index].name))
                {
                    *result = (AssemblyInstructionInfo){
                        .opcode = x87_memory_instructions[index].opcode,
                        .operand_count = 1,
                        .suffix_width = x87_memory_instructions[index].width,
                    };
                    return true;
                }
            }
        }
        if (syntax == ASSEMBLY_SYNTAX_INTEL && assembly_word_equal(mnemonic, S8("movq")))
        {
            *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_X86_MOVQ_MMX, .operand_count = 2};
            return true;
        }
        if (syntax == ASSEMBLY_SYNTAX_ATT)
        {
            static const struct
            {
                String8 name;
                AssemblyOpcode opcode;
                u8 destination_width;
                u8 source_width;
            } scalar_move_aliases[] = {
                {S8_INITIALIZER("movzbw"), ASSEMBLY_OPCODE_X86_MOVZX, 16, 8},
                {S8_INITIALIZER("movzbl"), ASSEMBLY_OPCODE_X86_MOVZX, 32, 8},
                {S8_INITIALIZER("movzbq"), ASSEMBLY_OPCODE_X86_MOVZX, 64, 8},
                {S8_INITIALIZER("movzwl"), ASSEMBLY_OPCODE_X86_MOVZX, 32, 16},
                {S8_INITIALIZER("movzwq"), ASSEMBLY_OPCODE_X86_MOVZX, 64, 16},
                {S8_INITIALIZER("movsbw"), ASSEMBLY_OPCODE_X86_MOVSX, 16, 8},
                {S8_INITIALIZER("movsbl"), ASSEMBLY_OPCODE_X86_MOVSX, 32, 8},
                {S8_INITIALIZER("movsbq"), ASSEMBLY_OPCODE_X86_MOVSX, 64, 8},
                {S8_INITIALIZER("movswl"), ASSEMBLY_OPCODE_X86_MOVSX, 32, 16},
                {S8_INITIALIZER("movswq"), ASSEMBLY_OPCODE_X86_MOVSX, 64, 16},
                {S8_INITIALIZER("movslq"), ASSEMBLY_OPCODE_X86_MOVSXD, 64, 32},
            };
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(scalar_move_aliases); index += 1)
            {
                if (assembly_word_equal(mnemonic, scalar_move_aliases[index].name))
                {
                    *result = (AssemblyInstructionInfo){
                        .opcode = scalar_move_aliases[index].opcode,
                        .operand_count = 2,
                        .suffix_width = scalar_move_aliases[index].destination_width,
                        .source_width = scalar_move_aliases[index].source_width,
                    };
                    return true;
                }
            }
        }
        if (assembly_x86_instruction_lookup_exact(mnemonic, result))
        {
            return true;
        }
        if (mnemonic.length > 2 && assembly_word_equal(string_slice(mnemonic, mnemonic.length - 2, mnemonic.length), S8("nf")))
        {
            String8 base = string_slice(mnemonic, 0, mnemonic.length - 2);
            AssemblyInstructionInfo base_info = {0};
            if (assembly_x86_instruction_lookup_exact(base, &base_info))
            {
                base_info.no_flags = true;
                *result = base_info;
                return true;
            }
        }
        if (syntax == ASSEMBLY_SYNTAX_ATT && mnemonic.length > 1)
        {
            char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
            u8 width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64 : 0;
            if (width && mnemonic.length > 3 &&
                assembly_word_equal(string_slice(mnemonic, mnemonic.length - 3, mnemonic.length - 1), S8("nf")))
            {
                String8 base = string_slice(mnemonic, 0, mnemonic.length - 3);
                AssemblyInstructionInfo base_info = {0};
                if (assembly_x86_instruction_lookup_exact(base, &base_info))
                {
                    base_info.no_flags = true;
                    base_info.suffix_width = width;
                    *result = base_info;
                    return true;
                }
            }
            String8 base = {.pointer = mnemonic.pointer, .length = mnemonic.length - 1};
            if (width && assembly_x86_instruction_lookup_exact(base, result))
            {
                if (!result->operand_count && result->opcode != ASSEMBLY_OPCODE_X86_RET)
                {
                    return false;
                }
                if (result->opcode == ASSEMBLY_OPCODE_X86_JCC || result->opcode == ASSEMBLY_OPCODE_X86_SETCC)
                {
                    return false;
                }
                result->suffix_width = width;
                return true;
            }
        }
        return false;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (assembly_word_equal(mnemonic, S8("nop")))
        {
            *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_AARCH64_NOP};
        }
        else if (assembly_word_equal(mnemonic, S8("ret")))
        {
            *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_AARCH64_RET};
        }
        else if (assembly_word_equal(mnemonic, S8("b")))
        {
            *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_AARCH64_B, .operand_count = 1};
        }
        else if (assembly_word_equal(mnemonic, S8("bl")))
        {
            *result = (AssemblyInstructionInfo){.opcode = ASSEMBLY_OPCODE_AARCH64_BL, .operand_count = 1};
        }
        else
        {
            return false;
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_sse2(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_MOVAPS && opcode <= ASSEMBLY_OPCODE_X86_DIVPD;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_avx(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_VMOVAPS && opcode <= ASSEMBLY_OPCODE_X86_VPMULLD;
}

typedef enum AssemblyVectorFormFlags
{
    ASSEMBLY_VECTOR_FORM_MOVE = 1u << 0,
    ASSEMBLY_VECTOR_FORM_SCALAR = 1u << 1,
    ASSEMBLY_VECTOR_FORM_INTEGER = 1u << 2,
    ASSEMBLY_VECTOR_FORM_MASK_DESTINATION = 1u << 3,
    ASSEMBLY_VECTOR_FORM_IMMEDIATE = 1u << 4,
    ASSEMBLY_VECTOR_FORM_SOURCE_RM = 1u << 5,
    ASSEMBLY_VECTOR_FORM_AVX512BW = 1u << 6,
    ASSEMBLY_VECTOR_FORM_ROUNDING = 1u << 7,
} AssemblyVectorFormFlags;

typedef struct AssemblyVectorForm AssemblyVectorForm;
struct AssemblyVectorForm
{
    AssemblyOpcode opcode;
    u8 map;
    u8 mandatory_prefix;
    u8 opcode_byte;
    u8 element_width;
    u8 flags;
    bool wide;
};

BUSTER_GLOBAL_LOCAL AssemblyVectorForm const* assembly_x86_vector_form(AssemblyOpcode opcode)
{
    static AssemblyVectorForm const forms[] = {
        {ASSEMBLY_OPCODE_X86_VMOVAPS, 1, 0, 0x28, 4, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVUPS, 1, 0, 0x10, 4, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVAPD, 1, 1, 0x28, 8, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVUPD, 1, 1, 0x10, 8, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VXORPS, 1, 0, 0x57, 4, 0, true},
        {ASSEMBLY_OPCODE_X86_VXORPD, 1, 1, 0x57, 8, 0, true},
        {ASSEMBLY_OPCODE_X86_VADDPS, 1, 0, 0x58, 4, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VADDPD, 1, 1, 0x58, 8, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VADDSS, 1, 2, 0x58, 4, ASSEMBLY_VECTOR_FORM_SCALAR | ASSEMBLY_VECTOR_FORM_ROUNDING, false},
        {ASSEMBLY_OPCODE_X86_VADDSD, 1, 3, 0x58, 8, ASSEMBLY_VECTOR_FORM_SCALAR | ASSEMBLY_VECTOR_FORM_ROUNDING, false},
        {ASSEMBLY_OPCODE_X86_VSUBPS, 1, 0, 0x5c, 4, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VSUBPD, 1, 1, 0x5c, 8, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VMULPS, 1, 0, 0x59, 4, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VMULPD, 1, 1, 0x59, 8, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VDIVPS, 1, 0, 0x5e, 4, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VDIVPD, 1, 1, 0x5e, 8, ASSEMBLY_VECTOR_FORM_ROUNDING, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA, 1, 1, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU, 1, 2, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPADDB, 1, 1, 0xfc, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPADDW, 1, 1, 0xfd, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPADDD, 1, 1, 0xfe, 4, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPADDQ, 1, 1, 0xd4, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPSUBB, 1, 1, 0xf8, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPSUBW, 1, 1, 0xf9, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPSUBD, 1, 1, 0xfa, 4, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPSUBQ, 1, 1, 0xfb, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPAND, 1, 1, 0xdb, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPOR, 1, 1, 0xeb, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPXOR, 1, 1, 0xef, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQB, 1, 1, 0x74, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQW, 1, 1, 0x75, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQD, 1, 1, 0x76, 4, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQQ, 2, 1, 0x29, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTB, 1, 1, 0x64, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTW, 1, 1, 0x65, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTD, 1, 1, 0x66, 4, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTQ, 2, 1, 0x37, 8, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VPMULLW, 1, 1, 0xd5, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPMULLD, 2, 1, 0x40, 4, ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA32, 1, 1, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU32, 1, 2, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA64, 1, 1, 0x6f, 8, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU64, 1, 2, 0x6f, 8, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU8, 1, 3, 0x6f, 1, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU16, 1, 3, 0x6f, 2, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VCMPPS, 1, 0, 0xc2, 4, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VCMPPD, 1, 1, 0xc2, 8, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VPCMPD, 3, 1, 0x1f, 4, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VPCMPQ, 3, 1, 0x1f, 8, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VPCMPB, 3, 1, 0x3f, 1, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPW, 3, 1, 0x3f, 2, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPUD, 3, 1, 0x1e, 4, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VPCMPUQ, 3, 1, 0x1e, 8, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE, true},
        {ASSEMBLY_OPCODE_X86_VRNDSCALEPS, 3, 1, 0x08, 4,
         ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_SOURCE_RM, true},
        {ASSEMBLY_OPCODE_X86_VRNDSCALEPD, 3, 1, 0x09, 8,
         ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_SOURCE_RM, true},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(forms); index += 1)
    {
        if (forms[index].opcode == opcode)
        {
            return forms + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_vector_form_w(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_VXORPD || opcode == ASSEMBLY_OPCODE_X86_VADDPD ||
           opcode == ASSEMBLY_OPCODE_X86_VADDSD || opcode == ASSEMBLY_OPCODE_X86_VSUBPD ||
           opcode == ASSEMBLY_OPCODE_X86_VMULPD || opcode == ASSEMBLY_OPCODE_X86_VDIVPD ||
           opcode == ASSEMBLY_OPCODE_X86_VMOVAPD || opcode == ASSEMBLY_OPCODE_X86_VMOVUPD ||
           opcode == ASSEMBLY_OPCODE_X86_VMOVDQA64 || opcode == ASSEMBLY_OPCODE_X86_VMOVDQU64 ||
           opcode == ASSEMBLY_OPCODE_X86_VMOVDQU16 ||
           opcode == ASSEMBLY_OPCODE_X86_VPADDQ || opcode == ASSEMBLY_OPCODE_X86_VPSUBQ ||
           opcode == ASSEMBLY_OPCODE_X86_VPCMPEQQ || opcode == ASSEMBLY_OPCODE_X86_VPCMPGTQ ||
           opcode == ASSEMBLY_OPCODE_X86_VCMPPD || opcode == ASSEMBLY_OPCODE_X86_VPCMPQ ||
           opcode == ASSEMBLY_OPCODE_X86_VPCMPUQ || opcode == ASSEMBLY_OPCODE_X86_VRNDSCALEPD;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_vector_register(AssemblyRegister reg)
{
    return reg.class == ASSEMBLY_REGISTER_XMM || reg.class == ASSEMBLY_REGISTER_YMM || reg.class == ASSEMBLY_REGISTER_ZMM;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_target_has_evex(Target target, u16 width)
{
    u8 avx10 = target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) ||
               target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2);
    if (width == 512)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) ||
               target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_512);
    }
    return avx10 || (target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) &&
                     target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VL));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_uses_evex(AssemblyInstruction instruction, Target target)
{
    (void)target;
    if ((instruction.opcode >= ASSEMBLY_OPCODE_X86_VMOVDQA32 &&
         instruction.opcode <= ASSEMBLY_OPCODE_X86_VRNDSCALEPD) ||
        instruction.opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction.opcode == ASSEMBLY_OPCODE_X86_APX_POP2)
    {
        return true;
    }
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if ((operand.kind == ASSEMBLY_OPERAND_REGISTER &&
             (operand.reg.class == ASSEMBLY_REGISTER_ZMM || operand.reg.index >= 16)) || operand.has_mask || operand.zeroing ||
            operand.broadcast || operand.rounding || operand.sae ||
            (operand.kind == ASSEMBLY_OPERAND_MEMORY &&
             ((operand.memory.has_base && operand.memory.base.index >= 16) ||
              (operand.memory.has_index && operand.memory.index.index >= 16))))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u16 assembly_x86_instruction_vector_width(AssemblyInstruction instruction, AssemblyVectorForm const* form)
{
    if (!form)
    {
        return 0;
    }
    u32 first = (form->flags & ASSEMBLY_VECTOR_FORM_MASK_DESTINATION) ? 1 : 0;
    for (u32 index = first; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if (operand.kind == ASSEMBLY_OPERAND_REGISTER && assembly_x86_vector_register(operand.reg))
        {
            return operand.reg.width;
        }
    }
    return 512;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_operand_decorators_parse(String8* text, AssemblySyntax syntax, AssemblyOperand* operand,
                                                               bool* no_flags)
{
    u64 first_brace = string_first_code_unit(*text, '{');
    if (first_brace == BUSTER_STRING_NO_MATCH)
    {
        return true;
    }
    String8 core = assembly_trim(string_slice(*text, 0, first_brace));
    u64 cursor = first_brace;
    while (cursor < text->length)
    {
        while (cursor < text->length && assembly_space(text->pointer[cursor]))
        {
            cursor += 1;
        }
        if (cursor >= text->length || text->pointer[cursor] != '{')
        {
            return false;
        }
        u64 end = cursor + 1;
        while (end < text->length && text->pointer[end] != '}')
        {
            end += 1;
        }
        if (end >= text->length)
        {
            return false;
        }
        String8 decorator = assembly_trim(string_slice(*text, cursor + 1, end));
        if (assembly_word_equal(decorator, S8("z")))
        {
            if (operand->zeroing)
            {
                return false;
            }
            operand->zeroing = true;
        }
        else if (assembly_word_equal(decorator, S8("sae")))
        {
            if (operand->sae || operand->rounding)
            {
                return false;
            }
            operand->sae = true;
        }
        else if (assembly_word_equal(decorator, S8("rn-sae")) || assembly_word_equal(decorator, S8("rd-sae")) ||
                 assembly_word_equal(decorator, S8("ru-sae")) || assembly_word_equal(decorator, S8("rz-sae")))
        {
            if (operand->rounding || operand->sae)
            {
                return false;
            }
            operand->rounding = assembly_word_equal(decorator, S8("rn-sae")) ? 1
                               : assembly_word_equal(decorator, S8("rd-sae")) ? 2
                               : assembly_word_equal(decorator, S8("ru-sae")) ? 3
                                                                                : 4;
            operand->sae = true;
        }
        else if (decorator.length > 3 && assembly_word_equal(string_slice(decorator, 0, 3), S8("1to")))
        {
            s64 lanes = 0;
            if (!assembly_parse_s64(string_slice(decorator, 3, decorator.length), &lanes) || lanes < 2 || lanes > UINT8_MAX)
            {
                return false;
            }
            if (operand->broadcast)
            {
                return false;
            }
            operand->broadcast = (u8)lanes;
        }
        else if (assembly_word_equal(decorator, S8("nf")))
        {
            if (!no_flags || *no_flags)
            {
                return false;
            }
            *no_flags = true;
        }
        else
        {
            AssemblyRegister mask = {0};
            if (operand->has_mask || !assembly_register_parse(decorator, syntax, &mask) ||
                mask.class != ASSEMBLY_REGISTER_OPMASK)
            {
                return false;
            }
            operand->mask = mask.index;
            operand->has_mask = true;
        }
        cursor = end + 1;
    }
    *text = core;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_avx_move(AssemblyOpcode opcode)
{
    return (opcode >= ASSEMBLY_OPCODE_X86_VMOVAPS && opcode <= ASSEMBLY_OPCODE_X86_VMOVUPD) ||
           opcode == ASSEMBLY_OPCODE_X86_VMOVDQA || opcode == ASSEMBLY_OPCODE_X86_VMOVDQU;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_avx_integer(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_VPADDB && opcode <= ASSEMBLY_OPCODE_X86_VPMULLD;
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_avx_map(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_VPCMPEQQ || opcode == ASSEMBLY_OPCODE_X86_VPCMPGTQ ||
                   opcode == ASSEMBLY_OPCODE_X86_VPMULLD
               ? 2
               : 1;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_legacy_packed(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_MOVQ_MMX && opcode <= ASSEMBLY_OPCODE_X86_PMULLW_MMX;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_data(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FLD && opcode <= ASSEMBLY_OPCODE_X86_FISTTP;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_arithmetic(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FADD && opcode <= ASSEMBLY_OPCODE_X86_FDIVR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_pop_arithmetic(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FADDP && opcode <= ASSEMBLY_OPCODE_X86_FDIVRP;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_compare(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FCOM && opcode <= ASSEMBLY_OPCODE_X86_FCMOVCC;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_integer_arithmetic(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FIADD && opcode <= ASSEMBLY_OPCODE_X86_FIDIVR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_state_memory(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FBLD && opcode <= ASSEMBLY_OPCODE_X86_FSTSW;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_stack_control(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_FFREE && opcode <= ASSEMBLY_OPCODE_X86_FDECSTP;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_x87_zero_operand(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_F2XM1 && opcode <= ASSEMBLY_OPCODE_X86_FWAIT;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_operand_count_valid(AssemblyOpcode opcode, u8 count, u8 canonical_count)
{
    if (opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return count >= 1 && count <= 3;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_SUB || opcode == ASSEMBLY_OPCODE_X86_AND ||
        opcode == ASSEMBLY_OPCODE_X86_OR || opcode == ASSEMBLY_OPCODE_X86_XOR)
    {
        return count == 2 || count == 3;
    }
    if (assembly_x86_opcode_is_x87_arithmetic(opcode) || assembly_x86_opcode_is_x87_pop_arithmetic(opcode))
    {
        return count <= 2;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_FXCH || opcode == ASSEMBLY_OPCODE_X86_FCOM || opcode == ASSEMBLY_OPCODE_X86_FCOMP ||
        opcode == ASSEMBLY_OPCODE_X86_FUCOM || opcode == ASSEMBLY_OPCODE_X86_FUCOMP)
    {
        return count <= 1;
    }
    return count == canonical_count;
}

BUSTER_GLOBAL_LOCAL AssemblyOperand assembly_x86_x87_register_operand(u8 index)
{
    return (AssemblyOperand){
        .reg = {.index = index, .width = 80, .class = ASSEMBLY_REGISTER_X87},
        .kind = ASSEMBLY_OPERAND_REGISTER,
    };
}

BUSTER_GLOBAL_LOCAL void assembly_x86_x87_normalize_omitted_operands(AssemblyInstruction* instruction, AssemblySyntax syntax)
{
    if (instruction->operand_count == 0)
    {
        if (assembly_x86_opcode_is_x87_arithmetic(instruction->opcode))
        {
            instruction->opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_FADD ? ASSEMBLY_OPCODE_X86_FADDP
                                  : instruction->opcode == ASSEMBLY_OPCODE_X86_FMUL ? ASSEMBLY_OPCODE_X86_FMULP
                                  : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUB
                                      ? (syntax == ASSEMBLY_SYNTAX_ATT ? ASSEMBLY_OPCODE_X86_FSUBRP : ASSEMBLY_OPCODE_X86_FSUBP)
                                  : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBR
                                      ? (syntax == ASSEMBLY_SYNTAX_ATT ? ASSEMBLY_OPCODE_X86_FSUBP : ASSEMBLY_OPCODE_X86_FSUBRP)
                                  : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIV
                                      ? (syntax == ASSEMBLY_SYNTAX_ATT ? ASSEMBLY_OPCODE_X86_FDIVRP : ASSEMBLY_OPCODE_X86_FDIVP)
                                      : (syntax == ASSEMBLY_SYNTAX_ATT ? ASSEMBLY_OPCODE_X86_FDIVP : ASSEMBLY_OPCODE_X86_FDIVRP);
            instruction->operands[0] = assembly_x86_x87_register_operand(1);
            instruction->operands[1] = assembly_x86_x87_register_operand(0);
            instruction->operand_count = 2;
        }
        else if (assembly_x86_opcode_is_x87_pop_arithmetic(instruction->opcode))
        {
            if (syntax == ASSEMBLY_SYNTAX_ATT)
            {
                instruction->opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBP ? ASSEMBLY_OPCODE_X86_FSUBRP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBRP ? ASSEMBLY_OPCODE_X86_FSUBP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIVP ? ASSEMBLY_OPCODE_X86_FDIVRP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIVRP ? ASSEMBLY_OPCODE_X86_FDIVP
                                                                                          : instruction->opcode;
            }
            instruction->operands[0] = assembly_x86_x87_register_operand(1);
            instruction->operands[1] = assembly_x86_x87_register_operand(0);
            instruction->operand_count = 2;
        }
        else if (instruction->opcode == ASSEMBLY_OPCODE_X86_FXCH || instruction->opcode == ASSEMBLY_OPCODE_X86_FCOM ||
                 instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMP || instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOM ||
                 instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMP)
        {
            instruction->operands[0] = assembly_x86_x87_register_operand(1);
            instruction->operand_count = 1;
        }
    }
    else if (instruction->operand_count == 1 && instruction->operands[0].kind == ASSEMBLY_OPERAND_REGISTER)
    {
        if (assembly_x86_opcode_is_x87_arithmetic(instruction->opcode))
        {
            instruction->operands[1] = instruction->operands[0];
            instruction->operands[0] = assembly_x86_x87_register_operand(0);
            instruction->operand_count = 2;
        }
        else if (assembly_x86_opcode_is_x87_pop_arithmetic(instruction->opcode))
        {
            if (syntax == ASSEMBLY_SYNTAX_ATT)
            {
                instruction->opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBP ? ASSEMBLY_OPCODE_X86_FSUBRP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBRP ? ASSEMBLY_OPCODE_X86_FSUBP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIVP ? ASSEMBLY_OPCODE_X86_FDIVRP
                                      : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIVRP ? ASSEMBLY_OPCODE_X86_FDIVP
                                                                                          : instruction->opcode;
            }
            instruction->operands[1] = assembly_x86_x87_register_operand(0);
            instruction->operand_count = 2;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_rex_needed(u16 width, AssemblyRegister first, AssemblyRegister second)
{
    return width == 64 || first.index >= 8 || second.index >= 8 ||
           (width == 8 && ((first.index >= 4 && !first.high_byte) || (second.index >= 4 && !second.high_byte)));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_extension_rex_needed(u16 width, AssemblyRegister destination, AssemblyRegister source,
                                                            u16 source_width)
{
    return assembly_x86_rex_needed(width, destination, source) ||
           (source_width == 8 && source.index >= 4 && !source.high_byte);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_displacement_size(AssemblyMemory memory, u32* result)
{
    if (memory.rip_relative || !memory.has_base)
    {
        if (!memory.displacement.has_symbol && (memory.displacement.addend < INT32_MIN || memory.displacement.addend > INT32_MAX))
        {
            return false;
        }
        *result = 4;
        return true;
    }
    if (memory.displacement.has_symbol)
    {
        *result = 4;
    }
    else if (!memory.displacement.addend && (memory.base.index & 7) != 5)
    {
        *result = 0;
    }
    else if (memory.displacement.addend >= INT8_MIN && memory.displacement.addend <= INT8_MAX)
    {
        *result = 1;
    }
    else if (memory.displacement.addend >= INT32_MIN && memory.displacement.addend <= INT32_MAX)
    {
        *result = 4;
    }
    else
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_encoding_size(AssemblyMemory memory, u32* result)
{
    u32 displacement_size = 0;
    if (!assembly_x86_memory_displacement_size(memory, &displacement_size))
    {
        return false;
    }
    u8 sib = !memory.rip_relative && (memory.has_index || !memory.has_base || (memory.base.index & 7) == 4);
    *result = 1u + (u32)sib + displacement_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_rex_needed(u16 width, AssemblyRegister reg, AssemblyMemory memory)
{
    return width == 64 || reg.index >= 8 || (memory.has_base && memory.base.index >= 8) ||
           (memory.has_index && memory.index.index >= 8) || (width == 8 && reg.index >= 4 && !reg.high_byte);
}

BUSTER_GLOBAL_LOCAL u16 assembly_operand_width(AssemblyOperand operand)
{
    return operand.kind == ASSEMBLY_OPERAND_REGISTER ? operand.reg.width
           : operand.kind == ASSEMBLY_OPERAND_MEMORY ? operand.memory.width
                                                      : 0;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_rotate(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ROL || opcode == ASSEMBLY_OPCODE_X86_ROR || opcode == ASSEMBLY_OPCODE_X86_RCL ||
           opcode == ASSEMBLY_OPCODE_X86_RCR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_count_is_cl(AssemblyOperand operand)
{
    return operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.class == ASSEMBLY_REGISTER_GPR && operand.reg.width == 8 &&
           operand.reg.index == 1;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_vex_three_byte_needed(AssemblyOperand rm)
{
    return (rm.kind == ASSEMBLY_OPERAND_REGISTER && rm.reg.index >= 8) ||
           (rm.kind == ASSEMBLY_OPERAND_MEMORY && ((rm.memory.has_base && rm.memory.base.index >= 8) ||
                                                   (rm.memory.has_index && rm.memory.index.index >= 8)));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_immediate_fits(s64 value, u16 width, bool signed_only)
{
    if (width == 64)
    {
        return true;
    }
    s64 signed_minimum = -(INT64_C(1) << (width - 1));
    u64 unsigned_maximum = (UINT64_C(1) << width) - 1;
    return value >= signed_minimum && (signed_only ? value <= (s64)(unsigned_maximum >> 1) : value < 0 || (u64)value <= unsigned_maximum);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_count_immediate_valid(AssemblyOperand operand)
{
    return operand.kind == ASSEMBLY_OPERAND_EXPRESSION && !operand.expression.has_symbol &&
           assembly_x86_immediate_fits(operand.expression.addend, 8, false);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_bit_atomic(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_BSF && opcode <= ASSEMBLY_OPCODE_X86_TZCNT;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_target_has_bit_atomic_feature(Target target, TargetCpuFeature feature)
{
    return target_cpu_feature_has(target, feature);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_operand_is_gpr(AssemblyOperand operand)
{
    return operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.class == ASSEMBLY_REGISTER_GPR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_rex_conflicts_high_byte(u16 width, AssemblyRegister first, AssemblyRegister second)
{
    return assembly_x86_rex_needed(width, first, second) && (first.high_byte || second.high_byte);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_rex_conflicts_high_byte(u16 width, AssemblyRegister reg, AssemblyMemory memory)
{
    return assembly_x86_memory_rex_needed(width, reg, memory) && reg.high_byte;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_set_width(AssemblyOperand* operand, u16 width)
{
    if (operand->memory.width && operand->memory.width != width)
    {
        return false;
    }
    operand->memory.width = width;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_lock_prefix_legal(AssemblyInstruction* instruction)
{
    if (!instruction->lock_prefix)
    {
        return true;
    }
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyOperand first = instruction->operands[0];
    if (opcode == ASSEMBLY_OPCODE_X86_XCHG)
    {
        return first.kind == ASSEMBLY_OPERAND_MEMORY || instruction->operands[1].kind == ASSEMBLY_OPERAND_MEMORY;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BTC || opcode == ASSEMBLY_OPCODE_X86_BTR || opcode == ASSEMBLY_OPCODE_X86_BTS ||
        opcode == ASSEMBLY_OPCODE_X86_XADD || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG ||
        opcode == ASSEMBLY_OPCODE_X86_CMPXCHG8B || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B)
    {
        return first.kind == ASSEMBLY_OPERAND_MEMORY;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_ADC || opcode == ASSEMBLY_OPCODE_X86_SUB ||
        opcode == ASSEMBLY_OPCODE_X86_SBB || opcode == ASSEMBLY_OPCODE_X86_AND || opcode == ASSEMBLY_OPCODE_X86_OR ||
        opcode == ASSEMBLY_OPCODE_X86_XOR || opcode == ASSEMBLY_OPCODE_X86_INC || opcode == ASSEMBLY_OPCODE_X86_DEC ||
        opcode == ASSEMBLY_OPCODE_X86_NEG || opcode == ASSEMBLY_OPCODE_X86_NOT)
    {
        return first.kind == ASSEMBLY_OPERAND_MEMORY;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_bit_atomic_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    u32 lock_size = instruction->lock_prefix ? 1 : 0;
    if (!assembly_x86_lock_prefix_legal(instruction))
    {
        return false;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BSF || opcode == ASSEMBLY_OPCODE_X86_BSR || opcode == ASSEMBLY_OPCODE_X86_POPCNT ||
        opcode == ASSEMBLY_OPCODE_X86_LZCNT || opcode == ASSEMBLY_OPCODE_X86_TZCNT)
    {
        if (!assembly_x86_operand_is_gpr(*first) || first->reg.width == 8 ||
            (first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        instruction->width = first->reg.width;
        if (second->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (!assembly_x86_operand_is_gpr(*second) || second->reg.width != instruction->width ||
                assembly_x86_rex_conflicts_high_byte(instruction->width, first->reg, second->reg))
            {
                return false;
            }
        }
        else
        {
            if (!assembly_x86_memory_set_width(second, instruction->width) ||
                assembly_x86_memory_rex_conflicts_high_byte(instruction->width, first->reg, second->memory))
            {
                return false;
            }
        }
        u32 address_size = 1;
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        u8 rex = second->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, second->reg)
                       : assembly_x86_memory_rex_needed(instruction->width, first->reg, second->memory);
        u32 mandatory_prefix = opcode == ASSEMBLY_OPCODE_X86_BSF || opcode == ASSEMBLY_OPCODE_X86_BSR ? 0 : 1;
        instruction->size = lock_size + (instruction->width == 16 ? 1 : 0) + mandatory_prefix + (rex ? 1 : 0) + 2 + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BSWAP)
    {
        if (!assembly_x86_operand_is_gpr(*first) || (first->reg.width != 32 && first->reg.width != 64) ||
            instruction->operand_count != 1)
        {
            return false;
        }
        instruction->width = first->reg.width;
        u8 rex = assembly_x86_rex_needed(instruction->width, (AssemblyRegister){0}, first->reg);
        instruction->size = lock_size + (rex ? 1 : 0) + 2;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BT || opcode == ASSEMBLY_OPCODE_X86_BTC || opcode == ASSEMBLY_OPCODE_X86_BTR ||
        opcode == ASSEMBLY_OPCODE_X86_BTS)
    {
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_EXPRESSION))
        {
            return false;
        }
        instruction->width = assembly_operand_width(*first);
        if (second->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (!assembly_x86_operand_is_gpr(*second))
            {
                return false;
            }
            if (!instruction->width)
            {
                instruction->width = second->reg.width;
            }
            if (second->reg.width != instruction->width)
            {
                return false;
            }
        }
        else if (!instruction->width)
        {
            return false;
        }
        if (instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER && !assembly_x86_operand_is_gpr(*first))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, instruction->width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_EXPRESSION &&
            (second->expression.has_symbol || second->expression.addend < 0 || second->expression.addend > UINT8_MAX))
        {
            return false;
        }
        u8 reg = second->kind == ASSEMBLY_OPERAND_EXPRESSION
                     ? opcode == ASSEMBLY_OPCODE_X86_BT   ? 4
                       : opcode == ASSEMBLY_OPCODE_X86_BTS ? 5
                       : opcode == ASSEMBLY_OPCODE_X86_BTR ? 6
                                                       : 7
                     : second->reg.index;
        u8 rex = first->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, (AssemblyRegister){.index = reg}, first->reg)
                       : assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){.index = reg}, first->memory);
        if (first->kind == ASSEMBLY_OPERAND_REGISTER &&
            assembly_x86_rex_conflicts_high_byte(instruction->width, (AssemblyRegister){.index = reg}, first->reg))
        {
            return false;
        }
        u32 address_size = 1;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->size = lock_size + (instruction->width == 16 ? 1 : 0) + (rex ? 1 : 0) +
                            (second->kind == ASSEMBLY_OPERAND_EXPRESSION ? 3 : 2) + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_XCHG)
    {
        AssemblyOperand* memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        if (!assembly_x86_operand_is_gpr(*first) && first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        if (!assembly_x86_operand_is_gpr(*second) && second->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        instruction->width = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg.width : second->reg.width;
        if (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER && second->kind == ASSEMBLY_OPERAND_REGISTER &&
            (first->reg.width != instruction->width || second->reg.width != instruction->width))
        {
            return false;
        }
        if (memory && !assembly_x86_memory_set_width(memory, instruction->width))
        {
            return false;
        }
        if (!memory && instruction->width != 8 && (first->reg.index == 0 || second->reg.index == 0))
        {
            AssemblyRegister other = first->reg.index == 0 ? second->reg : first->reg;
            u8 rex = assembly_x86_rex_needed(instruction->width, (AssemblyRegister){0}, other);
            instruction->size = lock_size + (instruction->width == 16 ? 1 : 0) + (rex ? 1 : 0) + 1;
            return true;
        }
        AssemblyRegister reg = first->kind == ASSEMBLY_OPERAND_MEMORY ? second->reg : first->reg;
        AssemblyOperand* rm = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second;
        if (rm->kind == ASSEMBLY_OPERAND_REGISTER && assembly_x86_rex_conflicts_high_byte(instruction->width, reg, rm->reg))
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = rm->kind == ASSEMBLY_OPERAND_MEMORY
                       ? assembly_x86_memory_rex_needed(instruction->width, reg, rm->memory)
                       : assembly_x86_rex_needed(instruction->width, reg, rm->reg);
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_memory_rex_conflicts_high_byte(instruction->width, reg, rm->memory))
        {
            return false;
        }
        instruction->size = lock_size + (instruction->width == 16 ? 1 : 0) + (rex ? 1 : 0) + 1 + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_XADD || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG)
    {
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            !assembly_x86_operand_is_gpr(*second))
        {
            return false;
        }
        instruction->width = assembly_operand_width(*first);
        if (!instruction->width)
        {
            instruction->width = second->reg.width;
        }
        if ((instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64) ||
            second->reg.width != instruction->width || (first->kind == ASSEMBLY_OPERAND_REGISTER && !assembly_x86_operand_is_gpr(*first)))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, instruction->width))
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = first->kind == ASSEMBLY_OPERAND_MEMORY
                       ? assembly_x86_memory_rex_needed(instruction->width, second->reg, first->memory)
                       : assembly_x86_rex_needed(instruction->width, second->reg, first->reg);
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        if ((first->kind == ASSEMBLY_OPERAND_REGISTER && assembly_x86_rex_conflicts_high_byte(instruction->width, second->reg, first->reg)) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_memory_rex_conflicts_high_byte(instruction->width, second->reg, first->memory)))
        {
            return false;
        }
        instruction->size = lock_size + (instruction->width == 16 ? 1 : 0) + (rex ? 1 : 0) + 2 + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CMPXCHG8B || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B)
    {
        u8 width = opcode == ASSEMBLY_OPCODE_X86_CMPXCHG8B ? 64 : 128;
        if (first->kind != ASSEMBLY_OPERAND_MEMORY || !assembly_x86_memory_set_width(first, width))
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        u8 rex = assembly_x86_memory_rex_needed(opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B ? 64 : 0,
                                                  (AssemblyRegister){.index = 1}, first->memory);
        instruction->width = width;
        instruction->size = lock_size + (rex ? 1 : 0) + 2 + address_size;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_mask(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_KMOVW && opcode <= ASSEMBLY_OPCODE_X86_KORTESTW;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_amx(AssemblyOpcode opcode)
{
    return opcode >= ASSEMBLY_OPCODE_X86_LDTILECFG && opcode <= ASSEMBLY_OPCODE_X86_TDPBUUD;
}

BUSTER_GLOBAL_LOCAL TargetCpuFeature assembly_x86_amx_feature(AssemblyOpcode opcode)
{
    if (opcode == ASSEMBLY_OPCODE_X86_TDPBF16PS)
    {
        return TARGET_CPU_FEATURE_X86_AMX_BF16;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_TDPBSSD || opcode == ASSEMBLY_OPCODE_X86_TDPBSUD ||
        opcode == ASSEMBLY_OPCODE_X86_TDPBUSD || opcode == ASSEMBLY_OPCODE_X86_TDPBUUD)
    {
        return TARGET_CPU_FEATURE_X86_AMX_INT8;
    }
    return TARGET_CPU_FEATURE_X86_AMX_TILE;
}

BUSTER_GLOBAL_LOCAL String8 assembly_x86_amx_feature_name(AssemblyOpcode opcode)
{
    if (opcode == ASSEMBLY_OPCODE_X86_TDPBF16PS)
    {
        return S8("amx-bf16");
    }
    if (opcode == ASSEMBLY_OPCODE_X86_TDPBSSD || opcode == ASSEMBLY_OPCODE_X86_TDPBSUD ||
        opcode == ASSEMBLY_OPCODE_X86_TDPBUSD || opcode == ASSEMBLY_OPCODE_X86_TDPBUUD)
    {
        return S8("amx-int8");
    }
    return S8("amx-tile");
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_target_has_amx_feature(Target target, AssemblyOpcode opcode)
{
    return (target_cpu_features_effective(target) & assembly_x86_amx_feature(opcode)) != 0;
}

BUSTER_GLOBAL_LOCAL TargetCpuFeature assembly_x86_mask_feature(AssemblyOpcode opcode)
{
    if (opcode == ASSEMBLY_OPCODE_X86_KMOVD || opcode == ASSEMBLY_OPCODE_X86_KMOVQ)
    {
        return TARGET_CPU_FEATURE_X86_AVX512BW;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_KADDW)
    {
        return TARGET_CPU_FEATURE_X86_AVX512DQ;
    }
    return TARGET_CPU_FEATURE_X86_AVX512F;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_target_has_mask_feature(Target target, AssemblyOpcode opcode)
{
    return target_cpu_feature_has(target, assembly_x86_mask_feature(opcode)) ||
           target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) ||
           target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2);
}

BUSTER_GLOBAL_LOCAL String8 assembly_x86_mask_feature_name(AssemblyOpcode opcode)
{
    if (opcode == ASSEMBLY_OPCODE_X86_KMOVD || opcode == ASSEMBLY_OPCODE_X86_KMOVQ)
    {
        return S8("opmask instruction requires avx512bw or avx10");
    }
    if (opcode == ASSEMBLY_OPCODE_X86_KADDW)
    {
        return S8("kaddw requires avx512dq or avx10");
    }
    return S8("opmask instruction requires avx512f or avx10");
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_apx_binary(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_SUB || opcode == ASSEMBLY_OPCODE_X86_AND ||
           opcode == ASSEMBLY_OPCODE_X86_OR || opcode == ASSEMBLY_OPCODE_X86_XOR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_has_extended_gpr(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if ((operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.class == ASSEMBLY_REGISTER_GPR && operand.reg.index >= 16) ||
            (operand.kind == ASSEMBLY_OPERAND_MEMORY &&
             ((operand.memory.has_base && operand.memory.base.index >= 16) ||
              (operand.memory.has_index && operand.memory.index.index >= 16))))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_operand_has_high_byte(AssemblyOperand operand)
{
    if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        return operand.reg.high_byte;
    }
    if (operand.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        return (operand.memory.has_base && operand.memory.base.high_byte) ||
               (operand.memory.has_index && operand.memory.index.high_byte);
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_has_high_byte(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        if (assembly_x86_operand_has_high_byte(instruction.operands[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_operand_has_any_decorator(AssemblyOperand operand)
{
    return operand.has_mask || operand.broadcast || operand.rounding || operand.zeroing || operand.sae;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_has_any_decorator(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        if (assembly_x86_operand_has_any_decorator(instruction.operands[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_operands_need_rex(AssemblyInstruction instruction)
{
    if (instruction.width == 64)
    {
        return true;
    }
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if (operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.class == ASSEMBLY_REGISTER_GPR &&
            (operand.reg.index >= 8 || operand.reg.width == 64 ||
             (operand.reg.width == 8 && operand.reg.index >= 4 && !operand.reg.high_byte)))
        {
            return true;
        }
        if (operand.kind == ASSEMBLY_OPERAND_MEMORY &&
            ((operand.memory.width == 64) || (operand.memory.has_base && operand.memory.base.index >= 8) ||
             (operand.memory.has_index && operand.memory.index.index >= 8)))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_evex_memory_displacement_size(AssemblyMemory memory, u32 tuple_scale, u32* result)
{
    if (memory.rip_relative || !memory.has_base)
    {
        return assembly_x86_memory_displacement_size(memory, result);
    }
    if (memory.displacement.has_symbol)
    {
        *result = 4;
        return true;
    }
    if (!memory.displacement.addend && (memory.base.index & 7) != 5)
    {
        *result = 0;
        return true;
    }
    if (tuple_scale && memory.displacement.addend % (s64)tuple_scale == 0)
    {
        s64 scaled = memory.displacement.addend / (s64)tuple_scale;
        if (scaled >= INT8_MIN && scaled <= INT8_MAX)
        {
            *result = 1;
            return true;
        }
    }
    if (memory.displacement.addend >= INT32_MIN && memory.displacement.addend <= INT32_MAX)
    {
        *result = 4;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_evex_memory_encoding_size(AssemblyMemory memory, u32 tuple_scale, u32* result)
{
    u32 displacement_size = 0;
    if (!assembly_x86_evex_memory_displacement_size(memory, tuple_scale, &displacement_size))
    {
        return false;
    }
    u8 sib = !memory.rip_relative && (memory.has_index || !memory.has_base || (memory.base.index & 7) == 4);
    *result = 1u + (u32)sib + displacement_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_evex_instruction_size(AssemblyInstruction* instruction, AssemblyVectorForm const* form)
{
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    AssemblyOperand* third = instruction->operands + 2;
    u8 move = (form->flags & ASSEMBLY_VECTOR_FORM_MOVE) != 0;
    u8 mask_destination = (form->flags & ASSEMBLY_VECTOR_FORM_MASK_DESTINATION) != 0;
    u8 source_rm = (form->flags & ASSEMBLY_VECTOR_FORM_SOURCE_RM) != 0;
    u8 immediate = (form->flags & ASSEMBLY_VECTOR_FORM_IMMEDIATE) != 0;
    AssemblyRegister vector = {0};
    AssemblyOperand* memory = 0;
    if (mask_destination)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_OPMASK ||
            first->has_mask || first->zeroing || second->kind != ASSEMBLY_OPERAND_REGISTER ||
            !assembly_x86_vector_register(second->reg) ||
            (third->kind != ASSEMBLY_OPERAND_REGISTER && third->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        vector = second->reg;
        memory = third->kind == ASSEMBLY_OPERAND_MEMORY ? third : 0;
        if (third->kind == ASSEMBLY_OPERAND_REGISTER && third->reg.class != vector.class)
        {
            return false;
        }
        if (instruction->operands[3].kind != ASSEMBLY_OPERAND_EXPRESSION || instruction->operands[3].expression.has_symbol ||
            instruction->operands[3].expression.addend < 0 || instruction->operands[3].expression.addend > UINT8_MAX)
        {
            return false;
        }
        u64 immediate_limit = instruction->opcode == ASSEMBLY_OPCODE_X86_VCMPPS || instruction->opcode == ASSEMBLY_OPCODE_X86_VCMPPD ? 31 : 7;
        if ((u64)instruction->operands[3].expression.addend > immediate_limit)
        {
            return false;
        }
    }
    else if (source_rm)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_vector_register(first->reg) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            instruction->operands[2].kind != ASSEMBLY_OPERAND_EXPRESSION || instruction->operands[2].expression.has_symbol ||
            instruction->operands[2].expression.addend < 0 || instruction->operands[2].expression.addend > UINT8_MAX)
        {
            return false;
        }
        vector = first->reg;
        memory = second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
        if (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != vector.class)
        {
            return false;
        }
    }
    else if (move)
    {
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        vector = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg : second->reg;
        memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
        if (!assembly_x86_vector_register(vector) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != vector.class) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != vector.class))
        {
            return false;
        }
    }
    else
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || second->kind != ASSEMBLY_OPERAND_REGISTER ||
            (third->kind != ASSEMBLY_OPERAND_REGISTER && third->kind != ASSEMBLY_OPERAND_MEMORY) ||
            !assembly_x86_vector_register(first->reg) || second->reg.class != first->reg.class)
        {
            return false;
        }
        vector = first->reg;
        memory = third->kind == ASSEMBLY_OPERAND_MEMORY ? third : 0;
        if (third->kind == ASSEMBLY_OPERAND_REGISTER && third->reg.class != vector.class)
        {
            return false;
        }
    }
    if ((form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) && vector.class != ASSEMBLY_REGISTER_XMM)
    {
        return false;
    }
    if (vector.width != 128 && vector.width != 256 && vector.width != 512)
    {
        return false;
    }
    if (first->broadcast)
    {
        return false;
    }
    if (!mask_destination && (first->has_mask || first->zeroing))
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_vector_register(first->reg) ||
            (first->has_mask && first->mask == 0) || (first->zeroing && !first->has_mask))
        {
            return false;
        }
    }
    for (u32 operand_index = mask_destination ? 0 : 1; operand_index < instruction->operand_count; operand_index += 1)
    {
        AssemblyOperand operand = instruction->operands[operand_index];
        if (operand.has_mask || operand.zeroing || operand.rounding || operand.sae ||
            (operand.broadcast && (!memory || instruction->operands + operand_index != memory)))
        {
            return false;
        }
    }
    if (first->rounding || first->sae)
    {
        if (!(form->flags & ASSEMBLY_VECTOR_FORM_ROUNDING) || (first->sae && !first->rounding) || move || mask_destination || memory)
        {
            return false;
        }
    }
    if (memory)
    {
        u16 element_width = (u16)form->element_width * 8u;
        u16 expected_width = (form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) ? element_width : vector.width;
        if (memory->broadcast)
        {
            if (move || (form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) ||
                memory->broadcast != vector.width / element_width)
            {
                return false;
            }
            expected_width = element_width;
        }
        if (memory->memory.width && memory->memory.width != expected_width)
        {
            return false;
        }
        memory->memory.width = expected_width;
        u32 tuple_scale = memory->broadcast ? form->element_width :
                            (form->flags & ASSEMBLY_VECTOR_FORM_SCALAR ? form->element_width : vector.width / 8u);
        u32 address_size = 0;
        if (!assembly_x86_evex_memory_encoding_size(memory->memory, tuple_scale, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = (u8)immediate;
        instruction->size = 4u + 1u + address_size + immediate;
    }
    else
    {
        if (first->broadcast || second->broadcast || third->broadcast)
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->size = 4u + 1u + 1u + immediate;
    }
    if (memory && (memory->rounding || memory->sae))
    {
        return false;
    }
    instruction->width = vector.width;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amx_memory_size(AssemblyMemory memory, bool forced_sib, u32* result)
{
    u32 address_size = 0;
    if (!assembly_x86_memory_encoding_size(memory, &address_size))
    {
        return false;
    }
    u8 needs_forced_sib = forced_sib && !memory.has_index && memory.has_base && (memory.base.index & 7) != 4;
    *result = address_size + (u32)needs_forced_sib;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amx_memory_uses_apx_evex(AssemblyMemory memory)
{
    return (memory.has_base && memory.base.index >= 16) || (memory.has_index && memory.index.index >= 16);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amx_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    if (opcode == ASSEMBLY_OPCODE_X86_LDTILECFG || opcode == ASSEMBLY_OPCODE_X86_STTILECFG)
    {
        if (instruction->operand_count != 1 || first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_amx_memory_size(first->memory, false, &address_size))
        {
            return false;
        }
        instruction->size = (assembly_x86_amx_memory_uses_apx_evex(first->memory) ? 4u : 3u) + 1u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_TILELOADD || opcode == ASSEMBLY_OPCODE_X86_TILELOADDT1)
    {
        if (instruction->operand_count != 2 || first->kind != ASSEMBLY_OPERAND_REGISTER ||
            first->reg.class != ASSEMBLY_REGISTER_TILE || second->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_amx_memory_size(second->memory, true, &address_size))
        {
            return false;
        }
        instruction->size = (assembly_x86_amx_memory_uses_apx_evex(second->memory) ? 4u : 3u) + 1u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_TILESTORED)
    {
        if (instruction->operand_count != 2 || first->kind != ASSEMBLY_OPERAND_MEMORY ||
            second->kind != ASSEMBLY_OPERAND_REGISTER || second->reg.class != ASSEMBLY_REGISTER_TILE)
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_amx_memory_size(first->memory, true, &address_size))
        {
            return false;
        }
        instruction->size = (assembly_x86_amx_memory_uses_apx_evex(first->memory) ? 4u : 3u) + 1u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_TILEZERO)
    {
        if (instruction->operand_count != 1 || first->kind != ASSEMBLY_OPERAND_REGISTER ||
            first->reg.class != ASSEMBLY_REGISTER_TILE)
        {
            return false;
        }
        instruction->size = 3u + 1u + 1u;
        return true;
    }
    if (instruction->operand_count != 3 || first->kind != ASSEMBLY_OPERAND_REGISTER ||
        second->kind != ASSEMBLY_OPERAND_REGISTER || instruction->operands[2].kind != ASSEMBLY_OPERAND_REGISTER ||
        first->reg.class != ASSEMBLY_REGISTER_TILE || second->reg.class != ASSEMBLY_REGISTER_TILE ||
        instruction->operands[2].reg.class != ASSEMBLY_REGISTER_TILE)
    {
        return false;
    }
    instruction->size = 3u + 1u + 1u;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_mask_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    AssemblyOpcode opcode = instruction->opcode;
    if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_OPMASK)
    {
        return false;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_KMOVW || opcode == ASSEMBLY_OPCODE_X86_KMOVD || opcode == ASSEMBLY_OPCODE_X86_KMOVQ ||
        opcode == ASSEMBLY_OPCODE_X86_KNOTW || opcode == ASSEMBLY_OPCODE_X86_KORTESTW)
    {
        if (instruction->operand_count != 2 || second->kind != ASSEMBLY_OPERAND_REGISTER ||
            second->reg.class != ASSEMBLY_REGISTER_OPMASK)
        {
            return false;
        }
        instruction->size = opcode == ASSEMBLY_OPCODE_X86_KMOVW ? 4 : 5;
        return true;
    }
    if (instruction->operand_count != 3 || instruction->operands[2].kind != ASSEMBLY_OPERAND_REGISTER ||
        instruction->operands[2].reg.class != ASSEMBLY_REGISTER_OPMASK)
    {
        return false;
    }
    instruction->size = 4;
    return opcode == ASSEMBLY_OPCODE_X86_KADDW || opcode == ASSEMBLY_OPCODE_X86_KANDW || opcode == ASSEMBLY_OPCODE_X86_KORW ||
           opcode == ASSEMBLY_OPCODE_X86_KXORW;
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_rex2_byte(u16 width, AssemblyRegister reg, AssemblyRegister rm)
{
    return (u8)((width == 64 ? 0x08 : 0) | (reg.index & 8 ? 0x04 : 0) | (rm.index & 8 ? 0x01 : 0) |
                 (reg.index & 16 ? 0x40 : 0) | (rm.index & 16 ? 0x10 : 0));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_legacy_instruction_size(AssemblyInstruction* instruction)
{
    if (!assembly_x86_instruction_has_extended_gpr(*instruction))
    {
        return false;
    }
    AssemblyOperand* first = instruction->operands;
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_PUSH || instruction->opcode == ASSEMBLY_OPCODE_X86_POP)
    {
        if (instruction->operand_count != 1 || first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR ||
            first->reg.width != 64)
        {
            return false;
        }
        instruction->size = 3;
        return true;
    }
    if (instruction->opcode != ASSEMBLY_OPCODE_X86_MOV && instruction->opcode != ASSEMBLY_OPCODE_X86_ADD &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_SUB && instruction->opcode != ASSEMBLY_OPCODE_X86_AND &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_OR && instruction->opcode != ASSEMBLY_OPCODE_X86_XOR &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_CMP && instruction->opcode != ASSEMBLY_OPCODE_X86_TEST)
    {
        return false;
    }
    AssemblyOperand* second = instruction->operands + 1;
    if (instruction->operand_count != 2 ||
        (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY) ||
        (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != ASSEMBLY_REGISTER_GPR) ||
        (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != ASSEMBLY_REGISTER_GPR))
    {
        return false;
    }
    instruction->width = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg.width : second->reg.width;
    if (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
    {
        return false;
    }
    if ((first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.width != instruction->width) ||
        (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.width != instruction->width))
    {
        return false;
    }
    if (first->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (first->memory.width && first->memory.width != instruction->width)
        {
            return false;
        }
        first->memory.width = instruction->width;
    }
    if (second->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (second->memory.width && second->memory.width != instruction->width)
        {
            return false;
        }
        second->memory.width = instruction->width;
    }
    u32 address_size = 1;
    AssemblyOperand* memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
    if (memory && !assembly_x86_memory_encoding_size(memory->memory, &address_size))
    {
        return false;
    }
    instruction->size = 2u + (instruction->width == 16) + 1u + address_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_ndd_instruction_size(AssemblyInstruction* instruction)
{
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction->opcode == ASSEMBLY_OPCODE_X86_APX_POP2)
    {
        if (instruction->operand_count != 2 || instruction->operands[0].kind != ASSEMBLY_OPERAND_REGISTER ||
            instruction->operands[1].kind != ASSEMBLY_OPERAND_REGISTER || instruction->operands[0].reg.class != ASSEMBLY_REGISTER_GPR ||
            instruction->operands[1].reg.class != ASSEMBLY_REGISTER_GPR || instruction->operands[0].reg.width != 64 ||
            instruction->operands[1].reg.width != 64 || instruction->operands[0].reg.index == 4 || instruction->operands[1].reg.index == 4)
        {
            return false;
        }
        instruction->size = 6;
        return true;
    }
    if (!assembly_x86_opcode_is_apx_binary(instruction->opcode) || instruction->operand_count != 3 ||
        instruction->operands[0].kind != ASSEMBLY_OPERAND_REGISTER || instruction->operands[1].kind != ASSEMBLY_OPERAND_REGISTER ||
        instruction->operands[2].kind != ASSEMBLY_OPERAND_REGISTER || instruction->operands[0].reg.class != ASSEMBLY_REGISTER_GPR ||
        instruction->operands[1].reg.class != ASSEMBLY_REGISTER_GPR || instruction->operands[2].reg.class != ASSEMBLY_REGISTER_GPR ||
        (instruction->operands[0].reg.width != 8 && instruction->operands[0].reg.width != 16 &&
         instruction->operands[0].reg.width != 32 && instruction->operands[0].reg.width != 64) ||
        instruction->operands[1].reg.width != instruction->operands[0].reg.width ||
        instruction->operands[2].reg.width != instruction->operands[0].reg.width || instruction->operands[0].reg.high_byte ||
        instruction->operands[1].reg.high_byte || instruction->operands[2].reg.high_byte)
    {
        return false;
    }
    instruction->width = instruction->operands[0].reg.width;
    instruction->size = 6;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_nf_instruction_size(AssemblyInstruction* instruction)
{
    if (!assembly_x86_opcode_is_apx_binary(instruction->opcode) || instruction->operand_count != 2)
    {
        return false;
    }
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY))
    {
        return false;
    }
    u16 width = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg.width
                : second->kind == ASSEMBLY_OPERAND_REGISTER ? second->reg.width
                                                             : first->memory.width;
    if (width != 8 && width != 16 && width != 32 && width != 64)
    {
        return false;
    }
    if (first->kind == ASSEMBLY_OPERAND_REGISTER && (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte))
    {
        return false;
    }
    if (second->kind == ASSEMBLY_OPERAND_REGISTER &&
        (second->reg.class != ASSEMBLY_REGISTER_GPR || second->reg.width != width || second->reg.high_byte))
    {
        return false;
    }
    if (first->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (first->memory.width && first->memory.width != width)
        {
            return false;
        }
        first->memory.width = width;
    }
    if (second->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (second->memory.width && second->memory.width != width)
        {
            return false;
        }
        second->memory.width = width;
    }
    u32 address_size = 1;
    AssemblyOperand* memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
    if (memory && !assembly_x86_memory_encoding_size(memory->memory, &address_size))
    {
        return false;
    }
    instruction->width = width;
    instruction->size = 4u + 1u + address_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyVectorForm const* vector_form = assembly_x86_vector_form(opcode);
    if (!assembly_x86_lock_prefix_legal(instruction))
    {
        return false;
    }
    if (assembly_x86_instruction_has_high_byte(*instruction) &&
        (instruction->evex || assembly_x86_instruction_has_extended_gpr(*instruction) ||
         assembly_x86_instruction_operands_need_rex(*instruction)))
    {
        return false;
    }
    if (!vector_form && assembly_x86_instruction_has_any_decorator(*instruction))
    {
        return false;
    }
    u32 lock_size = instruction->lock_prefix ? 1 : 0;
    if (instruction->no_flags && !assembly_x86_opcode_is_apx_binary(opcode))
    {
        return false;
    }
    if (assembly_x86_opcode_is_amx(opcode))
    {
        return assembly_x86_amx_instruction_size(instruction);
    }
    if (assembly_x86_opcode_is_mask(opcode))
    {
        return assembly_x86_mask_instruction_size(instruction);
    }
    if (opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || opcode == ASSEMBLY_OPCODE_X86_APX_POP2 ||
        (assembly_x86_opcode_is_apx_binary(opcode) && instruction->operand_count == 3))
    {
        return assembly_x86_apx_ndd_instruction_size(instruction);
    }
    if (instruction->no_flags && assembly_x86_opcode_is_apx_binary(opcode) && instruction->operand_count == 2)
    {
        return assembly_x86_apx_nf_instruction_size(instruction);
    }
    if (instruction->evex && vector_form)
    {
        return assembly_x86_evex_instruction_size(instruction, vector_form);
    }
    if (assembly_x86_instruction_has_extended_gpr(*instruction))
    {
        return assembly_x86_apx_legacy_instruction_size(instruction);
    }
    if (assembly_x86_opcode_is_bit_atomic(opcode))
    {
        return assembly_x86_bit_atomic_instruction_size(instruction);
    }
    if (opcode == ASSEMBLY_OPCODE_X86_LEA)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR ||
            (first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64) || second->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->width = first->reg.width;
        u8 rex = assembly_x86_memory_rex_needed(instruction->width, first->reg, second->memory);
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 1u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_MOVZX || opcode == ASSEMBLY_OPCODE_X86_MOVSX || opcode == ASSEMBLY_OPCODE_X86_MOVSXD)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.width == 8 ||
            (first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        u16 source_width = second->kind == ASSEMBLY_OPERAND_REGISTER ? second->reg.width : second->memory.width;
        if (opcode == ASSEMBLY_OPCODE_X86_MOVSXD)
        {
            if (first->reg.width != 64)
            {
                return false;
            }
            if (second->kind == ASSEMBLY_OPERAND_REGISTER)
            {
                if (second->reg.class != ASSEMBLY_REGISTER_GPR || second->reg.width != 32)
                {
                    return false;
                }
            }
            else
            {
                if (second->memory.width && second->memory.width != 32)
                {
                    return false;
                }
                second->memory.width = 32;
            }
            source_width = 32;
        }
        else
        {
            if (source_width != 8 && source_width != 16)
            {
                return false;
            }
            if ((source_width == 16 && first->reg.width == 16) || first->reg.width <= source_width)
            {
                return false;
            }
        }
        if (second->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (second->reg.class != ASSEMBLY_REGISTER_GPR || second->reg.width != source_width)
            {
                return false;
            }
        }
        else
        {
            if (!second->memory.width)
            {
                second->memory.width = source_width;
            }
            if (second->memory.width != source_width)
            {
                return false;
            }
        }
        u8 rex = second->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_extension_rex_needed(first->reg.width, first->reg, second->reg, source_width)
                       : assembly_x86_memory_rex_needed(first->reg.width, first->reg, second->memory);
        if ((second->kind == ASSEMBLY_OPERAND_REGISTER && rex && (first->reg.high_byte || second->reg.high_byte)) ||
            (second->kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_memory_rex_conflicts_high_byte(first->reg.width, first->reg, second->memory)))
        {
            return false;
        }
        u32 address_size = 1;
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->width = first->reg.width;
        instruction->size = (u32)(instruction->width == 16) + (u32)rex +
                            (opcode == ASSEMBLY_OPCODE_X86_MOVSXD ? 1u : 2u) + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_rotate(opcode))
    {
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER &&
             (first->reg.class != ASSEMBLY_REGISTER_GPR ||
              (first->reg.width != 8 && first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64))) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY &&
             (first->memory.width != 8 && first->memory.width != 16 && first->memory.width != 32 && first->memory.width != 64)) ||
            (!assembly_x86_count_is_cl(*second) && !assembly_x86_count_immediate_valid(*second)))
        {
            return false;
        }
        instruction->width = assembly_operand_width(*first);
        u32 address_size = 1;
        u8 rex = false;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            rex = assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){0}, first->memory);
        }
        else
        {
            rex = assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0});
        }
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 1u + address_size +
                            (u32)(second->kind == ASSEMBLY_OPERAND_EXPRESSION && second->expression.addend != 1);
        instruction->rip_relocation_trailing = second->kind == ASSEMBLY_OPERAND_EXPRESSION && second->expression.addend != 1;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_SHLD || opcode == ASSEMBLY_OPCODE_X86_SHRD)
    {
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != ASSEMBLY_REGISTER_GPR) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER || second->reg.class != ASSEMBLY_REGISTER_GPR) ||
            (!assembly_x86_count_is_cl(instruction->operands[2]) && !assembly_x86_count_immediate_valid(instruction->operands[2])))
        {
            return false;
        }
        instruction->width = assembly_operand_width(*first);
        if (!instruction->width)
        {
            instruction->width = second->reg.width;
        }
        if (instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_memory_set_width(first, instruction->width))
            {
                return false;
            }
        }
        if (second->reg.width != instruction->width)
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = false;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            rex = assembly_x86_memory_rex_needed(instruction->width, second->reg, first->memory);
        }
        else
        {
            rex = assembly_x86_rex_needed(instruction->width, second->reg, first->reg);
        }
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 2u + address_size +
                            (u32)(instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION);
        instruction->rip_relocation_trailing = instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_NOP || opcode == ASSEMBLY_OPCODE_X86_RET || opcode == ASSEMBLY_OPCODE_X86_INT3 ||
        opcode == ASSEMBLY_OPCODE_X86_CWDE || opcode == ASSEMBLY_OPCODE_X86_CDQ)
    {
        instruction->size = 1;
        return instruction->operand_count == 0;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CBW || opcode == ASSEMBLY_OPCODE_X86_CDQE || opcode == ASSEMBLY_OPCODE_X86_CWD ||
        opcode == ASSEMBLY_OPCODE_X86_CQO)
    {
        instruction->size = 2;
        return instruction->operand_count == 0;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_EMMS)
    {
        instruction->size = 2;
        return instruction->operand_count == 0;
    }
    if (assembly_x86_opcode_is_x87_zero_operand(opcode))
    {
        instruction->size = opcode == ASSEMBLY_OPCODE_X86_FWAIT ? 1
                            : opcode == ASSEMBLY_OPCODE_X86_FINIT || opcode == ASSEMBLY_OPCODE_X86_FCLEX ? 3
                                                                                                          : 2;
        return instruction->operand_count == 0;
    }
    if (assembly_x86_opcode_is_x87_data(opcode))
    {
        if (first->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if ((opcode != ASSEMBLY_OPCODE_X86_FLD && opcode != ASSEMBLY_OPCODE_X86_FST && opcode != ASSEMBLY_OPCODE_X86_FSTP) ||
                first->reg.class != ASSEMBLY_REGISTER_X87)
            {
                return false;
            }
            instruction->width = 80;
            instruction->size = 2;
            return true;
        }
        if (first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        u16 width = first->memory.width;
        u8 valid_width = opcode == ASSEMBLY_OPCODE_X86_FLD ? width == 32 || width == 64 || width == 80
                           : opcode == ASSEMBLY_OPCODE_X86_FST ? width == 32 || width == 64
                           : opcode == ASSEMBLY_OPCODE_X86_FSTP ? width == 32 || width == 64 || width == 80
                           : opcode == ASSEMBLY_OPCODE_X86_FILD ? width == 16 || width == 32 || width == 64
                           : opcode == ASSEMBLY_OPCODE_X86_FIST ? width == 16 || width == 32
                                                               : width == 16 || width == 32 || width == 64;
        u32 address_size = 0;
        if (!valid_width || !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->width = width;
        instruction->size = (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_x87_arithmetic(opcode))
    {
        if (instruction->operand_count == 1 && first->kind == ASSEMBLY_OPERAND_MEMORY &&
            (first->memory.width == 32 || first->memory.width == 64))
        {
            u32 address_size = 0;
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            instruction->width = first->memory.width;
            instruction->size = (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
            return true;
        }
        if (instruction->operand_count != 2 || first->kind != ASSEMBLY_OPERAND_REGISTER || second->kind != ASSEMBLY_OPERAND_REGISTER ||
            first->reg.class != ASSEMBLY_REGISTER_X87 || second->reg.class != ASSEMBLY_REGISTER_X87 ||
            (first->reg.index != 0 && second->reg.index != 0))
        {
            return false;
        }
        instruction->width = 80;
        instruction->size = 2;
        return true;
    }
    if (assembly_x86_opcode_is_x87_pop_arithmetic(opcode))
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || second->kind != ASSEMBLY_OPERAND_REGISTER ||
            first->reg.class != ASSEMBLY_REGISTER_X87 || second->reg.class != ASSEMBLY_REGISTER_X87 || second->reg.index != 0)
        {
            return false;
        }
        instruction->width = 80;
        instruction->size = 2;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_FXCH)
    {
        instruction->width = 80;
        instruction->size = 2;
        return first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class == ASSEMBLY_REGISTER_X87;
    }
    if (assembly_x86_opcode_is_x87_compare(opcode))
    {
        if (opcode == ASSEMBLY_OPCODE_X86_FCOMPP || opcode == ASSEMBLY_OPCODE_X86_FUCOMPP)
        {
            instruction->size = 2;
            return instruction->operand_count == 0;
        }
        if (opcode == ASSEMBLY_OPCODE_X86_FCOM || opcode == ASSEMBLY_OPCODE_X86_FCOMP)
        {
            if (first->kind == ASSEMBLY_OPERAND_REGISTER)
            {
                instruction->size = 2;
                return first->reg.class == ASSEMBLY_REGISTER_X87;
            }
            u32 address_size = 0;
            if (first->kind != ASSEMBLY_OPERAND_MEMORY || (first->memory.width != 32 && first->memory.width != 64) ||
                !assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            instruction->width = first->memory.width;
            instruction->size = (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
            return true;
        }
        if (opcode == ASSEMBLY_OPCODE_X86_FUCOM || opcode == ASSEMBLY_OPCODE_X86_FUCOMP)
        {
            instruction->size = 2;
            return first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class == ASSEMBLY_REGISTER_X87;
        }
        instruction->size = 2;
        return first->kind == ASSEMBLY_OPERAND_REGISTER && second->kind == ASSEMBLY_OPERAND_REGISTER &&
               first->reg.class == ASSEMBLY_REGISTER_X87 && second->reg.class == ASSEMBLY_REGISTER_X87 && first->reg.index == 0;
    }
    if (assembly_x86_opcode_is_x87_integer_arithmetic(opcode))
    {
        u32 address_size = 0;
        if (first->kind != ASSEMBLY_OPERAND_MEMORY || (first->memory.width != 16 && first->memory.width != 32) ||
            !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->width = first->memory.width;
        instruction->size = (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_x87_state_memory(opcode))
    {
        u8 waited = opcode == ASSEMBLY_OPCODE_X86_FSTCW || opcode == ASSEMBLY_OPCODE_X86_FSTENV ||
                      opcode == ASSEMBLY_OPCODE_X86_FSAVE || opcode == ASSEMBLY_OPCODE_X86_FSTSW;
        if ((opcode == ASSEMBLY_OPCODE_X86_FNSTSW || opcode == ASSEMBLY_OPCODE_X86_FSTSW) &&
            first->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            instruction->width = 16;
            instruction->size = waited ? 3 : 2;
            return first->reg.class == ASSEMBLY_REGISTER_GPR && first->reg.width == 16 && first->reg.index == 0;
        }
        if (first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        u16 expected_width = opcode == ASSEMBLY_OPCODE_X86_FBLD || opcode == ASSEMBLY_OPCODE_X86_FBSTP ? 80
                            : opcode == ASSEMBLY_OPCODE_X86_FLDCW || opcode == ASSEMBLY_OPCODE_X86_FNSTCW ||
                                      opcode == ASSEMBLY_OPCODE_X86_FSTCW || opcode == ASSEMBLY_OPCODE_X86_FNSTSW ||
                                      opcode == ASSEMBLY_OPCODE_X86_FSTSW
                                ? 16
                                : 0;
        if (first->memory.width && first->memory.width != expected_width)
        {
            return false;
        }
        u32 address_size = 0;
        if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->width = expected_width;
        instruction->size = (u32)waited + (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_x87_stack_control(opcode))
    {
        instruction->width = 80;
        instruction->size = 2;
        if (opcode == ASSEMBLY_OPCODE_X86_FINCSTP || opcode == ASSEMBLY_OPCODE_X86_FDECSTP)
        {
            return instruction->operand_count == 0;
        }
        return first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class == ASSEMBLY_REGISTER_X87;
    }
    if (assembly_x86_opcode_is_legacy_packed(opcode))
    {
        u8 move = opcode == ASSEMBLY_OPCODE_X86_MOVQ_MMX;
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY) ||
            (!move && first->kind != ASSEMBLY_OPERAND_REGISTER))
        {
            return false;
        }
        AssemblyRegister packed_reg = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg : second->reg;
        if ((packed_reg.class != ASSEMBLY_REGISTER_MMX && packed_reg.class != ASSEMBLY_REGISTER_XMM) ||
            (move && packed_reg.class != ASSEMBLY_REGISTER_MMX) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != packed_reg.class) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != packed_reg.class))
        {
            return false;
        }
        AssemblyOperand* memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : second->kind == ASSEMBLY_OPERAND_MEMORY ? second : 0;
        if (memory)
        {
            if (memory->memory.width && memory->memory.width != packed_reg.width)
            {
                return false;
            }
            memory->memory.width = packed_reg.width;
        }
        u8 load = !move || first->kind == ASSEMBLY_OPERAND_REGISTER;
        AssemblyRegister reg = load ? packed_reg : second->reg;
        AssemblyOperand* rm = load ? second : first;
        u32 address_size = 1;
        u8 rex = rm->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(0, reg, rm->reg)
                       : assembly_x86_memory_rex_needed(0, reg, rm->memory);
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        instruction->width = packed_reg.width;
        instruction->size = (u32)(packed_reg.class == ASSEMBLY_REGISTER_XMM) + (u32)rex + 2u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_JCC)
    {
        instruction->size = 6;
        return first->kind == ASSEMBLY_OPERAND_EXPRESSION;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_SETCC)
    {
        if (first->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.width != 8)
            {
                return false;
            }
            instruction->width = 8;
            instruction->size = (assembly_x86_rex_needed(8, (AssemblyRegister){0}, first->reg) ? 1 : 0) + 3;
            return true;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && (!first->memory.width || first->memory.width == 8))
        {
            u32 address_size = 0;
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            first->memory.width = 8;
            instruction->width = 8;
            instruction->size = (u32)assembly_x86_memory_rex_needed(8, (AssemblyRegister){0}, first->memory) + 2u + address_size;
            return true;
        }
        return false;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CMOVCC)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.width == 8 ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER &&
             (second->reg.class != ASSEMBLY_REGISTER_GPR || second->reg.width != first->reg.width)))
        {
            return false;
        }
        instruction->width = first->reg.width;
        if (second->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (second->memory.width && second->memory.width != instruction->width)
            {
                return false;
            }
            second->memory.width = instruction->width;
        }
        u32 address_size = 1;
        u8 rex = second->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, second->reg)
                       : assembly_x86_memory_rex_needed(instruction->width, first->reg, second->memory);
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 2u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_sse2(opcode))
    {
        u8 move = opcode >= ASSEMBLY_OPCODE_X86_MOVAPS && opcode <= ASSEMBLY_OPCODE_X86_MOVDQU;
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY) ||
            (!move && first->kind != ASSEMBLY_OPERAND_REGISTER) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER && (first->reg.class != ASSEMBLY_REGISTER_XMM || first->reg.index >= 16)) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER && (second->reg.class != ASSEMBLY_REGISTER_XMM || second->reg.index >= 16)))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (first->memory.width && first->memory.width != 128)
            {
                return false;
            }
            first->memory.width = 128;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (second->memory.width && second->memory.width != 128)
            {
                return false;
            }
            second->memory.width = 128;
        }
        u8 load = !move || first->kind == ASSEMBLY_OPERAND_REGISTER;
        AssemblyRegister reg = load ? first->reg : second->reg;
        AssemblyOperand* rm = load ? second : first;
        u32 address_size = 1;
        u8 rex = rm->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(0, reg, rm->reg)
                       : assembly_x86_memory_rex_needed(0, reg, rm->memory);
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        u8 mandatory_prefix = opcode == ASSEMBLY_OPCODE_X86_MOVAPD || opcode == ASSEMBLY_OPCODE_X86_MOVUPD ||
                                opcode == ASSEMBLY_OPCODE_X86_MOVDQA || opcode == ASSEMBLY_OPCODE_X86_MOVDQU ||
                                opcode == ASSEMBLY_OPCODE_X86_XORPD || opcode == ASSEMBLY_OPCODE_X86_PXOR ||
                                opcode == ASSEMBLY_OPCODE_X86_ADDPD || opcode == ASSEMBLY_OPCODE_X86_ADDSS ||
                                opcode == ASSEMBLY_OPCODE_X86_ADDSD || opcode == ASSEMBLY_OPCODE_X86_SUBPD ||
                                opcode == ASSEMBLY_OPCODE_X86_MULPD || opcode == ASSEMBLY_OPCODE_X86_DIVPD;
        instruction->width = 128;
        instruction->size = (u32)mandatory_prefix + (u32)rex + 2u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_avx(opcode))
    {
        u8 move = assembly_x86_opcode_is_avx_move(opcode);
        u8 scalar = opcode == ASSEMBLY_OPCODE_X86_VADDSS || opcode == ASSEMBLY_OPCODE_X86_VADDSD;
        AssemblyOperand* source = move ? second : instruction->operands + 2;
        if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            source->kind == ASSEMBLY_OPERAND_EXPRESSION || source->kind == ASSEMBLY_OPERAND_NONE ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && source->kind == ASSEMBLY_OPERAND_MEMORY) ||
            (!move && (first->kind != ASSEMBLY_OPERAND_REGISTER || second->kind != ASSEMBLY_OPERAND_REGISTER)) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != ASSEMBLY_REGISTER_XMM &&
             first->reg.class != ASSEMBLY_REGISTER_YMM) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != ASSEMBLY_REGISTER_XMM &&
             second->reg.class != ASSEMBLY_REGISTER_YMM) ||
            (source->kind == ASSEMBLY_OPERAND_REGISTER && source->reg.class != ASSEMBLY_REGISTER_XMM &&
             source->reg.class != ASSEMBLY_REGISTER_YMM))
        {
            return false;
        }
        AssemblyRegister vector_reg = first->kind == ASSEMBLY_OPERAND_REGISTER ? first->reg : second->reg;
        if ((scalar && vector_reg.class != ASSEMBLY_REGISTER_XMM) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.class != vector_reg.class) ||
            (source->kind == ASSEMBLY_OPERAND_REGISTER && source->reg.class != vector_reg.class))
        {
            return false;
        }
        u16 memory_width = scalar ? (opcode == ASSEMBLY_OPCODE_X86_VADDSS ? 32 : 64) : vector_reg.width;
        AssemblyOperand* memory = first->kind == ASSEMBLY_OPERAND_MEMORY ? first : source->kind == ASSEMBLY_OPERAND_MEMORY ? source : 0;
        if (memory)
        {
            if (memory->memory.width && memory->memory.width != memory_width)
            {
                return false;
            }
            memory->memory.width = memory_width;
        }
        u8 load = !move || first->kind == ASSEMBLY_OPERAND_REGISTER;
        AssemblyOperand* rm = load ? source : first;
        u32 address_size = 1;
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        instruction->width = vector_reg.width;
        instruction->size = (assembly_x86_avx_map(opcode) != 1 || assembly_x86_vex_three_byte_needed(*rm) ? 3u : 2u) + 1u + address_size;
        return true;
    }
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        if (instruction->operands[operand_index].kind == ASSEMBLY_OPERAND_REGISTER &&
            instruction->operands[operand_index].reg.class != ASSEMBLY_REGISTER_GPR)
        {
            return false;
        }
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CALL || opcode == ASSEMBLY_OPCODE_X86_JMP)
    {
        if (first->kind == ASSEMBLY_OPERAND_EXPRESSION)
        {
            instruction->size = 5;
            return true;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.width == 64)
        {
            instruction->width = 64;
            instruction->size = first->reg.index >= 8 ? 3 : 2;
            return true;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && (!first->memory.width || first->memory.width == 64))
        {
            u32 address_size = 0;
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            instruction->width = 64;
            first->memory.width = 64;
            instruction->size = (u32)assembly_x86_memory_rex_needed(0, (AssemblyRegister){0}, first->memory) + 1u + address_size;
            return true;
        }
        return false;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_PUSH || opcode == ASSEMBLY_OPCODE_X86_POP)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.width != 64)
        {
            return false;
        }
        instruction->width = 64;
        instruction->size = first->reg.index >= 8 ? 2 : 1;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_INC || opcode == ASSEMBLY_OPCODE_X86_DEC || opcode == ASSEMBLY_OPCODE_X86_NEG ||
        opcode == ASSEMBLY_OPCODE_X86_NOT || opcode == ASSEMBLY_OPCODE_X86_MUL || opcode == ASSEMBLY_OPCODE_X86_DIV ||
        opcode == ASSEMBLY_OPCODE_X86_IDIV || (opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 1))
    {
        instruction->width = assembly_operand_width(*first);
        if (!instruction->width || (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64) ||
            instruction->operand_count != 1 ||
            (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = first->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0})
                       : assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){0}, first->memory);
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->size = lock_size + (u32)(instruction->width == 16) + (u32)rex + 1u + address_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 3)
    {
        AssemblyOperand* third = instruction->operands + 2;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.width == 8 ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER &&
             (second->reg.class != ASSEMBLY_REGISTER_GPR || second->reg.width != first->reg.width)) ||
            third->kind != ASSEMBLY_OPERAND_EXPRESSION || third->expression.has_symbol)
        {
            return false;
        }
        instruction->width = first->reg.width;
        if (second->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (second->memory.width && second->memory.width != instruction->width)
            {
                return false;
            }
            second->memory.width = instruction->width;
        }
        u16 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
        s64 immediate = third->expression.addend;
        if (!assembly_x86_immediate_fits(immediate, full_immediate_width, true))
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = second->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, second->reg)
                       : assembly_x86_memory_rex_needed(instruction->width, first->reg, second->memory);
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        u32 immediate_size = immediate >= INT8_MIN && immediate <= INT8_MAX ? 1 : full_immediate_width / 8;
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 1u + address_size + immediate_size;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_SHL || opcode == ASSEMBLY_OPCODE_X86_SHR || opcode == ASSEMBLY_OPCODE_X86_SAR)
    {
        instruction->width = assembly_operand_width(*first);
        if (!instruction->width || (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            second->kind != ASSEMBLY_OPERAND_EXPRESSION || second->expression.has_symbol ||
            second->expression.addend < 0 || second->expression.addend > UINT8_MAX)
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = first->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0})
                       : assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){0}, first->memory);
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 1u + address_size +
                            (u32)(second->expression.addend != 1);
        return true;
    }
    if (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    instruction->width = assembly_operand_width(*first);
    if (!instruction->width)
    {
        instruction->width = assembly_operand_width(*second);
    }
    if (!instruction->width ||
        (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64))
    {
        return false;
    }
    if (first->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (first->memory.width && first->memory.width != instruction->width)
        {
            return false;
        }
        first->memory.width = instruction->width;
    }
    if (second->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (second->memory.width && second->memory.width != instruction->width)
        {
            return false;
        }
        second->memory.width = instruction->width;
    }
    if (second->kind == ASSEMBLY_OPERAND_REGISTER || second->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if ((second->kind == ASSEMBLY_OPERAND_REGISTER && second->reg.width != instruction->width) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY) ||
            (opcode == ASSEMBLY_OPCODE_X86_IMUL && first->kind != ASSEMBLY_OPERAND_REGISTER) ||
            (opcode != ASSEMBLY_OPCODE_X86_MOV && opcode != ASSEMBLY_OPCODE_X86_ADD && opcode != ASSEMBLY_OPCODE_X86_ADC &&
             opcode != ASSEMBLY_OPCODE_X86_SUB && opcode != ASSEMBLY_OPCODE_X86_SBB &&
             opcode != ASSEMBLY_OPCODE_X86_AND && opcode != ASSEMBLY_OPCODE_X86_OR && opcode != ASSEMBLY_OPCODE_X86_XOR &&
             opcode != ASSEMBLY_OPCODE_X86_CMP && opcode != ASSEMBLY_OPCODE_X86_TEST && opcode != ASSEMBLY_OPCODE_X86_IMUL))
        {
            return false;
        }
        if (opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->width == 8)
        {
            return false;
        }
        AssemblyRegister reg = opcode == ASSEMBLY_OPCODE_X86_IMUL || second->kind == ASSEMBLY_OPERAND_MEMORY ? first->reg : second->reg;
        AssemblyOperand* rm = opcode == ASSEMBLY_OPCODE_X86_IMUL || second->kind == ASSEMBLY_OPERAND_MEMORY ? second : first;
        u32 address_size = 1;
        u8 rex = rm->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, reg, rm->reg)
                       : assembly_x86_memory_rex_needed(instruction->width, reg, rm->memory);
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        if ((rm->kind == ASSEMBLY_OPERAND_REGISTER && assembly_x86_rex_conflicts_high_byte(instruction->width, reg, rm->reg)) ||
            (rm->kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_memory_rex_conflicts_high_byte(instruction->width, reg, rm->memory)))
        {
            return false;
        }
        instruction->size = lock_size + (u32)(instruction->width == 16) + (u32)rex +
                            (opcode == ASSEMBLY_OPCODE_X86_IMUL ? 2u : 1u) + address_size;
        return true;
    }
    if (second->kind != ASSEMBLY_OPERAND_EXPRESSION || second->expression.has_symbol || opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return false;
    }
    s64 immediate = second->expression.addend;
    if (opcode == ASSEMBLY_OPCODE_X86_MOV)
    {
        u16 immediate_width = first->kind == ASSEMBLY_OPERAND_MEMORY && instruction->width == 64 ? 32 : instruction->width;
        if (!assembly_x86_immediate_fits(immediate, immediate_width, first->kind == ASSEMBLY_OPERAND_MEMORY && instruction->width == 64))
        {
            return false;
        }
        u32 address_size = 1;
        u8 rex = first->kind == ASSEMBLY_OPERAND_REGISTER
                       ? assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0})
                       : assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){0}, first->memory);
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->size = (u32)(instruction->width == 16) + (u32)rex + 1u +
                            (first->kind == ASSEMBLY_OPERAND_MEMORY ? address_size : 0u) + immediate_width / 8;
        return true;
    }
    if (opcode != ASSEMBLY_OPCODE_X86_ADD && opcode != ASSEMBLY_OPCODE_X86_ADC && opcode != ASSEMBLY_OPCODE_X86_SUB &&
        opcode != ASSEMBLY_OPCODE_X86_SBB && opcode != ASSEMBLY_OPCODE_X86_AND && opcode != ASSEMBLY_OPCODE_X86_OR &&
        opcode != ASSEMBLY_OPCODE_X86_XOR && opcode != ASSEMBLY_OPCODE_X86_CMP &&
        opcode != ASSEMBLY_OPCODE_X86_TEST)
    {
        return false;
    }
    u8 signed_only = instruction->width == 64;
    u16 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
    if (!assembly_x86_immediate_fits(immediate, full_immediate_width, signed_only))
    {
        return false;
    }
    u32 immediate_size = opcode != ASSEMBLY_OPCODE_X86_TEST && immediate >= INT8_MIN && immediate <= INT8_MAX ? 1 : full_immediate_width / 8;
    u32 address_size = 1;
    u8 rex = first->kind == ASSEMBLY_OPERAND_REGISTER
                   ? assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0})
                   : assembly_x86_memory_rex_needed(instruction->width, (AssemblyRegister){0}, first->memory);
    if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
    {
        return false;
    }
    instruction->size = lock_size + (u32)(instruction->width == 16) + (u32)rex + 1u + address_size + immediate_size;
    return true;
}

BUSTER_GLOBAL_LOCAL void assembly_instruction_parse(AssemblyBuilder* builder, String8 statement, u32 line, u32 column, u64 offset,
                                                    Target target, AssemblySyntax syntax)
{
    u8 lock_prefix = false;
    u64 lock_end = 0;
    while (lock_end < statement.length && !assembly_space(statement.pointer[lock_end]))
    {
        lock_end += 1;
    }
    if (lock_end < statement.length && assembly_word_equal(string_slice(statement, 0, lock_end), S8("lock")))
    {
        lock_prefix = true;
        statement = assembly_trim(string_slice(statement, lock_end, statement.length));
        column += (u32)lock_end;
    }
    u8 pseudo_no_flags = false;
    if (target.cpu_arch == CPU_ARCH_X86_64 && statement.length && statement.pointer[0] == '{')
    {
        u64 pseudo_end = 1;
        while (pseudo_end < statement.length && statement.pointer[pseudo_end] != '}')
        {
            pseudo_end += 1;
        }
        if (pseudo_end < statement.length && assembly_word_equal(assembly_trim(string_slice(statement, 1, pseudo_end)), S8("nf")))
        {
            pseudo_no_flags = true;
            statement = assembly_trim(string_slice(statement, pseudo_end + 1, statement.length));
        }
    }
    u64 mnemonic_end = 0;
    while (mnemonic_end < statement.length && !assembly_space(statement.pointer[mnemonic_end]))
    {
        mnemonic_end += 1;
    }
    String8 mnemonic = {.pointer = statement.pointer, .length = mnemonic_end};
    String8 operands = assembly_trim((String8){.pointer = statement.pointer + mnemonic_end, .length = statement.length - mnemonic_end});
    u8 mnemonic_no_flags = false;
    u8 leading_rounding = 0;
    u8 leading_sae = false;
    if (target.cpu_arch == CPU_ARCH_X86_64 && operands.length && operands.pointer[0] == '{')
    {
        u64 leading_end = 1;
        while (leading_end < operands.length && operands.pointer[leading_end] != '}')
        {
            leading_end += 1;
        }
        if (leading_end < operands.length)
        {
            String8 decorator = assembly_trim(string_slice(operands, 1, leading_end));
            leading_rounding = assembly_word_equal(decorator, S8("rn-sae")) ? 1
                              : assembly_word_equal(decorator, S8("rd-sae")) ? 2
                              : assembly_word_equal(decorator, S8("ru-sae")) ? 3
                              : assembly_word_equal(decorator, S8("rz-sae")) ? 4
                              : 0;
            leading_sae = assembly_word_equal(decorator, S8("sae")) || leading_rounding != 0;
            u64 after_leading = leading_end + 1;
            while (after_leading < operands.length && assembly_space(operands.pointer[after_leading]))
            {
                after_leading += 1;
            }
            if (leading_sae && after_leading < operands.length && operands.pointer[after_leading] == ',')
            {
                operands = assembly_trim(string_slice(operands, after_leading + 1, operands.length));
            }
            else
            {
                leading_rounding = 0;
                leading_sae = false;
            }
        }
    }
    u64 mnemonic_brace = string_first_code_unit(mnemonic, '{');
    if (mnemonic_brace < mnemonic.length && mnemonic.length && mnemonic.pointer[mnemonic.length - 1] == '}' &&
        assembly_word_equal(string_slice(mnemonic, mnemonic_brace + 1, mnemonic.length - 1), S8("nf")))
    {
        mnemonic_no_flags = true;
        mnemonic = assembly_trim(string_slice(mnemonic, 0, mnemonic_brace));
    }
    AssemblyInstructionInfo info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    if (!assembly_instruction_lookup(target, syntax, mnemonic, &info))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION, line, column, (u32)mnemonic.length, S8("unknown instruction"));
        return;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_x86_opcode_is_sse2(info.opcode) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_SSE2))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                            S8("instruction requires the sse2 target feature"));
        return;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_x86_opcode_is_avx(info.opcode) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_512) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                            S8("instruction requires the avx target feature"));
        return;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && info.opcode == ASSEMBLY_OPCODE_X86_FISTTP &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_SSE3))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                            S8("instruction requires the sse3 target feature"));
        return;
    }
    TargetCpuFeature required_bit_atomic_feature = TARGET_CPU_FEATURE_NONE;
    String8 required_bit_atomic_feature_message = {0};
    if (info.opcode == ASSEMBLY_OPCODE_X86_POPCNT)
    {
        required_bit_atomic_feature = TARGET_CPU_FEATURE_X86_POPCNT;
        required_bit_atomic_feature_message = S8("instruction requires the popcnt target feature");
    }
    else if (info.opcode == ASSEMBLY_OPCODE_X86_LZCNT)
    {
        required_bit_atomic_feature = TARGET_CPU_FEATURE_X86_LZCNT;
        required_bit_atomic_feature_message = S8("instruction requires the lzcnt target feature");
    }
    else if (info.opcode == ASSEMBLY_OPCODE_X86_TZCNT)
    {
        required_bit_atomic_feature = TARGET_CPU_FEATURE_X86_BMI1;
        required_bit_atomic_feature_message = S8("instruction requires the bmi1 target feature");
    }
    else if (info.opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B)
    {
        required_bit_atomic_feature = TARGET_CPU_FEATURE_X86_CX16;
        required_bit_atomic_feature_message = S8("instruction requires the cx16 target feature");
    }
    if (required_bit_atomic_feature != TARGET_CPU_FEATURE_NONE &&
        !assembly_x86_target_has_bit_atomic_feature(target, required_bit_atomic_feature))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                            required_bit_atomic_feature_message);
        return;
    }
    if (builder->instruction_count >= builder->instruction_capacity)
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, line, column, (u32)statement.length, S8("too many statements"));
        return;
    }
    AssemblyInstruction instruction = {
        .offset = offset,
        .line = line,
        .column = column,
        .opcode = info.opcode,
        .operand_count = info.operand_count,
        .condition = info.condition,
        .lock_prefix = lock_prefix,
        .no_flags = pseudo_no_flags || mnemonic_no_flags || info.no_flags,
    };
    u8 parsed_operand_count = 0;
    u64 operand_start = 0;
    while (operand_start < operands.length && parsed_operand_count < BUSTER_ARRAY_LENGTH(instruction.operands))
    {
        u64 operand_end = operand_start;
        u32 delimiter_depth = 0;
        while (operand_end < operands.length)
        {
            char8 character = operands.pointer[operand_end];
            if (character == '(' || character == '[')
            {
                delimiter_depth += 1;
            }
            else if (character == ')' || character == ']')
            {
                if (!delimiter_depth)
                {
                    break;
                }
                delimiter_depth -= 1;
            }
            else if (character == ',' && !delimiter_depth)
            {
                break;
            }
            operand_end += 1;
        }
        String8 text = assembly_trim(string_slice(operands, operand_start, operand_end));
        AssemblyOperand* operand = instruction.operands + parsed_operand_count;
        if (!text.length)
        {
            break;
        }
        u8 branch = info.opcode == ASSEMBLY_OPCODE_X86_CALL || info.opcode == ASSEMBLY_OPCODE_X86_JMP || info.opcode == ASSEMBLY_OPCODE_X86_JCC;
        u8 indirect = syntax == ASSEMBLY_SYNTAX_ATT && branch && text.length && text.pointer[0] == '*';
        if (indirect)
        {
            text.pointer += 1;
            text.length -= 1;
        }
        if (!assembly_x86_operand_decorators_parse(&text, syntax, operand, &instruction.no_flags))
        {
            break;
        }
        if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_register_parse(text, syntax, &operand->reg))
        {
            if (syntax == ASSEMBLY_SYNTAX_ATT && branch && !indirect)
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_REGISTER;
        }
        else if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_x86_memory_parse(builder, text, syntax, &operand->memory))
        {
            if (branch && syntax == ASSEMBLY_SYNTAX_ATT && !indirect)
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_MEMORY;
        }
        else
        {
            u8 immediate = syntax == ASSEMBLY_SYNTAX_ATT && text.pointer[0] == '$';
            if (immediate)
            {
                text.pointer += 1;
                text.length -= 1;
            }
            else if (syntax != ASSEMBLY_SYNTAX_ATT && text.pointer[0] == '$')
            {
                break;
            }
            else if (syntax == ASSEMBLY_SYNTAX_ATT && target.cpu_arch == CPU_ARCH_X86_64 && !branch)
            {
                break;
            }
            if (!assembly_expression_parse(builder, text, &operand->expression))
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_EXPRESSION;
        }
        parsed_operand_count += 1;
        if (operand_end == operands.length)
        {
            operand_start = operand_end;
        }
        else
        {
            operand_start = operand_end + 1;
        }
    }
    u8 valid_operand_count = target.cpu_arch == CPU_ARCH_X86_64
                                   ? assembly_x86_operand_count_valid(info.opcode, parsed_operand_count, info.operand_count)
                                   : parsed_operand_count == info.operand_count;
    if (!valid_operand_count || operand_start < operands.length)
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                            (u32)operands.length, S8("invalid instruction operands"));
        return;
    }
    instruction.operand_count = parsed_operand_count;
    if (target.cpu_arch == CPU_ARCH_X86_64 && syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 2)
    {
        AssemblyOperand temporary = instruction.operands[0];
        instruction.operands[0] = instruction.operands[1];
        instruction.operands[1] = temporary;
    }
    else if (target.cpu_arch == CPU_ARCH_X86_64 && syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 3)
    {
        AssemblyVectorForm const* vector_form = assembly_x86_vector_form(instruction.opcode);
        if (vector_form && (vector_form->flags & ASSEMBLY_VECTOR_FORM_SOURCE_RM) &&
            (vector_form->flags & ASSEMBLY_VECTOR_FORM_IMMEDIATE))
        {
            AssemblyOperand immediate = instruction.operands[0];
            AssemblyOperand source = instruction.operands[1];
            AssemblyOperand destination = instruction.operands[2];
            instruction.operands[0] = destination;
            instruction.operands[1] = source;
            instruction.operands[2] = immediate;
        }
        else
        {
            AssemblyOperand destination = instruction.operands[2];
            instruction.operands[2] = instruction.operands[0];
            instruction.operands[0] = destination;
            if (instruction.opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction.operands[1].kind == ASSEMBLY_OPERAND_EXPRESSION)
            {
                AssemblyOperand immediate = instruction.operands[1];
                instruction.operands[1] = instruction.operands[2];
                instruction.operands[2] = immediate;
            }
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 2 &&
        instruction.operands[0].kind == ASSEMBLY_OPERAND_REGISTER && instruction.operands[0].reg.class == ASSEMBLY_REGISTER_X87 &&
        instruction.operands[0].reg.index != 0)
    {
        // The historical AT&T x87 register spellings reverse the non-commutative
        // opcode names as well as the operand order for a non-top destination.
        instruction.opcode = instruction.opcode == ASSEMBLY_OPCODE_X86_FSUB ? ASSEMBLY_OPCODE_X86_FSUBR
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FSUBR ? ASSEMBLY_OPCODE_X86_FSUB
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FDIV ? ASSEMBLY_OPCODE_X86_FDIVR
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FDIVR ? ASSEMBLY_OPCODE_X86_FDIV
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FSUBP ? ASSEMBLY_OPCODE_X86_FSUBRP
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FSUBRP ? ASSEMBLY_OPCODE_X86_FSUBP
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FDIVP ? ASSEMBLY_OPCODE_X86_FDIVRP
                             : instruction.opcode == ASSEMBLY_OPCODE_X86_FDIVRP ? ASSEMBLY_OPCODE_X86_FDIVP
                                                                                : instruction.opcode;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 4)
        {
            AssemblyVectorForm const* vector_form = assembly_x86_vector_form(instruction.opcode);
            if (vector_form && (vector_form->flags & ASSEMBLY_VECTOR_FORM_MASK_DESTINATION) &&
                (vector_form->flags & ASSEMBLY_VECTOR_FORM_IMMEDIATE))
            {
                AssemblyOperand immediate = instruction.operands[0];
                AssemblyOperand source_2 = instruction.operands[1];
                AssemblyOperand source_1 = instruction.operands[2];
                AssemblyOperand destination = instruction.operands[3];
                instruction.operands[0] = destination;
                instruction.operands[1] = source_1;
                instruction.operands[2] = source_2;
                instruction.operands[3] = immediate;
            }
            else
            {
                AssemblyOperand source_2 = instruction.operands[0];
                AssemblyOperand source_1 = instruction.operands[1];
                AssemblyOperand destination = instruction.operands[2];
                AssemblyOperand immediate = instruction.operands[3];
                instruction.operands[0] = destination;
                instruction.operands[1] = source_1;
                instruction.operands[2] = source_2;
                instruction.operands[3] = immediate;
            }
        }
        assembly_x86_x87_normalize_omitted_operands(&instruction, syntax);
        if (instruction.opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction.operand_count == 2 &&
            instruction.operands[1].kind == ASSEMBLY_OPERAND_EXPRESSION)
        {
            instruction.operands[2] = instruction.operands[1];
            instruction.operands[1] = instruction.operands[0];
            instruction.operand_count = 3;
        }
        if (leading_sae)
        {
            if (!instruction.operand_count || instruction.operands[0].kind != ASSEMBLY_OPERAND_REGISTER ||
                !assembly_x86_vector_register(instruction.operands[0].reg) || instruction.operands[0].rounding ||
                instruction.operands[0].sae)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("invalid instruction decorators"));
                return;
            }
            instruction.operands[0].rounding = leading_rounding;
            instruction.operands[0].sae = true;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && instruction.opcode == ASSEMBLY_OPCODE_X86_MOV &&
        ((instruction.operands[0].kind == ASSEMBLY_OPERAND_REGISTER &&
          instruction.operands[0].reg.class == ASSEMBLY_REGISTER_MMX) ||
         (instruction.operands[1].kind == ASSEMBLY_OPERAND_REGISTER &&
          instruction.operands[1].reg.class == ASSEMBLY_REGISTER_MMX)))
    {
        instruction.opcode = ASSEMBLY_OPCODE_X86_MOVQ_MMX;
    }
    if (info.suffix_width)
    {
        instruction.width = info.suffix_width;
        for (u32 operand_index = 0; operand_index < instruction.operand_count; operand_index += 1)
        {
            u8 suffix_applies = true;
            if (instruction.opcode == ASSEMBLY_OPCODE_X86_LEA || assembly_x86_opcode_is_rotate(instruction.opcode) ||
                instruction.opcode == ASSEMBLY_OPCODE_X86_MOVZX || instruction.opcode == ASSEMBLY_OPCODE_X86_MOVSX ||
                instruction.opcode == ASSEMBLY_OPCODE_X86_MOVSXD)
            {
                suffix_applies = operand_index == 0;
            }
            else if (instruction.opcode == ASSEMBLY_OPCODE_X86_SHLD || instruction.opcode == ASSEMBLY_OPCODE_X86_SHRD)
            {
                suffix_applies = operand_index < 2;
            }
            if (!suffix_applies)
            {
                continue;
            }
            if (instruction.operands[operand_index].kind == ASSEMBLY_OPERAND_REGISTER && instruction.operands[operand_index].reg.width != info.suffix_width)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("register width does not match mnemonic suffix"));
                return;
            }
            if (instruction.operands[operand_index].kind == ASSEMBLY_OPERAND_MEMORY)
            {
                if (instruction.operands[operand_index].memory.width && instruction.operands[operand_index].memory.width != info.suffix_width)
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                        (u32)operands.length, S8("memory width does not match mnemonic suffix"));
                    return;
                }
                instruction.operands[operand_index].memory.width = info.suffix_width;
            }
        }
    }
    if (info.source_width)
    {
        AssemblyOperand* source = instruction.operands + 1;
        if (source->kind == ASSEMBLY_OPERAND_REGISTER && source->reg.width != info.source_width)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("source width does not match mnemonic"));
            return;
        }
        if (source->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (source->memory.width && source->memory.width != info.source_width)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("memory width does not match mnemonic"));
                return;
            }
            source->memory.width = info.source_width;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        instruction.evex = assembly_x86_instruction_uses_evex(instruction, target);
        AssemblyVectorForm const* vector_form = assembly_x86_vector_form(instruction.opcode);
        if (vector_form && instruction.evex)
        {
            u16 vector_width = assembly_x86_instruction_vector_width(instruction, vector_form);
            if (!assembly_x86_target_has_evex(target, vector_width))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                    vector_width == 512 ? S8("512-bit EVEX instruction requires avx512f or avx10.512")
                                                        : S8("EVEX instruction requires avx512vl or avx10"));
                return;
            }
            if ((vector_form->flags & ASSEMBLY_VECTOR_FORM_AVX512BW) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                    S8("instruction requires avx512bw or avx10"));
                return;
            }
        }
        if (assembly_x86_opcode_is_mask(instruction.opcode) &&
            !assembly_x86_target_has_mask_feature(target, instruction.opcode))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                assembly_x86_mask_feature_name(instruction.opcode));
            return;
        }
        if (assembly_x86_opcode_is_amx(instruction.opcode) &&
            !assembly_x86_target_has_amx_feature(target, instruction.opcode))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                assembly_x86_amx_feature_name(instruction.opcode));
            return;
        }
        if ((assembly_x86_instruction_has_extended_gpr(instruction) || instruction.no_flags ||
             instruction.opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction.opcode == ASSEMBLY_OPCODE_X86_APX_POP2 ||
             (assembly_x86_opcode_is_apx_binary(instruction.opcode) && instruction.operand_count == 3)) &&
            !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_APX))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                S8("instruction requires the apx target feature"));
            return;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_x86_opcode_is_avx_integer(info.opcode) &&
        !instruction.evex && !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX2))
    {
        u8 requires_avx2 = false;
        for (u32 operand_index = 0; operand_index < instruction.operand_count; operand_index += 1)
        {
            requires_avx2 = requires_avx2 || (instruction.operands[operand_index].kind == ASSEMBLY_OPERAND_REGISTER &&
                                               instruction.operands[operand_index].reg.class == ASSEMBLY_REGISTER_YMM);
        }
        if (requires_avx2)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                S8("256-bit packed integer instruction requires the avx2 target feature"));
            return;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_x86_opcode_is_legacy_packed(info.opcode) &&
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_SSE2))
    {
        u8 requires_sse2 = false;
        for (u32 operand_index = 0; operand_index < instruction.operand_count; operand_index += 1)
        {
            requires_sse2 = requires_sse2 || (instruction.operands[operand_index].kind == ASSEMBLY_OPERAND_REGISTER &&
                                               instruction.operands[operand_index].reg.class == ASSEMBLY_REGISTER_XMM);
        }
        if (requires_sse2)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                S8("XMM packed integer instruction requires the sse2 target feature"));
            return;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (!assembly_x86_instruction_size(&instruction) || (info.suffix_width && instruction.width && info.suffix_width != instruction.width))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("unsupported x86 operand form"));
            return;
        }
    }
    else
    {
        instruction.size = 4;
        if (instruction.operand_count && instruction.operands[0].kind != ASSEMBLY_OPERAND_EXPRESSION)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid AArch64 operand form"));
            return;
        }
    }
    builder->instructions[builder->instruction_count++] = instruction;
}

BUSTER_GLOBAL_LOCAL void assembly_source_parse(AssemblyBuilder* builder, String8 source, Target target, AssemblySyntax syntax)
{
    u64 source_cursor = 0;
    u64 output_offset = 0;
    u32 line = 1;
    while (source_cursor < source.length)
    {
        u64 line_end = source_cursor;
        while (line_end < source.length && source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 original = {.pointer = source.pointer + source_cursor, .length = line_end - source_cursor};
        u64 comment = original.length;
        for (u64 index = 0; index < original.length; index += 1)
        {
            if (original.pointer[index] == ';' || (syntax == ASSEMBLY_SYNTAX_ATT && original.pointer[index] == '#') ||
                (original.pointer[index] == '/' && index + 1 < original.length && original.pointer[index + 1] == '/'))
            {
                comment = index;
                break;
            }
        }
        String8 statement = assembly_trim((String8){.pointer = original.pointer, .length = comment});
        u32 column = statement.length ? (u32)(statement.pointer - original.pointer) + 1 : 1;
        if (statement.length)
        {
            u64 colon = string_first_code_unit(statement, ':');
            if (colon < statement.length)
            {
                String8 label = assembly_trim((String8){.pointer = statement.pointer, .length = colon});
                if (!assembly_identifier(label))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, line, column, (u32)label.length, S8("invalid label"));
                }
                else
                {
                    u32 symbol = assembly_symbol_intern(builder, label);
                    if (symbol == UINT32_MAX)
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, line, column, (u32)label.length,
                                            S8("too many symbols"));
                    }
                    else if (builder->result.symbols[symbol].defined)
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL, line, column, (u32)label.length,
                                            S8("duplicate label"));
                    }
                    else
                    {
                        builder->result.symbols[symbol].defined = true;
                        builder->result.symbols[symbol].offset = output_offset;
                    }
                }
                statement = assembly_trim((String8){.pointer = statement.pointer + colon + 1, .length = statement.length - colon - 1});
                column = statement.length ? (u32)(statement.pointer - original.pointer) + 1 : column;
            }
            if (statement.length)
            {
                u64 directive_end = 0;
                while (directive_end < statement.length && !assembly_space(statement.pointer[directive_end]))
                {
                    directive_end += 1;
                }
                String8 directive = string_slice(statement, 0, directive_end);
                String8 qualifier = assembly_trim(string_slice(statement, directive_end, statement.length));
                if (assembly_word_equal(directive, S8(".intel_syntax")))
                {
                    if (target.cpu_arch != CPU_ARCH_X86_64 || !assembly_word_equal(qualifier, S8("noprefix")))
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX, line, column, (u32)statement.length,
                                            S8(".intel_syntax requires the x86 'noprefix' qualifier"));
                    }
                    else
                    {
                        syntax = ASSEMBLY_SYNTAX_INTEL;
                    }
                }
                else if (assembly_word_equal(directive, S8(".att_syntax")))
                {
                    if (target.cpu_arch != CPU_ARCH_X86_64 || !assembly_word_equal(qualifier, S8("prefix")))
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX, line, column, (u32)statement.length,
                                            S8(".att_syntax requires the x86 'prefix' qualifier"));
                    }
                    else
                    {
                        syntax = ASSEMBLY_SYNTAX_ATT;
                    }
                }
                else
                {
                    u32 instruction_count = builder->instruction_count;
                    assembly_instruction_parse(builder, statement, line, column, output_offset, target, syntax);
                    if (builder->instruction_count != instruction_count)
                    {
                        output_offset += builder->instructions[builder->instruction_count - 1].size;
                    }
                }
            }
        }
        source_cursor = line_end + (line_end < source.length);
        line += 1;
    }
    builder->output_capacity = output_offset;
}

BUSTER_GLOBAL_LOCAL void assembly_emit_u32(AssemblyBuilder* builder, u32 value)
{
    if (builder->output_count > builder->output_capacity || 4 > builder->output_capacity - builder->output_count)
    {
        return;
    }
    memcpy(builder->result.bytes.pointer + builder->output_count, &value, sizeof(value));
    builder->output_count += 4;
}

BUSTER_GLOBAL_LOCAL void assembly_emit_byte(AssemblyBuilder* builder, u8 value)
{
    if (builder->output_count < builder->output_capacity)
    {
        builder->result.bytes.pointer[builder->output_count++] = value;
    }
}

BUSTER_GLOBAL_LOCAL void assembly_emit_immediate(AssemblyBuilder* builder, u64 value, u32 byte_count)
{
    for (u32 byte_index = 0; byte_index < byte_count; byte_index += 1)
    {
        assembly_emit_byte(builder, (u8)(value >> (byte_index * 8)));
    }
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_rex(AssemblyBuilder* builder, u16 width, AssemblyRegister reg, AssemblyRegister rm)
{
    if (assembly_x86_rex_needed(width, reg, rm))
    {
        u8 rex = UINT8_C(0x40) | (width == 64 ? UINT8_C(0x08) : 0) | (reg.index >= 8 ? UINT8_C(0x04) : 0) |
                 (rm.index >= 8 ? UINT8_C(0x01) : 0);
        assembly_emit_byte(builder, rex);
    }
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_memory_rex(AssemblyBuilder* builder, u16 width, AssemblyRegister reg, AssemblyMemory memory)
{
    if (assembly_x86_memory_rex_needed(width, reg, memory))
    {
        u8 rex = UINT8_C(0x40) | (width == 64 ? UINT8_C(0x08) : 0) | (reg.index >= 8 ? UINT8_C(0x04) : 0) |
                 (memory.has_index && memory.index.index >= 8 ? UINT8_C(0x02) : 0) |
                 (memory.has_base && memory.base.index >= 8 ? UINT8_C(0x01) : 0);
        assembly_emit_byte(builder, rex);
    }
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_prefix(AssemblyBuilder* builder, u16 width, AssemblyRegister reg, AssemblyRegister rm)
{
    if (width == 16)
    {
        assembly_emit_byte(builder, 0x66);
    }
    assembly_x86_emit_rex(builder, width, reg, rm);
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_memory_prefix(AssemblyBuilder* builder, u16 width, AssemblyRegister reg, AssemblyMemory memory)
{
    if (width == 16)
    {
        assembly_emit_byte(builder, 0x66);
    }
    assembly_x86_emit_memory_rex(builder, width, reg, memory);
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_modrm(AssemblyBuilder* builder, u8 reg, u8 rm)
{
    assembly_emit_byte(builder, (u8)(UINT8_C(0xc0) | ((reg & 7) << 3) | (rm & 7)));
}

BUSTER_GLOBAL_LOCAL bool assembly_expression_target(AssemblyBuilder* builder, AssemblyExpression expression, s64* target)
{
    if (!expression.has_symbol)
    {
        *target = expression.addend;
        return true;
    }
    AssemblySymbol* symbol = builder->result.symbols + expression.symbol;
    if (!symbol->defined)
    {
        return false;
    }
    if (symbol->offset > (u64)INT64_MAX ||
        (expression.addend > 0 && symbol->offset > (u64)(INT64_MAX - expression.addend)))
    {
        *target = INT64_MAX;
        return true;
    }
    s64 value = (s64)symbol->offset;
    if (expression.addend < 0 && value < INT64_MIN - expression.addend)
    {
        *target = INT64_MIN;
        return true;
    }
    *target = value + expression.addend;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_relocation_append(AssemblyBuilder* builder, u64 offset, AssemblyExpression expression,
                                                     AssemblyRelocationKind kind, s64 implicit_addend)
{
    if (!expression.has_symbol || builder->result.relocation_count >= builder->relocation_capacity)
    {
        return false;
    }
    if ((implicit_addend > 0 && expression.addend > INT64_MAX - implicit_addend) ||
        (implicit_addend < 0 && expression.addend < INT64_MIN - implicit_addend))
    {
        return false;
    }
    builder->result.relocations[builder->result.relocation_count++] = (AssemblyRelocation){
        .addend = expression.addend + implicit_addend,
        .offset = offset,
        .symbol = expression.symbol,
        .kind = kind,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_memory(AssemblyBuilder* builder, AssemblyInstruction* instruction, u8 reg, AssemblyMemory memory)
{
    u32 displacement_size = 0;
    if (!assembly_x86_memory_displacement_size(memory, &displacement_size))
    {
        return false;
    }
    u8 sib = !memory.rip_relative && (memory.has_index || !memory.has_base || (memory.base.index & 7) == 4);
    u8 mod = displacement_size == 0 ? 0 : displacement_size == 1 ? 1 : 2;
    if (memory.rip_relative || !memory.has_base)
    {
        mod = 0;
    }
    u8 rm = memory.rip_relative ? 5 : sib ? 4 : (u8)(memory.base.index & 7);
    assembly_emit_byte(builder, (u8)((mod << 6) | ((reg & 7) << 3) | rm));
    if (sib)
    {
        u8 scale = memory.scale == 8 ? 3 : memory.scale == 4 ? 2 : memory.scale == 2 ? 1 : 0;
        u8 index = memory.has_index ? (u8)(memory.index.index & 7) : 4;
        u8 base = memory.has_base ? (u8)(memory.base.index & 7) : 5;
        assembly_emit_byte(builder, (u8)((scale << 6) | (index << 3) | base));
    }
    if (displacement_size)
    {
        u64 relocation_offset = builder->output_count;
        s64 value = memory.displacement.addend;
        if (memory.displacement.has_symbol)
        {
            if (assembly_expression_target(builder, memory.displacement, &value))
            {
                if (memory.rip_relative)
                {
                    s64 next = (s64)(instruction->offset + instruction->size);
                    if (value < next + INT32_MIN || value > next + INT32_MAX)
                    {
                        return false;
                    }
                    value -= next;
                }
            }
            else if (!assembly_relocation_append(builder, relocation_offset, memory.displacement,
                                                  memory.rip_relative ? ASSEMBLY_RELOCATION_X86_PC32 : ASSEMBLY_RELOCATION_X86_32,
                                                  memory.rip_relative ? -4 - (s64)instruction->rip_relocation_trailing : 0))
            {
                return false;
            }
            else
            {
                value = 0;
            }
        }
        if (displacement_size == 1 && (value < INT8_MIN || value > INT8_MAX))
        {
            return false;
        }
        if (displacement_size == 4 && (value < INT32_MIN || value > INT32_MAX))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)value, displacement_size);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_rm(AssemblyBuilder* builder, AssemblyInstruction* instruction, u8 reg, AssemblyOperand operand)
{
    if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        assembly_x86_emit_modrm(builder, reg, operand.reg.index);
        return true;
    }
    return operand.kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_emit_memory(builder, instruction, reg, operand.memory);
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_sse_prefix(AssemblyBuilder* builder, AssemblyOpcode opcode)
{
    if (opcode == ASSEMBLY_OPCODE_X86_MOVAPD || opcode == ASSEMBLY_OPCODE_X86_MOVUPD || opcode == ASSEMBLY_OPCODE_X86_MOVDQA ||
        opcode == ASSEMBLY_OPCODE_X86_XORPD || opcode == ASSEMBLY_OPCODE_X86_PXOR || opcode == ASSEMBLY_OPCODE_X86_ADDPD ||
        opcode == ASSEMBLY_OPCODE_X86_SUBPD || opcode == ASSEMBLY_OPCODE_X86_MULPD || opcode == ASSEMBLY_OPCODE_X86_DIVPD)
    {
        assembly_emit_byte(builder, 0x66);
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_MOVDQU || opcode == ASSEMBLY_OPCODE_X86_ADDSS)
    {
        assembly_emit_byte(builder, 0xf3);
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_ADDSD)
    {
        assembly_emit_byte(builder, 0xf2);
    }
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_vex_prefix(AssemblyBuilder* builder, AssemblyRegister reg, AssemblyOperand rm,
                                                       AssemblyRegisterClass vector_class, u8 source, u8 prefix, u8 map)
{
    u8 three_byte = map != 1 || assembly_x86_vex_three_byte_needed(rm);
    u8 inverted_reg = reg.index >= 8 ? 0 : UINT8_C(0x80);
    u8 vector_length = vector_class == ASSEMBLY_REGISTER_YMM ? UINT8_C(0x04) : 0;
    u8 inverted_source = (u8)((~source & 15) << 3);
    if (!three_byte)
    {
        assembly_emit_byte(builder, 0xc5);
        assembly_emit_byte(builder, (u8)(inverted_reg | inverted_source | vector_length | prefix));
        return;
    }
    u8 inverted_index = rm.kind != ASSEMBLY_OPERAND_MEMORY || !rm.memory.has_index || rm.memory.index.index < 8 ? UINT8_C(0x40) : 0;
    u8 inverted_base = rm.kind == ASSEMBLY_OPERAND_REGISTER ? (rm.reg.index < 8 ? UINT8_C(0x20) : 0)
                       : !rm.memory.has_base || rm.memory.base.index < 8 ? UINT8_C(0x20)
                                                                        : 0;
    assembly_emit_byte(builder, 0xc4);
    assembly_emit_byte(builder, (u8)(inverted_reg | inverted_index | inverted_base | map));
    assembly_emit_byte(builder, (u8)(inverted_source | vector_length | prefix));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_evex_memory(AssemblyBuilder* builder, AssemblyInstruction* instruction, u8 reg,
                                                        AssemblyMemory memory, u32 tuple_scale)
{
    u32 displacement_size = 0;
    if (!assembly_x86_evex_memory_displacement_size(memory, tuple_scale, &displacement_size))
    {
        return false;
    }
    u8 sib = !memory.rip_relative && (memory.has_index || !memory.has_base || (memory.base.index & 7) == 4);
    u8 mod = displacement_size == 0 ? 0 : displacement_size == 1 ? 1 : 2;
    if (memory.rip_relative || !memory.has_base)
    {
        mod = 0;
    }
    u8 rm = memory.rip_relative ? 5 : sib ? 4 : (u8)(memory.base.index & 7);
    assembly_emit_byte(builder, (u8)((mod << 6) | ((reg & 7) << 3) | rm));
    if (sib)
    {
        u8 scale = memory.scale == 8 ? 3 : memory.scale == 4 ? 2 : memory.scale == 2 ? 1 : 0;
        u8 index = memory.has_index ? (u8)(memory.index.index & 7) : 4;
        u8 base = memory.has_base ? (u8)(memory.base.index & 7) : 5;
        assembly_emit_byte(builder, (u8)((scale << 6) | (index << 3) | base));
    }
    if (displacement_size)
    {
        u64 relocation_offset = builder->output_count;
        s64 value = memory.displacement.addend;
        if (memory.displacement.has_symbol)
        {
            if (assembly_expression_target(builder, memory.displacement, &value))
            {
                if (memory.rip_relative)
                {
                    s64 next = (s64)(instruction->offset + instruction->size);
                    if (value < next + INT32_MIN || value > next + INT32_MAX)
                    {
                        return false;
                    }
                    value -= next;
                }
            }
            else if (!assembly_relocation_append(builder, relocation_offset, memory.displacement,
                                                  memory.rip_relative ? ASSEMBLY_RELOCATION_X86_PC32 : ASSEMBLY_RELOCATION_X86_32,
                                                  memory.rip_relative ? -4 - (s64)instruction->rip_relocation_trailing : 0))
            {
                return false;
            }
            else
            {
                value = 0;
            }
        }
        if (displacement_size == 1)
        {
            if (!memory.displacement.has_symbol && tuple_scale > 1)
            {
                if (value % (s64)tuple_scale)
                {
                    return false;
                }
                value /= (s64)tuple_scale;
            }
            if (value < INT8_MIN || value > INT8_MAX)
            {
                return false;
            }
        }
        if (displacement_size == 4 && (value < INT32_MIN || value > INT32_MAX))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)value, displacement_size);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_evex_prefix(AssemblyBuilder* builder, AssemblyRegister reg, AssemblyOperand rm,
                                                        u16 width, u8 source, u8 pp, u8 map, bool w, u8 mask, bool zeroing,
                                                        bool broadcast, u8 rounding, bool sae)
{
    u8 p0 = (u8)(0xf0 | (map & 7));
    if (reg.index & 8)
    {
        p0 &= (u8)~0x80;
    }
    if (reg.index & 16)
    {
        p0 &= (u8)~0x10;
    }
    if (rm.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        if (rm.reg.index & 8)
        {
            p0 &= (u8)~0x20;
        }
        if (rm.reg.index & 16)
        {
            p0 &= (u8)~0x40;
        }
    }
    else if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (rm.memory.has_base && rm.memory.base.index & 8)
        {
            p0 &= (u8)~0x20;
        }
        if (rm.memory.has_index && rm.memory.index.index & 8)
        {
            p0 &= (u8)~0x40;
        }
    }
    u8 p1 = (u8)((w ? 0x80 : 0) | ((~source & 15) << 3) | 0x04 | (pp & 3));
    if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (rm.memory.has_base && rm.memory.base.index & 16)
        {
            p0 |= 0x08;
        }
        if (rm.memory.has_index && rm.memory.index.index & 16)
        {
            p1 &= (u8)~0x04;
        }
    }
    u8 p2 = (u8)((zeroing ? 0x80 : 0) | (width == 512 ? 0x40 : width == 256 ? 0x20 : 0) |
                 ((broadcast || rounding || sae) ? 0x10 : 0) | (source < 16 ? 0x08 : 0) | (mask & 7));
    if (rounding)
    {
        p2 = (u8)((p2 & (u8)~0x60) | ((rounding - 1) << 5));
    }
    assembly_emit_byte(builder, 0x62);
    assembly_emit_byte(builder, p0);
    assembly_emit_byte(builder, p1);
    assembly_emit_byte(builder, p2);
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_vector_opcode_byte(AssemblyVectorForm const* form, bool load)
{
    if (!load && (form->flags & ASSEMBLY_VECTOR_FORM_MOVE))
    {
        return form->opcode_byte == 0x6f ? 0x7f : (u8)(form->opcode_byte + 1);
    }
    return form->opcode_byte;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_evex_vector(AssemblyBuilder* builder, AssemblyInstruction* instruction,
                                                        AssemblyVectorForm const* form)
{
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    AssemblyOperand* third = instruction->operands + 2;
    u8 move = (form->flags & ASSEMBLY_VECTOR_FORM_MOVE) != 0;
    u8 mask_destination = (form->flags & ASSEMBLY_VECTOR_FORM_MASK_DESTINATION) != 0;
    u8 source_rm = (form->flags & ASSEMBLY_VECTOR_FORM_SOURCE_RM) != 0;
    AssemblyRegister vector = {0};
    AssemblyRegister prefix_reg = {0};
    AssemblyOperand rm = {0};
    u8 source = 0;
    u8 load = true;
    if (mask_destination)
    {
        prefix_reg = first->reg;
        vector = second->reg;
        rm = *third;
        source = second->reg.index;
    }
    else if (source_rm)
    {
        prefix_reg = first->reg;
        vector = first->reg;
        rm = *second;
    }
    else if (move)
    {
        load = first->kind == ASSEMBLY_OPERAND_REGISTER;
        vector = load ? first->reg : second->reg;
        prefix_reg = vector;
        rm = load ? *second : *first;
    }
    else
    {
        prefix_reg = first->reg;
        vector = first->reg;
        rm = *third;
        source = second->reg.index;
    }
    u8 broadcast = rm.kind == ASSEMBLY_OPERAND_MEMORY && rm.broadcast;
    u8 mask = !mask_destination && first->has_mask ? first->mask : 0;
    u8 zeroing = !mask_destination && first->zeroing;
    u8 rounding = !mask_destination ? first->rounding : 0;
    u8 sae = !mask_destination && first->sae;
    u32 tuple_scale = rm.kind == ASSEMBLY_OPERAND_MEMORY
                          ? (broadcast ? form->element_width
                                       : (form->flags & ASSEMBLY_VECTOR_FORM_SCALAR ? form->element_width : vector.width / 8u))
                          : 0;
    assembly_x86_emit_evex_prefix(builder, prefix_reg, rm, vector.width, source, form->mandatory_prefix, form->map,
                                  assembly_x86_vector_form_w(form->opcode), mask, zeroing, broadcast, rounding, sae);
    assembly_emit_byte(builder, assembly_x86_vector_opcode_byte(form, load));
    if (rm.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        assembly_x86_emit_modrm(builder, prefix_reg.index, rm.reg.index);
    }
    else if (rm.kind == ASSEMBLY_OPERAND_MEMORY &&
             !assembly_x86_emit_evex_memory(builder, instruction, prefix_reg.index, rm.memory, tuple_scale))
    {
        return false;
    }
    if (form->flags & ASSEMBLY_VECTOR_FORM_IMMEDIATE)
    {
        u32 immediate_index = mask_destination ? 3u : 2u;
        assembly_emit_byte(builder, (u8)instruction->operands[immediate_index].expression.addend);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_mask(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyRegister destination = instruction->operands[0].reg;
    AssemblyOperand source = instruction->operands[1];
    u8 prefix[3] = {0};
    u8 prefix_count = 2;
    u8 operation = 0;
    u8 source_index = 0;
    if (opcode == ASSEMBLY_OPCODE_X86_KMOVW)
    {
        prefix[0] = 0xc5;
        prefix[1] = 0xf8;
        operation = 0x90;
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_KMOVD || opcode == ASSEMBLY_OPCODE_X86_KMOVQ)
    {
        prefix[0] = 0xc4;
        prefix[1] = 0xe1;
        prefix[2] = opcode == ASSEMBLY_OPCODE_X86_KMOVD ? 0xf9 : 0xf8;
        prefix_count = 3;
        operation = 0x90;
    }
    else
    {
        prefix[0] = 0xc5;
        prefix[1] = 0xf8;
        operation = opcode == ASSEMBLY_OPCODE_X86_KNOTW ? 0x44
                    : opcode == ASSEMBLY_OPCODE_X86_KORTESTW ? 0x98
                    : opcode == ASSEMBLY_OPCODE_X86_KADDW ? 0x4a
                    : opcode == ASSEMBLY_OPCODE_X86_KANDW ? 0x41
                    : opcode == ASSEMBLY_OPCODE_X86_KORW ? 0x45
                                                        : 0x47;
        if (instruction->operand_count == 3)
        {
            source_index = instruction->operands[1].reg.index;
            prefix[1] = (u8)(0x80 | 0x04 | ((~source_index & 15) << 3));
            source = instruction->operands[2];
        }
    }
    for (u8 index = 0; index < prefix_count; index += 1)
    {
        assembly_emit_byte(builder, prefix[index]);
    }
    assembly_emit_byte(builder, operation);
    assembly_x86_emit_modrm(builder, destination.index, source.reg.index);
    return true;
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_apx_memory_rex2_byte(u16 width, AssemblyRegister reg, AssemblyMemory memory)
{
    u8 result = (u8)((width == 64 ? 0x08 : 0) | (reg.index & 8 ? 0x04 : 0) | (reg.index & 16 ? 0x40 : 0));
    if (memory.has_base)
    {
        result |= (u8)(memory.base.index & 8 ? 0x01 : 0);
        result |= (u8)(memory.base.index & 16 ? 0x10 : 0);
    }
    if (memory.has_index)
    {
        result |= (u8)(memory.index.index & 8 ? 0x02 : 0);
        result |= (u8)(memory.index.index & 16 ? 0x20 : 0);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_apx_rex2(AssemblyBuilder* builder, u16 width, AssemblyRegister reg,
                                                     AssemblyRegister rm)
{
    assembly_emit_byte(builder, 0xd5);
    assembly_emit_byte(builder, assembly_x86_rex2_byte(width, reg, rm));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_apx_legacy(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyOperand first = instruction->operands[0];
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_PUSH || instruction->opcode == ASSEMBLY_OPCODE_X86_POP)
    {
        assembly_x86_emit_apx_rex2(builder, 0, (AssemblyRegister){0}, first.reg);
        assembly_emit_byte(builder, (u8)((instruction->opcode == ASSEMBLY_OPCODE_X86_PUSH ? 0x50 : 0x58) +
                                         (first.reg.index & 7)));
        return true;
    }
    AssemblyOperand second = instruction->operands[1];
    u8 load = second.kind == ASSEMBLY_OPERAND_MEMORY;
    AssemblyRegister reg = load ? first.reg : second.reg;
    AssemblyOperand rm = load ? second : first;
    if (instruction->width == 16)
    {
        assembly_emit_byte(builder, 0x66);
    }
    if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        assembly_emit_byte(builder, 0xd5);
        assembly_emit_byte(builder, assembly_x86_apx_memory_rex2_byte(instruction->width, reg, rm.memory));
    }
    else
    {
        assembly_x86_emit_apx_rex2(builder, instruction->width, reg, rm.reg);
    }
    u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_MOV ? 0x89
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_ADD ? 0x01
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_OR ? 0x09
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 0x21
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 0x29
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_XOR ? 0x31
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_CMP ? 0x39
                                                                     : 0x85;
    if (instruction->width == 8)
    {
        operation -= 1;
    }
    if (load && instruction->opcode != ASSEMBLY_OPCODE_X86_TEST)
    {
        operation += 2;
    }
    assembly_emit_byte(builder, operation);
    return rm.kind == ASSEMBLY_OPERAND_MEMORY ? assembly_x86_emit_memory(builder, instruction, reg.index, rm.memory)
                                               : (assembly_x86_emit_modrm(builder, reg.index, rm.reg.index), true);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_apx_nf(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyOperand first = instruction->operands[0];
    AssemblyOperand second = instruction->operands[1];
    u8 load = second.kind == ASSEMBLY_OPERAND_MEMORY;
    AssemblyRegister reg = load ? first.reg : second.reg;
    AssemblyOperand rm = load ? second : first;
    u8 p0 = 0xf4;
    if (reg.index & 8)
    {
        p0 &= (u8)~0x80;
    }
    if (reg.index & 16)
    {
        p0 &= (u8)~0x10;
    }
    if (rm.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        if (rm.reg.index & 8)
        {
            p0 &= (u8)~0x20;
        }
        if (rm.reg.index & 16)
        {
            p0 |= 0x08;
        }
    }
    else
    {
        if (rm.memory.has_base && rm.memory.base.index & 8)
        {
            p0 &= (u8)~0x20;
        }
        if (rm.memory.has_base && rm.memory.base.index & 16)
        {
            p0 |= 0x08;
        }
        if (rm.memory.has_index && rm.memory.index.index & 8)
        {
            p0 &= (u8)~0x40;
        }
    }
    u8 p1 = (u8)((instruction->width == 64 ? 0x80 : 0) | (instruction->width == 16 ? 0x01 : 0) | 0x7c);
    if (rm.kind == ASSEMBLY_OPERAND_MEMORY && rm.memory.has_index && rm.memory.index.index & 16)
    {
        p1 &= (u8)~0x04;
    }
    assembly_emit_byte(builder, 0x62);
    assembly_emit_byte(builder, p0);
    assembly_emit_byte(builder, p1);
    assembly_emit_byte(builder, 0x0c);
    u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_ADD ? 0x01
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_OR ? 0x09
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 0x21
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 0x29
                                                                  : 0x31;
    if (instruction->width == 8)
    {
        operation -= 1;
    }
    if (load)
    {
        operation += 2;
    }
    assembly_emit_byte(builder, operation);
    return rm.kind == ASSEMBLY_OPERAND_MEMORY ? assembly_x86_emit_memory(builder, instruction, reg.index, rm.memory)
                                               : (assembly_x86_emit_modrm(builder, reg.index, rm.reg.index), true);
}

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_apx_ndd_prefix(AssemblyBuilder* builder, AssemblyRegister destination,
                                                            AssemblyRegister source_1, AssemblyRegister source_2,
                                                            u16 width, bool no_flags)
{
    u8 p0 = 0xf4;
    if (source_2.index & 8)
    {
        p0 &= (u8)~0x80;
    }
    if (source_2.index & 16)
    {
        p0 &= (u8)~0x10;
    }
    if (source_1.index & 8)
    {
        p0 &= (u8)~0x20;
    }
    if (source_1.index & 16)
    {
        p0 |= 0x08;
    }
    u8 p1 = (u8)((width == 64 ? 0x80 : 0) | (width == 16 ? 0x01 : 0) | ((~destination.index & 15) << 3) | 0x04);
    u8 p2 = (u8)((no_flags ? 0x80 : 0) | 0x10 | (destination.index < 16 ? 0x08 : 0));
    assembly_emit_byte(builder, 0x62);
    assembly_emit_byte(builder, p0);
    assembly_emit_byte(builder, p1);
    assembly_emit_byte(builder, p2);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_apx_ndd(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction->opcode == ASSEMBLY_OPCODE_X86_APX_POP2)
    {
        AssemblyRegister first = instruction->operands[0].reg;
        AssemblyRegister second = instruction->operands[1].reg;
        u8 p0 = 0xf4;
        if (second.index & 8)
        {
            p0 &= (u8)~0x20;
        }
        if (second.index & 16)
        {
            p0 |= 0x08;
        }
        u8 p1 = (u8)(0x04 | ((~first.index & 15) << 3));
        u8 p2 = (u8)(0x10 | (first.index < 16 ? 0x08 : 0));
        assembly_emit_byte(builder, 0x62);
        assembly_emit_byte(builder, p0);
        assembly_emit_byte(builder, p1);
        assembly_emit_byte(builder, p2);
        assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 ? 0xff : 0x8f);
        assembly_x86_emit_modrm(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 ? 6 : 0, second.index);
        return true;
    }
    AssemblyRegister destination = instruction->operands[0].reg;
    AssemblyRegister source_1 = instruction->operands[1].reg;
    AssemblyRegister source_2 = instruction->operands[2].reg;
    assembly_x86_emit_apx_ndd_prefix(builder, destination, source_1, source_2, instruction->width, instruction->no_flags);
    u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_ADD ? 0x01
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_OR ? 0x09
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 0x21
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 0x29
                   : 0x31;
    if (instruction->width == 8)
    {
        operation -= 1;
    }
    assembly_emit_byte(builder, operation);
    assembly_x86_emit_modrm(builder, source_2.index, source_1.index);
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_amx_memory(AssemblyBuilder* builder, AssemblyInstruction* instruction, u8 reg,
                                                       AssemblyMemory memory, bool forced_sib)
{
    if (forced_sib && !memory.has_index && memory.has_base && (memory.base.index & 7) != 4)
    {
        memory.has_index = true;
        memory.index = (AssemblyRegister){.index = 4, .width = 64, .class = ASSEMBLY_REGISTER_GPR};
    }
    return assembly_x86_emit_memory(builder, instruction, reg, memory);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_amx(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyOperand first = instruction->operands[0];
    AssemblyOperand second = instruction->operands[1];
    u8 prefix = opcode == ASSEMBLY_OPCODE_X86_STTILECFG ? 1
                : opcode == ASSEMBLY_OPCODE_X86_TILELOADD ? 3
                : opcode == ASSEMBLY_OPCODE_X86_TILELOADDT1 ? 1
                : opcode == ASSEMBLY_OPCODE_X86_TILESTORED ? 2
                : opcode == ASSEMBLY_OPCODE_X86_TILEZERO ? 3
                : opcode == ASSEMBLY_OPCODE_X86_TDPBF16PS ? 2
                : opcode == ASSEMBLY_OPCODE_X86_TDPBSSD ? 3
                : opcode == ASSEMBLY_OPCODE_X86_TDPBSUD ? 2
                : opcode == ASSEMBLY_OPCODE_X86_TDPBUSD ? 1
                                                        : 0;
    u8 operation = opcode == ASSEMBLY_OPCODE_X86_LDTILECFG || opcode == ASSEMBLY_OPCODE_X86_STTILECFG ? 0x49
                   : opcode == ASSEMBLY_OPCODE_X86_TILELOADD || opcode == ASSEMBLY_OPCODE_X86_TILELOADDT1 ||
                           opcode == ASSEMBLY_OPCODE_X86_TILESTORED
                       ? 0x4b
                   : opcode == ASSEMBLY_OPCODE_X86_TILEZERO ? 0x49
                                                            : opcode == ASSEMBLY_OPCODE_X86_TDPBF16PS ? 0x5c : 0x5e;
    AssemblyRegister reg = {0};
    AssemblyOperand rm = {0};
    u8 source = 0;
    u8 forced_sib = false;
    if (opcode == ASSEMBLY_OPCODE_X86_LDTILECFG || opcode == ASSEMBLY_OPCODE_X86_STTILECFG)
    {
        rm = first;
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_TILELOADD || opcode == ASSEMBLY_OPCODE_X86_TILELOADDT1)
    {
        reg = first.reg;
        rm = second;
        forced_sib = true;
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_TILESTORED)
    {
        reg = second.reg;
        rm = first;
        forced_sib = true;
    }
    else if (opcode == ASSEMBLY_OPCODE_X86_TILEZERO)
    {
        reg = first.reg;
        rm = (AssemblyOperand){.kind = ASSEMBLY_OPERAND_REGISTER,
                               .reg = {.index = 0, .width = 0, .class = ASSEMBLY_REGISTER_TILE}};
    }
    else
    {
        reg = first.reg;
        rm = second;
        source = instruction->operands[2].reg.index;
    }
    u8 apx_evex = rm.kind == ASSEMBLY_OPERAND_MEMORY &&
                  ((rm.memory.has_base && rm.memory.base.index >= 16) ||
                   (rm.memory.has_index && rm.memory.index.index >= 16));
    if (apx_evex)
    {
        assembly_x86_emit_evex_prefix(builder, reg, rm, 128, source, prefix, 2, false, 0, false, false, 0, false);
    }
    else
    {
        assembly_x86_emit_vex_prefix(builder, reg, rm, ASSEMBLY_REGISTER_TILE, source, prefix, 2);
    }
    assembly_emit_byte(builder, operation);
    if (rm.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        assembly_x86_emit_modrm(builder, reg.index, rm.reg.index);
        return true;
    }
    return assembly_x86_emit_amx_memory(builder, instruction, reg.index, rm.memory, forced_sib);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_bit_atomic(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyOpcode opcode = instruction->opcode;
    AssemblyOperand first = instruction->operands[0];
    AssemblyOperand second = instruction->operands[1];
    if (opcode == ASSEMBLY_OPCODE_X86_BSF || opcode == ASSEMBLY_OPCODE_X86_BSR || opcode == ASSEMBLY_OPCODE_X86_POPCNT ||
        opcode == ASSEMBLY_OPCODE_X86_LZCNT || opcode == ASSEMBLY_OPCODE_X86_TZCNT)
    {
        u8 mandatory = opcode == ASSEMBLY_OPCODE_X86_POPCNT || opcode == ASSEMBLY_OPCODE_X86_LZCNT || opcode == ASSEMBLY_OPCODE_X86_TZCNT;
        u8 operation = opcode == ASSEMBLY_OPCODE_X86_BSF || opcode == ASSEMBLY_OPCODE_X86_TZCNT ? 0xbc
                       : opcode == ASSEMBLY_OPCODE_X86_BSR || opcode == ASSEMBLY_OPCODE_X86_LZCNT ? 0xbd
                                                                                                  : 0xb8;
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        if (mandatory)
        {
            assembly_emit_byte(builder, 0xf3);
        }
        if (second.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_x86_emit_memory_rex(builder, instruction->width, first.reg, second.memory);
        }
        else
        {
            assembly_x86_emit_rex(builder, instruction->width, first.reg, second.reg);
        }
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, operation);
        if (!assembly_x86_emit_rm(builder, instruction, first.reg.index, second))
        {
            return false;
        }
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BSWAP)
    {
        assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, (u8)(0xc8 + (first.reg.index & 7)));
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_BT || opcode == ASSEMBLY_OPCODE_X86_BTC || opcode == ASSEMBLY_OPCODE_X86_BTR ||
        opcode == ASSEMBLY_OPCODE_X86_BTS)
    {
        u8 immediate = second.kind == ASSEMBLY_OPERAND_EXPRESSION;
        u8 extension = opcode == ASSEMBLY_OPCODE_X86_BT   ? 4
                       : opcode == ASSEMBLY_OPCODE_X86_BTS ? 5
                       : opcode == ASSEMBLY_OPCODE_X86_BTR ? 6
                                                           : 7;
        u8 reg = immediate ? extension : second.reg.index;
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){.index = reg}, first.memory);
        }
        else
        {
            assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){.index = reg}, first.reg);
        }
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, immediate ? 0xba
                                              : opcode == ASSEMBLY_OPCODE_X86_BT   ? 0xa3
                                                : opcode == ASSEMBLY_OPCODE_X86_BTC ? 0xbb
                                                : opcode == ASSEMBLY_OPCODE_X86_BTR ? 0xb3
                                                                                    : 0xab);
        if (!assembly_x86_emit_rm(builder, instruction, reg, first))
        {
            return false;
        }
        if (immediate)
        {
            assembly_emit_byte(builder, (u8)second.expression.addend);
        }
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_XCHG)
    {
        u8 memory = first.kind == ASSEMBLY_OPERAND_MEMORY || second.kind == ASSEMBLY_OPERAND_MEMORY;
        if (!memory && instruction->width != 8 && (first.reg.index == 0 || second.reg.index == 0))
        {
            AssemblyRegister other = first.reg.index == 0 ? second.reg : first.reg;
            assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, other);
            assembly_emit_byte(builder, (u8)(0x90 + (other.index & 7)));
            return true;
        }
        AssemblyRegister reg = first.kind == ASSEMBLY_OPERAND_MEMORY ? second.reg : first.reg;
        AssemblyOperand rm = first.kind == ASSEMBLY_OPERAND_MEMORY ? first : second;
        if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_x86_emit_memory_prefix(builder, instruction->width, reg, rm.memory);
        }
        else
        {
            assembly_x86_emit_prefix(builder, instruction->width, reg, rm.reg);
        }
        assembly_emit_byte(builder, instruction->width == 8 ? 0x86 : 0x87);
        return assembly_x86_emit_rm(builder, instruction, reg.index, rm);
    }
    if (opcode == ASSEMBLY_OPCODE_X86_XADD || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG)
    {
        AssemblyRegister reg = second.reg;
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_x86_emit_memory_prefix(builder, instruction->width, reg, first.memory);
        }
        else
        {
            assembly_x86_emit_prefix(builder, instruction->width, reg, first.reg);
        }
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, opcode == ASSEMBLY_OPCODE_X86_XADD ? (instruction->width == 8 ? 0xc0 : 0xc1)
                                                                        : (instruction->width == 8 ? 0xb0 : 0xb1));
        return assembly_x86_emit_rm(builder, instruction, reg.index, first);
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CMPXCHG8B || opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B)
    {
        assembly_x86_emit_memory_prefix(builder, opcode == ASSEMBLY_OPCODE_X86_CMPXCHG16B ? 64 : 0,
                                         (AssemblyRegister){.index = 1}, first.memory);
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, 0xc7);
        return assembly_x86_emit_rm(builder, instruction, 1, first);
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void assembly_instructions_emit(AssemblyBuilder* builder)
{
    for (u32 instruction_index = 0; instruction_index < builder->instruction_count; instruction_index += 1)
    {
        AssemblyInstruction* instruction = builder->instructions + instruction_index;
        if (builder->output_count != instruction->offset)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, instruction->line, instruction->column, 1,
                                S8("internal instruction offset mismatch"));
            return;
        }
        if (instruction->lock_prefix)
        {
            assembly_emit_byte(builder, 0xf0);
        }
        if (assembly_x86_opcode_is_amx(instruction->opcode))
        {
            if (!assembly_x86_emit_amx(builder, instruction))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("AMX memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_mask(instruction->opcode))
        {
            assembly_x86_emit_mask(builder, instruction);
            continue;
        }
        if (instruction->no_flags && assembly_x86_opcode_is_apx_binary(instruction->opcode) && instruction->operand_count == 2)
        {
            if (!assembly_x86_emit_apx_nf(builder, instruction))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("APX no-flags memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction->opcode == ASSEMBLY_OPCODE_X86_APX_POP2 ||
            (assembly_x86_opcode_is_apx_binary(instruction->opcode) && instruction->operand_count == 3))
        {
            assembly_x86_emit_apx_ndd(builder, instruction);
            continue;
        }
        AssemblyVectorForm const* evex_form = assembly_x86_vector_form(instruction->opcode);
        if (instruction->evex && evex_form)
        {
            if (!assembly_x86_emit_evex_vector(builder, instruction, evex_form))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("EVEX memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_instruction_has_extended_gpr(*instruction))
        {
            if (!assembly_x86_emit_apx_legacy(builder, instruction))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("APX memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_NOP || instruction->opcode == ASSEMBLY_OPCODE_X86_RET ||
            instruction->opcode == ASSEMBLY_OPCODE_X86_INT3)
        {
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_NOP ? 0x90
                                        : instruction->opcode == ASSEMBLY_OPCODE_X86_RET ? 0xc3
                                                                                       : 0xcc);
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_CALL || instruction->opcode == ASSEMBLY_OPCODE_X86_JMP)
        {
            AssemblyOperand operand = instruction->operands[0];
            if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
            {
                assembly_x86_emit_prefix(builder, 0, (AssemblyRegister){0}, operand.reg);
                assembly_emit_byte(builder, 0xff);
                assembly_x86_emit_modrm(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_CALL ? 2 : 4, operand.reg.index);
                continue;
            }
            if (operand.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, operand.memory);
                assembly_emit_byte(builder, 0xff);
                if (!assembly_x86_emit_memory(builder, instruction, instruction->opcode == ASSEMBLY_OPCODE_X86_CALL ? 2 : 4, operand.memory))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x86 memory displacement is out of range"));
                    return;
                }
                continue;
            }
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_CALL ? 0xe8 : 0xe9);
            s64 target = 0;
            u32 displacement = 0;
            if (assembly_expression_target(builder, operand.expression, &target))
            {
                s64 next = (s64)instruction->offset + 5;
                if (target < next + INT32_MIN || target > next + INT32_MAX)
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("x86 branch target is out of range"));
                }
                else
                {
                    displacement = (u32)(s32)(target - next);
                }
            }
            else if (!assembly_relocation_append(builder, instruction->offset + 1, operand.expression,
                                                 ASSEMBLY_RELOCATION_X86_PC32, -4))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                    S8("x86 relocation addend is out of range"));
            }
            assembly_emit_u32(builder, displacement);
            continue;
        }
        if (assembly_x86_opcode_is_bit_atomic(instruction->opcode))
        {
            if (!assembly_x86_emit_bit_atomic(builder, instruction))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_LEA)
        {
            AssemblyRegister destination = instruction->operands[0].reg;
            AssemblyMemory memory = instruction->operands[1].memory;
            assembly_x86_emit_memory_prefix(builder, instruction->width, destination, memory);
            assembly_emit_byte(builder, 0x8d);
            if (!assembly_x86_emit_memory(builder, instruction, destination.index, memory))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_MOVZX || instruction->opcode == ASSEMBLY_OPCODE_X86_MOVSX ||
            instruction->opcode == ASSEMBLY_OPCODE_X86_MOVSXD)
        {
            AssemblyRegister destination = instruction->operands[0].reg;
            AssemblyOperand source = instruction->operands[1];
            u16 source_width = source.kind == ASSEMBLY_OPERAND_MEMORY ? source.memory.width : source.reg.width;
            if (source.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, instruction->width, destination, source.memory);
            }
            else
            {
                if (instruction->width == 16)
                {
                    assembly_emit_byte(builder, 0x66);
                }
                if (assembly_x86_extension_rex_needed(instruction->width, destination, source.reg, source_width))
                {
                    u8 rex = UINT8_C(0x40) | (instruction->width == 64 ? UINT8_C(0x08) : 0) |
                              (destination.index >= 8 ? UINT8_C(0x04) : 0) | (source.reg.index >= 8 ? UINT8_C(0x01) : 0);
                    assembly_emit_byte(builder, rex);
                }
            }
            if (instruction->opcode != ASSEMBLY_OPCODE_X86_MOVSXD)
            {
                assembly_emit_byte(builder, 0x0f);
            }
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_MOVSXD ? 0x63
                                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVSX ? (source_width == 16 ? 0xbf : 0xbe)
                                                                                           : (source_width == 16 ? 0xb7 : 0xb6));
            if (!assembly_x86_emit_rm(builder, instruction, destination.index, source))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_rotate(instruction->opcode))
        {
            AssemblyOperand destination = instruction->operands[0];
            AssemblyOperand count = instruction->operands[1];
            u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_ROL   ? 0
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_ROR ? 1
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_RCL ? 2
                                                                            : 3;
            u8 immediate = count.kind == ASSEMBLY_OPERAND_EXPRESSION;
            u8 one = immediate && count.expression.addend == 1;
            if (destination.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){0}, destination.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, destination.reg);
            }
            assembly_emit_byte(builder, one ? (instruction->width == 8 ? 0xd0 : 0xd1)
                                             : !immediate ? (instruction->width == 8 ? 0xd2 : 0xd3)
                                                          : (instruction->width == 8 ? 0xc0 : 0xc1));
            if (!assembly_x86_emit_rm(builder, instruction, extension, destination))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            if (immediate && !one)
            {
                assembly_emit_byte(builder, (u8)count.expression.addend);
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_SHLD || instruction->opcode == ASSEMBLY_OPCODE_X86_SHRD)
        {
            AssemblyOperand destination = instruction->operands[0];
            AssemblyRegister source = instruction->operands[1].reg;
            AssemblyOperand count = instruction->operands[2];
            u8 immediate = count.kind == ASSEMBLY_OPERAND_EXPRESSION;
            if (destination.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, instruction->width, source, destination.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, instruction->width, source, destination.reg);
            }
            assembly_emit_byte(builder, 0x0f);
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_SHLD ? (immediate ? 0xa4 : 0xa5)
                                                                                           : (immediate ? 0xac : 0xad));
            if (!assembly_x86_emit_rm(builder, instruction, source.index, destination))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            if (immediate)
            {
                assembly_emit_byte(builder, (u8)count.expression.addend);
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_CBW || instruction->opcode == ASSEMBLY_OPCODE_X86_CWDE ||
            instruction->opcode == ASSEMBLY_OPCODE_X86_CDQE || instruction->opcode == ASSEMBLY_OPCODE_X86_CWD ||
            instruction->opcode == ASSEMBLY_OPCODE_X86_CDQ || instruction->opcode == ASSEMBLY_OPCODE_X86_CQO)
        {
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_CBW || instruction->opcode == ASSEMBLY_OPCODE_X86_CWD)
            {
                assembly_emit_byte(builder, 0x66);
            }
            else if (instruction->opcode == ASSEMBLY_OPCODE_X86_CDQE || instruction->opcode == ASSEMBLY_OPCODE_X86_CQO)
            {
                assembly_emit_byte(builder, 0x48);
            }
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_CWD ||
                                           instruction->opcode == ASSEMBLY_OPCODE_X86_CDQ ||
                                           instruction->opcode == ASSEMBLY_OPCODE_X86_CQO
                                       ? 0x99
                                       : 0x98);
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_EMMS)
        {
            assembly_emit_byte(builder, 0x0f);
            assembly_emit_byte(builder, 0x77);
            continue;
        }
        if (assembly_x86_opcode_is_x87_zero_operand(instruction->opcode))
        {
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FWAIT)
            {
                assembly_emit_byte(builder, 0x9b);
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FINIT || instruction->opcode == ASSEMBLY_OPCODE_X86_FCLEX)
            {
                assembly_emit_byte(builder, 0x9b);
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FINIT || instruction->opcode == ASSEMBLY_OPCODE_X86_FNINIT ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_FCLEX || instruction->opcode == ASSEMBLY_OPCODE_X86_FNCLEX)
            {
                assembly_emit_byte(builder, 0xdb);
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FINIT ||
                                                   instruction->opcode == ASSEMBLY_OPCODE_X86_FNINIT
                                               ? 0xe3
                                               : 0xe2);
                continue;
            }
            assembly_emit_byte(builder, 0xd9);
            u8 second_byte = instruction->opcode == ASSEMBLY_OPCODE_X86_F2XM1 ? 0xf0
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FABS ? 0xe1
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FCHS ? 0xe0
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLD1 ? 0xe8
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDZ ? 0xee
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDPI ? 0xeb
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDL2E ? 0xea
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDL2T ? 0xe9
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDLG2 ? 0xec
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDLN2 ? 0xed
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FSQRT ? 0xfa
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FSIN ? 0xfe
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FCOS ? 0xff
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FSINCOS ? 0xfb
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FPTAN ? 0xf2
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FPATAN ? 0xf3
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FYL2X ? 0xf1
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FYL2XP1 ? 0xf9
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FRNDINT ? 0xfc
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FSCALE ? 0xfd
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FPREM ? 0xf8
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FPREM1 ? 0xf5
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FXTRACT ? 0xf4
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FTST ? 0xe4
                             : instruction->opcode == ASSEMBLY_OPCODE_X86_FXAM ? 0xe5
                                                                              : 0xd0;
            assembly_emit_byte(builder, second_byte);
            continue;
        }
        if (assembly_x86_opcode_is_x87_data(instruction->opcode))
        {
            AssemblyOperand operand = instruction->operands[0];
            if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
            {
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FLD ? 0xd9 : 0xdd);
                assembly_emit_byte(builder, (u8)((instruction->opcode == ASSEMBLY_OPCODE_X86_FLD ? 0xc0
                                                 : instruction->opcode == ASSEMBLY_OPCODE_X86_FST ? 0xd0
                                                                                                  : 0xd8) +
                                                operand.reg.index));
                continue;
            }
            assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, operand.memory);
            u8 primary = instruction->opcode == ASSEMBLY_OPCODE_X86_FLD
                             ? (instruction->width == 32 ? 0xd9 : instruction->width == 64 ? 0xdd : 0xdb)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FST
                             ? (instruction->width == 32 ? 0xd9 : 0xdd)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FSTP
                             ? (instruction->width == 32 ? 0xd9 : instruction->width == 64 ? 0xdd : 0xdb)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FILD
                             ? (instruction->width == 32 ? 0xdb : 0xdf)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FIST
                             ? (instruction->width == 32 ? 0xdb : 0xdf)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FISTP
                             ? (instruction->width == 32 ? 0xdb : 0xdf)
                             : (instruction->width == 16 ? 0xdf : instruction->width == 32 ? 0xdb : 0xdd);
            u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_FLD ? (instruction->width == 80 ? 5 : 0)
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FST ? 2
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FSTP ? (instruction->width == 80 ? 7 : 3)
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FILD ? (instruction->width == 64 ? 5 : 0)
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FIST ? 2
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FISTP ? (instruction->width == 64 ? 7 : 3)
                                                                         : 1;
            assembly_emit_byte(builder, primary);
            if (!assembly_x86_emit_memory(builder, instruction, group, operand.memory))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x87 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_x87_arithmetic(instruction->opcode))
        {
            if (instruction->operand_count == 1)
            {
                AssemblyMemory memory = instruction->operands[0].memory;
                assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, memory);
                assembly_emit_byte(builder, instruction->width == 32 ? 0xd8 : 0xdc);
                u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_FADD ? 0
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_FMUL ? 1
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUB ? 4
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBR ? 5
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIV ? 6
                                                                            : 7;
                if (!assembly_x86_emit_memory(builder, instruction, group, memory))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x87 memory displacement is out of range"));
                    return;
                }
                continue;
            }
            AssemblyRegister destination = instruction->operands[0].reg;
            AssemblyRegister source = instruction->operands[1].reg;
            u8 destination_is_top = destination.index == 0;
            u8 index = destination_is_top ? source.index : destination.index;
            assembly_emit_byte(builder, destination_is_top ? 0xd8 : 0xdc);
            u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_FADD ? 0xc0
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FMUL ? 0xc8
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUB ? (destination_is_top ? 0xe0 : 0xe8)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBR ? (destination_is_top ? 0xe8 : 0xe0)
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIV ? (destination_is_top ? 0xf0 : 0xf8)
                                                                           : (destination_is_top ? 0xf8 : 0xf0);
            assembly_emit_byte(builder, (u8)(operation + index));
            continue;
        }
        if (assembly_x86_opcode_is_x87_pop_arithmetic(instruction->opcode))
        {
            assembly_emit_byte(builder, 0xde);
            u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_FADDP ? 0xc0
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FMULP ? 0xc8
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBP ? 0xe8
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FSUBRP ? 0xe0
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FDIVP ? 0xf8
                                                                           : 0xf0;
            assembly_emit_byte(builder, (u8)(operation + instruction->operands[0].reg.index));
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_FXCH)
        {
            assembly_emit_byte(builder, 0xd9);
            assembly_emit_byte(builder, (u8)(0xc8 + instruction->operands[0].reg.index));
            continue;
        }
        if (assembly_x86_opcode_is_x87_compare(instruction->opcode))
        {
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMPP || instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMPP)
            {
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMPP ? 0xde : 0xda);
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMPP ? 0xd9 : 0xe9);
                continue;
            }
            AssemblyOperand first = instruction->operands[0];
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FCOM || instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMP)
            {
                if (first.kind == ASSEMBLY_OPERAND_REGISTER)
                {
                    assembly_emit_byte(builder, 0xd8);
                    assembly_emit_byte(builder, (u8)((instruction->opcode == ASSEMBLY_OPCODE_X86_FCOM ? 0xd0 : 0xd8) + first.reg.index));
                    continue;
                }
                assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, first.memory);
                assembly_emit_byte(builder, instruction->width == 32 ? 0xd8 : 0xdc);
                if (!assembly_x86_emit_memory(builder, instruction, instruction->opcode == ASSEMBLY_OPCODE_X86_FCOM ? 2 : 3, first.memory))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x87 comparison memory displacement is out of range"));
                    return;
                }
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOM || instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMP)
            {
                assembly_emit_byte(builder, 0xdd);
                assembly_emit_byte(builder, (u8)((instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOM ? 0xe0 : 0xe8) + first.reg.index));
                continue;
            }
            AssemblyRegister source = instruction->operands[1].reg;
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FCMOVCC)
            {
                u8 inverse = instruction->condition >= 4;
                u8 condition = inverse ? (u8)(instruction->condition - 4) : instruction->condition;
                assembly_emit_byte(builder, inverse ? 0xdb : 0xda);
                assembly_emit_byte(builder, (u8)(0xc0 + condition * 8 + source.index));
                continue;
            }
            u8 pop = instruction->opcode == ASSEMBLY_OPCODE_X86_FCOMIP || instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMIP;
            u8 unordered = instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMI || instruction->opcode == ASSEMBLY_OPCODE_X86_FUCOMIP;
            assembly_emit_byte(builder, pop ? 0xdf : 0xdb);
            assembly_emit_byte(builder, (u8)((unordered ? 0xe8 : 0xf0) + source.index));
            continue;
        }
        if (assembly_x86_opcode_is_x87_integer_arithmetic(instruction->opcode))
        {
            AssemblyMemory memory = instruction->operands[0].memory;
            assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, memory);
            assembly_emit_byte(builder, instruction->width == 16 ? 0xde : 0xda);
            u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_FIADD ? 0
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FIMUL ? 1
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FISUB ? 4
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FISUBR ? 5
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FIDIV ? 6
                                                                         : 7;
            if (!assembly_x86_emit_memory(builder, instruction, group, memory))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x87 integer arithmetic memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_x87_state_memory(instruction->opcode))
        {
            u8 waited = instruction->opcode == ASSEMBLY_OPCODE_X86_FSTCW || instruction->opcode == ASSEMBLY_OPCODE_X86_FSTENV ||
                          instruction->opcode == ASSEMBLY_OPCODE_X86_FSAVE || instruction->opcode == ASSEMBLY_OPCODE_X86_FSTSW;
            if (waited)
            {
                assembly_emit_byte(builder, 0x9b);
            }
            AssemblyOperand operand = instruction->operands[0];
            if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
            {
                assembly_emit_byte(builder, 0xdf);
                assembly_emit_byte(builder, 0xe0);
                continue;
            }
            assembly_x86_emit_memory_prefix(builder, 0, (AssemblyRegister){0}, operand.memory);
            u8 primary = instruction->opcode == ASSEMBLY_OPCODE_X86_FBLD || instruction->opcode == ASSEMBLY_OPCODE_X86_FBSTP ? 0xdf
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDCW || instruction->opcode == ASSEMBLY_OPCODE_X86_FNSTCW ||
                                   instruction->opcode == ASSEMBLY_OPCODE_X86_FSTCW || instruction->opcode == ASSEMBLY_OPCODE_X86_FLDENV ||
                                   instruction->opcode == ASSEMBLY_OPCODE_X86_FNSTENV || instruction->opcode == ASSEMBLY_OPCODE_X86_FSTENV
                             ? 0xd9
                             : 0xdd;
            u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_FBLD || instruction->opcode == ASSEMBLY_OPCODE_X86_FLDENV ||
                               instruction->opcode == ASSEMBLY_OPCODE_X86_FRSTOR
                           ? 4
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FLDCW ? 5
                       : instruction->opcode == ASSEMBLY_OPCODE_X86_FBSTP || instruction->opcode == ASSEMBLY_OPCODE_X86_FNSTENV ||
                                 instruction->opcode == ASSEMBLY_OPCODE_X86_FSTENV || instruction->opcode == ASSEMBLY_OPCODE_X86_FNSAVE ||
                                 instruction->opcode == ASSEMBLY_OPCODE_X86_FSAVE
                           ? 6
                           : 7;
            assembly_emit_byte(builder, primary);
            if (!assembly_x86_emit_memory(builder, instruction, group, operand.memory))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x87 state memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_x87_stack_control(instruction->opcode))
        {
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_FINCSTP || instruction->opcode == ASSEMBLY_OPCODE_X86_FDECSTP)
            {
                assembly_emit_byte(builder, 0xd9);
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FINCSTP ? 0xf7 : 0xf6);
            }
            else
            {
                assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_FFREE ? 0xdd : 0xdf);
                assembly_emit_byte(builder, (u8)(0xc0 + instruction->operands[0].reg.index));
            }
            continue;
        }
        if (assembly_x86_opcode_is_legacy_packed(instruction->opcode))
        {
            AssemblyOperand first = instruction->operands[0];
            AssemblyOperand second = instruction->operands[1];
            u8 move = instruction->opcode == ASSEMBLY_OPCODE_X86_MOVQ_MMX;
            u8 load = !move || first.kind == ASSEMBLY_OPERAND_REGISTER;
            AssemblyRegister reg = load ? first.reg : second.reg;
            AssemblyOperand rm = load ? second : first;
            if (reg.class == ASSEMBLY_REGISTER_XMM)
            {
                assembly_emit_byte(builder, 0x66);
            }
            if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, 0, reg, rm.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, 0, reg, rm.reg);
            }
            assembly_emit_byte(builder, 0x0f);
            u8 packed_opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_MOVQ_MMX ? (load ? 0x6f : 0x7f)
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PADDB_MMX ? 0xfc
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PADDW_MMX ? 0xfd
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PADDD_MMX ? 0xfe
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PADDQ_MMX ? 0xd4
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PSUBB_MMX ? 0xf8
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PSUBW_MMX ? 0xf9
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PSUBD_MMX ? 0xfa
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PSUBQ_MMX ? 0xfb
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PAND_MMX ? 0xdb
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_POR_MMX ? 0xeb
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PXOR_MMX ? 0xef
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPEQB_MMX ? 0x74
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPEQW_MMX ? 0x75
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPEQD_MMX ? 0x76
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPGTB_MMX ? 0x64
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPGTW_MMX ? 0x65
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_PCMPGTD_MMX ? 0x66
                                                                                        : 0xd5;
            assembly_emit_byte(builder, packed_opcode);
            if (!assembly_x86_emit_rm(builder, instruction, reg.index, rm))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_JCC)
        {
            assembly_emit_byte(builder, 0x0f);
            assembly_emit_byte(builder, (u8)(0x80 + instruction->condition));
            AssemblyExpression expression = instruction->operands[0].expression;
            s64 target = 0;
            u32 displacement = 0;
            if (assembly_expression_target(builder, expression, &target))
            {
                s64 next = (s64)instruction->offset + 6;
                if (target < next + INT32_MIN || target > next + INT32_MAX)
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("x86 conditional branch target is out of range"));
                }
                else
                {
                    displacement = (u32)(s32)(target - next);
                }
            }
            else if (!assembly_relocation_append(builder, instruction->offset + 2, expression, ASSEMBLY_RELOCATION_X86_PC32, -4))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                    S8("x86 conditional branch relocation addend is out of range"));
            }
            assembly_emit_u32(builder, displacement);
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_SETCC)
        {
            AssemblyOperand operand = instruction->operands[0];
            if (operand.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, 8, (AssemblyRegister){0}, operand.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, 8, (AssemblyRegister){0}, operand.reg);
            }
            assembly_emit_byte(builder, 0x0f);
            assembly_emit_byte(builder, (u8)(0x90 + instruction->condition));
            if (!assembly_x86_emit_rm(builder, instruction, 0, operand))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_CMOVCC)
        {
            AssemblyRegister destination = instruction->operands[0].reg;
            AssemblyOperand source = instruction->operands[1];
            if (source.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, instruction->width, destination, source.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, instruction->width, destination, source.reg);
            }
            assembly_emit_byte(builder, 0x0f);
            assembly_emit_byte(builder, (u8)(0x40 + instruction->condition));
            if (!assembly_x86_emit_rm(builder, instruction, destination.index, source))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_sse2(instruction->opcode))
        {
            AssemblyOperand first = instruction->operands[0];
            AssemblyOperand second = instruction->operands[1];
            u8 move = instruction->opcode >= ASSEMBLY_OPCODE_X86_MOVAPS && instruction->opcode <= ASSEMBLY_OPCODE_X86_MOVDQU;
            u8 load = !move || first.kind == ASSEMBLY_OPERAND_REGISTER;
            AssemblyRegister reg = load ? first.reg : second.reg;
            AssemblyOperand rm = load ? second : first;
            assembly_x86_emit_sse_prefix(builder, instruction->opcode);
            if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, 0, reg, rm.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, 0, reg, rm.reg);
            }
            assembly_emit_byte(builder, 0x0f);
            u8 opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_MOVAPS ? (load ? 0x28 : 0x29)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVUPS ? (load ? 0x10 : 0x11)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVAPD ? (load ? 0x28 : 0x29)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVUPD ? (load ? 0x10 : 0x11)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVDQA ? (load ? 0x6f : 0x7f)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVDQU ? (load ? 0x6f : 0x7f)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_XORPS  ? 0x57
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_XORPD  ? 0x57
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_PXOR   ? 0xef
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDPS  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDPD  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSS  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSD  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_SUBPS  ? 0x5c
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_SUBPD  ? 0x5c
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MULPS  ? 0x59
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_MULPD  ? 0x59
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_DIVPS  ? 0x5e
                                                                           : 0x5e;
            assembly_emit_byte(builder, opcode);
            if (!assembly_x86_emit_rm(builder, instruction, reg.index, rm))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (assembly_x86_opcode_is_avx(instruction->opcode))
        {
            AssemblyOperand first = instruction->operands[0];
            AssemblyOperand second = instruction->operands[1];
            u8 move = assembly_x86_opcode_is_avx_move(instruction->opcode);
            AssemblyOperand source = move ? second : instruction->operands[2];
            u8 load = !move || first.kind == ASSEMBLY_OPERAND_REGISTER;
            AssemblyRegister reg = load ? first.reg : second.reg;
            AssemblyOperand rm = load ? source : first;
            AssemblyRegisterClass vector_class = reg.class;
            u8 vex_source = move ? 0 : second.reg.index;
            u8 prefix = instruction->opcode == ASSEMBLY_OPCODE_X86_VADDSD ? 3
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VADDSS || instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVDQU ? 2
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVAPD || instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVUPD ||
                                  instruction->opcode == ASSEMBLY_OPCODE_X86_VXORPD || instruction->opcode == ASSEMBLY_OPCODE_X86_VADDPD ||
                                  instruction->opcode == ASSEMBLY_OPCODE_X86_VSUBPD || instruction->opcode == ASSEMBLY_OPCODE_X86_VMULPD ||
                                  instruction->opcode == ASSEMBLY_OPCODE_X86_VDIVPD || instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVDQA ||
                                  assembly_x86_opcode_is_avx_integer(instruction->opcode)
                              ? 1
                              : 0;
            assembly_x86_emit_vex_prefix(builder, reg, rm, vector_class, vex_source, prefix,
                                          assembly_x86_avx_map(instruction->opcode));
            u8 opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVAPS ? (load ? 0x28 : 0x29)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVUPS ? (load ? 0x10 : 0x11)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVAPD ? (load ? 0x28 : 0x29)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVUPD ? (load ? 0x10 : 0x11)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VXORPS  ? 0x57
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VXORPD  ? 0x57
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VADDPS  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VADDPD  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VADDSS  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VADDSD  ? 0x58
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VSUBPS  ? 0x5c
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VSUBPD  ? 0x5c
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMULPS  ? 0x59
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMULPD  ? 0x59
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VDIVPS    ? 0x5e
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VDIVPD    ? 0x5e
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVDQA   ? (load ? 0x6f : 0x7f)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VMOVDQU   ? (load ? 0x6f : 0x7f)
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPADDB    ? 0xfc
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPADDW    ? 0xfd
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPADDD    ? 0xfe
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPADDQ    ? 0xd4
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPSUBB    ? 0xf8
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPSUBW    ? 0xf9
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPSUBD    ? 0xfa
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPSUBQ    ? 0xfb
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPAND     ? 0xdb
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPOR      ? 0xeb
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPXOR     ? 0xef
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPEQB  ? 0x74
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPEQW  ? 0x75
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPEQD  ? 0x76
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPEQQ  ? 0x29
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPGTB  ? 0x64
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPGTW  ? 0x65
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPGTD  ? 0x66
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPCMPGTQ  ? 0x37
                        : instruction->opcode == ASSEMBLY_OPCODE_X86_VPMULLW   ? 0xd5
                                                                               : 0x40;
            assembly_emit_byte(builder, opcode);
            if (!assembly_x86_emit_rm(builder, instruction, reg.index, rm))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            continue;
        }
        if (instruction->opcode >= ASSEMBLY_OPCODE_X86_MOV && instruction->opcode <= ASSEMBLY_OPCODE_X86_SAR)
        {
            AssemblyOperand first = instruction->operands[0];
            AssemblyOperand second = instruction->operands[1];
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_PUSH || instruction->opcode == ASSEMBLY_OPCODE_X86_POP)
            {
                if (first.reg.index >= 8)
                {
                    assembly_emit_byte(builder, 0x41);
                }
                assembly_emit_byte(builder, (u8)((instruction->opcode == ASSEMBLY_OPCODE_X86_PUSH ? 0x50 : 0x58) + (first.reg.index & 7)));
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_NEG || instruction->opcode == ASSEMBLY_OPCODE_X86_NOT ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_MUL ||
                (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 1) ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_DIV || instruction->opcode == ASSEMBLY_OPCODE_X86_IDIV)
            {
                if (first.kind == ASSEMBLY_OPERAND_MEMORY)
                {
                    assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){0}, first.memory);
                }
                else
                {
                    assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                }
                u8 byte = instruction->width == 8;
                u8 increment = instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC;
                assembly_emit_byte(builder, increment ? (byte ? 0xfe : 0xff) : (byte ? 0xf6 : 0xf7));
                u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_INC   ? 0
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ? 1
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_NOT ? 2
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_NEG ? 3
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_MUL ? 4
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL ? 5
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_DIV ? 6
                                                                                : 7;
                if (!assembly_x86_emit_rm(builder, instruction, extension, first))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x86 memory displacement is out of range"));
                    return;
                }
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 3)
            {
                AssemblyOperand source = instruction->operands[1];
                AssemblyOperand immediate = instruction->operands[2];
                AssemblyRegister destination = first.reg;
                u16 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
                u8 immediate_size = immediate.expression.addend >= INT8_MIN && immediate.expression.addend <= INT8_MAX
                                        ? 1
                                        : (u8)(full_immediate_width / 8);
                if (source.kind == ASSEMBLY_OPERAND_MEMORY)
                {
                    assembly_x86_emit_memory_prefix(builder, instruction->width, destination, source.memory);
                }
                else
                {
                    assembly_x86_emit_prefix(builder, instruction->width, destination, source.reg);
                }
                assembly_emit_byte(builder, immediate_size == 1 ? 0x6b : 0x69);
                if (!assembly_x86_emit_rm(builder, instruction, destination.index, source))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x86 memory displacement is out of range"));
                    return;
                }
                assembly_emit_immediate(builder, (u64)immediate.expression.addend, immediate_size);
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_SHL || instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_SAR)
            {
                if (first.kind == ASSEMBLY_OPERAND_MEMORY)
                {
                    assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){0}, first.memory);
                }
                else
                {
                    assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                }
                u8 one = second.expression.addend == 1;
                assembly_emit_byte(builder, one ? (instruction->width == 8 ? 0xd0 : 0xd1) : (instruction->width == 8 ? 0xc0 : 0xc1));
                u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_SHL ? 4 : instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ? 5 : 7;
                if (!assembly_x86_emit_rm(builder, instruction, extension, first))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x86 memory displacement is out of range"));
                    return;
                }
                if (!one)
                {
                    assembly_emit_byte(builder, (u8)second.expression.addend);
                }
                continue;
            }
            if (second.kind == ASSEMBLY_OPERAND_REGISTER || second.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                u8 imul = instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL;
                u8 load = second.kind == ASSEMBLY_OPERAND_MEMORY;
                AssemblyRegister reg = imul || load ? first.reg : second.reg;
                AssemblyOperand rm = imul || load ? second : first;
                if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
                {
                    assembly_x86_emit_memory_prefix(builder, instruction->width, reg, rm.memory);
                }
                else
                {
                    assembly_x86_emit_prefix(builder, instruction->width, reg, rm.reg);
                }
                if (imul)
                {
                    assembly_emit_byte(builder, 0x0f);
                    assembly_emit_byte(builder, 0xaf);
                }
                else
                {
                    u8 opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_MOV ? 0x89
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_ADD ? 0x01
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_ADC ? 0x11
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_OR ? 0x09
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_SBB ? 0x19
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 0x21
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 0x29
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_XOR ? 0x31
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_CMP ? 0x39
                                                                                   : 0x85;
                    if (instruction->width == 8)
                    {
                        opcode -= 1;
                    }
                    if (load && instruction->opcode != ASSEMBLY_OPCODE_X86_TEST)
                    {
                        opcode += 2;
                    }
                    assembly_emit_byte(builder, opcode);
                }
                if (!assembly_x86_emit_rm(builder, instruction, reg.index, rm))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                        S8("x86 memory displacement is out of range"));
                    return;
                }
                continue;
            }
            s64 immediate = second.expression.addend;
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_MOV)
            {
                if (first.kind == ASSEMBLY_OPERAND_MEMORY)
                {
                    assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){0}, first.memory);
                    assembly_emit_byte(builder, instruction->width == 8 ? 0xc6 : 0xc7);
                    if (!assembly_x86_emit_memory(builder, instruction, 0, first.memory))
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                            S8("x86 memory displacement is out of range"));
                        return;
                    }
                    assembly_emit_immediate(builder, (u64)immediate, instruction->width == 64 ? 4 : instruction->width / 8);
                }
                else
                {
                    assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                    assembly_emit_byte(builder, (u8)((instruction->width == 8 ? 0xb0 : 0xb8) + (first.reg.index & 7)));
                    assembly_emit_immediate(builder, (u64)immediate, instruction->width / 8);
                }
                continue;
            }
            u8 test = instruction->opcode == ASSEMBLY_OPCODE_X86_TEST;
            u16 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
            u8 immediate_size = !test && immediate >= INT8_MIN && immediate <= INT8_MAX ? 1 : (u8)(full_immediate_width / 8);
            if (first.kind == ASSEMBLY_OPERAND_MEMORY)
            {
                assembly_x86_emit_memory_prefix(builder, instruction->width, (AssemblyRegister){0}, first.memory);
            }
            else
            {
                assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
            }
            assembly_emit_byte(builder, test ? (instruction->width == 8 ? 0xf6 : 0xf7)
                                             : immediate_size == 1 ? (instruction->width == 8 ? 0x80 : 0x83)
                                                                   : (instruction->width == 8 ? 0x80 : 0x81));
            u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_ADD ? 0
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_OR ? 1
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_ADC ? 2
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_SBB ? 3
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 4
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 5
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_XOR ? 6
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_CMP ? 7
                                                                            : 0;
            if (!assembly_x86_emit_rm(builder, instruction, extension, first))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("x86 memory displacement is out of range"));
                return;
            }
            assembly_emit_immediate(builder, (u64)immediate, immediate_size);
            continue;
        }
        u32 word = instruction->opcode == ASSEMBLY_OPCODE_AARCH64_NOP   ? UINT32_C(0xd503201f)
                   : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_RET ? UINT32_C(0xd65f03c0)
                   : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL  ? UINT32_C(0x94000000)
                                                                        : UINT32_C(0x14000000);
        if (instruction->opcode == ASSEMBLY_OPCODE_AARCH64_B || instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL)
        {
            s64 target = 0;
            if (assembly_expression_target(builder, instruction->operands[0].expression, &target))
            {
                s64 instruction_offset = (s64)instruction->offset;
                s64 lower = instruction_offset - (INT64_C(1) << 27);
                s64 upper = instruction_offset + (INT64_C(1) << 27);
                if (target < lower || target >= upper || (target - instruction_offset) % 4)
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("AArch64 branch target is out of range or unaligned"));
                }
                else
                {
                    word |= (u32)((target - instruction_offset) / 4) & UINT32_C(0x03ffffff);
                }
            }
            else if (!assembly_relocation_append(builder, instruction->offset, instruction->operands[0].expression,
                                                 ASSEMBLY_RELOCATION_AARCH64_BRANCH26, 0))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                    S8("AArch64 relocation addend is out of range"));
            }
        }
        assembly_emit_u32(builder, word);
    }
    builder->result.bytes.length = builder->output_count;
}

AssemblyEncodeResult assembly_encode(Arena* arena, String8 source, AssemblyEncodeOptions options)
{
    AssemblyEncodeResult empty = {0};
    if (!arena || (source.length && !source.pointer))
    {
        return empty;
    }
    u32 line_count = 1;
    for (u64 index = 0; index < source.length; index += 1)
    {
        if (source.pointer[index] == '\n' && line_count != UINT32_MAX)
        {
            line_count += 1;
        }
    }
    if (line_count > UINT32_MAX / 4)
    {
        return empty;
    }
    AssemblyBuilder builder = {
        .arena = arena,
        .instruction_capacity = line_count,
        .symbol_capacity = line_count * 2,
        .relocation_capacity = line_count,
        .diagnostic_capacity = line_count * 4,
    };
    builder.result.symbols = arena_allocate(arena, AssemblySymbol, builder.symbol_capacity);
    builder.result.relocations = arena_allocate(arena, AssemblyRelocation, builder.relocation_capacity);
    builder.result.diagnostics = arena_allocate(arena, AssemblyDiagnostic, builder.diagnostic_capacity);
    memset(builder.result.symbols, 0, sizeof(*builder.result.symbols) * builder.symbol_capacity);
    bool valid_target = options.target.cpu_arch == CPU_ARCH_X86_64 || options.target.cpu_arch == CPU_ARCH_AARCH64;
    bool valid_syntax = options.syntax >= ASSEMBLY_SYNTAX_DEFAULT && options.syntax < ASSEMBLY_SYNTAX_COUNT &&
                        (options.target.cpu_arch == CPU_ARCH_X86_64 || options.syntax == ASSEMBLY_SYNTAX_DEFAULT);
    if (!valid_target)
    {
        assembly_diagnostic(&builder, ASSEMBLY_DIAGNOSTIC_INVALID_TARGET, 1, 1, 0, S8("assembly target must be x86-64 or AArch64"));
        return builder.result;
    }
    if (!valid_syntax)
    {
        assembly_diagnostic(&builder, ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX, 1, 1, 0, S8("syntax dialect is incompatible with the target"));
        return builder.result;
    }
    TemporalArena temporary = scratch_begin(&arena, 1);
    builder.instructions = arena_allocate(temporary.arena, AssemblyInstruction, builder.instruction_capacity);
    AssemblySyntax syntax = options.syntax == ASSEMBLY_SYNTAX_DEFAULT ? ASSEMBLY_SYNTAX_INTEL : options.syntax;
    assembly_source_parse(&builder, source, options.target, syntax);
    builder.result.bytes.pointer = arena_allocate(arena, u8, builder.output_capacity);
    assembly_instructions_emit(&builder);
    scratch_end(temporary);
    return builder.result;
}
