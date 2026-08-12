#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_syntax.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>

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
    ASSEMBLY_OPCODE_X86_AMD_XOP,
    ASSEMBLY_OPCODE_X86_AMD_FMA4,
    ASSEMBLY_OPCODE_X86_AMD_TBM,
    ASSEMBLY_OPCODE_X86_AMD_3DNOW,
    ASSEMBLY_OPCODE_X86_AMD_FEMMS,
    ASSEMBLY_OPCODE_AARCH64_NOP,
    ASSEMBLY_OPCODE_AARCH64_RET,
    ASSEMBLY_OPCODE_AARCH64_B,
    ASSEMBLY_OPCODE_AARCH64_BL,
    ASSEMBLY_OPCODE_COUNT,
} AssemblyOpcode;

typedef struct AssemblyAmdForm AssemblyAmdForm;

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
    u64 unsigned_addend;
    u32 symbol;
    bool has_symbol;
    bool has_unsigned_addend;
    u8 reserved[2];
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
    ASSEMBLY_REGISTER_BND,
    ASSEMBLY_REGISTER_CONTROL,
    ASSEMBLY_REGISTER_DEBUG,
    ASSEMBLY_REGISTER_SEGMENT,
    ASSEMBLY_REGISTER_SPECIAL,
    ASSEMBLY_REGISTER_CLASS_COUNT,
} AssemblyRegisterClass;

struct AssemblyRegister
{
    u8 index;
    u16 width;
    AssemblyRegisterClass class;
    bool high_byte;
    bool stack_pointer;
};

typedef struct AssemblyMemory AssemblyMemory;
struct AssemblyMemory
{
    AssemblyExpression displacement;
    AssemblyRegister base;
    AssemblyRegister index;
    u8 scale;
    u8 address_size;
    u8 segment;
    u16 width;
    bool has_base;
    bool has_index;
    bool rip_relative;
    bool has_segment;
    bool vsib;
    // AT&T permits an absolute address without the parenthesized
    // displacement(base,index,scale) form.  Keep this source-level bit so
    // direct branch targets (which use the same bare spelling) can remain
    // expressions while indirect branches still see a memory operand.
    bool absolute;
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

enum
{
    // Generated AArch64 source forms include up to ten visible operands.
    // Keep parser and metadata projections at the same bounded capacity.
    ASSEMBLY_MAX_OPERANDS = 10,
};

typedef struct AssemblyInstruction AssemblyInstruction;

typedef enum AssemblyEncodingKind
{
    ASSEMBLY_ENCODING_HANDWRITTEN,
    ASSEMBLY_ENCODING_AARCH64_FIXED_WORD,
    ASSEMBLY_ENCODING_AARCH64_M1_GPR,
    ASSEMBLY_ENCODING_AARCH64_M1_SCALAR_INTEGER,
    ASSEMBLY_ENCODING_AARCH64_CONTROL,
} AssemblyEncodingKind;

struct AssemblyInstruction
{
    AssemblyOperand operands[ASSEMBLY_MAX_OPERANDS];
    u64 offset;
    u32 line;
    u32 column;
    u32 size;
    AssemblyOpcode opcode;
    AssemblyEncodingKind encoding_kind;
    u8 operand_count;
    u16 width;
    u8 condition;
    bool lock_prefix;
    bool no_flags;
    bool evex;
    u8 rip_relocation_trailing;
    String8 amd_mnemonic;
    AssemblyAmdForm const* amd_form;
    bool metadata;
    u8 metadata_operand_count;
    u16 metadata_reserved;
    u32 metadata_form_id;
    u32 fixed_word;
    u32 aarch64_gpr_form_index;
    u32 aarch64_scalar_integer_form_index;
    u32 aarch64_control_row_index;
    u8 aarch64_scalar_integer_operand_count;
    u8 aarch64_scalar_integer_modifier_count;
    u8 aarch64_scalar_integer_reserved[2];
    A64ScalarIntOperand aarch64_scalar_integer_operands[4];
    A64ScalarIntModifier aarch64_scalar_integer_modifiers[1];
    BusterAarch64ControlInstruction aarch64_control_instruction;
    AssemblyExpression aarch64_control_expressions[4];
    u8 aarch64_control_expression_mask;
    u8 aarch64_control_reserved[3];
    String8 metadata_mnemonic;
    u8 metadata_address_size;
    u8 metadata_reserved_address[3];
    BusterX86MetadataPhysicalOperand metadata_operands[ASSEMBLY_MAX_OPERANDS];
    BusterX86MetadataPhysicalAttributes metadata_attributes;
};

typedef struct AssemblyBuilder AssemblyBuilder;
struct AssemblyBuilder
{
    Arena* arena;
    Target target;
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

enum
{
    // A source line can define one label and contain ten metadata operands;
    // each of those can introduce one distinct symbol.
    ASSEMBLY_SOURCE_SYMBOLS_PER_LINE = ASSEMBLY_MAX_OPERANDS + 1,
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

typedef enum AssemblyOperandSplitStatus
{
    ASSEMBLY_OPERAND_SPLIT_END,
    ASSEMBLY_OPERAND_SPLIT_SUCCESS,
    ASSEMBLY_OPERAND_SPLIT_INVALID,
} AssemblyOperandSplitStatus;

enum
{
    ASSEMBLY_OPERAND_DELIMITER_CAPACITY = 64,
};

BUSTER_GLOBAL_LOCAL AssemblyOperandSplitStatus assembly_operand_split_next(String8 text, u64* cursor, String8* result)
{
    if (*cursor >= text.length)
    {
        return ASSEMBLY_OPERAND_SPLIT_END;
    }
    u64 operand_start = *cursor;
    u64 operand_end = operand_start;
    char8 delimiter_stack[ASSEMBLY_OPERAND_DELIMITER_CAPACITY] = {0};
    u32 delimiter_count = 0;
    while (operand_end < text.length)
    {
        char8 character = text.pointer[operand_end];
        if (character == '(' || character == '[' || character == '{')
        {
            if (delimiter_count == BUSTER_ARRAY_LENGTH(delimiter_stack))
            {
                return ASSEMBLY_OPERAND_SPLIT_INVALID;
            }
            delimiter_stack[delimiter_count++] = character;
        }
        else if (character == ')' || character == ']' || character == '}')
        {
            char8 expected = character == ')' ? '(' : character == ']' ? '[' : '{';
            if (!delimiter_count || delimiter_stack[delimiter_count - 1] != expected)
            {
                return ASSEMBLY_OPERAND_SPLIT_INVALID;
            }
            delimiter_count -= 1;
        }
        else if (character == ',' && !delimiter_count)
        {
            break;
        }
        operand_end += 1;
    }
    if (delimiter_count)
    {
        return ASSEMBLY_OPERAND_SPLIT_INVALID;
    }
    String8 operand = assembly_trim(string_slice(text, operand_start, operand_end));
    if (!operand.length)
    {
        return ASSEMBLY_OPERAND_SPLIT_INVALID;
    }
    *result = operand;
    *cursor = operand_end < text.length ? operand_end + 1 : operand_end;
    return ASSEMBLY_OPERAND_SPLIT_SUCCESS;
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

BUSTER_GLOBAL_LOCAL u64 assembly_leading_label_colon(String8 statement)
{
    u64 token_end = 0;
    while (token_end < statement.length && !assembly_space(statement.pointer[token_end]) && statement.pointer[token_end] != ':')
    {
        token_end += 1;
    }
    if (token_end < statement.length && statement.pointer[token_end] == ':')
    {
        return token_end;
    }
    u64 colon = token_end;
    while (colon < statement.length && assembly_space(statement.pointer[colon]))
    {
        colon += 1;
    }
    if (colon >= statement.length || statement.pointer[colon] != ':')
    {
        return BUSTER_STRING_NO_MATCH;
    }
    // A separated colon can be followed immediately by the instruction, as
    // in `label :nop`.  The exception is an AArch64 modifier token such as
    // `:lo12:symbol`, whose second colon makes the first one part of the
    // operand after the mnemonic rather than a label separator.
    u64 modifier_end = colon + 1;
    while (modifier_end < statement.length && assembly_character_identifier(statement.pointer[modifier_end]))
    {
        modifier_end += 1;
    }
    if (modifier_end == colon + 1 || modifier_end >= statement.length || statement.pointer[modifier_end] != ':')
    {
        return colon;
    }
    return BUSTER_STRING_NO_MATCH;
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

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_gpr_register_parse(String8 text, AssemblyRegister* result)
{
    if (!result || !text.pointer || !text.length)
    {
        return false;
    }
    text = assembly_trim(text);
    if (assembly_word_equal(text, S8("sp")) || assembly_word_equal(text, S8("wsp")))
    {
        *result = (AssemblyRegister){.index = 31, .width = assembly_word_equal(text, S8("wsp")) ? 32u : 64u,
                                    .class = ASSEMBLY_REGISTER_GPR, .stack_pointer = true};
        return true;
    }
    if (assembly_word_equal(text, S8("wzr")) || assembly_word_equal(text, S8("xzr")))
    {
        *result = (AssemblyRegister){.index = 31, .width = assembly_word_equal(text, S8("wzr")) ? 32u : 64u,
                                    .class = ASSEMBLY_REGISTER_GPR};
        return true;
    }
    if (text.length < 2 || (assembly_ascii_lower(text.pointer[0]) != 'w' && assembly_ascii_lower(text.pointer[0]) != 'x'))
    {
        return false;
    }
    if (text.length > 2 && text.pointer[1] == '0')
    {
        return false;
    }
    u32 index = 0;
    for (u64 position = 1; position < text.length; position += 1)
    {
        char8 value = text.pointer[position];
        if (value < '0' || value > '9' || index > 31)
        {
            return false;
        }
        index = index * 10u + (u32)(value - '0');
    }
    // Arm's architectural spelling for register 31 is WZR/XZR/WSP/SP.  A
    // literal W31/X31 is never accepted by this front door.
    if (index > 30)
    {
        return false;
    }
    *result = (AssemblyRegister){.index = (u8)index, .width = assembly_ascii_lower(text.pointer[0]) == 'w' ? 32u : 64u,
                                .class = ASSEMBLY_REGISTER_GPR};
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

BUSTER_GLOBAL_LOCAL bool assembly_parse_u64(String8 string, u64* result)
{
    string = assembly_trim(string);
    if (!string.length || !result || string.pointer[0] == '-')
    {
        return false;
    }
    u64 cursor = string.pointer[0] == '+' ? 1 : 0;
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
    for (; cursor < string.length; cursor += 1)
    {
        char8 character = string.pointer[cursor];
        u32 digit = character >= '0' && character <= '9' ? (u32)(character - '0')
                    : character >= 'a' && character <= 'f' ? (u32)(character - 'a') + 10
                    : character >= 'A' && character <= 'F' ? (u32)(character - 'A') + 10
                                                            : UINT32_MAX;
        if (digit >= base || value > (UINT64_MAX - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
    }
    *result = value;
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
    u64 unsigned_integer = 0;
    if (assembly_parse_u64(text, &unsigned_integer))
    {
        if (unsigned_integer <= (u64)INT64_MAX)
        {
            result->addend = (s64)unsigned_integer;
        }
        else
        {
            result->unsigned_addend = unsigned_integer;
            result->has_unsigned_addend = true;
        }
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
    if (text.length - prefix.length > 1 && text.pointer[prefix.length] == '0')
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
    if (suffix_start - 1 > 1 && text.pointer[1] == '0')
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

    // GNU and LLVM expose the complete four-bit CR/DR encoding domain.  Keep
    // CR0..CR15 and DR0..DR15 selectable as encodable register numbers; a
    // processor may still #UD for an architecturally reserved number when
    // the resulting instruction is executed.
    if (assembly_numbered_register_parse(text, S8("cr"), 15, 64, ASSEMBLY_REGISTER_CONTROL, result) ||
        assembly_numbered_register_parse(text, S8("dr"), 15, 64, ASSEMBLY_REGISTER_DEBUG, result) ||
        assembly_numbered_register_parse(text, S8("bnd"), 3, 128, ASSEMBLY_REGISTER_BND, result))
    {
        return true;
    }
    static String8 const names_segment[] = {
        S8_INITIALIZER("es"), S8_INITIALIZER("cs"), S8_INITIALIZER("ss"),
        S8_INITIALIZER("ds"), S8_INITIALIZER("fs"), S8_INITIALIZER("gs"),
    };
    for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(names_segment); register_index += 1)
    {
        if (assembly_word_equal(text, names_segment[register_index]))
        {
            *result = (AssemblyRegister){.index = (u8)register_index, .width = 16, .class = ASSEMBLY_REGISTER_SEGMENT};
            return true;
        }
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
    // ACE-1's BSRMOV forms name the fixed architectural BSR0 operand when
    // spelling the direction explicitly. It is a 64-bit special register,
    // distinct from the x87 ST* aliases below.
    if (assembly_word_equal(text, S8("bsr0")))
    {
        *result = (AssemblyRegister){.index = 0, .width = 64, .class = ASSEMBLY_REGISTER_SPECIAL};
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_segment_parse(String8 text, AssemblySyntax syntax, u8* result)
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
    static String8 const names[] = {
        S8_INITIALIZER("es"), S8_INITIALIZER("cs"), S8_INITIALIZER("ss"),
        S8_INITIALIZER("ds"), S8_INITIALIZER("fs"), S8_INITIALIZER("gs"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        if (assembly_word_equal(text, names[index]))
        {
            if (result)
            {
                *result = (u8)(index + 1);
            }
            return true;
        }
    }
    return false;
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
    if (!assembly_expression_parse(builder, text, &expression) || expression.has_unsigned_addend ||
        (destination->has_symbol && expression.has_symbol))
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

BUSTER_GLOBAL_LOCAL String8 assembly_x86_memory_strip_segment(String8 text, AssemblySyntax syntax, AssemblyMemory* result)
{
    u64 colon = string_first_code_unit(text, ':');
    if (colon < text.length)
    {
        u8 segment = 0;
        String8 prefix = assembly_trim(string_slice(text, 0, colon));
        if (result->has_segment || !assembly_x86_segment_parse(prefix, syntax, &segment))
        {
            return text;
        }
        result->has_segment = true;
        result->segment = segment;
        text = assembly_trim(string_slice(text, colon + 1, text.length));
    }
    return text;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_memory_parse_intel(AssemblyBuilder* builder, String8 text, AssemblyMemory* result)
{
    text = assembly_trim(text);
    text = assembly_x86_memory_strip_segment(text, ASSEMBLY_SYNTAX_INTEL, result);
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
    text = assembly_x86_memory_strip_segment(text, ASSEMBLY_SYNTAX_INTEL, result);
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
            if (subtract || result->has_index || !assembly_register_parse(register_text, ASSEMBLY_SYNTAX_INTEL, &reg) ||
                !assembly_x86_scale_parse(scale_text, &result->scale))
            {
                return false;
            }
            bool vector_index = reg.class == ASSEMBLY_REGISTER_XMM || reg.class == ASSEMBLY_REGISTER_YMM ||
                                reg.class == ASSEMBLY_REGISTER_ZMM;
            if ((!vector_index && (reg.class != ASSEMBLY_REGISTER_GPR || (reg.width != 32 && reg.width != 64) || reg.index == 4)) ||
                (vector_index && result->rip_relative))
            {
                return false;
            }
            if (!vector_index)
            {
                if (result->address_size && result->address_size != reg.width)
                {
                    return false;
                }
                result->address_size = (u8)reg.width;
            }
            result->index = reg;
            result->has_index = true;
            result->vsib = vector_index;
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
            if (subtract || reg.class != ASSEMBLY_REGISTER_GPR || (reg.width != 32 && reg.width != 64))
            {
                return false;
            }
            if (result->address_size && result->address_size != reg.width)
            {
                return false;
            }
            result->address_size = (u8)reg.width;
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
    text = assembly_x86_memory_strip_segment(text, ASSEMBLY_SYNTAX_ATT, result);
    u64 open = string_first_code_unit(text, '(');
    if (open == BUSTER_STRING_NO_MATCH)
    {
        // In AT&T syntax '$' is the immediate marker.  Do not let the
        // expression grammar's '$'-prefixed identifiers turn it into an
        // absolute memory operand before the immediate path sees it.
        if (!text.length || text.pointer[0] == '$' || !assembly_expression_parse(builder, text, &result->displacement))
        {
            return false;
        }
        result->absolute = true;
        result->scale = 1;
        return true;
    }
    if (text.length < open + 2 || text.pointer[text.length - 1] != ')')
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
        else if (!assembly_register_parse(fields[0], ASSEMBLY_SYNTAX_ATT, &result->base) ||
                 result->base.class != ASSEMBLY_REGISTER_GPR || (result->base.width != 32 && result->base.width != 64))
        {
            return false;
        }
        else
        {
            result->address_size = (u8)result->base.width;
            result->has_base = true;
        }
    }
    result->scale = 1;
    if (field_count >= 2 && fields[1].length)
    {
        if (result->rip_relative || !assembly_register_parse(fields[1], ASSEMBLY_SYNTAX_ATT, &result->index))
        {
            return false;
        }
        bool vector_index = result->index.class == ASSEMBLY_REGISTER_XMM || result->index.class == ASSEMBLY_REGISTER_YMM ||
                            result->index.class == ASSEMBLY_REGISTER_ZMM;
        if ((!vector_index && (result->index.class != ASSEMBLY_REGISTER_GPR ||
                               (result->index.width != 32 && result->index.width != 64) || result->index.index == 4)))
        {
            return false;
        }
        if (!vector_index)
        {
            if (result->address_size && result->address_size != result->index.width)
            {
                return false;
            }
            result->address_size = (u8)result->index.width;
        }
        result->has_index = true;
        result->vsib = vector_index;
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
    AssemblyEncodingKind encoding_kind;
    u32 fixed_word;
    u32 aarch64_control_row_index;
    AssemblyAmdForm const* amd_form;
};

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_condition_parse(String8 text, u64* value)
{
    text = assembly_trim(text);
    if (!value || !text.length)
    {
        return false;
    }
    for (u32 index = 0; index < buster_aarch64_control_condition_count(); index += 1)
    {
        BusterAarch64ControlCondition condition = {0};
        String8 name = {0};
        if (buster_aarch64_control_condition((u8)index, &condition) &&
            buster_aarch64_control_semantic_string(condition.name, &name) && assembly_word_equal(text, name))
        {
            *value = index;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_lookup(Target target, String8 mnemonic, AssemblyInstructionInfo* result)
{
    if (!result || !buster_aarch64_arm_m1_fixed_target(target))
    {
        return false;
    }
    bool conditional = false;
    u64 condition = 0;
    if (mnemonic.length > 2 && assembly_word_equal(string_slice(mnemonic, 0, 2), S8("b.")))
    {
        conditional = assembly_aarch64_control_condition_parse(string_slice(mnemonic, 2, mnemonic.length), &condition);
        if (!conditional)
        {
            return false;
        }
    }
    for (u32 row_index = 0; row_index < buster_aarch64_control_semantic_count(); row_index += 1)
    {
        BusterAarch64ControlSemanticRecord row = {0};
        String8 row_mnemonic = {0};
        if (!buster_aarch64_control_semantic_row(row_index, &row) ||
            !buster_aarch64_control_semantic_string(row.mnemonic, &row_mnemonic))
        {
            continue;
        }
        if (row.form == BUSTER_AARCH64_CONTROL_FORM_B || row.form == BUSTER_AARCH64_CONTROL_FORM_BL ||
            row.form == BUSTER_AARCH64_CONTROL_FORM_RET)
        {
            continue;
        }
        bool mnemonic_match = conditional ? row.form == BUSTER_AARCH64_CONTROL_FORM_B_COND
                                          : assembly_word_equal(mnemonic, row_mnemonic);
        if (mnemonic_match)
        {
            *result = (AssemblyInstructionInfo){
                .opcode = ASSEMBLY_OPCODE_COUNT,
                .encoding_kind = ASSEMBLY_ENCODING_AARCH64_CONTROL,
                .condition = (u8)condition,
                .aarch64_control_row_index = UINT32_MAX,
            };
            return true;
        }
    }
    return false;
}

typedef enum AssemblyAmdEncoding
{
    ASSEMBLY_AMD_ENCODING_XOP_VECTOR2,
    ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT,
    ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE,
    ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE,
    ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT,
    ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE,
    ASSEMBLY_AMD_ENCODING_VEX_FMA4,
    ASSEMBLY_AMD_ENCODING_XOP_GPR2,
    ASSEMBLY_AMD_ENCODING_XOP_GPR3_BEXTR,
    ASSEMBLY_AMD_ENCODING_XOP_LWP1,
    ASSEMBLY_AMD_ENCODING_XOP_LWP3,
    ASSEMBLY_AMD_ENCODING_3DNOW2,
    ASSEMBLY_AMD_ENCODING_FEMMS,
} AssemblyAmdEncoding;

enum
{
    ASSEMBLY_AMD_FORM_SCALAR = 1u << 0,
    ASSEMBLY_AMD_FORM_DEFAULT_W1 = 1u << 1,
    ASSEMBLY_AMD_FORM_ALLOW_W1 = 1u << 2,
};

struct AssemblyAmdForm
{
    String8 name;
    AssemblyOpcode opcode;
    AssemblyAmdEncoding encoding;
    u8 map;
    u8 opcode_byte;
    u8 fixed_reg;
    u8 operand_count;
    u8 vector_width_mask;
    u8 element_bytes;
    u8 mandatory_prefix;
    u8 flags;
    TargetCpuFeature feature;
};

BUSTER_GLOBAL_LOCAL AssemblyAmdForm const assembly_x86_amd_forms[] = {
    {S8_INITIALIZER("vpermil2ps"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE, 3, 0x48, 0, 5, 3, 4, 1, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpermil2pd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE, 3, 0x49, 0, 5, 3, 8, 1, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacssww"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x85, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacsswd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x86, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacssdql"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x87, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacsww"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x95, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacswd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x96, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacsdql"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x97, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcmov"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0xa2, 0, 4, 3, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpperm"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0xa3, 0, 4, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmadcsswd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0xa6, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmadcswd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0xb6, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE, 8, 0xc0, 0, 3, 1, 1, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE, 8, 0xc1, 0, 3, 1, 2, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE, 8, 0xc2, 0, 3, 1, 4, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE, 8, 0xc3, 0, 3, 1, 8, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x90, 0, 3, 1, 0, 0,
     ASSEMBLY_AMD_FORM_ALLOW_W1, TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x91, 0, 3, 1, 0, 0,
     ASSEMBLY_AMD_FORM_ALLOW_W1, TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x92, 0, 3, 1, 0, 0,
     ASSEMBLY_AMD_FORM_ALLOW_W1, TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vprotq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x93, 0, 3, 1, 0, 0,
     ASSEMBLY_AMD_FORM_ALLOW_W1, TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacssdd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x8e, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacssdqh"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x8f, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacsdd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x9e, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpmacsdqh"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT, 8, 0x9f, 0, 4, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xcc, 0, 4, 1, 1, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xcd, 0, 4, 1, 2, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xce, 0, 4, 1, 4, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xcf, 0, 4, 1, 8, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomub"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xec, 0, 4, 1, 1, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomuw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xed, 0, 4, 1, 2, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomud"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xee, 0, 4, 1, 4, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpcomuq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE, 8, 0xef, 0, 4, 1, 8, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vfrczps"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0x80, 0, 2, 3, 4, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vfrczpd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0x81, 0, 2, 3, 8, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vfrczss"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0x82, 0, 2, 1, 4, 0, ASSEMBLY_AMD_FORM_SCALAR,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vfrczsd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0x83, 0, 2, 1, 8, 0, ASSEMBLY_AMD_FORM_SCALAR,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshlb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x94, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshlw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x95, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshld"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x96, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshlq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x97, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddbw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xc1, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddbd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xc2, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddbq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xc3, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddwd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xc6, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddwq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xc7, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddubw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xd1, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddubd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xd2, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddubq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xd3, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphadduwd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xd6, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphadduwq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xd7, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphsubbw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xe1, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphsubwd"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xe2, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphsubdq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xe3, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshab"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x98, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshaw"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x99, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshad"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x9a, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vpshaq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT, 9, 0x9b, 0, 3, 1, 0, 0, ASSEMBLY_AMD_FORM_ALLOW_W1,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphadddq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xcb, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("vphaddudq"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_VECTOR2, 9, 0xdb, 0, 2, 1, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_XOP},
    {S8_INITIALIZER("llwpcb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_LWP1, 9, 0x12, 0, 1, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_LWP},
    {S8_INITIALIZER("slwpcb"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_LWP1, 9, 0x12, 1, 1, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_LWP},
    {S8_INITIALIZER("lwpins"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_LWP3, 10, 0x12, 0, 3, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_LWP},
    {S8_INITIALIZER("lwpval"), ASSEMBLY_OPCODE_X86_AMD_XOP, ASSEMBLY_AMD_ENCODING_XOP_LWP3, 10, 0x12, 1, 3, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_LWP},

    {S8_INITIALIZER("vfmaddsubps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x5c, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmaddsubpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x5d, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubaddps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x5e, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubaddpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x5f, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmaddps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x68, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmaddpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x69, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmaddss"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6a, 0, 4, 1, 4, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmaddsd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6b, 0, 4, 1, 8, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6c, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6d, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubss"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6e, 0, 4, 1, 4, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfmsubsd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x6f, 0, 4, 1, 8, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmaddps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x78, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmaddpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x79, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmaddss"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7a, 0, 4, 1, 4, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmaddsd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7b, 0, 4, 1, 8, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmsubps"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7c, 0, 4, 3, 4, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmsubpd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7d, 0, 4, 3, 8, 1, ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmsubss"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7e, 0, 4, 1, 4, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},
    {S8_INITIALIZER("vfnmsubsd"), ASSEMBLY_OPCODE_X86_AMD_FMA4, ASSEMBLY_AMD_ENCODING_VEX_FMA4, 3, 0x7f, 0, 4, 1, 8, 1, ASSEMBLY_AMD_FORM_SCALAR | ASSEMBLY_AMD_FORM_DEFAULT_W1,
     TARGET_CPU_FEATURE_X86_FMA4},

    {S8_INITIALIZER("bextr"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR3_BEXTR, 10, 0x10, 0, 3, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blcfill"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 1, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blci"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x02, 6, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blcic"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 5, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blcmsk"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x02, 1, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blcs"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 3, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blsfill"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 2, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("blsic"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 6, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("t1mskc"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 7, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},
    {S8_INITIALIZER("tzmsk"), ASSEMBLY_OPCODE_X86_AMD_TBM, ASSEMBLY_AMD_ENCODING_XOP_GPR2, 9, 0x01, 4, 2, 0, 0, 0, 0,
     TARGET_CPU_FEATURE_X86_TBM},

    {S8_INITIALIZER("femms"), ASSEMBLY_OPCODE_X86_AMD_FEMMS, ASSEMBLY_AMD_ENCODING_FEMMS, 0, 0, 0, 0, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pi2fw"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x0c, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOWA},
    {S8_INITIALIZER("pi2fd"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x0d, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pf2iw"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x1c, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOWA},
    {S8_INITIALIZER("pf2id"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x1d, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfnacc"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x8a, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOWA},
    {S8_INITIALIZER("pfpnacc"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x8e, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOWA},
    {S8_INITIALIZER("pfcmpge"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x90, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfmin"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x94, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfrcp"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x96, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfrsqrt"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x97, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfsub"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x9a, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfadd"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0x9e, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfcmpgt"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xa0, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfmax"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xa4, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfrcpit1"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xa6, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfrsqit1"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xa7, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfsubr"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xaa, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfacc"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xae, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfcmpeq"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xb0, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfmul"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xb4, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pfrcpit2"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xb6, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pmulhrw"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xb7, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
    {S8_INITIALIZER("pswapd"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xbb, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOWA},
    {S8_INITIALIZER("pavgusb"), ASSEMBLY_OPCODE_X86_AMD_3DNOW, ASSEMBLY_AMD_ENCODING_3DNOW2, 0, 0xbf, 0, 2, 0, 0, 0, 0, TARGET_CPU_FEATURE_X86_3DNOW},
};

BUSTER_GLOBAL_LOCAL AssemblyAmdForm const* assembly_x86_amd_form_lookup(String8 mnemonic)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(assembly_x86_amd_forms); index += 1)
    {
        if (assembly_word_equal(mnemonic, assembly_x86_amd_forms[index].name))
        {
            return assembly_x86_amd_forms + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AssemblyAmdForm const* assembly_x86_amd_form_select(AssemblyInstruction* instruction)
{
    AssemblyAmdForm const* form = instruction->amd_form;
    if (form && form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE && instruction->operand_count == 3 &&
        instruction->operands[2].kind != ASSEMBLY_OPERAND_EXPRESSION)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(assembly_x86_amd_forms); index += 1)
        {
            AssemblyAmdForm const* candidate = assembly_x86_amd_forms + index;
            if (candidate->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT &&
                assembly_word_equal(instruction->amd_mnemonic, candidate->name))
            {
                return candidate;
            }
        }
    }
    return form;
}

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
    AssemblyAmdForm const* amd_form = assembly_x86_amd_form_lookup(mnemonic);
    if (amd_form)
    {
        *result = (AssemblyInstructionInfo){.opcode = amd_form->opcode, .operand_count = amd_form->operand_count, .amd_form = amd_form};
        return true;
    }
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
        if (syntax == ASSEMBLY_SYNTAX_ATT && mnemonic.length > 1)
        {
            char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
            u8 width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64 : 0;
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
            if (!buster_aarch64_arm_m1_scalar_integer_target(target)) return false;
            u32 scalar_form_count = buster_aarch64_arm_m1_scalar_integer_form_count();
            u8 scalar_operand_count = 0;
            bool scalar_found = false;
            for (u32 form_index = 0; form_index < scalar_form_count; form_index += 1)
            {
                BusterAarch64ArmM1ScalarIntegerForm form = {0};
                if (!buster_aarch64_arm_m1_scalar_integer_form(form_index, &form) || !assembly_word_equal(mnemonic, form.mnemonic)) continue;
                if (!scalar_found)
                {
                    scalar_operand_count = form.operand_count;
                    scalar_found = true;
                }
                else if (scalar_operand_count != form.operand_count)
                {
                    return false;
                }
            }
            if (scalar_found)
            {
                *result = (AssemblyInstructionInfo){
                    .opcode = ASSEMBLY_OPCODE_COUNT,
                    .operand_count = scalar_operand_count,
                    .encoding_kind = ASSEMBLY_ENCODING_AARCH64_M1_SCALAR_INTEGER,
                };
                return true;
            }
            if (assembly_aarch64_control_lookup(target, mnemonic, result))
            {
                return true;
            }
            // The canonical direct-GPR projection is target-gated. Keep the
            // mnemonic known even when an explicit feature subtraction later
            // rejects the selected row, so diagnostics distinguish unsupported
            // features from malformed operands.
            if (!buster_aarch64_arm_m1_gpr_target(target)) return false;
            u32 form_count = buster_aarch64_arm_m1_gpr_form_count();
            u8 operand_count = 0;
            bool found = false;
            for (u32 form_index = 0; form_index < form_count; form_index += 1)
            {
                BusterAarch64ArmM1GprForm form = {0};
                if (!buster_aarch64_arm_m1_gpr_form(form_index, &form) || !assembly_word_equal(mnemonic, form.mnemonic)) continue;
                if (!found)
                {
                    operand_count = form.operand_count;
                    found = true;
                }
                else if (operand_count != form.operand_count)
                {
                    return false;
                }
            }
            if (!found) return false;
            *result = (AssemblyInstructionInfo){
                .opcode = ASSEMBLY_OPCODE_COUNT,
                .operand_count = operand_count,
                .encoding_kind = ASSEMBLY_ENCODING_AARCH64_M1_GPR,
            };
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_fixed_instruction_lookup(Target target, String8 statement, AssemblyInstructionInfo* result)
{
    if (!result || !buster_aarch64_arm_m1_fixed_target(target))
    {
        return false;
    }
    BusterAarch64ArmM1FixedSpelling fixed = {0};
    if (!buster_aarch64_arm_m1_fixed_lookup(statement, &fixed) ||
        !buster_aarch64_arm_m1_fixed_supported_for_target(fixed, target))
    {
        return false;
    }
    *result = (AssemblyInstructionInfo){
        .opcode = ASSEMBLY_OPCODE_COUNT,
        .encoding_kind = ASSEMBLY_ENCODING_AARCH64_FIXED_WORD,
        .fixed_word = fixed.word,
    };
    return true;
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
    ASSEMBLY_VECTOR_FORM_VEX_ONLY = 1u << 8,
    ASSEMBLY_VECTOR_FORM_BROADCAST = 1u << 9,
    ASSEMBLY_VECTOR_FORM_SAE = 1u << 10,
    ASSEMBLY_VECTOR_FORM_AVX512DQ = 1u << 11,
} AssemblyVectorFormFlags;

typedef struct AssemblyVectorForm AssemblyVectorForm;
struct AssemblyVectorForm
{
    AssemblyOpcode opcode;
    u8 map;
    u8 mandatory_prefix;
    u8 opcode_byte;
    u8 element_width;
    u16 flags;
    bool wide;
};

BUSTER_GLOBAL_LOCAL AssemblyVectorForm const* assembly_x86_vector_form(AssemblyOpcode opcode)
{
    static AssemblyVectorForm const forms[] = {
        {ASSEMBLY_OPCODE_X86_VMOVAPS, 1, 0, 0x28, 4, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVUPS, 1, 0, 0x10, 4, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVAPD, 1, 1, 0x28, 8, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VMOVUPD, 1, 1, 0x10, 8, ASSEMBLY_VECTOR_FORM_MOVE, true},
        {ASSEMBLY_OPCODE_X86_VXORPS, 1, 0, 0x57, 4, ASSEMBLY_VECTOR_FORM_BROADCAST | ASSEMBLY_VECTOR_FORM_AVX512DQ, true},
        {ASSEMBLY_OPCODE_X86_VXORPD, 1, 1, 0x57, 8, ASSEMBLY_VECTOR_FORM_BROADCAST | ASSEMBLY_VECTOR_FORM_AVX512DQ, true},
        {ASSEMBLY_OPCODE_X86_VADDPS, 1, 0, 0x58, 4, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VADDPD, 1, 1, 0x58, 8, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VADDSS, 1, 2, 0x58, 4, ASSEMBLY_VECTOR_FORM_SCALAR | ASSEMBLY_VECTOR_FORM_ROUNDING, false},
        {ASSEMBLY_OPCODE_X86_VADDSD, 1, 3, 0x58, 8, ASSEMBLY_VECTOR_FORM_SCALAR | ASSEMBLY_VECTOR_FORM_ROUNDING, false},
        {ASSEMBLY_OPCODE_X86_VSUBPS, 1, 0, 0x5c, 4, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VSUBPD, 1, 1, 0x5c, 8, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VMULPS, 1, 0, 0x59, 4, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VMULPD, 1, 1, 0x59, 8, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VDIVPS, 1, 0, 0x5e, 4, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VDIVPD, 1, 1, 0x5e, 8, ASSEMBLY_VECTOR_FORM_ROUNDING | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA, 1, 1, 0x6f, 4,
         ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_VEX_ONLY, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU, 1, 2, 0x6f, 4,
         ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_VEX_ONLY, true},
        {ASSEMBLY_OPCODE_X86_VPADDB, 1, 1, 0xfc, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPADDW, 1, 1, 0xfd, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPADDD, 1, 1, 0xfe, 4, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPADDQ, 1, 1, 0xd4, 8, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPSUBB, 1, 1, 0xf8, 1, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPSUBW, 1, 1, 0xf9, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPSUBD, 1, 1, 0xfa, 4, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPSUBQ, 1, 1, 0xfb, 8, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPAND, 1, 1, 0xdb, 8, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_VEX_ONLY, true},
        {ASSEMBLY_OPCODE_X86_VPOR, 1, 1, 0xeb, 8, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_VEX_ONLY, true},
        {ASSEMBLY_OPCODE_X86_VPXOR, 1, 1, 0xef, 8, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_VEX_ONLY, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQB, 1, 1, 0x74, 1,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQW, 1, 1, 0x75, 2,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQD, 1, 1, 0x76, 4, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPEQQ, 2, 1, 0x29, 8, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTB, 1, 1, 0x64, 1,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTW, 1, 1, 0x65, 2,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTD, 1, 1, 0x66, 4, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPGTQ, 2, 1, 0x37, 8, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPMULLW, 1, 1, 0xd5, 2, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPMULLD, 2, 1, 0x40, 4, ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA32, 1, 1, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU32, 1, 2, 0x6f, 4, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQA64, 1, 1, 0x6f, 8, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU64, 1, 2, 0x6f, 8, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU8, 1, 3, 0x6f, 1, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VMOVDQU16, 1, 3, 0x6f, 2, ASSEMBLY_VECTOR_FORM_MOVE | ASSEMBLY_VECTOR_FORM_INTEGER | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VCMPPS, 1, 0, 0xc2, 4,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST |
             ASSEMBLY_VECTOR_FORM_SAE,
         true},
        {ASSEMBLY_OPCODE_X86_VCMPPD, 1, 1, 0xc2, 8,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST |
             ASSEMBLY_VECTOR_FORM_SAE,
         true},
        {ASSEMBLY_OPCODE_X86_VPCMPD, 3, 1, 0x1f, 4,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPQ, 3, 1, 0x1f, 8,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPB, 3, 1, 0x3f, 1, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPW, 3, 1, 0x3f, 2, ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_AVX512BW, true},
        {ASSEMBLY_OPCODE_X86_VPCMPUD, 3, 1, 0x1e, 4,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VPCMPUQ, 3, 1, 0x1e, 8,
         ASSEMBLY_VECTOR_FORM_MASK_DESTINATION | ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_BROADCAST, true},
        {ASSEMBLY_OPCODE_X86_VRNDSCALEPS, 3, 1, 0x08, 4,
         ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_SOURCE_RM | ASSEMBLY_VECTOR_FORM_BROADCAST |
             ASSEMBLY_VECTOR_FORM_SAE,
         true},
        {ASSEMBLY_OPCODE_X86_VRNDSCALEPD, 3, 1, 0x09, 8,
         ASSEMBLY_VECTOR_FORM_IMMEDIATE | ASSEMBLY_VECTOR_FORM_SOURCE_RM | ASSEMBLY_VECTOR_FORM_BROADCAST |
             ASSEMBLY_VECTOR_FORM_SAE,
         true},
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
           opcode == ASSEMBLY_OPCODE_X86_VPCMPUQ || opcode == ASSEMBLY_OPCODE_X86_VPCMPW ||
           opcode == ASSEMBLY_OPCODE_X86_VRNDSCALEPD;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_vector_register(AssemblyRegister reg)
{
    return reg.class == ASSEMBLY_REGISTER_XMM || reg.class == ASSEMBLY_REGISTER_YMM || reg.class == ASSEMBLY_REGISTER_ZMM;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_target_has_evex(Target target, u16 width, u8 scalar)
{
    u8 avx10 = target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) ||
               target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2);
    if (scalar)
    {
        return avx10 || target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F);
    }
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
             (operand.reg.class == ASSEMBLY_REGISTER_ZMM || operand.reg.class == ASSEMBLY_REGISTER_OPMASK || operand.reg.index >= 16)) || operand.has_mask || operand.zeroing ||
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
    if (opcode == ASSEMBLY_OPCODE_X86_SHL || opcode == ASSEMBLY_OPCODE_X86_SHR || opcode == ASSEMBLY_OPCODE_X86_SAR)
    {
        return count == 2 || count == 3;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return count >= 1 && count <= 3;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_ADC || opcode == ASSEMBLY_OPCODE_X86_SUB ||
        opcode == ASSEMBLY_OPCODE_X86_SBB || opcode == ASSEMBLY_OPCODE_X86_AND || opcode == ASSEMBLY_OPCODE_X86_OR ||
        opcode == ASSEMBLY_OPCODE_X86_XOR)
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
    return target_cpu_feature_has(target, assembly_x86_amx_feature(opcode));
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_shift(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_SHL || opcode == ASSEMBLY_OPCODE_X86_SHR || opcode == ASSEMBLY_OPCODE_X86_SAR;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_apx_ndd(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_ADC || opcode == ASSEMBLY_OPCODE_X86_SUB ||
           opcode == ASSEMBLY_OPCODE_X86_SBB || opcode == ASSEMBLY_OPCODE_X86_AND || opcode == ASSEMBLY_OPCODE_X86_OR ||
           opcode == ASSEMBLY_OPCODE_X86_XOR || opcode == ASSEMBLY_OPCODE_X86_IMUL || assembly_x86_opcode_is_shift(opcode);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_opcode_is_apx_nf(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ADD || opcode == ASSEMBLY_OPCODE_X86_SUB || opcode == ASSEMBLY_OPCODE_X86_AND ||
           opcode == ASSEMBLY_OPCODE_X86_OR || opcode == ASSEMBLY_OPCODE_X86_XOR || opcode == ASSEMBLY_OPCODE_X86_IMUL ||
           opcode == ASSEMBLY_OPCODE_X86_INC || opcode == ASSEMBLY_OPCODE_X86_DEC || opcode == ASSEMBLY_OPCODE_X86_NEG ||
           assembly_x86_opcode_is_shift(opcode);
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_apx_binary_opcode(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ADD ? 0x01
           : opcode == ASSEMBLY_OPCODE_X86_ADC ? 0x11
           : opcode == ASSEMBLY_OPCODE_X86_OR ? 0x09
           : opcode == ASSEMBLY_OPCODE_X86_SBB ? 0x19
           : opcode == ASSEMBLY_OPCODE_X86_AND ? 0x21
           : opcode == ASSEMBLY_OPCODE_X86_SUB ? 0x29
           : opcode == ASSEMBLY_OPCODE_X86_XOR ? 0x31
                                               : 0;
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_apx_binary_immediate_extension(AssemblyOpcode opcode)
{
    return opcode == ASSEMBLY_OPCODE_X86_ADD ? 0
           : opcode == ASSEMBLY_OPCODE_X86_OR ? 1
           : opcode == ASSEMBLY_OPCODE_X86_ADC ? 2
           : opcode == ASSEMBLY_OPCODE_X86_SBB ? 3
           : opcode == ASSEMBLY_OPCODE_X86_AND ? 4
           : opcode == ASSEMBLY_OPCODE_X86_SUB ? 5
           : opcode == ASSEMBLY_OPCODE_X86_XOR ? 6
                                               : 7;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_immediate_encoding(u16 width, s64 value, u8* opcode, u8* size)
{
    if (!assembly_x86_immediate_fits(value, width == 64 ? 32 : width, width == 64))
    {
        return false;
    }
    if (width == 8)
    {
        if (!assembly_x86_immediate_fits(value, 8, false))
        {
            return false;
        }
        *opcode = 0x80;
        *size = 1;
    }
    else if (value >= INT8_MIN && value <= INT8_MAX)
    {
        *opcode = 0x83;
        *size = 1;
    }
    else
    {
        *opcode = 0x81;
        *size = (u8)((width == 64 ? 32 : width) / 8);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_legacy_immediate_encoding(AssemblyOpcode opcode, u16 width, s64 value, u8* immediate_opcode,
                                                                     u8* immediate_size)
{
    if (opcode == ASSEMBLY_OPCODE_X86_TEST)
    {
        u16 full_width = width == 64 ? 32 : width;
        if (!assembly_x86_immediate_fits(value, full_width, width == 64))
        {
            return false;
        }
        *immediate_opcode = width == 8 ? 0xf6 : 0xf7;
        *immediate_size = (u8)(full_width / 8);
        return true;
    }
    return assembly_x86_apx_immediate_encoding(width, value, immediate_opcode, immediate_size);
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
            (first->has_mask && first->mask == 0) || first->zeroing || second->kind != ASSEMBLY_OPERAND_REGISTER ||
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
        if (immediate &&
            (instruction->operands[3].kind != ASSEMBLY_OPERAND_EXPRESSION || instruction->operands[3].expression.has_symbol ||
             instruction->operands[3].expression.addend < 0 || instruction->operands[3].expression.addend > UINT8_MAX))
        {
            return false;
        }
        u64 immediate_limit = instruction->opcode == ASSEMBLY_OPCODE_X86_VCMPPS || instruction->opcode == ASSEMBLY_OPCODE_X86_VCMPPD ? 31 : 7;
        if (immediate && (u64)instruction->operands[3].expression.addend > immediate_limit)
        {
            return false;
        }
    }
    else if (source_rm)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_vector_register(first->reg) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            instruction->operands[2].kind != ASSEMBLY_OPERAND_EXPRESSION || instruction->operands[2].expression.has_symbol ||
            instruction->operands[2].expression.addend < INT8_MIN || instruction->operands[2].expression.addend > UINT8_MAX)
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
        if (first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!move || !first->has_mask || first->mask == 0 || first->zeroing)
            {
                return false;
            }
        }
        else if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_vector_register(first->reg) ||
                 (first->has_mask && first->mask == 0) || (first->zeroing && !first->has_mask))
        {
            return false;
        }
    }
    for (u32 operand_index = 1; operand_index < instruction->operand_count; operand_index += 1)
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
        u8 supports_rounding = (form->flags & ASSEMBLY_VECTOR_FORM_ROUNDING) != 0;
        u8 supports_sae = (form->flags & ASSEMBLY_VECTOR_FORM_SAE) != 0;
        if ((first->rounding &&
             (!supports_rounding || move || memory || (!(form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) && vector.width != 512))) ||
            (first->sae && !first->rounding && (!supports_sae || move || memory || vector.width != 512)) ||
            (first->sae && first->rounding && !supports_rounding))
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
            if (!(form->flags & ASSEMBLY_VECTOR_FORM_BROADCAST) || move || (form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) ||
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_amd_vector_register_valid(AssemblyRegister reg, u8 vector_width_mask)
{
    return !reg.high_byte && reg.index < 16 &&
           ((reg.class == ASSEMBLY_REGISTER_XMM && (vector_width_mask & 1)) ||
            (reg.class == ASSEMBLY_REGISTER_YMM && (vector_width_mask & 2)));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amd_vector_rm_valid(AssemblyAmdForm const* form, AssemblyOperand* operand,
                                                          AssemblyRegister destination, u16 vector_width)
{
    if (operand->kind == ASSEMBLY_OPERAND_REGISTER)
    {
        return assembly_x86_amd_vector_register_valid(operand->reg, form->vector_width_mask) && operand->reg.class == destination.class &&
               operand->reg.width == vector_width;
    }
    if (operand->kind != ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    u16 memory_width = (form->flags & ASSEMBLY_AMD_FORM_SCALAR) ? (u16)form->element_bytes * 8u : vector_width;
    return assembly_x86_memory_set_width(operand, memory_width);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amd_immediate_valid(AssemblyOperand operand, u64 maximum)
{
    return operand.kind == ASSEMBLY_OPERAND_EXPRESSION && !operand.expression.has_symbol && operand.expression.addend >= 0 &&
           (u64)operand.expression.addend <= maximum;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amd_gpr_valid(AssemblyOperand operand, u16 width)
{
    return operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.class == ASSEMBLY_REGISTER_GPR && !operand.reg.high_byte &&
           operand.reg.index < 16 && operand.reg.width == width;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amd_instruction_size(AssemblyInstruction* instruction, AssemblyAmdForm const* form)
{
    form = assembly_x86_amd_form_select(instruction);
    instruction->amd_form = form;
    if (instruction->evex || instruction->lock_prefix || instruction->no_flags || instruction->operand_count != form->operand_count)
    {
        return false;
    }
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    u32 address_size = 1;
    switch (form->encoding)
    {
    case ASSEMBLY_AMD_ENCODING_FEMMS:
        instruction->size = 2;
        return instruction->operand_count == 0;
    case ASSEMBLY_AMD_ENCODING_3DNOW2:
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_MMX || first->reg.index >= 8 ||
            first->reg.width != 64 || (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER &&
             (second->reg.class != ASSEMBLY_REGISTER_MMX || second->reg.index >= 8 || second->reg.width != 64)))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY &&
            (!assembly_x86_memory_set_width(second, 64) || !assembly_x86_memory_encoding_size(second->memory, &address_size)))
        {
            return false;
        }
        instruction->rip_relocation_trailing = second->kind == ASSEMBLY_OPERAND_MEMORY;
        instruction->width = 64;
        instruction->size = (u32)(second->kind == ASSEMBLY_OPERAND_MEMORY && assembly_x86_memory_rex_needed(0, first->reg, second->memory)) +
                            2u + address_size + 1u;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR2:
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(first->reg, form->vector_width_mask))
        {
            return false;
        }
        u16 vector_width = first->reg.width;
        if ((form->flags & ASSEMBLY_AMD_FORM_SCALAR) && vector_width != 128)
        {
            return false;
        }
        if (!assembly_x86_amd_vector_rm_valid(form, second, first->reg, vector_width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->width = vector_width;
        instruction->size = 3u + 1u + address_size;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT:
    {
        AssemblyOperand* source_2 = instruction->operands + 1;
        AssemblyOperand* source_3 = instruction->operands + 2;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(first->reg, form->vector_width_mask))
        {
            return false;
        }
        u16 vector_width = first->reg.width;
        if (source_3->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!(form->flags & ASSEMBLY_AMD_FORM_ALLOW_W1) || source_2->kind != ASSEMBLY_OPERAND_REGISTER ||
                !assembly_x86_amd_vector_register_valid(source_2->reg, form->vector_width_mask) || source_2->reg.class != first->reg.class ||
                source_2->reg.width != vector_width || !assembly_x86_amd_vector_rm_valid(form, source_3, first->reg, vector_width))
            {
                return false;
            }
        }
        else if (source_3->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_3->reg, form->vector_width_mask) ||
                 source_3->reg.class != first->reg.class || source_3->reg.width != vector_width ||
                 !assembly_x86_amd_vector_rm_valid(form, source_2, first->reg, vector_width))
        {
            return false;
        }
        if (source_3->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source_3->memory, &address_size))
        {
            return false;
        }
        if (source_2->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source_2->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->width = vector_width;
        instruction->size = 3u + 1u + address_size;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE:
    {
        AssemblyOperand* immediate = instruction->operands + 2;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(first->reg, form->vector_width_mask) ||
            !assembly_x86_amd_immediate_valid(*immediate, UINT8_MAX) ||
            !assembly_x86_amd_vector_rm_valid(form, second, first->reg, first->reg.width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = second->kind == ASSEMBLY_OPERAND_MEMORY;
        instruction->width = first->reg.width;
        instruction->size = 3u + 1u + address_size + 1u;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE:
    {
        AssemblyOperand* source_1 = instruction->operands + 1;
        AssemblyOperand* source_2 = instruction->operands + 2;
        AssemblyOperand* immediate = instruction->operands + 3;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(first->reg, form->vector_width_mask) ||
            source_1->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_1->reg, form->vector_width_mask) ||
            source_1->reg.class != first->reg.class || source_1->reg.width != first->reg.width ||
            !assembly_x86_amd_immediate_valid(*immediate, UINT8_MAX) ||
            !assembly_x86_amd_vector_rm_valid(form, source_2, first->reg, first->reg.width))
        {
            return false;
        }
        if (source_2->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source_2->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = source_2->kind == ASSEMBLY_OPERAND_MEMORY;
        instruction->width = first->reg.width;
        instruction->size = 3u + 1u + address_size + 1u;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT:
    case ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE:
    case ASSEMBLY_AMD_ENCODING_VEX_FMA4:
    {
        AssemblyOperand* source_1 = instruction->operands + 1;
        AssemblyOperand* source_2 = instruction->operands + 2;
        AssemblyOperand* source_3 = instruction->operands + 3;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(first->reg, form->vector_width_mask) ||
            source_1->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_1->reg, form->vector_width_mask) ||
            source_1->reg.class != first->reg.class || source_1->reg.width != first->reg.width)
        {
            return false;
        }
        u16 vector_width = first->reg.width;
        if ((form->flags & ASSEMBLY_AMD_FORM_SCALAR) && vector_width != 128)
        {
            return false;
        }
        if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT)
        {
            if (source_3->kind == ASSEMBLY_OPERAND_MEMORY)
            {
                if (!(form->flags & ASSEMBLY_AMD_FORM_ALLOW_W1) || source_2->kind != ASSEMBLY_OPERAND_REGISTER ||
                    !assembly_x86_amd_vector_rm_valid(form, source_3, first->reg, vector_width))
                {
                    return false;
                }
            }
            else if (source_3->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_3->reg, form->vector_width_mask) ||
                     source_3->reg.class != first->reg.class || source_3->reg.width != vector_width ||
                     !assembly_x86_amd_vector_rm_valid(form, source_2, first->reg, vector_width))
            {
                return false;
            }
        }
        else if (source_2->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_amd_vector_rm_valid(form, source_2, first->reg, vector_width) || source_3->kind != ASSEMBLY_OPERAND_REGISTER ||
                !assembly_x86_amd_vector_register_valid(source_3->reg, form->vector_width_mask) || source_3->reg.class != first->reg.class ||
                source_3->reg.width != vector_width)
            {
                return false;
            }
        }
        else if (source_2->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_2->reg, form->vector_width_mask) ||
                 source_2->reg.class != first->reg.class || source_2->reg.width != vector_width)
        {
            return false;
        }
        else if (source_3->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (source_2->kind == ASSEMBLY_OPERAND_MEMORY || !assembly_x86_amd_vector_rm_valid(form, source_3, first->reg, vector_width))
            {
                return false;
            }
        }
        else if (source_3->kind != ASSEMBLY_OPERAND_REGISTER || !assembly_x86_amd_vector_register_valid(source_3->reg, form->vector_width_mask) ||
                 source_3->reg.class != first->reg.class || source_3->reg.width != vector_width)
        {
            return false;
        }
        if (form->encoding == ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE &&
            !assembly_x86_amd_immediate_valid(instruction->operands[4], 15))
        {
            return false;
        }
        if (source_2->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source_2->memory, &address_size))
        {
            return false;
        }
        if (source_3->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source_3->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = (u8)(source_2->kind == ASSEMBLY_OPERAND_MEMORY || source_3->kind == ASSEMBLY_OPERAND_MEMORY);
        instruction->width = vector_width;
        instruction->size = (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT ? 3u : 3u) + 1u + address_size + 1u;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_GPR2:
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte || first->reg.index >= 16 ||
            (first->reg.width != 32 && first->reg.width != 64) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        u16 width = first->reg.width;
        if (second->kind == ASSEMBLY_OPERAND_REGISTER && !assembly_x86_amd_gpr_valid(*second, width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY &&
            (!assembly_x86_memory_set_width(second, width) || !assembly_x86_memory_encoding_size(second->memory, &address_size)))
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->width = width;
        instruction->size = 3u + 1u + address_size;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_GPR3_BEXTR:
    {
        AssemblyOperand* immediate = instruction->operands + 2;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte || first->reg.index >= 16 ||
            (first->reg.width != 32 && first->reg.width != 64) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            !assembly_x86_amd_immediate_valid(*immediate, UINT32_MAX))
        {
            return false;
        }
        u16 width = first->reg.width;
        if (second->kind == ASSEMBLY_OPERAND_REGISTER && !assembly_x86_amd_gpr_valid(*second, width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY &&
            (!assembly_x86_memory_set_width(second, width) || !assembly_x86_memory_encoding_size(second->memory, &address_size)))
        {
            return false;
        }
        instruction->rip_relocation_trailing = second->kind == ASSEMBLY_OPERAND_MEMORY ? 4 : 0;
        instruction->width = width;
        instruction->size = 3u + 1u + address_size + 4u;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_LWP1:
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte || first->reg.index >= 16 ||
            first->reg.width != 64)
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->width = 64;
        instruction->size = 5;
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_LWP3:
    {
        AssemblyOperand* immediate = instruction->operands + 2;
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte || first->reg.index >= 16 ||
            first->reg.width != 64 || (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            !assembly_x86_amd_immediate_valid(*immediate, UINT32_MAX))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (!assembly_x86_amd_gpr_valid(*second, 32))
            {
                return false;
            }
        }
        else if (!assembly_x86_memory_set_width(second, 32) || !assembly_x86_memory_encoding_size(second->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = second->kind == ASSEMBLY_OPERAND_MEMORY ? 4 : 0;
        instruction->width = 64;
        instruction->size = 3u + 1u + address_size + 4u;
        return true;
    }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_amx_memory_size(AssemblyMemory memory, bool forced_sib, u32* result)
{
    if (forced_sib && memory.rip_relative)
    {
        return false;
    }
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
        instruction->operands[2].reg.class != ASSEMBLY_REGISTER_TILE || first->reg.index == second->reg.index ||
        first->reg.index == instruction->operands[2].reg.index || second->reg.index == instruction->operands[2].reg.index)
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
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_CALL || instruction->opcode == ASSEMBLY_OPCODE_X86_JMP)
    {
        if (instruction->operand_count != 1)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.width != 64 || first->reg.high_byte)
            {
                return false;
            }
            instruction->width = 64;
            instruction->size = 4;
            return true;
        }
        if (first->kind != ASSEMBLY_OPERAND_MEMORY || (first->memory.width && first->memory.width != 64))
        {
            return false;
        }
        first->memory.width = 64;
        u32 address_size = 1;
        if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->width = 64;
        instruction->size = 3u + address_size;
        return true;
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_LEA)
    {
        if (instruction->operand_count != 2 || first->kind != ASSEMBLY_OPERAND_REGISTER ||
            first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte ||
            (first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64) ||
            instruction->operands[1].kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        AssemblyMemory memory = instruction->operands[1].memory;
        u32 address_size = 1;
        if (!assembly_x86_memory_encoding_size(memory, &address_size))
        {
            return false;
        }
        instruction->width = first->reg.width;
        instruction->size = (u32)(instruction->width == 16) + 3u + address_size;
        return true;
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ||
        instruction->opcode == ASSEMBLY_OPCODE_X86_NEG || instruction->opcode == ASSEMBLY_OPCODE_X86_NOT)
    {
        if (instruction->operand_count != 1 ||
            (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            !assembly_x86_lock_prefix_legal(instruction))
        {
            return false;
        }
        u16 width = assembly_operand_width(*first);
        if (width != 8 && width != 16 && width != 32 && width != 64)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER &&
            (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte))
        {
            return false;
        }
        u32 address_size = 1;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY &&
            (!assembly_x86_memory_set_width(first, width) || !assembly_x86_memory_encoding_size(first->memory, &address_size)))
        {
            return false;
        }
        instruction->width = width;
        instruction->rip_relocation_trailing = 0;
        instruction->size = (instruction->lock_prefix ? 1u : 0u) + (u32)(width == 16) + 3u + address_size;
        return true;
    }
    if (assembly_x86_opcode_is_sse2(instruction->opcode))
    {
        u8 move = instruction->opcode >= ASSEMBLY_OPCODE_X86_MOVAPS && instruction->opcode <= ASSEMBLY_OPCODE_X86_MOVDQU;
        AssemblyOperand* second = instruction->operands + 1;
        if (instruction->operand_count != 2 ||
            (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY) ||
            (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY) ||
            (!move && first->kind != ASSEMBLY_OPERAND_REGISTER) ||
            (first->kind == ASSEMBLY_OPERAND_REGISTER &&
             (first->reg.class != ASSEMBLY_REGISTER_XMM || first->reg.index >= 16)) ||
            (second->kind == ASSEMBLY_OPERAND_REGISTER &&
             (second->reg.class != ASSEMBLY_REGISTER_XMM || second->reg.index >= 16)))
        {
            return false;
        }
        u16 memory_width = instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSS ? 32
                         : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSD ? 64
                                                                            : 128;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, memory_width))
        {
            return false;
        }
        if (second->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(second, memory_width))
        {
            return false;
        }
        AssemblyOperand* rm = !move || first->kind == ASSEMBLY_OPERAND_REGISTER ? second : first;
        u32 address_size = 1;
        if (rm->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(rm->memory, &address_size))
        {
            return false;
        }
        instruction->width = 128;
        u8 mandatory_prefix = instruction->opcode == ASSEMBLY_OPCODE_X86_MOVAPD || instruction->opcode == ASSEMBLY_OPCODE_X86_MOVUPD ||
                                       instruction->opcode == ASSEMBLY_OPCODE_X86_MOVDQA || instruction->opcode == ASSEMBLY_OPCODE_X86_XORPD ||
                                       instruction->opcode == ASSEMBLY_OPCODE_X86_PXOR || instruction->opcode == ASSEMBLY_OPCODE_X86_ADDPD ||
                                       instruction->opcode == ASSEMBLY_OPCODE_X86_SUBPD || instruction->opcode == ASSEMBLY_OPCODE_X86_MULPD ||
                                       instruction->opcode == ASSEMBLY_OPCODE_X86_DIVPD
                                   ? 1
                                   : instruction->opcode == ASSEMBLY_OPCODE_X86_MOVDQU || instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSS
                                       ? 1
                                       : instruction->opcode == ASSEMBLY_OPCODE_X86_ADDSD ? 1 : 0;
        instruction->size = (u32)mandatory_prefix + 2u + 1u + address_size;
        return true;
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        if (instruction->operand_count != 1 && instruction->operand_count != 2 && instruction->operand_count != 3)
        {
            return false;
        }
        if (instruction->operand_count == 1 && first->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            instruction->width = first->memory.width;
            if (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
            {
                return false;
            }
            u32 address_size = 1;
            if (!assembly_x86_memory_encoding_size(first->memory, &address_size))
            {
                return false;
            }
            instruction->size = (u32)(instruction->width == 16) + 3u + address_size;
            return true;
        }
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte ||
            (first->reg.width != 8 && first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64))
        {
            return false;
        }
        instruction->width = first->reg.width;
        if (instruction->width == 8 && instruction->operand_count != 1)
        {
            return false;
        }
        if (instruction->operand_count == 1)
        {
            instruction->size = (u32)(instruction->width == 16) + 4u;
            return true;
        }
        AssemblyOperand* source = instruction->operands + 1;
        if (source->kind != ASSEMBLY_OPERAND_REGISTER && source->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        if (source->kind == ASSEMBLY_OPERAND_REGISTER &&
            (source->reg.class != ASSEMBLY_REGISTER_GPR || source->reg.width != instruction->width || source->reg.high_byte))
        {
            return false;
        }
        if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(source, instruction->width))
        {
            return false;
        }
        u32 address_size = 1;
        if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source->memory, &address_size))
        {
            return false;
        }
        if (instruction->operand_count == 2)
        {
            instruction->rip_relocation_trailing = 0;
            instruction->size = (u32)(instruction->width == 16) + 3u + address_size;
            return true;
        }
        AssemblyOperand* immediate = instruction->operands + 2;
        u16 full_width = instruction->width == 64 ? 32 : instruction->width;
        if (immediate->kind != ASSEMBLY_OPERAND_EXPRESSION || immediate->expression.has_symbol ||
            !assembly_x86_immediate_fits(immediate->expression.addend, full_width, true))
        {
            return false;
        }
        u8 immediate_size = immediate->expression.addend >= INT8_MIN && immediate->expression.addend <= INT8_MAX
                                ? 1
                                : (u8)(full_width / 8);
        instruction->rip_relocation_trailing = source->kind == ASSEMBLY_OPERAND_MEMORY ? immediate_size : 0;
        instruction->size = (u32)(instruction->width == 16) + 3u + address_size + immediate_size;
        return true;
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        if (instruction->operand_count != 2 ||
            (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER &&
            (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte))
        {
            return false;
        }
        instruction->width = assembly_operand_width(*first);
        if (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
        {
            return false;
        }
        u8 immediate = instruction->operands[1].kind == ASSEMBLY_OPERAND_EXPRESSION;
        if (!immediate && !assembly_x86_count_is_cl(instruction->operands[1]))
        {
            return false;
        }
        if (immediate && (instruction->operands[1].expression.has_symbol || instruction->operands[1].expression.addend < 0 ||
                          instruction->operands[1].expression.addend > UINT8_MAX))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, instruction->width))
        {
            return false;
        }
        u32 address_size = 1;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->size = (u32)(instruction->width == 16) + 3u + address_size +
                            (u32)(immediate && instruction->operands[1].expression.addend != 1);
        return true;
    }
    if (instruction->opcode != ASSEMBLY_OPCODE_X86_MOV && instruction->opcode != ASSEMBLY_OPCODE_X86_ADD &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_ADC && instruction->opcode != ASSEMBLY_OPCODE_X86_SUB &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_SBB && instruction->opcode != ASSEMBLY_OPCODE_X86_AND &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_OR && instruction->opcode != ASSEMBLY_OPCODE_X86_XOR &&
        instruction->opcode != ASSEMBLY_OPCODE_X86_CMP && instruction->opcode != ASSEMBLY_OPCODE_X86_TEST)
    {
        return false;
    }
    AssemblyOperand* second = instruction->operands + 1;
    if (instruction->operand_count == 2 && second->kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_REGISTER)
        {
            if (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte)
            {
                return false;
            }
            instruction->width = first->reg.width;
        }
        else
        {
            instruction->width = first->memory.width;
        }
        if (instruction->width != 8 && instruction->width != 16 && instruction->width != 32 && instruction->width != 64)
        {
            return false;
        }
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        if (second->expression.has_symbol)
        {
            return false;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_MOV)
        {
            u8 register_immediate_64 = (u8)(first->kind == ASSEMBLY_OPERAND_REGISTER && instruction->width == 64 &&
                                            !assembly_x86_immediate_fits(second->expression.addend, 32, true));
            u16 immediate_width = register_immediate_64 ? 64 : instruction->width == 64 ? 32 : instruction->width;
            if (!assembly_x86_immediate_fits(second->expression.addend, immediate_width,
                                             instruction->width == 64))
            {
                return false;
            }
            immediate_opcode = first->kind == ASSEMBLY_OPERAND_MEMORY ? (instruction->width == 8 ? 0xc6 : 0xc7)
                                                                       : instruction->width == 8 ? 0xb0
                                                                                                  : register_immediate_64 ? 0xb8
                                                                                                                          : instruction->width == 64 ? 0xc7 : 0xb8;
            immediate_size = (u8)(immediate_width / 8);
            if (first->kind == ASSEMBLY_OPERAND_REGISTER && instruction->width == 64 && register_immediate_64)
            {
                immediate_opcode = 0xb8;
            }
        }
        else if (!assembly_x86_apx_legacy_immediate_encoding(instruction->opcode, instruction->width,
                                                             second->expression.addend, &immediate_opcode, &immediate_size))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, instruction->width))
        {
            return false;
        }
        u8 has_modrm = (u8)(instruction->opcode == ASSEMBLY_OPCODE_X86_MOV && first->kind == ASSEMBLY_OPERAND_REGISTER &&
                            instruction->width == 64 && immediate_size == 4);
        u32 address_size = first->kind == ASSEMBLY_OPERAND_MEMORY
                               ? 1
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_MOV ? 0 : 1;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = first->kind == ASSEMBLY_OPERAND_MEMORY ? immediate_size : 0;
        instruction->size = (instruction->lock_prefix ? 1u : 0u) + (u32)(instruction->width == 16) + 2u + 1u + has_modrm +
                            address_size + immediate_size;
        return true;
    }
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
    u32 lock_size = instruction->lock_prefix ? 1 : 0;
    instruction->size = lock_size + 2u + (instruction->width == 16) + 1u + address_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_imul_evex_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOperand* first = instruction->operands;
    if ((instruction->operand_count != 2 && instruction->operand_count != 3) || first->kind != ASSEMBLY_OPERAND_REGISTER ||
        first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte ||
        (first->reg.width != 16 && first->reg.width != 32 && first->reg.width != 64))
    {
        return false;
    }
    instruction->width = first->reg.width;
    AssemblyOperand* source = instruction->operands + 1;
    if (source->kind != ASSEMBLY_OPERAND_REGISTER && source->kind != ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    if (source->kind == ASSEMBLY_OPERAND_REGISTER &&
        (source->reg.class != ASSEMBLY_REGISTER_GPR || source->reg.width != instruction->width || source->reg.high_byte))
    {
        return false;
    }
    if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(source, instruction->width))
    {
        return false;
    }
    if (instruction->operand_count == 2)
    {
        if (!instruction->no_flags)
        {
            return false;
        }
        u32 address_size = 1;
        if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = 0;
        instruction->size = 5u + address_size;
        return true;
    }
    AssemblyOperand* third = instruction->operands + 2;
    if (third->kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u16 full_width = instruction->width == 64 ? 32 : instruction->width;
        if (third->expression.has_symbol || !assembly_x86_immediate_fits(third->expression.addend, full_width, true))
        {
            return false;
        }
        u8 immediate_size = third->expression.addend >= INT8_MIN && third->expression.addend <= INT8_MAX
                                ? 1
                                : (u8)(full_width / 8);
        u32 address_size = 1;
        if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source->memory, &address_size))
        {
            return false;
        }
        instruction->rip_relocation_trailing = source->kind == ASSEMBLY_OPERAND_MEMORY ? immediate_size : 0;
        instruction->size = 5u + address_size + immediate_size;
        return true;
    }
    if (source->kind != ASSEMBLY_OPERAND_REGISTER)
    {
        return false;
    }
    if (third->kind != ASSEMBLY_OPERAND_REGISTER && third->kind != ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    if (third->kind == ASSEMBLY_OPERAND_REGISTER &&
        (third->reg.class != ASSEMBLY_REGISTER_GPR || third->reg.width != instruction->width || third->reg.high_byte))
    {
        return false;
    }
    u32 address_size = 1;
    if (third->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (!assembly_x86_memory_set_width(third, instruction->width) ||
            !assembly_x86_memory_encoding_size(third->memory, &address_size))
        {
            return false;
        }
    }
    instruction->rip_relocation_trailing = 0;
    instruction->size = 5u + address_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_shift_evex_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOperand* first = instruction->operands;
    if ((instruction->operand_count != 2 && instruction->operand_count != 3) ||
        (instruction->operand_count == 2 && !instruction->no_flags))
    {
        return false;
    }
    if (instruction->operand_count == 2)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
    }
    else if (first->kind != ASSEMBLY_OPERAND_REGISTER || first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte)
    {
        return false;
    }
    u16 width = assembly_operand_width(*first);
    if (width != 8 && width != 16 && width != 32 && width != 64)
    {
        return false;
    }
    if (first->kind == ASSEMBLY_OPERAND_REGISTER && first->reg.class != ASSEMBLY_REGISTER_GPR)
    {
        return false;
    }
    instruction->width = width;
    AssemblyOperand* source = instruction->operand_count == 2 ? first : instruction->operands + 1;
    if (instruction->operand_count == 3 && source->kind != ASSEMBLY_OPERAND_REGISTER && source->kind != ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    if (source->kind == ASSEMBLY_OPERAND_REGISTER &&
        (source->reg.class != ASSEMBLY_REGISTER_GPR || source->reg.width != width || source->reg.high_byte))
    {
        return false;
    }
    if (source->kind == ASSEMBLY_OPERAND_MEMORY &&
        !assembly_x86_memory_set_width(source, width))
    {
        return false;
    }
    AssemblyOperand* count = instruction->operands + (instruction->operand_count == 2 ? 1 : 2);
    u8 immediate = count->kind == ASSEMBLY_OPERAND_EXPRESSION;
    if (!immediate && !assembly_x86_count_is_cl(*count))
    {
        return false;
    }
    if (immediate && (count->expression.has_symbol || count->expression.addend < 0 || count->expression.addend > UINT8_MAX))
    {
        return false;
    }
    u32 address_size = 1;
    if (source->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(source->memory, &address_size))
    {
        return false;
    }
    u8 trailing = (u8)(immediate && count->expression.addend != 1);
    instruction->rip_relocation_trailing = (u8)(source->kind == ASSEMBLY_OPERAND_MEMORY && trailing);
    instruction->size = 5u + address_size + trailing;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_ndd_instruction_size(AssemblyInstruction* instruction)
{
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return assembly_x86_apx_imul_evex_instruction_size(instruction);
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        return assembly_x86_apx_shift_evex_instruction_size(instruction);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_APX_PUSH2 || instruction->opcode == ASSEMBLY_OPCODE_X86_APX_POP2)
    {
        if (instruction->operand_count != 2 || instruction->operands[0].kind != ASSEMBLY_OPERAND_REGISTER ||
            instruction->operands[1].kind != ASSEMBLY_OPERAND_REGISTER || instruction->operands[0].reg.class != ASSEMBLY_REGISTER_GPR ||
            instruction->operands[1].reg.class != ASSEMBLY_REGISTER_GPR || instruction->operands[0].reg.width != 64 ||
            instruction->operands[1].reg.width != 64 || instruction->operands[0].reg.index == 4 || instruction->operands[1].reg.index == 4)
        {
            return false;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_APX_POP2 &&
            instruction->operands[0].reg.index == instruction->operands[1].reg.index)
        {
            return false;
        }
        instruction->size = 6;
        return true;
    }
    if (!assembly_x86_opcode_is_apx_ndd(instruction->opcode) || instruction->operand_count != 3)
    {
        return false;
    }
    AssemblyOperand* destination = instruction->operands;
    AssemblyOperand* source_1 = instruction->operands + 1;
    AssemblyOperand* source_2 = instruction->operands + 2;
    if (destination->kind != ASSEMBLY_OPERAND_REGISTER || destination->reg.class != ASSEMBLY_REGISTER_GPR ||
        source_1->kind == ASSEMBLY_OPERAND_EXPRESSION ||
        (source_1->kind != ASSEMBLY_OPERAND_REGISTER && source_1->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (source_2->kind != ASSEMBLY_OPERAND_REGISTER && source_2->kind != ASSEMBLY_OPERAND_MEMORY &&
         source_2->kind != ASSEMBLY_OPERAND_EXPRESSION) || destination->reg.high_byte)
    {
        return false;
    }
    if (source_1->kind == ASSEMBLY_OPERAND_MEMORY && source_2->kind == ASSEMBLY_OPERAND_MEMORY)
    {
        return false;
    }
    u16 width = destination->reg.width;
    if (width != 8 && width != 16 && width != 32 && width != 64)
    {
        return false;
    }
    if (source_1->kind == ASSEMBLY_OPERAND_REGISTER &&
        (source_1->reg.class != ASSEMBLY_REGISTER_GPR || source_1->reg.width != width || source_1->reg.high_byte))
    {
        return false;
    }
    if (source_2->kind == ASSEMBLY_OPERAND_REGISTER &&
        (source_2->reg.class != ASSEMBLY_REGISTER_GPR || source_2->reg.width != width || source_2->reg.high_byte))
    {
        return false;
    }
    if (source_1->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(source_1, width))
    {
        return false;
    }
    if (source_2->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(source_2, width))
    {
        return false;
    }
    instruction->width = width;
    instruction->rip_relocation_trailing = 0;
    if (source_2->kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        if (source_2->expression.has_symbol ||
            !assembly_x86_apx_immediate_encoding(width, source_2->expression.addend, &immediate_opcode, &immediate_size))
        {
            return false;
        }
        if (source_1->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            u32 address_size = 1;
            if (!assembly_x86_memory_encoding_size(source_1->memory, &address_size))
            {
                return false;
            }
            instruction->rip_relocation_trailing = immediate_size;
            instruction->size = 5u + address_size + immediate_size;
        }
        else
        {
            instruction->rip_relocation_trailing = 0;
            instruction->size = 6u + immediate_size;
        }
        return true;
    }
    AssemblyOperand* memory = source_1->kind == ASSEMBLY_OPERAND_MEMORY ? source_1
                              : source_2->kind == ASSEMBLY_OPERAND_MEMORY ? source_2
                                                                          : 0;
    u32 address_size = 1;
    if (memory && !assembly_x86_memory_encoding_size(memory->memory, &address_size))
    {
        return false;
    }
    instruction->size = 5u + address_size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_apx_nf_instruction_size(AssemblyInstruction* instruction)
{
    if (instruction->lock_prefix || !assembly_x86_opcode_is_apx_nf(instruction->opcode))
    {
        return false;
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return assembly_x86_apx_imul_evex_instruction_size(instruction);
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        return assembly_x86_apx_shift_evex_instruction_size(instruction);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ||
        instruction->opcode == ASSEMBLY_OPCODE_X86_NEG)
    {
        if (instruction->operand_count != 1 ||
            (instruction->operands[0].kind != ASSEMBLY_OPERAND_REGISTER && instruction->operands[0].kind != ASSEMBLY_OPERAND_MEMORY))
        {
            return false;
        }
        AssemblyOperand* operand = instruction->operands;
        u16 width = assembly_operand_width(*operand);
        if (width != 8 && width != 16 && width != 32 && width != 64)
        {
            return false;
        }
        if (operand->kind == ASSEMBLY_OPERAND_REGISTER &&
            (operand->reg.class != ASSEMBLY_REGISTER_GPR || operand->reg.high_byte))
        {
            return false;
        }
        u32 address_size = 1;
        if (operand->kind == ASSEMBLY_OPERAND_MEMORY &&
            (!assembly_x86_memory_set_width(operand, width) || !assembly_x86_memory_encoding_size(operand->memory, &address_size)))
        {
            return false;
        }
        instruction->width = width;
        instruction->rip_relocation_trailing = 0;
        instruction->size = 5u + address_size;
        return true;
    }
    if (instruction->operand_count != 2)
    {
        return false;
    }
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    if ((first->kind != ASSEMBLY_OPERAND_REGISTER && first->kind != ASSEMBLY_OPERAND_MEMORY) ||
        (second->kind != ASSEMBLY_OPERAND_REGISTER && second->kind != ASSEMBLY_OPERAND_MEMORY &&
         second->kind != ASSEMBLY_OPERAND_EXPRESSION) ||
        (first->kind == ASSEMBLY_OPERAND_MEMORY && second->kind == ASSEMBLY_OPERAND_MEMORY))
    {
        return false;
    }
    if (second->kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u16 width = assembly_operand_width(*first);
        if (first->kind == ASSEMBLY_OPERAND_REGISTER &&
            (first->reg.class != ASSEMBLY_REGISTER_GPR || first->reg.high_byte))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !width)
        {
            return false;
        }
        if (width != 8 && width != 16 && width != 32 && width != 64)
        {
            return false;
        }
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        if (second->expression.has_symbol ||
            !assembly_x86_apx_immediate_encoding(width, second->expression.addend, &immediate_opcode, &immediate_size))
        {
            return false;
        }
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_set_width(first, width))
        {
            return false;
        }
        u32 address_size = 1;
        if (first->kind == ASSEMBLY_OPERAND_MEMORY && !assembly_x86_memory_encoding_size(first->memory, &address_size))
        {
            return false;
        }
        instruction->width = width;
        instruction->rip_relocation_trailing = immediate_size;
        instruction->size = 5u + address_size + immediate_size;
        return true;
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
    instruction->size = (instruction->lock_prefix ? 1u : 0u) + 4u + 1u + address_size;
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
    if (instruction->no_flags && !assembly_x86_opcode_is_apx_nf(opcode))
    {
        return false;
    }
    if (instruction->amd_form)
    {
        return assembly_x86_amd_instruction_size(instruction, instruction->amd_form);
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
        (assembly_x86_opcode_is_apx_ndd(opcode) && instruction->operand_count == 3 &&
         !(opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION)))
    {
        return assembly_x86_apx_ndd_instruction_size(instruction);
    }
    if (instruction->no_flags && assembly_x86_opcode_is_apx_nf(opcode) &&
        (instruction->operand_count == 1 || instruction->operand_count == 2 ||
         (opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 3 &&
          instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION)))
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

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_scalar_condition(String8 text, u64* value)
{
    static char const* names[] = {
        "eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "nv",
    };
    static u8 values[] = {0, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    if (!value) return false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        if (assembly_word_equal(text, string_from_pointer((char8*)names[index])))
        {
            *value = values[index];
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_scalar_constant(AssemblyBuilder* builder, String8 text, u64* value)
{
    text = assembly_trim(text);
    if (text.length && text.pointer[0] == '#') text = assembly_trim(string_slice(text, 1, text.length));
    if (!text.length || !value) return false;
    if (assembly_parse_u64(text, value)) return true;
    u32 symbol_count = builder ? builder->result.symbol_count : 0;
    AssemblyExpression expression = {0};
    bool parsed = builder && assembly_expression_parse(builder, text, &expression);
    if (builder) builder->result.symbol_count = symbol_count;
    if (!parsed || expression.has_symbol || expression.addend < 0) return false;
    *value = expression.has_unsigned_addend ? expression.unsigned_addend : (u64)expression.addend;
    return !expression.has_unsigned_addend || expression.unsigned_addend <= UINT64_MAX;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_scalar_modifier(String8 text, A64ScalarIntModifier* result)
{
    text = assembly_trim(text);
    if (!text.length || !result) return false;
    u64 separator = 0;
    while (separator < text.length && !assembly_space(text.pointer[separator])) separator += 1;
    String8 name = string_slice(text, 0, separator);
    String8 amount_text = separator < text.length ? assembly_trim(string_slice(text, separator, text.length)) : (String8){0};
    u64 amount = 0;
    if (amount_text.length)
    {
        if (amount_text.pointer[0] != '#') return false;
        if (!assembly_aarch64_scalar_constant(0, amount_text, &amount)) return false;
    }
    static char const* shifts[] = {"lsl", "lsr", "asr", "ror"};
    for (u8 index = 0; index < BUSTER_ARRAY_LENGTH(shifts); index += 1)
    {
        if (assembly_word_equal(name, string_from_pointer((char8*)shifts[index])))
        {
            if (!amount_text.length) return false;
            *result = (A64ScalarIntModifier){.amount = amount, .kind = A64_SCALAR_INT_MODIFIER_SHIFT, .value = index, .present = true};
            return true;
        }
    }
    static char const* extends[] = {"uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx"};
    for (u8 index = 0; index < BUSTER_ARRAY_LENGTH(extends); index += 1)
    {
        if (assembly_word_equal(name, string_from_pointer((char8*)extends[index])))
        {
            *result = (A64ScalarIntModifier){.amount = amount, .kind = A64_SCALAR_INT_MODIFIER_EXTEND, .value = index, .present = true};
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_scalar_instruction_parse(AssemblyBuilder* builder, String8 mnemonic, String8 operands_text,
                                                                    AssemblyInstruction* instruction, u32 line, u32 column)
{
    String8 tokens[ASSEMBLY_MAX_OPERANDS] = {0};
    u32 token_count = 0;
    u64 cursor = 0;
    while (cursor < operands_text.length)
    {
        if (token_count >= BUSTER_ARRAY_LENGTH(tokens)) return false;
        String8 token = {0};
        if (assembly_operand_split_next(operands_text, &cursor, &token) != ASSEMBLY_OPERAND_SPLIT_SUCCESS) return false;
        tokens[token_count++] = token;
    }
    A64ScalarIntOperand parsed_operands[4] = {0};
    A64ScalarIntModifier parsed_modifiers[1] = {0};
    u32 operand_count = 0;
    u32 modifier_count = 0;
    bool is_condcmp = assembly_word_equal(mnemonic, S8("ccmn")) || assembly_word_equal(mnemonic, S8("ccmp"));
    for (u32 index = 0; index < token_count; index += 1)
    {
        String8 token = assembly_trim(tokens[index]);
        /* CCMP/CCMN's final field is a bare condition mnemonic, never a
           numeric immediate (e.g. '#0'). Handle it before register parsing
           so malformed final operands cannot be accepted as GPRs. */
        if (is_condcmp && index + 1 == token_count)
        {
            u64 condition = 0;
            if (!assembly_aarch64_scalar_condition(token, &condition) || operand_count >= BUSTER_ARRAY_LENGTH(parsed_operands)) return false;
            parsed_operands[operand_count++] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = condition};
            continue;
        }
        AssemblyRegister reg = {0};
        if (assembly_aarch64_gpr_register_parse(token, &reg))
        {
            if (operand_count >= BUSTER_ARRAY_LENGTH(parsed_operands)) return false;
            parsed_operands[operand_count++] = (A64ScalarIntOperand){
                .kind = A64_SCALAR_INT_OPERAND_REGISTER,
                .width = (u8)reg.width,
                .index = reg.index,
                .stack_pointer = reg.stack_pointer,
            };
            continue;
        }
        A64ScalarIntModifier modifier = {0};
        if (assembly_aarch64_scalar_modifier(token, &modifier))
        {
            if (index + 1 != token_count) return false;
            if (modifier_count) return false;
            parsed_modifiers[modifier_count++] = modifier;
            continue;
        }
        u64 value = 0;
        if (!token.length || token.pointer[0] != '#' || !assembly_aarch64_scalar_constant(builder, token, &value) ||
            operand_count >= BUSTER_ARRAY_LENGTH(parsed_operands))
        {
            return false;
        }
        parsed_operands[operand_count++] = (A64ScalarIntOperand){.kind = A64_SCALAR_INT_OPERAND_IMMEDIATE, .value = value};
    }
    u32 form_index = UINT32_MAX;
    if (!buster_aarch64_arm_m1_scalar_integer_find_form(mnemonic, parsed_operands, operand_count, parsed_modifiers, modifier_count, &form_index))
    {
        return false;
    }
    BusterAarch64ArmM1ScalarIntegerForm form = {0};
    if (!buster_aarch64_arm_m1_scalar_integer_form(form_index, &form)) return false;
    if (form.required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(builder->target, form.required_feature))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                            S8("instruction requires an enabled AArch64 target feature"));
        return false;
    }
    instruction->aarch64_scalar_integer_form_index = form_index;
    instruction->aarch64_scalar_integer_operand_count = (u8)operand_count;
    instruction->aarch64_scalar_integer_modifier_count = (u8)modifier_count;
    memcpy(instruction->aarch64_scalar_integer_operands, parsed_operands,
           sizeof(*parsed_operands) * operand_count);
    memcpy(instruction->aarch64_scalar_integer_modifiers, parsed_modifiers,
           sizeof(*parsed_modifiers) * modifier_count);
    instruction->operand_count = (u8)operand_count;
    instruction->size = 4;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_pc_operand(AssemblyBuilder* builder, String8 text,
                                                              BusterAarch64ControlOperandValue* value,
                                                              AssemblyExpression* expression)
{
    if (!builder || !value || !expression || !assembly_expression_parse(builder, assembly_trim(text), expression))
    {
        return false;
    }
    *value = (BusterAarch64ControlOperandValue){
        .kind = BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE,
        .width = 64,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_register_operand(String8 text, BusterAarch64ControlOperandValue* value)
{
    AssemblyRegister reg = {0};
    if (!value || !assembly_aarch64_gpr_register_parse(assembly_trim(text), &reg))
    {
        return false;
    }
    *value = (BusterAarch64ControlOperandValue){
        .value = reg.index,
        .kind = BUSTER_AARCH64_CONTROL_OPERAND_REGISTER,
        .width = (u8)reg.width,
        .register31_role = reg.index == 31 ? (reg.stack_pointer ? BUSTER_AARCH64_CONTROL_REGISTER31_SP
                                                                  : BUSTER_AARCH64_CONTROL_REGISTER31_ZR)
                                          : BUSTER_AARCH64_CONTROL_REGISTER31_NONE,
        .register_class = BUSTER_AARCH64_CONTROL_REGISTER_CLASS_GPR,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_immediate(AssemblyBuilder* builder, String8 text, u8 width,
                                                             BusterAarch64ControlOperandValue* value)
{
    u64 immediate = 0;
    text = assembly_trim(text);
    if (!text.length || text.pointer[0] != '#' || !assembly_aarch64_scalar_constant(builder, text, &immediate) ||
        immediate > (u64)INT64_MAX || !value)
    {
        return false;
    }
    *value = (BusterAarch64ControlOperandValue){
        .value = (s64)immediate,
        .kind = BUSTER_AARCH64_CONTROL_OPERAND_IMMEDIATE,
        .width = width,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_row_select(String8 mnemonic, BusterAarch64ControlInstruction candidate,
                                                              u32* row_index)
{
    if (!row_index)
    {
        return false;
    }
    bool conditional = mnemonic.length > 2 && assembly_word_equal(string_slice(mnemonic, 0, 2), S8("b."));
    for (u32 index = 0; index < buster_aarch64_control_semantic_count(); index += 1)
    {
        BusterAarch64ControlSemanticRecord row = {0};
        String8 row_mnemonic = {0};
        if (!buster_aarch64_control_semantic_row(index, &row) ||
            !buster_aarch64_control_semantic_string(row.mnemonic, &row_mnemonic))
        {
            continue;
        }
        if (row.form == BUSTER_AARCH64_CONTROL_FORM_B || row.form == BUSTER_AARCH64_CONTROL_FORM_BL ||
            row.form == BUSTER_AARCH64_CONTROL_FORM_RET)
        {
            continue;
        }
        if ((conditional && row.form != BUSTER_AARCH64_CONTROL_FORM_B_COND) ||
            (!conditional && !assembly_word_equal(mnemonic, row_mnemonic)))
        {
            continue;
        }
        candidate.row = (u16)index;
        u32 ignored_word = 0;
        if (buster_aarch64_control_semantic_encode(&candidate, &ignored_word))
        {
            *row_index = index;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_control_instruction_parse(AssemblyBuilder* builder, String8 mnemonic,
                                                                     String8 operands_text, AssemblyInstruction* instruction)
{
    if (!builder || !instruction)
    {
        return false;
    }
    String8 tokens[ASSEMBLY_MAX_OPERANDS] = {0};
    u32 token_count = 0;
    u64 cursor = 0;
    while (cursor < operands_text.length)
    {
        if (token_count >= BUSTER_ARRAY_LENGTH(tokens))
        {
            return false;
        }
        String8 token = {0};
        if (assembly_operand_split_next(operands_text, &cursor, &token) != ASSEMBLY_OPERAND_SPLIT_SUCCESS)
        {
            return false;
        }
        tokens[token_count++] = assembly_trim(token);
    }
    bool conditional = mnemonic.length > 2 && assembly_word_equal(string_slice(mnemonic, 0, 2), S8("b."));
    u64 condition = 0;
    BusterAarch64ControlInstruction candidate = {0};
    u8 expression_mask = 0;
    AssemblyExpression expressions[4] = {0};
    if (conditional)
    {
        if (token_count != 1 || !assembly_aarch64_control_condition_parse(string_slice(mnemonic, 2, mnemonic.length), &condition))
        {
            return false;
        }
        candidate.operand_count = 2;
        candidate.operands[0] = (BusterAarch64ControlOperandValue){
            .kind = BUSTER_AARCH64_CONTROL_OPERAND_PC_RELATIVE,
            .width = 64,
        };
        candidate.operands[1].kind = BUSTER_AARCH64_CONTROL_OPERAND_CONDITION;
        candidate.operands[1].width = 4;
        candidate.operands[1].value = (s64)condition;
        if (!assembly_aarch64_control_pc_operand(builder, tokens[0], &candidate.operands[0], &expressions[0]))
        {
            return false;
        }
        expression_mask = 1;
    }
    else if (assembly_word_equal(mnemonic, S8("adr")) || assembly_word_equal(mnemonic, S8("adrp")))
    {
        if (token_count != 2 || !assembly_aarch64_control_register_operand(tokens[0], &candidate.operands[0]) ||
            !assembly_aarch64_control_pc_operand(builder, tokens[1], &candidate.operands[1], &expressions[1]))
        {
            return false;
        }
        candidate.operand_count = 2;
        expression_mask = 2;
    }
    else if (assembly_word_equal(mnemonic, S8("cbz")) || assembly_word_equal(mnemonic, S8("cbnz")))
    {
        if (token_count != 2 || !assembly_aarch64_control_register_operand(tokens[0], &candidate.operands[0]) ||
            !assembly_aarch64_control_pc_operand(builder, tokens[1], &candidate.operands[1], &expressions[1]))
        {
            return false;
        }
        candidate.operand_count = 2;
        expression_mask = 2;
    }
    else if (assembly_word_equal(mnemonic, S8("tbz")) || assembly_word_equal(mnemonic, S8("tbnz")))
    {
        if (token_count != 3 || !assembly_aarch64_control_register_operand(tokens[0], &candidate.operands[0]) ||
            !assembly_aarch64_control_immediate(builder, tokens[1], 6, &candidate.operands[1]) ||
            !assembly_aarch64_control_pc_operand(builder, tokens[2], &candidate.operands[2], &expressions[2]))
        {
            return false;
        }
        candidate.operand_count = 3;
        expression_mask = 4;
    }
    else if (assembly_word_equal(mnemonic, S8("csel")) || assembly_word_equal(mnemonic, S8("csinc")) ||
             assembly_word_equal(mnemonic, S8("csinv")) || assembly_word_equal(mnemonic, S8("csneg")))
    {
        if (token_count != 4 || !assembly_aarch64_control_register_operand(tokens[0], &candidate.operands[0]) ||
            !assembly_aarch64_control_register_operand(tokens[1], &candidate.operands[1]) ||
            !assembly_aarch64_control_register_operand(tokens[2], &candidate.operands[2]) ||
            !assembly_aarch64_control_condition_parse(tokens[3], &condition))
        {
            return false;
        }
        candidate.operand_count = 4;
        candidate.operands[3].value = (s64)condition;
        candidate.operands[3].kind = BUSTER_AARCH64_CONTROL_OPERAND_CONDITION;
        candidate.operands[3].width = 4;
    }
    else if (assembly_word_equal(mnemonic, S8("ldr")) || assembly_word_equal(mnemonic, S8("ldrsw")))
    {
        if (token_count != 2 || !assembly_aarch64_control_register_operand(tokens[0], &candidate.operands[0]) ||
            !assembly_aarch64_control_pc_operand(builder, tokens[1], &candidate.operands[1], &expressions[1]))
        {
            return false;
        }
        candidate.operand_count = 2;
        expression_mask = 2;
    }
    else if (assembly_word_equal(mnemonic, S8("prfm")))
    {
        if (token_count != 2 || !assembly_aarch64_control_immediate(builder, tokens[0], 5, &candidate.operands[0]) ||
            !assembly_aarch64_control_pc_operand(builder, tokens[1], &candidate.operands[1], &expressions[1]))
        {
            return false;
        }
        candidate.operand_count = 2;
        expression_mask = 2;
    }
    else
    {
        return false;
    }
    u32 row_index = UINT32_MAX;
    if (!assembly_aarch64_control_row_select(mnemonic, candidate, &row_index))
    {
        return false;
    }
    candidate.row = (u16)row_index;
    instruction->aarch64_control_instruction = candidate;
    instruction->aarch64_control_row_index = row_index;
    instruction->aarch64_control_expression_mask = expression_mask;
    memcpy(instruction->aarch64_control_expressions, expressions, sizeof(expressions));
    instruction->operand_count = candidate.operand_count;
    instruction->size = 4;
    return true;
}

BUSTER_GLOBAL_LOCAL void assembly_instruction_parse_handwritten(AssemblyBuilder* builder, String8 statement, u32 line, u32 column,
                                                                 u64 offset, Target target, AssemblySyntax syntax)
{
    String8 full_statement = assembly_trim(statement);
    u8 lock_prefix = false;
    u64 lock_end = 0;
    while (lock_end < statement.length && !assembly_space(statement.pointer[lock_end]))
    {
        lock_end += 1;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && lock_end < statement.length &&
        assembly_word_equal(string_slice(statement, 0, lock_end), S8("lock")))
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
    AssemblyInstructionInfo info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    bool fixed_spelling = false;
    if (!assembly_instruction_lookup(target, syntax, mnemonic, &info))
    {
        fixed_spelling = assembly_aarch64_fixed_instruction_lookup(target, full_statement, &info);
        if (!fixed_spelling)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION, line, column, (u32)mnemonic.length, S8("unknown instruction"));
            return;
        }
    }
    if (fixed_spelling)
    {
        // A fixed spelling is matched as one complete instruction, including
        // the sole internal separator in `TSB CSYNC`; it has no operands.
        mnemonic_end = statement.length;
        mnemonic = full_statement;
        operands = (String8){0};
    }
    if (info.amd_form && !target_cpu_feature_has(target, info.amd_form->feature))
    {
        String8 feature_message = info.amd_form->feature == TARGET_CPU_FEATURE_X86_FMA4  ? S8("instruction requires the fma4 target feature")
                              : info.amd_form->feature == TARGET_CPU_FEATURE_X86_TBM    ? S8("instruction requires the tbm target feature")
                              : info.amd_form->feature == TARGET_CPU_FEATURE_X86_3DNOWA ? S8("instruction requires the 3dnowa target feature")
                              : info.amd_form->feature == TARGET_CPU_FEATURE_X86_3DNOW  ? S8("instruction requires the 3dnow target feature")
                              : info.amd_form->feature == TARGET_CPU_FEATURE_X86_LWP    ? S8("instruction requires the lwp target feature")
                              : info.amd_form->feature == TARGET_CPU_FEATURE_X86_XOP    ? S8("instruction requires the xop target feature")
                                                                                       : S8("unsupported AMD instruction feature");
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length, feature_message);
        return;
    }
    u8 no_flags_source_count = (u8)pseudo_no_flags + (u8)info.no_flags;
    if (no_flags_source_count > 1)
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column, (u32)mnemonic.length,
                            S8("duplicate APX no-flags decorator"));
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
        .encoding_kind = info.encoding_kind,
        .operand_count = info.operand_count,
        .condition = info.condition,
        .lock_prefix = lock_prefix,
        .no_flags = pseudo_no_flags || info.no_flags,
        .amd_mnemonic = mnemonic,
        .amd_form = info.amd_form,
        .fixed_word = info.fixed_word,
    };
    if (instruction.encoding_kind == ASSEMBLY_ENCODING_AARCH64_M1_SCALAR_INTEGER)
    {
        if (!assembly_aarch64_scalar_instruction_parse(builder, mnemonic, operands, &instruction, line, column))
        {
            if (!builder->result.diagnostic_count || builder->result.diagnostics[builder->result.diagnostic_count - 1].line != line ||
                builder->result.diagnostics[builder->result.diagnostic_count - 1].column != column)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("invalid instruction operands"));
            }
            return;
        }
        builder->instructions[builder->instruction_count++] = instruction;
        return;
    }
    if (instruction.encoding_kind == ASSEMBLY_ENCODING_AARCH64_CONTROL)
    {
        if (!assembly_aarch64_control_instruction_parse(builder, mnemonic, operands, &instruction))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid AArch64 control instruction operands"));
            return;
        }
        builder->instructions[builder->instruction_count++] = instruction;
        return;
    }
    u8 parsed_operand_count = 0;
    u64 operand_start = 0;
    while (operand_start < operands.length && parsed_operand_count < BUSTER_ARRAY_LENGTH(instruction.operands))
    {
        u64 next_operand_start = operand_start;
        String8 text = {0};
        if (assembly_operand_split_next(operands, &next_operand_start, &text) != ASSEMBLY_OPERAND_SPLIT_SUCCESS)
        {
            break;
        }
        if (text.length >= 3 && text.pointer[0] == '{' && text.pointer[text.length - 1] == '}')
        {
            String8 decorator = assembly_trim(string_slice(text, 1, text.length - 1));
            u8 pseudo_rounding = assembly_word_equal(decorator, S8("rn-sae")) ? 1
                               : assembly_word_equal(decorator, S8("rd-sae")) ? 2
                               : assembly_word_equal(decorator, S8("ru-sae")) ? 3
                               : assembly_word_equal(decorator, S8("rz-sae")) ? 4
                                                                              : 0;
            u8 pseudo_sae = assembly_word_equal(decorator, S8("sae")) || pseudo_rounding != 0;
            if (pseudo_sae)
            {
                AssemblyVectorForm const* pseudo_form = assembly_x86_vector_form(info.opcode);
                u8 immediate_form = pseudo_form && (pseudo_form->flags & ASSEMBLY_VECTOR_FORM_IMMEDIATE);
                u8 canonical_position = syntax == ASSEMBLY_SYNTAX_INTEL
                                             ? (immediate_form ? parsed_operand_count + 1 == info.operand_count
                                                               : parsed_operand_count == info.operand_count)
                                             : (immediate_form && parsed_operand_count == 1);
                if (!canonical_position || leading_sae)
                {
                    break;
                }
                leading_rounding = pseudo_rounding;
                leading_sae = true;
                operand_start = next_operand_start;
                continue;
            }
        }
        AssemblyOperand* operand = instruction.operands + parsed_operand_count;
        u8 branch = info.opcode == ASSEMBLY_OPCODE_X86_CALL || info.opcode == ASSEMBLY_OPCODE_X86_JMP || info.opcode == ASSEMBLY_OPCODE_X86_JCC;
        u8 indirect = syntax == ASSEMBLY_SYNTAX_ATT && branch && text.length && text.pointer[0] == '*';
        if (indirect)
        {
            text.pointer += 1;
            text.length -= 1;
        }
        if (!assembly_x86_operand_decorators_parse(&text, syntax, operand, 0))
        {
            break;
        }
        bool att_immediate = syntax == ASSEMBLY_SYNTAX_ATT && text.length && text.pointer[0] == '$';
        if (att_immediate)
        {
            text.pointer += 1;
            text.length -= 1;
        }
        if (target.cpu_arch == CPU_ARCH_AARCH64 && instruction.encoding_kind == ASSEMBLY_ENCODING_AARCH64_M1_GPR)
        {
            if (!assembly_aarch64_gpr_register_parse(text, &operand->reg))
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_REGISTER;
        }
        else if (target.cpu_arch == CPU_ARCH_X86_64 && !att_immediate && assembly_register_parse(text, syntax, &operand->reg))
        {
            if (syntax == ASSEMBLY_SYNTAX_ATT && branch && !indirect)
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_REGISTER;
        }
        else if (target.cpu_arch == CPU_ARCH_X86_64 && !att_immediate && assembly_x86_memory_parse(builder, text, syntax, &operand->memory) &&
                 !(branch && syntax == ASSEMBLY_SYNTAX_ATT && !indirect && operand->memory.absolute))
        {
            if (branch && syntax == ASSEMBLY_SYNTAX_ATT && !indirect)
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_MEMORY;
        }
        else if (att_immediate)
        {
            if (!text.length || !assembly_expression_parse(builder, text, &operand->expression))
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_EXPRESSION;
        }
        else
        {
            if (syntax != ASSEMBLY_SYNTAX_ATT && text.length && text.pointer[0] == '$')
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
        operand_start = next_operand_start;
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
    if (target.cpu_arch == CPU_ARCH_AARCH64 && instruction.encoding_kind == ASSEMBLY_ENCODING_AARCH64_M1_GPR)
    {
        A64GprOperand gpr_operands[4] = {0};
        if (parsed_operand_count > BUSTER_ARRAY_LENGTH(gpr_operands))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid instruction operands"));
            return;
        }
        for (u32 operand_index = 0; operand_index < parsed_operand_count; operand_index += 1)
        {
            AssemblyOperand operand = instruction.operands[operand_index];
            if (operand.kind != ASSEMBLY_OPERAND_REGISTER || operand.reg.class != ASSEMBLY_REGISTER_GPR)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("invalid instruction operands"));
                return;
            }
            gpr_operands[operand_index] = (A64GprOperand){
                .index = operand.reg.index,
                .width = (u8)operand.reg.width,
                .stack_pointer = operand.reg.stack_pointer,
            };
        }
        u32 form_index = UINT32_MAX;
        if (!buster_aarch64_arm_m1_gpr_find_form(mnemonic, gpr_operands, parsed_operand_count, &form_index))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid instruction operands"));
            return;
        }
        BusterAarch64ArmM1GprForm form = {0};
        if (!buster_aarch64_arm_m1_gpr_form(form_index, &form))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid instruction operands"));
            return;
        }
        if (form.required_feature != TARGET_CPU_FEATURE_NONE && !target_cpu_feature_has(target, form.required_feature))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                S8("instruction requires an enabled AArch64 target feature"));
            return;
        }
        instruction.aarch64_gpr_form_index = form_index;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && info.amd_form && syntax == ASSEMBLY_SYNTAX_ATT)
    {
        for (u32 left = 0; left < parsed_operand_count / 2; left += 1)
        {
            AssemblyOperand temporary = instruction.operands[left];
            u32 right = parsed_operand_count - 1 - left;
            instruction.operands[left] = instruction.operands[right];
            instruction.operands[right] = temporary;
        }
    }
    else if (target.cpu_arch == CPU_ARCH_X86_64 && syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 2)
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
        if (syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 4 && !info.amd_form)
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
            AssemblyVectorForm const* leading_form = assembly_x86_vector_form(instruction.opcode);
            u8 leading_mask_destination = leading_form && (leading_form->flags & ASSEMBLY_VECTOR_FORM_MASK_DESTINATION);
            u8 leading_register_valid = instruction.operand_count && instruction.operands[0].kind == ASSEMBLY_OPERAND_REGISTER &&
                                        ((leading_mask_destination && instruction.operands[0].reg.class == ASSEMBLY_REGISTER_OPMASK) ||
                                         (!leading_mask_destination && assembly_x86_vector_register(instruction.operands[0].reg)));
            if (!leading_register_valid || instruction.operands[0].rounding || instruction.operands[0].sae)
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
            else if (assembly_x86_opcode_is_shift(instruction.opcode))
            {
                suffix_applies = operand_index + 1 < instruction.operand_count;
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
        if (vector_form && instruction.evex && (vector_form->flags & ASSEMBLY_VECTOR_FORM_VEX_ONLY))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("the mnemonic has no EVEX form"));
            return;
        }
        if (vector_form && instruction.evex)
        {
            u16 vector_width = assembly_x86_instruction_vector_width(instruction, vector_form);
            if (!assembly_x86_target_has_evex(target, vector_width, (u8)((vector_form->flags & ASSEMBLY_VECTOR_FORM_SCALAR) != 0)))
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
            if ((vector_form->flags & ASSEMBLY_VECTOR_FORM_AVX512DQ) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512DQ) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_1) &&
                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX10_2))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, line, column, (u32)mnemonic.length,
                                    S8("instruction requires avx512dq or avx10"));
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
             (assembly_x86_opcode_is_apx_ndd(instruction.opcode) && instruction.operand_count == 3 &&
              !(instruction.opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction.operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION))) &&
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
        if (instruction.encoding_kind != ASSEMBLY_ENCODING_AARCH64_M1_GPR &&
            instruction.encoding_kind != ASSEMBLY_ENCODING_AARCH64_CONTROL && instruction.operand_count &&
            instruction.operands[0].kind != ASSEMBLY_OPERAND_EXPRESSION)
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid AArch64 operand form"));
            return;
        }
    }
    builder->instructions[builder->instruction_count++] = instruction;
}

BUSTER_GLOBAL_LOCAL u32 assembly_x86_metadata_feature_names(Target target, String8* names, u32 capacity)
{
    static TargetCpuFeature const x86_feature_bits[] = {
        TARGET_CPU_FEATURE_X86_SSE2,
        TARGET_CPU_FEATURE_X86_AVX,
        TARGET_CPU_FEATURE_X86_AVX2,
        TARGET_CPU_FEATURE_X86_AVX512F,
        TARGET_CPU_FEATURE_X86_AVX512VL,
        TARGET_CPU_FEATURE_X86_AVX10_1,
        TARGET_CPU_FEATURE_X86_AVX10_2,
        TARGET_CPU_FEATURE_X86_AVX10_512,
        TARGET_CPU_FEATURE_X86_APX,
        TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_SSE3,
        TARGET_CPU_FEATURE_X86_SSSE3,
        TARGET_CPU_FEATURE_X86_SSE4_1,
        TARGET_CPU_FEATURE_X86_SSE4_2,
        TARGET_CPU_FEATURE_X86_SSE4A,
        TARGET_CPU_FEATURE_X86_F16C,
        TARGET_CPU_FEATURE_X86_FMA,
        TARGET_CPU_FEATURE_X86_POPCNT,
        TARGET_CPU_FEATURE_X86_LZCNT,
        TARGET_CPU_FEATURE_X86_BMI1,
        TARGET_CPU_FEATURE_X86_BMI2,
        TARGET_CPU_FEATURE_X86_ADX,
        TARGET_CPU_FEATURE_X86_MOVBE,
        TARGET_CPU_FEATURE_X86_RDRAND,
        TARGET_CPU_FEATURE_X86_RDSEED,
        TARGET_CPU_FEATURE_X86_WAITPKG,
        TARGET_CPU_FEATURE_X86_PKU,
        TARGET_CPU_FEATURE_X86_PTWRITE,
        TARGET_CPU_FEATURE_X86_SERIALIZE,
        TARGET_CPU_FEATURE_X86_CLFLUSHOPT,
        TARGET_CPU_FEATURE_X86_CLWB,
        TARGET_CPU_FEATURE_X86_FSGSBASE,
        TARGET_CPU_FEATURE_X86_RTM,
        TARGET_CPU_FEATURE_X86_TSXLDTRK,
        TARGET_CPU_FEATURE_X86_UINTR,
        TARGET_CPU_FEATURE_X86_PREFETCHWT1,
        TARGET_CPU_FEATURE_X86_CX16,
        TARGET_CPU_FEATURE_X86_AVX512CD,
        TARGET_CPU_FEATURE_X86_AVX512DQ,
        TARGET_CPU_FEATURE_X86_AVX512IFMA,
        TARGET_CPU_FEATURE_X86_AVX512PF,
        TARGET_CPU_FEATURE_X86_AVX512ER,
        TARGET_CPU_FEATURE_X86_AVX512VBMI,
        TARGET_CPU_FEATURE_X86_AVX512VBMI2,
        TARGET_CPU_FEATURE_X86_AVX512VNNI,
        TARGET_CPU_FEATURE_X86_AVX512BITALG,
        TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
        TARGET_CPU_FEATURE_X86_AVX5124VNNIW,
        TARGET_CPU_FEATURE_X86_AVX5124FMAPS,
        TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
        TARGET_CPU_FEATURE_X86_AVX512BF16,
        TARGET_CPU_FEATURE_X86_AVX512FP16,
        TARGET_CPU_FEATURE_X86_GFNI,
        TARGET_CPU_FEATURE_X86_VAES,
        TARGET_CPU_FEATURE_X86_VPCLMULQDQ,
        TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_PCLMUL,
        TARGET_CPU_FEATURE_X86_AVX10_V1_AUX,
        TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
        TARGET_CPU_FEATURE_X86_AMX_TILE,
        TARGET_CPU_FEATURE_X86_AMX_INT8,
        TARGET_CPU_FEATURE_X86_AMX_BF16,
        TARGET_CPU_FEATURE_X86_AMX_FP16,
        TARGET_CPU_FEATURE_X86_AMX_COMPLEX,
        TARGET_CPU_FEATURE_X86_AMX_FP8,
        TARGET_CPU_FEATURE_X86_AMX_AVX512,
        TARGET_CPU_FEATURE_X86_AMX_MOVRS,
        TARGET_CPU_FEATURE_X86_AVX_VNNI,
        TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8,
        TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16,
        TARGET_CPU_FEATURE_X86_AVX_IFMA,
        TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT,
        TARGET_CPU_FEATURE_X86_MOVRS,
        TARGET_CPU_FEATURE_X86_3DNOW,
        TARGET_CPU_FEATURE_X86_3DNOWA,
        TARGET_CPU_FEATURE_X86_FMA4,
        TARGET_CPU_FEATURE_X86_LWP,
        TARGET_CPU_FEATURE_X86_TBM,
        TARGET_CPU_FEATURE_X86_XOP,
        TARGET_CPU_FEATURE_X86_IBT,
        TARGET_CPU_FEATURE_X86_CLDEMOTE,
        TARGET_CPU_FEATURE_X86_PREFETCHI,
        TARGET_CPU_FEATURE_X86_SHSTK,
        TARGET_CPU_FEATURE_X86_VMX,
        TARGET_CPU_FEATURE_X86_SVM,
        TARGET_CPU_FEATURE_X86_ENQCMD,
        TARGET_CPU_FEATURE_X86_FRED,
        TARGET_CPU_FEATURE_X86_HRESET,
        TARGET_CPU_FEATURE_X86_INVLPGB,
        TARGET_CPU_FEATURE_X86_INVPCID,
        TARGET_CPU_FEATURE_X86_KEYLOCKER,
        TARGET_CPU_FEATURE_X86_LKGS,
        TARGET_CPU_FEATURE_X86_MSR_IMM,
        TARGET_CPU_FEATURE_X86_MSRLIST,
        TARGET_CPU_FEATURE_X86_MONITOR,
        TARGET_CPU_FEATURE_X86_MOVDIR64B,
        TARGET_CPU_FEATURE_X86_PBNDKB,
        TARGET_CPU_FEATURE_X86_PCONFIG,
        TARGET_CPU_FEATURE_X86_SMAP,
        TARGET_CPU_FEATURE_X86_SGX,
        TARGET_CPU_FEATURE_X86_SNP,
        TARGET_CPU_FEATURE_X86_TDX,
        TARGET_CPU_FEATURE_X86_WBNOINVD,
        TARGET_CPU_FEATURE_X86_WRMSRNS,
        TARGET_CPU_FEATURE_X86_XSAVE,
        TARGET_CPU_FEATURE_X86_XSAVES,
        TARGET_CPU_FEATURE_X86_ACE_1,
    };
    static TargetCpuFeature const aarch64_feature_bits[] = {
        TARGET_CPU_FEATURE_AARCH64_NEON,
        TARGET_CPU_FEATURE_AARCH64_V8_4A,
        TARGET_CPU_FEATURE_AARCH64_AES,
        TARGET_CPU_FEATURE_AARCH64_ALTNZCV,
        TARGET_CPU_FEATURE_AARCH64_CCDP,
        TARGET_CPU_FEATURE_AARCH64_CCPP,
        TARGET_CPU_FEATURE_AARCH64_COMPLXNUM,
        TARGET_CPU_FEATURE_AARCH64_CRC,
        TARGET_CPU_FEATURE_AARCH64_DOTPROD,
        TARGET_CPU_FEATURE_AARCH64_FLAGM,
        TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
        TARGET_CPU_FEATURE_AARCH64_FP16FML,
        TARGET_CPU_FEATURE_AARCH64_FPTOINT,
        TARGET_CPU_FEATURE_AARCH64_FULLFP16,
        TARGET_CPU_FEATURE_AARCH64_JSCONV,
        TARGET_CPU_FEATURE_AARCH64_LSE,
        TARGET_CPU_FEATURE_AARCH64_LOR,
        TARGET_CPU_FEATURE_AARCH64_PAUTH,
        TARGET_CPU_FEATURE_AARCH64_PERFMON,
        TARGET_CPU_FEATURE_AARCH64_PREDRES,
        TARGET_CPU_FEATURE_AARCH64_RAS,
        TARGET_CPU_FEATURE_AARCH64_RCPC,
        TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO,
        TARGET_CPU_FEATURE_AARCH64_RDM,
        TARGET_CPU_FEATURE_AARCH64_SB,
        TARGET_CPU_FEATURE_AARCH64_SHA2,
        TARGET_CPU_FEATURE_AARCH64_SHA3,
        TARGET_CPU_FEATURE_AARCH64_SPECRESTRICT,
        TARGET_CPU_FEATURE_AARCH64_SSBS,
        TARGET_CPU_FEATURE_AARCH64_TRACEV8_4,
        TARGET_CPU_FEATURE_AARCH64_SME,
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(x86_feature_bits) + BUSTER_ARRAY_LENGTH(aarch64_feature_bits) ==
                    (u32)TARGET_CPU_FEATURE_COUNT - 2);
    TargetCpuFeature const* feature_bits = target.cpu_arch == CPU_ARCH_AARCH64 ? aarch64_feature_bits : x86_feature_bits;
    u32 feature_bit_count = target.cpu_arch == CPU_ARCH_AARCH64 ? BUSTER_ARRAY_LENGTH(aarch64_feature_bits)
                                                                : BUSTER_ARRAY_LENGTH(x86_feature_bits);
    if (!names || capacity < feature_bit_count)
    {
        return 0;
    }
    TargetCpuFeatures effective = target_cpu_features_effective(target);
    u32 count = 0;
    for (u32 index = 0; index < feature_bit_count; index += 1)
    {
        if (target_cpu_features_contains(effective, feature_bits[index]))
        {
            names[count++] = target_cpu_feature_to_string(feature_bits[index]);
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u8 assembly_x86_metadata_physical_class(AssemblyRegisterClass class)
{
    return class == ASSEMBLY_REGISTER_GPR       ? BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR
           : class == ASSEMBLY_REGISTER_XMM     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
           : class == ASSEMBLY_REGISTER_YMM     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
           : class == ASSEMBLY_REGISTER_ZMM     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM
           : class == ASSEMBLY_REGISTER_OPMASK  ? BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK
           : class == ASSEMBLY_REGISTER_TILE    ? BUSTER_X86_METADATA_PHYSICAL_CLASS_TMM
           : class == ASSEMBLY_REGISTER_MMX     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX
           : class == ASSEMBLY_REGISTER_X87     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL
           : class == ASSEMBLY_REGISTER_BND     ? BUSTER_X86_METADATA_PHYSICAL_CLASS_BND
           : class == ASSEMBLY_REGISTER_CONTROL ? BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL
           : class == ASSEMBLY_REGISTER_DEBUG   ? BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG
           : class == ASSEMBLY_REGISTER_SEGMENT ? BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT
           : class == ASSEMBLY_REGISTER_SPECIAL ? BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL
                                                : BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN;
}

BUSTER_GLOBAL_LOCAL String8 assembly_x86_metadata_symbol_name(AssemblyBuilder* builder, u32 symbol)
{
    return symbol < builder->result.symbol_count ? builder->result.symbols[symbol].name : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_physical_operand(AssemblyBuilder* builder, AssemblyOperand operand, bool relative,
                                                                 bool absolute, BusterX86MetadataPhysicalOperand* result)
{
    if (!result)
    {
        return false;
    }
    *result = (BusterX86MetadataPhysicalOperand){0};
    if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        result->kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER;
        result->width = operand.reg.width;
        result->reg = (BusterX86MetadataPhysicalRegister){
            .index = operand.reg.index,
            .width = operand.reg.width,
            .physical_class = assembly_x86_metadata_physical_class(operand.reg.class),
            .high_byte = operand.reg.high_byte,
        };
        return result->reg.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_UNKNOWN;
    }
    if (operand.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        AssemblyMemory memory = operand.memory;
        result->kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY;
        // Metadata memory widths describe the encoded scalar element for
        // vector forms, while the source qualifier records the aggregate
        // vector width (xmmword/ymmword/zmmword).  Leave aggregate widths
        // unresolved so the selected vector form supplies the element width;
        // scalar and branch memory widths retain their source value.
        result->width = memory.width > 64 ? 0 : memory.width ? memory.width : (relative ? 64 : 0);
        result->memory.source_width = memory.width;
        result->memory.address_size = memory.address_size ? memory.address_size : 64;
        result->memory.scale = memory.scale ? memory.scale : 1;
        result->memory.segment = memory.has_segment ? memory.segment : BUSTER_X86_METADATA_SEGMENT_NONE;
        result->memory.has_segment = memory.has_segment;
        result->memory.has_base = memory.has_base;
        result->memory.has_index = memory.has_index;
        result->memory.rip_relative = memory.rip_relative;
        result->memory.vsib = memory.vsib;
        if (memory.has_base)
        {
            result->memory.base = (BusterX86MetadataPhysicalRegister){
                .index = memory.base.index,
                .width = memory.base.width,
                .physical_class = assembly_x86_metadata_physical_class(memory.base.class),
            };
        }
        if (memory.has_index)
        {
            result->memory.index = (BusterX86MetadataPhysicalRegister){
                .index = memory.index.index,
                .width = memory.index.width,
                .physical_class = assembly_x86_metadata_physical_class(memory.index.class),
            };
        }
        if (memory.displacement.has_symbol)
        {
            result->memory.symbol = assembly_x86_metadata_symbol_name(builder, memory.displacement.symbol);
            result->memory.has_symbol = result->memory.symbol.length != 0;
            result->memory.addend = memory.displacement.addend;
            if (!result->memory.has_symbol)
            {
                return false;
            }
        }
        else
        {
            if (memory.displacement.has_unsigned_addend)
            {
                return false;
            }
            result->memory.displacement = memory.displacement.addend;
            result->memory.has_displacement = memory.displacement.addend != 0;
        }
        if (memory.has_base && result->memory.base.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
        {
            return false;
        }
        if (memory.has_index && memory.vsib)
        {
            if (result->memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                result->memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM &&
                result->memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM)
            {
                return false;
            }
        }
        else if (memory.has_index && result->memory.index.physical_class != BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR)
        {
            return false;
        }
        return true;
    }
    if (operand.kind != ASSEMBLY_OPERAND_EXPRESSION)
    {
        return false;
    }
    result->kind = absolute       ? BUSTER_X86_METADATA_PHYSICAL_OPERAND_ABSOLUTE
                   : relative    ? BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE
                                 : BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE;
    result->width = 0;
    if (operand.expression.has_symbol)
    {
        result->symbol = assembly_x86_metadata_symbol_name(builder, operand.expression.symbol);
        result->has_symbol = result->symbol.length != 0;
        result->addend = operand.expression.addend;
        return result->has_symbol;
    }
    if (operand.expression.has_unsigned_addend)
    {
        result->unsigned_value = operand.expression.unsigned_addend;
        result->has_unsigned_value = true;
    }
    else
    {
        result->value = operand.expression.addend;
        result->has_value = true;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_absolute_mnemonic(String8 mnemonic)
{
    return assembly_word_equal(mnemonic, S8("jmpabs"));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_adjust_relative_literals(BusterX86MetadataPhysicalOperand* operands, u32 operand_count,
                                                                          u64 offset, u32 byte_count)
{
    if (!operands)
    {
        return false;
    }
    bool has_relative_literal = false;
    for (u32 index = 0; index < operand_count; index += 1)
    {
        has_relative_literal |= operands[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE && !operands[index].has_symbol;
    }
    if (!has_relative_literal)
    {
        return true;
    }
    if (offset > (u64)INT64_MAX || (u64)byte_count > (u64)INT64_MAX - offset)
    {
        return false;
    }
    s64 next = (s64)(offset + byte_count);
    for (u32 index = 0; index < operand_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand* operand = operands + index;
        if (operand->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE || operand->has_symbol)
        {
            continue;
        }
        if (operand->has_unsigned_value)
        {
            if (operand->unsigned_value > (u64)INT64_MAX)
            {
                return false;
            }
            operand->value = (s64)operand->unsigned_value;
            operand->has_unsigned_value = false;
            operand->has_value = true;
        }
        if (!operand->has_value || operand->value < INT64_MIN + next)
        {
            return false;
        }
        operand->value -= next;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_relative_mnemonic(String8 mnemonic)
{
    if (!mnemonic.length)
    {
        return false;
    }
    if (assembly_word_equal(mnemonic, S8("jmpabs")))
    {
        return false;
    }
    if (mnemonic.pointer[0] == 'j' || mnemonic.pointer[0] == 'J')
    {
        return true;
    }
    return assembly_word_equal(mnemonic, S8("call")) || assembly_word_equal(mnemonic, S8("call_near")) ||
           assembly_word_equal(mnemonic, S8("loop")) ||
           assembly_word_equal(mnemonic, S8("loope")) || assembly_word_equal(mnemonic, S8("loopz")) ||
           assembly_word_equal(mnemonic, S8("loopne")) || assembly_word_equal(mnemonic, S8("loopnz")) ||
           assembly_word_equal(mnemonic, S8("xbegin"));
}

BUSTER_GLOBAL_LOCAL String8 assembly_x86_metadata_att_string_alias(AssemblySyntax syntax, String8 mnemonic)
{
    if (syntax != ASSEMBLY_SYNTAX_ATT) return mnemonic;
    if (assembly_word_equal(mnemonic, S8("movsl"))) return S8("movsd");
    if (assembly_word_equal(mnemonic, S8("cmpsl"))) return S8("cmpsd");
    if (assembly_word_equal(mnemonic, S8("stosl"))) return S8("stosd");
    if (assembly_word_equal(mnemonic, S8("lodsl"))) return S8("lodsd");
    if (assembly_word_equal(mnemonic, S8("scasl"))) return S8("scasd");
    if (assembly_word_equal(mnemonic, S8("insl"))) return S8("insd");
    if (assembly_word_equal(mnemonic, S8("outsl"))) return S8("outsd");
    return mnemonic;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_string_has_token(BusterX86MetadataString string, String8 token)
{
    u32 offset = 0;
    while (offset < string.length)
    {
        while (offset < string.length && assembly_space(buster_x86_metadata_string_byte(string, offset))) offset += 1;
        u32 start = offset;
        while (offset < string.length && !assembly_space(buster_x86_metadata_string_byte(string, offset))) offset += 1;
        u32 length = offset - start;
        if (length != token.length) continue;
        bool equal = true;
        for (u32 index = 0; index < length; index += 1)
        {
            if (assembly_ascii_lower(buster_x86_metadata_string_byte(string, start + index)) !=
                assembly_ascii_lower(token.pointer[index]))
            {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_mnemonic_has_att_operand_order_exception(String8 mnemonic)
{
    if (!assembly_word_equal(mnemonic, S8("enter"))) return false;
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(mnemonic);
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (buster_x86_metadata_candidate_at(candidates, position, &form_id) && buster_x86_metadata_form(form_id, &form) &&
            assembly_x86_metadata_string_has_token(form.attributes, S8("ATT_OPERAND_ORDER_EXCEPTION")))
            return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 assembly_x86_metadata_mnemonic(String8 mnemonic)
{
    if (assembly_word_equal(mnemonic, S8("loopz"))) return S8("loope");
    if (assembly_word_equal(mnemonic, S8("loopnz"))) return S8("loopne");
    if (assembly_word_equal(mnemonic, S8("xlatb"))) return S8("xlat");
    if (buster_x86_metadata_lookup_mnemonic(mnemonic).count)
    {
        return mnemonic;
    }
    if (mnemonic.length > 5 && assembly_word_equal(string_slice(mnemonic, mnemonic.length - 5, mnemonic.length), S8("_near")))
    {
        String8 base = string_slice(mnemonic, 0, mnemonic.length - 5);
        if (buster_x86_metadata_lookup_mnemonic(base).count)
        {
            return base;
        }
    }
    return mnemonic;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_mnemonic_requires_dfv(String8 mnemonic)
{
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(mnemonic);
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        if (buster_x86_metadata_candidate_at(candidates, position, &form_id) &&
            buster_x86_metadata_form_requires_dfv(form_id))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_parse_dfv(AssemblyOperand operand, u8* value)
{
    if (operand.kind != ASSEMBLY_OPERAND_EXPRESSION || operand.expression.has_symbol)
    {
        return false;
    }
    if (operand.expression.has_unsigned_addend)
    {
        if (operand.expression.unsigned_addend > 15) return false;
        if (value) *value = (u8)operand.expression.unsigned_addend;
        return true;
    }
    if (operand.expression.addend < 0 || operand.expression.addend > 15)
    {
        return false;
    }
    if (value) *value = (u8)operand.expression.addend;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_mnemonic_has_visible_operands(String8 mnemonic)
{
    BusterX86MetadataCandidateRange candidates = buster_x86_metadata_lookup_mnemonic(mnemonic);
    for (u32 position = 0; position < candidates.count; position += 1)
    {
        u32 form_id = 0;
        BusterX86MetadataForm form = {0};
        if (!buster_x86_metadata_candidate_at(candidates, position, &form_id) || !buster_x86_metadata_form(form_id, &form))
        {
            continue;
        }
        for (u32 operand_index = 0; operand_index < form.operand_count; operand_index += 1)
        {
            BusterX86MetadataOperand operand = {0};
            if (buster_x86_metadata_operand(form_id, operand_index, &operand) && operand.visible)
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_suffix_alias(Target target, AssemblySyntax syntax, String8 mnemonic,
                                                             String8* base, AssemblyInstructionInfo* base_info, u8* width)
{
    if (syntax != ASSEMBLY_SYNTAX_ATT || mnemonic.length <= 1 || !base || !width)
    {
        return false;
    }
    char8 suffix = assembly_ascii_lower(mnemonic.pointer[mnemonic.length - 1]);
    u8 suffix_width = suffix == 'b' ? 8 : suffix == 'w' ? 16 : suffix == 'l' ? 32 : suffix == 'q' ? 64 : 0;
    if (!suffix_width)
    {
        return false;
    }
    String8 candidate = string_slice(mnemonic, 0, mnemonic.length - 1);
    AssemblyInstructionInfo info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    bool has_handwritten_base = assembly_instruction_lookup(target, syntax, candidate, &info);
    if (has_handwritten_base)
    {
        if ((!info.operand_count && info.opcode != ASSEMBLY_OPCODE_X86_RET) || info.opcode == ASSEMBLY_OPCODE_X86_JCC ||
            info.opcode == ASSEMBLY_OPCODE_X86_SETCC)
        {
            return false;
        }
    }
    else if (!buster_x86_metadata_lookup_mnemonic(candidate).count ||
             !assembly_x86_metadata_mnemonic_has_visible_operands(candidate))
    {
        return false;
    }
    if (assembly_x86_metadata_relative_mnemonic(candidate) && !assembly_word_equal(candidate, S8("jmp")) &&
        !assembly_word_equal(candidate, S8("call")) && !assembly_word_equal(candidate, S8("call_near")))
    {
        return false;
    }
    *base = candidate;
    if (base_info)
    {
        *base_info = info;
    }
    *width = suffix_width;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_suffix_applies(AssemblyOpcode opcode, u32 operand_index, u32 operand_count)
{
    if (opcode == ASSEMBLY_OPCODE_X86_LEA || assembly_x86_opcode_is_rotate(opcode) ||
        opcode == ASSEMBLY_OPCODE_X86_MOVZX || opcode == ASSEMBLY_OPCODE_X86_MOVSX || opcode == ASSEMBLY_OPCODE_X86_MOVSXD)
    {
        return operand_index == 0;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_SHLD || opcode == ASSEMBLY_OPCODE_X86_SHRD)
    {
        return operand_index < 2;
    }
    if (assembly_x86_opcode_is_shift(opcode))
    {
        return operand_index + 1 < operand_count;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_suffix_width_matches(AssemblyInstructionInfo info, u8 suffix_width,
                                                                      AssemblyOperand const* operands,
                                                                      BusterX86MetadataPhysicalOperand* physical,
                                                                      u32 operand_count)
{
    bool saw_width_operand = false;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        if (!assembly_x86_metadata_suffix_applies(info.opcode, operand_index, operand_count))
        {
            continue;
        }
        AssemblyOperand operand = operands[operand_index];
        if (operand.kind == ASSEMBLY_OPERAND_REGISTER && operand.reg.width != suffix_width)
        {
            return false;
        }
        if (operand.kind == ASSEMBLY_OPERAND_REGISTER)
        {
            saw_width_operand = true;
        }
        if (operand.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (operand.memory.width && operand.memory.width != suffix_width)
            {
                return false;
            }
            physical[operand_index].width = suffix_width;
            saw_width_operand = true;
        }
    }
    // A suffix must describe a data-width operand.  This excludes metadata
    // mnemonics whose visible operand is only an immediate or branch target
    // (for example intb), while retaining register and memory aliases such as
    // smswl and pushpq.
    return saw_width_operand;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataSelectResult assembly_x86_metadata_select_source_form(
    BusterX86MetadataPhysicalQuery query, String8 mnemonic, String8 suffix_base, AssemblyInstructionInfo suffix_info,
    u8 suffix_width, AssemblyOperand const* operands, BusterX86MetadataPhysicalOperand* physical, u32 operand_count,
    String8* selected_mnemonic)
{
    if (selected_mnemonic)
    {
        *selected_mnemonic = mnemonic;
    }
    BusterX86MetadataSelectResult selection = buster_x86_metadata_select_form(query);
    if (selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS || !suffix_base.length)
    {
        return selection;
    }
    if (!assembly_x86_metadata_suffix_width_matches(suffix_info, suffix_width, operands, physical, operand_count))
    {
        selection.status = BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        return selection;
    }
    query.mnemonic = suffix_base;
    BusterX86MetadataSelectResult suffix_selection = buster_x86_metadata_select_form(query);
    if (suffix_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && selected_mnemonic)
    {
        *selected_mnemonic = suffix_base;
    }
    return suffix_selection;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_operand_decorators(BusterX86MetadataPhysicalAttributes* attributes,
                                                                    AssemblyOperand const* operands, u32 operand_count)
{
    for (u32 index = 0; index < operand_count; index += 1)
    {
        AssemblyOperand operand = operands[index];
        if ((operand.has_mask || operand.zeroing || operand.rounding || operand.sae) &&
            (index != 0 || operand.kind != ASSEMBLY_OPERAND_REGISTER))
        {
            return false;
        }
        if (operand.zeroing && !operand.has_mask)
        {
            return false;
        }
        if (operand.broadcast && operand.kind != ASSEMBLY_OPERAND_MEMORY)
        {
            return false;
        }
        if (operand.has_mask)
        {
            if (attributes->has_mask_register && attributes->mask_register != operand.mask)
            {
                return false;
            }
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_MASK;
            attributes->has_mask_register = true;
            attributes->mask_register = operand.mask;
        }
        if (operand.zeroing)
        {
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ZEROING;
            attributes->zeroing = true;
        }
        if (operand.broadcast)
        {
            if (attributes->broadcast_elements && attributes->broadcast_elements != operand.broadcast)
            {
                return false;
            }
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_BROADCAST;
            attributes->broadcast_elements = operand.broadcast;
        }
        if (operand.rounding)
        {
            if (attributes->rounding_mode && attributes->rounding_mode != operand.rounding)
            {
                return false;
            }
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ROUNDING;
            attributes->rounding_mode = operand.rounding;
            attributes->sae = true;
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_SAE;
        }
        else if (operand.sae)
        {
            attributes->decorator_flags |= BUSTER_X86_METADATA_DECORATOR_SAE;
            attributes->sae = true;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_physical_has_duplicate_registers(BusterX86MetadataPhysicalOperand const* operands,
                                                                                 u32 operand_count)
{
    for (u32 left = 0; left < operand_count; left += 1)
    {
        if (operands[left].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
        {
            continue;
        }
        for (u32 right = left + 1; right < operand_count; right += 1)
        {
            if (operands[right].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                operands[left].reg.physical_class == operands[right].reg.physical_class &&
                operands[left].reg.index == operands[right].reg.index &&
                operands[left].reg.width == operands[right].reg.width)
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataEncodeStatus assembly_x86_metadata_instruction_parse(
    AssemblyBuilder* builder, String8 statement, u32 line, u32 column, u64 offset, Target target, AssemblySyntax syntax)
{
    if (target.cpu_arch != CPU_ARCH_X86_64)
    {
        return BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC;
    }
    bool lock = false;
    bool rep = false;
    bool repne = false;
    bool notrack = false;
    u8 segment_override = BUSTER_X86_METADATA_SEGMENT_NONE;
    bool segment_override_seen = false;
    bool source_address_size_seen = false;
    u8 source_address_size = 64;
    String8 work = assembly_trim(statement);
    for (;;)
    {
        u64 prefix_end = 0;
        while (prefix_end < work.length && !assembly_space(work.pointer[prefix_end])) prefix_end += 1;
        String8 prefix = string_slice(work, 0, prefix_end);
        if (assembly_word_equal(prefix, S8("lock")))
        {
            if (lock)
            {
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            }
            lock = true;
        }
        else if (assembly_word_equal(prefix, S8("rep")) || assembly_word_equal(prefix, S8("repe")) ||
                 assembly_word_equal(prefix, S8("repz")))
        {
            if (rep || repne)
            {
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            }
            rep = true;
        }
        else if (assembly_word_equal(prefix, S8("repne")) || assembly_word_equal(prefix, S8("repnz")))
        {
            if (rep || repne)
            {
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            }
            repne = true;
        }
        else if (assembly_word_equal(prefix, S8("notrack")))
        {
            if (notrack)
            {
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            }
            notrack = true;
        }
        else if (assembly_word_equal(prefix, S8("addr32")))
        {
            if (source_address_size_seen)
            {
                // Address-size prefixes are source-level instruction
                // modifiers.  A duplicate or conflicting modifier must not
                // silently select a different EAMODE row.
                return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
            }
            source_address_size = 32;
            source_address_size_seen = true;
        }
        else
        {
            u8 requested_segment = BUSTER_X86_METADATA_SEGMENT_NONE;
            if (!assembly_x86_segment_parse(prefix, ASSEMBLY_SYNTAX_INTEL, &requested_segment))
            {
                break;
            }
            if (segment_override_seen)
            {
                return BUSTER_X86_METADATA_ENCODE_PREFIX_COMBINATION;
            }
            segment_override = requested_segment;
            segment_override_seen = true;
        }
        work = assembly_trim(string_slice(work, prefix_end, work.length));
    }
    bool no_flags = false;
    if (work.length && work.pointer[0] == '{')
    {
        u64 end = 1;
        while (end < work.length && work.pointer[end] != '}') end += 1;
        if (end >= work.length || !assembly_word_equal(assembly_trim(string_slice(work, 1, end)), S8("nf")))
        {
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        no_flags = true;
        work = assembly_trim(string_slice(work, end + 1, work.length));
    }
    u64 mnemonic_end = 0;
    while (mnemonic_end < work.length && !assembly_space(work.pointer[mnemonic_end])) mnemonic_end += 1;
    String8 source_mnemonic = string_slice(work, 0, mnemonic_end);
    String8 metadata_source_mnemonic = assembly_x86_metadata_att_string_alias(syntax, source_mnemonic);
    String8 mnemonic = assembly_x86_metadata_mnemonic(metadata_source_mnemonic);
    String8 mnemonic_suffix_base = {0};
    AssemblyInstructionInfo mnemonic_suffix_info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    u8 mnemonic_suffix_width = 0;
    bool has_suffix_alias = assembly_x86_metadata_suffix_alias(target, syntax, source_mnemonic, &mnemonic_suffix_base,
                                                                &mnemonic_suffix_info, &mnemonic_suffix_width);
    bool has_mnemonic = mnemonic.length && buster_x86_metadata_lookup_mnemonic(mnemonic).count;
    if (!has_mnemonic)
    {
        if (!has_suffix_alias)
        {
            return BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC;
        }
        mnemonic = mnemonic_suffix_base;
    }
    else if (!has_suffix_alias)
    {
        mnemonic_suffix_base = (String8){0};
    }
    bool suffix_alias_selected = !has_mnemonic && has_suffix_alias;
    String8 operands_text = assembly_trim(string_slice(work, mnemonic_end, work.length));
    u8 leading_rounding = 0;
    bool leading_sae = false;
    if (operands_text.length && operands_text.pointer[0] == '{')
    {
        u64 end = 1;
        while (end < operands_text.length && operands_text.pointer[end] != '}') end += 1;
        if (end >= operands_text.length)
        {
            return BUSTER_X86_METADATA_ENCODE_DECORATOR;
        }
        String8 decorator = assembly_trim(string_slice(operands_text, 1, end));
        leading_rounding = assembly_word_equal(decorator, S8("rn-sae")) ? BUSTER_X86_METADATA_ROUNDING_NEAREST
                          : assembly_word_equal(decorator, S8("rd-sae")) ? BUSTER_X86_METADATA_ROUNDING_DOWN
                          : assembly_word_equal(decorator, S8("ru-sae")) ? BUSTER_X86_METADATA_ROUNDING_UP
                          : assembly_word_equal(decorator, S8("rz-sae")) ? BUSTER_X86_METADATA_ROUNDING_ZERO
                                                                          : 0;
        leading_sae = assembly_word_equal(decorator, S8("sae")) || leading_rounding != 0;
        if (!leading_sae)
        {
            return BUSTER_X86_METADATA_ENCODE_DECORATOR;
        }
        operands_text = assembly_trim(string_slice(operands_text, end + 1, operands_text.length));
        if (operands_text.length && operands_text.pointer[0] == ',')
        {
            operands_text = assembly_trim(string_slice(operands_text, 1, operands_text.length));
        }
    }
    AssemblyOperand operands[ASSEMBLY_MAX_OPERANDS] = {0};
    u32 operand_count = 0;
    u64 operand_start = 0;
    bool relative = assembly_x86_metadata_relative_mnemonic(mnemonic);
    u8 branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NONE;
    u8 implicit_segment = segment_override;
    if (segment_override == BUSTER_X86_METADATA_SEGMENT_CS)
    {
        if (relative)
        {
            branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_NOT_TAKEN;
            implicit_segment = BUSTER_X86_METADATA_SEGMENT_NONE;
        }
    }
    else if (segment_override == BUSTER_X86_METADATA_SEGMENT_DS)
    {
        if (relative)
        {
            branch_hint = BUSTER_X86_METADATA_BRANCH_HINT_TAKEN;
            implicit_segment = BUSTER_X86_METADATA_SEGMENT_NONE;
        }
    }
    bool mnemonic_requires_dfv = assembly_x86_metadata_mnemonic_requires_dfv(mnemonic);
    u32 symbol_count_before_dfv = builder->result.symbol_count;
    bool has_dfv = false;
    u8 dfv = 0;
    while (operand_start < operands_text.length)
    {
        if (operand_count >= BUSTER_ARRAY_LENGTH(operands))
        {
            return BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT;
        }
        String8 text = {0};
        if (assembly_operand_split_next(operands_text, &operand_start, &text) != ASSEMBLY_OPERAND_SPLIT_SUCCESS)
        {
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        bool indirect = syntax == ASSEMBLY_SYNTAX_ATT && relative && text.pointer[0] == '*';
        if (indirect)
        {
            text = assembly_trim(string_slice(text, 1, text.length));
            if (!text.length)
            {
                return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            }
        }
        AssemblyOperand* operand = operands + operand_count;
        if (!assembly_x86_operand_decorators_parse(&text, syntax, operand, &no_flags))
        {
            return BUSTER_X86_METADATA_ENCODE_DECORATOR;
        }
        bool att_immediate = syntax == ASSEMBLY_SYNTAX_ATT && text.length && text.pointer[0] == '$';
        if (att_immediate)
        {
            text = assembly_trim(string_slice(text, 1, text.length));
        }
        if (!att_immediate && assembly_register_parse(text, syntax, &operand->reg))
        {
            operand->kind = ASSEMBLY_OPERAND_REGISTER;
        }
        else if (!att_immediate && assembly_x86_memory_parse(builder, text, syntax, &operand->memory) &&
                 !(syntax == ASSEMBLY_SYNTAX_ATT && relative && !indirect && operand->memory.absolute))
        {
            operand->kind = ASSEMBLY_OPERAND_MEMORY;
        }
        else
        {
            bool immediate = att_immediate;
            if (!immediate && syntax == ASSEMBLY_SYNTAX_INTEL && text.length && text.pointer[0] == '$')
            {
                return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            }
            else if (!immediate && syntax == ASSEMBLY_SYNTAX_ATT && !relative)
            {
                return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
            }
            if (!text.length || !assembly_expression_parse(builder, text, &operand->expression))
            {
                return BUSTER_X86_METADATA_ENCODE_INVALID_EXPRESSION;
            }
            operand->kind = ASSEMBLY_OPERAND_EXPRESSION;
        }
        operand_count += 1;
    }
    if (mnemonic_requires_dfv)
    {
        if (!operand_count || !assembly_x86_metadata_parse_dfv(operands[0], &dfv))
        {
            // A rejected symbolic DFV must not intern a source symbol as a
            // side effect of parsing the pseudo-operand.
            builder->result.symbol_count = symbol_count_before_dfv;
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        has_dfv = true;
        for (u32 index = 1; index < operand_count; index += 1)
        {
            operands[index - 1] = operands[index];
        }
        operand_count -= 1;
    }
    bool att_operand_order_exception = syntax == ASSEMBLY_SYNTAX_ATT &&
                                       assembly_x86_metadata_mnemonic_has_att_operand_order_exception(mnemonic);
    if (syntax == ASSEMBLY_SYNTAX_ATT && !att_operand_order_exception)
    {
        for (u32 left = 0; left < operand_count / 2; left += 1)
        {
            AssemblyOperand temporary = operands[left];
            u32 right = operand_count - left - 1;
            operands[left] = operands[right];
            operands[right] = temporary;
        }
    }
    BusterX86MetadataPhysicalOperand physical[ASSEMBLY_MAX_OPERANDS] = {0};
    bool absolute = assembly_x86_metadata_absolute_mnemonic(mnemonic);
    for (u32 index = 0; index < operand_count; index += 1)
    {
        if (!assembly_x86_metadata_physical_operand(builder, operands[index], relative, absolute,
                                                     physical + index))
        {
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
    }
    // These metadata forms have a fixed memory element width.  Intel permits
    // unsized CLRSSBSY/RSTORSSP memory operands, while AT&T leaves the width
    // implicit for the WRSS/WRUSS rows too.  Normalize the cohort before form
    // selection so both dialects select the same rows, including APX-F CET.
    u16 implicit_memory_width = 0;
    bool cet_unsized_intel = syntax == ASSEMBLY_SYNTAX_INTEL &&
                             (assembly_word_equal(mnemonic, S8("rstorssp")) || assembly_word_equal(mnemonic, S8("clrssbsy")));
    bool implicit_att_memory = syntax == ASSEMBLY_SYNTAX_ATT;
    if (implicit_att_memory && assembly_word_equal(mnemonic, S8("cldemote"))) implicit_memory_width = 8;
    else if (implicit_att_memory &&
             (assembly_word_equal(mnemonic, S8("wrssd")) || assembly_word_equal(mnemonic, S8("wrussd"))))
        implicit_memory_width = 32;
    else if (implicit_att_memory &&
             (assembly_word_equal(mnemonic, S8("wrssq")) || assembly_word_equal(mnemonic, S8("wrussq")) ||
              assembly_word_equal(mnemonic, S8("rstorssp")) || assembly_word_equal(mnemonic, S8("clrssbsy"))))
        implicit_memory_width = 64;
    else if (implicit_att_memory &&
             (assembly_word_equal(mnemonic, S8("bsrmovh")) || assembly_word_equal(mnemonic, S8("bsrmovl"))))
        // BSRMOVH/L's only memory schema is a fixed u64 element.  AT&T has
        // no ptr qualifier, so resolve an unsized memory operand here; an
        // explicit q suffix remains accepted by the generic alias path.
        implicit_memory_width = 64;
    else if (cet_unsized_intel) implicit_memory_width = 64;
    if (implicit_memory_width)
    {
        for (u32 index = 0; index < operand_count; index += 1)
        {
            if (physical[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !physical[index].width)
            {
                physical[index].width = implicit_memory_width;
            }
        }
    }
    if (suffix_alias_selected && !assembly_x86_metadata_suffix_width_matches(mnemonic_suffix_info, mnemonic_suffix_width, operands,
                                                                               physical, operand_count))
    {
        return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
    }
    for (u32 immediate_index = 0; immediate_index < operand_count; immediate_index += 1)
    {
        if (physical[immediate_index].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE || physical[immediate_index].width)
        {
            continue;
        }
        for (u32 source_index = 0; source_index < operand_count; source_index += 1)
        {
            if (source_index == immediate_index)
            {
                continue;
            }
            BusterX86MetadataPhysicalOperand source = physical[source_index];
            u16 width = 0;
            if (source.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                source.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && source.reg.width <= 64)
            {
                width = source.reg.width;
            }
            else if (source.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && source.width && source.width <= 64)
            {
                width = source.width;
            }
            if (width)
            {
                physical[immediate_index].width = width;
                break;
            }
        }
    }
    BusterX86MetadataPhysicalAttributes attributes = {
        .lock = lock,
        .rep = rep,
        .repne = repne,
        .implicit_segment = implicit_segment,
        .branch_hint = branch_hint,
        .notrack = notrack,
        .no_flags = no_flags,
        .dfv = dfv,
        .has_dfv = has_dfv,
    };
    if (no_flags)
    {
        attributes.apx_flags |= BUSTER_X86_METADATA_APX_NF;
    }
    if (leading_sae)
    {
        attributes.sae = true;
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_SAE;
    }
    if (!assembly_x86_metadata_operand_decorators(&attributes, operands, operand_count))
    {
        return BUSTER_X86_METADATA_ENCODE_DECORATOR;
    }
    if (leading_rounding)
    {
        attributes.rounding_mode = leading_rounding;
        attributes.sae = true;
        attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ROUNDING | BUSTER_X86_METADATA_DECORATOR_SAE;
    }
    String8 feature_names[TARGET_CPU_FEATURE_COUNT] = {0};
    u32 feature_count = assembly_x86_metadata_feature_names(target, feature_names, BUSTER_ARRAY_LENGTH(feature_names));
    u8 address_size = source_address_size;
    bool memory_address_size_seen = false;
    for (u32 index = 0; index < operand_count; index += 1)
    {
        if (physical[index].kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            continue;
        }
        u8 memory_address_size = physical[index].memory.address_size ? physical[index].memory.address_size : 64;
        if (source_address_size_seen && address_size != memory_address_size)
        {
            return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        }
        if (memory_address_size_seen && address_size != memory_address_size)
        {
            return BUSTER_X86_METADATA_ENCODE_ADDRESSING;
        }
        address_size = memory_address_size;
        memory_address_size_seen = true;
    }
    BusterX86MetadataPhysicalQuery query = {
        .mnemonic = mnemonic,
        .operands = physical,
        .operand_count = operand_count,
        .features = {.names = feature_names, .count = feature_count},
        .attributes = attributes,
        .address_size = address_size,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_privileged = true,
        .include_not64 = false,
        .source_semantics = true,
    };
    bool relative_literal = false;
    for (u32 index = 0; index < operand_count; index += 1)
    {
        relative_literal |= physical[index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE && !physical[index].has_symbol;
    }
    BusterX86MetadataSelectResult selection = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = UINT32_MAX,
    };
    bool selected = false;
    u32 trial_count = relative_literal ? 15 : 1;
    for (u32 trial_byte_count = 1; trial_byte_count <= trial_count; trial_byte_count += 1)
    {
        BusterX86MetadataPhysicalOperand trial_physical[ASSEMBLY_MAX_OPERANDS] = {0};
        memcpy(trial_physical, physical, operand_count * sizeof(*physical));
        if (relative_literal && !assembly_x86_metadata_adjust_relative_literals(trial_physical, operand_count, offset, trial_byte_count))
        {
            continue;
        }
        query.operands = trial_physical;
        String8 trial_mnemonic = mnemonic;
        BusterX86MetadataSelectResult trial_selection = assembly_x86_metadata_select_source_form(
            query, mnemonic, mnemonic_suffix_base, mnemonic_suffix_info, mnemonic_suffix_width, operands, trial_physical, operand_count,
            &trial_mnemonic);
        if (trial_selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS &&
            (!relative_literal || trial_selection.selected_byte_count == trial_byte_count))
        {
            memcpy(physical, trial_physical, operand_count * sizeof(*physical));
            mnemonic = trial_mnemonic;
            selection = trial_selection;
            selected = true;
            break;
        }
        if (!relative_literal)
        {
            selection = trial_selection;
            break;
        }
    }
    if (!selected && relative_literal)
    {
        query.operands = physical;
        String8 original_mnemonic = mnemonic;
        selection = assembly_x86_metadata_select_source_form(query, mnemonic, mnemonic_suffix_base, mnemonic_suffix_info,
                                                              mnemonic_suffix_width, operands, physical, operand_count, &original_mnemonic);
        if (selection.status == BUSTER_X86_METADATA_ENCODE_SUCCESS)
        {
            selection.status = BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE;
        }
    }
    if (selection.status != BUSTER_X86_METADATA_ENCODE_SUCCESS)
    {
        if (selection.status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
            (!selection.required_feature.length ||
             (assembly_x86_metadata_physical_has_duplicate_registers(physical, operand_count) && query.source_semantics)))
        {
            return BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH;
        }
        return selection.status;
    }
    if (builder->instruction_count >= builder->instruction_capacity)
    {
        return BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY;
    }
    AssemblyInstruction instruction = {
        .offset = offset,
        .line = line,
        .column = column,
        .size = selection.selected_byte_count,
        .metadata = true,
        .metadata_operand_count = (u8)operand_count,
        .metadata_form_id = selection.form_id,
        .metadata_mnemonic = mnemonic,
        .metadata_address_size = address_size,
        .metadata_attributes = attributes,
    };
    if (operand_count)
    {
        memcpy(instruction.metadata_operands, physical, operand_count * sizeof(*physical));
    }
    builder->instructions[builder->instruction_count++] = instruction;
    return BUSTER_X86_METADATA_ENCODE_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_has_unsigned_expression(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if (operand.kind == ASSEMBLY_OPERAND_EXPRESSION && operand.expression.has_unsigned_addend)
        {
            return true;
        }
        if (operand.kind == ASSEMBLY_OPERAND_MEMORY && operand.memory.displacement.has_unsigned_addend)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_instruction_is_novel(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.metadata_operand_count; index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = instruction.metadata_operands[index];
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
        {
            if ((operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR && operand.reg.index >= 16) ||
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_BND ||
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_CONTROL ||
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_DEBUG ||
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SEGMENT ||
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL)
            {
                return true;
            }
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                 (operand.memory.address_size != 64 || operand.memory.has_segment || operand.memory.vsib ||
                  (operand.memory.has_base && operand.memory.base.index >= 16) ||
                  (operand.memory.has_index && operand.memory.index.index >= 16)))
        {
            return true;
        }
    }
    BusterX86MetadataForm form = {0};
    if (buster_x86_metadata_form_is_moffs(instruction.metadata_form_id))
    {
        // A bare AT&T absolute address may need metadata solely to select the
        // accumulator-only MOV moffs encoding when its displacement exceeds
        // the ModRM disp32 range.  Treat that form as a genuine metadata
        // extension, rather than restoring the handwritten invalid-operand
        // diagnostic below.
        return true;
    }
    if (buster_x86_metadata_form(instruction.metadata_form_id, &form) &&
        (form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2 || form.encoder_family == BUSTER_X86_METADATA_ENCODER_XOP))
    {
        return true;
    }
    return instruction.metadata_attributes.apx_flags != 0 || instruction.metadata_attributes.amx_flags != 0;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_requires_metadata(AssemblyInstruction instruction)
{
    for (u32 index = 0; index < instruction.operand_count; index += 1)
    {
        AssemblyOperand operand = instruction.operands[index];
        if (operand.kind == ASSEMBLY_OPERAND_REGISTER &&
            (operand.reg.class == ASSEMBLY_REGISTER_BND || operand.reg.class == ASSEMBLY_REGISTER_CONTROL ||
             operand.reg.class == ASSEMBLY_REGISTER_DEBUG || operand.reg.class == ASSEMBLY_REGISTER_SEGMENT ||
             operand.reg.class == ASSEMBLY_REGISTER_SPECIAL))
        {
            return true;
        }
        if (operand.kind == ASSEMBLY_OPERAND_MEMORY &&
            ((operand.memory.address_size && operand.memory.address_size != 64) || operand.memory.has_segment || operand.memory.vsib))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_instruction_has_duplicate_registers(AssemblyInstruction instruction)
{
    for (u32 left = 0; left < instruction.metadata_operand_count; left += 1)
    {
        BusterX86MetadataPhysicalOperand left_operand = instruction.metadata_operands[left];
        if (left_operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
        {
            continue;
        }
        for (u32 right = left + 1; right < instruction.metadata_operand_count; right += 1)
        {
            BusterX86MetadataPhysicalOperand right_operand = instruction.metadata_operands[right];
            if (right_operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                left_operand.reg.physical_class == right_operand.reg.physical_class &&
                left_operand.reg.index == right_operand.reg.index && left_operand.reg.width == right_operand.reg.width)
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_statement_has_extended_gpr(String8 statement)
{
    for (u64 index = 0; index < statement.length; index += 1)
    {
        if (assembly_ascii_lower(statement.pointer[index]) != 'r')
        {
            continue;
        }
        u64 end = index + 1;
        while (end < statement.length &&
               ((statement.pointer[end] >= '0' && statement.pointer[end] <= '9') ||
                assembly_ascii_lower(statement.pointer[end]) == 'b' ||
                assembly_ascii_lower(statement.pointer[end]) == 'd' ||
                assembly_ascii_lower(statement.pointer[end]) == 'w'))
        {
            end += 1;
        }
        AssemblyRegister register_value = {0};
        if (assembly_extended_gpr_parse(string_slice(statement, index, end), &register_value))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_statement_has_no_flags_decorator(String8 statement)
{
    for (u64 index = 0; index < statement.length; index += 1)
    {
        if (statement.pointer[index] != '{')
        {
            continue;
        }
        u64 end = index + 1;
        while (end < statement.length && statement.pointer[end] != '}')
        {
            end += 1;
        }
        if (end < statement.length && assembly_word_equal(assembly_trim(string_slice(statement, index + 1, end)), S8("nf")))
        {
            return true;
        }
        index = end;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void assembly_x86_metadata_diagnostic(AssemblyBuilder* builder, BusterX86MetadataEncodeStatus status,
                                                           u32 line, u32 column, u32 length)
{
    AssemblyDiagnosticKind kind = ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS;
    String8 message = S8("metadata instruction form is not encodable");
    if (status == BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC)
    {
        kind = ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION;
        message = S8("unknown instruction");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_INVALID_EXPRESSION)
    {
        kind = ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION;
        message = S8("invalid instruction expression");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE)
    {
        kind = ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE;
        message = S8("instruction requires an enabled target feature");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_RELATIVE_RANGE)
    {
        kind = ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE;
        message = S8("x86 branch target is out of range");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_DISPLACEMENT_RANGE)
    {
        kind = ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION;
        message = S8("x86 memory displacement is out of range");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE)
    {
        kind = ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION;
        message = S8("x86 immediate is out of range");
    }
    else if (status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY ||
             status == BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY)
    {
        kind = ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT;
        message = S8("metadata instruction output capacity exceeded");
    }
    assembly_diagnostic(builder, kind, line, column, length, message);
}

BUSTER_GLOBAL_LOCAL void assembly_instruction_parse(AssemblyBuilder* builder, String8 statement, u32 line, u32 column, u64 offset,
                                                     Target target, AssemblySyntax syntax)
{
    u32 instruction_count = builder->instruction_count;
    u32 symbol_count = builder->result.symbol_count;
    u32 relocation_count = builder->result.relocation_count;
    u32 diagnostic_count = builder->result.diagnostic_count;
    u64 output_count = builder->output_count;
    assembly_instruction_parse_handwritten(builder, statement, line, column, offset, target, syntax);
    bool handwritten_succeeded = builder->instruction_count != instruction_count;
    bool unsigned_expression = handwritten_succeeded &&
                               assembly_x86_instruction_has_unsigned_expression(builder->instructions[instruction_count]);
    bool handwritten_requires_metadata = handwritten_succeeded &&
                                         assembly_x86_instruction_requires_metadata(builder->instructions[instruction_count]);
    if (handwritten_succeeded && !unsigned_expression && !handwritten_requires_metadata)
    {
        return;
    }
    AssemblyDiagnosticKind handwritten_kind = ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT;
    AssemblyDiagnostic handwritten_diagnostic = {0};
    bool has_handwritten_diagnostic = false;
    if (builder->result.diagnostic_count > diagnostic_count)
    {
        handwritten_kind = builder->result.diagnostics[builder->result.diagnostic_count - 1].kind;
        handwritten_diagnostic = builder->result.diagnostics[builder->result.diagnostic_count - 1];
        has_handwritten_diagnostic = true;
    }
    bool fallback_allowed = target.cpu_arch == CPU_ARCH_X86_64 &&
                             (!handwritten_succeeded || unsigned_expression || handwritten_requires_metadata);
    if (!fallback_allowed)
    {
        return;
    }
    builder->instruction_count = instruction_count;
    builder->result.symbol_count = symbol_count;
    builder->result.relocation_count = relocation_count;
    builder->result.diagnostic_count = diagnostic_count;
    builder->output_count = output_count;
    BusterX86MetadataEncodeStatus status = assembly_x86_metadata_instruction_parse(builder, statement, line, column, offset, target, syntax);
    if (status == BUSTER_X86_METADATA_ENCODE_SUCCESS)
    {
        bool metadata_novel = builder->instruction_count > instruction_count &&
                              assembly_x86_metadata_instruction_is_novel(builder->instructions[instruction_count]);
        if (handwritten_kind == ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE ||
            (handwritten_kind == ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS &&
             (!metadata_novel || assembly_x86_metadata_instruction_has_duplicate_registers(builder->instructions[instruction_count]))))
        {
            builder->instruction_count = instruction_count;
            builder->result.symbol_count = symbol_count;
            builder->result.relocation_count = relocation_count;
            builder->result.diagnostic_count = diagnostic_count;
            builder->output_count = output_count;
            if (has_handwritten_diagnostic)
            {
                assembly_diagnostic(builder, handwritten_diagnostic.kind, handwritten_diagnostic.line, handwritten_diagnostic.column,
                                    handwritten_diagnostic.length, handwritten_diagnostic.message);
            }
        }
        return;
    }
    builder->instruction_count = instruction_count;
    builder->result.symbol_count = symbol_count;
    builder->result.relocation_count = relocation_count;
    builder->result.diagnostic_count = diagnostic_count;
    builder->output_count = output_count;
    bool metadata_precise = handwritten_requires_metadata || (!handwritten_succeeded &&
                            (handwritten_kind != ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS ||
                             (status == BUSTER_X86_METADATA_ENCODE_FEATURE_MODE_PRIVILEGE &&
                              assembly_x86_statement_has_extended_gpr(statement) &&
                              !assembly_x86_statement_has_no_flags_decorator(statement)) ||
                             (handwritten_kind == ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION &&
                              (status == BUSTER_X86_METADATA_ENCODE_WRONG_OPERAND_COUNT ||
                               status == BUSTER_X86_METADATA_ENCODE_OPERAND_MISMATCH))) &&
                            status != BUSTER_X86_METADATA_ENCODE_UNKNOWN_MNEMONIC &&
                            status != BUSTER_X86_METADATA_ENCODE_INVALID_INPUT &&
                            handwritten_kind != ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE);
    if (metadata_precise)
    {
        u32 length = statement.length > UINT32_MAX ? UINT32_MAX : (u32)statement.length;
        assembly_x86_metadata_diagnostic(builder, status, line, column, length);
    }
    else if (handwritten_succeeded)
    {
        assembly_instruction_parse_handwritten(builder, statement, line, column, offset, target, syntax);
    }
    else if (has_handwritten_diagnostic)
    {
        assembly_diagnostic(builder, handwritten_diagnostic.kind, handwritten_diagnostic.line, handwritten_diagnostic.column,
                            handwritten_diagnostic.length, handwritten_diagnostic.message);
    }
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
            u64 colon = assembly_leading_label_colon(statement);
            bool segment_override = false;
            bool segment_colon_error = false;
            if (colon < statement.length)
            {
                u64 segment_start = colon;
                while (segment_start && !assembly_space(statement.pointer[segment_start - 1]))
                {
                    segment_start -= 1;
                }
                String8 segment = string_slice(statement, segment_start, colon);
                u64 following_index = colon + 1;
                while (following_index < statement.length && assembly_space(statement.pointer[following_index]))
                {
                    following_index += 1;
                }
                char8 following = following_index < statement.length ? statement.pointer[following_index] : 0;
                u8 segment_index = 0;
                bool memory_after_segment = following == '[' || following == '(';
                if (syntax == ASSEMBLY_SYNTAX_ATT && !memory_after_segment && following_index < statement.length)
                {
                    String8 after_segment = string_slice(statement, following_index, statement.length);
                    memory_after_segment = string_first_code_unit(after_segment, '(') < after_segment.length;
                }
                segment_override = memory_after_segment && assembly_x86_segment_parse(segment, syntax, &segment_index);
                if (!segment_override)
                {
                    // A bare segment word followed by ':' is only a memory
                    // operand spelling.  Keep a label-only line such as
                    // `fs:` usable, but reject `fs: movsb` instead of
                    // silently turning the prefix-looking token into a
                    // label and encoding the instruction without it.
                    u8 segment_label_index = 0;
                    bool segment_label = assembly_x86_segment_parse(segment, syntax, &segment_label_index) ||
                                         assembly_x86_segment_parse(segment, ASSEMBLY_SYNTAX_INTEL, &segment_label_index);
                    if (segment_label &&
                        following_index < statement.length)
                    {
                        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, line, column,
                                            (u32)(colon + 1), S8("segment prefix requires a memory operand"));
                        segment_colon_error = true;
                    }
                }
            }
            if (colon < statement.length && !segment_override && !segment_colon_error)
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
            if (statement.length && !segment_colon_error)
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
        *target = expression.has_unsigned_addend && expression.unsigned_addend > (u64)INT64_MAX
                      ? INT64_MAX
                      : expression.has_unsigned_addend ? (s64)expression.unsigned_addend : expression.addend;
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_pc_value(AssemblyInstruction* instruction, u64 relocation_offset, u64 target,
                                                         s64 addend, s64* value)
{
    if (!instruction || !value || instruction->offset > (u64)INT64_MAX || relocation_offset > (u64)INT64_MAX ||
        instruction->offset > (u64)INT64_MAX - relocation_offset)
    {
        return false;
    }
    u64 place_unsigned = instruction->offset + relocation_offset;
    if (place_unsigned > (u64)INT64_MAX || target > (u64)INT64_MAX)
    {
        return false;
    }
    s64 target_value = (s64)target;
    s64 place_value = (s64)place_unsigned;
    if ((addend > 0 && target_value > INT64_MAX - addend) || (addend < 0 && target_value < INT64_MIN - addend))
    {
        return false;
    }
    target_value += addend;
    if ((place_value > 0 && target_value < INT64_MIN + place_value) ||
        (place_value < 0 && target_value > INT64_MAX + place_value))
    {
        return false;
    }
    *value = target_value - place_value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_relocation_kind(u8 metadata_kind, AssemblyRelocationKind* kind)
{
    if (!kind)
    {
        return false;
    }
    switch ((BusterX86MetadataRelocationKind)metadata_kind)
    {
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE8:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE8;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE16:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE16;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE32;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE64:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE64;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_SIGN_EXTENDED:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_ABSOLUTE32_ZERO_EXTENDED:
        *kind = ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC8:
        *kind = ASSEMBLY_RELOCATION_X86_PC8;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC16:
        *kind = ASSEMBLY_RELOCATION_X86_PC16;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC32:
        *kind = ASSEMBLY_RELOCATION_X86_PC32;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_PC64:
        *kind = ASSEMBLY_RELOCATION_X86_PC64;
        return true;
    case BUSTER_X86_METADATA_RELOCATION_KIND_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_local_relocation(AssemblyBuilder* builder, AssemblyInstruction* instruction,
                                                                 BusterX86MetadataRelocation relocation, u8* bytes)
{
    if (!builder || !instruction || !bytes || relocation.width == 0 || relocation.width > 8 ||
        relocation.offset > UINT32_MAX - relocation.width || relocation.offset + relocation.width > instruction->size)
    {
        return false;
    }
    u32 symbol_index = assembly_symbol_find(builder, relocation.symbol);
    if (symbol_index == UINT32_MAX)
    {
        return false;
    }
    AssemblySymbol symbol = builder->result.symbols[symbol_index];
    AssemblyRelocationKind kind = ASSEMBLY_RELOCATION_COUNT;
    if (!assembly_x86_metadata_relocation_kind(relocation.kind, &kind))
    {
        return false;
    }
    if (!symbol.defined)
    {
        return true;
    }
    u64 value = 0;
    if (kind == ASSEMBLY_RELOCATION_X86_PC8 || kind == ASSEMBLY_RELOCATION_X86_PC16 ||
        kind == ASSEMBLY_RELOCATION_X86_PC32 || kind == ASSEMBLY_RELOCATION_X86_PC64)
    {
        s64 relative = 0;
        if (!assembly_x86_metadata_pc_value(instruction, relocation.offset, symbol.offset, relocation.addend, &relative))
        {
            return false;
        }
        if ((kind == ASSEMBLY_RELOCATION_X86_PC8 && (relative < INT8_MIN || relative > INT8_MAX)) ||
            (kind == ASSEMBLY_RELOCATION_X86_PC16 && (relative < INT16_MIN || relative > INT16_MAX)) ||
            (kind == ASSEMBLY_RELOCATION_X86_PC32 && (relative < INT32_MIN || relative > INT32_MAX)))
        {
            return false;
        }
        value = (u64)relative;
    }
    else
    {
        u64 magnitude = 0;
        if (relocation.addend >= 0)
        {
            u64 unsigned_addend = (u64)relocation.addend;
            if (symbol.offset > UINT64_MAX - unsigned_addend)
            {
                return false;
            }
            value = symbol.offset + unsigned_addend;
        }
        else
        {
            magnitude = (u64)(-(relocation.addend + 1)) + 1;
            if (symbol.offset < magnitude)
            {
                if (kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE64)
                {
                    value = 0 - (magnitude - symbol.offset);
                }
                else if (kind != ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED)
                {
                    return false;
                }
                else if (magnitude > (u64)INT64_MAX)
                {
                    s64 signed_value = INT64_MIN + (s64)symbol.offset;
                    if (signed_value < INT32_MIN || signed_value > INT32_MAX)
                    {
                        return false;
                    }
                    value = (u64)signed_value;
                }
                else
                {
                    s64 signed_value = (s64)symbol.offset - (s64)magnitude;
                    if (signed_value < INT32_MIN || signed_value > INT32_MAX)
                    {
                        return false;
                    }
                    value = (u64)signed_value;
                }
            }
            else
            {
                value = symbol.offset - magnitude;
            }
        }
        if ((kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE8 && value > UINT8_MAX) ||
            (kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE16 && value > UINT16_MAX) ||
            (kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32 && value > UINT32_MAX) ||
            (kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_ZERO_EXTENDED && value > UINT32_MAX) ||
            (kind == ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED &&
             (relocation.addend >= 0 || symbol.offset >= magnitude) && value > (u64)INT32_MAX))
        {
            return false;
        }
    }
    for (u32 byte_index = 0; byte_index < relocation.width; byte_index += 1)
    {
        bytes[relocation.offset + byte_index] = (u8)(value >> (byte_index * 8));
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_metadata_emit(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    String8 feature_names[TARGET_CPU_FEATURE_COUNT] = {0};
    u32 feature_count = assembly_x86_metadata_feature_names(builder->target, feature_names, BUSTER_ARRAY_LENGTH(feature_names));
    BusterX86MetadataPhysicalOperand operands[ASSEMBLY_MAX_OPERANDS] = {0};
    if (instruction->metadata_operand_count > BUSTER_ARRAY_LENGTH(operands))
    {
        return false;
    }
    if (instruction->metadata_operand_count)
    {
        memcpy(operands, instruction->metadata_operands, instruction->metadata_operand_count * sizeof(*operands));
    }
    BusterX86MetadataPhysicalQuery physical = {
        .mnemonic = instruction->metadata_mnemonic,
        .operands = operands,
        .operand_count = instruction->metadata_operand_count,
        .features = {.names = feature_names, .count = feature_count},
        .attributes = instruction->metadata_attributes,
        .address_size = instruction->metadata_address_size ? instruction->metadata_address_size : 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_privileged = true,
        .include_not64 = false,
        .source_semantics = true,
    };
    u8 bytes[64] = {0};
    BusterX86MetadataRelocation metadata_relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = physical,
        .form_id = instruction->metadata_form_id,
        .output = bytes,
        .output_capacity = BUSTER_ARRAY_LENGTH(bytes),
        .relocations = metadata_relocations,
        .relocation_capacity = BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY,
    });
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.byte_count != instruction->size ||
        builder->result.relocation_count > builder->relocation_capacity ||
        emitted.relocation_count > builder->relocation_capacity - builder->result.relocation_count)
    {
        BusterX86MetadataEncodeStatus status = emitted.status;
        if (status == BUSTER_X86_METADATA_ENCODE_SUCCESS)
        {
            status = BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY;
        }
        assembly_x86_metadata_diagnostic(builder, status, instruction->line, instruction->column, 1);
        return false;
    }
    for (u32 relocation_index = 0; relocation_index < emitted.relocation_count; relocation_index += 1)
    {
        BusterX86MetadataRelocation relocation = metadata_relocations[relocation_index];
        if (!assembly_x86_metadata_local_relocation(builder, instruction, relocation, bytes))
        {
            assembly_x86_metadata_diagnostic(builder, BUSTER_X86_METADATA_ENCODE_IMMEDIATE_RANGE, instruction->line, instruction->column, 1);
            return false;
        }
    }
    if (builder->output_count > builder->output_capacity || emitted.byte_count > builder->output_capacity - builder->output_count)
    {
        assembly_x86_metadata_diagnostic(builder, BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY, instruction->line, instruction->column, 1);
        return false;
    }
    u32 relocation_symbols[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    AssemblyRelocationKind relocation_kinds[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    u32 unresolved_relocation_count = 0;
    for (u32 relocation_index = 0; relocation_index < emitted.relocation_count; relocation_index += 1)
    {
        BusterX86MetadataRelocation relocation = metadata_relocations[relocation_index];
        u32 symbol = assembly_symbol_find(builder, relocation.symbol);
        AssemblyRelocationKind kind = ASSEMBLY_RELOCATION_COUNT;
        if (symbol == UINT32_MAX || !assembly_x86_metadata_relocation_kind(relocation.kind, &kind))
        {
            assembly_x86_metadata_diagnostic(builder, BUSTER_X86_METADATA_ENCODE_INVALID_EXPRESSION, instruction->line, instruction->column, 1);
            return false;
        }
        if (!builder->result.symbols[symbol].defined)
        {
            if (instruction->offset > UINT64_MAX - relocation.offset)
            {
                assembly_x86_metadata_diagnostic(builder, BUSTER_X86_METADATA_ENCODE_INVALID_EXPRESSION, instruction->line, instruction->column, 1);
                return false;
            }
            relocation_symbols[unresolved_relocation_count] = symbol;
            relocation_kinds[unresolved_relocation_count] = kind;
            unresolved_relocation_count += 1;
        }
    }
    if (unresolved_relocation_count > builder->relocation_capacity - builder->result.relocation_count)
    {
        assembly_x86_metadata_diagnostic(builder, BUSTER_X86_METADATA_ENCODE_RELOCATION_CAPACITY, instruction->line, instruction->column, 1);
        return false;
    }
    u32 unresolved_index = 0;
    for (u32 relocation_index = 0; relocation_index < emitted.relocation_count; relocation_index += 1)
    {
        BusterX86MetadataRelocation relocation = metadata_relocations[relocation_index];
        u32 symbol = assembly_symbol_find(builder, relocation.symbol);
        if (builder->result.symbols[symbol].defined)
        {
            continue;
        }
        builder->result.relocations[builder->result.relocation_count++] = (AssemblyRelocation){
            .addend = relocation.addend,
            .offset = instruction->offset + relocation.offset,
            .symbol = relocation_symbols[unresolved_index],
            .kind = relocation_kinds[unresolved_index],
        };
        unresolved_index += 1;
    }
    memcpy(builder->result.bytes.pointer + builder->output_count, bytes, emitted.byte_count);
    builder->output_count += emitted.byte_count;
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

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_amd_prefix(AssemblyBuilder* builder, AssemblyAmdForm const* form, AssemblyRegister reg,
                                                       AssemblyOperand rm, u16 width, u8 source, bool w)
{
    u8 p0 = (u8)(0xe0 | (form->map & 0x1f));
    if (reg.index >= 8)
    {
        p0 &= (u8)~0x80;
    }
    if (rm.kind == ASSEMBLY_OPERAND_REGISTER)
    {
        if (rm.reg.index >= 8)
        {
            p0 &= (u8)~0x20;
        }
    }
    else if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
    {
        if (rm.memory.has_base && rm.memory.base.index >= 8)
        {
            p0 &= (u8)~0x20;
        }
        if (rm.memory.has_index && rm.memory.index.index >= 8)
        {
            p0 &= (u8)~0x40;
        }
    }
    u8 p1 = (u8)((w ? 0x80 : 0) | ((~source & 15) << 3) | (width == 256 ? 0x04 : 0) | (form->mandatory_prefix & 3));
    assembly_emit_byte(builder, form->encoding == ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE ||
                                      form->encoding == ASSEMBLY_AMD_ENCODING_VEX_FMA4
                                  ? 0xc4
                                  : 0x8f);
    assembly_emit_byte(builder, p0);
    assembly_emit_byte(builder, p1);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_amd(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    AssemblyAmdForm const* form = instruction->amd_form;
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    switch (form->encoding)
    {
    case ASSEMBLY_AMD_ENCODING_FEMMS:
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, 0x0e);
        return true;
    case ASSEMBLY_AMD_ENCODING_3DNOW2:
        if (second->kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_x86_emit_memory_prefix(builder, 0, first->reg, second->memory);
        }
        assembly_emit_byte(builder, 0x0f);
        assembly_emit_byte(builder, 0x0f);
        if (!assembly_x86_emit_rm(builder, instruction, first->reg.index, *second))
        {
            return false;
        }
        assembly_emit_byte(builder, form->opcode_byte);
        return true;
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR2:
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT:
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE:
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE:
    case ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT:
    case ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE:
    case ASSEMBLY_AMD_ENCODING_VEX_FMA4:
    {
        AssemblyOperand rm = *second;
        AssemblyRegister selector = {0};
        u8 source = 0;
        u8 immediate = 0;
        bool has_selector = false;
        bool w = false;
        if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_SHIFT)
        {
            w = instruction->operands[2].kind == ASSEMBLY_OPERAND_MEMORY;
            rm = instruction->operands[w ? 2 : 1];
            source = instruction->operands[w ? 1 : 2].reg.index;
        }
        else if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE)
        {
            rm = instruction->operands[2];
            source = instruction->operands[1].reg.index;
        }
        else if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR4_SELECT)
        {
            source = instruction->operands[1].reg.index;
            w = instruction->operands[3].kind == ASSEMBLY_OPERAND_MEMORY;
            rm = instruction->operands[w ? 3 : 2];
            selector = instruction->operands[w ? 2 : 3].reg;
            has_selector = true;
        }
        else if (form->encoding == ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE || form->encoding == ASSEMBLY_AMD_ENCODING_VEX_FMA4)
        {
            source = instruction->operands[1].reg.index;
            bool memory_second = instruction->operands[2].kind == ASSEMBLY_OPERAND_MEMORY;
            bool memory_third = instruction->operands[3].kind == ASSEMBLY_OPERAND_MEMORY;
            w = memory_third || (!memory_second && (form->flags & ASSEMBLY_AMD_FORM_DEFAULT_W1));
            rm = instruction->operands[w ? 3 : 2];
            selector = instruction->operands[w ? 2 : 3].reg;
            has_selector = true;
            if (form->encoding == ASSEMBLY_AMD_ENCODING_VEX_VECTOR5_IMMEDIATE)
            {
                immediate = (u8)instruction->operands[4].expression.addend;
            }
        }
        assembly_x86_emit_amd_prefix(builder, form, first->reg, rm, first->reg.width, source, w);
        assembly_emit_byte(builder, form->opcode_byte);
        if (!assembly_x86_emit_rm(builder, instruction, first->reg.index, rm))
        {
            return false;
        }
        if (has_selector)
        {
            assembly_emit_byte(builder, (u8)((selector.index << 4) | immediate));
        }
        else if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_IMMEDIATE)
        {
            assembly_emit_byte(builder, (u8)instruction->operands[2].expression.addend);
        }
        else if (form->encoding == ASSEMBLY_AMD_ENCODING_XOP_VECTOR3_COMPARE_IMMEDIATE)
        {
            assembly_emit_byte(builder, (u8)instruction->operands[3].expression.addend);
        }
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_GPR2:
    {
        AssemblyOperand rm = *second;
        AssemblyRegister fixed = {.index = form->fixed_reg, .width = first->reg.width, .class = ASSEMBLY_REGISTER_GPR};
        assembly_x86_emit_amd_prefix(builder, form, fixed, rm, first->reg.width, first->reg.index, first->reg.width == 64);
        assembly_emit_byte(builder, form->opcode_byte);
        return assembly_x86_emit_rm(builder, instruction, form->fixed_reg, rm);
    }
    case ASSEMBLY_AMD_ENCODING_XOP_GPR3_BEXTR:
    {
        AssemblyOperand rm = *second;
        assembly_x86_emit_amd_prefix(builder, form, first->reg, rm, first->reg.width, 0, first->reg.width == 64);
        assembly_emit_byte(builder, form->opcode_byte);
        if (!assembly_x86_emit_rm(builder, instruction, first->reg.index, rm))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)instruction->operands[2].expression.addend, 4);
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_LWP1:
    {
        AssemblyRegister fixed = {.index = form->fixed_reg, .width = 64, .class = ASSEMBLY_REGISTER_GPR};
        assembly_x86_emit_amd_prefix(builder, form, fixed, *first, 64, 0, true);
        assembly_emit_byte(builder, form->opcode_byte);
        assembly_x86_emit_modrm(builder, form->fixed_reg, first->reg.index);
        return true;
    }
    case ASSEMBLY_AMD_ENCODING_XOP_LWP3:
    {
        AssemblyOperand rm = *second;
        AssemblyRegister fixed = {.index = form->fixed_reg, .width = 64, .class = ASSEMBLY_REGISTER_GPR};
        assembly_x86_emit_amd_prefix(builder, form, fixed, rm, 64, first->reg.index, true);
        assembly_emit_byte(builder, form->opcode_byte);
        if (!assembly_x86_emit_rm(builder, instruction, form->fixed_reg, rm))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)instruction->operands[2].expression.addend, 4);
        return true;
    }
    }
    return false;
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
    else if (sae)
    {
        p2 &= (u8)~0x60;
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
    u8 mask = first->has_mask ? first->mask : 0;
    u8 zeroing = !mask_destination && first->zeroing;
    u8 rounding = first->rounding;
    u8 sae = first->sae;
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
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_CALL || instruction->opcode == ASSEMBLY_OPCODE_X86_JMP)
    {
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_CALL ? 2 : 4;
        AssemblyRegister group_register = {.index = group};
        assembly_emit_byte(builder, 0xd5);
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_emit_byte(builder, assembly_x86_apx_memory_rex2_byte(0, group_register, first.memory));
        }
        else
        {
            assembly_emit_byte(builder, assembly_x86_rex2_byte(0, group_register, first.reg));
        }
        assembly_emit_byte(builder, 0xff);
        return first.kind == ASSEMBLY_OPERAND_MEMORY
                   ? assembly_x86_emit_memory(builder, instruction, group, first.memory)
                   : (assembly_x86_emit_modrm(builder, group, first.reg.index), true);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_LEA)
    {
        AssemblyRegister destination = first.reg;
        AssemblyMemory memory = instruction->operands[1].memory;
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        assembly_emit_byte(builder, 0xd5);
        assembly_emit_byte(builder, assembly_x86_apx_memory_rex2_byte(instruction->width, destination, memory));
        assembly_emit_byte(builder, 0x8d);
        return assembly_x86_emit_memory(builder, instruction, destination.index, memory);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ||
        instruction->opcode == ASSEMBLY_OPCODE_X86_NEG || instruction->opcode == ASSEMBLY_OPCODE_X86_NOT)
    {
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_INC   ? 0
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ? 1
                   : instruction->opcode == ASSEMBLY_OPCODE_X86_NOT ? 2
                                                                      : 3;
        u8 byte = instruction->width == 8;
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        assembly_emit_byte(builder, 0xd5);
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_emit_byte(builder, assembly_x86_apx_memory_rex2_byte(instruction->width, (AssemblyRegister){.index = group},
                                                                            first.memory));
        }
        else
        {
            assembly_emit_byte(builder, assembly_x86_rex2_byte(instruction->width, (AssemblyRegister){.index = group}, first.reg));
        }
        assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC
                                         ? (byte ? 0xfe : 0xff)
                                         : (byte ? 0xf6 : 0xf7));
        return first.kind == ASSEMBLY_OPERAND_MEMORY ? assembly_x86_emit_memory(builder, instruction, group, first.memory)
                                                     : (assembly_x86_emit_modrm(builder, group, first.reg.index), true);
    }
    if (assembly_x86_opcode_is_sse2(instruction->opcode))
    {
        u8 move = instruction->opcode >= ASSEMBLY_OPCODE_X86_MOVAPS && instruction->opcode <= ASSEMBLY_OPCODE_X86_MOVDQU;
        u8 load = (u8)(!move || first.kind == ASSEMBLY_OPERAND_REGISTER);
        AssemblyRegister reg = load ? first.reg : instruction->operands[1].reg;
        AssemblyOperand rm = load ? instruction->operands[1] : first;
        assembly_x86_emit_sse_prefix(builder, instruction->opcode);
        assembly_emit_byte(builder, 0xd5);
        if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_emit_byte(builder, (u8)(assembly_x86_apx_memory_rex2_byte(0, reg, rm.memory) | 0x80));
        }
        else
        {
            assembly_emit_byte(builder, (u8)(assembly_x86_rex2_byte(0, reg, rm.reg) | 0x80));
        }
        u8 operation = instruction->opcode == ASSEMBLY_OPCODE_X86_MOVAPS ? (load ? 0x28 : 0x29)
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
        assembly_emit_byte(builder, operation);
        return rm.kind == ASSEMBLY_OPERAND_MEMORY ? assembly_x86_emit_memory(builder, instruction, reg.index, rm.memory)
                                                   : (assembly_x86_emit_modrm(builder, reg.index, rm.reg.index), true);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        AssemblyRegister destination = first.reg;
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        if (instruction->operand_count == 1)
        {
            AssemblyRegister group = {.index = 5};
            assembly_emit_byte(builder, 0xd5);
            assembly_emit_byte(builder, (u8)(first.kind == ASSEMBLY_OPERAND_MEMORY
                                                 ? assembly_x86_apx_memory_rex2_byte(instruction->width, group, first.memory)
                                                 : assembly_x86_rex2_byte(instruction->width, group, destination)));
            assembly_emit_byte(builder, instruction->width == 8 ? 0xf6 : 0xf7);
            return first.kind == ASSEMBLY_OPERAND_MEMORY
                       ? assembly_x86_emit_memory(builder, instruction, 5, first.memory)
                       : (assembly_x86_emit_modrm(builder, 5, destination.index), true);
        }
        AssemblyOperand source = instruction->operands[1];
        AssemblyRegister source_register = source.kind == ASSEMBLY_OPERAND_REGISTER ? source.reg : (AssemblyRegister){0};
        u8 operation = instruction->operand_count == 2 ? 0xaf : 0x6b;
        u8 immediate_size = 0;
        if (instruction->operand_count == 3)
        {
            u16 full_width = instruction->width == 64 ? 32 : instruction->width;
            immediate_size = instruction->operands[2].expression.addend >= INT8_MIN &&
                                     instruction->operands[2].expression.addend <= INT8_MAX
                                 ? 1
                                 : (u8)(full_width / 8);
            if (immediate_size != 1)
            {
                operation = 0x69;
            }
        }
        assembly_emit_byte(builder, 0xd5);
        assembly_emit_byte(builder, (u8)((source.kind == ASSEMBLY_OPERAND_MEMORY
                                               ? assembly_x86_apx_memory_rex2_byte(instruction->width, destination, source.memory)
                                               : assembly_x86_rex2_byte(instruction->width, destination, source_register)) |
                                          (instruction->operand_count == 2 ? 0x80 : 0)));
        assembly_emit_byte(builder, operation);
        if (source.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_emit_memory(builder, instruction, destination.index, source.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, destination.index, source_register.index);
        }
        if (instruction->operand_count == 3)
        {
            assembly_emit_immediate(builder, (u64)instruction->operands[2].expression.addend, immediate_size);
        }
        return true;
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_SHL ? 4 : instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ? 5 : 7;
        AssemblyRegister group_register = {.index = group};
        AssemblyOperand count = instruction->operands[1];
        u8 immediate = count.kind == ASSEMBLY_OPERAND_EXPRESSION;
        u8 operation = 0;
        if (immediate)
        {
            operation = count.expression.addend == 1 ? (instruction->width == 8 ? 0xd0 : 0xd1)
                                                     : (instruction->width == 8 ? 0xc0 : 0xc1);
        }
        else
        {
            operation = instruction->width == 8 ? 0xd2 : 0xd3;
        }
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        assembly_emit_byte(builder, 0xd5);
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            assembly_emit_byte(builder, assembly_x86_apx_memory_rex2_byte(instruction->width, group_register, first.memory));
        }
        else
        {
            assembly_emit_byte(builder, assembly_x86_rex2_byte(instruction->width, group_register, first.reg));
        }
        assembly_emit_byte(builder, operation);
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_emit_memory(builder, instruction, group, first.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, group, first.reg.index);
        }
        if (immediate && count.expression.addend != 1)
        {
            assembly_emit_byte(builder, (u8)count.expression.addend);
        }
        return true;
    }
    AssemblyOperand second = instruction->operands[1];
    if (instruction->operand_count == 2 && second.kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        u8 memory = first.kind == ASSEMBLY_OPERAND_MEMORY;
        if (instruction->width == 16)
        {
            assembly_emit_byte(builder, 0x66);
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_MOV)
        {
            u8 register_immediate_64 = (u8)(!memory && instruction->width == 64 &&
                                            !assembly_x86_immediate_fits(second.expression.addend, 32, true));
            u8 operation = memory ? (instruction->width == 8 ? 0xc6 : 0xc7)
                                  : instruction->width == 8 ? (u8)(0xb0 + (first.reg.index & 7))
                                  : instruction->width == 64 && !register_immediate_64 ? 0xc7
                                  : instruction->width == 64 ? (u8)(0xb8 + (first.reg.index & 7))
                                                             : (u8)(0xb8 + (first.reg.index & 7));
            assembly_emit_byte(builder, 0xd5);
            assembly_emit_byte(builder, memory
                                             ? assembly_x86_apx_memory_rex2_byte(instruction->width, (AssemblyRegister){0}, first.memory)
                                             : assembly_x86_rex2_byte(instruction->width, (AssemblyRegister){0}, first.reg));
            assembly_emit_byte(builder, operation);
            if (memory || (instruction->width == 64 && !register_immediate_64))
            {
                if (memory)
                {
                    if (!assembly_x86_emit_memory(builder, instruction, 0, first.memory))
                    {
                        return false;
                    }
                }
                else
                {
                    assembly_x86_emit_modrm(builder, 0, first.reg.index);
                }
            }
            assembly_emit_immediate(builder, (u64)second.expression.addend,
                                    register_immediate_64 ? 8 : (u8)(instruction->width == 64 ? 4 : instruction->width / 8));
            return true;
        }
        if (!assembly_x86_apx_legacy_immediate_encoding(instruction->opcode, instruction->width, second.expression.addend,
                                                         &immediate_opcode, &immediate_size))
        {
            return false;
        }
        u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_TEST ? 0 : assembly_x86_apx_binary_immediate_extension(instruction->opcode);
        AssemblyRegister group = {.index = extension};
        assembly_emit_byte(builder, 0xd5);
        assembly_emit_byte(builder, memory ? assembly_x86_apx_memory_rex2_byte(instruction->width, group, first.memory)
                                           : assembly_x86_rex2_byte(instruction->width, group, first.reg));
        assembly_emit_byte(builder, immediate_opcode);
        if (memory)
        {
            if (!assembly_x86_emit_memory(builder, instruction, extension, first.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, extension, first.reg.index);
        }
        assembly_emit_immediate(builder, (u64)second.expression.addend, immediate_size);
        return true;
    }
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

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_apx_prefix(AssemblyBuilder* builder, AssemblyRegister destination,
                                                       AssemblyRegister reg, AssemblyOperand rm, u16 width, u8 ndd,
                                                       u8 no_flags)
{
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
    else if (rm.kind == ASSEMBLY_OPERAND_MEMORY)
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
    u8 p1 = (u8)((width == 64 ? 0x80 : 0) | (width == 16 ? 0x01 : 0) | (ndd ? ((~destination.index & 15) << 3) | 0x04 : 0x7c));
    if (rm.kind == ASSEMBLY_OPERAND_MEMORY && rm.memory.has_index && rm.memory.index.index & 16)
    {
        p1 &= (u8)~0x04;
    }
    u8 p2 = ndd ? (u8)(0x10 | (no_flags ? 0x04 : 0) | (destination.index < 16 ? 0x08 : 0)) : 0x0c;
    assembly_emit_byte(builder, 0x62);
    assembly_emit_byte(builder, p0);
    assembly_emit_byte(builder, p1);
    assembly_emit_byte(builder, p2);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_apx_nf(AssemblyBuilder* builder, AssemblyInstruction* instruction)
{
    if (instruction->lock_prefix)
    {
        return false;
    }
    AssemblyOperand first = instruction->operands[0];
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ||
        instruction->opcode == ASSEMBLY_OPCODE_X86_NEG)
    {
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_INC ? 0 : instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ? 1 : 3;
        AssemblyRegister group_register = {.index = group};
        assembly_x86_emit_apx_prefix(builder, (AssemblyRegister){0}, group_register, first, instruction->width, 0, 0);
        assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_NEG
                                         ? (instruction->width == 8 ? 0xf6 : 0xf7)
                                         : (instruction->width == 8 ? 0xfe : 0xff));
        return first.kind == ASSEMBLY_OPERAND_MEMORY ? assembly_x86_emit_memory(builder, instruction, group, first.memory)
                                                     : (assembly_x86_emit_modrm(builder, group, first.reg.index), true);
    }
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        AssemblyOperand source = instruction->operands[1];
        assembly_x86_emit_apx_prefix(builder, (AssemblyRegister){0}, first.reg, source, instruction->width, 0, 0);
        u8 operation = 0xaf;
        u8 immediate_size = 0;
        if (instruction->operand_count == 3)
        {
            u16 full_width = instruction->width == 64 ? 32 : instruction->width;
            immediate_size = instruction->operands[2].expression.addend >= INT8_MIN &&
                                     instruction->operands[2].expression.addend <= INT8_MAX
                                 ? 1
                                 : (u8)(full_width / 8);
            operation = immediate_size == 1 ? 0x6b : 0x69;
        }
        assembly_emit_byte(builder, operation);
        if (source.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_emit_memory(builder, instruction, first.reg.index, source.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, first.reg.index, source.reg.index);
        }
        if (instruction->operand_count == 3)
        {
            assembly_emit_immediate(builder, (u64)instruction->operands[2].expression.addend, immediate_size);
        }
        return true;
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_SHL ? 4 : instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ? 5 : 7;
        AssemblyOperand count = instruction->operands[1];
        u8 immediate = count.kind == ASSEMBLY_OPERAND_EXPRESSION;
        u8 operation = immediate ? (count.expression.addend == 1 ? (instruction->width == 8 ? 0xd0 : 0xd1)
                                                                  : (instruction->width == 8 ? 0xc0 : 0xc1))
                                 : (instruction->width == 8 ? 0xd2 : 0xd3);
        AssemblyRegister group_register = {.index = group};
        assembly_x86_emit_apx_prefix(builder, (AssemblyRegister){0}, group_register, first, instruction->width, 0, 0);
        assembly_emit_byte(builder, operation);
        if (first.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_emit_memory(builder, instruction, group, first.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, group, first.reg.index);
        }
        if (immediate && count.expression.addend != 1)
        {
            assembly_emit_byte(builder, (u8)count.expression.addend);
        }
        return true;
    }
    AssemblyOperand second = instruction->operands[1];
    if (second.kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        if (!assembly_x86_apx_immediate_encoding(instruction->width, second.expression.addend, &immediate_opcode, &immediate_size))
        {
            return false;
        }
        AssemblyRegister destination = first.kind == ASSEMBLY_OPERAND_REGISTER ? first.reg : (AssemblyRegister){0};
        u8 extension = assembly_x86_apx_binary_immediate_extension(instruction->opcode);
        AssemblyRegister group = {.index = extension};
        assembly_x86_emit_apx_prefix(builder, destination, group, first, instruction->width, 0, 0);
        assembly_emit_byte(builder, immediate_opcode);
        if (!assembly_x86_emit_rm(builder, instruction, extension, first))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)second.expression.addend, immediate_size);
        return true;
    }
    u8 load = second.kind == ASSEMBLY_OPERAND_MEMORY;
    AssemblyRegister reg = load ? first.reg : second.reg;
    AssemblyOperand rm = load ? second : first;
    assembly_x86_emit_apx_prefix(builder, (AssemblyRegister){0}, reg, rm, instruction->width, 0, 0);
    u8 operation = assembly_x86_apx_binary_opcode(instruction->opcode);
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
    if (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        AssemblyRegister destination = instruction->operands[0].reg;
        AssemblyOperand source_1 = instruction->operands[1];
        AssemblyOperand source_2 = instruction->operands[2];
        if (source_2.kind == ASSEMBLY_OPERAND_EXPRESSION)
        {
            return false;
        }
        assembly_x86_emit_apx_prefix(builder, destination, source_1.reg, source_2, instruction->width, 1, instruction->no_flags);
        assembly_emit_byte(builder, 0xaf);
        return source_2.kind == ASSEMBLY_OPERAND_MEMORY
                   ? assembly_x86_emit_memory(builder, instruction, source_1.reg.index, source_2.memory)
                   : (assembly_x86_emit_modrm(builder, source_1.reg.index, source_2.reg.index), true);
    }
    if (assembly_x86_opcode_is_shift(instruction->opcode))
    {
        AssemblyRegister destination = instruction->operands[0].reg;
        AssemblyOperand source = instruction->operands[1];
        AssemblyOperand count = instruction->operands[2];
        u8 group = instruction->opcode == ASSEMBLY_OPCODE_X86_SHL ? 4 : instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ? 5 : 7;
        u8 immediate = count.kind == ASSEMBLY_OPERAND_EXPRESSION;
        u8 operation = immediate ? (count.expression.addend == 1 ? (instruction->width == 8 ? 0xd0 : 0xd1)
                                                                  : (instruction->width == 8 ? 0xc0 : 0xc1))
                                 : (instruction->width == 8 ? 0xd2 : 0xd3);
        AssemblyRegister group_register = {.index = group};
        assembly_x86_emit_apx_prefix(builder, destination, group_register, source, instruction->width, 1, instruction->no_flags);
        assembly_emit_byte(builder, operation);
        if (source.kind == ASSEMBLY_OPERAND_MEMORY)
        {
            if (!assembly_x86_emit_memory(builder, instruction, group, source.memory))
            {
                return false;
            }
        }
        else
        {
            assembly_x86_emit_modrm(builder, group, source.reg.index);
        }
        if (immediate && count.expression.addend != 1)
        {
            assembly_emit_byte(builder, (u8)count.expression.addend);
        }
        return true;
    }
    AssemblyRegister destination = instruction->operands[0].reg;
    AssemblyOperand source_1 = instruction->operands[1];
    AssemblyOperand source_2 = instruction->operands[2];
    if (source_2.kind == ASSEMBLY_OPERAND_EXPRESSION)
    {
        u8 immediate_opcode = 0;
        u8 immediate_size = 0;
        if (!assembly_x86_apx_immediate_encoding(instruction->width, source_2.expression.addend, &immediate_opcode, &immediate_size))
        {
            return false;
        }
        u8 extension = assembly_x86_apx_binary_immediate_extension(instruction->opcode);
        AssemblyRegister group = {.index = extension};
        assembly_x86_emit_apx_prefix(builder, destination, group, source_1, instruction->width, 1, instruction->no_flags);
        assembly_emit_byte(builder, immediate_opcode);
        if (!assembly_x86_emit_rm(builder, instruction, extension, source_1))
        {
            return false;
        }
        assembly_emit_immediate(builder, (u64)source_2.expression.addend, immediate_size);
        return true;
    }
    u8 load = source_2.kind == ASSEMBLY_OPERAND_MEMORY;
    AssemblyRegister reg = load ? source_1.reg : source_2.reg;
    AssemblyOperand rm = load ? source_2 : source_1;
    assembly_x86_emit_apx_prefix(builder, destination, reg, rm, instruction->width, 1, instruction->no_flags);
    u8 operation = assembly_x86_apx_binary_opcode(instruction->opcode);
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_emit_amx_memory(AssemblyBuilder* builder, AssemblyInstruction* instruction, u8 reg,
                                                       AssemblyMemory memory, bool forced_sib)
{
    if (forced_sib && memory.rip_relative)
    {
        return false;
    }
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

BUSTER_GLOBAL_LOCAL bool assembly_aarch64_target_difference(s64 target, u64 place, s64* difference)
{
    if (!difference)
    {
        return false;
    }
    if (target >= 0)
    {
        u64 target_unsigned = (u64)target;
        if (target_unsigned >= place)
        {
            u64 magnitude = target_unsigned - place;
            if (magnitude > (u64)INT64_MAX)
            {
                return false;
            }
            *difference = (s64)magnitude;
            return true;
        }
        u64 magnitude = place - target_unsigned;
        if (magnitude > (u64)INT64_MAX + 1)
        {
            return false;
        }
        *difference = magnitude == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)magnitude;
        return true;
    }
    u64 target_magnitude = (u64)(-(target + 1)) + 1;
    if (place > (u64)INT64_MAX + 1 - target_magnitude)
    {
        return false;
    }
    u64 magnitude = place + target_magnitude;
    *difference = magnitude == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)magnitude;
    return true;
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
        if (instruction->metadata)
        {
            if (!assembly_x86_metadata_emit(builder, instruction))
            {
                return;
            }
            continue;
        }
        if (instruction->encoding_kind == ASSEMBLY_ENCODING_AARCH64_FIXED_WORD)
        {
            assembly_emit_u32(builder, instruction->fixed_word);
            continue;
        }
        if (instruction->encoding_kind == ASSEMBLY_ENCODING_AARCH64_M1_GPR)
        {
            A64GprOperand gpr_operands[4] = {0};
            for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
            {
                AssemblyOperand operand = instruction->operands[operand_index];
                gpr_operands[operand_index] = (A64GprOperand){
                    .index = operand.reg.index,
                    .width = (u8)operand.reg.width,
                    .stack_pointer = operand.reg.stack_pointer,
                };
            }
            u32 word = 0;
            if (!a64_arm_m1_gpr_encode(builder->target, instruction->aarch64_gpr_form_index, gpr_operands, instruction->operand_count, &word))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, instruction->line, instruction->column, 1,
                                    S8("AArch64 instruction could not be encoded"));
                return;
            }
            assembly_emit_u32(builder, word);
            continue;
        }
        if (instruction->encoding_kind == ASSEMBLY_ENCODING_AARCH64_CONTROL)
        {
            u32 word = 0;
            if (!buster_aarch64_control_semantic_encode(&instruction->aarch64_control_instruction, &word))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, instruction->line, instruction->column, 1,
                                    S8("AArch64 control instruction could not be encoded"));
                return;
            }
            for (u32 operand_index = 0; operand_index < 4; operand_index += 1)
            {
                if (!(instruction->aarch64_control_expression_mask & (u8)(1u << operand_index)))
                {
                    continue;
                }
                AssemblyExpression expression = instruction->aarch64_control_expressions[operand_index];
                s64 target = 0;
                if (!assembly_expression_target(builder, expression, &target))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("AArch64 control relocation requires a local label"));
                    return;
                }
                if (target < 0 || (u64)target > UINT64_MAX)
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("AArch64 control target is out of range"));
                    return;
                }
                u32 patched = 0;
                BusterAarch64ControlFixupResult fixup = {0};
                if (!buster_aarch64_control_semantic_fixup(
                        instruction->aarch64_control_row_index, word,
                        (BusterAarch64ControlFixupRequest){
                            .target = builder->target,
                            .place_address = instruction->offset,
                            .target_address = (u64)target,
                            .symbol_defined = true,
                        },
                        &patched, &fixup))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("AArch64 control target is out of range or unaligned"));
                    return;
                }
                word = patched;
            }
            assembly_emit_u32(builder, word);
            continue;
        }
        if (instruction->encoding_kind == ASSEMBLY_ENCODING_AARCH64_M1_SCALAR_INTEGER)
        {
            u32 word = 0;
            if (!a64_arm_m1_scalar_integer_encode(builder->target, instruction->aarch64_scalar_integer_form_index,
                                                  instruction->aarch64_scalar_integer_operands,
                                                  instruction->aarch64_scalar_integer_operand_count,
                                                  instruction->aarch64_scalar_integer_modifiers,
                                                  instruction->aarch64_scalar_integer_modifier_count, &word))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, instruction->line, instruction->column, 1,
                                    S8("AArch64 instruction could not be encoded"));
                return;
            }
            assembly_emit_u32(builder, word);
            continue;
        }
        if (instruction->lock_prefix)
        {
            assembly_emit_byte(builder, 0xf0);
        }
        if (instruction->amd_form)
        {
            if (!assembly_x86_emit_amd(builder, instruction))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, instruction->line, instruction->column, 1,
                                    S8("AMD instruction memory displacement is out of range"));
                return;
            }
            continue;
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
        if (instruction->no_flags && assembly_x86_opcode_is_apx_nf(instruction->opcode) &&
            (instruction->operand_count == 1 || instruction->operand_count == 2 ||
             (instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operand_count == 3 &&
              instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION)))
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
            (assembly_x86_opcode_is_apx_ndd(instruction->opcode) && instruction->operand_count == 3 &&
             !(instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->operands[2].kind == ASSEMBLY_OPERAND_EXPRESSION)))
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
        A64Opcode opcode = instruction->opcode == ASSEMBLY_OPCODE_AARCH64_NOP   ? A64_OPCODE_NOP
                           : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_RET ? A64_OPCODE_RET
                           : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL  ? A64_OPCODE_BL
                                                                                : A64_OPCODE_B;
        A64MCInst exact = {
            .opcode = opcode,
            .operand_count = opcode == A64_OPCODE_NOP ? 0 : 1,
        };
        if (opcode == A64_OPCODE_RET)
        {
            exact.operands[0] = (A64MCOperand){.value = 30, .kind = A64_MC_OPERAND_REGISTER};
        }
        else if (opcode == A64_OPCODE_B || opcode == A64_OPCODE_BL)
        {
            exact.operands[0] = (A64MCOperand){.kind = A64_MC_OPERAND_PC_RELATIVE};
        }
        u32 word = 0;
        if (!a64_mc_encode(&exact, &word))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, instruction->line, instruction->column, 1,
                                S8("AArch64 instruction could not be encoded"));
            return;
        }
        if (instruction->opcode == ASSEMBLY_OPCODE_AARCH64_B || instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL)
        {
            s64 target = 0;
            if (assembly_expression_target(builder, instruction->operands[0].expression, &target))
            {
                s64 displacement = 0;
                if (!assembly_aarch64_target_difference(target, instruction->offset, &displacement) ||
                    !a64_pc_relative_patch(opcode, word, displacement, &word))
                {
                    assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                        S8("AArch64 branch target is out of range or unaligned"));
                }
            }
            else if (!assembly_relocation_append(builder, instruction->offset, instruction->operands[0].expression,
                                                 opcode == A64_OPCODE_BL ? ASSEMBLY_RELOCATION_AARCH64_CALL26
                                                                         : ASSEMBLY_RELOCATION_AARCH64_JUMP26,
                                                 0))
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
    if (line_count > UINT32_MAX / BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY ||
        line_count > UINT32_MAX / ASSEMBLY_SOURCE_SYMBOLS_PER_LINE)
    {
        return empty;
    }
    AssemblyBuilder builder = {
        .arena = arena,
        .target = options.target,
        .instruction_capacity = line_count,
        .symbol_capacity = line_count * ASSEMBLY_SOURCE_SYMBOLS_PER_LINE,
        .relocation_capacity = line_count * BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY,
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

#if BUSTER_INCLUDE_TESTS
bool assembly_test_split_operands(String8 source, String8* operands, u32 operand_capacity, u32* operand_count)
{
    if (!operand_count || (source.length && !source.pointer) || (operand_capacity && !operands))
    {
        return false;
    }
    *operand_count = 0;
    u64 cursor = 0;
    while (cursor < source.length)
    {
        if (*operand_count >= operand_capacity || *operand_count >= ASSEMBLY_MAX_OPERANDS ||
            assembly_operand_split_next(source, &cursor, operands + *operand_count) != ASSEMBLY_OPERAND_SPLIT_SUCCESS)
        {
            return false;
        }
        *operand_count += 1;
    }
    return true;
}
#endif
