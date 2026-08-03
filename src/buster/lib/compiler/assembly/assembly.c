#include <buster/lib/compiler/assembly/assembly.h>

#include <buster/lib/string.h>

typedef enum AssemblyOpcode
{
    ASSEMBLY_OPCODE_X86_NOP,
    ASSEMBLY_OPCODE_X86_RET,
    ASSEMBLY_OPCODE_X86_INT3,
    ASSEMBLY_OPCODE_X86_CALL,
    ASSEMBLY_OPCODE_X86_JMP,
    ASSEMBLY_OPCODE_AARCH64_NOP,
    ASSEMBLY_OPCODE_AARCH64_RET,
    ASSEMBLY_OPCODE_AARCH64_B,
    ASSEMBLY_OPCODE_AARCH64_BL,
    ASSEMBLY_OPCODE_COUNT,
} AssemblyOpcode;

typedef struct AssemblyExpression AssemblyExpression;
struct AssemblyExpression
{
    s64 addend;
    u32 symbol;
    bool has_symbol;
    u8 reserved[3];
};

typedef struct AssemblyInstruction AssemblyInstruction;
struct AssemblyInstruction
{
    AssemblyExpression operand;
    u64 offset;
    u32 line;
    u32 column;
    u32 size;
    AssemblyOpcode opcode;
    bool has_operand;
    u8 reserved[3];
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

BUSTER_GLOBAL_LOCAL bool assembly_instruction_lookup(Target target, String8 mnemonic, AssemblyOpcode* opcode, u32* size, bool* operand)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (assembly_word_equal(mnemonic, S8("nop")))
        {
            *opcode = ASSEMBLY_OPCODE_X86_NOP;
            *size = 1;
            *operand = false;
        }
        else if (assembly_word_equal(mnemonic, S8("ret")))
        {
            *opcode = ASSEMBLY_OPCODE_X86_RET;
            *size = 1;
            *operand = false;
        }
        else if (assembly_word_equal(mnemonic, S8("int3")))
        {
            *opcode = ASSEMBLY_OPCODE_X86_INT3;
            *size = 1;
            *operand = false;
        }
        else if (assembly_word_equal(mnemonic, S8("call")))
        {
            *opcode = ASSEMBLY_OPCODE_X86_CALL;
            *size = 5;
            *operand = true;
        }
        else if (assembly_word_equal(mnemonic, S8("jmp")))
        {
            *opcode = ASSEMBLY_OPCODE_X86_JMP;
            *size = 5;
            *operand = true;
        }
        else
        {
            return false;
        }
        return true;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (assembly_word_equal(mnemonic, S8("nop")))
        {
            *opcode = ASSEMBLY_OPCODE_AARCH64_NOP;
            *operand = false;
        }
        else if (assembly_word_equal(mnemonic, S8("ret")))
        {
            *opcode = ASSEMBLY_OPCODE_AARCH64_RET;
            *operand = false;
        }
        else if (assembly_word_equal(mnemonic, S8("b")))
        {
            *opcode = ASSEMBLY_OPCODE_AARCH64_B;
            *operand = true;
        }
        else if (assembly_word_equal(mnemonic, S8("bl")))
        {
            *opcode = ASSEMBLY_OPCODE_AARCH64_BL;
            *operand = true;
        }
        else
        {
            return false;
        }
        *size = 4;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void assembly_instruction_parse(AssemblyBuilder* builder, String8 statement, u32 line, u32 column, u64 offset,
                                                    Target target)
{
    u64 mnemonic_end = 0;
    while (mnemonic_end < statement.length && !assembly_space(statement.pointer[mnemonic_end]))
    {
        mnemonic_end += 1;
    }
    String8 mnemonic = {.pointer = statement.pointer, .length = mnemonic_end};
    String8 operands = assembly_trim((String8){.pointer = statement.pointer + mnemonic_end, .length = statement.length - mnemonic_end});
    AssemblyOpcode opcode = ASSEMBLY_OPCODE_COUNT;
    u32 size = 0;
    bool expects_operand = false;
    if (!assembly_instruction_lookup(target, mnemonic, &opcode, &size, &expects_operand))
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
        .size = size,
        .opcode = opcode,
        .has_operand = expects_operand,
    };
    if (expects_operand)
    {
        if (!assembly_expression_parse(builder, operands, &instruction.operand))
        {
            assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION, line, column + (u32)mnemonic_end,
                                (u32)operands.length, S8("invalid branch expression"));
            return;
        }
    }
    else if (operands.length)
    {
        assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_OPERANDS, line, column + (u32)mnemonic_end,
                            (u32)operands.length, S8("instruction takes no operands"));
        return;
    }
    builder->instructions[builder->instruction_count++] = instruction;
}

BUSTER_GLOBAL_LOCAL void assembly_source_parse(AssemblyBuilder* builder, String8 source, Target target)
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
                assembly_instruction_parse(builder, statement, line, column, output_offset, target);
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
            assembly_emit_byte(builder, instruction->opcode == ASSEMBLY_OPCODE_X86_CALL ? 0xe8 : 0xe9);
            s64 target = 0;
            u32 displacement = 0;
            if (assembly_expression_target(builder, instruction->operand, &target))
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
            else if (!assembly_relocation_append(builder, instruction->offset + 1, instruction->operand,
                                                 ASSEMBLY_RELOCATION_X86_PC32, -4))
            {
                assembly_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, instruction->line, instruction->column, 1,
                                    S8("x86 relocation addend is out of range"));
            }
            assembly_emit_u32(builder, displacement);
            continue;
        }
        u32 word = instruction->opcode == ASSEMBLY_OPCODE_AARCH64_NOP   ? UINT32_C(0xd503201f)
                   : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_RET ? UINT32_C(0xd65f03c0)
                   : instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL  ? UINT32_C(0x94000000)
                                                                        : UINT32_C(0x14000000);
        if (instruction->opcode == ASSEMBLY_OPCODE_AARCH64_B || instruction->opcode == ASSEMBLY_OPCODE_AARCH64_BL)
        {
            s64 target = 0;
            if (assembly_expression_target(builder, instruction->operand, &target))
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
            else if (!assembly_relocation_append(builder, instruction->offset, instruction->operand,
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
    assembly_source_parse(&builder, source, options.target);
    builder.result.bytes.pointer = arena_allocate(arena, u8, builder.output_capacity);
    assembly_instructions_emit(&builder);
    scratch_end(temporary);
    return builder.result;
}
