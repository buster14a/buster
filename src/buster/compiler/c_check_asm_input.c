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

static bool check_asm_register(CheckAsmText text)
{
    static const char* const names[] =
    {
        "al",
        "ah",
        "ax",
        "eax",
        "rax",
        "bl",
        "bh",
        "bx",
        "ebx",
        "rbx",
        "cl",
        "ch",
        "cx",
        "ecx",
        "rcx",
        "dl",
        "dh",
        "dx",
        "edx",
        "rdx",
        "si",
        "sil",
        "esi",
        "rsi",
        "di",
        "dil",
        "edi",
        "rdi",
        "sp",
        "spl",
        "esp",
        "rsp",
        "bp",
        "bpl",
        "ebp",
        "rbp",
        "st",
        "flags",
        "dirflag",
        "fpsr",
        "fpcr",
    };
    bool valid = false;
    if (text.length != 0 && (text.bytes[0] == '%' || text.bytes[0] == '#'))
    {
        text.bytes += 1;
        text.length -= 1;
    }
    for (uint32_t i = 0; !valid && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        valid = check_asm_text_equal(text, names[i]);
    }
    if (!valid && text.length == 5 && memcmp(text.bytes, "st(", 3) == 0 && text.bytes[3] >= '1' && text.bytes[3] <= '7' && text.bytes[4] == ')')
    {
        valid = true;
    }
    uint32_t prefix = 0;
    uint32_t limit = 0;
    uint32_t minimum = 0;
    bool general = false;
    if (text.length >= 2 && text.bytes[0] == 'r')
    {
        prefix = 1;
        minimum = 8;
        limit = 31;
        general = true;
    }
    else if (text.length >= 2 && text.bytes[0] == 'k')
    {
        prefix = 1;
        limit = 7;
    }
    else if (text.length >= 3 && memcmp(text.bytes, "mm", 2) == 0)
    {
        prefix = 2;
        limit = 7;
    }
    else if (text.length >= 4 && memcmp(text.bytes, "tmm", 3) == 0)
    {
        prefix = 3;
        limit = 7;
    }
    else if (text.length >= 4 && (text.bytes[0] == 'x' || text.bytes[0] == 'y' || text.bytes[0] == 'z') && memcmp(text.bytes + 1, "mm", 2) == 0)
    {
        prefix = 3;
        limit = 31;
    }
    if (!valid && prefix != 0)
    {
        uint32_t number = 0;
        uint32_t cursor = prefix;
        while (cursor < text.length && text.bytes[cursor] >= '0' && text.bytes[cursor] <= '9' && number <= limit)
        {
            number = number * 10 + text.bytes[cursor++] - '0';
        }
        const bool leading_zero = cursor > prefix + 1 && text.bytes[prefix] == '0';
        if (general && cursor + 1 == text.length && (text.bytes[cursor] == 'b' || text.bytes[cursor] == 'w' || text.bytes[cursor] == 'd'))
        {
            cursor += 1;
        }
        valid = cursor == text.length && cursor > prefix && number >= minimum && number <= limit && !leading_zero;
    }
    return valid;
}

static void check_input_asm_mark(CheckInputReader* reader, uint32_t begin, uint32_t end, uint32_t id)
{
    for (uint32_t token = begin; token < end; token += 1)
    {
        reader->ignored[token] = true;
        reader->output->asm_ids[token] = id;
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
        const CheckStringLiteral literal = check_literal_strings(parts, after - begin, parser->language.standard >= 2023, reader->arena);
        reader->output->resource |= literal.resource;
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
            valid = !output;
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
                    valid = memchr(clobber->bytes, 0, clobber->length) == NULL &&
                            (check_asm_text_equal(*clobber, "memory") || check_asm_text_equal(*clobber, "cc") || check_asm_register(*clobber));
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
            valid = check_asm_constraint(statement, operand, reader->pp->table, &current) && (operand == 0 || current == alternatives);
            alternatives = current;
            for (uint32_t before = 0; valid && operands[operand].name != 0 && before < operand; before += 1)
            {
                valid = operands[before].name != operands[operand].name;
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
    if (!valid)
    {
        check_input_report(reader, failed, CHECK_INPUT_DECLARATION);
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

// Of the next 8 tokens from `first` below `end`, those with attributes: few have them. The scalar path takes them all.
static uint32_t check_input_asm_attributed(const CheckInput* input, uint32_t first, uint32_t end, bool scalar)
{
    (void)scalar;

    return BUSTER_SCALAR_PATH(scalar) ? (uint32_t)check_input_scan_all(first, end) & 0xff : check_input_scan_pointers((const void* const*)input->attributes, first, end);
}

// Of the next 16 tokens from `first` below `end`, those with a declaration: identifiers. The scalar path takes them all.
static uint32_t check_input_asm_declared(const CheckInput* input, uint32_t first, uint32_t end, bool scalar)
{
    (void)scalar;

    return BUSTER_SCALAR_PATH(scalar) ? (uint32_t)check_input_scan_all(first, end) & 0xffff : check_input_scan_below(input->declarations, first, end, input->token_count);
}

static void check_input_asm_labels(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    input->asm_warnings_prepared = true;
    const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar);
    const uint64_t mark = reader->scratch->length;
    const uint64_t slots = (uint64_t)input->token_count + 1;
    const uint32_t limit = reader->expression_limit;
    bool present = false;
    for (uint32_t first = 0; !present && input->asm_capacity != 0 && first < limit; first += 8)
    {
        for (uint32_t selected = check_input_asm_attributed(input, first, limit, scalar); selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            const CheckInputAttributes* attributes = !input->registers[token] ? input->attributes[token] : NULL;
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                present |= attributes->records[i].kind == CHECK_ATTRIBUTE_ASM_LABEL && attributes->records[i].end <= limit;
            }
        }
    }
    const CheckInputAttribute** labels = present ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, const CheckInputAttribute*, slots) : NULL;
    uint8_t* states = labels != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, slots) : NULL;
    input->resource |= present && states == NULL;
    if (states != NULL)
    {
        memset(labels, 0, slots * sizeof(*labels));
        memset(states, 0, slots);
        // Bit 0 marks only canonical objects with labels, bit 1 a preceding use and bit 2 a real definition.
        // Tentative definitions deliberately do not make a later first label an ignored attribute.
        for (uint32_t first = 0; first < limit; first += 8)
        {
            for (uint32_t selected = check_input_asm_attributed(input, first, limit, scalar); selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const CheckInputAttributes* attributes = !input->registers[token] ? input->attributes[token] : NULL;
                const uint32_t object = input->objects[token];
                for (uint32_t i = 0; object != 0 && object < slots && attributes != NULL && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->end <= limit)
                    {
                        states[object] |= 1;
                    }
                }
            }
        }
        for (uint32_t first = 0; first < limit && !input->resource; first += 16)
        {
            for (uint32_t selected = check_input_asm_declared(input, first, limit, scalar); selected != 0 && !input->resource; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t declaration = input->declarations[token];
                const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
                if (object != 0 && object < slots && (states[object] & 1) != 0)
                {
                    if (declaration == token)
                    {
                        CheckInputAttributes* attributes = !input->registers[token] ? input->attributes[token] : NULL;
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
                                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
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
                        states[object] |= input->defined[token] ? 4 : 0;
                    }
                    else if (reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND && check_input_asm_label_use(reader, token))
                    {
                        states[object] |= 2;
                    }
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}

static void check_input_asm_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    check_input_asm_labels(reader);
    for (uint32_t first = 0; input->asm_capacity != 0 && first < reader->expression_limit && !input->resource; first += 8)
    {
        for (uint32_t selected = check_input_asm_attributed(input, first, reader->expression_limit, BUSTER_SCALAR_PATH(parser->options.scalar)); selected != 0 && !input->resource;
             selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            const CheckInputAttributes* attributes = input->registers[token] ? input->attributes[token] : NULL;
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && attribute->end <= reader->expression_limit)
                {
                    const CheckAsmText name =
                    {
                        .bytes = (const uint8_t*)attribute->text.pointer,
                        .length = attribute->text.length,
                    };
                    if (!check_asm_register(name))
                    {
                        check_input_report(reader, attribute->argument_first, CHECK_INPUT_DECLARATION);
                    }
                }
            }
        }
    }
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
                    const uint32_t name = reader->first_child[token];
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

static CheckAsmStatus check_asm_operand(CheckAsmCheckedOperand* result, const CheckInput* input, const CheckStream* stream,
                                        TypeTable* types, uint32_t statement, uint32_t operand)
{
    const CheckInputAsm* assembly = input->asm_statements + statement;
    const CheckAsmOperand* source = assembly->operands + operand;
    bool valid = source->node < stream->count;
    CheckActionResult checked = { 0 };
    CheckValue value = { 0 };
    if (valid)
    {
        const CheckValue original = stream->values[source->node];
        CheckAction action =
        {
            .node = source->node,
            .tag_completions = input->graph.tag_completions,
            .tag_completion_count = input->graph.tag_completion_count,
            .source_position = input->graph.source_positions != NULL ? input->graph.source_positions[source->node] : BUSTER_CHECK_NONE,
            .extensions = input->graph.extensions,
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
        if (valid && (source->flags & CHECK_ASM_FLAG) != 0)
        {
            uint32_t width = 0;
            bool sign = false;
            valid = check_action_integer_type(types, original.type, &width, &sign);
        }
        if (valid && (source->flags & CHECK_ASM_MATCH) != 0)
        {
            valid = source->match < assembly->output_count && assembly->operands[source->match].node < stream->count;
            if (valid)
            {
                const CheckValue target = stream->values[assembly->operands[source->match].node];
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

typedef struct CheckAsmDiagnostic CheckAsmDiagnostic;
struct CheckAsmDiagnostic
{
    CheckAsmStatus status;
    uint32_t token;
};

// Diagnose only complete original statements before the caller's cutoff and every existing input/graph error.
// Clang elaborates every operand before checking asm constraints, so an unresolved later operand in the same
// statement takes precedence. Checked dependency closures and matching output roots are required; missing or
// tainted roots are declined. True means the scan completed, even on a semantic error. Nothing is published.
static bool check_asms_diagnose(CheckAsmDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                TypeTable* types, uint32_t before_token, Arena* scratch)
{
    *result = (CheckAsmDiagnostic){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = scratch != NULL ? scratch->length : 0;
    bool valid = input != NULL && stream != NULL && types != NULL && scratch != NULL;
    if (valid)
    {
        valid = !input->resource && stream->status == CHECK_STATUS_OK && stream->count == input->graph.count &&
                (input->report_count == 0 || (input->diagnostic_graph && input->reports != NULL)) &&
                (stream->report_count == 0 || stream->reports != NULL) && (input->asm_count == 0 || input->asm_statements != NULL) &&
                (stream->count == 0 || (stream->values != NULL && stream->diagnostics != NULL && input->graph.tokens != NULL &&
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
        const uint32_t position = input->reports[i].completion != 0 ? input->reports[i].completion : input->reports[i].token;
        cutoff = position < cutoff ? position : cutoff;
    }
    for (uint32_t i = 0; valid && i < stream->report_count; i += 1)
    {
        cutoff = stream->reports[i].token < cutoff ? stream->reports[i].token : cutoff;
    }
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
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, stream->values[node].type);
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
    for (uint32_t statement = 0; valid && statement < input->asm_count; statement += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + statement;
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
            }
        }
    }
    if (scratch != NULL)
    {
        arena_pop_to(scratch, mark);
    }
    return valid;
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
