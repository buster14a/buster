// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU assembly source metadata and typed operand validation for the checker's x86-64 target. Assembly text is
// retained, not interpreted as C or executed. The program consumer owns evaluation and control edges; machine
// emission must explicitly reject this metadata until an assembly consumer can preserve all of its effects.

#define CHECK_ASM_WRITE 0x0001u
#define CHECK_ASM_READ 0x0002u
#define CHECK_ASM_MEMORY 0x0004u
#define CHECK_ASM_REGISTER 0x0008u
#define CHECK_ASM_IMMEDIATE 0x0010u
#define CHECK_ASM_MATCH 0x0020u
#define CHECK_ASM_EARLY 0x0040u
#define CHECK_ASM_COMMUTATIVE 0x0080u
#define CHECK_ASM_FLAG 0x0100u

typedef struct CheckAsmText CheckAsmText;
struct CheckAsmText
{
    const uint8_t* bytes;
    uint32_t length;
    uint32_t token;
};

typedef struct CheckAsmOperand CheckAsmOperand;
struct CheckAsmOperand
{
    CheckAsmText constraint;
    uint32_t name;
    uint32_t token;
    uint32_t opening;
    uint32_t closing;
    uint32_t node;
    uint32_t match;
    uint32_t flags;
};

typedef struct CheckAsmLabel CheckAsmLabel;
struct CheckAsmLabel
{
    uint32_t name;
    uint32_t token;
    uint32_t target;
};

struct CheckInputAsm
{
    CheckAsmText template_text;
    CheckAsmOperand* operands;
    CheckAsmText* clobbers;
    CheckAsmLabel* labels;
    uint32_t output_count;
    uint32_t input_count;
    uint32_t clobber_count;
    uint32_t label_count;
    uint32_t token;
    uint32_t closing;
    uint32_t statement;
    uint32_t body;
    // The widest vector register an x, v or Yz operand takes in its function (#2231): 512 bits with AVX-512F, 256 with
    // AVX, else 128, by the function's features (the unit's, or its target attribute's).
    uint16_t vector_bits;
    bool basic;
    bool is_volatile;
    bool is_inline;
    bool is_goto;
    bool memory_clobber;
    bool cc_clobber;
};

typedef enum CheckAsmStatus
{
    CHECK_ASM_OK,
    CHECK_ASM_INVALID,
    CHECK_ASM_RESOURCE,
} CheckAsmStatus;

typedef struct CheckAsmCheckedOperand CheckAsmCheckedOperand;
struct CheckAsmCheckedOperand
{
    CheckValue value;
    CheckConversion conversion;
    uint32_t statement;
    uint32_t operand;
    uint32_t node;
    uint32_t flags;
};

typedef struct CheckAsms CheckAsms;
struct CheckAsms
{
    CheckAsmCheckedOperand* operands;
    uint32_t count;
    uint32_t token;
    CheckAsmStatus status;
};

static bool check_asm_text_equal(CheckAsmText text, const char* expected)
{
    return text.length == strlen(expected) && (text.length == 0 || memcmp(text.bytes, expected, text.length) == 0);
}

// The widest vector register of the current function's features (#2231, CheckInputAsm.vector_bits), as clang's
// validateOperandSize reads its feature map (ASTContext::getFunctionFeatureMap): the unit's, or those a target attribute
// on the function's definition gives (predefines_function_target). Once a body: its statements share it.
static uint16_t check_input_asm_vector_bits(CheckInputReader* reader)
{
    const uint32_t body = reader->function_body;
    if (reader->asm_vector_bits == 0 || reader->asm_vector_body != body)
    {
        X86Target target = reader->pp->options.target;
        const uint32_t function = body < reader->parser->tokens.count ? check_sparse_get(reader->output->body_declarations, body) : BUSTER_CHECK_NONE;
        const CheckInputAttribute* attribute = function < reader->parser->tokens.count ?
            check_input_attribute_of(check_input_attributes_at(reader->output, function), CHECK_ATTRIBUTE_TARGET) : NULL;
        int32_t cpu = -1;
        const bool version = attribute != NULL && attribute->text.length < UINT32_MAX &&
                             predefines_target_attribute(attribute->text.pointer, (uint32_t)attribute->text.length, &cpu) == PREDEFINES_TARGET_ATTRIBUTE_VERSION;
        if (version)
        {
            const PredefinesHost host = predefines_host_of(&reader->pp->options.predefines);
            target = predefines_function_target(&reader->pp->options.predefines, &host, target, attribute->text.pointer, (uint32_t)attribute->text.length, cpu);
        }
        reader->asm_vector_bits = (target.features & BUSTER_X86_FEATURE(AVX512F)) != 0 ? 512 : (target.features & BUSTER_X86_FEATURE(AVX)) != 0 ? 256 : 128;
        reader->asm_vector_body = body;
    }
    return reader->asm_vector_bits;
}

static void check_input_asm_mark(CheckInputReader* reader, uint32_t begin, uint32_t end, uint32_t id)
{
    for (uint32_t token = begin; token < end; token += 1)
    {
        reader->ignored[token] = true;
        reader->output->asm_ids[token] = id;
        check_input_sparse_mark(reader, token, id != 0);
    }
}

static bool check_input_asm_text(CheckInputReader* reader, uint32_t* cursor, uint32_t end, uint32_t id, CheckAsmText* text)
{
    const uint32_t begin = *cursor;
    uint32_t after = begin;
    const Parser* parser = reader->parser;
    while (after < end && parser->tokens.kinds[after] == PARSE_KIND_STRING)
    {
        after += 1;
    }
    const uint64_t mark = reader->scratch->length;
    CheckStringPart* parts = after > begin ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckStringPart, after - begin) : NULL;
    bool valid = parts != NULL;
    reader->output->resource |= after > begin && parts == NULL;
    for (uint32_t token = begin; valid && token < after; token += 1)
    {
        valid = check_input_spelling(reader, token, &parts[token - begin].bytes, &parts[token - begin].length);
    }
    if (valid)
    {
        CheckStringLiteral literal = check_literal_strings(parts, after - begin, parser->language.standard >= 2023, reader->arena);
        reader->output->resource |= literal.resource;
        if (!literal.valid && !literal.resource)
        {
            // An evaluated string literal's escapes the fast decoding refuses that clang takes (#1447), and the refusal
            // of one it refuses too, which the driver words as clang does (check_driver_literal_escapes).
            literal = check_input_strings_extended(reader, begin, parts, after - begin);
            if (!literal.valid && !literal.resource)
            {
                check_input_report(reader, begin, CHECK_INPUT_LITERAL);
            }
        }
        valid = literal.valid && literal.element_type == BUSTER_TYPE_CHAR && literal.byte_count != 0 && literal.byte_count <= UINT32_MAX;
        if (valid)
        {
            *text = (CheckAsmText)
            {
                .bytes = literal.bytes,
                .length = (uint32_t)literal.byte_count - 1,
                .token = begin,
            };
        }
    }
    if (valid)
    {
        check_input_asm_mark(reader, begin, after, id);
        *cursor = after;
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static uint32_t check_asm_named(const CheckInputAsm* statement, const InternTable* names, const uint8_t* bytes, uint32_t length, bool labels)
{
    const uint32_t count = labels ? statement->label_count : statement->output_count + statement->input_count;
    uint32_t found = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; found == BUSTER_CHECK_NONE && i < count; i += 1)
    {
        const uint32_t name = labels ? statement->labels[i].name : statement->operands[i].name;
        if (name != 0 && intern_length(names, name) == length && memcmp(intern_bytes(names, name), bytes, length) == 0)
        {
            found = i;
        }
    }
    return found;
}

// The letters clang's x86 target takes in a constraint (#1973, X86TargetInfo::validateAsmConstraint and the generic
// TargetInfo's): those that allow a register or memory, which an output needs one of, and those that allow neither
// (constants), which an input may be alone; and an input's alone, x87's 'f' and an address's 'p'. Y and W take a second
// letter.
static const char check_asm_clang_operands[] = "abcdgklmoqrtuvxyADQRSVX<>";
static const char check_asm_clang_others[] = "efinpsCEFGIJKLMNOZ";

// Whether clang's validateOutputConstraint (an output's) or validateInputConstraint (an input's) takes operand `index`'s
// constraint (#1973): where it doesn't, its error is clang's "invalid output|input constraint 'C' in asm", and where it
// does but check_asm_constraint refuses it, the checker's own. A cold path, of a refused constraint alone.
static bool check_asm_clang_valid(const CheckInputAsm* statement, uint32_t index, const InternTable* names)
{
    const CheckAsmText text = statement->operands[index].constraint;
    const bool output = index < statement->output_count;
    // (An empty constraint is neither's, an input's too: clang's "invalid input constraint '' in asm".)
    bool valid = text.length != 0 && (!output || text.bytes[0] == '=' || text.bytes[0] == '+');
    const bool read_write = output && valid && text.bytes[0] == '+';
    bool operand = false;
    bool register_allowed = false;
    bool early = false;
    uint32_t tied = BUSTER_CHECK_NONE;
    for (uint32_t cursor = output ? 1 : 0; valid && cursor < text.length; cursor += 1)
    {
        const uint8_t byte = text.bytes[cursor];
        const uint8_t next = cursor + 1 < text.length ? text.bytes[cursor + 1] : 0;
        uint32_t match = BUSTER_CHECK_NONE;
        if (byte == ',')
        {
            cursor += output && (next == '=' || next == '+') ? 1 : 0;
        }
        else if (byte == '#')
        {
            while (cursor + 1 < text.length && text.bytes[cursor + 1] != ',')
            {
                cursor += 1;
            }
        }
        else if (byte == '?' || byte == '!' || byte == '*' || byte == '%' || (output && (byte == 'i' || byte == 'n' || byte == 'E' || byte == 'F')))
        {
        }
        else if (byte == '&')
        {
            valid = output;
            early = true;
        }
        else if (byte != 0 && strchr(check_asm_clang_operands, byte) != NULL)
        {
            operand = true;
            register_allowed |= strchr("moV<>", byte) == NULL;
        }
        else if (byte != 0 && strchr(check_asm_clang_others, byte) != NULL)
        {
            // x87's 'f' isn't an output's, and 'p' only the generic input check knows.
            valid = !output || (byte != 'f' && byte != 'p');
        }
        else if (output && byte == '@' && next == 'c' && cursor + 2 < text.length && text.bytes[cursor + 2] == 'c')
        {
            // A flag output (`=@ccz`), which x86's validateAsmConstraint takes with its condition: a register's
            // (check_asm_constraint checks the condition's name).
            operand = true;
            register_allowed = true;
            while (cursor + 1 < text.length && text.bytes[cursor + 1] != ',')
            {
                cursor += 1;
            }
        }
        else if ((byte == 'Y' && next != 0 && strchr("ikmtz2", next) != NULL) || (byte == 'W' && next == 's'))
        {
            operand = true;
            register_allowed = true;
            cursor += 1;
        }
        else if (!output && byte >= '0' && byte <= '9')
        {
            match = byte - '0';
            while (cursor + 1 < text.length && text.bytes[cursor + 1] >= '0' && text.bytes[cursor + 1] <= '9' && match < statement->output_count)
            {
                cursor += 1;
                match = match * 10 + text.bytes[cursor] - '0';
            }
            valid = match < statement->output_count;
        }
        else if (!output && byte == '[')
        {
            const uint32_t begin = cursor + 1;
            while (cursor < text.length && text.bytes[cursor] != ']')
            {
                cursor += 1;
            }
            match = cursor < text.length ? check_asm_named(statement, names, text.bytes + begin, cursor - begin, false) : BUSTER_CHECK_NONE;
            valid = match < statement->output_count;
        }
        else
        {
            valid = false;
        }
        // A tied input's output must be an output alone, and one input is tied to one output.
        valid = valid && (match == BUSTER_CHECK_NONE ||
                          (statement->operands[match].constraint.length != 0 && statement->operands[match].constraint.bytes[0] != '+' &&
                           (tied == BUSTER_CHECK_NONE || tied == match)));
        tied = match != BUSTER_CHECK_NONE ? match : tied;
    }
    // An output allows a register or memory, and an early clobber of a read-write one a register.
    return valid && (!output || (operand && !(early && read_write && !register_allowed)));
}

static bool check_asm_constraint(CheckInputAsm* statement, uint32_t index, const InternTable* names, uint32_t* alternatives)
{
    CheckAsmOperand* operand = statement->operands + index;
    const bool output = index < statement->output_count;
    const CheckAsmText text = operand->constraint;
    bool valid = text.length != 0 && memchr(text.bytes, 0, text.length) == NULL;
    uint32_t cursor = 0;
    operand->match = BUSTER_CHECK_NONE;
    operand->flags = output ? CHECK_ASM_WRITE : CHECK_ASM_READ;
    if (output)
    {
        valid = valid && (text.bytes[0] == '=' || text.bytes[0] == '+');
        operand->flags |= valid && text.bytes[0] == '+' ? CHECK_ASM_READ : 0;
        cursor = 1;
    }
    *alternatives = 1;
    bool alternative = false;
    while (valid && cursor < text.length)
    {
        const uint8_t byte = text.bytes[cursor++];
        if (byte == ',')
        {
            valid = !output || alternative;
            alternative = false;
            *alternatives += 1;
        }
        else if (byte == '&' || byte == '%')
        {
            valid = byte != '&' || output;
            operand->flags |= byte == '&' ? CHECK_ASM_EARLY : CHECK_ASM_COMMUTATIVE;
        }
        else if (byte == '?' || byte == '!' || byte == '*' || byte == '#' || byte == ' ' || byte == '\t')
        {
            // Alternative preference modifiers do not create a register or memory class themselves.
        }
        else if (!output && (byte == '[' || (byte >= '0' && byte <= '9')))
        {
            uint32_t match = BUSTER_CHECK_NONE;
            if (byte == '[')
            {
                const uint32_t begin = cursor;
                while (cursor < text.length && text.bytes[cursor] != ']')
                {
                    cursor += 1;
                }
                valid = cursor < text.length;
                match = valid ? check_asm_named(statement, names, text.bytes + begin, cursor - begin, false) : BUSTER_CHECK_NONE;
                cursor += valid;
            }
            else
            {
                match = byte - '0';
                while (cursor < text.length && text.bytes[cursor] >= '0' && text.bytes[cursor] <= '9' && match < statement->output_count)
                {
                    match = match * 10 + text.bytes[cursor++] - '0';
                }
            }
            valid &= match < statement->output_count && (operand->match == BUSTER_CHECK_NONE || operand->match == match);
            if (valid)
            {
                operand->match = match;
                operand->flags |= CHECK_ASM_MATCH | (statement->operands[match].flags & (CHECK_ASM_MEMORY | CHECK_ASM_REGISTER));
            }
            alternative = true;
        }
        else if (byte == '@' && output && cursor + 2 < text.length && text.bytes[cursor] == 'c' && text.bytes[cursor + 1] == 'c')
        {
            cursor += 2;
            const uint32_t begin = cursor;
            while (cursor < text.length && text.bytes[cursor] != ',')
            {
                cursor += 1;
            }
            static const char* const conditions[] =
            {
                "a",
                "ae",
                "b",
                "be",
                "c",
                "e",
                "z",
                "g",
                "ge",
                "l",
                "le",
                "o",
                "p",
                "s",
                "na",
                "nae",
                "nb",
                "nbe",
                "nc",
                "ne",
                "nz",
                "ng",
                "nge",
                "nl",
                "nle",
                "no",
                "np",
                "ns",
            };
            const CheckAsmText condition =
            {
                .bytes = text.bytes + begin,
                .length = cursor - begin,
            };
            bool known = false;
            for (uint32_t i = 0; !known && i < BUSTER_ARRAY_LENGTH(conditions); i += 1)
            {
                known = check_asm_text_equal(condition, conditions[i]);
            }
            valid &= known;
            operand->flags |= CHECK_ASM_FLAG | CHECK_ASM_REGISTER;
            alternative = true;
        }
        else if (byte == '{')
        {
            // Clang's x86-64 GNU C constraints do not accept GCC's hard-register brace spelling.
            valid = false;
        }
        else if (byte == 'Y')
        {
            valid = cursor < text.length && strchr("2tzimk", text.bytes[cursor]) != NULL;
            cursor += valid;
            operand->flags |= CHECK_ASM_REGISTER;
            alternative = true;
        }
        else if (byte == 'm' || byte == 'o' || byte == 'V' || byte == '<' || byte == '>')
        {
            operand->flags |= CHECK_ASM_MEMORY;
            alternative = true;
        }
        else if (strchr("rRqQlabcdSDAftuxvyk", byte) != NULL)
        {
            valid = byte != 'f' || !output;
            operand->flags |= CHECK_ASM_REGISTER;
            alternative = true;
        }
        else if (strchr("inCEFGIJKLMNOseZ", byte) != NULL)
        {
            // An output's constant letters are clang's, which no output takes: its alternative's other letters decide
            // (`=re`'s register; `=i` alone has none, check_asm_clang_valid's error).
            operand->flags |= !output ? CHECK_ASM_IMMEDIATE : 0;
            alternative |= !output;
        }
        else if (byte == 'W' && !output && cursor < text.length && text.bytes[cursor] == 's')
        {
            // x86's Ws: a symbol's address with an optional addend, an input's constant.
            cursor += 1;
            operand->flags |= CHECK_ASM_IMMEDIATE;
            alternative = true;
        }
        else if (byte == 'g' || byte == 'X' || byte == 'p')
        {
            valid = byte != 'p' || !output;
            operand->flags |= CHECK_ASM_REGISTER | CHECK_ASM_MEMORY | (!output ? CHECK_ASM_IMMEDIATE : 0);
            alternative = true;
        }
        else
        {
            valid = false;
        }
    }
    // Clang accepts a nonempty input constraint made solely from preference modifiers or separators.
    // It still checks the operand's value type; an output needs a real register or memory class.
    return valid && (!output || alternative);
}

static bool check_asm_template(const CheckInputAsm* statement, const InternTable* names)
{
    const CheckAsmText text = statement->template_text;
    const uint32_t operands = statement->input_count + statement->output_count;
    uint32_t read_write = 0;
    for (uint32_t i = 0; i < statement->output_count; i += 1)
    {
        read_write += (statement->operands[i].flags & CHECK_ASM_READ) != 0;
    }
    bool valid = true;
    for (uint32_t cursor = 0; valid && !statement->basic && cursor < text.length; cursor += 1)
    {
        if (text.bytes[cursor] == '%')
        {
            cursor += 1;
            valid = cursor < text.length;
            if (valid && (text.bytes[cursor] == 0 || strchr("%=}{", text.bytes[cursor]) == NULL))
            {
                const bool label = text.bytes[cursor] == 'l';
                if ((text.bytes[cursor] >= 'a' && text.bytes[cursor] <= 'z') || (text.bytes[cursor] >= 'A' && text.bytes[cursor] <= 'Z'))
                {
                    cursor += 1;
                }
                valid = cursor < text.length;
                if (valid && text.bytes[cursor] == '[')
                {
                    const uint32_t begin = ++cursor;
                    while (cursor < text.length && text.bytes[cursor] != ']')
                    {
                        cursor += 1;
                    }
                    valid = cursor < text.length && check_asm_named(statement, names, text.bytes + begin, cursor - begin, label) != BUSTER_CHECK_NONE;
                }
                else if (valid)
                {
                    uint32_t index = 0;
                    const uint32_t begin = cursor;
                    while (cursor < text.length && text.bytes[cursor] >= '0' && text.bytes[cursor] <= '9' && index <= operands + read_write + statement->label_count)
                    {
                        index = index * 10 + text.bytes[cursor++] - '0';
                    }
                    valid = cursor > begin && (label ? index >= operands + read_write && index < operands + read_write + statement->label_count : index < operands);
                    cursor -= cursor > begin;
                }
            }
        }
    }
    return valid;
}

static void check_input_asm_prepare(CheckInputReader* reader, uint32_t keyword)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    // (A string literal's own refusal, check_input_asm_text's, stands for the statement's.)
    const uint32_t reports = input->report_count;
    uint32_t opening = keyword + 1;
    while (opening < parser->tokens.count && parser->tokens.kinds[opening] != PARSE_KIND_L_PAREN)
    {
        opening += 1;
    }
    const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : BUSTER_CHECK_NONE;
    bool valid = opening < parser->tokens.count && parser->tree.roles[opening] == PARSE_ROLE_OPEN_ASM && closing < parser->tokens.count &&
                 closing + 1 < parser->tokens.count && parser->tokens.kinds[closing + 1] == PARSE_KIND_SEMI;
    input->resource |= input->asm_count >= input->asm_capacity;
    valid &= !input->resource;
    CheckInputAsm* statement = valid ? input->asm_statements + input->asm_count : NULL;
    const uint32_t id = input->asm_count + 1;
    const uint64_t scratch_mark = reader->scratch->length;
    uint32_t cursor = opening + 1;
    uint32_t failed = keyword;
    if (valid)
    {
        *statement = (CheckInputAsm)
        {
            .token = keyword,
            .closing = closing,
            .statement = closing + 1,
            .body = reader->function_body,
            .vector_bits = check_input_asm_vector_bits(reader),
        };
        for (uint32_t token = keyword + 1; token < opening; token += 1)
        {
            statement->is_volatile |= parser->tokens.kinds[token] == PARSE_KIND_QUALIFIER;
            statement->is_inline |= parser->tokens.kinds[token] == PARSE_KIND_STORAGE;
            statement->is_goto |= parser->tokens.kinds[token] == PARSE_KIND_GOTO;
        }
        check_input_asm_mark(reader, keyword, opening + 1, id);
        check_input_asm_mark(reader, closing, closing + 2, id);
        valid = check_input_asm_text(reader, &cursor, closing, id, &statement->template_text);
    }
    const uint32_t capacity = valid ? closing - cursor : 0;
    CheckAsmOperand* operands = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAsmOperand, capacity) : NULL;
    CheckAsmText* clobbers = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAsmText, capacity) : NULL;
    CheckAsmLabel* labels = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAsmLabel, capacity) : NULL;
    input->resource |= capacity != 0 && (operands == NULL || clobbers == NULL || labels == NULL);
    valid &= !input->resource;
    uint32_t section = 0;
    while (valid && cursor < closing)
    {
        valid = parser->tokens.kinds[cursor] == PARSE_KIND_COLON && section < (statement->is_goto ? 4u : 3u);
        if (!valid)
        {
            break;
        }
        check_input_asm_mark(reader, cursor, cursor + 1, id);
        cursor += 1;
        section += 1;
        bool first = true;
        while (valid && cursor < closing && parser->tokens.kinds[cursor] != PARSE_KIND_COLON)
        {
            if (!first)
            {
                valid = parser->tokens.kinds[cursor] == PARSE_KIND_COMMA;
                if (valid)
                {
                    check_input_asm_mark(reader, cursor, cursor + 1, id);
                    cursor += 1;
                }
            }
            first = false;
            if (valid && section <= 2)
            {
                const uint32_t index = statement->output_count + statement->input_count;
                CheckAsmOperand* operand = operands + index;
                *operand = (CheckAsmOperand)
                {
                    .token = cursor,
                    .node = BUSTER_CHECK_NONE,
                    .match = BUSTER_CHECK_NONE,
                };
                if (cursor < closing && parser->tokens.kinds[cursor] == PARSE_KIND_L_SQUARE)
                {
                    valid = cursor + 2 < closing && parser->tokens.kinds[cursor + 1] == PARSE_KIND_IDENTIFIER && parser->tokens.kinds[cursor + 2] == PARSE_KIND_R_SQUARE;
                    if (valid)
                    {
                        operand->name = parser->tokens.names[cursor + 1];
                        check_input_asm_mark(reader, cursor, cursor + 3, id);
                        cursor += 3;
                    }
                }
                valid = valid && check_input_asm_text(reader, &cursor, closing, id, &operand->constraint);
                operand->opening = cursor;
                operand->closing = cursor < closing ? parser->tree.parents[cursor] : BUSTER_CHECK_NONE;
                valid = valid && cursor < closing && parser->tokens.kinds[cursor] == PARSE_KIND_L_PAREN &&
                        operand->closing < closing && operand->closing > cursor + 1;
                if (valid)
                {
                    check_input_asm_mark(reader, cursor, cursor + 1, id);
                    check_input_asm_mark(reader, operand->closing, operand->closing + 1, id);
                    cursor = operand->closing + 1;
                    statement->output_count += section == 1;
                    statement->input_count += section == 2;
                }
            }
            else if (valid && section == 3)
            {
                CheckAsmText* clobber = clobbers + statement->clobber_count;
                failed = cursor;
                valid = check_input_asm_text(reader, &cursor, closing, id, clobber);
                if (valid)
                {
                    statement->memory_clobber |= check_asm_text_equal(*clobber, "memory");
                    statement->cc_clobber |= check_asm_text_equal(*clobber, "cc");
                    valid = memchr(clobber->bytes, 0, clobber->length) == NULL;
                    statement->clobber_count += 1;
                }
            }
            else if (valid)
            {
                valid = cursor < closing && parser->tokens.kinds[cursor] == PARSE_KIND_IDENTIFIER;
                if (valid)
                {
                    labels[statement->label_count++] = (CheckAsmLabel)
                    {
                        .name = parser->tokens.names[cursor],
                        .token = cursor,
                        .target = BUSTER_CHECK_NONE,
                    };
                    check_input_asm_mark(reader, cursor, cursor + 1, id);
                    cursor += 1;
                }
            }
        }
    }
    if (valid)
    {
        statement->basic = section == 0;
        statement->is_volatile |= statement->output_count == 0 || statement->is_goto;
        valid = cursor == closing && (!statement->is_goto || statement->label_count != 0) &&
                (statement->body != BUSTER_CHECK_NONE || statement->basic);
        const uint32_t count = statement->output_count + statement->input_count;
        statement->operands = operands;
        statement->clobbers = clobbers;
        statement->labels = labels;
        uint32_t alternatives = 0;
        for (uint32_t operand = 0; valid && operand < count; operand += 1)
        {
            uint32_t current = 0;
            failed = operands[operand].constraint.token;
            valid = check_asm_constraint(statement, operand, reader->pp->table, &current);
            // clang's validation first, which refuses some the checker's classes take (an early clobber of a read-write
            // memory output, an input tied to a read-write output).
            const bool clang_valid = check_asm_clang_valid(statement, operand, reader->pp->table);
            valid = valid && clang_valid;
            if (!clang_valid)
            {
                // clang's error of a constraint it refuses (#1973), as written: its bytes stay in the reader's arena.
                const CheckAsmText text = operands[operand].constraint;
                uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)text.length + 1);
                const uint32_t before = input->report_count;
                if (bytes != NULL && text.length < BUSTER_CHECK_ASM_OUTPUT)
                {
                    memcpy(bytes, text.bytes, text.length);
                    check_input_report(reader, text.token, CHECK_INPUT_ASM_CONSTRAINT);
                }
                if (input->report_count > before)
                {
                    input->reports[before].value = (uint64_t)(uintptr_t)bytes;
                    input->reports[before].detail = text.length | (operand < statement->output_count ? BUSTER_CHECK_ASM_OUTPUT : 0u);
                }
            }
            valid = valid && (operand == 0 || current == alternatives);
            alternatives = current;
            for (uint32_t before = 0; valid && operands[operand].name != 0 && before < operand; before += 1)
            {
                valid = operands[before].name != operands[operand].name;
            }
        }
        // The clobbers clang's TargetInfo::isValidClobber takes (#2262): a register name clang's target knows
        // (check_input_register_name_valid, past one '%' or '#', or an index into its names), memory, cc and unwind. The
        // first other is clang's "unknown register name 'X' in asm" at its string, after the operands' constraints, and
        // ends the statement, as clang returns there.
        for (uint32_t c = 0; valid && c < statement->clobber_count; c += 1)
        {
            const CheckAsmText text = clobbers[c];
            const String8 name = { .pointer = (char*)text.bytes, .length = text.length };
            const bool known = check_asm_text_equal(text, "memory") || check_asm_text_equal(text, "cc") || check_asm_text_equal(text, "unwind") ||
                               check_input_register_name_valid(name);
            if (!known)
            {
                // Its bytes stay in the reader's arena, as a register variable's label's (CHECK_INPUT_REGISTER_NAME).
                uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)text.length + 1);
                const uint32_t before = input->report_count;
                if (bytes != NULL && text.length < BUSTER_CHECK_REGISTER_NAME_UNSUITABLE)
                {
                    memcpy(bytes, text.bytes, text.length);
                    check_input_report(reader, text.token, CHECK_INPUT_REGISTER_NAME);
                }
                if (input->report_count > before)
                {
                    input->reports[before].value = (uint64_t)(uintptr_t)bytes;
                    input->reports[before].detail = text.length;
                }
                failed = text.token;
                valid = false;
            }
        }
        for (uint32_t label = 0; valid && label < statement->label_count; label += 1)
        {
            failed = labels[label].token;
            for (uint32_t operand = 0; valid && operand < count; operand += 1)
            {
                valid = labels[label].name != operands[operand].name;
            }
            for (uint32_t before = 0; valid && before < label; before += 1)
            {
                valid = labels[before].name != labels[label].name;
            }
        }
        if (valid)
        {
            failed = statement->template_text.token;
            valid = check_asm_template(statement, reader->pp->table);
        }
        if (valid)
        {
            statement->operands = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckAsmOperand, count) : NULL;
            statement->clobbers = statement->clobber_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckAsmText, statement->clobber_count) : NULL;
            statement->labels = statement->label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckAsmLabel, statement->label_count) : NULL;
            input->resource |= (count != 0 && statement->operands == NULL) || (statement->clobber_count != 0 && statement->clobbers == NULL) ||
                               (statement->label_count != 0 && statement->labels == NULL);
            valid = !input->resource;
            if (valid)
            {
                if (count != 0)
                {
                    memcpy(statement->operands, operands, (uint64_t)count * sizeof(*operands));
                }
                if (statement->clobber_count != 0)
                {
                    memcpy(statement->clobbers, clobbers, (uint64_t)statement->clobber_count * sizeof(*clobbers));
                }
                if (statement->label_count != 0)
                {
                    memcpy(statement->labels, labels, (uint64_t)statement->label_count * sizeof(*labels));
                }
                input->asm_count += 1;
            }
        }
    }
    if (!valid && input->report_count == reports)
    {
        check_input_report(reader, failed, CHECK_INPUT_DECLARATION);
    }
    else if (!valid && input->report_count == reports + 1 &&
             (input->reports[reports].message == CHECK_INPUT_ASM_CONSTRAINT || input->reports[reports].message == CHECK_INPUT_REGISTER_NAME))
    {
        // clang's Sema error of a constraint or a clobber (#2369): ActOnGCCAsmStmt drops the statement and checking goes on
        // past it, so the error is recovered and the statement is no asm statement of the unit's.
        check_input_recover_reports(reader, reports);
    }
    arena_pop_to(reader->scratch, scratch_mark);
}

// Clang's declaration-use rule is lexical, not the backend's evaluated-reference graph. An ordinary reference
// in an unchosen conditional, generic association or choose_expr still prevents a later first asm label. The
// controlling generic expression and non-VLA sizeof/typeof operands are genuinely unevaluated contexts.
static bool check_input_asm_label_use(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    bool evaluated = true;
    bool variable_bound = false;
    for (uint32_t ancestor = token, steps = 0; evaluated && ancestor < input->token_count && steps < input->token_count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[ancestor];
        const uint32_t bound = reader->vla_ends[ancestor];
        if (bound != 0 && bound <= input->vla_count)
        {
            variable_bound |= input->vlas[bound - 1].evaluated;
        }
        if (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[ancestor] == PARSE_KIND_SIZEOF)
        {
            const uint32_t node = input->token_nodes[ancestor];
            evaluated = node < input->graph.count && input->graph.ops[node] == CHECK_OP_SIZEOF_VLA;
        }
        if (role == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            const uint32_t keyword = reader->first_child[ancestor];
            if (keyword < input->token_count && parser->tokens.kinds[keyword] == PARSE_KIND_TYPEOF)
            {
                const CheckInputSpecifierJob* job = reader->specifier_finishes[ancestor];
                evaluated = variable_bound || (job != NULL && check_input_vla_modified(reader->types, job->result));
            }
        }
        if (role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            const uint32_t keyword = reader->first_child[ancestor];
            if (keyword < input->token_count && parser->tokens.names[keyword] == C_NAME_GENERIC)
            {
                const uint32_t separator = check_constant_punctuation(parser, keyword + 2, ancestor, PARSE_KIND_COMMA);
                evaluated = token >= separator;
            }
        }
        ancestor = parser->tree.parents[ancestor];
    }
    return evaluated;
}

// The tokens below `limit` with attributes, in order, into `tokens` (room for `limit`), and how many (#877, #1167): the
// vector path skips 256 tokens without attributes with a load of their directory entries and tests 16 tokens' values 8
// at a time (check_sparse_wide_entries); the scalar path, its twin, tests each token's. The asm labels' passes read them
// rather than the column, which a unit's few attributed tokens left them sweeping three times.
static uint32_t check_input_asm_attributed_tokens(const CheckInput* input, uint32_t limit, uint32_t* tokens, bool scalar)
{
    (void)scalar;

    return check_sparse_wide_entries(input->attributes, limit, !BUSTER_SCALAR_PATH(scalar), tokens);
}

// Whether bit `index` of `bits` is set: words of 32 bits, as the kernels below gather them.
static bool check_input_asm_bit(const uint32_t* bits, uint32_t index)
{
    return ((bits[index / 32] >> (index % 32)) & 1) != 0;
}

// The tokens whose object (CheckInput.objects) is labelled, by `labelled`'s bits (an object's), as bits of `declared` (a
// token's, (count + 31) / 32 words), which it writes whole (#877): a labelled object's declarations, to which the uses
// below refer. AVX-512 gathers 16 tokens' words of `labelled` where the labelled objects' filter (CheckInputFilter)
// passes one of their objects, and stores their 16 bits at once: a unit that includes glibc's headers labels a few dozen
// objects, and gathering at every 16 tokens in both kernels took 0.45% of sqlite3.c's -c's cycles (#529). The scalar
// path, its twin, tests each token's.
static void check_input_asm_labelled_tokens(const CheckInput* input, const uint32_t* labelled, uint32_t* declared, bool scalar)
{
    (void)scalar;

    const uint32_t count = input->token_count;
    const uint32_t slots = count + 1;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        memset(declared, 0, ((uint64_t)count + 31) / 32 * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t object = input->objects[token];
            if (object != 0 && object < slots && check_input_asm_bit(labelled, object))
            {
                declared[token / 32] |= UINT32_C(1) << (token % 32);
            }
        }
    }
    else
    {
        // A group's 16 bits are a 16-bit half of `declared`'s 32-bit words: on x86-64's little-endian words, group g is
        // tokens 16g to 16g + 15, bits 16(g % 2) to 16(g % 2) + 15 of word g / 2, the scalar path's bits. The build's
        // -fno-strict-aliasing allows the 16-bit stores into the words, and an odd last group's other half is zeroed
        // below, so that every word the tokens reach is whole.
        uint16_t* halves = (uint16_t*)declared;
        const __m512i ones = _mm512_set1_epi32(1);
        const CheckInputFilter filter = check_input_filter_bits(labelled, (slots + 31) / 32);
        for (uint32_t first = 0; first < count; first += 16)
        {
            const __mmask16 lanes = (__mmask16)check_input_scan_all(first, count);
            const __m512i objects = _mm512_maskz_loadu_epi32(lanes, input->objects + first);
            const __mmask16 named = _mm512_mask_test_epi32_mask(lanes, objects, objects) & _mm512_cmplt_epu32_mask(objects, _mm512_set1_epi32((int32_t)slots));
            const __mmask16 candidates = check_input_filter_test(&filter, objects, named);
            __mmask16 marked = 0;
            if (candidates != 0)
            {
                const __m512i words = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, _mm512_srli_epi32(objects, 5), labelled, 4);
                const __m512i bits = _mm512_srlv_epi32(words, _mm512_and_si512(objects, _mm512_set1_epi32(31)));
                marked = _mm512_mask_test_epi32_mask(candidates, bits, ones);
            }
            halves[first / 16] = marked;
        }
        // The last word's other half, past the tokens.
        if (((count + 15) / 16) % 2 != 0)
        {
            halves[(count + 15) / 16] = 0;
        }
    }
}

// Of the next 16 tokens from `first` below `end`, those whose declaration (CheckInput.declarations) is a token `declared`
// marks: the uses and declarations of labelled objects. AVX-512 gathers their words of `declared` where `filter`
// (check_input_filter_bits of `declared`) passes one of their declarations; the scalar path, its twin, takes every token,
// whose declaration the pass tests, and leaves the filter unread.
static uint32_t check_input_asm_labelled_uses(const CheckInput* input, const uint32_t* declared, const CheckInputFilter* filter, uint32_t first, uint32_t end,
                                              bool scalar)
{
    (void)scalar;
    (void)filter;

    uint32_t lanes = (uint32_t)check_input_scan_all(first, end) & 0xffff;
    if (!BUSTER_SCALAR_PATH(scalar))
    {
        const __m512i declarations = _mm512_maskz_loadu_epi32((__mmask16)lanes, input->declarations + first);
        const __mmask16 named = _mm512_mask_cmplt_epu32_mask((__mmask16)lanes, declarations, _mm512_set1_epi32((int32_t)input->token_count));
        const __mmask16 candidates = check_input_filter_test(filter, declarations, named);
        lanes = 0;
        if (candidates != 0)
        {
            const __m512i words = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, _mm512_srli_epi32(declarations, 5), declared, 4);
            const __m512i bits = _mm512_srlv_epi32(words, _mm512_and_si512(declarations, _mm512_set1_epi32(31)));
            lanes = _mm512_mask_test_epi32_mask(candidates, bits, _mm512_set1_epi32(1));
        }
    }
    return lanes;
}

// The asm labels' checks (GCC's and clang's rules for a declaration's linkage label), then register variables' labels'
// names. The tokens with attributes are listed once (check_input_asm_attributed_tokens); a labelled object's state and
// first label are kept for the labelled objects alone, which `labelled` marks, and the pass over the uses tests whether a
// token's declaration is a labelled object's (`declared`) before it reads the object (#877), where it read every
// identifier's object and state, and cleared both columns for every token.
static void check_input_asm_labels(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    input->asm_warnings_prepared = true;
    const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar);
    const uint64_t mark = reader->scratch->length;
    const uint64_t slots = (uint64_t)input->token_count + 1;
    const uint64_t words = (slots + 31) / 32;
    const uint32_t limit = reader->expression_limit;
    // A label needs an asm keyword: without one, no token has a label to check.
    uint32_t* attributed = input->asm_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)limit + 1) : NULL;
    input->resource |= input->asm_capacity != 0 && attributed == NULL;
    const uint32_t attributed_count = attributed != NULL ? check_input_asm_attributed_tokens(input, limit, attributed, scalar) : 0;
    bool present = false;
    for (uint32_t a = 0; !present && a < attributed_count; a += 1)
    {
        const uint32_t token = attributed[a];
        const CheckInputAttributes* attributes = !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER) ? check_input_attributes_at(input, token) : NULL;
        for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
        {
            present |= attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL && attributes->records[i].end <= limit;
        }
    }
    const CheckInputAttribute** labels = present ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, const CheckInputAttribute*, slots) : NULL;
    uint8_t* states = labels != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, slots) : NULL;
    uint32_t* labelled = states != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, words) : NULL;
    uint32_t* declared = labelled != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, words) : NULL;
    input->resource |= present && declared == NULL;
    if (declared != NULL)
    {
        memset(labelled, 0, words * sizeof(uint32_t));
        // Bit 0 marks only canonical objects with labels, bit 1 a preceding use and bit 2 a real definition.
        // Tentative definitions deliberately do not make a later first label an ignored attribute. An object's state and
        // label are cleared where `labelled` first marks it: no other is read.
        for (uint32_t a = 0; a < attributed_count; a += 1)
        {
            const uint32_t token = attributed[a];
            const CheckInputAttributes* attributes = !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER) ? check_input_attributes_at(input, token) : NULL;
            const uint32_t object = input->objects[token];
            for (uint32_t i = 0; object != 0 && object < slots && attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->end <= limit)
                {
                    if (!check_input_asm_bit(labelled, object))
                    {
                        labelled[object / 32] |= UINT32_C(1) << (object % 32);
                        states[object] = 0;
                        labels[object] = NULL;
                    }
                    states[object] |= 1;
                }
            }
        }
        check_input_asm_labelled_tokens(input, labelled, declared, scalar);
        const CheckInputFilter filter = !scalar ? check_input_filter_bits(declared, (uint32_t)((input->token_count + 31) / 32)) : (CheckInputFilter){ 0 };
        for (uint32_t first = 0; first < limit && !input->resource; first += 16)
        {
            const uint32_t uses = check_input_asm_labelled_uses(input, declared, &filter, first, limit, scalar);
            for (uint32_t selected = uses; selected != 0 && !input->resource; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t declaration = input->declarations[token];
                if (declaration < input->token_count && check_input_asm_bit(declared, declaration))
                {
                    const uint32_t object = input->objects[declaration];
                    if (declaration == token)
                    {
                        CheckInputAttributes* attributes = !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER) ? check_input_attributes_at(input, token) : NULL;
                        for (uint32_t i = 0; attributes != NULL && i < attributes->count && !input->resource; i += 1)
                        {
                            CheckInputAttribute* attribute = attributes->records + i;
                            if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !attribute->ignored && attribute->end <= reader->expression_limit)
                            {
                                const CheckInputAttribute* previous = labels[object];
                                if (attribute->text.length == 0)
                                {
                                    check_input_report(reader, attribute->argument_first, CHECK_INPUT_DECLARATION);
                                }
                                else if (previous != NULL && (previous->text.length != attribute->text.length ||
                                         memcmp(previous->text.pointer, attribute->text.pointer, attribute->text.length) != 0))
                                {
                                    // clang's "conflicting asm label" at the name, noted at the earlier label (#2262), before
                                    // a redefinition's error there, which the reader recovered from: published too.
                                    const uint32_t before = input->report_count;
                                    check_input_report(reader, token, CHECK_INPUT_ASM_LABEL_CONFLICT);
                                    if (input->report_count > before)
                                    {
                                        input->reports[before].note_token = previous->argument_first;
                                        input->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
                                    }
                                    for (uint32_t r = 0; r < input->recovered_count; r += 1)
                                    {
                                        input->recovered_reports[r].repeated |= input->recovered_reports[r].token == token;
                                    }
                                }
                                else if (previous == NULL && (states[object] & 2) != 0)
                                {
                                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                                }
                                else if (previous == NULL && (states[object] & 4) != 0)
                                {
                                    attribute->ignored = true;
                                    input->ignored_asm_count += 1;
                                }
                                else
                                {
                                    labels[object] = attribute;
                                }
                            }
                        }
                        states[object] |= check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) ? 4 : 0;
                    }
                    else if (reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND && check_input_asm_label_use(reader, token))
                    {
                        states[object] |= 2;
                    }
                }
            }
        }
    }
    // A local register variable's label names a register clang knows (#1846, check_input_register_name_valid), as clang's
    // Sema checks it: here, after its body, so that a later syntax error's repair keeps it first. A global's is
    // check_input_register_label's.
    for (uint32_t a = 0; a < attributed_count && !input->resource; a += 1)
    {
        const uint32_t token = attributed[a];
        // An object in a body of automatic storage, not `register` (#2370): clang's Sema ignores its label with "ignored asm
        // label 'X' on automatic variable", in no group, at the label's string, as it declares the object. A static or
        // extern one has static storage; a typedef's or a function's label is not an object's.
        const uint8_t bits = token < input->token_count ? input->declaration_bits[token] : 0;
        const bool automatic = reader->parser->tree.depths[token] != 0 && reader->declaration_kinds[token] == C_BINDING_OBJECT &&
                               (bits & (BUSTER_CHECK_BITS_REGISTER | BUSTER_CHECK_BITS_STATIC)) == 0 && check_input_linkage(input->declaration_bits, token) == 0;
        const CheckInputAttributes* automatic_attributes = automatic ? check_input_attributes_at(input, token) : NULL;
        for (uint32_t i = 0; automatic_attributes != NULL && token < reader->expression_limit && i < automatic_attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = automatic_attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->end <= reader->expression_limit)
            {
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_ASM_LABEL_ON_AUTO,
                    .kind = CHECK_WARNING_EVENT_ASM_LABEL_ON_AUTO,
                    .token = attribute->argument_first,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = attribute->argument_first,
                    .payload = { .token = attribute->argument_first },
                };
                check_input_warning_event(reader, &event, NULL, 0);
            }
        }
        // (In a body: a file-scope declaration's tokens are at depth 0.)
        const bool local = check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER) && reader->parser->tree.depths[token] != 0;
        const CheckInputAttributes* attributes = local ? check_input_attributes_at(input, token) : NULL;
        for (uint32_t i = 0; attributes != NULL && token < reader->expression_limit && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->end <= reader->expression_limit && !check_input_register_name_valid(attribute->text) &&
                attribute->text.length < BUSTER_CHECK_REGISTER_NAME_UNSUITABLE)
            {
                // Its bytes stay in the reader's arena, as an asm constraint's (CHECK_INPUT_REGISTER_NAME).
                uint8_t* copy = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)attribute->text.length + 1);
                const uint32_t before = input->report_count;
                if (copy != NULL)
                {
                    memcpy(copy, attribute->text.pointer, attribute->text.length);
                    check_input_report(reader, attribute->argument_first, CHECK_INPUT_REGISTER_NAME);
                }
                if (input->report_count > before)
                {
                    input->reports[before].value = (uint64_t)(uintptr_t)copy;
                    input->reports[before].detail = (uint32_t)attribute->text.length;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}

// Whether the unit has an asm goto with a label (check_input_vla_finish's jumps).
static bool check_input_asm_gotos(const CheckInput* input)
{
    bool found = false;
    for (uint32_t a = 0; a < input->asm_count; a += 1)
    {
        found |= input->asm_statements[a].is_goto && input->asm_statements[a].label_count != 0;
    }
    return found;
}

// Whether the asm goto whose keyword is `token` jumps where clang's JumpScopeChecker refuses (#147, #1126's review): to
// a label of its own in a variable-length array's or a cleanup's scope it isn't in (check_input_vla_finish's scope
// marks), or out of `innermost`, the innermost cleanup's scope that holds it, if any.
static bool check_input_asm_goto_refused(const CheckInputReader* reader, uint32_t token, const CheckInputCleanup* innermost)
{
    const Parser* parser = reader->parser;
    const uint32_t id = check_input_asm_id(reader->output, token);
    const CheckInputAsm* assembly = id != 0 ? reader->output->asm_statements + id - 1 : NULL;
    bool refused = false;
    for (uint32_t l = 0; assembly != NULL && assembly->token == token && assembly->is_goto && l < assembly->label_count; l += 1)
    {
        const uint32_t label = assembly->labels[l].target;
        const uint32_t declaration = label < parser->tokens.count && reader->scope_marks[label] != 0 ? reader->scope_marks[label] - 1 : BUSTER_CHECK_NONE;
        const bool enters = declaration != BUSTER_CHECK_NONE && (token < declaration || token >= reader->vla_scope_ends[declaration]);
        const bool exits = innermost != NULL && label < parser->tokens.count && (label <= innermost->declaration || label >= innermost->end);
        refused |= enters || exits;
    }
    return refused;
}

static void check_input_asm_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    check_input_asm_labels(reader);
    for (uint32_t statement = 0; statement < input->asm_count && !input->resource; statement += 1)
    {
        CheckInputAsm* assembly = input->asm_statements + statement;
        if (assembly->closing >= check_input_expression_limit(reader, assembly->closing))
        {
            continue;
        }
        for (uint32_t operand = 0; operand < assembly->output_count + assembly->input_count; operand += 1)
        {
            CheckAsmOperand* value = assembly->operands + operand;
            value->node = input->token_nodes[value->closing];
            if (value->node == BUSTER_CHECK_NONE && value->closing < check_input_expression_limit(reader, value->closing))
            {
                check_input_report(reader, value->token, CHECK_INPUT_DECLARATION);
            }
        }
        for (uint32_t label = 0; label < assembly->label_count; label += 1)
        {
            CheckAsmLabel* target = assembly->labels + label;
            const uint32_t body = assembly->body;
            const uint32_t end = body < input->token_count ? parser->tree.parents[body] : 0;
            for (uint32_t token = body < input->token_count ? body + 1 : 0; token < end && target->target == BUSTER_CHECK_NONE; token += 1)
            {
                if (parser->tree.roles[token] == PARSE_ROLE_LABEL)
                {
                    const uint32_t name = check_input_label_name(reader, token);
                    if (name < input->token_count && parser->tokens.names[name] == target->name)
                    {
                        target->target = token;
                    }
                }
            }
            if (target->target == BUSTER_CHECK_NONE || !check_input_region_transfer(reader, assembly->token, target->target))
            {
                check_input_report(reader, target->token, CHECK_INPUT_UNDECLARED);
            }
        }
    }
}

// Whether expression `node` names a global register variable (#1846: `register long sp __asm__("rsp")` at file scope),
// as clang's refersToGlobalRegisterVar takes it: the variable itself, past parentheses.
static bool check_asm_global_register(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    const CheckValue value = node < stream->count && input->graph.ops[node] == CHECK_OP_VALUE ? check_stream_value(stream, node) : (CheckValue){ 0 };
    const uint32_t declaration = value.object != 0 && value.object <= input->token_count ? value.object - 1 : BUSTER_CHECK_NONE;
    return declaration < input->token_count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_REGISTER) &&
           check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC);
}

// Whether an operand of `bits` fits its constraint (#2231), as clang's X86TargetInfo::validateOperandSize checks it by the
// constraint's first letter past `=`, `+` and `&`: k, y, Ym and Yk at most 64 bits; f, t and u 128; x, v and Yz the
// function's widest vector register (CheckInputAsm.vector_bits); any other (Yi, Yt and Y2 with SSE2, which every target
// here has) any size.
static bool check_asm_operand_size_valid(const CheckInputAsm* assembly, const CheckAsmOperand* source, uint64_t bits)
{
    const CheckAsmText text = source->constraint;
    uint32_t at = 0;
    while (at < text.length && (text.bytes[at] == '=' || text.bytes[at] == '+' || text.bytes[at] == '&'))
    {
        at += 1;
    }
    const uint8_t first = at < text.length ? text.bytes[at] : 0;
    const uint8_t second = at + 1 < text.length ? text.bytes[at + 1] : 0;
    const bool mask = first == 'k' || first == 'y' || (first == 'Y' && (second == 'm' || second == 'k'));
    const bool stack = first == 'f' || first == 't' || first == 'u';
    const bool vector = first == 'x' || first == 'v' || (first == 'Y' && second == 'z');
    const uint64_t limit = mask ? 64 : stack ? 128 : vector ? assembly->vector_bits : UINT64_MAX;
    return bits <= limit;
}

static CheckAsmStatus check_asm_operand_checks(CheckAsmCheckedOperand* result, const CheckInput* input, const CheckStream* stream, TypeTable* types, uint32_t statement,
                                               uint32_t operand, bool whole);

// An operand's checks, clang's: its own, its size's (#2231) and its tie's.
static CheckAsmStatus check_asm_operand(CheckAsmCheckedOperand* result, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, uint32_t statement, uint32_t operand)
{
    return check_asm_operand_checks(result, input, stream, types, statement, operand, true);
}

// An operand's own checks, and with `whole` its size's and its tie's after them, as clang makes them.
static CheckAsmStatus check_asm_operand_checks(CheckAsmCheckedOperand* result, const CheckInput* input, const CheckStream* stream, TypeTable* types, uint32_t statement,
                                               uint32_t operand, bool whole)
{
    const CheckInputAsm* assembly = input->asm_statements + statement;
    const CheckAsmOperand* source = assembly->operands + operand;
    bool valid = source->node < stream->count;
    CheckActionResult checked = { 0 };
    CheckValue value = { 0 };
    if (valid)
    {
        const CheckValue original = check_stream_value(stream, source->node);
        CheckAction action =
        {
            .node = source->node,
            .context = check_graph_context(&input->graph, source->node, false),
        };
        action.operands[0] = original;
        value = original;
        const bool write = (source->flags & CHECK_ASM_WRITE) != 0;
        const bool memory = (source->flags & CHECK_ASM_MEMORY) != 0 && (source->flags & (CHECK_ASM_REGISTER | CHECK_ASM_IMMEDIATE)) == 0;
        if (write)
        {
            // GNU asm can write a whole array, unlike a C assignment. Its element constness and
            // completeness still constrain writing, and the published value retains the array type.
            CheckValue destination = original;
            while (type_kind_is_array(check_action_kind(types, destination.type)))
            {
                destination.type = type_element(types, type_strip_aligned(types, destination.type));
            }
            valid = check_action_source_complete(types, original.type, &action) && check_action_modifiable(types, &destination, &action, &checked);
            checked.conversions[0].type = original.type;
        }
        else if (memory)
        {
            valid = original.category == CHECK_LVALUE && check_action_source_complete(types, original.type, &action) &&
                    (original.flags & BUSTER_CHECK_BIT_FIELD) == 0;
            checked.conversions[0].type = original.type;
        }
        else
        {
            value = check_action_operand(types, &action, &checked, 0);
            valid = checked.diagnostic == CHECK_DIAG_NONE && check_action_kind(types, value.type) != BUSTER_TYPE_KIND_VOID &&
                    check_action_source_complete(types, value.type, &action);
        }
        valid &= !memory || (original.flags & BUSTER_CHECK_BIT_FIELD) == 0;
        // Nor a global register variable of a constraint that allows memory (#1846), which has no address.
        valid &= (source->flags & CHECK_ASM_MEMORY) == 0 || input->global_register_count == 0 || !check_asm_global_register(input, stream, source->node);
        // clang refuses a _BitInt operand of any constraint ("invalid type in asm input", #147), and takes a flag
        // output of any other type, whose size its code generation (and the lowering, by name) limits.
        valid &= check_action_kind(types, type_strip_aligned(types, original.type)) != BUSTER_TYPE_KIND_BIT_INT;
        // The operand's size, by its (converted) type's.
        const uint64_t bits = type_is_complete(types, value.type) ? type_size(types, value.type) * 8 : 0;
        valid = valid && (!whole || check_asm_operand_size_valid(assembly, source, bits));
        if (valid && whole && (source->flags & CHECK_ASM_MATCH) != 0)
        {
            valid = source->match < assembly->output_count && assembly->operands[source->match].node < stream->count;
            if (valid)
            {
                const CheckValue target = check_stream_value(stream, assembly->operands[source->match].node);
                const uint8_t a_kind = check_action_kind(types, value.type);
                const uint8_t b_kind = check_action_kind(types, target.type);
                const bool a_float = a_kind == BUSTER_TYPE_KIND_FLOATING;
                const bool b_float = b_kind == BUSTER_TYPE_KIND_FLOATING;
                const bool a_record = a_kind == BUSTER_TYPE_KIND_STRUCT || a_kind == BUSTER_TYPE_KIND_UNION;
                const bool b_record = b_kind == BUSTER_TYPE_KIND_STRUCT || b_kind == BUSTER_TYPE_KIND_UNION;
                valid = a_float == b_float && a_record == b_record;
            }
        }
    }
    if (valid)
    {
        *result = (CheckAsmCheckedOperand)
        {
            .value = value,
            .conversion = checked.conversions[0],
            .statement = statement,
            .operand = operand,
            .node = source->node,
            .flags = source->flags,
        };
    }
    return checked.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_ASM_RESOURCE : valid ? CHECK_ASM_OK : CHECK_ASM_INVALID;
}

// Why an operand fails, by clang's checks (Sema::ActOnGCCAsmStmt), whose words the driver gives it (#1447).
typedef enum CheckAsmError
{
    // One clang has no words of ours for: "invalid assembly operand or constraint".
    CHECK_ASM_ERROR_GENERIC,
    // An output that isn't a modifiable lvalue: "invalid lvalue in asm output".
    CHECK_ASM_ERROR_OUTPUT_LVALUE,
    // A memory-only input that isn't an lvalue: "invalid lvalue in asm input for constraint 'C'".
    CHECK_ASM_ERROR_INPUT_LVALUE,
    // A bit-field of a constraint that allows memory: "reference to a bit-field in asm input|output with a memory
    // constraint 'C'", clang's words of an output naming an input and of an input an output.
    CHECK_ASM_ERROR_BIT_FIELD,
    // So a global register variable's (#1846): "reference to a global register variable in asm input|output with a memory
    // constraint 'C'", with the same exchange.
    CHECK_ASM_ERROR_GLOBAL_REGISTER,
    // A _BitInt's: "invalid type 'T' in asm input|output", with the same exchange.
    CHECK_ASM_ERROR_BIT_INT,
    // A void input of a constraint that allows a register: "invalid type 'void' in asm input for constraint 'C'".
    CHECK_ASM_ERROR_VOID,
    // An incomplete type's: "dereference of pointer to incomplete type 'T'", noted at a tag's forward declaration.
    CHECK_ASM_ERROR_INCOMPLETE,
    // An input whose type its output's doesn't take, checked after every operand's own: "unsupported inline asm: input
    // with type 'T' matching output with type 'U'".
    CHECK_ASM_ERROR_TIED,
    // An output that is a cast of an lvalue (GNU's lvalue cast), which clang reports at its operand through the casts
    // that don't change its value and goes on: "invalid use of a cast in an inline asm context requiring an lvalue
    // [-Winvalid-gnu-asm-cast]".
    CHECK_ASM_ERROR_CAST,
    // An operand too wide for its constraint (#2231, check_asm_operand_size_valid), after its own checks and before its
    // tie's: "invalid output|input size for constraint 'C'", the constraint as written.
    CHECK_ASM_ERROR_OUTPUT_SIZE,
    CHECK_ASM_ERROR_INPUT_SIZE,
} CheckAsmError;

// One statement's error (check_asms_diagnose_all): clang gives a statement's first, and returns, but after a cast's.
typedef struct CheckAsmFailure CheckAsmFailure;
struct CheckAsmFailure
{
    uint32_t token;
    uint32_t statement;
    uint32_t operand;
    // The node whose place a cast's error takes: the outermost cast that changes the value, or the lvalue it casts.
    uint32_t node;
    CheckAsmError error;
    // The operand's type, and for a tied input its output's.
    TypeId type;
    TypeId output_type;
};

typedef struct CheckAsmDiagnostic CheckAsmDiagnostic;
struct CheckAsmDiagnostic
{
    CheckAsmStatus status;
    uint32_t token;
    // check_asms_diagnose_all's: each failing statement's error, in statement order.
    const CheckAsmFailure* failures;
    uint32_t failure_count;
};

// Why an operand check_asm_operand refuses fails: its type and clang's first check of those it fails, an output's
// (a memory constraint's bit-field, a _BitInt, a modifiable lvalue's completeness then itself), or an input's (a memory
// constraint's bit-field, a memory-only constraint's lvalue, a register's void, a _BitInt, a complete type), and a
// tie's last. Cold: only a refused operand's.
static CheckAsmError check_asm_operand_error(const CheckInput* input, const CheckStream* stream, TypeTable* types, uint32_t statement, uint32_t operand,
                                             TypeId* type, TypeId* output_type, uint32_t* place)
{
    const CheckGraph* graph = &input->graph;
    const CheckInputAsm* assembly = input->asm_statements + statement;
    const CheckAsmOperand* source = assembly->operands + operand;
    const CheckValue value = check_stream_value(stream, source->node);
    const uint8_t kind = check_action_kind(types, type_strip_aligned(types, value.type));
    const bool write = (source->flags & CHECK_ASM_WRITE) != 0;
    const bool allows_memory = (source->flags & CHECK_ASM_MEMORY) != 0;
    const bool memory_only = allows_memory && (source->flags & (CHECK_ASM_REGISTER | CHECK_ASM_IMMEDIATE)) == 0;
    const bool bit_field = (value.flags & BUSTER_CHECK_BIT_FIELD) != 0;
    const CheckContext context = check_graph_context(&input->graph, source->node, false);
    const bool complete = kind == BUSTER_TYPE_KIND_VOID || check_context_source_complete(types, value.type, &context);
    CheckAsmError result = CHECK_ASM_ERROR_GENERIC;
    *type = value.type;
    *output_type = 0;
    // Its own checks pass, so its size's or its tie's fails.
    CheckAsmCheckedOperand own = { 0 };
    const bool own_valid = check_asm_operand_checks(&own, input, stream, types, statement, operand, false) == CHECK_ASM_OK;
    const uint64_t bits = own_valid && type_is_complete(types, own.value.type) ? type_size(types, own.value.type) * 8 : 0;
    if (own_valid && !check_asm_operand_size_valid(assembly, source, bits))
    {
        result = write ? CHECK_ASM_ERROR_OUTPUT_SIZE : CHECK_ASM_ERROR_INPUT_SIZE;
    }
    else if (allows_memory && bit_field)
    {
        result = CHECK_ASM_ERROR_BIT_FIELD;
    }
    else if (allows_memory && input->global_register_count != 0 && check_asm_global_register(input, stream, source->node))
    {
        result = CHECK_ASM_ERROR_GLOBAL_REGISTER;
    }
    else if (write && kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = CHECK_ASM_ERROR_BIT_INT;
    }
    else if (write && graph->ops[source->node] == CHECK_OP_CAST && graph->operand0[source->node] < stream->count &&
             check_stream_value(stream, graph->operand0[source->node]).category == CHECK_LVALUE &&
             check_action_kind(types, type_unqualified(type_strip_aligned(types, check_stream_type(stream, source->node)))) != BUSTER_TYPE_KIND_VOID)
    {
        // Through the casts to the same type, or between integers and pointers of one width (IgnoreParenNoopCasts). A cast
        // to void is no cast clang names: its result is no lvalue, so the operand is an output's invalid lvalue instead.
        uint32_t at = source->node;
        bool through = true;
        for (uint32_t steps = 0; through && steps < graph->count && graph->ops[at] == CHECK_OP_CAST && graph->operand0[at] < stream->count; steps += 1)
        {
            const TypeId cast = type_unqualified(type_strip_aligned(types, check_stream_type(stream, at)));
            const TypeId operand_type = type_unqualified(type_strip_aligned(types, check_stream_type(stream, graph->operand0[at])));
            const uint8_t cast_kind = check_action_kind(types, cast);
            const uint8_t operand_kind = check_action_kind(types, operand_type);
            uint32_t width = 0;
            bool sign = false;
            const bool cast_scalar = cast_kind == BUSTER_TYPE_KIND_POINTER || check_action_integer_type(types, cast, &width, &sign);
            const bool operand_scalar = operand_kind == BUSTER_TYPE_KIND_POINTER || check_action_integer_type(types, operand_type, &width, &sign);
            through = cast == operand_type || (cast_scalar && operand_scalar && type_size(types, cast) == type_size(types, operand_type));
            at = through ? graph->operand0[at] : at;
        }
        *place = at;
        result = CHECK_ASM_ERROR_CAST;
    }
    else if (write)
    {
        result = value.category == CHECK_LVALUE && !complete && kind != BUSTER_TYPE_KIND_VOID ? CHECK_ASM_ERROR_INCOMPLETE : CHECK_ASM_ERROR_OUTPUT_LVALUE;
    }
    else if (memory_only && value.category != CHECK_LVALUE)
    {
        result = CHECK_ASM_ERROR_INPUT_LVALUE;
    }
    else if (!memory_only && kind == BUSTER_TYPE_KIND_VOID)
    {
        result = CHECK_ASM_ERROR_VOID;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = CHECK_ASM_ERROR_BIT_INT;
    }
    else if (!complete)
    {
        result = CHECK_ASM_ERROR_INCOMPLETE;
    }
    else if ((source->flags & CHECK_ASM_MATCH) != 0 && source->match < assembly->output_count && assembly->operands[source->match].node < stream->count)
    {
        result = CHECK_ASM_ERROR_TIED;
        *output_type = check_stream_type(stream, assembly->operands[source->match].node);
    }
    return result;
}

// Diagnose only complete original statements before the caller's cutoff and every existing input error (a graph's error
// declines only the statements whose operands read it).
// Clang elaborates every operand before checking asm constraints, so an unresolved later operand in the same
// statement takes precedence. Checked dependency closures and matching output roots are required; missing or
// tainted roots are declined. True means the scan completed, even on a semantic error. Nothing is published.
// `output`, when given, takes the list of each failing statement's error (CheckAsmDiagnostic.failures, #1447): its
// first operand's by clang's order, the operands' own checks before their ties'.
static bool check_asms_diagnose_all(CheckAsmDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                    TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch)
{
    *result = (CheckAsmDiagnostic){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = scratch != NULL ? scratch->length : 0;
    bool valid = input != NULL && stream != NULL && types != NULL && scratch != NULL;
    if (valid)
    {
        valid = !input->resource && stream->status == CHECK_STATUS_OK && stream->count == input->graph.count &&
                (input->report_count == 0 || ((input->diagnostic_graph || input->recovered_valid) && input->reports != NULL)) &&
                (stream->report_count == 0 || stream->reports != NULL) && (input->asm_count == 0 || input->asm_statements != NULL) &&
                (stream->count == 0 || (check_stream_has_values(stream) && stream->diagnostics != NULL && input->graph.tokens != NULL &&
                 input->graph.ops != NULL && input->graph.operand0 != NULL && input->graph.operand1 != NULL && input->graph.operand2 != NULL));
    }
    if (!valid)
    {
        result->status = (input != NULL && input->resource) || (stream != NULL && stream->status == CHECK_STATUS_RESOURCE) ?
                         CHECK_ASM_RESOURCE : CHECK_ASM_INVALID;
    }
    uint32_t cutoff = valid ? input->token_count : 0;
    cutoff = before_token < cutoff ? before_token : cutoff;
    for (uint32_t i = valid ? input->diagnostic_report_count : 0; valid && i < input->report_count; i += 1)
    {
        // A recovered report (#1447) ends nothing: its declaration stayed valid.
        const uint32_t position = input->reports[i].recovered ? cutoff : input->reports[i].completion != 0 ? input->reports[i].completion : input->reports[i].token;
        cutoff = position < cutoff ? position : cutoff;
    }
    // Not the graph's reports: an expression's error ends nothing but what reads it (#1447's semantic recovery, phase 3),
    // which `complete` declines, as clang checks each operand of a statement whose other operands are in error.
    const bool needed = valid && input->asm_count != 0 && stream->count != 0;
    uint8_t* complete = needed ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, stream->count) : NULL;
    if (needed && complete == NULL)
    {
        valid = false;
        result->status = CHECK_ASM_RESOURCE;
    }
    for (uint32_t node = 0; valid && needed && node < stream->count; node += 1)
    {
        const CheckGraph* graph = &input->graph;
        const uint32_t local = check_input_diagnostic_limit(input, graph->tokens[node]);
        const uint32_t limit = local < cutoff ? local : cutoff;
        bool checked = graph->tokens[node] < limit && (graph->source_positions == NULL || graph->source_positions[node] <= limit) &&
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, check_stream_type(stream, node));
        const uint32_t operands[] =
        {
            graph->operand0[node],
            graph->operand1[node],
            graph->operand2[node],
        };
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            checked = operands[i] == BUSTER_CHECK_NONE || (operands[i] < node && complete[operands[i]]);
        }
        if (checked && check_graph_has_arguments(graph->ops[node]))
        {
            checked = graph->argument_offsets != NULL && graph->argument_counts != NULL;
            const uint32_t first = checked ? graph->argument_offsets[node] : 0;
            const uint32_t count = checked ? graph->argument_counts[node] : 0;
            checked = checked && first <= graph->argument_count && count <= graph->argument_count - first && (count == 0 || graph->argument_nodes != NULL);
            for (uint32_t i = 0; checked && i < count; i += 1)
            {
                const uint32_t operand = graph->argument_nodes[first + i];
                checked = operand < node && complete[operand];
            }
        }
        complete[node] = checked;
    }
    // Each operand's at most, as a statement's casts give one each.
    uint64_t operands = 0;
    for (uint32_t statement = 0; valid && output != NULL && statement < input->asm_count; statement += 1)
    {
        operands += (uint64_t)input->asm_statements[statement].output_count + input->asm_statements[statement].input_count;
    }
    CheckAsmFailure* failures = valid && output != NULL && operands != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckAsmFailure, operands) : NULL;
    if (valid && output != NULL && operands != 0 && failures == NULL)
    {
        valid = false;
        result->status = CHECK_ASM_RESOURCE;
    }
    for (uint32_t statement = 0; valid && statement < input->asm_count; statement += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + statement;
        CheckAsmFailure tied = { .token = BUSTER_CHECK_NONE };
        bool ended = false;
        for (uint32_t operand = 0; valid && operand < assembly->output_count + assembly->input_count; operand += 1)
        {
            const CheckAsmOperand* source = assembly->operands + operand;
            bool eligible = assembly->closing < cutoff && assembly->closing < check_input_diagnostic_limit(input, assembly->closing) &&
                            source->closing < cutoff && source->node < stream->count && complete[source->node];
            if (eligible && (source->flags & CHECK_ASM_MATCH) != 0)
            {
                eligible = source->match < assembly->output_count && assembly->operands[source->match].node < stream->count &&
                           complete[assembly->operands[source->match].node];
            }
            if (eligible)
            {
                CheckAsmCheckedOperand checked;
                const CheckAsmStatus status = check_asm_operand(&checked, input, stream, types, statement, operand);
                if (status == CHECK_ASM_RESOURCE || (status != CHECK_ASM_OK && source->opening + 1 < result->token))
                {
                    result->status = status;
                    result->token = source->opening + 1;
                    valid = status != CHECK_ASM_RESOURCE;
                }
                if (valid && failures != NULL && status != CHECK_ASM_OK && !ended)
                {
                    CheckAsmFailure found = {
                        .token = source->opening + 1,
                        .statement = statement,
                        .operand = operand,
                        .node = source->node,
                    };
                    found.error = check_asm_operand_error(input, stream, types, statement, operand, &found.type, &found.output_type, &found.node);
                    // A cast's error doesn't end the statement; its others' do, but a tie's, which waits for them.
                    if (found.error == CHECK_ASM_ERROR_CAST)
                    {
                        failures[result->failure_count++] = found;
                    }
                    else if (found.error != CHECK_ASM_ERROR_TIED)
                    {
                        failures[result->failure_count++] = found;
                        ended = true;
                    }
                    else if (tied.token == BUSTER_CHECK_NONE)
                    {
                        tied = found;
                    }
                }
            }
        }
        if (failures != NULL && !ended && tied.token != BUSTER_CHECK_NONE)
        {
            failures[result->failure_count++] = tied;
        }
    }
    result->failures = valid ? failures : NULL;
    result->failure_count = valid ? result->failure_count : 0;
    if (scratch != NULL)
    {
        arena_pop_to(scratch, mark);
    }
    return valid;
}

static bool check_asms_diagnose(CheckAsmDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                TypeTable* types, uint32_t before_token, Arena* scratch)
{
    return check_asms_diagnose_all(result, input, stream, types, before_token, NULL, scratch);
}

static bool check_asms_run(CheckAsms* result, const CheckInput* input, const CheckStream* stream, TypeTable* types, Arena* output, Arena* scratch)
{
    *result = (CheckAsms){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    uint64_t count = 0;
    bool valid = output != scratch && stream->status == CHECK_STATUS_OK && stream->report_count == 0 && stream->count == input->graph.count;
    for (uint32_t statement = 0; valid && statement < input->asm_count; statement += 1)
    {
        count += (uint64_t)input->asm_statements[statement].output_count + input->asm_statements[statement].input_count;
    }
    result->operands = valid && count != 0 && count <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output, CheckAsmCheckedOperand, count) : NULL;
    if (valid && count != 0 && result->operands == NULL)
    {
        result->status = CHECK_ASM_RESOURCE;
        valid = false;
    }
    for (uint32_t statement = 0; valid && statement < input->asm_count; statement += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + statement;
        for (uint32_t operand = 0; valid && operand < assembly->output_count + assembly->input_count; operand += 1)
        {
            const CheckAsmOperand* source = assembly->operands + operand;
            result->token = source->opening + 1;
            result->status = check_asm_operand(result->operands + result->count, input, stream, types, statement, operand);
            valid = result->status == CHECK_ASM_OK;
            if (valid)
            {
                result->count += 1;
            }
        }
    }
    if (!valid)
    {
        result->status = result->status == CHECK_ASM_RESOURCE ? result->status : CHECK_ASM_INVALID;
        result->operands = NULL;
        result->count = 0;
        arena_pop_to(output, output_mark);
    }
    else
    {
        result->token = BUSTER_CHECK_NONE;
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}
