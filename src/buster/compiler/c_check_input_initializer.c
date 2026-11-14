// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Initializer syntax stays separate from expression actions. Resolve designator ICEs after the declaration's
// activation point, retain their value for the final initializer planner, and finish an outer array at the end of
// its initializer. Structural preparation emits no future binding events; the source pass executes each bound
// job at its actual opener. The token scan skips expression groups and uses no recursive descent.

// A GNU designator's -Wgnu-designator warning (#2202), as clang's parser gives it, at `token`: `form` one of the
// BUSTER_CHECK_GNU_DESIGNATOR_* forms. An array range is -pedantic's, ignored by default. Once whichever read reaches it.
static __attribute__((noinline, cold)) void check_input_gnu_designator(CheckInputReader* reader, uint32_t token, uint32_t form)
{
    const bool range = form == BUSTER_CHECK_GNU_DESIGNATOR_RANGE;
    const CheckWarningEvent event =
    {
        .id = range ? CHECK_WARNING_ID_GNU_ARRAY_RANGE : CHECK_WARNING_ID_GNU_DESIGNATOR,
        .kind = CHECK_WARNING_EVENT_GNU_DESIGNATOR,
        .token = token,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .flags = range ? BUSTER_CHECK_WARNING_DEFAULT_IGNORED : 0,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = token + 1,
        .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = form },
    };
    check_input_warning_once(reader, &event);
}

static bool check_input_initializer_prepare_slow(CheckInputReader* reader, uint32_t assignment)
{
    const Parser* parser = reader->parser;
    const uint32_t declaration = reader->initializer_declarations[assignment];
    const uint32_t begin = check_sparse_get(reader->output->initializer_begins, declaration);
    const uint32_t end = check_sparse_get(reader->output->initializer_ends, declaration);
    TypeId type = reader->declared_types[declaration];
    bool valid = begin < end && end <= parser->tokens.count;
    bool item = true;
    // A string literal check_input_string refused, which reported its own error (#1447): no declaration's beside it.
    bool string_refused = false;
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
        else if (item && check_init_old_style_designator(parser, token, end))
        {
            check_input_gnu_designator(reader, token, BUSTER_CHECK_GNU_DESIGNATOR_OLD_STYLE);
            reader->ignored[token] = true;
            reader->initializers[token + 1] = 2;
            item = false;
            token += 2;
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
                // A designation's only designator, an array's, without its `=` (clang's ext_gnu_missing_equal_designator), and
                // an array range: GNU's (#2202).
                const uint8_t before = token > begin ? parser->tokens.kinds[token - 1] : PARSE_KIND_L_BRACE;
                const uint8_t after = close + 1 < end ? parser->tokens.kinds[close + 1] : PARSE_KIND_EQUAL;
                if (range != close)
                {
                    check_input_gnu_designator(reader, range, BUSTER_CHECK_GNU_DESIGNATOR_RANGE);
                }
                if ((before == PARSE_KIND_L_BRACE || before == PARSE_KIND_COMMA) && after != PARSE_KIND_EQUAL && after != PARSE_KIND_L_SQUARE &&
                    after != PARSE_KIND_PERIOD)
                {
                    check_input_gnu_designator(reader, close + 1, BUSTER_CHECK_GNU_DESIGNATOR_MISSING_EQUAL);
                }
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
                 parser->tokens.kinds[token - 1] == PARSE_KIND_EQUAL ||
                 (token >= begin + 2 && check_init_old_style_designator(parser, token - 2, end))) &&
                (parser->tokens.kinds[token + 1] == PARSE_KIND_COMMA || parser->tokens.kinds[token + 1] == PARSE_KIND_R_BRACE))
            {
                reader->initializers[token] = 3;
            }
            item = false;
            if (kind == PARSE_KIND_STRING)
            {
                valid = check_input_string(reader, token) != 0;
                string_refused = !valid;
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
    // An array whose initializer is parenthesized may be a compound literal's (GNU's, check_input_initializer_finish).
    const bool parenthesized = parser->tokens.kinds[begin] == PARSE_KIND_L_PAREN && type_kind_is_array(type_kind(reader->types, type_strip_aligned(reader->types, type)));
    // (A variable whose type tags register its initializer's value, #1447, c_check_type_safety_input.c, finishes too.)
    if (valid && (incomplete || parenthesized || check_const_eligible(reader->types, type) || check_input_auto_type(type) ||
                  (BUSTER_CHECK_RARE(reader, declaration_flags, declaration) & BUSTER_CHECK_DECLARATION_DEDUCED) != 0 ||
                  (reader->type_tag_attributes && check_type_tags_pending(reader->output, declaration) != NULL)))
    {
        check_input_rare_claim(reader, &reader->incomplete_array_types[declaration]);
        reader->incomplete_array_types[declaration] = incomplete ? type : 0;
        check_input_rare_claim(reader, &reader->initializer_finish_next[declaration]);
        reader->initializer_finish_next[declaration] = reader->initializer_finish_heads[end];
        reader->initializer_finish_heads[end] = declaration;
        check_input_visit(reader, end, BUSTER_CHECK_RESOLVE_FINISH);
        reader->inference_end = end > reader->inference_end ? end : reader->inference_end;
    }
    if (!valid && !string_refused)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_initializer_prepare(CheckInputReader* reader, uint32_t assignment)
{
    const bool compact = check_input_literal_table_prepare(reader, assignment);
    const bool valid = compact || (!reader->output->resource && check_input_initializer_prepare_slow(reader, assignment));
    if (valid)
    {
        const uint32_t declaration = reader->initializer_declarations[assignment];
        check_input_noderef_set(reader->output, assignment, check_input_noderef_at(reader->output, declaration));
    }
    return valid;
}

static bool check_input_initializer_designator(CheckInputReader* reader, uint32_t token)
{
    const uint32_t close = (uint32_t)reader->designator_indices[token];
    const uint32_t range = reader->designator_ranges[token];
    int64_t index = 0;
    int64_t high = 0;
    // Its undeclared names are clang's errors at each one (#1447, check_constant_name), which end the walk; clang drops the
    // initializer list there.
    const uint32_t named = reader->constant_named;
    CheckConstantFailure failure = { .token = BUSTER_CHECK_NONE, .callee = BUSTER_CHECK_NONE };
    reader->constant_names = CHECK_CONSTANT_NAMES_REPORT;
    reader->constant_failure = &failure;
    bool valid = check_input_constant(reader, token + 1, range, &index) && index >= 0;
    reader->constant_names = valid && range != close ? CHECK_CONSTANT_NAMES_REPORT : CHECK_CONSTANT_NAMES_NONE;
    reader->constant_failure = valid && range != close ? &failure : NULL;
    valid = valid && (range == close || (check_input_constant(reader, range + 1, close, &high) && high >= index));
    reader->constant_names = CHECK_CONSTANT_NAMES_NONE;
    reader->constant_failure = NULL;
    if (valid)
    {
        check_input_rare_claim(reader, &reader->designator_indices[token]);
        reader->designator_indices[token] = (uint64_t)index;
        check_input_rare_claim(reader, &reader->designator_valid[token]);
        reader->designator_valid[token] = 1;
        check_input_rare_claim(reader, &reader->designator_tokens[range]);
        reader->designator_tokens[range] = token + 1;
        check_input_sparse_mark(reader, range, true);
        if (range != close)
        {
            check_input_rare_claim(reader, &reader->designator_indices[range]);
            reader->designator_indices[range] = (uint64_t)high;
            check_input_rare_claim(reader, &reader->designator_valid[range]);
            reader->designator_valid[range] = 1;
            check_input_rare_claim(reader, &reader->designator_tokens[close]);
            reader->designator_tokens[close] = range + 1;
            check_input_sparse_mark(reader, close, true);
        }
    }
    else if (reader->constant_named == named || reader->output->report_count == 0)
    {
        // Its failure's error in clang's words (#1447).
        check_input_constant_failure_report(reader, &failure, token + 1, token + 1);
    }
    return valid;
}

#include "c_check_input_const_values.c"

// An __auto_type declaration's type, once the walk has read its initializer (whose uses' declarations it captured:
// inference_end), as clang deduces it: the initializer's, converted as an lvalue's (its qualifiers dropped, but not
// _Atomic) and decayed, with the specifiers' const and volatile. A use of the variable in its initializer is an error
// there, a bit-field's value one at the member's name, an incomplete type one at the variable's name, and an _Alignas
// below the type's own alignment one at the _Alignas (the specifiers and the declarator left it to this). A variably
// modified type is the initializer's, whose sizes its own declaration or cast evaluated: the declaration evaluates none,
// as clang's evaluates none for a deduced type. The declaration's columns take the type, which is returned, or 0.
static __attribute__((noinline)) TypeId check_input_auto_type_finish(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end,
                                                                     TypeId placeholder)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t assignment = begin - 1;
    // Deduced through its declarator (check_input_c23_auto_deduce): C23's auto's, or __auto_type's, whose errors name it.
    const bool deduced_declarator = (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_DEDUCED) != 0;
    const bool gnu = (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_GNU_AUTO) != 0;
    const bool c23 = deduced_declarator && !gnu;
    uint32_t self = BUSTER_CHECK_NONE;
    for (uint32_t token = begin; token < end && self == BUSTER_CHECK_NONE; token += 1)
    {
        self = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && reader->output->declarations[token] == declaration ? token : BUSTER_CHECK_NONE;
    }
    CheckValue value = { 0 };
    uint32_t failed = BUSTER_CHECK_NONE;
    const bool evaluated = self == BUSTER_CHECK_NONE && check_input_evaluate_cached_at(reader, begin, end, &value, &failed);
    // The specifiers' _Atomic and restrict, the deduced type's (#1844, check_input_auto_type_qualifiers; their declarators'
    // deductions, check_input_c23_auto_deduce, apply them). Refused by name: either through a declarator (`_Atomic
    // __auto_type *p`, whose initializer clang always finds incompatible), an _Atomic of an atomic value (clang's error
    // names the placeholder's type), and a restrict of a value that is no pointer to an object, whose `restrict int` the
    // placeholder can't be told from.
    // The placeholder itself, or under a declarator's pointers and arrays.
    TypeId marker = placeholder;
    for (uint32_t steps = 0; marker != 0 && !check_input_auto_type(marker) && steps < 64; steps += 1)
    {
        const TypeId level = type_strip_aligned(types, marker);
        const uint8_t kind = type_kind(types, level);
        marker = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? types->types.base[type_index(level)] : 0;
    }
    const bool declarator = marker != placeholder;
    bool atomic = false;
    const uint32_t qualifiers = check_input_auto_type_qualifiers(marker, &atomic);
    const bool restricted = (qualifiers & BUSTER_TYPE_RESTRICT) != 0;
    const TypeId adjusted = evaluated ? type_strip_aligned(types, type_adjust_parameter(types, value.type)) : 0;
    const uint8_t adjusted_kind = adjusted != 0 ? type_kind(types, adjusted) : BUSTER_TYPE_KIND_NONE;
    const bool object_pointer = adjusted_kind == BUSTER_TYPE_KIND_POINTER && type_kind(types, types->types.base[type_index(adjusted)]) != BUSTER_TYPE_KIND_FUNCTION;
    const bool refused = (atomic || restricted) && evaluated &&
                         (declarator || (atomic && adjusted_kind == BUSTER_TYPE_KIND_ATOMIC) || (restricted && !object_pointer));
    const CheckNoderefType* noderef = evaluated ? check_input_noderef_value(reader, value.type, reader->constant_noderef) : NULL;
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
        if (!reader->output->resource && (atomic || restricted) && !undeclared)
        {
            // An initializer the walk can't evaluate under the specifiers' _Atomic or restrict: refused by name, as before
            // they were taken.
            check_input_report(reader, declaration, CHECK_INPUT_UNSUPPORTED);
        }
        else if (!reader->output->resource)
        {
            check_input_report(reader, failed < parser->tokens.count ? failed : begin, undeclared ? CHECK_INPUT_UNDECLARED : CHECK_INPUT_DECLARATION);
        }
    }
    else if (refused)
    {
        const bool atomic_value = atomic && !declarator && adjusted_kind == BUSTER_TYPE_KIND_ATOMIC;
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, declaration, atomic_value ? CHECK_INPUT_AUTO_TYPE_ATOMIC_INITIALIZER : CHECK_INPUT_UNSUPPORTED);
        if (atomic_value && reader->output->report_count > before)
        {
            CheckInputReport* report = reader->output->reports + before;
            report->type = placeholder;
            report->value = value.type;
            report->completion = end;
            if (begin + 1 == end && parser->tokens.kinds[begin] == PARSE_KIND_IDENTIFIER)
            {
                report->type_spelling = check_input_token_type_spelling(reader->output, parser, begin, false);
            }
            for (uint32_t token = begin; token < end; token += 1)
            {
                if (report->type_spelling == 0 && parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST)
                {
                    const uint32_t spelling = check_input_token_type_spelling(reader->output, parser, token, true);
                    if (spelling < reader->output->token_count && spelling != 0 &&
                        type_same_canonical(types, reader->declared_types[spelling], value.type) == TYPE_ANSWER_YES)
                    {
                        report->type_spelling = spelling;
                    }
                }
            }
            // Keep a usable declaration after diagnosing the invalid double-atomic deduction. This lets the ordinary
            // expression and warning walks retain the initializer and later statements without accepting the unit.
            if (!reader->lane && check_input_recover_reports(reader, before))
            {
                result = check_input_type_result(reader, type_qualify(types, adjusted, qualifiers));
                reader->declared_types[declaration] = result;
                reader->query_types[assignment] = result;
                check_input_noderef_set(reader->output, declaration, noderef);
                check_input_noderef_set(reader->output, assignment, noderef);
                reader->initializers[assignment] = check_init_aggregate(types, result) ? 2 : 1;
                if (check_input_linkage(reader->declaration_bits, declaration) != 0)
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
        TypeId deduced = deduced_declarator ? check_input_type_result(reader, check_input_c23_auto_deduce(reader, placeholder, value.type, declaration, &deduced_base)) :
                                              check_input_type_result(reader, type_strip_aligned(types, type_adjust_parameter(types, value.type)));
        if (deduced == 0 && !reader->output->resource && reader->output->report_count == 0)
        {
            // A declarator shape its initializer doesn't fit, at clang's name for it.
            check_input_report(reader, declaration, CHECK_INPUT_DECLARATION);
        }
        if (deduced_declarator && deduced != 0)
        {
            // C23's later declarators deduce the same type (6.7.9), and __auto_type's as clang deduces them: clang reports
            // the difference at the declaration.
            const uint32_t group = reader->declaration_specifiers[declaration];
            const TypeId previous = group < parser->tokens.count ? reader->declaration_deduced[group] : 0;
            if (previous != 0 && type_same_canonical(types, previous, deduced_base) != TYPE_ANSWER_YES)
            {
                // At the declaration's auto, where clang points its two deductions' error.
                check_input_report(reader, group < parser->tokens.count ? group : declaration, gnu ? CHECK_INPUT_AUTO_TYPE_MISMATCH : CHECK_INPUT_AUTO_DEDUCED_MISMATCH);
                deduced = 0;
            }
            else if (previous == 0 && group < parser->tokens.count)
            {
                check_input_rare_claim(reader, &reader->declaration_deduced[group]);
                reader->declaration_deduced[group] = deduced_base;
            }
        }
        CheckInputAttributes* attributes = check_input_attributes_at(reader->output, declaration);
        const uint32_t required = attributes != 0 ? attributes->required_alignment_log2 : 0;
        if (deduced != 0 && !type_is_complete(types, deduced))
        {
            check_input_report(reader, declaration, CHECK_INPUT_INCOMPLETE_OBJECT);
        }
        else if (deduced != 0 && required != 0 && required - 1 < type_alignment_log2(types, deduced))
        {
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = &attributes->records[i];
                // (One whose value has an error has no alignment: check_input_alignas_declarator.)
                if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS && attribute->deferred == NULL)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_ALIGNMENT);
                }
            }
        }
        else if (deduced != 0)
        {
            const TypeId atomic_deduced = atomic && !deduced_declarator ? check_input_type_result(reader, type_atomic(types, deduced)) : deduced;
            result = deduced_declarator ? deduced : atomic_deduced != 0 ? type_qualify(types, atomic_deduced, qualifiers) : 0;
            reader->declared_types[declaration] = result;
            reader->query_types[assignment] = result;
            check_input_noderef_set(reader->output, declaration, noderef);
            check_input_noderef_set(reader->output, assignment, noderef);
            reader->initializers[assignment] = check_init_aggregate(types, result) ? 2 : 1;
            if (check_input_linkage(reader->declaration_bits, declaration) != 0)
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
    const uint32_t assignment = check_input_bit(reader->output->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) ? declaration : begin - 1;
    TypeId qualified = check_input_type_result(reader, type_qualify(types, *type, BUSTER_TYPE_CONST));
    bool valid = qualified != 0;
    if (valid)
    {
        *type = qualified;
        reader->declared_types[declaration] = qualified;
        reader->query_types[assignment] = qualified;
        if (check_input_linkage(reader->declaration_bits, declaration) != 0)
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
                              (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0 &&
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
                check_input_constexpr_add(reader, node);
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
                    (values[expression].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0 &&
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

// The braced body `{`'s token of an array's compound literal that is all of an initializer from `begin` to `end`, past
// parentheses around it, and the literal's type at its `)` (as the casts' are), or BUSTER_CHECK_NONE.
static uint32_t check_input_array_literal(const CheckInputReader* reader, uint32_t begin, uint32_t end, TypeId* literal)
{
    const Parser* parser = reader->parser;
    uint32_t inner = begin;
    uint32_t inner_end = end;
    while (inner + 1 < inner_end && parser->tokens.kinds[inner] == PARSE_KIND_L_PAREN && parser->tree.parents[inner] == inner_end - 1)
    {
        inner += 1;
        inner_end -= 1;
    }
    const uint32_t group_close = inner < inner_end && parser->tokens.kinds[inner] == PARSE_KIND_L_PAREN ? parser->tree.parents[inner] : 0;
    const bool compound = group_close > inner && group_close + 1 < inner_end && parser->tree.roles[group_close + 1] == PARSE_ROLE_OPEN_COMPOUND_BODY &&
                          parser->tree.parents[group_close + 1] == inner_end - 1;
    // The literal's own declaration, at its `(`, has its completed type once its initializer finished (`(int[]){1, 2}`'s
    // int[2]); its written type is at its `)`.
    const bool declared = compound && reader->output->declarations[inner] == inner && check_input_bit(reader->output->declaration_bits, inner, BUSTER_CHECK_BITS_COMPOUND);
    const TypeId type = !compound ? 0 : type_strip_aligned(reader->types, declared ? reader->declared_types[inner] : reader->query_types[group_close]);
    const bool array = type != 0 && type_kind_is_array(type_kind(reader->types, type));
    *literal = array ? type : 0;
    return array ? group_close + 1 : BUSTER_CHECK_NONE;
}

static bool check_input_initializer_finish(CheckInputReader* reader, uint32_t declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t written = check_sparse_get(reader->output->initializer_begins, declaration);
    uint32_t begin = written;
    uint32_t end = check_sparse_get(reader->output->initializer_ends, declaration);
    // GNU's array initialized by a compound literal of its array type (#1447), as clang accepts it: the same element type,
    // qualifiers too, and the same size or none yet. The literal's body is then the array's initializer, which every
    // reader of the initializer's tokens takes from here. Another array type is clang's "cannot initialize array of type
    // 'int[3]' with array of type 'int[2]'" at the name, recovered, the array keeping its type and no initializer.
    TypeId literal = 0;
    const TypeId declared = type_strip_aligned(reader->types, reader->declared_types[declaration]);
    const bool array = begin < end && end <= parser->tokens.count && parser->tokens.kinds[begin] == PARSE_KIND_L_PAREN &&
                       type_kind_is_array(type_kind(reader->types, declared));
    const uint32_t body = array ? check_input_array_literal(reader, begin, end, &literal) : BUSTER_CHECK_NONE;
    const bool unsized = array && type_kind(reader->types, declared) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    const bool matched = body != BUSTER_CHECK_NONE && type_element(reader->types, declared) == type_element(reader->types, literal) &&
                         (unsized || reader->types->types.extent[type_index(declared)] == reader->types->types.extent[type_index(literal)]);
    bool mismatched = false;
    if (matched)
    {
        begin = body;
        end = parser->tree.parents[body] + 1;
        check_sparse_set(reader->output->sparse_pool, reader->output->initializer_begins, declaration, begin);
        check_sparse_set(reader->output->sparse_pool, reader->output->initializer_ends, declaration, end);
    }
    else if (body != BUSTER_CHECK_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, declaration, CHECK_INPUT_ARRAY_LITERAL_MISMATCH);
        if (reader->output->report_count == before + 1 && !reader->output->resource)
        {
            reader->output->reports[before].type = reader->declared_types[declaration];
            reader->output->reports[before].type_spelling = check_input_typedef_spelling(reader->output, parser, declaration);
            // (The literal's type name's typedef in the high half, #1812: `'A2' (aka 'int[2]')`.)
            reader->output->reports[before].value = (uint64_t)literal | (uint64_t)check_input_token_type_spelling(reader->output, parser, begin, false) << 32;
            reader->output->reports[before].completion = end;
        }
        mismatched = reader->output->report_count == before + 1 && check_input_recover_reports(reader, before);
        if (mismatched)
        {
            check_sparse_set(reader->output->sparse_pool, reader->output->initializer_begins, declaration, BUSTER_CHECK_NONE);
            check_sparse_set(reader->output->sparse_pool, reader->output->initializer_ends, declaration, BUSTER_CHECK_NONE);
        }
    }
    const uint32_t assignment = check_input_bit(reader->output->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) ? declaration : written - 1;
    TypeId type = reader->declared_types[declaration];
    bool valid = !mismatched && begin < end && end <= parser->tokens.count;
    // An array of unknown size whose initializer is no list, recovered (below): it keeps its type and no initializer.
    bool listless = false;
    if (valid && (check_input_auto_type(type) || (BUSTER_CHECK_RARE(reader, declaration_flags, declaration) & BUSTER_CHECK_DECLARATION_DEDUCED) != 0))
    {
        type = check_input_auto_type_finish(reader, declaration, begin, end, type);
        valid = type != 0 && check_input_cleanup_deduced(reader, declaration, type);
    }
    if (valid && (BUSTER_CHECK_RARE(reader, declaration_flags, declaration) & BUSTER_CHECK_DECLARATION_CONSTEXPR) != 0)
    {
        valid = check_input_constexpr_finish(reader, declaration, begin, end, &type);
    }
    if (valid && type_kind(reader->types, type) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
    {
        // A compound literal's array of its own size gives it (GNU's, above): `((int[3]){1})` makes int[3].
        const bool sized = matched && type_kind(reader->types, literal) == BUSTER_TYPE_KIND_ARRAY;
        uint32_t string_token = begin;
        string_token += parser->tree.roles[begin] == PARSE_ROLE_OPEN_INITIALIZER || parser->tree.roles[begin] == PARSE_ROLE_OPEN_COMPOUND_BODY;
        const TypeId element = type_unqualified(type_strip_aligned(reader->types, type_element(reader->types, type)));
        const CheckInitString* string = string_token < end && parser->tokens.kinds[string_token] == PARSE_KIND_STRING ? check_input_string(reader, string_token) : 0;
        const TypeId string_element = string != 0 ? type_element(reader->types, string->type) : 0;
        const bool character = element == BUSTER_TYPE_CHAR || element == BUSTER_TYPE_SCHAR || element == BUSTER_TYPE_UCHAR;
        string = string != 0 && (element == string_element || (character && string_element == BUSTER_TYPE_CHAR)) ? string : 0;
        if (sized)
        {
            type = check_input_type_result(reader, type_array(reader->types, type_element(reader->types, type), reader->types->types.extent[type_index(literal)]));
            valid = type != 0;
        }
        else if (string != 0)
        {
            type = check_input_type_result(reader, type_array(reader->types, type_element(reader->types, type), reader->types->types.extent[type_index(string->type)]));
            valid = type != 0;
        }
        else
        {
            uint32_t failed = begin;
            CheckInitStatus status = CHECK_INIT_OK;
            TypeId failed_type = 0;
            // The casts in the initializer have their types already, at their `)`s: it finishes at its end.
            valid = check_initializer_infer_array(reader->types, parser, reader->pp->table, type, begin, end, reader->designator_indices,
                                                  reader->designator_valid, reader->declared_types, reader->query_types, reader->scratch, &type,
                                                  &failed, &status, &failed_type);
            reader->output->resource |= status == CHECK_INIT_RESOURCE;
            // An initializer of no braces that isn't a string of the element (#1447, phase 2): clang's "array initializer
            // must be an initializer list" (FK_ArrayNeedsInitList) at the declarator, worded by the literal its element
            // takes (CHECK_INPUT_DIRECT_INITIALIZER_ARRAY's detail), after the initializer's own diagnostics (its
            // completion at the initializer's end), and none where the initializer is no valid value, which the expressions'
            // check decides (check_init_listless_drop, in the driver). The report is recovered: the array keeps its unknown size, as clang's invalid
            // declaration does, and its initializer leaves the initializers' run. A compound literal (GNU's array
            // initializer, `(int[2]){1, 2}`) keeps the refusal below; any other parenthesized initializer is no list.
            const uint8_t first_role = parser->tree.roles[begin];
            // Past parentheses around the whole initializer, an array's compound literal that is all of it: its type at its
            // `)` (as the casts' are, above), its body closing the initializer.
            uint32_t inner = begin;
            uint32_t inner_end = end;
            while (inner + 1 < inner_end && parser->tokens.kinds[inner] == PARSE_KIND_L_PAREN && parser->tree.parents[inner] == inner_end - 1)
            {
                inner += 1;
                inner_end -= 1;
            }
            const uint32_t group_close = parser->tokens.kinds[inner] == PARSE_KIND_L_PAREN ? parser->tree.parents[inner] : 0;
            const bool compound = group_close > inner && group_close + 1 < inner_end && parser->tree.roles[group_close + 1] == PARSE_ROLE_OPEN_COMPOUND_BODY &&
                                  parser->tree.parents[group_close + 1] == inner_end - 1 &&
                                  type_kind_is_array(type_kind(reader->types, type_strip_aligned(reader->types, reader->query_types[group_close])));
            const bool unbraced = first_role != PARSE_ROLE_OPEN_INITIALIZER && first_role != PARSE_ROLE_OPEN_COMPOUND_BODY && !compound;
            const bool designator = status == CHECK_INIT_MEMBER || status == CHECK_INIT_ARRAY_DESIGNATOR || status == CHECK_INIT_FIELD_DESIGNATOR ||
                                    status == CHECK_INIT_NON_AGGREGATE_DESIGNATOR;
            const bool needs_list = !valid && unbraced && !designator && !reader->output->resource;
            if (needs_list)
            {
                const uint32_t before = reader->output->report_count;
                const bool wide = element == BUSTER_TYPE_INT || element == BUSTER_TYPE_USHORT || element == BUSTER_TYPE_UINT;
                check_input_report(reader, declaration, CHECK_INPUT_DIRECT_INITIALIZER_ARRAY);
                if (reader->output->report_count == before + 1)
                {
                    reader->output->reports[before].detail = character ? CHECK_INPUT_ARRAY_LIST_STRING : wide ? CHECK_INPUT_ARRAY_LIST_WIDE : CHECK_INPUT_ARRAY_LIST_ONLY;
                    reader->output->reports[before].completion = end;
                    // Its initializer's first token, for check_init_listless_drop.
                    reader->output->reports[before].value = begin;
                }
                type = reader->declared_types[declaration];
                listless = reader->output->report_count == before + 1 && check_input_recover_reports(reader, before);
                valid = listless;
                if (listless)
                {
                    check_sparse_set(reader->output->sparse_pool, reader->output->initializer_begins, declaration, BUSTER_CHECK_NONE);
                    check_sparse_set(reader->output->sparse_pool, reader->output->initializer_ends, declaration, BUSTER_CHECK_NONE);
                    // An extern array's -Wextern-initializer, which clang gives of no invalid initializer: its event leaves
                    // the reader's warnings (events name their notes by index, so the others keep theirs).
                    CheckWarningBuffer* events = &reader->warning_events;
                    bool found = false;
                    for (uint32_t e = events->count; e > 0 && !found; e -= 1)
                    {
                        CheckWarningEvent* event = events->events + (e - 1);
                        found = event->id == CHECK_WARNING_ID_EXTERN_INITIALIZER && event->token == declaration;
                        if (found)
                        {
                            memmove(event, event + 1, (uint64_t)(events->count - e) * sizeof(CheckWarningEvent));
                            events->count -= 1;
                            reader->output->warning_events = check_warning_view(events);
                        }
                    }
                }
            }
            else if (!valid)
            {
                const uint32_t before = reader->output->report_count;
                check_input_report(reader, failed, designator ? CHECK_INPUT_INITIALIZER_MEMBER : CHECK_INPUT_DECLARATION);
                if (reader->output->report_count > before && designator)
                {
                    reader->output->reports[before].type = failed_type;
                    // Which designator's error (#2202): the walk's status, which the driver words.
                    reader->output->reports[before].detail = (uint32_t)status;
                    reader->output->reports[before].completion = end;
                }
            }
        }
        if (valid)
        {
            reader->declared_types[declaration] = type;
            reader->query_types[assignment] = type;
            const uint32_t name = parser->tokens.names[declaration];
            if (check_input_linkage(reader->declaration_bits, declaration) != 0)
            {
                reader->linked_types[name] = type;
                if (reader->deferred != 0)
                {
                    check_deferred_linked(reader, name, declaration);
                }
            }
        }
    }
    if (valid && !listless && check_const_eligible(reader->types, type))
    {
        check_input_initializer_const(reader, declaration, begin, end, type);
    }
    if (valid && !listless && reader->type_tag_attributes)
    {
        valid = check_input_type_tag_finish(reader, declaration, begin, end);
    }
    if (!valid && !mismatched && reader->output->report_count == 0)
    {
        check_input_report(reader, assignment, CHECK_INPUT_DECLARATION);
    }
    return valid || mismatched;
}
