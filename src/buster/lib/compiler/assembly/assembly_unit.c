// The assembly translation unit: assembly_unit_encode at the bottom takes the
// complete text of a `.s` file and returns sections, symbols, relocations, and
// diagnostics. assembly.c's assembly_encode is the instruction layer beneath
// it and is called once per instruction statement; everything a whole file has that
// a single statement does not lives here.
//
// Three passes over an AssemblyUnitBuilder sized from the statement count:
//   assembly_unit_collect_numeric_labels  local `1:` definitions in source
//                                         order, so `1f`/`1b` can name one
//   assembly_unit_parse                   labels, directives, and one
//                                         assembly_encode call per
//                                         instruction statement, each producing a
//                                         piece of bytes in a section
//   assembly_unit_materialize             pieces concatenated per section and
//                                         binding-invariant same-section
//                                         PC-relative fixups resolved in place
//
// Layout, in file order; each anchor is a definition to search for:
//   assembly_unit_space ..                 text plumbing shared by the passes
//   assembly_unit_diagnostic
//   assembly_unit_symbol_intern ..         symbol and section tables
//   assembly_unit_section_select
//   assembly_unit_evaluate                 section-relative directive expressions
//   assembly_unit_materialize_integers     final-label data evaluation
//   assembly_unit_directive_*              the directive vocabulary
//   assembly_unit_instruction              the assembly_encode call
//   assembly_unit_statement                target comments/separators and positions
//   assembly_unit_aarch64_control_fixup     checked final-label control fixups
//   assembly_unit_parse, assembly_unit_encode
//
// Anything the vocabulary does not cover is refused with a diagnostic naming
// the directive and its line; nothing is silently dropped.

#include <buster/lib/compiler/assembly/assembly_unit_internal.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>

#include <buster/lib/string.h>

enum
{
    ASSEMBLY_UNIT_SECTION_CAPACITY = 32,
    ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY = 16,
    ASSEMBLY_UNIT_OPERAND_CAPACITY = 64,
    // `.Lnum.` plus a 20-digit value, a dot, and a 10-digit ordinal.
    ASSEMBLY_UNIT_GENERATED_NAME_MAX = 40,
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
    u64 statement;
};

typedef struct AssemblyUnitSourceCursor AssemblyUnitSourceCursor;
struct AssemblyUnitSourceCursor
{
    u64 offset;
    u64 line_start;
    u32 line;
};

typedef struct AssemblyUnitBuilder AssemblyUnitBuilder;
typedef struct AssemblyUnitInteger AssemblyUnitInteger;
struct AssemblyUnitInteger
{
    String8 expression;
    String8 directive;
    u64 offset;
    u32 section;
    u32 width;
    u32 line;
    u32 column;
};

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
    u32* relocation_columns;
    AssemblyUnitInteger* integers;
    u32 integer_count;
    u32 integer_capacity;
    u32 piece_count;
    u32 piece_capacity;
    u32 numeric_label_count;
    u32 numeric_label_capacity;
    u32 symbol_capacity;
    u32 relocation_capacity;
    u32 current_section;
    u32 line;
    u32 column;
    u64 statement;
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

// The printer quotes ordinary symbol names and section names too.
BUSTER_GLOBAL_LOCAL String8 assembly_unit_unquote(String8 name)
{
    if (name.length >= 2 && name.pointer[0] == '"' && name.pointer[name.length - 1] == '"')
    {
        name = string_slice(name, 1, name.length - 1);
    }
    return name;
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

// Bare section defaults are an exact name, or a dot-delimited member of
// an ordinary code/data family. DWARF names retain their nonallocated kinds;
// unsupported bare names cannot silently acquire flags from a raw prefix.
BUSTER_GLOBAL_LOCAL bool assembly_unit_section_kind_for_name(String8 name, AssemblyUnitSectionKind* kind)
{
    static const struct
    {
        String8 name;
        AssemblyUnitSectionKind kind;
        bool suffix;
    } rows[] = {
        {S8_INITIALIZER(".text"), ASSEMBLY_UNIT_SECTION_TEXT, true},
        {S8_INITIALIZER(".init"), ASSEMBLY_UNIT_SECTION_TEXT, false},
        {S8_INITIALIZER(".fini"), ASSEMBLY_UNIT_SECTION_TEXT, false},
        {S8_INITIALIZER(".rodata"), ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA, true},
        {S8_INITIALIZER(".data"), ASSEMBLY_UNIT_SECTION_DATA, true},
        {S8_INITIALIZER(".bss"), ASSEMBLY_UNIT_SECTION_ZERO, true},
        {S8_INITIALIZER(".debug_info"), ASSEMBLY_UNIT_SECTION_DEBUG_INFO, false},
        {S8_INITIALIZER(".debug_abbrev"), ASSEMBLY_UNIT_SECTION_DEBUG_ABBREV, false},
        {S8_INITIALIZER(".debug_line"), ASSEMBLY_UNIT_SECTION_DEBUG_LINE, false},
        {S8_INITIALIZER(".debug_str"), ASSEMBLY_UNIT_SECTION_DEBUG_STR, false},
        {S8_INITIALIZER(".debug_loc"), ASSEMBLY_UNIT_SECTION_DEBUG_LOC, false},
        {S8_INITIALIZER(".debug_ranges"), ASSEMBLY_UNIT_SECTION_DEBUG_RANGES, false},
        {S8_INITIALIZER(".debug_addr"), ASSEMBLY_UNIT_SECTION_DEBUG_ADDR, false},
        {S8_INITIALIZER(".debug_str_offsets"), ASSEMBLY_UNIT_SECTION_DEBUG_STR_OFFSETS, false},
        {S8_INITIALIZER(".debug_line_str"), ASSEMBLY_UNIT_SECTION_DEBUG_LINE_STR, false},
        {S8_INITIALIZER(".debug_rnglists"), ASSEMBLY_UNIT_SECTION_DEBUG_RNGLISTS, false},
        {S8_INITIALIZER(".debug_loclists"), ASSEMBLY_UNIT_SECTION_DEBUG_LOCLISTS, false},
    };
    bool matched = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rows) && !matched; index += 1)
    {
        matched = string_equal(name, rows[index].name) ||
                  (rows[index].suffix && name.length > rows[index].name.length &&
                   name.pointer[rows[index].name.length] == '.' && string_starts_with_sequence(name, rows[index].name));
        if (matched)
        {
            *kind = rows[index].kind;
        }
    }
    return matched;
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
    u32 subtract_symbol;
    bool has_symbol;
    bool has_subtract_symbol;
    bool location;
    bool subtract_location;
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
        if (digit >= base || result > (UINT64_MAX - digit) / base)
        {
            return false;
        }
        result = result * base + digit;
        any = true;
    }
    *value = result;
    return any;
}

#if BUSTER_INCLUDE_TESTS
bool assembly_unit_test_parse_number(String8 text, u64* value)
{
    return assembly_unit_parse_number(text, value);
}
#endif

// Preserve the section base of `.` until two same-section terms cancel or
// the data writer selects S+A-P. The caller supplies this operand's location,
// including its position in a comma-separated list. Names may be quoted, as
// in the -S printer. At most one positive and one negative symbolic term fit
// the relocation model; unsupported expressions are diagnosed by the caller.
BUSTER_GLOBAL_LOCAL bool assembly_unit_evaluate(AssemblyUnitBuilder* builder, String8 text, u64 location, AssemblyUnitValue* value)
{
    AssemblyUnitValue result = {.symbol = UINT32_MAX, .subtract_symbol = UINT32_MAX};
    u64 index = 0;
    s64 sign = 1;
    bool expect_term = true;
    bool valid = true;
    while (index < text.length && valid)
    {
        while (index < text.length && assembly_unit_space(text.pointer[index]))
        {
            index += 1;
        }
        if (index < text.length)
        {
            char8 code_unit = text.pointer[index];
            if (!expect_term)
            {
                valid = code_unit == '+' || code_unit == '-';
                sign = code_unit == '-' ? -1 : 1;
                index += 1;
                expect_term = true;
            }
            else
            {
                if (code_unit == '+' || code_unit == '-')
                {
                    sign *= code_unit == '-' ? -1 : 1;
                    index += 1;
                    while (index < text.length && assembly_unit_space(text.pointer[index])) index += 1;
                    code_unit = index < text.length ? text.pointer[index] : 0;
                }
                u64 start = index;
                u32 symbol = UINT32_MAX;
                bool symbolic = false;
                bool dot = code_unit == '.' && (index + 1 >= text.length || !assembly_unit_name_character(text.pointer[index + 1]));
                if (dot)
                {
                    symbolic = true;
                    result.constant += sign * (s64)location;
                    index += 1;
                }
                else if (assembly_unit_digit(code_unit))
                {
                    while (index < text.length && assembly_unit_name_character(text.pointer[index])) index += 1;
                    u64 number = 0;
                    valid = assembly_unit_parse_number(string_slice(text, start, index), &number);
                    result.constant += sign * (s64)number;
                }
                else if (assembly_unit_name_start(code_unit) || code_unit == '"')
                {
                    bool quoted = code_unit == '"';
                    index += quoted;
                    start = index;
                    while (index < text.length && (quoted ? text.pointer[index] != '"' : assembly_unit_name_character(text.pointer[index]))) index += 1;
                    valid = index > start && (!quoted || index < text.length);
                    if (valid)
                    {
                        symbol = assembly_unit_symbol_intern(builder, string_slice(text, start, index));
                        valid = symbol != UINT32_MAX;
                        symbolic = true;
                        index += quoted;
                    }
                }
                else
                {
                    valid = false;
                }
                if (valid && symbolic)
                {
                    if (sign > 0)
                    {
                        valid = !result.has_symbol;
                        result.has_symbol = true;
                        result.symbol = symbol;
                        result.location = dot;
                    }
                    else
                    {
                        valid = !result.has_subtract_symbol;
                        result.has_subtract_symbol = true;
                        result.subtract_symbol = symbol;
                        result.subtract_location = dot;
                    }
                }
                sign = 1;
                expect_term = false;
            }
        }
    }
    valid = valid && !expect_term;
    if (valid && result.has_symbol && result.has_subtract_symbol)
    {
        AssemblyUnitSymbol positive = result.location ? (AssemblyUnitSymbol){.defined = true, .section = builder->current_section}
                                                     : builder->result.symbols[result.symbol];
        AssemblyUnitSymbol negative = result.subtract_location ? (AssemblyUnitSymbol){.defined = true, .section = builder->current_section}
                                                              : builder->result.symbols[result.subtract_symbol];
        if (positive.defined && negative.defined && positive.section == negative.section && !positive.weak && !negative.weak)
        {
            result.constant += (s64)positive.value - (s64)negative.value;
            result.has_symbol = false;
            result.has_subtract_symbol = false;
        }
    }
    if (valid) *value = result;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_evaluate_absolute(AssemblyUnitBuilder* builder, String8 text, s64* value)
{
    AssemblyUnitValue evaluated = {0};
    u64 location = builder->current_section < builder->result.section_count ? builder->section_offsets[builder->current_section] : 0;
    bool valid = assembly_unit_evaluate(builder, assembly_unit_trim(text), location, &evaluated) &&
                 !evaluated.has_symbol && !evaluated.has_subtract_symbol;
    if (valid) *value = evaluated.constant;
    return valid;
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
        if (index < text.length && quoted && text.pointer[index] == '\\' && index + 1 < text.length)
        {
            index += 1;
            continue;
        }
        if (index < text.length && text.pointer[index] == '"')
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
    String8 name = assembly_unit_unquote(assembly_unit_word(parts[0], 0));
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
    u32 symbol = assembly_unit_symbol_intern(builder, assembly_unit_unquote(assembly_unit_word(parts[0], 0)));
    if (symbol == UINT32_MAX)
    {
        return false;
    }
    AssemblyUnitSymbol* record = builder->result.symbols + symbol;
    if (string_equal(directive, S8(".globl")) || string_equal(directive, S8(".global")) || string_equal(directive, S8(".extern")))
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
    // GNU as reads `.align` as a byte count on x86 ELF but as a power of two
    // on AArch64, where it is the same directive as `.p2align`.
    bool power = string_equal(directive, S8(".p2align")) || (string_equal(directive, S8(".align")) && builder->target.cpu_arch == CPU_ARCH_AARCH64);
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
    bool filled = true;
    if (part_count == 1 && section->kind == ASSEMBLY_UNIT_SECTION_TEXT)
    {
        filled = assembly_fill_executable_padding(builder->target, bytes, offset, padding);
    }
    else
    {
        // An explicit fill is user data, even inside an executable section.
        memset(bytes, part_count == 2 ? (u8)fill : 0, padding);
    }
    return filled && assembly_unit_append(builder, bytes, padding);
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
    builder->relocation_columns[builder->result.relocation_count] = builder->column;
    builder->result.relocations[builder->result.relocation_count++] = (AssemblyUnitRelocation){
        .addend = addend,
        .offset = offset,
        .section = builder->current_section,
        .symbol = symbol,
        .kind = kind,
    };
    return true;
}

// Byte width of an integer data directive, or zero for any other spelling.
// GNU as gives `.word` the target's word: 16 bits on x86, 32 on AArch64,
// which also spells 16 bits `.hword` and 64 bits `.xword`/`.dword`.
BUSTER_GLOBAL_LOCAL u32 assembly_unit_integer_directive_width(Target target, String8 directive)
{
    bool aarch64 = target.cpu_arch == CPU_ARCH_AARCH64;
    u32 width = 0;
    if (string_equal(directive, S8(".byte")))
    {
        width = 1;
    }
    else if (string_equal(directive, S8(".short")) || string_equal(directive, S8(".hword")) || string_equal(directive, S8(".value")))
    {
        width = 2;
    }
    else if (string_equal(directive, S8(".word")))
    {
        width = aarch64 ? 4 : 2;
    }
    else if (string_equal(directive, S8(".long")) || string_equal(directive, S8(".int")))
    {
        width = 4;
    }
    else if (string_equal(directive, S8(".quad")) || (aarch64 && (string_equal(directive, S8(".xword")) || string_equal(directive, S8(".dword")))))
    {
        width = 8;
    }
    return width;
}

// Retain data expressions until labels and binding directives are complete.
// Bytes reserve their final positions now; materialization evaluates each
// expression at its own field offset, so forward differences need no pass
// over instructions and comma-separated `.` operands do not share a place.
BUSTER_GLOBAL_LOCAL bool assembly_unit_directive_integer(AssemblyUnitBuilder* builder, u32 width, String8 directive, String8 operands)
{
    String8 parts[ASSEMBLY_UNIT_OPERAND_CAPACITY] = {0};
    u32 count = assembly_unit_split_operands(operands, parts, BUSTER_ARRAY_LENGTH(parts));
    bool valid = count != UINT32_MAX && count && assembly_unit_section_current(builder);
    valid = valid && builder->result.sections[builder->current_section].kind != ASSEMBLY_UNIT_SECTION_ZERO &&
            count <= builder->integer_capacity - builder->integer_count;
    // Refuse malformed expressions before reserving any bytes. Symbols and
    // bindings are evaluated again after parsing to resolve forward differences.
    u64 base = valid ? builder->section_offsets[builder->current_section] : 0;
    for (u32 index = 0; index < count && valid; index += 1)
    {
        AssemblyUnitValue value = {0};
        valid = assembly_unit_evaluate(builder, parts[index], base + (u64)index * width, &value);
    }
    if (valid)
    {
        for (u32 index = 0; index < count; index += 1)
        {
            builder->integers[builder->integer_count++] = (AssemblyUnitInteger){
                .expression = parts[index], .directive = directive, .offset = base + (u64)index * width,
                .section = builder->current_section, .width = width, .line = builder->line, .column = builder->column,
            };
        }
        u64 length = (u64)count * width;
        u8* bytes = arena_allocate_zeroed(builder->arena, u8, length);
        valid = assembly_unit_append(builder, bytes, length);
    }
    return valid;
}

// A private local symbol at section offset zero represents a section base.
// It is created only when a `.` address actually needs a relocation.
BUSTER_GLOBAL_LOCAL u32 assembly_unit_location_symbol(AssemblyUnitBuilder* builder)
{
    u32 symbol = builder->result.symbol_count;
    for (u32 index = 0; index < builder->result.symbol_count; index += 1)
    {
        AssemblyUnitSymbol record = builder->result.symbols[index];
        if (record.defined && record.section == builder->current_section && !record.value && !record.global && !record.weak &&
            string_equal(record.name, builder->result.sections[builder->current_section].name))
        {
            symbol = index;
            break;
        }
    }
    if (symbol == builder->result.symbol_count)
    {
        if (symbol < builder->symbol_capacity)
        {
            builder->result.symbols[builder->result.symbol_count++] = (AssemblyUnitSymbol){
                .name = builder->result.sections[builder->current_section].name, .section = builder->current_section, .defined = true,
            };
        }
        else
        {
            symbol = UINT32_MAX;
        }
    }
    return symbol;
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_materialize_integers(AssemblyUnitBuilder* builder)
{
    bool valid = true;
    for (u32 index = 0; index < builder->integer_count && valid; index += 1)
    {
        AssemblyUnitInteger integer = builder->integers[index];
        builder->current_section = integer.section;
        builder->line = integer.line;
        builder->column = integer.column;
        AssemblyUnitValue value = {0};
        valid = assembly_unit_evaluate(builder, integer.expression, integer.offset, &value);
        bool relative = value.has_symbol && value.has_subtract_symbol && value.subtract_location && !value.location;
        valid = valid && (!value.has_subtract_symbol || relative);
        if (valid && value.has_symbol)
        {
            AssemblyRelocationKind kind = ASSEMBLY_RELOCATION_COUNT;
            if (integer.width == 4 || integer.width == 8)
            {
                bool aarch64 = builder->target.cpu_arch == CPU_ARCH_AARCH64;
                kind = relative ? (integer.width == 8 ? (aarch64 ? ASSEMBLY_RELOCATION_AARCH64_PREL64 : ASSEMBLY_RELOCATION_X86_PC64)
                                                     : (aarch64 ? ASSEMBLY_RELOCATION_AARCH64_PREL32 : ASSEMBLY_RELOCATION_X86_PC32))
                                : (integer.width == 8 ? ASSEMBLY_RELOCATION_X86_ABSOLUTE64 : ASSEMBLY_RELOCATION_X86_ABSOLUTE32);
            }
            u32 symbol = value.location ? assembly_unit_location_symbol(builder) : value.symbol;
            s64 addend = value.constant + (relative ? (s64)integer.offset : 0);
            valid = kind != ASSEMBLY_RELOCATION_COUNT && symbol != UINT32_MAX &&
                    assembly_unit_relocation_append(builder, symbol, integer.offset, addend, kind);
        }
        else if (valid && integer.width < 8)
        {
            // Accept either a signed or an unsigned reading of the field and
            // refuse anything wider, as llvm-mc does: those bits would be
            // lost, which GNU as only warns about.
            s64 limit = (s64)1 << (integer.width * 8);
            if (value.constant < -(limit >> 1) || value.constant >= limit)
            {
                assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("value does not fit directive {S8}"),
                                                integer.directive);
                valid = false;
            }
        }
        if (valid && !value.has_symbol)
        {
            ByteSlice bytes = builder->result.sections[integer.section].data;
            for (u32 byte = 0; byte < integer.width; byte += 1)
            {
                bytes.pointer[integer.offset + byte] = (u8)((u64)value.constant >> (byte * 8));
            }
        }
        if (!valid && !builder->result.diagnostic_count)
        {
            assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE,
                                            S8("unsupported operand for directive {S8}"), integer.directive);
        }
    }
    return valid;
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
    if (string_equal(directive, S8(".globl")) || string_equal(directive, S8(".global")) || string_equal(directive, S8(".extern")) ||
        string_equal(directive, S8(".weak")) || string_equal(directive, S8(".hidden")) || string_equal(directive, S8(".type")) || string_equal(directive, S8(".size")))
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
    u32 width = assembly_unit_integer_directive_width(builder->target, directive);
    if (width)
    {
        return assembly_unit_directive_integer(builder, width, directive, operands);
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
// here into a plain symbol name in a fresh copy, preserving the PLT request
// beside the resulting relocation and the original text for diagnostics.
BUSTER_GLOBAL_LOCAL bool assembly_unit_rewrite_line(AssemblyUnitBuilder* builder, String8 line, String8* rewritten, String8* plt_symbol)
{
    bool changed = false;
    bool quoted = false;
    for (u64 index = 0; index < line.length && !changed; index += 1)
    {
        if (quoted && line.pointer[index] == '\\' && index + 1 < line.length)
        {
            index += 1;
        }
        else if (line.pointer[index] == '"')
        {
            quoted = !quoted;
        }
        else if (!quoted)
        {
            bool boundary = !index || (!assembly_unit_name_character(line.pointer[index - 1]) && line.pointer[index - 1] != '@');
            changed = (line.pointer[index] == '@') || (boundary && assembly_unit_digit(line.pointer[index]));
        }
    }
    if (!changed)
    {
        *rewritten = line;
        return true;
    }
    // A generated local-label name is longer than the `1f` it replaces, so the
    // copy is sized for every character of the line becoming one.
    u64 capacity = line.length * ASSEMBLY_UNIT_GENERATED_NAME_MAX + 64;
    char8* text = arena_allocate(builder->arena, char8, capacity);
    u64 length = 0;
    u64 index = 0;
    u64 quoted_name_begin = 0;
    u64 quoted_name_end = 0;
    quoted = false;
    while (index < line.length)
    {
        char8 code_unit = line.pointer[index];
        bool boundary = !index || (!assembly_unit_name_character(line.pointer[index - 1]) && line.pointer[index - 1] != '@');
        if (quoted && code_unit == '\\' && index + 1 < line.length)
        {
            text[length++] = code_unit;
            text[length++] = line.pointer[index + 1];
            index += 2;
            continue;
        }
        if (code_unit == '"')
        {
            if (quoted) quoted_name_end = index;
            else quoted_name_begin = index + 1;
            quoted = !quoted;
            text[length++] = code_unit;
            index += 1;
            continue;
        }
        if (quoted)
        {
            text[length++] = code_unit;
            index += 1;
            continue;
        }
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
                    if (direction == 'b' && candidate.statement <= builder->statement)
                    {
                        name = candidate.name;
                    }
                    if (direction == 'f' && candidate.statement > builder->statement && !name.length)
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
                    assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("too many local label references on one line"));
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
            u64 name_begin = index;
            u64 name_end = index;
            if (index && line.pointer[index - 1] == '"' && quoted_name_end == index - 1)
            {
                name_begin = quoted_name_begin;
                name_end = quoted_name_end;
            }
            else
            {
                while (name_begin && assembly_unit_name_character(line.pointer[name_begin - 1]))
                {
                    name_begin -= 1;
                }
            }
            if ((!string_equal(suffix, S8("PLT")) && !string_equal(suffix, S8("plt"))) ||
                name_begin == name_end || plt_symbol->length)
            {
                assembly_unit_diagnostic_format(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("unsupported symbol modifier '@{S8}'"), suffix);
                return false;
            }
            *plt_symbol = string_slice(line, name_begin, name_end);
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
    String8 plt_symbol = {0};
    if (!assembly_unit_rewrite_line(builder, line, &rewritten, &plt_symbol))
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
                                                       .unit_control_relocations = true,
                                                   });
    if (encoded.diagnostic_count)
    {
        assembly_unit_diagnostic(builder, encoded.diagnostics[0].kind, encoded.diagnostics[0].message);
        return false;
    }
    u64 base = builder->section_offsets[builder->current_section];
    bool plt_matched = !plt_symbol.length;
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
        bool direct_branch = relocation.kind == ASSEMBLY_RELOCATION_X86_PC32 && relocation.offset &&
                             (encoded.bytes.pointer[relocation.offset - 1] == 0xe8 || encoded.bytes.pointer[relocation.offset - 1] == 0xe9);
        builder->result.relocations[builder->result.relocation_count - 1].x86_branch = direct_branch;
        if (string_equal(encoded.symbols[relocation.symbol].name, plt_symbol))
        {
            plt_matched = direct_branch;
            builder->result.relocations[builder->result.relocation_count - 1].plt = direct_branch;
        }
    }
    bool result;
    if (plt_matched)
    {
        result = assembly_unit_append(builder, encoded.bytes.pointer, encoded.bytes.length);
    }
    else
    {
        assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE, S8("@PLT requires a direct x86 call or jump with a 32-bit displacement"));
        result = false;
    }
    return result;
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
    else if (line.pointer[0] == '"')
    {
        end = 1;
        while (end < line.length && line.pointer[end] != '"') end += 1;
        end += end < line.length;
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

// Apple AArch64 uses ';' for comments and '%%' between statements. The other
// supported targets use ';' between statements; '#' is an x86 comment only.
BUSTER_GLOBAL_LOCAL bool assembly_unit_apple_aarch64(Target target)
{
    return target.cpu_arch == CPU_ARCH_AARCH64 &&
           (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS);
}

BUSTER_GLOBAL_LOCAL bool assembly_unit_line_comment(Target target, String8 source, u64 index)
{
    char8 value = source.pointer[index];
    return (value == '#' && target.cpu_arch == CPU_ARCH_X86_64) ||
           (value == ';' && assembly_unit_apple_aarch64(target)) ||
           (value == '/' && index + 1 < source.length && source.pointer[index + 1] == '/');
}

BUSTER_GLOBAL_LOCAL u32 assembly_unit_separator_length(Target target, String8 source, u64 index)
{
    u32 length;
    if (assembly_unit_apple_aarch64(target))
    {
        length = source.pointer[index] == '%' && index + 1 < source.length && source.pointer[index + 1] == '%' ? 2 : 0;
    }
    else
    {
        length = source.pointer[index] == ';' ? 1 : 0;
    }
    return length;
}

// Block comments have already been blanked. Both passes consume this same
// target-aware stream; physical positions stay separate from statement order.
BUSTER_GLOBAL_LOCAL String8 assembly_unit_statement(Target target, String8 source, AssemblyUnitSourceCursor* cursor, u32* line_number, u32* column)
{
    u64 start = cursor->offset;
    u64 line_start = cursor->line_start;
    u64 end = start;
    char8 quote = 0;
    bool done = false;
    *line_number = cursor->line;
    while (end < source.length && !done)
    {
        char8 value = source.pointer[end];
        if (value == '\n')
        {
            cursor->offset = end + 1;
            cursor->line_start = end + 1;
            cursor->line += 1;
            done = true;
        }
        else if (quote)
        {
            if (value == '\\' && end + 1 < source.length && source.pointer[end + 1] != '\n')
            {
                end += 2;
            }
            else
            {
                if (value == quote) quote = 0;
                end += 1;
            }
        }
        else if (value == '"' || value == '\'')
        {
            quote = value;
            end += 1;
        }
        else if (assembly_unit_line_comment(target, source, end))
        {
            cursor->offset = end;
            while (cursor->offset < source.length && source.pointer[cursor->offset] != '\n') cursor->offset += 1;
            if (cursor->offset < source.length)
            {
                cursor->offset += 1;
                cursor->line_start = cursor->offset;
                cursor->line += 1;
            }
            done = true;
        }
        else
        {
            u32 separator_length = assembly_unit_separator_length(target, source, end);
            if (separator_length)
            {
                cursor->offset = end + separator_length;
                done = true;
            }
            else
            {
                end += 1;
            }
        }
    }
    if (!done) cursor->offset = end;
    String8 trimmed = assembly_unit_trim(string_slice(source, start, end));
    *column = trimmed.length ? (u32)((u64)(trimmed.pointer - source.pointer) - line_start + 1) : 1;
    return trimmed;
}

BUSTER_GLOBAL_LOCAL void assembly_unit_collect_numeric_labels(AssemblyUnitBuilder* builder, String8 source)
{
    AssemblyUnitSourceCursor cursor = {.line = 1};
    u64 statement = 0;
    while (cursor.offset < source.length)
    {
        u32 column = 0;
        u32 line_number = 0;
        String8 line = assembly_unit_statement(builder->target, source, &cursor, &line_number, &column);
        statement += 1;
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
                    .statement = statement,
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
    AssemblyUnitSourceCursor cursor = {.line = 1};
    u32 numeric_index = 0;
    String8 pending_prefix = {0};
    while (cursor.offset < source.length && builder->result.diagnostic_count < ASSEMBLY_UNIT_DIAGNOSTIC_CAPACITY)
    {
        String8 line = assembly_unit_statement(builder->target, source, &cursor, &builder->line, &builder->column);
        builder->statement += 1;
        u64 label = assembly_unit_leading_label(line);
        if (pending_prefix.length && (label || (line.length && line.pointer[0] == '.')))
        {
            assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_INVALID_STATEMENT,
                                     S8("a repeat or lock prefix on its own line must be followed by an instruction"));
            return;
        }
        while (label)
        {
            String8 spelling = assembly_unit_unquote(string_slice(line, 0, label));
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

BUSTER_GLOBAL_LOCAL bool assembly_unit_aarch64_control_relocation(AssemblyRelocationKind kind)
{
    return kind == ASSEMBLY_RELOCATION_AARCH64_BRANCH26 || kind == ASSEMBLY_RELOCATION_AARCH64_CALL26 ||
           kind == ASSEMBLY_RELOCATION_AARCH64_CONDBR19 || kind == ASSEMBLY_RELOCATION_AARCH64_COMPAREBR19 ||
           kind == ASSEMBLY_RELOCATION_AARCH64_TESTBR14;
}

// Decode the retained word through the shared semantic owner before applying
// its checked displacement. Symbol values and signed addends remain separate.
BUSTER_GLOBAL_LOCAL bool assembly_unit_aarch64_control_fixup(AssemblyUnitBuilder* builder, AssemblyUnitRelocation relocation,
                                                             AssemblyUnitSymbol symbol, String8* mnemonic)
{
    ByteSlice data = builder->result.sections[relocation.section].data;
    bool valid = !(relocation.offset & 3) && relocation.offset <= data.length &&
                 sizeof(u32) <= data.length - relocation.offset;
    BusterAarch64ControlInstruction instruction = {0};
    BusterAarch64ControlSemanticRecord row = {0};
    u32 word = 0;
    if (valid)
    {
        for (u32 byte = 0; byte < sizeof(word); byte += 1)
        {
            word |= (u32)data.pointer[relocation.offset + byte] << (byte * 8);
        }
        valid = buster_aarch64_control_semantic_decode(word, &instruction) &&
                buster_aarch64_control_semantic_row(instruction.row, &row);
    }
    if (valid)
    {
        (void)buster_aarch64_control_semantic_string(row.mnemonic, mnemonic);
        valid = (relocation.kind == ASSEMBLY_RELOCATION_AARCH64_BRANCH26 && row.fixup_kind == BUSTER_AARCH64_CONTROL_FIXUP_BRANCH26) ||
                (relocation.kind == ASSEMBLY_RELOCATION_AARCH64_CALL26 && row.fixup_kind == BUSTER_AARCH64_CONTROL_FIXUP_CALL26) ||
                (relocation.kind == ASSEMBLY_RELOCATION_AARCH64_CONDBR19 && row.fixup_kind == BUSTER_AARCH64_CONTROL_FIXUP_B_COND19) ||
                (relocation.kind == ASSEMBLY_RELOCATION_AARCH64_COMPAREBR19 && row.fixup_kind == BUSTER_AARCH64_CONTROL_FIXUP_COMPARE19) ||
                (relocation.kind == ASSEMBLY_RELOCATION_AARCH64_TESTBR14 && row.fixup_kind == BUSTER_AARCH64_CONTROL_FIXUP_TEST14);
    }
    u32 patched = 0;
    BusterAarch64ControlFixupResult fixup = {0};
    if (valid)
    {
        valid = buster_aarch64_control_semantic_fixup(instruction.row, word,
            (BusterAarch64ControlFixupRequest){.target = builder->target, .place_address = relocation.offset,
                .target_address = symbol.value, .addend = relocation.addend, .symbol_defined = true}, &patched, &fixup) &&
                fixup.resolved;
    }
    if (valid)
    {
        for (u32 byte = 0; byte < sizeof(patched); byte += 1)
        {
            data.pointer[relocation.offset + byte] = (u8)(patched >> (byte * 8));
        }
    }
    return valid;
}

// Pieces become section bytes, and every relocation that names a symbol
// defined in its own section with binding-invariant identity can be resolved
// here. Weak definitions remain replaceable even when hidden; ELF globals
// with default visibility remain preemptible in an external shared link.
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
    if (!assembly_unit_materialize_integers(builder))
    {
        return;
    }
    u32 kept = 0;
    bool elf = builder->target.os == OPERATING_SYSTEM_LINUX || builder->target.os == OPERATING_SYSTEM_ANDROID ||
               builder->target.os == OPERATING_SYSTEM_FREESTANDING;
    for (u32 index = 0; index < builder->result.relocation_count; index += 1)
    {
        AssemblyUnitRelocation relocation = builder->result.relocations[index];
        AssemblyUnitSymbol symbol = builder->result.symbols[relocation.symbol];
        u32 width = relocation.kind == ASSEMBLY_RELOCATION_X86_PC8    ? 1
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC16 ? 2
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC32 ? 4
                    : relocation.kind == ASSEMBLY_RELOCATION_X86_PC64 || relocation.kind == ASSEMBLY_RELOCATION_AARCH64_PREL64 ? 8
                    : relocation.kind == ASSEMBLY_RELOCATION_AARCH64_PREL32 ? 4
                                                                      : 0;
        bool replaceable = symbol.weak || (elf && symbol.global && !symbol.hidden);
        bool control = assembly_unit_aarch64_control_relocation(relocation.kind);
        bool short_control = control && relocation.kind != ASSEMBLY_RELOCATION_AARCH64_BRANCH26 &&
                             relocation.kind != ASSEMBLY_RELOCATION_AARCH64_CALL26;
        if (control && symbol.defined && symbol.section == relocation.section && !replaceable)
        {
            String8 mnemonic = S8("control");
            if (!assembly_unit_aarch64_control_fixup(builder, relocation, symbol, &mnemonic))
            {
                builder->line = builder->relocation_lines[index];
                builder->column = builder->relocation_columns[index];
                assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_BRANCH_OUT_OF_RANGE,
                    string_format(builder->arena, S8("AArch64 {S8} branch to '{S8}' is out of range or unaligned"), mnemonic, symbol.name));
                return;
            }
            continue;
        }
        if (short_control)
        {
            String8 mnemonic = relocation.kind == ASSEMBLY_RELOCATION_AARCH64_CONDBR19 ? S8("b.cond")
                               : relocation.kind == ASSEMBLY_RELOCATION_AARCH64_COMPAREBR19 ? S8("cbz/cbnz")
                                                                                           : S8("tbz/tbnz");
            builder->line = builder->relocation_lines[index];
            builder->column = builder->relocation_columns[index];
            assembly_unit_diagnostic(builder, ASSEMBLY_DIAGNOSTIC_UNSUPPORTED_FEATURE,
                string_format(builder->arena, S8("AArch64 {S8} branch to '{S8}' requires a binding-invariant target defined in the same section"),
                    mnemonic, symbol.name));
            return;
        }
        if (!width || !symbol.defined || symbol.section != relocation.section || replaceable)
        {
            // A PC-relative data field can follow an opcode-valued byte. Only
            // instruction encoding proves that this reference is a branch.
            relocation.plt = elf && (relocation.plt || (relocation.x86_branch && !symbol.hidden && (symbol.global || !symbol.defined)));
            builder->relocation_lines[kept] = builder->relocation_lines[index];
            builder->relocation_columns[kept] = builder->relocation_columns[index];
            builder->result.relocations[kept++] = relocation;
            continue;
        }
        s64 value = (s64)symbol.value + relocation.addend - (s64)relocation.offset;
        if ((width == 1 && (value < INT8_MIN || value > INT8_MAX)) || (width == 2 && (value < INT16_MIN || value > INT16_MAX)) ||
            (width == 4 && (value < INT32_MIN || value > INT32_MAX)))
        {
            builder->line = builder->relocation_lines[index];
            builder->column = builder->relocation_columns[index];
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
    u32 statement_count = 1;
    u32 comma_count = 0;
    for (u64 index = 0; index < source.length; index += 1)
    {
        text[index] = source.pointer[index];
        statement_count += source.pointer[index] == '\n';
        comma_count += source.pointer[index] == ',';
    }
    char8 quote = 0;
    for (u64 index = 0; index < source.length;)
    {
        char8 value = source.pointer[index];
        if (value == '\n')
        {
            quote = 0;
            index += 1;
            continue;
        }
        if (quote)
        {
            if (value == '\\' && index + 1 < source.length && source.pointer[index + 1] != '\n')
            {
                index += 2;
            }
            else
            {
                if (value == quote) quote = 0;
                index += 1;
            }
            continue;
        }
        if (value == '"' || value == '\'')
        {
            quote = value;
            index += 1;
            continue;
        }
        if (assembly_unit_line_comment(options.target, source, index))
        {
            while (index < source.length && source.pointer[index] != '\n') index += 1;
            continue;
        }
        u32 separator_length = assembly_unit_separator_length(options.target, source, index);
        if (separator_length)
        {
            statement_count += 1;
            index += separator_length;
            continue;
        }
        // The scan reads `source` rather than `text`: `text` is being blanked
        // as it goes, so a closing `*/` looked for there would never be found
        // and the whole file after the first comment would disappear.
        if (value != '/' || index + 1 >= source.length || source.pointer[index + 1] != '*')
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

    builder.symbol_capacity = statement_count * 2 + comma_count + ASSEMBLY_UNIT_SECTION_CAPACITY;
    builder.relocation_capacity = statement_count + comma_count + 16;
    builder.integer_capacity = statement_count + comma_count + 16;
    builder.piece_capacity = statement_count + 16;
    builder.numeric_label_capacity = statement_count * 2 + 16;
    builder.result.sections = arena_allocate(arena, AssemblyUnitSection, ASSEMBLY_UNIT_SECTION_CAPACITY);
    builder.section_offsets = arena_allocate(arena, u64, ASSEMBLY_UNIT_SECTION_CAPACITY);
    builder.result.symbols = arena_allocate(arena, AssemblyUnitSymbol, builder.symbol_capacity);
    builder.result.relocations = arena_allocate(arena, AssemblyUnitRelocation, builder.relocation_capacity);
    builder.integers = arena_allocate(arena, AssemblyUnitInteger, builder.integer_capacity);
    builder.relocation_lines = arena_allocate(arena, u32, builder.relocation_capacity);
    builder.relocation_columns = arena_allocate(arena, u32, builder.relocation_capacity);
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
