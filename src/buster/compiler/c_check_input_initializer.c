// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Initializer syntax stays separate from expression actions. Resolve designator ICEs after the declaration's
// activation point, retain their value for the final initializer planner, and finish an outer array at the end of
// its initializer. Structural preparation emits no future binding events; the source pass executes each bound
// job at its actual opener. The token scan skips expression groups and uses no recursive descent.

static bool check_input_initializer_prepare_slow(CheckInputReader* reader, uint32_t assignment)
{
    const Parser* parser = reader->parser;
    const uint32_t declaration = reader->initializer_declarations[assignment];
    const uint32_t begin = reader->output->initializer_begins[declaration];
    const uint32_t end = reader->output->initializer_ends[declaration];
    TypeId type = reader->declared_types[declaration];
    bool valid = begin < end && end <= parser->tokens.count;
    bool item = true;
    for (uint32_t token = begin; valid && token < end;)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_CLOSE_INITIALIZER ||
            (role == PARSE_ROLE_OPEN_COMPOUND_BODY && token == begin) || (role == PARSE_ROLE_CLOSE_COMPOUND_BODY && token + 1 == end) || kind == PARSE_KIND_COMMA)
        {
            item = true;
            token += 1;
        }
        else if (item && kind == PARSE_KIND_PERIOD)
        {
            valid = token + 1 < end && parser->tokens.kinds[token + 1] == PARSE_KIND_IDENTIFIER;
            reader->ignored[token] = true;
            if (valid)
            {
                reader->ignored[token + 1] = true;
                token += 2;
            }
        }
        else if (item && kind == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[token];
            uint32_t range = close;
            for (uint32_t at = token + 1; at < close && close < end; at += 1)
            {
                if (parser->tokens.kinds[at] == PARSE_KIND_ELLIPSIS)
                {
                    range = at;
                    break;
                }
                if (parser->tokens.kinds[at] == PARSE_KIND_L_PAREN || parser->tokens.kinds[at] == PARSE_KIND_L_SQUARE)
                {
                    at = parser->tree.parents[at];
                }
            }
            valid = close > token && close < end;
            if (valid)
            {
                check_input_rare_claim(reader, &reader->designator_indices[token]);
                reader->designator_indices[token] = close;
                check_input_rare_claim(reader, &reader->designator_valid[token]);
                reader->designator_valid[token] = 2;
                check_input_visit(reader, token, BUSTER_CHECK_RESOLVE_QUERY);
                check_input_rare_claim(reader, &reader->designator_ranges[token]);
                reader->designator_ranges[token] = range;
                memset(reader->ignored + token, 1, close - token + 1);
                token = close + 1;
            }
        }
        else if (item && kind == PARSE_KIND_EQUAL)
        {
            reader->initializers[token] = 2;
            item = false;
            token += 1;
        }
        else
        {
            uint32_t embed_length = 0;
            const uint8_t* embed = parse_embed_bytes(parser, token, &embed_length);
            if (embed != NULL && token > begin && token + 1 < end &&
                (parser->tokens.kinds[token - 1] == PARSE_KIND_L_BRACE || parser->tokens.kinds[token - 1] == PARSE_KIND_COMMA ||
                 parser->tokens.kinds[token - 1] == PARSE_KIND_EQUAL) &&
                (parser->tokens.kinds[token + 1] == PARSE_KIND_COMMA || parser->tokens.kinds[token + 1] == PARSE_KIND_R_BRACE))
            {
                reader->initializers[token] = 3;
            }
            item = false;
            if (kind == PARSE_KIND_STRING)
            {
                valid = check_input_string(reader, token) != 0;
            }
            if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
            {
                const uint32_t close = parser->tree.parents[token];
                valid = close > token && close < end;
                token = valid ? close + 1 : end;
            }
            else
            {
                token += 1;
            }
        }
    }
    const bool incomplete = type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    if (valid && (incomplete || check_const_eligible(reader->types, type) || check_input_auto_type(type) ||
                  (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_DEDUCED) != 0))
    {
        check_input_rare_claim(reader, &reader->incomplete_array_types[declaration]);
        reader->incomplete_array_types[declaration] = incomplete ? type : 0;
        check_input_rare_claim(reader, &reader->initializer_finish_next[declaration]);
        reader->initializer_finish_next[declaration] = reader->initializer_finish_heads[end];
        reader->initializer_finish_heads[end] = declaration;
        check_input_visit(reader, end, BUSTER_CHECK_RESOLVE_FINISH);
        reader->inference_end = end > reader->inference_end ? end : reader->inference_end;
    }
    if (!valid)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_initializer_prepare(CheckInputReader* reader, uint32_t assignment)
{
    const bool compact = check_input_literal_table_prepare(reader, assignment);
    const bool valid = compact || (!reader->output->resource && check_input_initializer_prepare_slow(reader, assignment));
    return valid;
}

static bool check_input_initializer_designator(CheckInputReader* reader, uint32_t token)
{
    const uint32_t close = (uint32_t)reader->designator_indices[token];
    const uint32_t range = reader->designator_ranges[token];
    int64_t index = 0;
    int64_t high = 0;
    bool valid = check_input_constant(reader, token + 1, range, &index) && index >= 0;
    valid = valid && (range == close || (check_input_constant(reader, range + 1, close, &high) && high >= index));
    if (valid)
    {
        check_input_rare_claim(reader, &reader->designator_indices[token]);
        reader->designator_indices[token] = (uint64_t)index;
        check_input_rare_claim(reader, &reader->designator_valid[token]);
        reader->designator_valid[token] = 1;
        check_input_rare_claim(reader, &reader->designator_tokens[range]);
        reader->designator_tokens[range] = token + 1;
        if (range != close)
        {
            check_input_rare_claim(reader, &reader->designator_indices[range]);
            reader->designator_indices[range] = (uint64_t)high;
            check_input_rare_claim(reader, &reader->designator_valid[range]);
            reader->designator_valid[range] = 1;
            check_input_rare_claim(reader, &reader->designator_tokens[close]);
            reader->designator_tokens[close] = range + 1;
        }
    }
    else
    {
        check_input_report(reader, token + 1, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

#include "c_check_input_const_values.c"

// An __auto_type declaration's type, once the walk has read its initializer (whose uses' declarations it captured:
// inference_end), as clang deduces it: the initializer's, converted as an lvalue's (its qualifiers dropped, but not
// _Atomic) and decayed, with the specifiers' const and volatile. A use of the variable in its initializer is an error
// there, a bit-field's value one at the member's name, an incomplete type one at the variable's name, and an _Alignas
// below the type's own alignment one at the _Alignas (the specifiers and the declarator left it to this); a variably
// modified type, whose sizes the declaration would have evaluated, is refused by name. The declaration's columns take the
// type, which is returned, or 0.
static __attribute__((noinline)) TypeId check_input_auto_type_finish(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end,
                                                                     TypeId placeholder)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t assignment = begin - 1;
    const bool c23 = (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_DEDUCED) != 0;
    uint32_t self = BUSTER_CHECK_NONE;
    for (uint32_t token = begin; token < end && self == BUSTER_CHECK_NONE; token += 1)
    {
        self = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && reader->output->declarations[token] == declaration ? token : BUSTER_CHECK_NONE;
    }
    CheckValue value = { 0 };
    uint32_t failed = BUSTER_CHECK_NONE;
    const bool evaluated = self == BUSTER_CHECK_NONE && check_input_evaluate_cached_at(reader, begin, end, &value, &failed);
    TypeId result = 0;
    if (self != BUSTER_CHECK_NONE)
    {
        check_input_report(reader, self, c23 ? CHECK_INPUT_AUTO_DEDUCED_SELF : CHECK_INPUT_AUTO_TYPE_SELF);
    }
    else if (!evaluated)
    {
        // An identifier the walk found no declaration for is clang's undeclared one.
        const bool undeclared = failed < parser->tokens.count && parser->tokens.kinds[failed] == PARSE_KIND_IDENTIFIER &&
                                reader->output->declarations[failed] == BUSTER_CHECK_NONE;
        if (!reader->output->resource)
        {
            check_input_report(reader, failed < parser->tokens.count ? failed : begin, undeclared ? CHECK_INPUT_UNDECLARED : CHECK_INPUT_DECLARATION);
        }
    }
    else if ((value.flags & BUSTER_CHECK_BIT_FIELD) != 0)
    {
        uint32_t root = reader->first_child[assignment];
        while (root != BUSTER_CHECK_NONE && root < assignment)
        {
            root = reader->next_child[root];
        }
        const bool member = root < end && (parser->tokens.kinds[root] == PARSE_KIND_PERIOD || parser->tokens.kinds[root] == PARSE_KIND_ARROW);
        check_input_report(reader, member ? reader->ends[root] : begin, c23 ? CHECK_INPUT_AUTO_DEDUCED_BIT_FIELD : CHECK_INPUT_AUTO_TYPE_BIT_FIELD);
    }
    else
    {
        TypeId deduced_base = 0;
        TypeId deduced = c23 ? check_input_type_result(reader, check_input_c23_auto_deduce(reader, placeholder, value.type, declaration, &deduced_base)) :
                               check_input_type_result(reader, type_adjust_parameter(types, value.type));
        if (deduced == 0 && !reader->output->resource && reader->output->report_count == 0)
        {
            // A declarator shape its initializer doesn't fit, at clang's name for it.
            check_input_report(reader, declaration, CHECK_INPUT_DECLARATION);
        }
        if (c23 && deduced != 0)
        {
            // C23's later declarators deduce the same type (6.7.9): clang reports the difference at the declaration.
            const uint32_t group = reader->declaration_specifiers[declaration];
            const TypeId previous = group < parser->tokens.count ? reader->declaration_deduced[group] : 0;
            if (previous != 0 && type_same_canonical(types, previous, deduced_base) != TYPE_ANSWER_YES)
            {
                // At the declaration's auto, where clang points its two deductions' error.
                check_input_report(reader, group < parser->tokens.count ? group : declaration, CHECK_INPUT_AUTO_DEDUCED_MISMATCH);
                deduced = 0;
            }
            else if (previous == 0 && group < parser->tokens.count)
            {
                check_input_rare_claim(reader, &reader->declaration_deduced[group]);
                reader->declaration_deduced[group] = deduced_base;
            }
        }
        CheckInputAttributes* attributes = reader->output->attributes[declaration];
        const uint32_t required = attributes != 0 ? attributes->required_alignment_log2 : 0;
        if (deduced != 0 && !type_is_complete(types, deduced))
        {
            check_input_report(reader, declaration, CHECK_INPUT_INCOMPLETE_OBJECT);
        }
        else if (deduced != 0 && (type_inherited_flags(types, deduced) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) != 0)
        {
            check_input_report(reader, declaration, CHECK_INPUT_UNSUPPORTED);
        }
        else if (deduced != 0 && required != 0 && required - 1 < type_alignment_log2(types, deduced))
        {
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = &attributes->records[i];
                if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_ALIGNMENT);
                }
            }
        }
        else if (deduced != 0)
        {
            result = c23 ? deduced : type_qualify(types, deduced, type_qualifiers(placeholder) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
            reader->declared_types[declaration] = result;
            reader->query_types[assignment] = result;
            reader->initializers[assignment] = check_init_aggregate(types, result) ? 2 : 1;
            if (reader->linkage[declaration] != 0)
            {
                const uint32_t name = parser->tokens.names[declaration];
                reader->linked_types[name] = result;
                if (reader->deferred != 0)
                {
                    check_deferred_linked(reader, name, declaration);
                }
            }
        }
    }
    return result;
}

// Whether a constexpr initializer's value is exactly representable in its declared integer type (C23 6.7.1): clang
// refuses `constexpr char c = 300`, `constexpr unsigned u = -1` and `constexpr int y = 2147483648`.
static bool check_input_constexpr_integer_fits(const TypeTable* types, const CheckValue* value, uint32_t width, bool is_signed)
{
    uint32_t source_width = 0;
    bool source_signed = false;
    bool result = true;
    if (check_action_integer_type(types, value->type, &source_width, &source_signed) && source_width != 0 && source_width <= 128 && width != 0 && width <= 128)
    {
        const unsigned __int128 source_mask = source_width == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << source_width) - 1);
        const unsigned __int128 raw = value->integer & source_mask;
        if (!source_signed)
        {
            result = is_signed ? raw <= (((unsigned __int128)1 << (width - 1)) - 1) : width == 128 || raw <= (((unsigned __int128)1 << width) - 1);
        }
        else if ((raw & ((unsigned __int128)1 << (source_width - 1))) == 0)
        {
            result = is_signed ? raw <= (((unsigned __int128)1 << (width - 1)) - 1) : width == 128 || raw <= (((unsigned __int128)1 << width) - 1);
        }
        else
        {
            const unsigned __int128 magnitude = (~raw + 1) & source_mask;
            result = is_signed && magnitude <= ((unsigned __int128)1 << (width - 1));
        }
    }
    return result;
}

// Its floating type: `constexpr float f = 0.1` is refused because 0.1 isn't a float, and `constexpr double d = 0.1f`
// is taken because every float is a double. The half-precision formats and __float128 keep their existing conversion
// rules rather than claim an exactness this doesn't compute.
static bool check_input_constexpr_float_fits(const TypeTable* types, const CheckValue* value, TypeId type)
{
    long double source = 0.0L;
    bool known = value->constant_kind == CHECK_CONSTANT_FLOATING;
    if (value->constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t width = 0;
        bool is_signed = false;
        if (check_action_integer_type(types, value->type, &width, &is_signed) && width != 0 && width <= 128)
        {
            const unsigned __int128 mask = width == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << width) - 1);
            const unsigned __int128 raw = value->integer & mask;
            const unsigned __int128 sign = (unsigned __int128)1 << (width - 1);
            source = is_signed && (raw & sign) != 0 ? (long double)(__int128)(raw | ~mask) : (long double)(is_signed ? (__int128)raw : raw);
            known = true;
        }
    }
    else if (known)
    {
        source = value->floating;
    }
    bool result = true;
    if (known)
    {
        const uint8_t code = type_arithmetic_code(types, type);
        if (code == BUSTER_TYPE_CODE_FLOAT)
        {
            const float converted = (float)source;
            result = (long double)converted == source;
        }
        else if (code == BUSTER_TYPE_CODE_DOUBLE)
        {
            const double converted = (double)source;
            result = (long double)converted == source;
        }
    }
    return result;
}

// C23's constexpr (#793): the declared object is const-qualified, its initializer must be a constant expression, and
// a pointer's must be null. The value a scalar, floating, complex or pointer object's initializer folds to is kept for
// its uses' constant readings; an aggregate's initializer keeps no value, but each of its expressions must be
// constant too.
static __attribute__((noinline)) bool check_input_constexpr_finish(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end,
                                                                   TypeId* type)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t assignment = reader->output->compound_objects[declaration] ? declaration : begin - 1;
    TypeId qualified = check_input_type_result(reader, type_qualify(types, *type, BUSTER_TYPE_CONST));
    bool valid = qualified != 0;
    if (valid)
    {
        *type = qualified;
        reader->declared_types[declaration] = qualified;
        reader->query_types[assignment] = qualified;
        if (reader->linkage[declaration] != 0)
        {
            const uint32_t name = parser->tokens.names[declaration];
            reader->linked_types[name] = qualified;
            if (reader->deferred != 0)
            {
                check_deferred_linked(reader, name, declaration);
            }
        }
    }
    const uint8_t kind = valid ? type_kind(types, type_strip_aligned(types, qualified)) : BUSTER_TYPE_KIND_VOID;
    const bool aggregate = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || type_kind_is_array(kind);
    if (valid && !aggregate)
    {
        CheckValue value = { 0 };
        uint32_t failed = BUSTER_CHECK_NONE;
        const bool evaluated = check_input_evaluate_cached_at(reader, begin, end, &value, &failed);
        // C23's constexpr takes a constant expression: an integer one where the object's type is an integer or
        // enumeration, and a floating, complex or arithmetic one otherwise. A non-const object's folded initializer is
        // GNU's constant form, not an integer constant expression.
        const bool integer_object = check_action_is_integer(types, qualified);
        const bool constant = evaluated && value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                              (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                              (!integer_object || (value.flags & BUSTER_CHECK_ICE) != 0);
        if (!evaluated)
        {
            if (!reader->output->resource)
            {
                check_input_report_subject(reader, failed < parser->tokens.count ? failed : begin, CHECK_INPUT_CONSTEXPR_INITIALIZER, declaration);
            }
            valid = false;
        }
        else if (kind == BUSTER_TYPE_KIND_POINTER)
        {
            // C23 6.7.1: a constexpr pointer is initialized with a null pointer constant, or nullptr.
            const bool null = constant && ((value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == 0 &&
                                            (value.flags & BUSTER_CHECK_ICE) != 0) ||
                                           (value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object == 0 && value.offset == 0) ||
                                           (value.type != 0 && type_kind(types, type_strip_aligned(types, value.type)) == BUSTER_TYPE_KIND_NULLPTR));
            if (!null)
            {
                check_input_report(reader, declaration, CHECK_INPUT_CONSTEXPR_POINTER);
                valid = false;
            }
            else
            {
                // The null pointer constant's value, whatever spelling gave it.
                value = (CheckValue){ .type = qualified, .category = CHECK_LVALUE, .flags = BUSTER_CHECK_ICE, .constant_kind = CHECK_CONSTANT_INTEGER };
            }
        }
        else if (!constant)
        {
            check_input_report_subject(reader, begin, CHECK_INPUT_CONSTEXPR_INITIALIZER, declaration);
            valid = false;
        }
        if (valid && kind != BUSTER_TYPE_KIND_POINTER)
        {
            uint32_t width = 0;
            bool is_signed = false;
            const bool representable = check_action_integer_type(types, qualified, &width, &is_signed) ?
                check_input_constexpr_integer_fits(types, &value, width, is_signed) :
                kind != BUSTER_TYPE_KIND_FLOATING || check_input_constexpr_float_fits(types, &value, qualified);
            if (!representable)
            {
                check_input_report_subject(reader, begin, CHECK_INPUT_CONSTEXPR_REPRESENTABLE, declaration);
                valid = false;
            }
        }
        if (valid)
        {
            CheckConstexprValue* node = arena_push(reader->arena, sizeof(*node), _Alignof(CheckConstexprValue));
            reader->output->resource |= node == 0;
            valid = node != 0;
            if (node != 0)
            {
                node->declaration = declaration;
                node->value = value;
                node->value.flags |= BUSTER_CHECK_ICE;
                node->value.fault = CHECK_FAULT_NONE;
                node->next = reader->constexpr_values;
                reader->constexpr_values = node;
                reader->constexpr_count += 1;
            }
        }
    }
    else if (valid)
    {
        // An aggregate's elements must be constant expressions too: the initializer's expressions, each evaluated as
        // its bracket syntax and designators leave them, as a const object's constant initializer is.
        const uint64_t mark = reader->scratch->length;
        const uint32_t count = check_input_const_expressions(reader, begin, end, NULL);
        CheckInitExpression* expressions = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckInitExpression, count) : NULL;
        CheckValue* values = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, count) : NULL;
        valid = count == 0 || (expressions != NULL && values != NULL);
        reader->output->resource |= !valid;
        if (valid)
        {
            check_input_const_expressions(reader, begin, end, expressions);
        }
        for (uint32_t expression = 0; valid && expression < count; expression += 1)
        {
            values[expression] = (CheckValue){ 0 };
            valid = check_input_evaluate_cached_at(reader, expressions[expression].first, expressions[expression].end, &values[expression], NULL) &&
                    values[expression].constant_kind != CHECK_CONSTANT_NONE && values[expression].fault == CHECK_FAULT_NONE &&
                    (values[expression].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                    (values[expression].constant_kind != CHECK_CONSTANT_INTEGER || (values[expression].flags & BUSTER_CHECK_ICE) != 0) &&
                    (values[expression].constant_kind != CHECK_CONSTANT_ADDRESS || values[expression].object == 0 ||
                     (values[expression].flags & BUSTER_CHECK_STATIC_OBJECT) != 0);
            if (!valid)
            {
                check_input_report_subject(reader, expressions[expression].first, CHECK_INPUT_CONSTEXPR_INITIALIZER, declaration);
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

static bool check_input_initializer_finish(CheckInputReader* reader, uint32_t declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t begin = reader->output->initializer_begins[declaration];
    const uint32_t end = reader->output->initializer_ends[declaration];
    const uint32_t assignment = reader->output->compound_objects[declaration] ? declaration : begin - 1;
    TypeId type = reader->declared_types[declaration];
    bool valid = begin < end && end <= parser->tokens.count;
    if (valid && (check_input_auto_type(type) || (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_DEDUCED) != 0))
    {
        type = check_input_auto_type_finish(reader, declaration, begin, end, type);
        valid = type != 0 && check_input_cleanup_deduced(reader, declaration, type);
    }
    if (valid && (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) != 0)
    {
        valid = check_input_constexpr_finish(reader, declaration, begin, end, &type);
    }
    if (valid && type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
    {
        uint32_t string_token = begin;
        string_token += parser->tree.roles[begin] == PARSE_ROLE_OPEN_INITIALIZER || parser->tree.roles[begin] == PARSE_ROLE_OPEN_COMPOUND_BODY;
        const TypeId element = type_unqualified(type_strip_aligned(reader->types, type_element(reader->types, type)));
        const CheckInitString* string = string_token < end && parser->tokens.kinds[string_token] == PARSE_KIND_STRING ? check_input_string(reader, string_token) : 0;
        const TypeId string_element = string != 0 ? type_element(reader->types, string->type) : 0;
        const bool character = element == BUSTER_TYPE_CHAR || element == BUSTER_TYPE_SCHAR || element == BUSTER_TYPE_UCHAR;
        string = string != 0 && (element == string_element || (character && string_element == BUSTER_TYPE_CHAR)) ? string : 0;
        if (string != 0)
        {
            type = check_input_type_result(reader, type_array(reader->types, type_element(reader->types, type), reader->types->types.extent[type_index(string->type)]));
            valid = type != 0;
        }
        else
        {
            uint32_t failed = begin;
            CheckInitStatus status = CHECK_INIT_OK;
            // The casts in the initializer have their types already, at their `)`s: it finishes at its end.
            valid = check_initializer_infer_array(reader->types, parser, reader->pp->table, type, begin, end, reader->designator_indices,
                                                  reader->designator_valid, reader->declared_types, reader->query_types, reader->scratch, &type,
                                                  &failed, &status);
            reader->output->resource |= status == CHECK_INIT_RESOURCE;
            if (!valid)
            {
                check_input_report(reader, failed, CHECK_INPUT_DECLARATION);
            }
        }
        if (valid)
        {
            reader->declared_types[declaration] = type;
            reader->query_types[assignment] = type;
            const uint32_t name = parser->tokens.names[declaration];
            if (reader->linkage[declaration] != 0)
            {
                reader->linked_types[name] = type;
                if (reader->deferred != 0)
                {
                    check_deferred_linked(reader, name, declaration);
                }
            }
        }
    }
    if (valid && check_const_eligible(reader->types, type))
    {
        check_input_initializer_const(reader, declaration, begin, end, type);
    }
    if (!valid && reader->output->report_count == 0)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid;
}
