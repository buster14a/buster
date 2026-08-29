// The assembly translation unit: assembly_unit_encode at the bottom takes the
// complete text of a `.s` file and returns sections, symbols, relocations, and
// diagnostics. assembly.c's assembly_encode is the instruction layer beneath
// it and is called once per instruction line; everything a whole file has that
// a single statement does not lives here.
//
// Three passes over an AssemblyUnitBuilder sized from the line count:
//   assembly_unit_collect_numeric_labels  local `1:` definitions in source
//                                         order, so `1f`/`1b` can name one
//   assembly_unit_parse                   labels, directives, and one
//                                         assembly_encode call per
//                                         instruction line, each producing a
//                                         piece of bytes in a section
//   assembly_unit_materialize             pieces concatenated per section and
//                                         same-section PC-relative
//                                         relocations resolved in place
//
// Layout, in file order; each anchor is a definition to search for:
//   assembly_unit_space ..                 text plumbing shared by the passes
//   assembly_unit_diagnostic
//   assembly_unit_symbol_intern ..         symbol and section tables
//   assembly_unit_section_select
//   assembly_unit_evaluate                 directive operand expressions
//   assembly_unit_directive_*              the directive vocabulary
//   assembly_unit_instruction              the assembly_encode call
//   assembly_unit_parse, assembly_unit_encode
//
// Anything the vocabulary does not cover is refused with a diagnostic naming
// the directive and its line; nothing is silently dropped.

#include <buster/lib/compiler/assembly/assembly_unit.h>

#include <buster/lib/string.h>

enum
{
    ASSEMBLY_UNIT_SECTION_CAPACITY = 32,
    ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY = 16,
    ASSEMBLY_UNIT_OPERAND_CAPACITY = 64,
};

typedef struct AssemblyUnitPiece AssemblyUnitPiece;
struct AssemblyUnitPiece
{
    u8* bytes;
    u64 length;
    u32 section;
};

typedef struct AssemblyUnitNumericLabel AssemblyUnitNumericLabel;
struct AssemblyUnitNumericLabel
{
    String8 name;
    u64 value;
    u32 line;
};

typedef struct AssemblyUnitBuilder AssemblyUnitBuilder;
struct AssemblyUnitBuilder
{
    Arena* arena;
    Target target;
    AssemblySyntax syntax;
    AssemblyUnitResult result;
    u64* section_offsets;
    AssemblyUnitPiece* pieces;
    AssemblyUnitNumericLabel* numeric_labels;
    // The source line each relocation came from, kept beside the public array
    // so a displacement that does not fit can name the line that wrote it.
    u32* relocation_lines;
    u32 piece_count;
    u32 piece_capacity;
    u32 numeric_label_count;
    u32 numeric_label_capacity;
    u32 symbol_capacity;
    u32 relocation_capacity;
    u32 current_section;
    u32 line;
    u32 column;
};

BUSTER_GLOBAL_LOCAL bool assembly_unit_space(char8 value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\v' || value == '\f';
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_digit(char8 value)
{
    return value >= '0' && value <= '9';
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_name_start(char8 value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_' || value == '.' || value == '$';
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_name_character(char8 value)
{
    return assembly_unit_name_start(value) || assembly_unit_digit(value);
}

BUSTER_GLOBAL_LOCAL String8 assembly_unit_trim(String8 text)
{
    while (text.length && assembly_unit_space(text.pointer[0]))
    {
        text.pointer += 1;
        text.length -= 1;
    }
    while (text.length && assembly_unit_space(text.pointer[text.length - 1]))
    {
        text.length -= 1;
    }
    return text;
}

// The first whitespace-delimited word, and the rest of the line after it.
BUSTER_GLOBAL_LOCAL String8 assembly_unit_word(String8 text, String8* rest)
{
    u64 end = 0;
    while (end < text.length && !assembly_unit_space(text.pointer[end]))
    {
        end += 1;
    }
    if (rest)
    {
        *rest = assembly_unit_trim(string_slice(text, end, text.length));
    }
    return string_slice(text, 0, end);
}

BUSTER_GLOBAL_LOCAL void assembly_unit_diagnostic(AssemblyUnitBuilder* builder, AssemblyDiagnosticKind kind, String8 message)
{
    if (builder->result.diagnostic_count >= ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY)
    {
        return;
    }
    builder->result.diagnostics[builder->result.diagnostic_count++] = (AssemblyDiagnostic){
        .message = message,
        .line = builder->line,
        .column = builder->column,
        .length = 1,
        .kind = kind,
    };
}

BUSTER_GLOBAL_LOCAL void assembly_unit_diagnostic_format(AssemblyUnitBuilder* builder, AssemblyDiagnosticKind kind, String8 format, String8 argument)
{
    assembly_unit_diagnostic(builder, kind, string_format(builder->arena, format, argument));
}

// ---------------------------------------------------------------- symbols

BUSTER_GLOBAL_LOCAL u32 assembly_unit_symbol_intern(AssemblyUnitBuilder* builder, String8 name)
{
    for (u32 index = 0; index < builder->result.symbol_count; index += 1)
    {
        if (string_equal(builder->result.symbols[index].name, name))
        {
            return index;
        }
    }
    if (builder->result.symbol_count >= builder->symbol_capacity)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("too many symbols in one assembly unit"));
        return UINT32_MAX;
    }
    u32 index = builder->result.symbol_count++;
    builder->result.symbols[index] = (AssemblyUnitSymbol){
        .name = name,
        .section = ASSEMBLY_UNIT_SECTION_UNDEFINED,
    };
    return index;
}

// --------------------------------------------------------------- sections

// The section a name selects when the directive carries no flags. musl's
// crti.s writes `.section .init` with nothing else, and GNU as places `.init`
// and `.fini` in executable sections by name.
BUSTER_GLOBAL_LOCAL bool assembly_unit_section_kind_for_name(String8 name, AssemblyUnitSectionKind* kind)
{
    static const struct
    {
        String8 prefix;
        AssemblyUnitSectionKind kind;
    } rows[] = {
        {S8_INITIALIZER(".text"), ASSEMBLY_UNIT_SECTION_TEXT},
        {S8_INITIALIZER(".init"), ASSEMBLY_UNIT_SECTION_TEXT},
        {S8_INITIALIZER(".fini"), ASSEMBLY_UNIT_SECTION_TEXT},
        {S8_INITIALIZER(".rodata"), ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA},
        {S8_INITIALIZER(".data"), ASSEMBLY_UNIT_SECTION_DATA},
        {S8_INITIALIZER(".bss"), ASSEMBLY_UNIT_SECTION_ZERO},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rows); index += 1)
    {
        if (string_starts_with_sequence(name, rows[index].prefix))
        {
            *kind = rows[index].kind;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 assembly_unit_section_select(AssemblyUnitBuilder* builder, String8 name, AssemblyUnitSectionKind kind)
{
    for (u32 index = 0; index < builder->result.section_count; index += 1)
    {
        if (string_equal(builder->result.sections[index].name, name))
        {
            return index;
        }
    }
    if (builder->result.section_count >= ASSEMBLY_UNIT_SECTION_CAPACITY)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("too many sections in one assembly unit"));
        return UINT32_MAX;
    }
    u32 index = builder->result.section_count++;
    builder->result.sections[index] = (AssemblyUnitSection){
        .name = name,
        // GNU as gives a hand-written section no alignment of its own: the
        // `.init` fragments of crti.o and crtn.o are concatenated byte after
        // byte, and any padding between them would run as code.
        .alignment = 1,
        .kind = kind,
    };
    builder->section_offsets[index] = 0;
    return index;
}

// The section every unit starts in, created on first use so a file that only
// carries directives produces no sections at all.
BUSTER_GLOBAL_LOCAL bool assembly_unit_section_current(AssemblyUnitBuilder* builder)
{
    if (builder->current_section == UINT32_MAX)
    {
        builder->current_section = assembly_unit_section_select(builder, S8(".text"), ASSEMBLY_UNIT_SECTION_TEXT);
    }
    return builder->current_section != UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_append(AssemblyUnitBuilder* builder, u8* bytes, u64 length)
{
    if (!assembly_unit_section_current(builder))
    {
        return false;
    }
    if (builder->piece_count >= builder->piece_capacity)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("too many emitted pieces in one assembly unit"));
        return false;
    }
    builder->pieces[builder->piece_count++] = (AssemblyUnitPiece){
        .bytes = bytes,
        .length = length,
        .section = builder->current_section,
    };
    builder->section_offsets[builder->current_section] += length;
    return true;
}

// ------------------------------------------------------------ expressions

typedef struct AssemblyUnitValue AssemblyUnitValue;
struct AssemblyUnitValue
{
    s64 constant;
    u32 symbol;
    bool has_symbol;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL bool assembly_unit_parse_number(String8 text, u64* value)
{
    if (!text.length)
    {
        return false;
    }
    u64 base = 10;
    u64 index = 0;
    if (text.length > 2 && text.pointer[0] == '0' && (text.pointer[1] == 'x' || text.pointer[1] == 'X'))
    {
        base = 16;
        index = 2;
    }
    else if (text.length > 2 && text.pointer[0] == '0' && (text.pointer[1] == 'b' || text.pointer[1] == 'B'))
    {
        base = 2;
        index = 2;
    }
    else if (text.length > 1 && text.pointer[0] == '0')
    {
        base = 8;
        index = 1;
    }
    u64 result = 0;
    bool any = false;
    for (; index < text.length; index += 1)
    {
        char8 code_unit = text.pointer[index];
        u64 digit = 0;
        if (code_unit >= '0' && code_unit <= '9')
        {
            digit = (u64)(code_unit - '0');
        }
        else if (code_unit >= 'a' && code_unit <= 'f')
        {
            digit = (u64)(code_unit - 'a') + 10;
        }
        else if (code_unit >= 'A' && code_unit <= 'F')
        {
            digit = (u64)(code_unit - 'A') + 10;
        }
        else
        {
            return false;
        }
        if (digit >= base)
        {
            return false;
        }
        result = result * base + digit;
        any = true;
    }
    *value = result;
    return any;
}

// One operand expression. The vocabulary is deliberately narrow: an absolute
// constant built from `+`, `-` and `*` over numbers, the location counter `.`,
// and a single symbol reference with a constant addend. Anything else is
// refused by the caller with the directive named.
BUSTER_GLOBAL_LOCAL bool assembly_unit_evaluate(AssemblyUnitBuilder* builder, String8 text, AssemblyUnitValue* value)
{
    AssemblyUnitValue result = {.symbol = UINT32_MAX};
    u64 index = 0;
    s64 sign = 1;
    bool expect_term = true;
    while (index < text.length)
    {
        while (index < text.length && assembly_unit_space(text.pointer[index]))
        {
            index += 1;
        }
        if (index >= text.length)
        {
            break;
        }
        char8 code_unit = text.pointer[index];
        if (!expect_term)
        {
            if (code_unit == '+')
            {
                sign = 1;
            }
            else if (code_unit == '-')
            {
                sign = -1;
            }
            else
            {
                return false;
            }
            index += 1;
            expect_term = true;
            continue;
        }
        u64 term_start = index;
        s64 term = 0;
        if (code_unit == '.' && (index + 1 >= text.length || !assembly_unit_name_character(text.pointer[index + 1])))
        {
            if (!assembly_unit_section_current(builder))
            {
                return false;
            }
            term = (s64)builder->section_offsets[builder->current_section];
            index += 1;
        }
        else if (assembly_unit_digit(code_unit))
        {
            while (index < text.length && (assembly_unit_digit(text.pointer[index]) ||
                                           (text.pointer[index] >= 'a' && text.pointer[index] <= 'f') ||
                                           (text.pointer[index] >= 'A' && text.pointer[index] <= 'F') || text.pointer[index] == 'x' ||
                                           text.pointer[index] == 'X'))
            {
                index += 1;
            }
            u64 number = 0;
            if (!assembly_unit_parse_number(string_slice(text, term_start, index), &number))
            {
                return false;
            }
            term = (s64)number;
        }
        else if (assembly_unit_name_start(code_unit))
        {
            while (index < text.length && assembly_unit_name_character(text.pointer[index]))
            {
                index += 1;
            }
            String8 name = string_slice(text, term_start, index);
            if (result.has_symbol || sign < 0)
            {
                return false;
            }
            u32 symbol = assembly_unit_symbol_intern(builder, name);
            if (symbol == UINT32_MAX)
            {
                return false;
            }
            result.has_symbol = true;
            result.symbol = symbol;
            expect_term = false;
            continue;
        }
        else
        {
            return false;
        }
        result.constant += sign * term;
        sign = 1;
        expect_term = false;
    }
    if (expect_term)
    {
        return false;
    }
    *value = result;
    return true;
}

// `.size name,.-name` is the one location-counter form a hand-written libc
// writes. It folds here rather than reaching the object, which has no
// expressions.
BUSTER_GLOBAL_LOCAL bool assembly_unit_evaluate_absolute(AssemblyUnitBuilder* builder, String8 text, s64* value)
{
    String8 trimmed = assembly_unit_trim(text);
    u64 minus = string_first_code_unit(trimmed, '-');
    if (trimmed.length > 1 && trimmed.pointer[0] == '.' && minus == 1)
    {
        String8 name = assembly_unit_trim(string_slice(trimmed, 2, trimmed.length));
        u32 symbol = assembly_unit_symbol_intern(builder, name);
        if (symbol == UINT32_MAX || !builder->result.symbols[symbol].defined ||
            builder->result.symbols[symbol].section != builder->current_section || !assembly_unit_section_current(builder))
        {
            return false;
        }
        *value = (s64)builder->section_offsets[builder->current_section] - (s64)builder->result.symbols[symbol].value;
        return true;
    }
    AssemblyUnitValue evaluated = {0};
    if (!assembly_unit_evaluate(builder, trimmed, &evaluated) || evaluated.has_symbol)
    {
        return false;
    }
    *value = evaluated.constant;
    return true;
}

// Splits a directive operand list on top-level commas, keeping quoted text
// together so `.ascii "a,b"` stays one operand.
BUSTER_GLOBAL_LOCAL u32 assembly_unit_split_operands(String8 text, String8* operands, u32 capacity)
{
    u32 count = 0;
    u64 start = 0;
    bool quoted = false;
    for (u64 index = 0; index <= text.length; index += 1)
    {
        if (index < text.length && text.pointer[index] == '"' && (!index || text.pointer[index - 1] != '\\'))
        {
            quoted = !quoted;
            continue;
        }
        if (index != text.length && (quoted || text.pointer[index] != ','))
        {
            continue;
        }
        if (count >= capacity)
        {
            return UINT32_MAX;
        }
        operands[count++] = assembly_unit_trim(string_slice(text, start, index));
        start = index + 1;
    }
    return count;
}

// ------------------------------------------------------------- directives

BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_section(AssemblyUnitBuilder* builder, String8 operands)
{
    String8 parts[4] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    if (part_count == UINT32_MAX || !part_count || !parts[0].length)
    {
        return false;
    }
    String8 name = assembly_unit_word(parts[0], 0);
    AssemblyUnitSectionKind kind = ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA;
    bool classified = false;
    if (part_count > 1 && parts[1].length >= 2 && parts[1].pointer[0] == '"')
    {
        String8 flags = string_slice(parts[1], 1, parts[1].length - 1);
        bool writable = false;
        bool executable = false;
        for (u64 index = 0; index < flags.length; index += 1)
        {
            writable = writable || flags.pointer[index] == 'w';
            executable = executable || flags.pointer[index] == 'x';
        }
        bool no_bits = part_count > 2 && string_ends_with_sequence(parts[2], S8("nobits"));
        kind = executable    ? ASSEMBLY_UNIT_SECTION_TEXT
               : no_bits     ? ASSEMBLY_UNIT_SECTION_ZERO
               : writable    ? ASSEMBLY_UNIT_SECTION_DATA
                             : ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA;
        classified = true;
    }
    if (!classified && !assembly_unit_section_kind_for_name(name, &kind))
    {
        return false;
    }
    u32 section = assembly_unit_section_select(builder, name, kind);
    if (section == UINT32_MAX)
    {
        return false;
    }
    builder->current_section = section;
    return true;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_symbol(AssemblyUnitBuilder* builder, String8 directive, String8 operands)
{
    String8 parts[3] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    if (part_count == UINT32_MAX || !part_count || !parts[0].length)
    {
        return false;
    }
    u32 symbol = assembly_unit_symbol_intern(builder, assembly_unit_word(parts[0], 0));
    if (symbol == UINT32_MAX)
    {
        return false;
    }
    AssemblyUnitSymbol* record = builder->result.symbols + symbol;
    if (string_equal(directive, S8(".globl")) || string_equal(directive, S8(".global")))
    {
        record->global = true;
        return part_count == 1;
    }
    if (string_equal(directive, S8(".weak")))
    {
        record->global = true;
        record->weak = true;
        return part_count == 1;
    }
    if (string_equal(directive, S8(".hidden")))
    {
        record->hidden = true;
        return part_count == 1;
    }
    if (string_equal(directive, S8(".type")))
    {
        if (part_count != 2)
        {
            return false;
        }
        // `@function` in GNU as, `%function` where `@` starts a comment, and
        // the spelled-out `STT_FUNC` are the three forms a libc writes.
        bool function = string_ends_with_sequence(parts[1], S8("function")) || string_ends_with_sequence(parts[1], S8("STT_FUNC"));
        bool object = string_ends_with_sequence(parts[1], S8("object")) || string_ends_with_sequence(parts[1], S8("STT_OBJECT"));
        record->function = function;
        return function || object;
    }
    if (string_equal(directive, S8(".size")))
    {
        s64 size = 0;
        if (part_count != 2 || !assembly_unit_evaluate_absolute(builder, parts[1], &size) || size < 0)
        {
            return false;
        }
        record->size = (u64)size;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_align(AssemblyUnitBuilder* builder, String8 directive, String8 operands)
{
    String8 parts[3] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    if (part_count == UINT32_MAX || !part_count || part_count > 2)
    {
        return false;
    }
    bool power = string_equal(directive, S8(".p2align"));
    s64 requested = 0;
    if (!assembly_unit_evaluate_absolute(builder, parts[0], &requested) || requested < 0 || requested > (power ? 20 : 1 << 20))
    {
        return false;
    }
    u64 alignment = power ? (u64)1 << (u64)requested : (u64)requested;
    if (!alignment || (alignment & (alignment - 1)))
    {
        return false;
    }
    s64 fill = 0;
    if (part_count == 2 && (!assembly_unit_evaluate_absolute(builder, parts[1], &fill) || fill < 0 || fill > 255))
    {
        return false;
    }
    if (!assembly_unit_section_current(builder))
    {
        return false;
    }
    AssemblyUnitSection* section = builder->result.sections + builder->current_section;
    if (alignment > section->alignment)
    {
        section->alignment = (u32)alignment;
    }
    u64 offset = builder->section_offsets[builder->current_section];
    u64 padding = (alignment - (offset & (alignment - 1))) & (alignment - 1);
    if (!padding)
    {
        return true;
    }
    if (section->kind == ASSEMBLY_UNIT_SECTION_ZERO)
    {
        return assembly_unit_append(builder, 0, padding);
    }
    u8* bytes = arena_allocate(builder->arena, u8, padding);
    // A text section pads with one-byte NOPs, which is what runs when the
    // padding is reached by falling through rather than jumped over.
    u8 value = part_count == 2 ? (u8)fill : (section->kind == ASSEMBLY_UNIT_SECTION_TEXT ? 0x90 : 0);
    for (u64 index = 0; index < padding; index += 1)
    {
        bytes[index] = value;
    }
    return assembly_unit_append(builder, bytes, padding);
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_zero(AssemblyUnitBuilder* builder, String8 operands)
{
    String8 parts[2] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    s64 count = 0;
    s64 fill = 0;
    if (part_count == UINT32_MAX || !part_count || part_count > 2 || !assembly_unit_evaluate_absolute(builder, parts[0], &count) || count < 0 ||
        (part_count == 2 && (!assembly_unit_evaluate_absolute(builder, parts[1], &fill) || fill < 0 || fill > 255)))
    {
        return false;
    }
    if (!count)
    {
        return true;
    }
    if (!assembly_unit_section_current(builder))
    {
        return false;
    }
    if (builder->result.sections[builder->current_section].kind == ASSEMBLY_UNIT_SECTION_ZERO)
    {
        return assembly_unit_append(builder, 0, (u64)count);
    }
    u8* bytes = arena_allocate(builder->arena, u8, (u64)count);
    for (s64 index = 0; index < count; index += 1)
    {
        bytes[index] = (u8)fill;
    }
    return assembly_unit_append(builder, bytes, (u64)count);
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_relocation_append(AssemblyUnitBuilder* builder, u32 symbol, u64 offset, s64 addend, AssemblyRelocationKind kind)
{
    if (builder->result.relocation_count >= builder->relocation_capacity)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("too many relocations in one assembly unit"));
        return false;
    }
    builder->relocation_lines[builder->result.relocation_count] = builder->line;
    builder->result.relocations[builder->result.relocation_count++] = (AssemblyUnitRelocation){
        .addend = addend,
        .offset = offset,
        .section = builder->current_section,
        .symbol = symbol,
        .kind = kind,
    };
    return true;
}

// `.byte`/`.short`/`.long`/`.quad`. A width that can hold an address also
// accepts one symbol reference, which becomes an absolute relocation.
BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_integer(AssemblyUnitBuilder* builder, u32 width, String8 operands)
{
    String8 parts[ASSEMBLY_UNIT_OPERAND_CAPACITY] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    if (part_count == UINT32_MAX || !part_count)
    {
        return false;
    }
    if (!assembly_unit_section_current(builder))
    {
        return false;
    }
    if (builder->result.sections[builder->current_section].kind == ASSEMBLY_UNIT_SECTION_ZERO)
    {
        return false;
    }
    u64 length = (u64)part_count * width;
    u8* bytes = arena_allocate(builder->arena, u8, length);
    u64 base = builder->section_offsets[builder->current_section];
    for (u32 index = 0; index < part_count; index += 1)
    {
        AssemblyUnitValue value = {.symbol = UINT32_MAX};
        if (!assembly_unit_evaluate(builder, parts[index], &value))
        {
            return false;
        }
        u64 written = (u64)value.constant;
        if (value.has_symbol)
        {
            AssemblyRelocationKind kind = width == 8 ? ASSEMBLY_RELOCATION_X86_ABSOLUTE64
                                          : width == 4 ? ASSEMBLY_RELOCATION_X86_ABSOLUTE32
                                                       : ASSEMBLY_RELOCATION_COUNT;
            if (kind == ASSEMBLY_RELOCATION_COUNT ||
                !assembly_unit_relocation_append(builder, value.symbol, base + (u64)index * width, value.constant, kind))
            {
                return false;
            }
            written = 0;
        }
        for (u32 byte_index = 0; byte_index < width; byte_index += 1)
        {
            bytes[(u64)index * width + byte_index] = (u8)(written >> (byte_index * 8));
        }
    }
    return assembly_unit_append(builder, bytes, length);
}

// `.ascii` and the zero-terminated `.asciz`/`.string`.
BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_ascii(AssemblyUnitBuilder* builder, bool terminated, String8 operands)
{
    String8 parts[ASSEMBLY_UNIT_OPERAND_CAPACITY] = {0};
    u32 part_count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    if (part_count == UINT32_MAX || !part_count)
    {
        return false;
    }
    if (!assembly_unit_section_current(builder) || builder->result.sections[builder->current_section].kind == ASSEMBLY_UNIT_SECTION_ZERO)
    {
        return false;
    }
    u64 capacity = operands.length + part_count + 1;
    u8* bytes = arena_allocate(builder->arena, u8, capacity);
    u64 length = 0;
    for (u32 index = 0; index < part_count; index += 1)
    {
        String8 text = parts[index];
        if (text.length < 2 || text.pointer[0] != '"' || text.pointer[text.length - 1] != '"')
        {
            return false;
        }
        text = string_slice(text, 1, text.length - 1);
        for (u64 position = 0; position < text.length; position += 1)
        {
            char8 code_unit = text.pointer[position];
            if (code_unit != '\\')
            {
                bytes[length++] = (u8)code_unit;
                continue;
            }
            position += 1;
            if (position >= text.length)
            {
                return false;
            }
            char8 escape = text.pointer[position];
            if (escape >= '0' && escape <= '7')
            {
                u64 value = 0;
                u32 digits = 0;
                while (digits < 3 && position < text.length && text.pointer[position] >= '0' && text.pointer[position] <= '7')
                {
                    value = value * 8 + (u64)(text.pointer[position] - '0');
                    position += 1;
                    digits += 1;
                }
                position -= 1;
                bytes[length++] = (u8)value;
                continue;
            }
            u8 value = escape == 'n'    ? '\n'
                       : escape == 't'  ? '\t'
                       : escape == 'r'  ? '\r'
                       : escape == 'b'  ? '\b'
                       : escape == 'f'  ? '\f'
                       : escape == 'v'  ? '\v'
                       : escape == '\\' ? '\\'
                       : escape == '"'  ? '"'
                                        : 0;
            if (!value)
            {
                return false;
            }
            bytes[length++] = value;
        }
        if (terminated)
        {
            bytes[length++] = 0;
        }
    }
    return assembly_unit_append(builder, bytes, length);
}

// One directive line. `recognized` stays false for a spelling no arm claims,
// which the caller reports by name; a recognized directive that returns false
// is a recognized directive with an operand form this vocabulary does not
// cover, and is reported the same way.
BUSTER_GLOBAL_LOCAL bool assembly_unit_directive(AssemblyUnitBuilder* builder, String8 line, bool* recognized)
{
    String8 operands = {0};
    String8 directive = assembly_unit_word(line, &operands);
    *recognized = true;
    // Call frame information describes unwinding, not bytes: accepted and
    // dropped, the way a build without unwind tables would have it.
    if (string_starts_with_sequence(directive, S8(".cfi_")))
    {
        return true;
    }
    if (string_equal(directive, S8(".text")) || string_equal(directive, S8(".data")) || string_equal(directive, S8(".bss")) ||
        string_equal(directive, S8(".rodata")))
    {
        AssemblyUnitSectionKind kind = ASSEMBLY_UNIT_SECTION_TEXT;
        if (operands.length || !assembly_unit_section_kind_for_name(directive, &kind))
        {
            return false;
        }
        u32 section = assembly_unit_section_select(builder, directive, kind);
        builder->current_section = section;
        return section != UINT32_MAX;
    }
    if (string_equal(directive, S8(".section")))
    {
        return assembly_unit_directive_section(builder, operands);
    }
    if (string_equal(directive, S8(".globl")) || string_equal(directive, S8(".global")) || string_equal(directive, S8(".weak")) ||
        string_equal(directive, S8(".hidden")) || string_equal(directive, S8(".type")) || string_equal(directive, S8(".size")))
    {
        return assembly_unit_directive_symbol(builder, directive, operands);
    }
    if (string_equal(directive, S8(".align")) || string_equal(directive, S8(".balign")) || string_equal(directive, S8(".p2align")))
    {
        return assembly_unit_directive_align(builder, directive, operands);
    }
    if (string_equal(directive, S8(".zero")) || string_equal(directive, S8(".skip")) || string_equal(directive, S8(".space")))
    {
        return assembly_unit_directive_zero(builder, operands);
    }
    if (string_equal(directive, S8(".byte")))
    {
        return assembly_unit_directive_integer(builder, 1, operands);
    }
    if (string_equal(directive, S8(".short")) || string_equal(directive, S8(".word")) || string_equal(directive, S8(".hword")) ||
        string_equal(directive, S8(".value")))
    {
        return assembly_unit_directive_integer(builder, 2, operands);
    }
    if (string_equal(directive, S8(".long")) || string_equal(directive, S8(".int")))
    {
        return assembly_unit_directive_integer(builder, 4, operands);
    }
    if (string_equal(directive, S8(".quad")))
    {
        return assembly_unit_directive_integer(builder, 8, operands);
    }
    if (string_equal(directive, S8(".ascii")))
    {
        return assembly_unit_directive_ascii(builder, false, operands);
    }
    if (string_equal(directive, S8(".asciz")) || string_equal(directive, S8(".string")))
    {
        return assembly_unit_directive_ascii(builder, true, operands);
    }
    if (string_equal(directive, S8(".intel_syntax")) || string_equal(directive, S8(".att_syntax")))
    {
        bool intel = string_equal(directive, S8(".intel_syntax"));
        if (builder->target.cpu_arch != CPU_ARCH_X86_64 || !string_equal(operands, intel ? S8("noprefix") : S8("prefix")))
        {
            return false;
        }
        builder->syntax = intel ? ASSEMBLY_SYNTAX_INTEL : ASSEMBLY_SYNTAX_ATT;
        return true;
    }
    *recognized = false;
    return false;
}

// ------------------------------------------------------------ instructions

// The local-label reference `1f`/`1b` and the `@PLT` suffix are the two
// spellings the instruction layer below does not read. Both are rewritten
// here into a plain symbol name, into a fresh copy of the line so the original
// text stays available for diagnostics.
BUSTER_GLOBAL_LOCAL bool assembly_unit_rewrite_line(AssemblyUnitBuilder* builder, String8 line, String8* rewritten)
{
    bool changed = false;
    for (u64 index = 0; index < line.length && !changed; index += 1)
    {
        bool boundary = !index || (!assembly_unit_name_character(line.pointer[index - 1]) && line.pointer[index - 1] != '@');
        changed = (line.pointer[index] == '@') || (boundary && assembly_unit_digit(line.pointer[index]));
    }
    if (!changed)
    {
        *rewritten = line;
        return true;
    }
    u64 capacity = line.length * 2 + 64;
    char8* text = arena_allocate(builder->arena, char8, capacity);
    u64 length = 0;
    u64 index = 0;
    while (index < line.length)
    {
        char8 code_unit = line.pointer[index];
        bool boundary = !index || (!assembly_unit_name_character(line.pointer[index - 1]) && line.pointer[index - 1] != '@');
        if (boundary && assembly_unit_digit(code_unit))
        {
            u64 digits_end = index;
            while (digits_end < line.length && assembly_unit_digit(line.pointer[digits_end]))
            {
                digits_end += 1;
            }
            char8 direction = digits_end < line.length ? line.pointer[digits_end] : 0;
            bool reference = (direction == 'f' || direction == 'b') &&
                             (digits_end + 1 >= line.length || !assembly_unit_name_character(line.pointer[digits_end + 1]));
            u64 value = 0;
            if (reference && assembly_unit_parse_number(string_slice(line, index, digits_end), &value))
            {
                String8 name = {0};
                for (u32 label = 0; label < builder->numeric_label_count; label += 1)
                {
                    AssemblyUnitNumericLabel candidate = builder->numeric_labels[label];
                    if (candidate.value != value)
                    {
                        continue;
                    }
                    if (direction == 'b' && candidate.line <= builder->line)
                    {
                        name = candidate.name;
                    }
                    if (direction == 'f' && candidate.line > builder->line && !name.length)
                    {
                        name = candidate.name;
                    }
                }
                if (!name.length)
                {
                    assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_EXPRESSION,
                                             S8("no local label matches this backward or forward reference"));
                    return false;
                }
                if (length + name.length >= capacity)
                {
                    return false;
                }
                memcpy(text + length, name.pointer, name.length);
                length += name.length;
                index = digits_end + 1;
                continue;
            }
            while (index < digits_end)
            {
                text[length++] = line.pointer[index++];
            }
            continue;
        }
        if (code_unit == '@')
        {
            u64 suffix_end = index + 1;
            while (suffix_end < line.length && assembly_unit_name_character(line.pointer[suffix_end]))
            {
                suffix_end += 1;
            }
            String8 suffix = string_slice(line, index + 1, suffix_end);
            // A static link resolves a `@PLT` call the same way it resolves a
            // plain one, so the suffix is dropped rather than turned into a
            // relocation family this object model does not carry.
            if (!string_equal(suffix, S8("PLT")) && !string_equal(suffix, S8("plt")))
            {
                assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("unsupported symbol modifier '@{S8}'"), suffix);
                return false;
            }
            index = suffix_end;
            continue;
        }
        text[length++] = code_unit;
        index += 1;
    }
    *rewritten = (String8){.pointer = text, .length = length};
    return true;
}

// A line that is nothing but a repeat or lock prefix. GNU as accepts one and
// applies it to the next instruction, which is how musl's memcpy writes
// `rep` above `movsq`; the two are joined here so the instruction layer below
// still sees one complete statement.
BUSTER_GLOBAL_LOCAL bool assembly_unit_prefix_only(String8 line)
{
    static String8 const prefixes[] = {
        S8_INITIALIZER("rep"),  S8_INITIALIZER("repe"),  S8_INITIALIZER("repz"),
        S8_INITIALIZER("repne"), S8_INITIALIZER("repnz"), S8_INITIALIZER("lock"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(prefixes); index += 1)
    {
        if (string_equal(line, prefixes[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_instruction(AssemblyUnitBuilder* builder, String8 line)
{
    String8 rewritten = {0};
    if (!assembly_unit_rewrite_line(builder, line, &rewritten))
    {
        return false;
    }
    if (!assembly_unit_section_current(builder))
    {
        return false;
    }
    if (builder->result.sections[builder->current_section].kind == ASSEMBLY_UNIT_SECTION_ZERO)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, S8("an instruction cannot be emitted into a zero-fill section"));
        return false;
    }
    AssemblyEncodeResult encoded = assembly_encode(builder->arena, rewritten,
                                                   (AssemblyEncodeOptions){
                                                       .target = builder->target,
                                                       .syntax = builder->syntax,
                                                   });
    if (encoded.diagnostic_count)
    {
        assembly_unit_diagnostic(builder, encoded.diagnostics[0].kind, encoded.diagnostics[0].message);
        return false;
    }
    u64 base = builder->section_offsets[builder->current_section];
    for (u32 index = 0; index < encoded.relocation_count; index += 1)
    {
        AssemblyRelocation relocation = encoded.relocations[index];
        if (relocation.symbol >= encoded.symbol_count)
        {
            return false;
        }
        u32 symbol = assembly_unit_symbol_intern(builder, encoded.symbols[relocation.symbol].name);
        if (symbol == UINT32_MAX || !assembly_unit_relocation_append(builder, symbol, base + relocation.offset, relocation.addend, relocation.kind))
        {
            return false;
        }
    }
    return assembly_unit_append(builder, encoded.bytes.pointer, encoded.bytes.length);
}

// ------------------------------------------------------------ the passes

// A leading `name:` or `1:`, repeated: `sigsetjmp: __sigsetjmp:` defines two
// names at the same offset on one line.
BUSTER_GLOBAL_LOCAL u64 assembly_unit_leading_label(String8 line)
{
    if (!line.length)
    {
        return 0;
    }
    u64 end = 0;
    if (assembly_unit_digit(line.pointer[0]))
    {
        while (end < line.length && assembly_unit_digit(line.pointer[end]))
        {
            end += 1;
        }
    }
    else if (assembly_unit_name_start(line.pointer[0]))
    {
        while (end < line.length && assembly_unit_name_character(line.pointer[end]))
        {
            end += 1;
        }
    }
    return end && end < line.length && line.pointer[end] == ':' ? end : 0;
}

// Every source line, with block comments already blanked. Line comments are
// stripped here so the two label passes and the instruction layer all see the
// same text.
BUSTER_GLOBAL_LOCAL String8 assembly_unit_statement(String8 source, u64* cursor, u32* column)
{
    u64 start = *cursor;
    char8 const* line_start = source.pointer + start;
    u64 end = start;
    while (end < source.length && source.pointer[end] != '\n')
    {
        end += 1;
    }
    *cursor = end < source.length ? end + 1 : source.length;
    String8 line = string_slice(source, start, end);
    for (u64 index = 0; index < line.length; index += 1)
    {
        if (line.pointer[index] == '#' || line.pointer[index] == ';' ||
            (line.pointer[index] == '/' && index + 1 < line.length && line.pointer[index + 1] == '/'))
        {
            line.length = index;
            break;
        }
    }
    String8 trimmed = assembly_unit_trim(line);
    *column = trimmed.length ? (u32)((u64)(trimmed.pointer - line_start) + 1) : 1;
    return trimmed;
}

BUSTER_GLOBAL_LOCAL void assembly_unit_collect_numeric_labels(AssemblyUnitBuilder* builder, String8 source)
{
    u64 cursor = 0;
    u32 line_number = 0;
    while (cursor < source.length)
    {
        u32 column = 0;
        String8 line = assembly_unit_statement(source, &cursor, &column);
        line_number += 1;
        u64 label = assembly_unit_leading_label(line);
        while (label)
        {
            String8 name = string_slice(line, 0, label);
            u64 value = 0;
            if (assembly_unit_digit(name.pointer[0]) && assembly_unit_parse_number(name, &value) &&
                builder->numeric_label_count < builder->numeric_label_capacity)
            {
                builder->numeric_labels[builder->numeric_label_count] = (AssemblyUnitNumericLabel){
                    .name = string_format(builder->arena, S8(".Lnum.{u64}.{u32}"), value, builder->numeric_label_count),
                    .value = value,
                    .line = line_number,
                };
                builder->numeric_label_count += 1;
            }
            line = assembly_unit_trim(string_slice(line, label + 1, line.length));
            label = assembly_unit_leading_label(line);
        }
    }
}

BUSTER_GLOBAL_LOCAL void assembly_unit_parse(AssemblyUnitBuilder* builder, String8 source)
{
    u64 cursor = 0;
    u32 numeric_index = 0;
    String8 pending_prefix = {0};
    while (cursor < source.length && builder->result.diagnostic_count < ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY)
    {
        String8 line = assembly_unit_statement(source, &cursor, &builder->column);
        builder->line += 1;
        u64 label = assembly_unit_leading_label(line);
        if (pending_prefix.length && (label || (line.length && line.pointer[0] == '.')))
        {
            assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT,
                                     S8("a repeat or lock prefix on its own line must be followed by an instruction"));
            return;
        }
        while (label)
        {
            String8 spelling = string_slice(line, 0, label);
            String8 name = spelling;
            if (assembly_unit_digit(spelling.pointer[0]))
            {
                if (numeric_index >= builder->numeric_label_count)
                {
                    assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, S8("local label definition was not collected"));
                    return;
                }
                name = builder->numeric_labels[numeric_index++].name;
            }
            u32 symbol = assembly_unit_symbol_intern(builder, name);
            if (symbol == UINT32_MAX || !assembly_unit_section_current(builder))
            {
                return;
            }
            if (builder->result.symbols[symbol].defined)
            {
                assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_DUPLICATE_SYMBOL, S8("'{S8}' is defined more than once"), spelling);
                return;
            }
            builder->result.symbols[symbol].defined = true;
            builder->result.symbols[symbol].section = builder->current_section;
            builder->result.symbols[symbol].value = builder->section_offsets[builder->current_section];
            line = assembly_unit_trim(string_slice(line, label + 1, line.length));
            label = assembly_unit_leading_label(line);
        }
        if (!line.length)
        {
            continue;
        }
        if (assembly_unit_prefix_only(line))
        {
            pending_prefix = pending_prefix.length ? string_format(builder->arena, S8("{S8} {S8}"), pending_prefix, line) : line;
            continue;
        }
        if (line.pointer[0] == '.')
        {
            bool recognized = false;
            if (!assembly_unit_directive(builder, line, &recognized))
            {
                if (!builder->result.diagnostic_count)
                {
                    assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE,
                                                    recognized ? S8("unsupported operand form for the '{S8}' directive")
                                                               : S8("unsupported assembler directive '{S8}'"),
                                                    assembly_unit_word(line, 0));
                }
                return;
            }
            continue;
        }
        if (pending_prefix.length)
        {
            line = string_format(builder->arena, S8("{S8} {S8}"), pending_prefix, line);
            pending_prefix = (String8){0};
        }
        if (!assembly_unit_instruction(builder, line))
        {
            if (!builder->result.diagnostic_count)
            {
                assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT, S8("instruction could not be assembled"));
            }
            return;
        }
    }
    if (pending_prefix.length)
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT,
                                 S8("a repeat or lock prefix on its own line must be followed by an instruction"));
    }
}

// Pieces become section bytes, and every relocation that names a symbol
// defined in its own section with a PC-relative kind is resolved here rather
// than handed to the linker -- exactly the branches and `lea`s a local label
// produces.
BUSTER_GLOBAL_LOCAL void assembly_unit_materialize(AssemblyUnitBuilder* builder)
{
    for (u32 section = 0; section < builder->result.section_count; section += 1)
    {
        AssemblyUnitSection* record = builder->result.sections + section;
        u64 size = builder->section_offsets[section];
        if (record->kind == ASSEMBLY_UNIT_SECTION_ZERO)
        {
            record->zero_size = size;
            continue;
        }
        record->data = (ByteSlice){
            .pointer = size ? arena_allocate(builder->arena, u8, size) : 0,
            .length = size,
        };
    }
    u64* filled = arena_allocate(builder->arena, u64, builder->result.section_count ? builder->result.section_count : 1);
    for (u32 section = 0; section < builder->result.section_count; section += 1)
    {
        filled[section] = 0;
    }
    for (u32 index = 0; index < builder->piece_count; index += 1)
    {
        AssemblyUnitPiece piece = builder->pieces[index];
        AssemblyUnitSection* record = builder->result.sections + piece.section;
        if (record->kind == ASSEMBLY_UNIT_SECTION_ZERO || !piece.length)
        {
            filled[piece.section] += piece.length;
            continue;
        }
        memcpy(record->data.pointer + filled[piece.section], piece.bytes, piece.length);
        filled[piece.section] += piece.length;
    }
    u32 kept = 0;
    for (u32 index = 0; index < builder->result.relocation_count; index += 1)
    {
        AssemblyUnitRelocation relocation = builder->result.relocations[index];
        AssemblyUnitSymbol symbol = builder->result.symbols[relocation.symbol];
        u32 width = relocation.kind == ASSEMBLY_RELOCATION_X86_PC8    ? 1
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC16 ? 2
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC32 ? 4
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC64 ? 8
                                                                      : 0;
        if (!width || !symbol.defined || symbol.section != relocation.section)
        {
            builder->relocation_lines[kept] = builder->relocation_lines[index];
            builder->result.relocations[kept++] = relocation;
            continue;
        }
        s64 value = (s64)symbol.value + relocation.addend - (s64)relocation.offset;
        if ((width == 1 && (value < INT8_MIN || value > INT8_MAX)) || (width == 2 && (value < INT16_MIN || value > INT16_MAX)) ||
            (width == 4 && (value < INT32_MIN || value > INT32_MAX)))
        {
            builder->line = builder->relocation_lines[index];
            builder->column = 1;
            assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE, S8("branch to '{S8}' does not fit its displacement"),
                                            symbol.name);
            return;
        }
        ByteSlice data = builder->result.sections[relocation.section].data;
        for (u32 byte_index = 0; byte_index < width; byte_index += 1)
        {
            data.pointer[relocation.offset + byte_index] = (u8)((u64)value >> (byte_index * 8));
        }
    }
    builder->result.relocation_count = kept;
    // An undefined name is what the linker is asked to resolve, so it must be
    // global whether or not a `.globl` mentioned it; GNU as does the same.
    for (u32 index = 0; index < builder->result.symbol_count; index += 1)
    {
        builder->result.symbols[index].global = builder->result.symbols[index].global || !builder->result.symbols[index].defined;
        // A label in an executable section is a function entry unless a
        // `.type` said otherwise: it is what a disassembler needs to know
        // where code starts, and the object model has no third kind.
        builder->result.symbols[index].function =
            builder->result.symbols[index].function ||
            (builder->result.symbols[index].defined &&
             builder->result.sections[builder->result.symbols[index].section].kind == ASSEMBLY_UNIT_SECTION_TEXT);
    }
    // The generated names for `1:`/`1f` are assembler bookkeeping. Every
    // reference to one has just been resolved in place, so they leave the
    // symbol table the way GNU as drops its own `.L` locals -- a tool reading
    // the object should see the file's names and nothing else.
    u32* remapped = arena_allocate(builder->arena, u32, builder->result.symbol_count ? builder->result.symbol_count : 1);
    u32 surviving = 0;
    for (u32 index = 0; index < builder->result.symbol_count; index += 1)
    {
        AssemblyUnitSymbol symbol = builder->result.symbols[index];
        bool generated = symbol.defined && !symbol.global && string_starts_with_sequence(symbol.name, S8(".Lnum."));
        for (u32 relocation = 0; relocation < builder->result.relocation_count && generated; relocation += 1)
        {
            generated = builder->result.relocations[relocation].symbol != index;
        }
        remapped[index] = generated ? UINT32_MAX : surviving;
        builder->result.symbols[surviving] = symbol;
        surviving += !generated;
    }
    builder->result.symbol_count = surviving;
    for (u32 index = 0; index < builder->result.relocation_count; index += 1)
    {
        builder->result.relocations[index].symbol = remapped[builder->result.relocations[index].symbol];
    }
}

AssemblyUnitResult assembly_unit_encode(Arena* arena, String8 source, AssemblyEncodeOptions options)
{
    AssemblyUnitResult empty = {0};
    if (!arena)
    {
        return empty;
    }
    AssemblyUnitBuilder builder = {
        .arena = arena,
        .target = options.target,
        .syntax = options.syntax == ASSEMBLY_SYNTAX_DEFAULT && options.target.cpu_arch == CPU_ARCH_X86_64 ? ASSEMBLY_SYNTAX_ATT : options.syntax,
        .current_section = UINT32_MAX,
        .column = 1,
    };
    builder.result.diagnostics = arena_allocate(arena, AssemblyDiagnostic, ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY);
    if (options.target.cpu_arch != CPU_ARCH_X86_64 && options.syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        assembly_unit_diagnostic(&builder, ASSEMBLY_DIAGNOSTIC_INVALID_SYNTAX, S8("the AT&T and Intel dialects are x86-64 only"));
        return builder.result;
    }

    // A block comment may span lines, so it is blanked in a copy before
    // anything else reads the text: every pass then sees the same line
    // numbering, and the same columns, as the file on disk.
    char8* text = arena_allocate(arena, char8, source.length ? source.length : 1);
    u32 line_count = 1;
    for (u64 index = 0; index < source.length; index += 1)
    {
        text[index] = source.pointer[index];
        line_count += source.pointer[index] == '\n';
    }
    for (u64 index = 0; index + 1 < source.length;)
    {
        // The scan reads `source` rather than `text`: `text` is being blanked
        // as it goes, so a closing `*/` looked for there would never be found
        // and the whole file after the first comment would disappear.
        if (source.pointer[index] != '/' || source.pointer[index + 1] != '*')
        {
            index += 1;
            continue;
        }
        text[index] = ' ';
        text[index + 1] = ' ';
        u64 scan = index + 2;
        while (scan < source.length)
        {
            bool closing = scan + 1 < source.length && source.pointer[scan] == '*' && source.pointer[scan + 1] == '/';
            text[scan] = source.pointer[scan] == '\n' ? '\n' : ' ';
            scan += 1;
            if (closing)
            {
                text[scan] = ' ';
                scan += 1;
                break;
            }
        }
        index = scan;
    }
    String8 blanked = {.pointer = text, .length = source.length};

    builder.symbol_capacity = line_count * 2 + 16;
    builder.relocation_capacity = line_count + 16;
    builder.piece_capacity = line_count + 16;
    builder.numeric_label_capacity = line_count * 2 + 16;
    builder.result.sections = arena_allocate(arena, AssemblyUnitSection, ASSEMBLY_UNIT_SECTION_CAPACITY);
    builder.section_offsets = arena_allocate(arena, u64, ASSEMBLY_UNIT_SECTION_CAPACITY);
    builder.result.symbols = arena_allocate(arena, AssemblyUnitSymbol, builder.symbol_capacity);
    builder.result.relocations = arena_allocate(arena, AssemblyUnitRelocation, builder.relocation_capacity);
    builder.relocation_lines = arena_allocate(arena, u32, builder.relocation_capacity);
    builder.pieces = arena_allocate(arena, AssemblyUnitPiece, builder.piece_capacity);
    builder.numeric_labels = arena_allocate(arena, AssemblyUnitNumericLabel, builder.numeric_label_capacity);

    assembly_unit_collect_numeric_labels(&builder, blanked);
    assembly_unit_parse(&builder, blanked);
    if (!builder.result.diagnostic_count)
    {
        assembly_unit_materialize(&builder);
    }
    return builder.result;
}
