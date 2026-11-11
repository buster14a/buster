// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An overloadable function's enable_if conditions in its Itanium name (#1447), as clang mangles them: after the name,
// `Ua9enable_ifI`, each condition's template argument, then `E`. Two overloads of one signature whose enable_ifs differ
// are two functions (c_check_overload.c), and this is what names them apart. Each condition's argument is made where
// the reader builds it (check_condition_mangle), from the parse tree, whose literals and constants the reader types: a
// lone literal is its `L<type><value>E`, anything else `X<expression>E` in prefix order, a parameter `fL0p<cv><n>_`
// (from its place and its type's qualifiers), parentheses dropped, an operator's code before its operands, a cast's
// `cv<type>`, an enumerator and a sizeof or an alignof folded to their values' literals. A shape outside these (a call, a
// subscript, a member, a cast to a type other than an arithmetic one, a variable) leaves the condition without one,
// and a function with such an enable_if isn't emitted (check_mangle_name's refusal). Clang is the external oracle of the
// encodings; no clang code is incorporated. Included by c_check_condition.c.

// An arithmetic type's Itanium code, or NULL.
static const char* check_condition_mangle_type(const TypeTable* types, TypeId type)
{
    static const char* const codes[BUSTER_TYPE_CODE_LONG_DOUBLE + 1] = {
        NULL, "v", "b", "c", "a", "h", "s", "t", "i", "j", "l", "m", "x", "y", "n", "o", NULL, NULL, NULL, "f", "d", "e",
    };
    const TypeId bare = type_strip_aligned(types, type);
    const uint8_t kind = type_is_valid(types, bare) ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const uint32_t code = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING ? types->types.code[type_index(bare)] : BUSTER_TYPE_CODE_NONE;
    return type_qualifiers(bare) == 0 && code < BUSTER_ARRAY_LENGTH(codes) ? codes[code] : NULL;
}

// A constant's literal, `L<type><value>E`, into `text` (room for 64 bytes): an integer's value in decimal, `n` before a
// negative one's magnitude, a float's, a double's and a long double's (x87's 80) bits in lowercase hexadecimal. False for a constant of another type.
static bool check_condition_mangle_literal(const TypeTable* types, TypeId type, unsigned __int128 integer, long double floating, char* text)
{
    const char* code = check_condition_mangle_type(types, type);
    const TypeId bare = code != NULL ? type_strip_aligned(types, type) : 0;
    const uint32_t type_code = bare != 0 ? types->types.code[type_index(bare)] : BUSTER_TYPE_CODE_NONE;
    const bool real = type_code == BUSTER_TYPE_CODE_FLOAT || type_code == BUSTER_TYPE_CODE_DOUBLE || type_code == BUSTER_TYPE_CODE_LONG_DOUBLE;
    const bool valid = code != NULL && type_code != BUSTER_TYPE_CODE_VOID && (type_code <= BUSTER_TYPE_CODE_ULLONG || real);
    if (valid && type_code == BUSTER_TYPE_CODE_FLOAT)
    {
        const float value = (float)floating;
        uint32_t bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        snprintf(text, 64, "L%s%08xE", code, bits);
    }
    else if (valid && type_code == BUSTER_TYPE_CODE_DOUBLE)
    {
        const double value = (double)floating;
        uint64_t bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        snprintf(text, 64, "L%s%016llxE", code, (unsigned long long)bits);
    }
    else if (valid && type_code == BUSTER_TYPE_CODE_LONG_DOUBLE)
    {
        // x87's 80 bits: the sign and exponent, then the significand.
        uint64_t storage[2] = { 0, 0 };
        memcpy(storage, &floating, 10);
        snprintf(text, 64, "L%s%04x%016llxE", code, (unsigned)(storage[1] & 0xffffu), (unsigned long long)storage[0]);
    }
    else if (valid)
    {
        const uint32_t width = (uint32_t)type_size(types, bare) * 8;
        const unsigned __int128 mask = width >= 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << width) - 1);
        const unsigned __int128 value = integer & mask;
        const bool is_signed = (types->types.flags[type_index(bare)] & BUSTER_TYPE_FLAG_UNSIGNED) == 0;
        const bool negative = is_signed && width < 128 && width != 0 && ((value >> (width - 1)) & 1) != 0;
        const uint64_t magnitude = (uint64_t)(negative ? ((~value + 1) & mask) : value);
        snprintf(text, 64, "L%s%s%lluE", code, negative ? "n" : "", (unsigned long long)magnitude);
    }
    return valid;
}

// The mangled argument of `condition` [first, end), whose tokens' parameter places are `places` (null for none) and
// function type is `function`, into the arena, or a null spelling where a shape isn't one of these.
static String8 check_condition_mangle(CheckInputReader* reader, const CheckCondition* condition)
{
    const Parser* parser = reader->parser;
    const uint32_t first = condition->first;
    const uint32_t end = condition->end;
    const uint32_t n = end > first ? end - first : 0;
    const uint64_t mark = reader->scratch->length;
    uint32_t* first_child = n != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n) : NULL;
    uint32_t* next_sibling = first_child != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n) : NULL;
    uint32_t* last_child = next_sibling != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n) : NULL;
    uint32_t* stack = last_child != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, n) : NULL;
    // The text, at most 64 bytes for each token's part.
    char* text = stack != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, char, (uint64_t)n * 64 + 8) : NULL;
    reader->output->resource |= n != 0 && text == NULL;
    bool valid = text != NULL;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    for (uint32_t i = 0; valid && i < n; i += 1)
    {
        first_child[i] = BUSTER_CHECK_NONE;
        next_sibling[i] = BUSTER_CHECK_NONE;
        last_child[i] = BUSTER_CHECK_NONE;
    }
    for (uint32_t i = 0; valid && i < n; i += 1)
    {
        const uint32_t parent = parser->tree.parents[first + i];
        if (parent >= first && parent < end && parent != first + i)
        {
            const uint32_t p = parent - first;
            if (last_child[p] == BUSTER_CHECK_NONE)
            {
                first_child[p] = i;
            }
            else
            {
                next_sibling[last_child[p]] = i;
            }
            last_child[p] = i;
        }
        else
        {
            root = i;
            roots += 1;
        }
    }
    valid &= roots == 1;
    // Past the parentheses around the whole, whether it is a lone literal, which takes no `X...E`.
    uint32_t bare = root;
    while (valid && parser->tree.roles[first + bare] == PARSE_ROLE_CLOSE_GROUP)
    {
        uint32_t inner = BUSTER_CHECK_NONE;
        for (uint32_t c = first_child[bare]; c != BUSTER_CHECK_NONE; c = next_sibling[c])
        {
            inner = parser->tokens.kinds[first + c] != PARSE_KIND_L_PAREN || first_child[c] != BUSTER_CHECK_NONE ? c : inner;
        }
        valid = inner != BUSTER_CHECK_NONE;
        bare = valid ? inner : bare;
    }
    const uint8_t bare_kind = valid ? parser->tokens.kinds[first + bare] : PARSE_KIND_END;
    // (A lone enumerator, which clang's mangling gives as its value's literal, is one too.)
    const bool literal = (bare_kind == PARSE_KIND_NUMBER || bare_kind == PARSE_KIND_CHARACTER ||
                          (bare_kind == PARSE_KIND_IDENTIFIER && (condition->places == NULL || condition->places[bare] == BUSTER_CHECK_NONE))) &&
                         first_child[bare] == BUSTER_CHECK_NONE;
    uint32_t length = 0;
    if (valid && !literal)
    {
        text[length++] = 'X';
    }
    // Prefix order: each node's code, then its operands, from an explicit stack.
    uint32_t depth = 0;
    if (valid)
    {
        stack[depth++] = root;
    }
    while (valid && depth != 0)
    {
        const uint32_t v = stack[--depth];
        const uint32_t token = first + v;
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        uint32_t operands[3] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t operand_count = 0;
        uint32_t opener = BUSTER_CHECK_NONE;
        for (uint32_t c = first_child[v]; c != BUSTER_CHECK_NONE; c = next_sibling[c])
        {
            const uint8_t child_kind = parser->tokens.kinds[first + c];
            const bool open = (child_kind == PARSE_KIND_L_PAREN && first_child[c] == BUSTER_CHECK_NONE) || child_kind == PARSE_KIND_QUESTION;
            opener = open && child_kind == PARSE_KIND_L_PAREN ? c : opener;
            if (!open && operand_count < 3)
            {
                operands[operand_count] = c;
            }
            operand_count += open ? 0u : 1u;
        }
        char part[64];
        part[0] = 0;
        uint32_t pushed = 0;
        const uint32_t name = parser->tokens.names[token];
        const bool unevaluated = (name == C_NAME_SIZEOF || name == C_NAME_ALIGNOF || name == C_NAME_ALIGNOF_C11 || name == C_NAME_GNU_ALIGNOF ||
                                  name == C_NAME_GNU_ALIGNOF2) && first_child[v] != BUSTER_CHECK_NONE;
        if (kind == PARSE_KIND_IDENTIFIER && first_child[v] == BUSTER_CHECK_NONE && condition->places != NULL && condition->places[v] != BUSTER_CHECK_NONE)
        {
            // A parameter: its place and its declared type's top-level qualifiers (the function type's are adjusted).
            uint32_t count = 0;
            type_parameters(reader->types, condition->function, &count);
            const uint32_t place = condition->places[v];
            const uint32_t declaration = reader->output->declarations[token];
            const uint32_t qualifiers = declaration < parser->tokens.count ? type_qualifiers(reader->declared_types[declaration]) : 0;
            valid = place < count && declaration < parser->tokens.count;
            char index[16];
            index[0] = 0;
            if (place != 0)
            {
                snprintf(index, sizeof(index), "%u", place - 1);
            }
            snprintf(part, sizeof(part), "fL0p%s%s%s%s_", (qualifiers & BUSTER_TYPE_RESTRICT) != 0 ? "r" : "", (qualifiers & BUSTER_TYPE_VOLATILE) != 0 ? "V" : "",
                     (qualifiers & BUSTER_TYPE_CONST) != 0 ? "K" : "", index);
        }
        else if ((kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER) && first_child[v] == BUSTER_CHECK_NONE)
        {
            CheckLiteral read;
            check_input_literal(reader, token, &read);
            valid = read.valid && read.bit_width == 0 && !read.imaginary &&
                    check_condition_mangle_literal(reader->types, read.type, read.integer, read.floating, part);
        }
        else if ((kind == PARSE_KIND_IDENTIFIER && first_child[v] == BUSTER_CHECK_NONE) || unevaluated)
        {
            // An enumerator, or a sizeof's or an alignof's value: its constant's literal. A keyword's subtree is the run
            // from it of the tokens it is above.
            uint32_t high = v;
            bool below = unevaluated;
            for (uint32_t t = v + 1; below && t < n; t += 1)
            {
                uint32_t at = first + t;
                while (at != first + v && parser->tree.parents[at] >= first && parser->tree.parents[at] < end && parser->tree.parents[at] != at)
                {
                    at = parser->tree.parents[at];
                }
                below = at == first + v;
                high = below ? t : high;
            }
            CheckValue value = { 0 };
            TypeId unused = 0;
            const bool read = check_input_constant_read(reader, first + v, first + high + 1, false, false, true, &value, &unused, NULL, NULL) &&
                              check_input_const_rvalue(reader, first + high + 1, &value);
            valid = read && value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE &&
                    check_condition_mangle_literal(reader->types, value.type, value.integer, 0, part);
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP && operand_count == 1)
        {
            stack[depth++] = operands[0];
        }
        else if (role == PARSE_ROLE_CLOSE_CAST && opener != BUSTER_CHECK_NONE && operand_count != 0 && operand_count <= 3 && operands[operand_count - 1] > v)
        {
            const char* code = check_condition_mangle_type(reader->types, check_input_constant_type(reader, first + opener + 1, token));
            valid = code != NULL;
            snprintf(part, sizeof(part), "cv%s", valid ? code : "");
            stack[depth++] = operands[operand_count - 1];
        }
        else if (kind == PARSE_KIND_COLON && operand_count == 3)
        {
            snprintf(part, sizeof(part), "qu");
            pushed = 3;
        }
        else if (opener == BUSTER_CHECK_NONE && operand_count == 1)
        {
            const char* code = kind == PARSE_KIND_MINUS ? "ng" : kind == PARSE_KIND_PLUS ? "ps" : kind == PARSE_KIND_EXCLAIM ? "nt" : kind == PARSE_KIND_TILDE ? "co" :
                               kind == PARSE_KIND_AMP ? "ad" : kind == PARSE_KIND_STAR ? "de" : NULL;
            valid = code != NULL && operands[0] > v;
            snprintf(part, sizeof(part), "%s", valid ? code : "");
            pushed = 1;
        }
        else if (opener == BUSTER_CHECK_NONE && operand_count == 2)
        {
            const char* code = NULL;
            switch (kind)
            {
                case PARSE_KIND_STAR: code = "ml"; break;
                case PARSE_KIND_SLASH: code = "dv"; break;
                case PARSE_KIND_PERCENT: code = "rm"; break;
                case PARSE_KIND_PLUS: code = "pl"; break;
                case PARSE_KIND_MINUS: code = "mi"; break;
                case PARSE_KIND_LESSLESS: code = "ls"; break;
                case PARSE_KIND_GREATERGREATER: code = "rs"; break;
                case PARSE_KIND_LESS: code = "lt"; break;
                case PARSE_KIND_LESSEQUAL: code = "le"; break;
                case PARSE_KIND_GREATER: code = "gt"; break;
                case PARSE_KIND_GREATEREQUAL: code = "ge"; break;
                case PARSE_KIND_EQUALEQUAL: code = "eq"; break;
                case PARSE_KIND_EXCLAIMEQUAL: code = "ne"; break;
                case PARSE_KIND_AMP: code = "an"; break;
                case PARSE_KIND_PIPE: code = "or"; break;
                case PARSE_KIND_CARET: code = "eo"; break;
                case PARSE_KIND_AMPAMP: code = "aa"; break;
                case PARSE_KIND_PIPEPIPE: code = "oo"; break;
                case PARSE_KIND_COMMA: code = "cm"; break;
                default: break;
            }
            valid = code != NULL;
            snprintf(part, sizeof(part), "%s", valid ? code : "");
            pushed = 2;
        }
        else
        {
            valid = false;
        }
        for (uint32_t k = pushed; valid && k > 0; k -= 1)
        {
            stack[depth++] = operands[k - 1];
        }
        const uint32_t part_length = valid ? (uint32_t)strlen(part) : 0;
        memcpy(text + length, part, part_length);
        length += part_length;
    }
    if (valid && !literal)
    {
        text[length++] = 'E';
    }
    char* kept = valid ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, char, length) : NULL;
    reader->output->resource |= valid && kept == NULL;
    if (kept != NULL)
    {
        memcpy(kept, text, length);
    }
    arena_pop_to(reader->scratch, mark);
    return kept != NULL ? (String8){ .pointer = kept, .length = length } : (String8){ 0 };
}
