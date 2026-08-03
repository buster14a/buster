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
    ASSEMBLY_OPCODE_X86_SUB,
    ASSEMBLY_OPCODE_X86_AND,
    ASSEMBLY_OPCODE_X86_OR,
    ASSEMBLY_OPCODE_X86_XOR,
    ASSEMBLY_OPCODE_X86_CMP,
    ASSEMBLY_OPCODE_X86_TEST,
    ASSEMBLY_OPCODE_X86_IMUL,
    ASSEMBLY_OPCODE_X86_PUSH,
    ASSEMBLY_OPCODE_X86_POP,
    ASSEMBLY_OPCODE_X86_INC,
    ASSEMBLY_OPCODE_X86_DEC,
    ASSEMBLY_OPCODE_X86_NEG,
    ASSEMBLY_OPCODE_X86_NOT,
    ASSEMBLY_OPCODE_X86_SHL,
    ASSEMBLY_OPCODE_X86_SHR,
    ASSEMBLY_OPCODE_X86_SAR,
    ASSEMBLY_OPCODE_X86_CBW,
    ASSEMBLY_OPCODE_X86_CWDE,
    ASSEMBLY_OPCODE_X86_CDQE,
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
struct AssemblyRegister
{
    u8 index;
    u8 width;
};

typedef struct AssemblyOperand AssemblyOperand;
struct AssemblyOperand
{
    AssemblyExpression expression;
    AssemblyRegister reg;
    AssemblyOperandKind kind;
};

typedef struct AssemblyInstruction AssemblyInstruction;
struct AssemblyInstruction
{
    AssemblyOperand operands[3];
    u64 offset;
    u32 line;
    u32 column;
    u32 size;
    AssemblyOpcode opcode;
    u8 operand_count;
    u8 width;
    u8 reserved[2];
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
    static String8 const names_8[] = {
        S8_INITIALIZER("al"), S8_INITIALIZER("cl"), S8_INITIALIZER("dl"), S8_INITIALIZER("bl"),
        S8_INITIALIZER("spl"), S8_INITIALIZER("bpl"), S8_INITIALIZER("sil"), S8_INITIALIZER("dil"),
        S8_INITIALIZER("r8b"), S8_INITIALIZER("r9b"), S8_INITIALIZER("r10b"), S8_INITIALIZER("r11b"),
        S8_INITIALIZER("r12b"), S8_INITIALIZER("r13b"), S8_INITIALIZER("r14b"), S8_INITIALIZER("r15b"),
    };
    static const struct
    {
        String8 const* names;
        u8 width;
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
    return false;
}

typedef struct AssemblyInstructionInfo AssemblyInstructionInfo;
struct AssemblyInstructionInfo
{
    AssemblyOpcode opcode;
    u8 operand_count;
    u8 suffix_width;
};

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
        {S8_INITIALIZER("add"), ASSEMBLY_OPCODE_X86_ADD, 2},      {S8_INITIALIZER("sub"), ASSEMBLY_OPCODE_X86_SUB, 2},
        {S8_INITIALIZER("and"), ASSEMBLY_OPCODE_X86_AND, 2},      {S8_INITIALIZER("or"), ASSEMBLY_OPCODE_X86_OR, 2},
        {S8_INITIALIZER("xor"), ASSEMBLY_OPCODE_X86_XOR, 2},      {S8_INITIALIZER("cmp"), ASSEMBLY_OPCODE_X86_CMP, 2},
        {S8_INITIALIZER("test"), ASSEMBLY_OPCODE_X86_TEST, 2},    {S8_INITIALIZER("imul"), ASSEMBLY_OPCODE_X86_IMUL, 2},
        {S8_INITIALIZER("push"), ASSEMBLY_OPCODE_X86_PUSH, 1},    {S8_INITIALIZER("pop"), ASSEMBLY_OPCODE_X86_POP, 1},
        {S8_INITIALIZER("inc"), ASSEMBLY_OPCODE_X86_INC, 1},      {S8_INITIALIZER("dec"), ASSEMBLY_OPCODE_X86_DEC, 1},
        {S8_INITIALIZER("neg"), ASSEMBLY_OPCODE_X86_NEG, 1},      {S8_INITIALIZER("not"), ASSEMBLY_OPCODE_X86_NOT, 1},
        {S8_INITIALIZER("shl"), ASSEMBLY_OPCODE_X86_SHL, 2},      {S8_INITIALIZER("sal"), ASSEMBLY_OPCODE_X86_SHL, 2},
        {S8_INITIALIZER("shr"), ASSEMBLY_OPCODE_X86_SHR, 2},      {S8_INITIALIZER("sar"), ASSEMBLY_OPCODE_X86_SAR, 2},
        {S8_INITIALIZER("cbw"), ASSEMBLY_OPCODE_X86_CBW, 0},      {S8_INITIALIZER("cwde"), ASSEMBLY_OPCODE_X86_CWDE, 0},
        {S8_INITIALIZER("cdqe"), ASSEMBLY_OPCODE_X86_CDQE, 0},    {S8_INITIALIZER("cbtw"), ASSEMBLY_OPCODE_X86_CBW, 0},
        {S8_INITIALIZER("cwtl"), ASSEMBLY_OPCODE_X86_CWDE, 0},    {S8_INITIALIZER("cltq"), ASSEMBLY_OPCODE_X86_CDQE, 0},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(instructions); index += 1)
    {
        if (assembly_word_equal(mnemonic, instructions[index].name))
        {
            *result = (AssemblyInstructionInfo){.opcode = instructions[index].opcode, .operand_count = instructions[index].operand_count};
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_instruction_lookup(Target target, AssemblySyntax syntax, String8 mnemonic, AssemblyInstructionInfo* result)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
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

BUSTER_GLOBAL_LOCAL bool assembly_x86_rex_needed(u8 width, AssemblyRegister first, AssemblyRegister second)
{
    return width == 64 || first.index >= 8 || second.index >= 8 || (width == 8 && (first.index >= 4 || second.index >= 4));
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_immediate_fits(s64 value, u8 width, bool signed_only)
{
    if (width == 64)
    {
        return true;
    }
    s64 signed_minimum = -(INT64_C(1) << (width - 1));
    u64 unsigned_maximum = (UINT64_C(1) << width) - 1;
    return value >= signed_minimum && (signed_only ? value <= (s64)(unsigned_maximum >> 1) : value < 0 || (u64)value <= unsigned_maximum);
}

BUSTER_GLOBAL_LOCAL bool assembly_x86_instruction_size(AssemblyInstruction* instruction)
{
    AssemblyOperand* first = instruction->operands;
    AssemblyOperand* second = instruction->operands + 1;
    AssemblyOpcode opcode = instruction->opcode;
    if (opcode == ASSEMBLY_OPCODE_X86_NOP || opcode == ASSEMBLY_OPCODE_X86_RET || opcode == ASSEMBLY_OPCODE_X86_INT3 ||
        opcode == ASSEMBLY_OPCODE_X86_CWDE)
    {
        instruction->size = 1;
        return instruction->operand_count == 0;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_CBW || opcode == ASSEMBLY_OPCODE_X86_CDQE)
    {
        instruction->size = 2;
        return instruction->operand_count == 0;
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
        opcode == ASSEMBLY_OPCODE_X86_NOT)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER)
        {
            return false;
        }
        instruction->width = first->reg.width;
        instruction->size = (instruction->width == 16 ? 1 : 0) + (assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0}) ? 1 : 0) + 2;
        return true;
    }
    if (opcode == ASSEMBLY_OPCODE_X86_SHL || opcode == ASSEMBLY_OPCODE_X86_SHR || opcode == ASSEMBLY_OPCODE_X86_SAR)
    {
        if (first->kind != ASSEMBLY_OPERAND_REGISTER || second->kind != ASSEMBLY_OPERAND_EXPRESSION || second->expression.has_symbol ||
            second->expression.addend < 0 || second->expression.addend > UINT8_MAX)
        {
            return false;
        }
        instruction->width = first->reg.width;
        instruction->size = (instruction->width == 16 ? 1 : 0) + (assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0}) ? 1 : 0) +
                            (second->expression.addend == 1 ? 2 : 3);
        return true;
    }
    if (first->kind != ASSEMBLY_OPERAND_REGISTER)
    {
        return false;
    }
    instruction->width = first->reg.width;
    if (second->kind == ASSEMBLY_OPERAND_REGISTER)
    {
        if (second->reg.width != instruction->width || (opcode != ASSEMBLY_OPCODE_X86_MOV && opcode != ASSEMBLY_OPCODE_X86_ADD &&
                                                        opcode != ASSEMBLY_OPCODE_X86_SUB && opcode != ASSEMBLY_OPCODE_X86_AND &&
                                                        opcode != ASSEMBLY_OPCODE_X86_OR && opcode != ASSEMBLY_OPCODE_X86_XOR &&
                                                        opcode != ASSEMBLY_OPCODE_X86_CMP && opcode != ASSEMBLY_OPCODE_X86_TEST &&
                                                        opcode != ASSEMBLY_OPCODE_X86_IMUL))
        {
            return false;
        }
        if (opcode == ASSEMBLY_OPCODE_X86_IMUL && instruction->width == 8)
        {
            return false;
        }
        instruction->size = (instruction->width == 16 ? 1 : 0) + (assembly_x86_rex_needed(instruction->width, first->reg, second->reg) ? 1 : 0) +
                            (opcode == ASSEMBLY_OPCODE_X86_IMUL ? 3 : 2);
        return true;
    }
    if (second->kind != ASSEMBLY_OPERAND_EXPRESSION || second->expression.has_symbol || opcode == ASSEMBLY_OPCODE_X86_IMUL)
    {
        return false;
    }
    s64 immediate = second->expression.addend;
    if (opcode == ASSEMBLY_OPCODE_X86_MOV)
    {
        if (!assembly_x86_immediate_fits(immediate, instruction->width, false))
        {
            return false;
        }
        u32 immediate_size = instruction->width / 8;
        instruction->size = (instruction->width == 16 ? 1 : 0) +
                            (assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0}) ? 1 : 0) + 1 + immediate_size;
        return true;
    }
    if (opcode != ASSEMBLY_OPCODE_X86_ADD && opcode != ASSEMBLY_OPCODE_X86_SUB && opcode != ASSEMBLY_OPCODE_X86_AND &&
        opcode != ASSEMBLY_OPCODE_X86_OR && opcode != ASSEMBLY_OPCODE_X86_XOR && opcode != ASSEMBLY_OPCODE_X86_CMP &&
        opcode != ASSEMBLY_OPCODE_X86_TEST)
    {
        return false;
    }
    bool signed_only = instruction->width == 64;
    u8 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
    if (!assembly_x86_immediate_fits(immediate, full_immediate_width, signed_only))
    {
        return false;
    }
    u32 immediate_size = opcode != ASSEMBLY_OPCODE_X86_TEST && immediate >= INT8_MIN && immediate <= INT8_MAX ? 1 : full_immediate_width / 8;
    instruction->size = (instruction->width == 16 ? 1 : 0) +
                        (assembly_x86_rex_needed(instruction->width, first->reg, (AssemblyRegister){0}) ? 1 : 0) + 2 + immediate_size;
    return true;
}

BUSTER_GLOBAL_LOCAL void assembly_instruction_parse(AssemblyBuilder* builder, String8 statement, u32 line, u32 column, u64 offset,
                                                    Target target, AssemblySyntax syntax)
{
    u64 mnemonic_end = 0;
    while (mnemonic_end < statement.length && !assembly_space(statement.pointer[mnemonic_end]))
    {
        mnemonic_end += 1;
    }
    String8 mnemonic = {.pointer = statement.pointer, .length = mnemonic_end};
    String8 operands = assembly_trim((String8){.pointer = statement.pointer + mnemonic_end, .length = statement.length - mnemonic_end});
    AssemblyInstructionInfo info = {.opcode = ASSEMBLY_OPCODE_COUNT};
    if (!assembly_instruction_lookup(target, syntax, mnemonic, &info))
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNKNOWN_INSTRUCTION, line, column, (u32)mnemonic.length, S8("unknown instruction"));
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
    };
    u8 parsed_operand_count = 0;
    u64 operand_start = 0;
    while (operand_start < operands.length && parsed_operand_count < BUSTER_ARRAY_LENGTH(instruction.operands))
    {
        u64 operand_end = operand_start;
        while (operand_end < operands.length && operands.pointer[operand_end] != ',')
        {
            operand_end += 1;
        }
        String8 text = assembly_trim(string_slice(operands, operand_start, operand_end));
        AssemblyOperand* operand = instruction.operands + parsed_operand_count;
        if (!text.length)
        {
            break;
        }
        bool branch = info.opcode == ASSEMBLY_OPCODE_X86_CALL || info.opcode == ASSEMBLY_OPCODE_X86_JMP;
        bool indirect = syntax == ASSEMBLY_SYNTAX_ATT && branch && text.length && text.pointer[0] == '*';
        if (indirect)
        {
            text.pointer += 1;
            text.length -= 1;
        }
        if (target.cpu_arch == CPU_ARCH_X86_64 && assembly_register_parse(text, syntax, &operand->reg))
        {
            if (syntax == ASSEMBLY_SYNTAX_ATT && branch && !indirect)
            {
                break;
            }
            operand->kind = ASSEMBLY_OPERAND_REGISTER;
        }
        else
        {
            bool immediate = syntax == ASSEMBLY_SYNTAX_ATT && text.pointer[0] == '$';
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
    if (parsed_operand_count != info.operand_count || operand_start < operands.length)
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                            (u32)operands.length, S8("invalid instruction operands"));
        return;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && syntax == ASSEMBLY_SYNTAX_ATT && parsed_operand_count == 2)
    {
        AssemblyOperand temporary = instruction.operands[0];
        instruction.operands[0] = instruction.operands[1];
        instruction.operands[1] = temporary;
    }
    if (info.suffix_width)
    {
        instruction.width = info.suffix_width;
        for (u32 operand_index = 0; operand_index < instruction.operand_count; operand_index += 1)
        {
            if (instruction.operands[operand_index].kind == ASSEMBLY_OPERAND_REGISTER && instruction.operands[operand_index].reg.width != info.suffix_width)
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                                    (u32)operands.length, S8("register width does not match mnemonic suffix"));
                return;
            }
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
            if (original.pointer[index] == ';' ||
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
                u32 instruction_count = builder->instruction_count;
                assembly_instruction_parse(builder, statement, line, column, output_offset, target, syntax);
                if (builder->instruction_count != instruction_count)
                {
                    output_offset += builder->instructions[builder->instruction_count - 1].size;
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

BUSTER_GLOBAL_LOCAL void assembly_x86_emit_prefix(AssemblyBuilder* builder, u8 width, AssemblyRegister reg, AssemblyRegister rm)
{
    if (width == 16)
    {
        assembly_emit_byte(builder, 0x66);
    }
    if (assembly_x86_rex_needed(width, reg, rm))
    {
        u8 rex = UINT8_C(0x40) | (width == 64 ? UINT8_C(0x08) : 0) | (reg.index >= 8 ? UINT8_C(0x04) : 0) |
                 (rm.index >= 8 ? UINT8_C(0x01) : 0);
        assembly_emit_byte(builder, rex);
    }
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
        if (instruction->opcode == ASSEMBLY_OPCODE_X86_CBW || instruction->opcode == ASSEMBLY_OPCODE_X86_CWDE ||
            instruction->opcode == ASSEMBLY_OPCODE_X86_CDQE)
        {
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_CBW)
            {
                assembly_emit_byte(builder, 0x66);
            }
            else if (instruction->opcode == ASSEMBLY_OPCODE_X86_CDQE)
            {
                assembly_emit_byte(builder, 0x48);
            }
            assembly_emit_byte(builder, 0x98);
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
                instruction->opcode == ASSEMBLY_OPCODE_X86_NEG || instruction->opcode == ASSEMBLY_OPCODE_X86_NOT)
            {
                assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                bool byte = instruction->width == 8;
                bool increment = instruction->opcode == ASSEMBLY_OPCODE_X86_INC || instruction->opcode == ASSEMBLY_OPCODE_X86_DEC;
                assembly_emit_byte(builder, increment ? (byte ? 0xfe : 0xff) : (byte ? 0xf6 : 0xf7));
                u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_INC   ? 0
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_DEC ? 1
                               : instruction->opcode == ASSEMBLY_OPCODE_X86_NOT ? 2
                                                                                : 3;
                assembly_x86_emit_modrm(builder, extension, first.reg.index);
                continue;
            }
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_SHL || instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ||
                instruction->opcode == ASSEMBLY_OPCODE_X86_SAR)
            {
                assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                bool one = second.expression.addend == 1;
                assembly_emit_byte(builder, one ? (instruction->width == 8 ? 0xd0 : 0xd1) : (instruction->width == 8 ? 0xc0 : 0xc1));
                u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_SHL ? 4 : instruction->opcode == ASSEMBLY_OPCODE_X86_SHR ? 5 : 7;
                assembly_x86_emit_modrm(builder, extension, first.reg.index);
                if (!one)
                {
                    assembly_emit_byte(builder, (u8)second.expression.addend);
                }
                continue;
            }
            if (second.kind == ASSEMBLY_OPERAND_REGISTER)
            {
                bool imul = instruction->opcode == ASSEMBLY_OPCODE_X86_IMUL;
                AssemblyRegister reg = imul ? first.reg : second.reg;
                AssemblyRegister rm = imul ? second.reg : first.reg;
                assembly_x86_emit_prefix(builder, instruction->width, reg, rm);
                if (imul)
                {
                    assembly_emit_byte(builder, 0x0f);
                    assembly_emit_byte(builder, 0xaf);
                }
                else
                {
                    u8 opcode = instruction->opcode == ASSEMBLY_OPCODE_X86_MOV    ? 0x89
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_ADD  ? 0x01
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_OR   ? 0x09
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_AND  ? 0x21
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB  ? 0x29
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_XOR  ? 0x31
                                : instruction->opcode == ASSEMBLY_OPCODE_X86_CMP  ? 0x39
                                                                                  : 0x85;
                    if (instruction->width == 8)
                    {
                        opcode -= 1;
                    }
                    assembly_emit_byte(builder, opcode);
                }
                assembly_x86_emit_modrm(builder, reg.index, rm.index);
                continue;
            }
            s64 immediate = second.expression.addend;
            if (instruction->opcode == ASSEMBLY_OPCODE_X86_MOV)
            {
                assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
                assembly_emit_byte(builder, (u8)((instruction->width == 8 ? 0xb0 : 0xb8) + (first.reg.index & 7)));
                assembly_emit_immediate(builder, (u64)immediate, instruction->width / 8);
                continue;
            }
            bool test = instruction->opcode == ASSEMBLY_OPCODE_X86_TEST;
            u8 full_immediate_width = instruction->width == 64 ? 32 : instruction->width;
            u8 immediate_size = !test && immediate >= INT8_MIN && immediate <= INT8_MAX ? 1 : (u8)(full_immediate_width / 8);
            assembly_x86_emit_prefix(builder, instruction->width, (AssemblyRegister){0}, first.reg);
            assembly_emit_byte(builder, test ? (instruction->width == 8 ? 0xf6 : 0xf7)
                                             : immediate_size == 1 ? (instruction->width == 8 ? 0x80 : 0x83)
                                                                   : (instruction->width == 8 ? 0x80 : 0x81));
            u8 extension = instruction->opcode == ASSEMBLY_OPCODE_X86_ADD   ? 0
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_OR  ? 1
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_AND ? 4
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_SUB ? 5
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_XOR ? 6
                           : instruction->opcode == ASSEMBLY_OPCODE_X86_CMP ? 7
                                                                            : 0;
            assembly_x86_emit_modrm(builder, extension, first.reg.index);
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
