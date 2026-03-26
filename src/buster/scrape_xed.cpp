#pragma once

// Scrape Intel x86_64 instruction set data from Intel XED source files.
//
// Parses the XED datafiles to extract instruction definitions, encoding
// patterns, operand specifications, and register info -- everything needed
// to build an encoder/decoder for a compiler backend.
//
// Usage:
//   ./scrape_xed /path/to/xed
//   ./scrape_xed /path/to/xed --stats
//   ./scrape_xed /path/to/xed --dump MOV
//   ./scrape_xed /path/to/xed --filter-extension BASE
//   ./scrape_xed /path/to/xed --list-extensions
//   ./scrape_xed /path/to/xed --list-categories
//   ./scrape_xed /path/to/xed --list-iclasses
//   ./scrape_xed /path/to/xed --generate

#include <buster/base.h>
#include <buster/system_headers.h>
#include <buster/arena.h>
#include <buster/file.h>
#include <buster/path.h>
#include <buster/entry_point.h>
#include <buster/arguments.h>
#include <buster/string.h>

#include <dirent.h>
#include <sys/stat.h>

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.cpp>
#include <buster/target.cpp>
#if defined(__x86_64__)
#include <buster/x86_64.cpp>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.cpp>
#endif
#include <buster/assertion.cpp>
#include <buster/memory.cpp>
#include <buster/os.cpp>
#include <buster/string.cpp>
#include <buster/arena.cpp>
#include <buster/integer.cpp>
#include <buster/file.cpp>
#include <buster/path.cpp>
#include <buster/arguments.cpp>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.cpp>
#endif
#endif

// ---------------------------------------------------------------------------
// Tokenizer: split on whitespace or a specific delimiter
// ---------------------------------------------------------------------------

STRUCT(ScrapeTokenizer)
{
    String8 remaining;
};

BUSTER_GLOBAL_LOCAL String8 scrape_tokenizer_next_whitespace(ScrapeTokenizer* tokenizer)
{
    String8 remaining = tokenizer->remaining;
    while (remaining.length > 0 && (remaining.pointer[0] == ' ' || remaining.pointer[0] == '\t'))
    {
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    String8 result = { .pointer = remaining.pointer, .length = 0 };
    while (remaining.length > 0 && remaining.pointer[0] != ' ' && remaining.pointer[0] != '\t')
    {
        result.length += 1;
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    tokenizer->remaining = remaining;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_str_trim(String8 string)
{
    while (string.length > 0 && (string.pointer[0] == ' ' || string.pointer[0] == '\t' || string.pointer[0] == '\r' || string.pointer[0] == '\n'))
    {
        string.pointer += 1;
        string.length -= 1;
    }
    while (string.length > 0)
    {
        char8 last = string.pointer[string.length - 1];
        if (last == ' ' || last == '\t' || last == '\r' || last == '\n')
        {
            string.length -= 1;
        }
        else
        {
            break;
        }
    }
    return string;
}

BUSTER_GLOBAL_LOCAL String8 scrape_tokenizer_next_delimiter(ScrapeTokenizer* tokenizer, char8 delimiter)
{
    String8 remaining = tokenizer->remaining;
    String8 result = { .pointer = remaining.pointer, .length = 0 };

    while (remaining.length > 0 && remaining.pointer[0] != delimiter)
    {
        result.length += 1;
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    // Skip the delimiter
    if (remaining.length > 0)
    {
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    tokenizer->remaining = remaining;
    return scrape_str_trim(result);
}

// ---------------------------------------------------------------------------
// Strip comments
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL String8 scrape_str_strip_comment(String8 string)
{
    for (u64 i = 0; i < string.length; i += 1)
    {
        if (string.pointer[i] == '#')
        {
            string.length = i;
            break;
        }
    }
    return scrape_str_trim(string);
}

// ---------------------------------------------------------------------------
// Parse a hex byte like "0xC6"
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL bool scrape_parse_hex_byte(String8 token, u8* out)
{
    bool result = token.length == 4 && token.pointer[0] == '0' && (token.pointer[1] == 'x' || token.pointer[1] == 'X');
    if (result)
    {
        u8 value = 0;
        for (u64 i = 2; i < 4; i += 1)
        {
            char8 ch = token.pointer[i];
            u8 digit = 0;
            if (ch >= '0' && ch <= '9') digit = (u8)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') digit = (u8)(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') digit = (u8)(ch - 'A' + 10);
            else { result = false; break; }
            value = (u8)(value * 16 + digit);
        }
        if (result) *out = value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_parse_binary_opcode(String8 token, u8* out_value, u8* out_mask)
{
    bool result = token.length >= 3 && token.pointer[0] == '0' && (token.pointer[1] == 'b' || token.pointer[1] == 'B');
    u8 value = 0;
    u8 mask = 0;
    u8 bit_count = 0;
    if (result)
    {
        for (u64 i = 2; i < token.length; i += 1)
        {
            char8 ch = token.pointer[i];
            if (ch == '_')
            {
                continue;
            }

            if (bit_count >= 8)
            {
                result = false;
                break;
            }

            value <<= 1;
            mask <<= 1;
            bit_count += 1;

            if (ch == '0' || ch == '1')
            {
                mask |= 1;
                if (ch == '1')
                {
                    value |= 1;
                }
            }
            else if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
            {
                result = false;
                break;
            }
        }

        if (result && bit_count > 0 && bit_count < 8)
        {
            u8 shift = (u8)(8 - bit_count);
            value = (u8)(value << shift);
            mask = (u8)(mask << shift);
        }
        else if (bit_count == 0)
        {
            result = false;
        }
    }

    if (result)
    {
        *out_value = value;
        *out_mask = mask;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Parse binary like "0b000"
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL u8 scrape_parse_binary_string(String8 token)
{
    u8 result = 0;
    for (u64 i = 0; i < token.length; i += 1)
    {
        if (token.pointer[i] == '0' || token.pointer[i] == '1')
        {
            result = (u8)(result * 2 + (token.pointer[i] - '0'));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

#define SCRAPE_MAX_OPERAND_COUNT 8
#define SCRAPE_MAX_OPCODE_BYTE_COUNT 4
#define SCRAPE_MAX_ATTRIBUTE_COUNT 16

STRUCT(ScrapeOperand)
{
    String8 name;
    String8 register_name;
    String8 read_write;
    String8 visibility;
    String8 width;
    String8 element_type;
};

STRUCT(ScrapeInstructionForm)
{
    String8 iclass;
    String8 uname;
    String8 category;
    String8 extension;
    String8 isa_set;
    String8 cpl;
    String8 flags;
    String8 pattern;
    String8 iform;
    String8 exceptions;
    String8 prefix_type;        // "", "VEX", "EVEX", "XOP"
    String8 vex_pp;             // V66, VF2, VF3, VNP
    String8 vex_map;            // V0F, V0F38, V0F3A
    String8 vector_length;      // VL128, VL256, VL512
    String8 rex_w;              // W0, W1
    String8 attributes[SCRAPE_MAX_ATTRIBUTE_COUNT];
    ScrapeOperand operands[SCRAPE_MAX_OPERAND_COUNT];

    // Derived from pattern
    u8 opcode_bytes[SCRAPE_MAX_OPCODE_BYTE_COUNT];
    u8 opcode_masks[SCRAPE_MAX_OPCODE_BYTE_COUNT];
    u32 opcode_byte_count;
    u32 attribute_count;
    u32 operand_count;
    bool has_modrm;
    s8 modrm_reg_value;         // -1 if not fixed
    u8 reserved[2];
};

STRUCT(ScrapeRegister)
{
    String8 name;
    String8 register_class;
    String8 enclosing_64;
    String8 enclosing_32;
    u32 width;
    s32 register_id;
    bool is_high_byte;
    u8 reserved[7];
};

STRUCT(ScrapeOperandWidth)
{
    String8 name;
    String8 element_type;
    u32 width_16;
    u32 width_32;
    u32 width_64;
    u8 reserved[4];
};

STRUCT(ScrapeInstructionDatabase)
{
    ScrapeInstructionForm* forms;
    u64 form_count;
    u64 form_capacity;

    ScrapeRegister* registers;
    u64 register_count;
    u64 register_capacity;

    ScrapeOperandWidth* operand_widths;
    u64 operand_width_count;
    u64 operand_width_capacity;
};

// ---------------------------------------------------------------------------
// Case-insensitive comparison
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL bool scrape_str_equal_case_insensitive(String8 a, String8 b)
{
    bool result = a.length == b.length;
    if (result)
    {
        for (u64 i = 0; i < a.length; i += 1)
        {
            char8 ca = a.pointer[i];
            char8 cb = b.pointer[i];
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb)
            {
                result = false;
                break;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Line iterator
// ---------------------------------------------------------------------------

STRUCT(ScrapeLineIterator)
{
    String8 remaining;
    bool in_legal_block;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL String8 scrape_line_iterator_next(ScrapeLineIterator* iterator)
{
    String8 remaining = iterator->remaining;
    String8 result = { .pointer = remaining.pointer, .length = 0 };

    while (remaining.length > 0 && remaining.pointer[0] != '\n')
    {
        result.length += 1;
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    // Skip the newline
    if (remaining.length > 0)
    {
        remaining.pointer += 1;
        remaining.length -= 1;
    }

    iterator->remaining = remaining;

    // Handle legal block skipping
    let trimmed = scrape_str_trim(result);
    if (string8_equal(trimmed, S8("#BEGIN_LEGAL")))
    {
        iterator->in_legal_block = true;
        return scrape_line_iterator_next(iterator);
    }
    if (string8_equal(trimmed, S8("#END_LEGAL")))
    {
        iterator->in_legal_block = false;
        return scrape_line_iterator_next(iterator);
    }
    if (iterator->in_legal_block)
    {
        return scrape_line_iterator_next(iterator);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_line_iterator_has_more(ScrapeLineIterator* iterator)
{
    return iterator->remaining.length > 0;
}

// ---------------------------------------------------------------------------
// Parse a single operand token
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL ScrapeOperand scrape_parse_operand(Arena* arena, String8 token)
{
    ScrapeOperand result = {};

    ScrapeTokenizer colon_tokenizer = { .remaining = token };
    String8 head = scrape_tokenizer_next_delimiter(&colon_tokenizer, ':');

    // Split name=register
    bool found_equals = false;
    for (u64 i = 0; i < head.length; i += 1)
    {
        if (head.pointer[i] == '=')
        {
            String8 name_part = { .pointer = head.pointer, .length = i };
            String8 register_part = { .pointer = head.pointer + i + 1, .length = head.length - i - 1 };
            result.name = string8_duplicate_arena(arena, name_part, true);
            result.register_name = string8_duplicate_arena(arena, register_part, true);
            found_equals = true;
            break;
        }
    }

    if (!found_equals)
    {
        result.name = string8_duplicate_arena(arena, head, true);
    }

    // Parse remaining colon-separated parts: rw, visibility, width/xtype
    while (colon_tokenizer.remaining.length > 0)
    {
        String8 part = scrape_tokenizer_next_delimiter(&colon_tokenizer, ':');
        if (part.length == 0) continue;

        if (string8_equal(part, S8("r")) || string8_equal(part, S8("w")) || string8_equal(part, S8("rw")) ||
            string8_equal(part, S8("cr")) || string8_equal(part, S8("cw")) || string8_equal(part, S8("rcw")) ||
            string8_equal(part, S8("crw")))
        {
            result.read_write = string8_duplicate_arena(arena, part, true);
        }
        else if (string8_equal(part, S8("SUPP")) || string8_equal(part, S8("SUPPRESSED")))
        {
            result.visibility = S8("SUPPRESSED");
        }
        else if (string8_equal(part, S8("IMPL")) || string8_equal(part, S8("IMPLICIT")))
        {
            result.visibility = S8("IMPLICIT");
        }
        else if (string8_equal(part, S8("EXPL")) || string8_equal(part, S8("EXPLICIT")))
        {
            result.visibility = S8("EXPLICIT");
        }
        else if (string8_equal(part, S8("TXT")))
        {
            result.visibility = S8("ECOND");
        }
        else
        {
            if (result.width.length == 0)
            {
                result.width = string8_duplicate_arena(arena, part, true);
            }
            else
            {
                result.element_type = string8_duplicate_arena(arena, part, true);
            }
        }
    }

    if (result.visibility.length == 0)
    {
        result.visibility = S8("EXPLICIT");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Analyze encoding pattern
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void scrape_analyze_pattern(ScrapeInstructionForm* form)
{
    ScrapeTokenizer tokenizer = { .remaining = form->pattern };
    form->modrm_reg_value = -1;

    while (tokenizer.remaining.length > 0)
    {
        String8 token = scrape_tokenizer_next_whitespace(&tokenizer);
        if (token.length == 0) continue;

        u8 byte_value = 0;
        if (scrape_parse_hex_byte(token, &byte_value))
        {
            if (form->opcode_byte_count < SCRAPE_MAX_OPCODE_BYTE_COUNT)
            {
                form->opcode_bytes[form->opcode_byte_count] = byte_value;
                form->opcode_masks[form->opcode_byte_count] = 0xff;
                form->opcode_byte_count += 1;
            }
        }
        else
        {
            u8 opcode_mask = 0;
            if (scrape_parse_binary_opcode(token, &byte_value, &opcode_mask))
            {
                if (form->opcode_byte_count < SCRAPE_MAX_OPCODE_BYTE_COUNT)
                {
                    form->opcode_bytes[form->opcode_byte_count] = byte_value;
                    form->opcode_masks[form->opcode_byte_count] = opcode_mask;
                    form->opcode_byte_count += 1;
                }
            }
            else if (string8_equal(token, S8("MODRM()")) || string8_starts_with_sequence(token, S8("MOD[")))
            {
                form->has_modrm = true;
            }
            else if (string8_starts_with_sequence(token, S8("REG[0b")))
            {
                // Extract binary value from REG[0bNNN]
                String8 binary_part = { .pointer = token.pointer + 6, .length = 0 };
                while (binary_part.pointer + binary_part.length < token.pointer + token.length &&
                       binary_part.pointer[binary_part.length] != ']')
                {
                    binary_part.length += 1;
                }
                form->modrm_reg_value = (s8)scrape_parse_binary_string(binary_part);
            }
            else if (string8_equal(token, S8("VV1")) || string8_equal(token, S8("VV0")))
            {
                form->prefix_type = S8("VEX");
            }
            else if (string8_equal(token, S8("EVV")))
            {
                form->prefix_type = S8("EVEX");
            }
            else if (string8_equal(token, S8("XOPV")))
            {
                form->prefix_type = S8("XOP");
            }
            else if (string8_equal(token, S8("V66")) || string8_equal(token, S8("VF2")) ||
                     string8_equal(token, S8("VF3")) || string8_equal(token, S8("VNP")))
            {
                form->vex_pp = token;
            }
            else if (string8_equal(token, S8("V0F")) || string8_equal(token, S8("V0F38")) ||
                     string8_equal(token, S8("V0F3A")))
            {
                form->vex_map = token;
            }
            else if (string8_equal(token, S8("VL128")) || string8_equal(token, S8("VL256")) ||
                     string8_equal(token, S8("VL512")))
            {
                form->vector_length = token;
            }
            else if (string8_equal(token, S8("W0")) || string8_equal(token, S8("REXW=0")))
            {
                form->rex_w = S8("W0");
            }
            else if (string8_equal(token, S8("W1")) || string8_equal(token, S8("REXW=1")))
            {
                form->rex_w = S8("W1");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Parse instruction definition file
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void scrape_parse_instruction_file(Arena* arena, ScrapeInstructionDatabase* database, StringOs path)
{
    let content = file_read(arena, path, (FileReadOptions){});
    String8 text = BYTE_SLICE_TO_STRING(8, content);
    if (text.length == 0) return;

    ScrapeLineIterator lines = { .remaining = text };
    bool in_block = false;
    ScrapeInstructionForm current = {};

    while (scrape_line_iterator_has_more(&lines))
    {
        String8 line = scrape_line_iterator_next(&lines);
        String8 trimmed = scrape_str_trim(line);

        if (trimmed.length == 0 || trimmed.pointer[0] == '#') continue;
        if (string8_starts_with_sequence(trimmed, S8("INSTRUCTIONS()"))) continue;

        if (string8_equal(trimmed, S8("{")))
        {
            in_block = true;
            memset(&current, 0, sizeof(current));
            current.modrm_reg_value = -1;
            continue;
        }

        if (string8_equal(trimmed, S8("}")))
        {
            if (in_block && current.iclass.length > 0)
            {
                if (database->form_count >= database->form_capacity)
                {
                    u64 new_capacity = database->form_capacity * 2;
                    ScrapeInstructionForm* new_forms = arena_allocate(arena, ScrapeInstructionForm, new_capacity);
                    memcpy(new_forms, database->forms, sizeof(ScrapeInstructionForm) * database->form_count);
                    database->forms = new_forms;
                    database->form_capacity = new_capacity;
                }
                database->forms[database->form_count] = current;
                database->form_count += 1;
            }
            in_block = false;
            continue;
        }

        if (!in_block) continue;

        // Strip inline comments
        trimmed = scrape_str_strip_comment(trimmed);
        if (trimmed.length == 0) continue;

        // Find first colon to split key:value
        u64 colon_index = BUSTER_STRING_NO_MATCH;
        for (u64 i = 0; i < trimmed.length; i += 1)
        {
            if (trimmed.pointer[i] == ':')
            {
                colon_index = i;
                break;
            }
        }
        if (colon_index == BUSTER_STRING_NO_MATCH) continue;

        String8 key = scrape_str_trim(string8_from_pointer_length(trimmed.pointer, colon_index));
        String8 value = scrape_str_trim(string8_from_pointer_length(trimmed.pointer + colon_index + 1, trimmed.length - colon_index - 1));

        if (string8_equal(key, S8("ICLASS")))
        {
            current.iclass = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("UNAME")))
        {
            current.uname = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("CPL")))
        {
            current.cpl = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("CATEGORY")))
        {
            current.category = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("EXTENSION")))
        {
            current.extension = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("ISA_SET")))
        {
            current.isa_set = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("EXCEPTIONS")))
        {
            current.exceptions = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("FLAGS")))
        {
            current.flags = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("IFORM")))
        {
            current.iform = string8_duplicate_arena(arena, value, true);
        }
        else if (string8_equal(key, S8("ATTRIBUTES")))
        {
            ScrapeTokenizer attribute_tokenizer = { .remaining = value };
            while (attribute_tokenizer.remaining.length > 0 && current.attribute_count < SCRAPE_MAX_ATTRIBUTE_COUNT)
            {
                String8 attribute = scrape_tokenizer_next_whitespace(&attribute_tokenizer);
                if (attribute.length > 0)
                {
                    current.attributes[current.attribute_count] = string8_duplicate_arena(arena, attribute, true);
                    current.attribute_count += 1;
                }
            }
        }
        else if (string8_equal(key, S8("PATTERN")))
        {
            current.pattern = string8_duplicate_arena(arena, value, true);
            scrape_analyze_pattern(&current);
        }
        else if (string8_equal(key, S8("OPERANDS")))
        {
            ScrapeTokenizer operand_tokenizer = { .remaining = value };
            while (operand_tokenizer.remaining.length > 0 && current.operand_count < SCRAPE_MAX_OPERAND_COUNT)
            {
                String8 operand_token = scrape_tokenizer_next_whitespace(&operand_tokenizer);
                if (operand_token.length > 0)
                {
                    current.operands[current.operand_count] = scrape_parse_operand(arena, operand_token);
                    current.operand_count += 1;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Parse registers
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL u32 scrape_parse_u32_decimal(String8 s)
{
    u32 result = 0;
    for (u64 i = 0; i < s.length; i += 1)
    {
        char8 ch = s.pointer[i];
        if (ch < '0' || ch > '9') break;
        result = result * 10 + (u32)(ch - '0');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 scrape_parse_s32_decimal(String8 s)
{
    if (s.length == 0) return -1;
    bool negative = false;
    u64 start = 0;
    if (s.pointer[0] == '-')
    {
        negative = true;
        start = 1;
    }
    s32 result = 0;
    for (u64 i = start; i < s.length; i += 1)
    {
        char8 ch = s.pointer[i];
        if (ch < '0' || ch > '9') break;
        result = result * 10 + (s32)(ch - '0');
    }
    return negative ? -result : result;
}

BUSTER_GLOBAL_LOCAL void scrape_parse_registers(Arena* arena, ScrapeInstructionDatabase* database, StringOs path)
{
    let content = file_read(arena, path, (FileReadOptions){});
    String8 text = BYTE_SLICE_TO_STRING(8, content);
    if (text.length == 0) return;

    ScrapeLineIterator lines = { .remaining = text };

    while (scrape_line_iterator_has_more(&lines))
    {
        String8 line = scrape_line_iterator_next(&lines);
        String8 stripped = scrape_str_strip_comment(line);
        if (stripped.length == 0) continue;

        ScrapeTokenizer tokenizer = { .remaining = stripped };
        String8 name = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 register_class = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 width_str = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 enclosing = scrape_tokenizer_next_whitespace(&tokenizer);

        if (name.length == 0 || register_class.length == 0 || width_str.length == 0 || enclosing.length == 0)
        {
            continue;
        }

        u32 width = scrape_parse_u32_decimal(width_str);
        if (width == 0 && width_str.pointer[0] != '0') continue;

        if (database->register_count >= database->register_capacity)
        {
            u64 new_capacity = database->register_capacity * 2;
            ScrapeRegister* new_registers = arena_allocate(arena, ScrapeRegister, new_capacity);
            memcpy(new_registers, database->registers, sizeof(ScrapeRegister) * database->register_count);
            database->registers = new_registers;
            database->register_capacity = new_capacity;
        }

        ScrapeRegister* entry = &database->registers[database->register_count];
        entry->name = string8_duplicate_arena(arena, name, true);
        entry->register_class = string8_duplicate_arena(arena, register_class, true);
        entry->width = width;

        // Split enclosing: "RAX/EAX" or just "RAX"
        bool found_slash = false;
        for (u64 i = 0; i < enclosing.length; i += 1)
        {
            if (enclosing.pointer[i] == '/')
            {
                String8 enc64 = { .pointer = enclosing.pointer, .length = i };
                String8 enc32 = { .pointer = enclosing.pointer + i + 1, .length = enclosing.length - i - 1 };
                entry->enclosing_64 = string8_duplicate_arena(arena, enc64, true);
                entry->enclosing_32 = string8_duplicate_arena(arena, enc32, true);
                found_slash = true;
                break;
            }
        }
        if (!found_slash)
        {
            entry->enclosing_64 = string8_duplicate_arena(arena, enclosing, true);
            entry->enclosing_32 = string8_duplicate_arena(arena, enclosing, true);
        }

        String8 register_id_str = scrape_tokenizer_next_whitespace(&tokenizer);
        entry->register_id = register_id_str.length > 0 ? scrape_parse_s32_decimal(register_id_str) : -1;

        String8 high_byte_marker = scrape_tokenizer_next_whitespace(&tokenizer);
        entry->is_high_byte = string8_equal(high_byte_marker, S8("h"));

        database->register_count += 1;
    }
}

// ---------------------------------------------------------------------------
// Parse operand widths
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void scrape_parse_operand_widths(Arena* arena, ScrapeInstructionDatabase* database, StringOs path)
{
    let content = file_read(arena, path, (FileReadOptions){});
    String8 text = BYTE_SLICE_TO_STRING(8, content);
    if (text.length == 0) return;

    ScrapeLineIterator lines = { .remaining = text };

    while (scrape_line_iterator_has_more(&lines))
    {
        String8 line = scrape_line_iterator_next(&lines);
        String8 stripped = scrape_str_strip_comment(line);
        if (stripped.length == 0) continue;

        ScrapeTokenizer tokenizer = { .remaining = stripped };
        String8 name = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 element_type = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 width1_str = scrape_tokenizer_next_whitespace(&tokenizer);

        if (name.length == 0 || element_type.length == 0 || width1_str.length == 0) continue;

        u32 width1 = scrape_parse_u32_decimal(width1_str);
        if (width1 == 0 && (width1_str.length == 0 || width1_str.pointer[0] != '0')) continue;

        if (database->operand_width_count >= database->operand_width_capacity)
        {
            u64 new_capacity = database->operand_width_capacity * 2;
            ScrapeOperandWidth* new_widths = arena_allocate(arena, ScrapeOperandWidth, new_capacity);
            memcpy(new_widths, database->operand_widths, sizeof(ScrapeOperandWidth) * database->operand_width_count);
            database->operand_widths = new_widths;
            database->operand_width_capacity = new_capacity;
        }

        ScrapeOperandWidth* entry = &database->operand_widths[database->operand_width_count];
        entry->name = string8_duplicate_arena(arena, name, true);
        entry->element_type = string8_duplicate_arena(arena, element_type, true);

        String8 width2_str = scrape_tokenizer_next_whitespace(&tokenizer);
        String8 width3_str = scrape_tokenizer_next_whitespace(&tokenizer);

        if (width2_str.length > 0 && width3_str.length > 0)
        {
            entry->width_16 = width1;
            entry->width_32 = scrape_parse_u32_decimal(width2_str);
            entry->width_64 = scrape_parse_u32_decimal(width3_str);
        }
        else if (width2_str.length > 0)
        {
            entry->width_16 = width1;
            entry->width_32 = scrape_parse_u32_decimal(width2_str);
            entry->width_64 = entry->width_32;
        }
        else
        {
            entry->width_16 = width1;
            entry->width_32 = width1;
            entry->width_64 = width1;
        }

        database->operand_width_count += 1;
    }
}

// ---------------------------------------------------------------------------
// Recursively find instruction files
// ---------------------------------------------------------------------------

#define SCRAPE_MAX_FILE_COUNT 512

STRUCT(ScrapeFileList)
{
    StringOs* paths;
    u64 count;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL void scrape_find_instruction_files_recursive(Arena* arena, StringOs directory, ScrapeFileList* list)
{
    // null-terminate for opendir
    char* dir_cstr = (char*)directory.pointer;

    DIR* dir = opendir(dir_cstr);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;

        String8 entry_name = string8_from_pointer((char8*)entry->d_name);
        String8 parts[] = { directory, S8("/"), entry_name };
        String8 full_path = string8_join_arena(arena, BUSTER_ARRAY_TO_SLICE(parts), true);

        struct stat statbuf;
        if (stat((char*)full_path.pointer, &statbuf) != 0) continue;

        if (S_ISDIR(statbuf.st_mode))
        {
            scrape_find_instruction_files_recursive(arena, full_path, list);
        }
        else if (S_ISREG(statbuf.st_mode))
        {
            bool is_instruction_file = string8_ends_with_sequence(entry_name, S8("-isa.xed.txt")) ||
                                       string8_ends_with_sequence(entry_name, S8("-isa.txt")) ||
                                       string8_equal(entry_name, S8("xed-isa.txt"));
            if (is_instruction_file && list->count < list->capacity)
            {
                list->paths[list->count] = full_path;
                list->count += 1;
            }
        }
    }

    closedir(dir);
}

// ---------------------------------------------------------------------------
// Print helpers
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void scrape_print_instruction_form(ScrapeInstructionForm* form, u32 index)
{
    string8_print(S8("  Form {u32}:\n"), index);
    string8_print(S8("    ISA set:    {S8}\n"), form->isa_set);
    string8_print(S8("    Extension:  {S8}\n"), form->extension);
    string8_print(S8("    Category:   {S8}\n"), form->category);
    if (form->iform.length > 0) string8_print(S8("    IFORM:      {S8}\n"), form->iform);
    string8_print(S8("    Pattern:    {S8}\n"), form->pattern);

    string8_print(S8("    Opcode:    "));
    for (u32 i = 0; i < form->opcode_byte_count; i += 1)
    {
        string8_print(S8(" 0x{u8:x,width=[0,2],no_prefix}"), form->opcode_bytes[i]);
    }
    string8_print(S8("\n"));

    if (form->has_modrm)
    {
        string8_print(S8("    ModRM:      yes"));
        if (form->modrm_reg_value >= 0)
        {
            string8_print(S8(" (REG=/{s32})"), form->modrm_reg_value);
        }
        string8_print(S8("\n"));
    }

    if (form->prefix_type.length > 0)   string8_print(S8("    Prefix:     {S8}\n"), form->prefix_type);
    if (form->vex_pp.length > 0)        string8_print(S8("    VEX.pp:     {S8}\n"), form->vex_pp);
    if (form->vex_map.length > 0)       string8_print(S8("    VEX.map:    {S8}\n"), form->vex_map);
    if (form->vector_length.length > 0)  string8_print(S8("    VL:         {S8}\n"), form->vector_length);
    if (form->rex_w.length > 0)         string8_print(S8("    REX.W:      {S8}\n"), form->rex_w);

    if (form->attribute_count > 0)
    {
        string8_print(S8("    Attributes:"));
        for (u32 i = 0; i < form->attribute_count; i += 1)
        {
            string8_print(S8(" {S8}"), form->attributes[i]);
        }
        string8_print(S8("\n"));
    }

    string8_print(S8("    Operands:\n"));
    for (u32 i = 0; i < form->operand_count; i += 1)
    {
        ScrapeOperand* operand = &form->operands[i];
        string8_print(S8("      {S8}"), operand->name);
        if (operand->register_name.length > 0) string8_print(S8(" = {S8}"), operand->register_name);
        if (operand->read_write.length > 0)    string8_print(S8(" [{S8}]"), operand->read_write);
        if (!string8_equal(operand->visibility, S8("EXPLICIT"))) string8_print(S8(" ({S8})"), operand->visibility);
        if (operand->width.length > 0)         string8_print(S8(" width={S8}"), operand->width);
        string8_print(S8("\n"));
    }

    string8_print(S8("\n"));
}

// ---------------------------------------------------------------------------
// Count pair for statistics
// ---------------------------------------------------------------------------

STRUCT(ScrapeCountPair)
{
    String8 name;
    u64 count;
};

STRUCT(ScrapeStringTable)
{
    String8* values;
    u64 count;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL s64 scrape_string_table_find_index(ScrapeStringTable* table, String8 value)
{
    s64 result = -1;
    for (u64 i = 0; i < table->count; i += 1)
    {
        if (string8_equal(table->values[i], value))
        {
            result = (s64)i;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 scrape_string_table_intern(ScrapeStringTable* table, String8 value)
{
    String8 normalized = value;
    if (normalized.length == 0 ||
        scrape_str_equal_case_insensitive(normalized, S8("INVALID")) ||
        scrape_str_equal_case_insensitive(normalized, S8("EMPTY")) ||
        scrape_str_equal_case_insensitive(normalized, S8("N/A")))
    {
        normalized = S8("NONE");
    }

    s64 existing = scrape_string_table_find_index(table, normalized);
    u64 result = 0;

    if (existing >= 0)
    {
        result = (u64)existing;
    }
    else
    {
        if (table->count < table->capacity)
        {
            table->values[table->count] = normalized;
            table->count += 1;
            result = table->count - 1;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u64 scrape_string_table_enum_index(ScrapeStringTable* table, String8 value)
{
    String8 normalized = value;
    if (normalized.length == 0 ||
        scrape_str_equal_case_insensitive(normalized, S8("INVALID")) ||
        scrape_str_equal_case_insensitive(normalized, S8("EMPTY")) ||
        scrape_str_equal_case_insensitive(normalized, S8("N/A")))
    {
        normalized = S8("NONE");
    }

    s64 index = scrape_string_table_find_index(table, normalized);
    u64 result = table->count;
    if (index >= 0)
    {
        result = (u64)index;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_enum_token_from_value(Arena* arena, String8 value)
{
    String8 result = { 0 };
    u64 capacity = value.length * 2 + 16;
    char8* buffer = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    bool previous_is_underscore = false;

    for (u64 i = 0; i < value.length; i += 1)
    {
        char8 ch = value.pointer[i];
        bool is_letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool is_digit = (ch >= '0' && ch <= '9');

        if (is_letter || is_digit)
        {
            if (ch >= 'a' && ch <= 'z')
            {
                ch = (char8)(ch - ('a' - 'A'));
            }
            buffer[length] = ch;
            length += 1;
            previous_is_underscore = false;
        }
        else
        {
            if (!previous_is_underscore && length > 0)
            {
                buffer[length] = '_';
                length += 1;
                previous_is_underscore = true;
            }
        }
    }

    while (length > 0 && buffer[length - 1] == '_')
    {
        length -= 1;
    }

    if (length == 0)
    {
        String8 fallback = S8("EMPTY");
        for (u64 i = 0; i < fallback.length; i += 1)
        {
            buffer[i] = fallback.pointer[i];
        }
        length = fallback.length;
    }

    result = string8_from_pointer_length(buffer, length);
    return result;
}

BUSTER_GLOBAL_LOCAL bool scrape_enum_token_in_use(String8* tokens, u64 token_count, String8 token)
{
    bool result = false;
    for (u64 i = 0; i < token_count; i += 1)
    {
        if (string8_equal(tokens[i], token))
        {
            result = true;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_string8_to_lower_arena(Arena* arena, String8 value)
{
    char8* data = arena_allocate(arena, char8, value.length);
    for (u64 i = 0; i < value.length; i += 1)
    {
        char8 ch = value.pointer[i];
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = (char8)(ch + ('a' - 'A'));
        }
        data[i] = ch;
    }
    return string8_from_pointer_length(data, value.length);
}

BUSTER_GLOBAL_LOCAL void scrape_build_enum_tokens(Arena* arena, ScrapeStringTable* table, String8* tokens)
{
    for (u64 i = 0; i < table->count; i += 1)
    {
        String8 base = scrape_enum_token_from_value(arena, table->values[i]);
        if (string8_equal(base, S8("COUNT")))
        {
            String8 parts[] = { base, S8("_VALUE") };
            base = string8_join_arena(arena, BUSTER_ARRAY_TO_SLICE(parts), true);
        }

        String8 candidate = base;
        u64 suffix = 2;
        while (scrape_enum_token_in_use(tokens, i, candidate))
        {
            candidate = string8_format_z(arena, S8("{S8}_{u64}"), base, suffix);
            suffix += 1;
        }

        tokens[i] = candidate;
    }
}

BUSTER_GLOBAL_LOCAL String8 scrape_operand_kind_string(ScrapeOperand* operand)
{
    String8 result = S8("SPECIAL");
    if (operand->register_name.length > 0)
    {
        result = S8("REGISTER");
    }
    else if (string8_starts_with_sequence(operand->name, S8("MEM")) ||
             string8_starts_with_sequence(operand->name, S8("AGEN")) ||
             string8_starts_with_sequence(operand->name, S8("PTR")))
    {
        result = S8("MEMORY");
    }
    else if (string8_starts_with_sequence(operand->name, S8("IMM")) ||
             string8_starts_with_sequence(operand->name, S8("UIMM")) ||
             string8_starts_with_sequence(operand->name, S8("SIMM")) ||
             string8_first_sequence(operand->name, S8("IMM")) != BUSTER_STRING_NO_MATCH)
    {
        result = S8("IMMEDIATE");
    }
    else if (string8_starts_with_sequence(operand->name, S8("RELBR")) ||
             string8_starts_with_sequence(operand->name, S8("ABSBR")) ||
             string8_first_sequence(operand->name, S8("BRDISP")) != BUSTER_STRING_NO_MATCH)
    {
        result = S8("BRANCH");
    }
    else if (string8_first_sequence(operand->name, S8("FLAGS")) != BUSTER_STRING_NO_MATCH)
    {
        result = S8("FLAGS");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 scrape_operand_action_string(ScrapeOperand* operand)
{
    String8 result = S8("NONE");
    if (string8_equal(operand->read_write, S8("r")) || string8_equal(operand->read_write, S8("cr")))
    {
        result = S8("READ");
    }
    else if (string8_equal(operand->read_write, S8("w")) || string8_equal(operand->read_write, S8("cw")))
    {
        result = S8("WRITE");
    }
    else if (string8_equal(operand->read_write, S8("rw")) || string8_equal(operand->read_write, S8("rcw")) ||
             string8_equal(operand->read_write, S8("crw")))
    {
        result = S8("READ_WRITE");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 scrape_count_unique_insert(ScrapeCountPair* pairs, u64* pair_count, u64 pair_capacity, String8 name)
{
    for (u64 i = 0; i < *pair_count; i += 1)
    {
        if (string8_equal(pairs[i].name, name))
        {
            pairs[i].count += 1;
            return *pair_count;
        }
    }

    if (*pair_count < pair_capacity)
    {
        pairs[*pair_count].name = name;
        pairs[*pair_count].count = 1;
        *pair_count += 1;
    }

    return *pair_count;
}

// Simple insertion sort (replacing qsort for count pairs descending)
BUSTER_GLOBAL_LOCAL void scrape_sort_count_pairs_descending(ScrapeCountPair* pairs, u64 count)
{
    for (u64 i = 1; i < count; i += 1)
    {
        ScrapeCountPair temp = pairs[i];
        u64 j = i;
        while (j > 0 && pairs[j - 1].count < temp.count)
        {
            pairs[j] = pairs[j - 1];
            j -= 1;
        }
        pairs[j] = temp;
    }
}

// ---------------------------------------------------------------------------
// C source generation
// ---------------------------------------------------------------------------

BUSTER_GLOBAL_LOCAL void scrape_generate_c_source(Arena* arena, ScrapeInstructionDatabase* database, StringOs output_path)
{
    String8 parts[4096];
    u64 part_count = 0;
    String8 accumulated = { 0 };

#define SCRAPE_FLUSH_PARTS() \
    do { \
        let chunk = string8_join_arena(arena, { .pointer = parts, .length = part_count }, true); \
        if (accumulated.length == 0) \
        { \
            accumulated = chunk; \
        } \
        else \
        { \
            String8 both[2] = { accumulated, chunk }; \
            accumulated = string8_join_arena(arena, { .pointer = both, .length = 2 }, true); \
        } \
        part_count = 0; \
    } while (0)

#define SCRAPE_FLUSH_IF_NEEDED() \
    do { \
        if (part_count > 3800) { SCRAPE_FLUSH_PARTS(); } \
    } while (0)

    parts[part_count++] = S8("// Auto-generated x86-64 instruction data from Intel XED.\n");
    parts[part_count++] = S8("// Do not edit manually.\n\n");
    parts[part_count++] = S8("#pragma once\n\n");
    parts[part_count++] = S8("#include <buster/base.h>\n");
    parts[part_count++] = S8("#include <buster/string8.h>\n\n");

    ScrapeStringTable iclass_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable iform_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable register_name_table = {
        .values = arena_allocate(arena, String8, database->register_count * 3 + 1),
        .capacity = database->register_count * 3 + 1,
    };
    ScrapeStringTable register_class_table = {
        .values = arena_allocate(arena, String8, database->register_count + 1),
        .capacity = database->register_count + 1,
    };
    ScrapeStringTable operand_visibility_table = {
        .values = arena_allocate(arena, String8, 8),
        .capacity = 8,
    };
    ScrapeStringTable operand_action_table = {
        .values = arena_allocate(arena, String8, 8),
        .capacity = 8,
    };
    ScrapeStringTable operand_kind_table = {
        .values = arena_allocate(arena, String8, 8),
        .capacity = 8,
    };
    ScrapeStringTable operand_register_constraint_table = {
        .values = arena_allocate(arena, String8, database->form_count * SCRAPE_MAX_OPERAND_COUNT + 1),
        .capacity = database->form_count * SCRAPE_MAX_OPERAND_COUNT + 1,
    };
    ScrapeStringTable operand_width_name_table = {
        .values = arena_allocate(arena, String8, database->operand_width_count + 1),
        .capacity = database->operand_width_count + 1,
    };
    ScrapeStringTable operand_element_type_table = {
        .values = arena_allocate(arena, String8, database->operand_width_count + 1),
        .capacity = database->operand_width_count + 1,
    };
    ScrapeStringTable category_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable extension_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable isa_set_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable prefix_type_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable vex_pp_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable vex_map_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable vector_length_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };
    ScrapeStringTable rex_w_table = {
        .values = arena_allocate(arena, String8, database->form_count + 1),
        .capacity = database->form_count + 1,
    };

    for (u64 i = 0; i < database->form_count; i += 1)
    {
        ScrapeInstructionForm* form = &database->forms[i];
        scrape_string_table_intern(&iclass_table, form->iclass);
        scrape_string_table_intern(&iform_table, form->iform);
        scrape_string_table_intern(&category_table, form->category);
        scrape_string_table_intern(&extension_table, form->extension);
        scrape_string_table_intern(&isa_set_table, form->isa_set);
        scrape_string_table_intern(&prefix_type_table, form->prefix_type);
        scrape_string_table_intern(&vex_pp_table, form->vex_pp);
        scrape_string_table_intern(&vex_map_table, form->vex_map);
        scrape_string_table_intern(&vector_length_table, form->vector_length);
        scrape_string_table_intern(&rex_w_table, form->rex_w);
        for (u32 operand_i = 0; operand_i < form->operand_count; operand_i += 1)
        {
            ScrapeOperand* operand = &form->operands[operand_i];
            scrape_string_table_intern(&operand_visibility_table, operand->visibility);
            scrape_string_table_intern(&operand_action_table, scrape_operand_action_string(operand));
            scrape_string_table_intern(&operand_kind_table, scrape_operand_kind_string(operand));
            scrape_string_table_intern(&operand_register_constraint_table, operand->register_name);
        }
    }
    for (u64 i = 0; i < database->register_count; i += 1)
    {
        ScrapeRegister* reg = &database->registers[i];
        scrape_string_table_intern(&register_name_table, reg->name);
        scrape_string_table_intern(&register_name_table, reg->enclosing_64);
        scrape_string_table_intern(&register_name_table, reg->enclosing_32);
        scrape_string_table_intern(&register_class_table, reg->register_class);
    }
    for (u64 i = 0; i < database->operand_width_count; i += 1)
    {
        ScrapeOperandWidth* ow = &database->operand_widths[i];
        scrape_string_table_intern(&operand_width_name_table, ow->name);
        scrape_string_table_intern(&operand_element_type_table, ow->element_type);
    }

    String8* iclass_tokens = arena_allocate(arena, String8, iclass_table.count);
    String8* iform_tokens = arena_allocate(arena, String8, iform_table.count);
    String8* category_tokens = arena_allocate(arena, String8, category_table.count);
    String8* extension_tokens = arena_allocate(arena, String8, extension_table.count);
    String8* isa_set_tokens = arena_allocate(arena, String8, isa_set_table.count);
    String8* prefix_type_tokens = arena_allocate(arena, String8, prefix_type_table.count);
    String8* vex_pp_tokens = arena_allocate(arena, String8, vex_pp_table.count);
    String8* vex_map_tokens = arena_allocate(arena, String8, vex_map_table.count);
    String8* vector_length_tokens = arena_allocate(arena, String8, vector_length_table.count);
    String8* rex_w_tokens = arena_allocate(arena, String8, rex_w_table.count);
    String8* register_name_tokens = arena_allocate(arena, String8, register_name_table.count);
    String8* register_class_tokens = arena_allocate(arena, String8, register_class_table.count);
    String8* operand_visibility_tokens = arena_allocate(arena, String8, operand_visibility_table.count);
    String8* operand_action_tokens = arena_allocate(arena, String8, operand_action_table.count);
    String8* operand_kind_tokens = arena_allocate(arena, String8, operand_kind_table.count);
    String8* operand_register_constraint_tokens = arena_allocate(arena, String8, operand_register_constraint_table.count);
    String8* operand_width_name_tokens = arena_allocate(arena, String8, operand_width_name_table.count);
    String8* operand_element_type_tokens = arena_allocate(arena, String8, operand_element_type_table.count);

    scrape_build_enum_tokens(arena, &iclass_table, iclass_tokens);
    scrape_build_enum_tokens(arena, &iform_table, iform_tokens);
    scrape_build_enum_tokens(arena, &category_table, category_tokens);
    scrape_build_enum_tokens(arena, &extension_table, extension_tokens);
    scrape_build_enum_tokens(arena, &isa_set_table, isa_set_tokens);
    scrape_build_enum_tokens(arena, &prefix_type_table, prefix_type_tokens);
    scrape_build_enum_tokens(arena, &vex_pp_table, vex_pp_tokens);
    scrape_build_enum_tokens(arena, &vex_map_table, vex_map_tokens);
    scrape_build_enum_tokens(arena, &vector_length_table, vector_length_tokens);
    scrape_build_enum_tokens(arena, &rex_w_table, rex_w_tokens);
    scrape_build_enum_tokens(arena, &register_name_table, register_name_tokens);
    scrape_build_enum_tokens(arena, &register_class_table, register_class_tokens);
    scrape_build_enum_tokens(arena, &operand_visibility_table, operand_visibility_tokens);
    scrape_build_enum_tokens(arena, &operand_action_table, operand_action_tokens);
    scrape_build_enum_tokens(arena, &operand_kind_table, operand_kind_tokens);
    scrape_build_enum_tokens(arena, &operand_register_constraint_table, operand_register_constraint_tokens);
    scrape_build_enum_tokens(arena, &operand_width_name_table, operand_width_name_tokens);
    scrape_build_enum_tokens(arena, &operand_element_type_table, operand_element_type_tokens);

#define SCRAPE_EMIT_ENUM_AND_LOOKUPS(type_name, enum_prefix, table, tokens, lookup_name) \
    do { \
        parts[part_count++] = S8("ENUM(" #type_name ",\n"); \
        for (u64 value_i = 0; value_i < (table).count; value_i += 1) \
        { \
            parts[part_count++] = string8_format_z(arena, S8("    " #enum_prefix "_{S8} = {u64},\n"), (tokens)[value_i], value_i); \
            SCRAPE_FLUSH_IF_NEEDED(); \
        } \
        parts[part_count++] = string8_format_z(arena, S8("    " #enum_prefix "_COUNT = {u64},\n"), (table).count); \
        parts[part_count++] = S8(");\n\n"); \
        parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL String8 " #lookup_name "_strings[" #enum_prefix "_COUNT] = {\n"); \
        for (u64 value_i = 0; value_i < (table).count; value_i += 1) \
        { \
            String8 lower_value = scrape_string8_to_lower_arena(arena, (table).values[value_i]); \
            parts[part_count++] = string8_format_z(arena, S8("    [" #enum_prefix "_{S8}] = S8(\"{S8}\"),\n"), (tokens)[value_i], lower_value); \
            SCRAPE_FLUSH_IF_NEEDED(); \
        } \
        parts[part_count++] = S8("};\n\n"); \
        parts[part_count++] = S8("static_assert(BUSTER_ARRAY_LENGTH(" #lookup_name "_strings) == " #enum_prefix "_COUNT);\n\n"); \
    } while (0)

    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86Iclass, X86_ICLASS, iclass_table, iclass_tokens, x86_iclass);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86Iform, X86_IFORM, iform_table, iform_tokens, x86_iform);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86Category, X86_CATEGORY, category_table, category_tokens, x86_category);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86Extension, X86_EXTENSION, extension_table, extension_tokens, x86_extension);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86IsaSet, X86_ISA_SET, isa_set_table, isa_set_tokens, x86_isa_set);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86PrefixType, X86_PREFIX_TYPE, prefix_type_table, prefix_type_tokens, x86_prefix_type);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86VexPP, X86_VEX_PP, vex_pp_table, vex_pp_tokens, x86_vex_pp);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86VexMap, X86_VEX_MAP, vex_map_table, vex_map_tokens, x86_vex_map);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86VectorLength, X86_VECTOR_LENGTH, vector_length_table, vector_length_tokens, x86_vector_length);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86RexW, X86_REX_W, rex_w_table, rex_w_tokens, x86_rex_w);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86RegisterName, X86_REGISTER, register_name_table, register_name_tokens, x86_register_name);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86RegisterClass, X86_REGISTER_CLASS, register_class_table, register_class_tokens, x86_register_class);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandVisibility, X86_OPERAND_VISIBILITY, operand_visibility_table, operand_visibility_tokens, x86_operand_visibility);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandAction, X86_OPERAND_ACTION, operand_action_table, operand_action_tokens, x86_operand_action);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandKind, X86_OPERAND_KIND, operand_kind_table, operand_kind_tokens, x86_operand_kind);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandRegisterConstraint, X86_OPERAND_REGISTER_CONSTRAINT, operand_register_constraint_table, operand_register_constraint_tokens, x86_operand_register_constraint);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandWidthName, X86_OPERAND_WIDTH, operand_width_name_table, operand_width_name_tokens, x86_operand_width_name);
    SCRAPE_EMIT_ENUM_AND_LOOKUPS(X86OperandElementType, X86_OPERAND_ELEMENT_TYPE, operand_element_type_table, operand_element_type_tokens, x86_operand_element_type);

#undef SCRAPE_EMIT_ENUM_AND_LOOKUPS

    // Generate register table
    parts[part_count++] = S8("STRUCT(X86Register)\n{\n");
    parts[part_count++] = S8("    X86RegisterClass register_class;\n");
    parts[part_count++] = S8("    u32 width;\n");
    parts[part_count++] = S8("    X86RegisterName enclosing_64;\n");
    parts[part_count++] = S8("    X86RegisterName enclosing_32;\n");
    parts[part_count++] = S8("    s32 register_id;\n");
    parts[part_count++] = S8("    bool is_high_byte;\n");
    parts[part_count++] = S8("    u8 reserved[3];\n");
    parts[part_count++] = S8("};\n\n");

    parts[part_count++] = string8_format_z(arena, S8("BUSTER_GLOBAL_LOCAL u64 x86_register_count = {u64};\n"), database->register_count);
    parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL X86Register x86_registers[X86_REGISTER_COUNT] = {\n");

    for (u64 i = 0; i < database->register_count; i += 1)
    {
        ScrapeRegister* reg = &database->registers[i];
        String8 is_high_byte = reg->is_high_byte ? S8("true") : S8("false");
        u64 name_id = scrape_string_table_enum_index(&register_name_table, reg->name);
        u64 class_id = scrape_string_table_enum_index(&register_class_table, reg->register_class);
        u64 enclosing_64_id = scrape_string_table_enum_index(&register_name_table, reg->enclosing_64);
        u64 enclosing_32_id = scrape_string_table_enum_index(&register_name_table, reg->enclosing_32);
        String8 name_token = name_id < register_name_table.count ? register_name_tokens[name_id] : S8("NONE");
        String8 class_token = class_id < register_class_table.count ? register_class_tokens[class_id] : S8("NONE");
        String8 enclosing_64_token = enclosing_64_id < register_name_table.count ? register_name_tokens[enclosing_64_id] : S8("NONE");
        String8 enclosing_32_token = enclosing_32_id < register_name_table.count ? register_name_tokens[enclosing_32_id] : S8("NONE");
        parts[part_count++] = string8_format_z(arena,
            S8("    [X86_REGISTER_{S8}] = {{ .register_class = X86_REGISTER_CLASS_{S8}, .width = {u32}, .enclosing_64 = X86_REGISTER_{S8}, .enclosing_32 = X86_REGISTER_{S8}, .register_id = {s32}, .is_high_byte = {S8}, .reserved = {{ 0 }} }},\n"),
            name_token, class_token, reg->width, enclosing_64_token, enclosing_32_token, reg->register_id, is_high_byte);
        SCRAPE_FLUSH_IF_NEEDED();
    }

    parts[part_count++] = S8("};\n\n");
    parts[part_count++] = S8("static_assert(BUSTER_ARRAY_LENGTH(x86_registers) == X86_REGISTER_COUNT);\n\n");

    // Generate operand width table
    parts[part_count++] = S8("STRUCT(X86OperandWidth)\n{\n");
    parts[part_count++] = S8("    X86OperandElementType element_type;\n");
    parts[part_count++] = S8("    u32 width_16;\n");
    parts[part_count++] = S8("    u32 width_32;\n");
    parts[part_count++] = S8("    u32 width_64;\n");
    parts[part_count++] = S8("};\n\n");

    parts[part_count++] = string8_format_z(arena, S8("BUSTER_GLOBAL_LOCAL u64 x86_operand_width_count = {u64};\n"), operand_width_name_table.count);
    parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL X86OperandWidth x86_operand_widths[X86_OPERAND_WIDTH_COUNT] = {\n");

    for (u64 name_i = 0; name_i < operand_width_name_table.count; name_i += 1)
    {
        ScrapeOperandWidth fallback = {
            .name = operand_width_name_table.values[name_i],
            .element_type = S8("NONE"),
            .width_16 = 0,
            .width_32 = 0,
            .width_64 = 0,
            .reserved = { 0 },
        };
        ScrapeOperandWidth* ow = &fallback;
        for (u64 i = 0; i < database->operand_width_count; i += 1)
        {
            if (string8_equal(database->operand_widths[i].name, operand_width_name_table.values[name_i]))
            {
                ow = &database->operand_widths[i];
                break;
            }
        }

        u64 element_id = scrape_string_table_enum_index(&operand_element_type_table, ow->element_type);
        String8 name_token = operand_width_name_tokens[name_i];
        String8 element_token = element_id < operand_element_type_table.count ? operand_element_type_tokens[element_id] : S8("NONE");
        parts[part_count++] = string8_format_z(arena,
            S8("    [X86_OPERAND_WIDTH_{S8}] = {{ .element_type = X86_OPERAND_ELEMENT_TYPE_{S8}, .width_16 = {u32}, .width_32 = {u32}, .width_64 = {u32} }},\n"),
            name_token, element_token, ow->width_16, ow->width_32, ow->width_64);
        SCRAPE_FLUSH_IF_NEEDED();
    }

    parts[part_count++] = S8("};\n\n");
    parts[part_count++] = S8("static_assert(BUSTER_ARRAY_LENGTH(x86_operand_widths) == X86_OPERAND_WIDTH_COUNT);\n\n");

    u64 operand_total_count = 0;
    for (u64 i = 0; i < database->form_count; i += 1)
    {
        operand_total_count += database->forms[i].operand_count;
    }

    parts[part_count++] = S8("STRUCT(X86InstructionOperand)\n{\n");
    parts[part_count++] = S8("    X86OperandKind kind;\n");
    parts[part_count++] = S8("    X86OperandRegisterConstraint register_constraint;\n");
    parts[part_count++] = S8("    X86OperandVisibility visibility;\n");
    parts[part_count++] = S8("    X86OperandAction action;\n");
    parts[part_count++] = S8("    X86OperandWidthName width_name;\n");
    parts[part_count++] = S8("    bool has_register_constraint;\n");
    parts[part_count++] = S8("    bool has_width;\n");
    parts[part_count++] = S8("    bool has_element_type;\n");
    parts[part_count++] = S8("    u8 reserved[5];\n");
    parts[part_count++] = S8("};\n\n");

    parts[part_count++] = string8_format_z(arena, S8("BUSTER_GLOBAL_LOCAL u64 x86_instruction_operand_count = {u64};\n"), operand_total_count);
    parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL X86InstructionOperand x86_instruction_operands[] = {\n");

    for (u64 i = 0; i < database->form_count; i += 1)
    {
        ScrapeInstructionForm* form = &database->forms[i];
        for (u32 operand_i = 0; operand_i < form->operand_count; operand_i += 1)
        {
            ScrapeOperand* operand = &form->operands[operand_i];
            String8 operand_kind = scrape_operand_kind_string(operand);
            String8 operand_action = scrape_operand_action_string(operand);
            u64 register_constraint_id = scrape_string_table_enum_index(&operand_register_constraint_table, operand->register_name);
            u64 visibility_id = scrape_string_table_enum_index(&operand_visibility_table, operand->visibility);
            u64 action_id = scrape_string_table_enum_index(&operand_action_table, operand_action);
            u64 kind_id = scrape_string_table_enum_index(&operand_kind_table, operand_kind);
            u64 width_name_id = scrape_string_table_enum_index(&operand_width_name_table, operand->width);

            String8 register_constraint_token = register_constraint_id < operand_register_constraint_table.count ? operand_register_constraint_tokens[register_constraint_id] : S8("NONE");
            String8 visibility_token = visibility_id < operand_visibility_table.count ? operand_visibility_tokens[visibility_id] : S8("NONE");
            String8 action_token = action_id < operand_action_table.count ? operand_action_tokens[action_id] : S8("NONE");
            String8 kind_token = kind_id < operand_kind_table.count ? operand_kind_tokens[kind_id] : S8("SPECIAL");
            String8 width_name_token = width_name_id < operand_width_name_table.count ? operand_width_name_tokens[width_name_id] : S8("NONE");
            String8 has_register_constraint = operand->register_name.length > 0 ? S8("true") : S8("false");
            String8 has_width = operand->width.length > 0 ? S8("true") : S8("false");
            String8 has_element_type = operand->element_type.length > 0 ? S8("true") : S8("false");

            parts[part_count++] = S8("    { .kind = X86_OPERAND_KIND_");
            parts[part_count++] = kind_token;
            parts[part_count++] = S8(", .register_constraint = X86_OPERAND_REGISTER_CONSTRAINT_");
            parts[part_count++] = register_constraint_token;
            parts[part_count++] = S8(", .visibility = X86_OPERAND_VISIBILITY_");
            parts[part_count++] = visibility_token;
            parts[part_count++] = S8(", .action = X86_OPERAND_ACTION_");
            parts[part_count++] = action_token;
            parts[part_count++] = S8(", .width_name = X86_OPERAND_WIDTH_");
            parts[part_count++] = width_name_token;
            parts[part_count++] = S8(", .has_register_constraint = ");
            parts[part_count++] = has_register_constraint;
            parts[part_count++] = S8(", .has_width = ");
            parts[part_count++] = has_width;
            parts[part_count++] = S8(", .has_element_type = ");
            parts[part_count++] = has_element_type;
            parts[part_count++] = S8(", .reserved = { 0 } },\n");
            SCRAPE_FLUSH_IF_NEEDED();
        }
    }

    parts[part_count++] = S8("};\n\n");

    // Group forms by iclass for fast encoder-side lookup
    if (database->form_count > 65535)
    {
        string8_print(S8("Too many instruction forms ({u64}) for u16 iclass ranges\n"), database->form_count);
        return;
    }

    u64 iclass_enum_count = iclass_table.count;
    u16* iclass_form_counts = arena_allocate(arena, u16, iclass_enum_count);
    u16* iclass_form_starts = arena_allocate(arena, u16, iclass_enum_count);
    u16* iclass_form_cursors = arena_allocate(arena, u16, iclass_enum_count);
    u64* form_order = arena_allocate(arena, u64, database->form_count);
    memset(iclass_form_counts, 0, sizeof(u16) * iclass_enum_count);
    memset(iclass_form_starts, 0, sizeof(u16) * iclass_enum_count);
    memset(iclass_form_cursors, 0, sizeof(u16) * iclass_enum_count);

    for (u64 i = 0; i < database->form_count; i += 1)
    {
        ScrapeInstructionForm* form = &database->forms[i];
        u64 iclass_id = scrape_string_table_enum_index(&iclass_table, form->iclass);
        if (iclass_id < iclass_enum_count)
        {
            iclass_form_counts[iclass_id] += 1;
        }
        else
        {
            string8_print(S8("Missing iclass enum token for form [{u64}] ({S8})\n"), i, form->iclass);
            return;
        }
    }

    u16 running_form_index = 0;
    for (u64 i = 0; i < iclass_enum_count; i += 1)
    {
        iclass_form_starts[i] = running_form_index;
        iclass_form_cursors[i] = running_form_index;
        running_form_index += iclass_form_counts[i];
    }

    for (u64 i = 0; i < database->form_count; i += 1)
    {
        ScrapeInstructionForm* form = &database->forms[i];
        u64 iclass_id = scrape_string_table_enum_index(&iclass_table, form->iclass);
        if (iclass_id < iclass_enum_count)
        {
            u16 destination = iclass_form_cursors[iclass_id];
            form_order[destination] = i;
            iclass_form_cursors[iclass_id] += 1;
        }
        else
        {
            string8_print(S8("Missing iclass enum token for form [{u64}] ({S8})\n"), i, form->iclass);
            return;
        }
    }

    // Generate instruction form table
    parts[part_count++] = string8_format_z(arena, S8("BUSTER_GLOBAL_LOCAL u64 x86_instruction_form_count = {u64};\n\n"), database->form_count);

    parts[part_count++] = S8("STRUCT(X86InstructionForm)\n{\n");
    parts[part_count++] = S8("    X86Iclass iclass;\n");
    parts[part_count++] = S8("    X86Iform iform;\n");
    parts[part_count++] = S8("    X86Category category;\n");
    parts[part_count++] = S8("    X86Extension extension;\n");
    parts[part_count++] = S8("    X86IsaSet isa_set;\n");
    parts[part_count++] = S8("    u8 opcode_bytes[4];\n");
    parts[part_count++] = S8("    u8 opcode_masks[4];\n");
    parts[part_count++] = S8("    u32 opcode_byte_count;\n");
    parts[part_count++] = S8("    u32 operand_start;\n");
    parts[part_count++] = S8("    X86PrefixType prefix_type;\n");
    parts[part_count++] = S8("    X86VexPP vex_pp;\n");
    parts[part_count++] = S8("    X86VexMap vex_map;\n");
    parts[part_count++] = S8("    X86VectorLength vector_length;\n");
    parts[part_count++] = S8("    X86RexW rex_w;\n");
    parts[part_count++] = S8("    u32 operand_count;\n");
    parts[part_count++] = S8("    bool has_modrm;\n");
    parts[part_count++] = S8("    s8 modrm_reg_value;\n");
    parts[part_count++] = S8("    u8 reserved[2];\n");
    parts[part_count++] = S8("};\n\n");

    parts[part_count++] = S8("STRUCT(X86IclassFormRange)\n{\n");
    parts[part_count++] = S8("    u16 start;\n");
    parts[part_count++] = S8("    u16 count;\n");
    parts[part_count++] = S8("};\n\n");

    parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL X86IclassFormRange x86_iclass_form_ranges[X86_ICLASS_COUNT] = {\n");
    for (u64 i = 0; i < iclass_table.count; i += 1)
    {
        parts[part_count++] = string8_format_z(arena,
            S8("    [X86_ICLASS_{S8}] = {{ .start = {u16}, .count = {u16} }},\n"),
            iclass_tokens[i], iclass_form_starts[i], iclass_form_counts[i]);
        SCRAPE_FLUSH_IF_NEEDED();
    }
    parts[part_count++] = S8("};\n\n");
    parts[part_count++] = S8("static_assert(BUSTER_ARRAY_LENGTH(x86_iclass_form_ranges) == X86_ICLASS_COUNT);\n\n");

    parts[part_count++] = S8("BUSTER_GLOBAL_LOCAL X86InstructionForm x86_instruction_forms[] = {\n");

    u32 operand_cursor = 0;
    for (u64 i = 0; i < database->form_count; i += 1)
    {
        u64 source_form_index = form_order[i];
        ScrapeInstructionForm* form = &database->forms[source_form_index];
        String8 has_modrm = form->has_modrm ? S8("true") : S8("false");
        u64 iclass_id = scrape_string_table_enum_index(&iclass_table, form->iclass);
        u64 iform_id = scrape_string_table_enum_index(&iform_table, form->iform);
        u64 category_id = scrape_string_table_enum_index(&category_table, form->category);
        u64 extension_id = scrape_string_table_enum_index(&extension_table, form->extension);
        u64 isa_set_id = scrape_string_table_enum_index(&isa_set_table, form->isa_set);
        u64 prefix_type_id = scrape_string_table_enum_index(&prefix_type_table, form->prefix_type);
        u64 vex_pp_id = scrape_string_table_enum_index(&vex_pp_table, form->vex_pp);
        u64 vex_map_id = scrape_string_table_enum_index(&vex_map_table, form->vex_map);
        u64 vector_length_id = scrape_string_table_enum_index(&vector_length_table, form->vector_length);
        u64 rex_w_id = scrape_string_table_enum_index(&rex_w_table, form->rex_w);

        String8 iclass_token = iclass_id < iclass_table.count ? iclass_tokens[iclass_id] : S8("NONE");
        String8 iform_token = iform_id < iform_table.count ? iform_tokens[iform_id] : S8("NONE");
        String8 category_token = category_id < category_table.count ? category_tokens[category_id] : S8("NONE");
        String8 extension_token = extension_id < extension_table.count ? extension_tokens[extension_id] : S8("NONE");
        String8 isa_set_token = isa_set_id < isa_set_table.count ? isa_set_tokens[isa_set_id] : S8("NONE");
        String8 prefix_type_token = prefix_type_id < prefix_type_table.count ? prefix_type_tokens[prefix_type_id] : S8("NONE");
        String8 vex_pp_token = vex_pp_id < vex_pp_table.count ? vex_pp_tokens[vex_pp_id] : S8("NONE");
        String8 vex_map_token = vex_map_id < vex_map_table.count ? vex_map_tokens[vex_map_id] : S8("NONE");
        String8 vector_length_token = vector_length_id < vector_length_table.count ? vector_length_tokens[vector_length_id] : S8("NONE");
        String8 rex_w_token = rex_w_id < rex_w_table.count ? rex_w_tokens[rex_w_id] : S8("NONE");

        parts[part_count++] = string8_format_z(arena,
            S8("    [{u64}] = {{ .iclass = X86_ICLASS_{S8}, .iform = X86_IFORM_{S8}, .category = X86_CATEGORY_{S8}, .extension = X86_EXTENSION_{S8}, .isa_set = X86_ISA_SET_{S8}, .opcode_bytes = {{ "),
            i, iclass_token, iform_token, category_token, extension_token, isa_set_token);

        for (u32 j = 0; j < SCRAPE_MAX_OPCODE_BYTE_COUNT; j += 1)
        {
            if (j > 0) parts[part_count++] = S8(", ");
            parts[part_count++] = string8_format_z(arena, S8("[{u32}] = 0x{u8:x,width=[0,2],no_prefix}"), j, form->opcode_bytes[j]);
        }

        parts[part_count++] = S8(" }, .opcode_masks = { ");

        for (u32 j = 0; j < SCRAPE_MAX_OPCODE_BYTE_COUNT; j += 1)
        {
            if (j > 0) parts[part_count++] = S8(", ");
            parts[part_count++] = string8_format_z(arena, S8("[{u32}] = 0x{u8:x,width=[0,2],no_prefix}"), j, form->opcode_masks[j]);
        }

        parts[part_count++] = string8_format_z(arena,
            S8(" }, .opcode_byte_count = {u32}, .operand_start = {u32}, .has_modrm = {S8}, .modrm_reg_value = {s32}, .prefix_type = X86_PREFIX_TYPE_{S8}, .vex_pp = X86_VEX_PP_{S8}, .vex_map = X86_VEX_MAP_{S8}, .vector_length = X86_VECTOR_LENGTH_{S8}, .rex_w = X86_REX_W_{S8}, .operand_count = {u32}, .reserved = {{ 0 }} }},\n"),
            form->opcode_byte_count, operand_cursor, has_modrm, form->modrm_reg_value,
            prefix_type_token, vex_pp_token, vex_map_token, vector_length_token, rex_w_token,
            form->operand_count);

        operand_cursor += form->operand_count;
        SCRAPE_FLUSH_IF_NEEDED();
    }

    parts[part_count++] = S8("};\n");

    SCRAPE_FLUSH_PARTS();
    file_write(output_path, (ByteSlice){ .pointer = (u8*)accumulated.pointer, .length = accumulated.length });

#undef SCRAPE_FLUSH_PARTS
#undef SCRAPE_FLUSH_IF_NEEDED
}

// ---------------------------------------------------------------------------
// Program state and entry points
// ---------------------------------------------------------------------------

STRUCT(ScrapeXedProgramState)
{
    ProgramState general_program_state;
    StringOs xed_root;
    StringOs dump_iclass;
    StringOs filter_extension;
    StringOs generate_output;
    bool show_stats;
    bool list_extensions;
    bool list_categories;
    bool list_iclasses;
    bool generate;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL ScrapeXedProgramState scrape_xed_program_state = {};

BUSTER_V_IMPL ProgramState* program_state = &scrape_xed_program_state.general_program_state;

#if BUSTER_FUZZING
BUSTER_IMPL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_V_IMPL ProcessResult process_arguments()
{
    let result = ProcessResult::Success;

    let argv = program_state->input.argv;
    let envp = program_state->input.envp;
    let arg_it = string_os_list_iterator_initialize(argv);

    // Skip program name
    string_os_list_iterator_next(&arg_it);

    let first_argument = string_os_list_iterator_next(&arg_it);
    if (!first_argument.pointer)
    {
        string8_print(S8("Usage: scrape_xed /path/to/xed [options]\n"));
        string8_print(S8("Options:\n"));
        string8_print(S8("  --stats                  Print summary statistics\n"));
        string8_print(S8("  --dump ICLASS            Dump all forms of a specific instruction\n"));
        string8_print(S8("  --filter-extension EXT   Only include instructions from this extension\n"));
        string8_print(S8("  --list-extensions        List all extensions\n"));
        string8_print(S8("  --list-categories        List all categories\n"));
        string8_print(S8("  --list-iclasses          List unique instruction classes\n"));
        string8_print(S8("  --generate               Generate C source file\n"));
        return ProcessResult::Failed;
    }

    if (string8_equal(first_argument, SOs("test")))
    {
        scrape_xed_program_state.xed_root = SOs("/home/david/dev/xed");
        scrape_xed_program_state.generate = true;
    }
    else
    {
        // First positional arg: xed root path
        scrape_xed_program_state.xed_root = first_argument;

        u64 i = 2;
        for (StringOs arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it), i += 1)
        {
            if (string8_equal(arg, SOs("--stats")))
            {
                scrape_xed_program_state.show_stats = true;
            }
            else if (string8_equal(arg, SOs("--dump")))
            {
                scrape_xed_program_state.dump_iclass = string_os_list_iterator_next(&arg_it);
                i += 1;
            }
            else if (string8_equal(arg, SOs("--filter-extension")))
            {
                scrape_xed_program_state.filter_extension = string_os_list_iterator_next(&arg_it);
                i += 1;
            }
            else if (string8_equal(arg, SOs("--list-extensions")))
            {
                scrape_xed_program_state.list_extensions = true;
            }
            else if (string8_equal(arg, SOs("--list-categories")))
            {
                scrape_xed_program_state.list_categories = true;
            }
            else if (string8_equal(arg, SOs("--list-iclasses")))
            {
                scrape_xed_program_state.list_iclasses = true;
            }
            else if (string8_equal(arg, SOs("--generate")))
            {
                scrape_xed_program_state.generate = true;
                // Optional next arg as output path
                let next = string_os_list_iterator_next(&arg_it);
                if (next.pointer && next.pointer[0] != '-')
                {
                    scrape_xed_program_state.generate_output = next;
                    i += 1;
                }
            }
            else
            {
                let r = buster_argument_process(argv, envp, i, arg);
                if (r != ProcessResult::Success)
                {
                    string8_print(S8("Unknown argument: {SOs}\n"), arg);
                    result = r;
                    break;
                }
            }
        }
    }


    return result;
}

BUSTER_F_IMPL void async_user_tick()
{
}

BUSTER_F_IMPL ProcessResult entry_point()
{
    let arena = arena_create((ArenaCreation){ .reserved_size = BUSTER_GB(4)});

    StringOs xed_root = scrape_xed_program_state.xed_root;

    // Initialize database
    ScrapeInstructionDatabase database = {
        .forms = arena_allocate(arena, ScrapeInstructionForm, 16384),
        .form_capacity = 16384,
        .registers = arena_allocate(arena, ScrapeRegister, 1024),
        .register_capacity = 1024,
        .operand_widths = arena_allocate(arena, ScrapeOperandWidth, 256),
        .operand_width_capacity = 256,
    };

    // Find and parse all instruction files
    String8 datafiles_parts[] = { xed_root, S8("/datafiles") };
    String8 datafiles_path = string8_join_arena(arena, BUSTER_ARRAY_TO_SLICE(datafiles_parts), true);

    ScrapeFileList file_list = {
        .paths = arena_allocate(arena, StringOs, SCRAPE_MAX_FILE_COUNT),
        .capacity = SCRAPE_MAX_FILE_COUNT,
    };
    scrape_find_instruction_files_recursive(arena, datafiles_path, &file_list);
    string8_print(S8("Found {u64} instruction definition files\n"), file_list.count);

    for (u64 i = 0; i < file_list.count; i += 1)
    {
        u64 before = database.form_count;
        scrape_parse_instruction_file(arena, &database, file_list.paths[i]);
        let parsed = database.form_count - before;
        if (parsed > 0)
        {
            // Show relative path
            String8 path = file_list.paths[i];
            if (string8_starts_with_sequence(path, xed_root))
            {
                path = string8_from_pointer_length(path.pointer + xed_root.length + 1, path.length - xed_root.length - 1);
            }
            string8_print(S8("  {S8}: {u64} instruction forms\n"), path, parsed);
        }
    }

    string8_print(S8("\nTotal: {u64} instruction forms\n"), database.form_count);

    // Parse supporting data
    String8 reg_parts[] = { xed_root, S8("/datafiles/xed-regs.txt") };
    String8 registers_path = string8_join_arena(arena, BUSTER_ARRAY_TO_SLICE(reg_parts), true);
    scrape_parse_registers(arena, &database, registers_path);

    String8 width_parts[] = { xed_root, S8("/datafiles/xed-operand-width.txt") };
    String8 widths_path = string8_join_arena(arena, BUSTER_ARRAY_TO_SLICE(width_parts), true);
    scrape_parse_operand_widths(arena, &database, widths_path);

    string8_print(S8("Parsed {u64} registers, {u64} operand widths\n"), database.register_count, database.operand_width_count);

    // Apply extension filter
    if (scrape_xed_program_state.filter_extension.pointer)
    {
        String8 filter = scrape_xed_program_state.filter_extension;
        u64 write_index = 0;
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            if (scrape_str_equal_case_insensitive(database.forms[i].extension, filter))
            {
                if (write_index != i)
                {
                    database.forms[write_index] = database.forms[i];
                }
                write_index += 1;
            }
        }
        database.form_count = write_index;
        string8_print(S8("After extension filter: {u64} forms\n"), database.form_count);
    }

    // --generate
    if (scrape_xed_program_state.generate)
    {
        StringOs output_path = scrape_xed_program_state.generate_output.pointer
            ? scrape_xed_program_state.generate_output
            : SOs("build/x86_64_instructions.c");
        string8_print(S8("Generating C source: {SOs}\n"), output_path);
        scrape_generate_c_source(arena, &database, output_path);
        string8_print(S8("Done.\n"));
        return ProcessResult::Success;
    }

    // --dump ICLASS
    if (scrape_xed_program_state.dump_iclass.pointer)
    {
        String8 target = scrape_xed_program_state.dump_iclass;
        u32 match_count = 0;

        string8_print(S8("\n{S8} encoding forms:\n\n"), target);
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            if (scrape_str_equal_case_insensitive(database.forms[i].iclass, target))
            {
                scrape_print_instruction_form(&database.forms[i], match_count);
                match_count += 1;
            }
        }

        if (match_count == 0)
        {
            string8_print(S8("No instruction forms found for {S8}\n"), target);
            return ProcessResult::Failed;
        }
        string8_print(S8("Total: {u32} forms\n"), match_count);
        return ProcessResult::Success;
    }

    // --list-extensions
    if (scrape_xed_program_state.list_extensions)
    {
        ScrapeCountPair* pairs = arena_allocate(arena, ScrapeCountPair, 512);
        u64 pair_count = 0;
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            scrape_count_unique_insert(pairs, &pair_count, 512, database.forms[i].extension);
        }
        scrape_sort_count_pairs_descending(pairs, pair_count);
        for (u64 i = 0; i < pair_count; i += 1)
        {
            string8_print(S8("  {S8:w=25} {u64:w=6} forms\n"), pairs[i].name, pairs[i].count);
        }
        return ProcessResult::Success;
    }

    // --list-categories
    if (scrape_xed_program_state.list_categories)
    {
        ScrapeCountPair* pairs = arena_allocate(arena, ScrapeCountPair, 512);
        u64 pair_count = 0;
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            scrape_count_unique_insert(pairs, &pair_count, 512, database.forms[i].category);
        }
        scrape_sort_count_pairs_descending(pairs, pair_count);
        for (u64 i = 0; i < pair_count; i += 1)
        {
            string8_print(S8("  {S8:w=25} {u64:w=6} forms\n"), pairs[i].name, pairs[i].count);
        }
        return ProcessResult::Success;
    }

    // --list-iclasses
    if (scrape_xed_program_state.list_iclasses)
    {
        ScrapeCountPair* pairs = arena_allocate(arena, ScrapeCountPair, 8192);
        u64 pair_count = 0;
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            scrape_count_unique_insert(pairs, &pair_count, 8192, database.forms[i].iclass);
        }
        scrape_sort_count_pairs_descending(pairs, pair_count);
        for (u64 i = 0; i < pair_count; i += 1)
        {
            string8_print(S8("  {S8:w=25} ({u64} forms)\n"), pairs[i].name, pairs[i].count);
        }
        return ProcessResult::Success;
    }

    // --stats
    if (scrape_xed_program_state.show_stats)
    {
        ScrapeCountPair* extensions = arena_allocate(arena, ScrapeCountPair, 512);
        u64 extension_count = 0;
        ScrapeCountPair* categories = arena_allocate(arena, ScrapeCountPair, 512);
        u64 category_count = 0;
        u64 legacy_count = 0;
        u64 vex_count = 0;
        u64 evex_count = 0;
        u64 xop_count = 0;

        for (u64 i = 0; i < database.form_count; i += 1)
        {
            ScrapeInstructionForm* form = &database.forms[i];
            scrape_count_unique_insert(extensions, &extension_count, 512, form->extension);
            scrape_count_unique_insert(categories, &category_count, 512, form->category);

            if (form->prefix_type.length == 0) legacy_count += 1;
            else if (string8_equal(form->prefix_type, S8("VEX"))) vex_count += 1;
            else if (string8_equal(form->prefix_type, S8("EVEX"))) evex_count += 1;
            else if (string8_equal(form->prefix_type, S8("XOP"))) xop_count += 1;
        }

        // Count unique iclasses
        ScrapeCountPair* iclass_pairs = arena_allocate(arena, ScrapeCountPair, 8192);
        u64 iclass_count = 0;
        for (u64 i = 0; i < database.form_count; i += 1)
        {
            scrape_count_unique_insert(iclass_pairs, &iclass_count, 8192, database.forms[i].iclass);
        }

        string8_print(S8("\n=== Instruction Set Statistics ===\n"));
        string8_print(S8("Total instruction forms:  {u64}\n"), database.form_count);
        string8_print(S8("Unique instruction names: {u64}\n"), iclass_count);
        string8_print(S8("\nBy encoding type:\n"));
        string8_print(S8("  legacy     {u64:w=6}\n"), legacy_count);
        string8_print(S8("  VEX        {u64:w=6}\n"), vex_count);
        string8_print(S8("  EVEX       {u64:w=6}\n"), evex_count);
        string8_print(S8("  XOP        {u64:w=6}\n"), xop_count);

        scrape_sort_count_pairs_descending(extensions, extension_count);
        string8_print(S8("\nTop 15 extensions:\n"));
        u64 ext_display = extension_count < 15 ? extension_count : 15;
        for (u64 i = 0; i < ext_display; i += 1)
        {
            string8_print(S8("  {S8:w=25} {u64:w=6}\n"), extensions[i].name, extensions[i].count);
        }

        scrape_sort_count_pairs_descending(categories, category_count);
        string8_print(S8("\nTop 15 categories:\n"));
        u64 cat_display = category_count < 15 ? category_count : 15;
        for (u64 i = 0; i < cat_display; i += 1)
        {
            string8_print(S8("  {S8:w=25} {u64:w=6}\n"), categories[i].name, categories[i].count);
        }

        return ProcessResult::Success;
    }

    // Default: print first 100 forms as a table
    string8_print(S8("\n{S8:w=20} {S8:w=12} {S8:w=16} {S8}\n"), S8("ICLASS"), S8("EXT"), S8("OPCODE"), S8("PATTERN (short)"));
    for (u32 i = 0; i < 80; i += 1) string8_print(S8("-"));
    string8_print(S8("\n"));

    u64 display_count = database.form_count < 100 ? database.form_count : 100;
    for (u64 i = 0; i < display_count; i += 1)
    {
        ScrapeInstructionForm* form = &database.forms[i];

        // Build opcode string
        String8 opcode_parts[SCRAPE_MAX_OPCODE_BYTE_COUNT * 2];
        u64 opcode_part_count = 0;
        for (u32 j = 0; j < form->opcode_byte_count; j += 1)
        {
            if (j > 0) opcode_parts[opcode_part_count++] = S8(" ");
            opcode_parts[opcode_part_count++] = string8_format_z(arena, S8("{u8:x,width=[0,2],no_prefix}"), form->opcode_bytes[j]);
        }
        String8 opcode_string;
        if (form->opcode_byte_count == 0)
        {
            opcode_string = S8("?");
        }
        else
        {
            opcode_string = string8_join_arena(arena, { .pointer = opcode_parts, .length = opcode_part_count }, true);
        }

        // Truncate pattern to 40 chars
        String8 pattern_short = form->pattern;
        if (pattern_short.length > 40)
        {
            pattern_short.length = 40;
        }

        string8_print(S8("{S8:w=20} {S8:w=12} {S8:w=16} {S8}\n"), form->iclass, form->extension, opcode_string, pattern_short);
    }

    if (database.form_count > 100)
    {
        string8_print(S8("  ... and {u64} more (use --dump ICLASS or --stats)\n"), database.form_count - 100);
    }

    return ProcessResult::Success;
}
#endif
