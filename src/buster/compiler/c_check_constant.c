// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Declaration-time integer expressions and type names. Source-order binding capture precedes the parser's
// postorder evaluation; every value retains its actual declaration and target type. Type-name declarators use
// an explicit stack and consume array bounds already evaluated in postorder. They never call the declaration
// reader, which itself calls this module for bounds and attributes. This keeps the entire call graph acyclic.
// Temporary arrays have one record per token in the requested bounded slice, with no padding assumption.

#define BUSTER_CHECK_CONSTANT_BOUND_CONTEXT 2u

// Every mutation, including speculative-call restoration and unselected-range clearing, keeps the final nonzero-row
// count. The driver can skip the token column entirely when it is empty; no extra final pass computes this metadata.
static void check_input_constant_warning_set(CheckInput* input, uint32_t token, uint8_t warnings)
{
    input->constant_warning_count = input->constant_warning_count - (input->constant_warnings[token] != 0) + (warnings != 0);
    input->constant_warnings[token] = warnings;
}

static void check_input_constant_warning_add(CheckInput* input, uint32_t token, uint8_t warnings)
{
    if (warnings != 0)
    {
        check_input_constant_warning_set(input, token, input->constant_warnings[token] | warnings);
    }
}

// clang's Sema has GNU's extensions in every C language mode, -std=c99 as -std=gnu99, and only -pedantic warns about
// them: folding a constant expression that isn't an ICE among them. GNU still names -std=gnu*, which the vector
// builtins' index rule reads (c_check_builtin_constant.c's, #148).
static uint32_t check_input_expression_extensions(const Parser* parser)
{
    return BUSTER_CHECK_EXTENSION_GNU_CONSTANT | (parser->language.gnu ? BUSTER_CHECK_EXTENSION_GNU : 0);
}

static bool check_input_integer_constant(const CheckInputReader* reader, const CheckValue* value)
{
    return check_action_integer_constant(value, check_input_expression_extensions(reader->parser));
}

static bool check_input_integer_ice(const CheckInputReader* reader, const CheckValue* value)
{
    const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
    return check_input_integer_constant(reader, value) && (value->flags & forms) != 0;
}

static bool check_input_integer_attribute(const CheckInputReader* reader, const CheckValue* value)
{
    return check_input_integer_ice(reader, value) && (value->flags & BUSTER_CHECK_GNU_DIVISION) == 0;
}

static void check_input_constant_warning_clear(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t warnings)
{
    if (reader->output->constant_warnings != 0 && first < end && end <= reader->parser->tokens.count)
    {
        for (uint32_t token = first; token < end; token += 1)
        {
            const bool independent = reader->constant_contexts != 0 && (reader->constant_contexts[token] & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) != 0;
            if (!independent)
            {
                check_input_constant_warning_set(reader->output, token, reader->output->constant_warnings[token] & (uint8_t)~warnings);
                if (reader->output->constant_warning_ignored != 0)
                {
                    reader->output->constant_warning_ignored[token] &= (uint8_t)~warnings;
                }
            }
        }
    }
}

static void check_input_constant_fold_warning(CheckInputReader* reader, uint32_t first, uint32_t end, const CheckValue* value, bool ignored)
{
    if (first < end && end <= reader->parser->tokens.count && reader->output->constant_warnings != 0 &&
        check_input_integer_constant(reader, value) && (value->flags & BUSTER_CHECK_ICE) == 0)
    {
        const bool present = (reader->output->constant_warnings[first] & CHECK_WARNING_GNU_CONSTANT) != 0;
        check_input_constant_warning_add(reader->output, first, CHECK_WARNING_GNU_CONSTANT);
        if (reader->output->constant_warning_ignored != 0)
        {
            if (ignored && !present)
            {
                reader->output->constant_warning_ignored[first] |= CHECK_WARNING_GNU_CONSTANT;
            }
            else if (!ignored)
            {
                reader->output->constant_warning_ignored[first] &= (uint8_t)~CHECK_WARNING_GNU_CONSTANT;
            }
        }
    }
}

// Required integer contexts diagnose the non-ICE fold as a whole. Runtime shift diagnostics are not emitted
// for those operands by Clang; overflow and discarded shift bits keep their own operator-local diagnostics.
static void check_input_constant_warning(CheckInputReader* reader, uint32_t first, uint32_t end, const CheckValue* value, bool ignored)
{
    check_input_constant_warning_clear(reader, first, end,
        CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE);
    check_input_constant_fold_warning(reader, first, end, value, ignored);
}

static void check_input_constant_bound_warning(CheckInputReader* reader, uint32_t first, uint32_t end, const CheckValue* value)
{
    check_input_constant_warning(reader, first, end, value, false);
    if (reader->constant_contexts != 0 && first < end && end <= reader->parser->tokens.count)
    {
        for (uint32_t token = first; token < end; token += 1)
        {
            reader->constant_contexts[token] |= BUSTER_CHECK_CONSTANT_BOUND_CONTEXT;
        }
    }
}

typedef struct CheckConstantAttributes CheckConstantAttributes;
struct CheckConstantAttributes
{
    CheckInputAttributes parsed;
    uint32_t owner;
    uint32_t end;
    bool publish;
};

typedef struct CheckConstantInput CheckConstantInput;
struct CheckConstantInput
{
    CheckInputReader* reader;
    uint64_t mark;
    uint32_t first;
    uint32_t end;
    uint32_t bind_cursor;
    uint32_t pending_tag;
    uint32_t failure;
    // The reader's prototype depth when the evaluation began, which it gives back when it ends, whether or not its
    // binding reached every list's `)` (check_constant_prototype_open).
    uint32_t prototype_depth;
    CheckValue* values;
    CheckValue* proof_values;
    uint8_t* proof_active;
    uint8_t* present;
    uint8_t* syntax;
    uint8_t* statement_syntax;
    uint32_t* first_value;
    uint32_t* last_value;
    uint32_t* next_value;
    uint32_t* type_begin;
    uint32_t* warning_begin;
    TypeId* queries;
    uint8_t* abi_origins;
    uint8_t* attribute_syntax;
    CheckConstantAttributes** attribute_starts;
    CheckConstantAttributes** attribute_ends;
    uint32_t* tag_finishes;
    uint32_t* tag_next;
    uint32_t* prototype_marks;
    uint32_t* failed_token;
    bool raw;
    bool cached;
    bool deferred_tags;
};

static CheckOverloadResult check_constant_address(CheckConstantInput* input, const CheckValue* source, TypeId target, CheckValue* resolved)
{
    CheckInputReader* reader = input->reader;
    const CheckOverloadContext context = {
        .tag_completions = reader->tag_completions,
        .transparent_unions = reader->transparent_unions,
        .tag_completion_count = reader->types->tags.capacity,
        .source_position = input->end,
        .extensions = check_input_expression_extensions(reader->parser),
    };
    const CheckGraph* graph = &reader->output->graph;
    const CheckOverloadResult selected = check_overload_argument(reader->types, source, target, graph->overload_candidates,
        graph->overload_candidate_count, &context, reader->scratch, resolved);
    reader->output->resource |= selected.status == CHECK_OVERLOAD_FULL;
    return selected;
}

static bool check_constant_resolve(CheckConstantInput* input, const CheckValue* source, TypeId target, CheckValue* resolved)
{
    const CheckOverloadResult selected = check_constant_address(input, source, target, resolved);
    return selected.status == CHECK_OVERLOAD_OK;
}

static bool check_constant_designator(CheckConstantInput* input, const CheckValue* value)
{
    CheckValue resolved;
    const CheckOverloadResult selected = check_constant_address(input, value, 0, &resolved);
    return selected.status == CHECK_OVERLOAD_OK || selected.status == CHECK_OVERLOAD_AMBIGUOUS;
}

static bool check_constant_arguments(CheckConstantInput* input, CheckValue* arguments, uint32_t count)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = check_constant_resolve(input, arguments + i, 0, arguments + i);
    }
    return valid;
}

static uint32_t check_constant_unevaluated_warnings(void)
{
    return CHECK_WARNING_INTEGER_OVERFLOW | CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW | CHECK_WARNING_SHIFT_NEGATIVE_VALUE;
}

static void check_input_warning_suppress(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t warnings)
{
    check_input_constant_warning_clear(reader, first, end, warnings);
    if (reader->warning_suppressed != 0 && first < end && end <= reader->parser->tokens.count)
    {
        for (uint32_t token = first; token < end; token += 1)
        {
            if (reader->constant_contexts == 0 || (reader->constant_contexts[token] & BUSTER_CHECK_CONSTANT_BOUND_CONTEXT) == 0)
            {
                reader->warning_suppressed[token] |= (uint8_t)warnings;
            }
        }
    }
}

static bool check_constant_warning_unevaluated(const TypeTable* types, const CheckAction* action, uint32_t operand, uint32_t count)
{
    const TypeId type = action->type != 0 ? action->type : action->operands[0].type;
    bool skip = action->op == CHECK_OP_ALIGNOF || (action->op == CHECK_OP_SIZEOF && !check_action_runtime_size(types, type));
    bool truth = false;
    if (!skip && check_action_truth(types, &action->operands[0], &truth))
    {
        skip = operand == 1 && ((action->op == CHECK_OP_LOGICAL_AND && !truth) || (action->op == CHECK_OP_LOGICAL_OR && truth));
        if (action->op == CHECK_OP_CONDITIONAL)
        {
            skip = count == 2 ? truth && operand == 1 : operand == (truth ? 2u : 1u);
        }
    }
    return skip;
}

static void check_constant_warning_operands(CheckConstantInput* input, const CheckAction* action, uint32_t first, uint32_t count)
{
    uint32_t operand = 0;
    for (uint32_t child = first; child != BUSTER_CHECK_NONE; child = input->next_value[child - input->first])
    {
        if (check_constant_warning_unevaluated(input->reader->types, action, operand, count))
        {
            check_input_constant_warning_clear(input->reader, input->warning_begin[child - input->first], input->reader->ends[child] + 1,
                                               check_constant_unevaluated_warnings());
        }
        operand += 1;
    }
}

typedef struct CheckConstantPart CheckConstantPart;
struct CheckConstantPart
{
    uint64_t extent;
    uint32_t next;
    uint32_t token;
    uint32_t qualifiers;
    uint32_t parameter_first;
    uint32_t parameter_last;
    uint32_t parameter_count;
    uint32_t parameter_names;
    uint8_t kind;
    uint8_t flags;
};

typedef struct CheckConstantParameter CheckConstantParameter;
struct CheckConstantParameter
{
    TypeId type;
    uint32_t next;
    uint32_t name;
    uint32_t name_children[2];
};

// Each prototype owns a digital search tree in its existing parameter records. Links are index + 1, with zero
// empty. Low bits first distribute consecutive interned names without hashing or clearing an identifier table.
// Every edge fixes another name bit: at most 32 unequal records can precede an empty link or an equal name.
// Insert only when the old scan ran, after parameter adjustment, preserving source-order failure and activation.
static bool check_constant_parameter_unique(CheckConstantPart* function, CheckConstantParameter* parameters, uint32_t parameter)
{
    const uint32_t name = parameters[parameter].name;
    bool unique = true;
    if (name != 0)
    {
        uint32_t* link = &function->parameter_names;
        uint32_t bits = name;
        while (unique && *link != 0)
        {
            CheckConstantParameter* previous = &parameters[*link - 1];
            unique = previous->name != name;
            link = &previous->name_children[bits & 1];
            bits >>= 1;
        }
        if (unique)
        {
            *link = parameter + 1;
        }
    }
    return unique;
}

typedef struct CheckConstantTypeFrame CheckConstantTypeFrame;
typedef struct CheckConstantTypeAttribute CheckConstantTypeAttribute;
struct CheckConstantTypeAttribute
{
    CheckConstantAttributes* attributes;
    uint32_t next;
};

struct CheckConstantTypeFrame
{
    TypeId base;
    uint32_t cursor;
    uint32_t end;
    uint32_t first;
    uint32_t last;
    uint32_t pointers;
    uint32_t function;
    uint32_t name;
    uint32_t attribute_first;
    uint32_t attribute_last;
    uint8_t state;
    uint8_t abi_origin;
    bool group;
    bool parameter;
};

static CheckConstantTypeFrame check_constant_type_frame(TypeId base, uint32_t first, uint32_t end, bool group, bool parameter)
{
    return (CheckConstantTypeFrame)
    {
        .base = base,
        .cursor = first,
        .end = end,
        .first = BUSTER_CHECK_NONE,
        .last = BUSTER_CHECK_NONE,
        .pointers = BUSTER_CHECK_NONE,
        .function = BUSTER_CHECK_NONE,
        .name = BUSTER_CHECK_NONE,
        .attribute_first = BUSTER_CHECK_NONE,
        .attribute_last = BUSTER_CHECK_NONE,
        .group = group,
        .parameter = parameter,
    };
}

static bool check_constant_type_attribute(CheckConstantTypeFrame* frame, CheckConstantTypeAttribute* attributes,
                                          uint32_t* count, uint32_t capacity, CheckConstantAttributes* value)
{
    const bool valid = *count < capacity;
    if (valid)
    {
        const uint32_t index = (*count)++;
        attributes[index] = (CheckConstantTypeAttribute){ .attributes = value, .next = BUSTER_CHECK_NONE };
        if (frame->attribute_last != BUSTER_CHECK_NONE)
        {
            attributes[frame->attribute_last].next = index;
        }
        else
        {
            frame->attribute_first = index;
        }
        frame->attribute_last = index;
    }
    return valid;
}

// This intentionally does not call check_input_specifiers: that reader may evaluate an attribute operand.
// All identifier and tag lookup was captured by check_constant_bind in source order before this pass.
static TypeId check_constant_specifiers(CheckConstantInput* input, uint32_t* cursor, uint32_t end, bool parameter,
                                       CheckConstantTypeFrame* frame, CheckConstantTypeAttribute* links, uint32_t* link_count, uint32_t capacity)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    uint32_t qualifiers = 0;
    uint32_t longs = 0;
    uint32_t shorts = 0;
    uint32_t signedness = 0;
    TypeId base = 0;
    bool integer_seen = false;
    bool atomic = false;
    bool complex = false;
    bool named_type = false;
    bool bit_int_specifier = false;
    bool register_seen = false;
    bool saw = false;
    bool valid = true;
    bool reading = true;
    while (reading && i < end)
    {
        const uint32_t name = parser->tokens.names[i];
        const uint8_t kind = parser->tokens.kinds[i];
        bool consumed = true;
        if (input->attribute_starts[i - input->first] != 0)
        {
            i = input->attribute_starts[i - input->first]->end - 1;
        }
        else if (check_input_qualifier(name, &qualifiers))
        {
        }
        else if (kind == PARSE_KIND_STORAGE)
        {
            valid &= parameter && name == C_NAME_REGISTER && !register_seen;
            register_seen = true;
        }
        else if (name == C_NAME_COMPLEX_C99 || name == C_NAME_GNU_COMPLEX || name == C_NAME_GNU_COMPLEX2)
        {
            complex = true;
        }
        else if (kind == PARSE_KIND_ATOMIC && (i + 1 >= end || parser->tokens.kinds[i + 1] != PARSE_KIND_L_PAREN))
        {
            atomic = true;
        }
        else if ((kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC) && i + 1 < end &&
                 (parser->tree.roles[i + 1] == PARSE_ROLE_OPEN_SPECIFIER || (input->raw && parser->tokens.kinds[i + 1] == PARSE_KIND_L_PAREN)))
        {
            const uint32_t close = parser->tree.parents[i + 1];
            const TypeId selected = close > i + 1 && close < end ? input->queries[close - input->first] : 0;
            valid &= selected != 0 && base == 0;
            base = selected;
            named_type = true;
            bit_int_specifier = name == C_NAME_BITINT || name == C_NAME_EXTINT;
            frame->abi_origin = selected != 0 ? input->abi_origins[close - input->first] : 0;
            i = close > i + 1 && close < end ? close : i;
        }
        else if (name == C_NAME_LONG)
        {
            longs += 1;
        }
        else if (name == C_NAME_SHORT)
        {
            shorts = 1;
        }
        else if (name == C_NAME_SIGNED || name == C_NAME_GNU_SIGNED || name == C_NAME_GNU_SIGNED2 || name == C_NAME_UNSIGNED)
        {
            const uint32_t selected = name == C_NAME_UNSIGNED ? 2 : 1;
            valid &= signedness == 0 || signedness == selected;
            signedness = selected;
        }
        else if (name == C_NAME_INT)
        {
            valid &= !integer_seen;
            integer_seen = true;
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
        {
            const TypeId selected = reader->tag_types[i];
            valid &= selected != 0 && base == 0;
            base = selected;
            named_type = true;
            i = selected != 0 ? reader->tag_ends[i] - 1 : i;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            TypeId selected = 0;
            switch (name)
            {
                case C_NAME_CHAR: selected = BUSTER_TYPE_CHAR; break;
                case C_NAME_VOID: selected = BUSTER_TYPE_VOID; break;
                case C_NAME_BOOL: case C_NAME_BOOL_C99: selected = BUSTER_TYPE_BOOL; break;
                case C_NAME_FLOAT: selected = BUSTER_TYPE_FLOAT; break;
                case C_NAME_DOUBLE: selected = BUSTER_TYPE_DOUBLE; break;
                case C_NAME_INT128: selected = BUSTER_TYPE_INT128; break;
                case C_NAME_FP16: selected = BUSTER_TYPE_HALF; break;
                case C_NAME_FLOAT16: selected = BUSTER_TYPE_FLOAT16; break;
                case C_NAME_FLOAT128: selected = BUSTER_TYPE_FLOAT128; break;
                case C_NAME_BF16: selected = BUSTER_TYPE_BF16; break;
                default: break;
            }
            valid &= base == 0 && selected != 0;
            base = selected;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && (parser->tree.roles[i] == PARSE_ROLE_SPECIFIER || input->raw) && base == 0 && !integer_seen && !longs && !shorts && !signedness)
        {
            base = reader->declared_types[i];
            const uint32_t declaration = reader->output->declarations[i];
            frame->abi_origin = reader->output->explicit_abis != 0 && declaration < parser->tokens.count ? reader->output->explicit_abis[declaration] : 0;
            named_type = true;
            valid &= base != 0;
        }
        else
        {
            consumed = false;
            reading = false;
        }
        if (consumed)
        {
            saw = true;
            i += 1;
        }
    }
    valid &= saw && (base != 0 || integer_seen || longs || shorts || signedness || complex) && longs <= 2 && shorts <= 1 && !(longs && shorts);
    if (named_type && !bit_int_specifier)
    {
        valid &= !longs && !shorts && !signedness && !integer_seen && !complex;
    }
    else if (base == 0)
    {
        base = complex && !integer_seen && !longs && !shorts && !signedness ? BUSTER_TYPE_DOUBLE :
               shorts ? (signedness == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
               longs == 2 ? (signedness == 2 ? BUSTER_TYPE_ULLONG : BUSTER_TYPE_LLONG) :
               longs == 1 ? (signedness == 2 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                            (signedness == 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT);
    }
    else if (base == BUSTER_TYPE_CHAR)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UCHAR : signedness == 1 ? BUSTER_TYPE_SCHAR : base;
    }
    else if (base == BUSTER_TYPE_DOUBLE)
    {
        valid &= longs <= 1 && !shorts && !signedness && !integer_seen;
        base = longs ? BUSTER_TYPE_LONG_DOUBLE : base;
    }
    else if (base == BUSTER_TYPE_INT128)
    {
        valid &= !longs && !shorts && !integer_seen;
        base = signedness == 2 ? BUSTER_TYPE_UINT128 : base;
    }
    else if (type_kind(reader->types, base) == BUSTER_TYPE_KIND_BIT_INT)
    {
        valid &= !longs && !shorts && !integer_seen;
        const uint64_t width = reader->types->types.extent[type_index(base)];
        base = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, signedness == 2)) : 0;
    }
    else
    {
        valid &= !longs && !shorts && !signedness && !integer_seen;
    }
    if ((qualifiers & BUSTER_TYPE_RESTRICT) != 0)
    {
        valid &= type_kind(reader->types, base) == BUSTER_TYPE_KIND_POINTER &&
                 type_kind(reader->types, type_element(reader->types, base)) != BUSTER_TYPE_KIND_FUNCTION;
    }
    if (valid && complex)
    {
        const uint8_t code = type_arithmetic_code(reader->types, base);
        valid = code < 64 && ((BUSTER_TYPE_COMPLEX_SPELLABLE >> code) & 1) != 0;
        base = valid ? check_input_type_result(reader, type_complex(reader->types, base)) : 0;
    }
    if (valid && atomic)
    {
        base = check_input_type_result(reader, type_atomic(reader->types, base));
    }
    // Only attributes belonging to this specifier sequence affect its base. Attribute groups inside a tag or
    // nested typeof already belong to those independently completed type jobs.
    for (uint32_t token = *cursor; valid && token < i;)
    {
        CheckConstantAttributes* attributes = input->attribute_starts[token - input->first];
        const uint8_t kind = parser->tokens.kinds[token];
        if (attributes != 0)
        {
            base = check_input_attribute_type(reader, &attributes->parsed, base, false);
            valid = base != 0 && check_constant_type_attribute(frame, links, link_count, capacity, attributes);
            token = attributes->end;
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
        {
            token = reader->tag_ends[token] > token ? reader->tag_ends[token] : token + 1;
        }
        else if (kind == PARSE_KIND_L_PAREN && parser->tree.parents[token] > token && parser->tree.parents[token] < i)
        {
            token = parser->tree.parents[token] + 1;
        }
        else
        {
            token += 1;
        }
    }
    *cursor = i;
    return valid ? check_input_type_result(reader, type_qualify(reader->types, base, qualifiers)) : 0;
}

static uint32_t check_constant_range_root(const CheckConstantInput* input, uint32_t first, uint32_t end)
{
    const Parser* parser = input->reader->parser;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    if (input->raw && first < end)
    {
        root = first;
        roots = 1;
    }
    for (uint32_t token = first; !input->raw && token < end; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent < first || parent >= end)
        {
            root = token;
            roots += 1;
        }
    }
    return roots == 1 && root >= input->first && root < input->end && input->present[root - input->first] ? root : BUSTER_CHECK_NONE;
}

static bool check_constant_range_value(CheckConstantInput* input, uint32_t first, uint32_t end, CheckValue* value)
{
    const uint32_t root = check_constant_range_root(input, first, end);
    const bool valid = root != BUSTER_CHECK_NONE;
    if (valid)
    {
        *value = input->values[root - input->first];
    }
    return valid;
}

static bool check_constant_raw_type_start(CheckConstantInput* input, uint32_t token);
static bool check_constant_raw_parameters(CheckConstantInput* input, uint32_t open, uint32_t close);

static TypeId check_constant_type(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    CheckInputReader* reader = input->reader;
    reader->constant_abi_origin = 0;
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t capacity = end - first + 1;
    CheckConstantTypeFrame* frames = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantTypeFrame), 8);
    CheckConstantPart* parts = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantPart), 8);
    CheckConstantParameter* parameters = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantParameter), 4);
    CheckConstantTypeAttribute* attributes = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConstantTypeAttribute, capacity);
    reader->output->resource |= frames == 0 || parts == 0 || parameters == 0 || attributes == 0;
    uint32_t cursor = first;
    TypeId result = 0;
    uint32_t attribute_count = 0;
    if (!reader->output->resource)
    {
        frames[0] = check_constant_type_frame(0, first, end, false, false);
    }
    const TypeId base = !reader->output->resource ? check_constant_specifiers(input, &cursor, end, false, frames, attributes, &attribute_count, capacity) : 0;
    bool valid = base != 0;
    uint32_t depth = 0;
    uint32_t part_count = 0;
    uint32_t parameter_count = 0;
    if (valid)
    {
        frames[0].base = base;
        frames[0].cursor = cursor;
        depth = 1;
    }
    while (valid && depth != 0)
    {
        CheckConstantTypeFrame* frame = &frames[depth - 1];
        uint32_t i = frame->cursor;
        if (frame->state == 0)
        {
            while (valid && i < frame->end && (parser->tokens.kinds[i] == PARSE_KIND_STAR || input->attribute_starts[i - input->first] != 0))
            {
                CheckConstantAttributes* attribute = input->attribute_starts[i - input->first];
                if (attribute != 0)
                {
                    valid = check_constant_type_attribute(frame, attributes, &attribute_count, capacity, attribute);
                    i = attribute->end;
                    continue;
                }
                const uint32_t token = i++;
                uint32_t qualifiers = 0;
                bool atomic = false;
                while (valid && i < frame->end && (input->attribute_starts[i - input->first] != 0 ||
                       check_input_qualifier(parser->tokens.names[i], &qualifiers) || parser->tokens.names[i] == C_NAME_ATOMIC))
                {
                    CheckConstantAttributes* pointer_attribute = input->attribute_starts[i - input->first];
                    if (pointer_attribute != 0)
                    {
                        valid = check_constant_type_attribute(frame, attributes, &attribute_count, capacity, pointer_attribute);
                        i = pointer_attribute->end;
                    }
                    else
                    {
                        atomic |= parser->tokens.names[i] == C_NAME_ATOMIC;
                        i += 1;
                    }
                }
                parts[part_count] = (CheckConstantPart)
                {
                    .next = frame->pointers,
                    .token = token,
                    .qualifiers = qualifiers,
                    .kind = BUSTER_TYPE_KIND_POINTER,
                    .flags = atomic,
                };
                frame->pointers = part_count++;
            }
            frame->state = 1;
            if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_IDENTIFIER && parser->tree.roles[i] == PARSE_ROLE_OPERAND)
            {
                frame->name = i++;
            }
            else if (i < frame->end && parser->tree.roles[i] == PARSE_ROLE_OPEN_GROUP &&
                     (!input->raw || (i + 1 < frame->end && !check_constant_raw_parameters(input, i, parser->tree.parents[i]))))
            {
                const uint32_t close = parser->tree.parents[i];
                valid = close > i && close < frame->end;
                if (valid)
                {
                    frame->cursor = close + 1;
                    frames[depth++] = check_constant_type_frame(frame->base, i + 1, close, true, false);
                }
            }
            frame->cursor = frame == &frames[depth - 1] ? i : frame->cursor;
        }
        else if (frame->state == 2)
        {
            CheckConstantPart* function = &parts[frame->function];
            const uint32_t close = parser->tree.parents[function->token];
            if (i == close)
            {
                frame->cursor = close + 1;
                frame->state = 1;
            }
            else if (i < close && parser->tokens.kinds[i] == PARSE_KIND_ELLIPSIS)
            {
                valid = (function->parameter_count != 0 || parser->language.standard >= 2023) && i + 1 == close;
                function->flags |= BUSTER_TYPE_FLAG_VARIADIC;
                frame->cursor = i + 1;
            }
            else if (i < close)
            {
                frames[depth] = check_constant_type_frame(0, i, close, false, true);
                const TypeId parameter = check_constant_specifiers(input, &i, close, true, frames + depth, attributes, &attribute_count, capacity);
                valid = parameter != 0;
                if (valid)
                {
                    frames[depth].base = parameter;
                    frames[depth].cursor = i;
                    depth += 1;
                }
            }
            else
            {
                valid = false;
            }
        }
        else if (i < frame->end && input->attribute_starts[i - input->first] != 0)
        {
            CheckConstantAttributes* attribute = input->attribute_starts[i - input->first];
            valid = check_constant_type_attribute(frame, attributes, &attribute_count, capacity, attribute);
            frame->cursor = attribute->end;
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            CheckConstantPart part =
            {
                .next = BUSTER_CHECK_NONE,
                .token = i,
                .kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
            };
            if (valid && i + 1 != close)
            {
                CheckValue bound = { 0 };
                int64_t extent = 0;
                valid = check_constant_range_value(input, i + 1, close, &bound) && check_action_is_integer(reader->types, bound.type);
                if (valid && (check_input_integer_attribute(reader, &bound) || (bound.flags & BUSTER_CHECK_ICE) != 0))
                {
                    valid = check_action_i64(reader->types, &bound, &extent) && extent >= 0;
                    part.kind = BUSTER_TYPE_KIND_ARRAY;
                    part.extent = (uint64_t)extent;
                    if (valid)
                    {
                        check_input_constant_bound_warning(reader, i + 1, close, &bound);
                    }
                }
                else if (valid)
                {
                    part.kind = BUSTER_TYPE_KIND_VARIABLE_ARRAY;
                    part.extent = (uint64_t)i + 2;
                }
            }
            if (valid)
            {
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = part;
                frame->last = part_count++;
                frame->cursor = close + 1;
            }
        }
        else if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_L_PAREN &&
                 (input->raw || parser->tree.roles[i] == PARSE_ROLE_OPEN_FUNCTION || parser->tree.roles[i] == PARSE_ROLE_OPEN_CALL))
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < frame->end;
            if (valid)
            {
                if (frame->last != BUSTER_CHECK_NONE)
                {
                    parts[frame->last].next = part_count;
                }
                else
                {
                    frame->first = part_count;
                }
                parts[part_count] = (CheckConstantPart)
                {
                    .next = BUSTER_CHECK_NONE,
                    .token = i,
                    .parameter_first = BUSTER_CHECK_NONE,
                    .parameter_last = BUSTER_CHECK_NONE,
                    .kind = BUSTER_TYPE_KIND_FUNCTION,
                    .flags = i + 1 != close || parser->language.standard >= 2023 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0,
                };
                frame->function = part_count;
                frame->last = part_count++;
                frame->state = 2;
                frame->cursor = i + 1;
            }
        }
        else
        {
            valid = frame->parameter ? i <= frame->end && (i == frame->end || parser->tokens.kinds[i] == PARSE_KIND_COMMA) : i == frame->end;
            if (frame->last != BUSTER_CHECK_NONE)
            {
                parts[frame->last].next = frame->pointers;
            }
            else
            {
                frame->first = frame->pointers;
            }
            if (frame->pointers != BUSTER_CHECK_NONE)
            {
                frame->last = frame->pointers;
                while (parts[frame->last].next != BUSTER_CHECK_NONE)
                {
                    frame->last = parts[frame->last].next;
                }
            }
            if (valid && frame->group)
            {
                CheckConstantTypeFrame* parent = &frames[depth - 2];
                parent->name = frame->name;
                parent->first = frame->first;
                parent->last = frame->last;
                if (frame->attribute_first != BUSTER_CHECK_NONE)
                {
                    if (parent->attribute_last != BUSTER_CHECK_NONE)
                    {
                        attributes[parent->attribute_last].next = frame->attribute_first;
                    }
                    else
                    {
                        parent->attribute_first = frame->attribute_first;
                    }
                    parent->attribute_last = frame->attribute_last;
                }
                depth -= 1;
            }
            else if (valid)
            {
                uint32_t reverse = BUSTER_CHECK_NONE;
                for (uint32_t p = frame->first; p != BUSTER_CHECK_NONE;)
                {
                    const uint32_t next = parts[p].next;
                    parts[p].next = reverse;
                    reverse = p;
                    p = next;
                }
                TypeId type = frame->base;
                for (uint32_t p = reverse; type != 0 && p != BUSTER_CHECK_NONE; p = parts[p].next)
                {
                    const CheckConstantPart* part = &parts[p];
                    if (part->kind == BUSTER_TYPE_KIND_POINTER)
                    {
                        const bool restricted_function = (part->qualifiers & BUSTER_TYPE_RESTRICT) != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION;
                        type = restricted_function ? 0 : check_input_type_result(reader, type_pointer(reader->types, type));
                        type = type != 0 && part->flags ? check_input_type_result(reader, type_atomic(reader->types, type)) : type;
                        type = type != 0 ? check_input_type_result(reader, type_qualify(reader->types, type, part->qualifiers)) : 0;
                    }
                    else if (part->kind == BUSTER_TYPE_KIND_FUNCTION)
                    {
                        frame->abi_origin = 0;
                        const uint64_t parameter_mark = reader->scratch->length;
                        TypeId* types = arena_push_array(reader->scratch, (uint64_t)part->parameter_count + 1, sizeof(TypeId), 4);
                        reader->output->resource |= types == 0;
                        uint32_t count = 0;
                        for (uint32_t parameter = part->parameter_first; types != 0 && parameter != BUSTER_CHECK_NONE; parameter = parameters[parameter].next)
                        {
                            types[count++] = parameters[parameter].type;
                        }
                        type = types != 0 ? check_input_type_result(reader, type_function(reader->types, type, types, count, part->flags)) : 0;
                        arena_pop_to(reader->scratch, parameter_mark);
                    }
                    else
                    {
                        type = check_input_type_result(reader, part->kind == BUSTER_TYPE_KIND_ARRAY ? type_array(reader->types, type, part->extent) :
                               part->kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY ? type_variable_array(reader->types, type, (uint32_t)part->extent) :
                                                                              type_incomplete_array(reader->types, type));
                    }
                }
                for (uint32_t attribute = frame->attribute_first; type != 0 && attribute != BUSTER_CHECK_NONE; attribute = attributes[attribute].next)
                {
                    CheckInputAttributes* parsed = &attributes[attribute].attributes->parsed;
                    frame->abi_origin = check_input_attribute_abi_origin(reader, parsed, frame->abi_origin);
                    type = check_input_attribute_abi(reader, parsed, type);
                }
                valid = type != 0;
                if (valid && frame->parameter)
                {
                    CheckConstantTypeFrame* parent = &frames[depth - 2];
                    CheckConstantPart* function = &parts[parent->function];
                    const bool only_void = type == BUSTER_TYPE_VOID && frame->name == BUSTER_CHECK_NONE && function->parameter_count == 0 && i == frame->end;
                    if (!only_void)
                    {
                        const TypeId adjusted = check_input_type_result(reader, type_adjust_parameter(reader->types, type));
                        valid = adjusted != 0 && type_kind(reader->types, adjusted) != BUSTER_TYPE_KIND_VOID;
                        if (valid)
                        {
                            const uint32_t name = frame->name != BUSTER_CHECK_NONE ? parser->tokens.names[frame->name] : 0;
                            parameters[parameter_count] = (CheckConstantParameter)
                            {
                                .type = adjusted,
                                .next = BUSTER_CHECK_NONE,
                                .name = name,
                            };
                            valid = check_constant_parameter_unique(function, parameters, parameter_count);
                            if (function->parameter_last != BUSTER_CHECK_NONE)
                            {
                                parameters[function->parameter_last].next = parameter_count;
                            }
                            else
                            {
                                function->parameter_first = parameter_count;
                            }
                            function->parameter_last = parameter_count++;
                            function->parameter_count += 1;
                        }
                    }
                    parent->cursor = i;
                    if (i < frame->end && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                    {
                        parent->cursor = i + 1;
                        valid &= i + 1 < frame->end;
                    }
                    depth -= 1;
                }
                else if (valid)
                {
                    valid = frame->name == BUSTER_CHECK_NONE;
                    result = type;
                    reader->constant_abi_origin = frame->abi_origin;
                    depth -= 1;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid ? result : 0;
}

// Mark only declarator syntax. Array-bound expressions remain ordinary expression nodes and can themselves
// contain casts/type queries; the increasing source-order pass will refine each nested type range afterward.
static void check_constant_mark_type(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    const Parser* parser = input->reader->parser;
    for (uint32_t token = first; token < end; token += 1)
    {
        input->syntax[token - input->first] = true;
    }
    for (uint32_t token = first; token < end; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE)
        {
            const uint32_t close = parser->tree.parents[token];
            if (close > token && close < end)
            {
                for (uint32_t item = token + 1; item < close; item += 1)
                {
                    input->syntax[item - input->first] = false;
                }
                token = close;
            }
        }
    }
}

static uint32_t check_constant_punctuation(const Parser* parser, uint32_t first, uint32_t end, uint8_t punctuation)
{
    uint32_t token = first;
    while (token < end && parser->tokens.kinds[token] != punctuation)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
        {
            const uint32_t close = parser->tree.parents[token];
            token = close > token && close < end ? close + 1 : end;
        }
        else
        {
            token += 1;
        }
    }
    return token;
}

static uint32_t check_constant_separator(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    return check_constant_punctuation(input->reader->parser, first, end, PARSE_KIND_COMMA);
}

#include "c_check_intrinsic_input.c"

// Only a direct call captured by the intrinsic adapter supplies a prototype. During source-order declaration
// queries prepare that same record; after binding replay use the captured record, since its scope has closed.
// The constant reader checks it as an ordinary call, retaining a nonconstant result with the exact return type.
static TypeId check_constant_intrinsic_type(CheckConstantInput* input, uint32_t token, uint32_t handle)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    TypeId type = 0;
    if (handle == 0 && parser->tree.parents[token] < input->end && check_input_intrinsic_call_shape(parser, token))
    {
        const uint32_t close = parser->tree.parents[token];
        const bool prepared = input->cached ? reader->output->intrinsic_ids != 0 && reader->output->intrinsic_ids[close] != 0 :
                              check_input_intrinsic_prepare(reader, token) == CHECK_INPUT_INTRINSIC_PREPARED;
        if (prepared)
        {
            type = check_input_intrinsic_type(reader, token);
        }
    }
    return type;
}

static bool check_constant_type_start(const Parser* parser, uint32_t token)
{
    const uint8_t kind = parser->tokens.kinds[token];
    return parser->tree.roles[token] == PARSE_ROLE_SPECIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
           kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC;
}

static bool check_constant_raw_type_start(CheckConstantInput* input, uint32_t token);
static bool check_constant_raw_expression(CheckConstantInput* input, uint32_t first, uint32_t end);

// These builtins have two source operands but only one value operand. Keep the destination as a type job so
// nested typeof/attribute queries finish before the call, without entering the source declaration reader.
static bool check_constant_type_builtin_prepare(CheckConstantInput* input, uint32_t opening, uint16_t builtin)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t closing = parser->tree.parents[opening];
    const uint32_t separator = closing < input->end ? check_constant_punctuation(parser, opening + 1, closing, PARSE_KIND_COMMA) : closing;
    const bool valid = opening + 1 < separator && separator + 1 < closing && closing < input->end &&
                       check_constant_punctuation(parser, separator + 1, closing, PARSE_KIND_COMMA) == closing;
    if (valid)
    {
        const bool first_type = builtin == CBUILTIN_BIT_CAST;
        const uint32_t first = first_type ? opening + 1 : separator + 1;
        const uint32_t end = first_type ? separator : closing;
        input->syntax[opening - 1 - input->first] = true;
        input->type_begin[closing - input->first] = first;
        check_constant_mark_type(input, first, end);
        reader->builtins[closing] = builtin;
    }
    return valid;
}

// Attribute syntax is scanned once, without evaluating its operands. Each group is an explicit dependency job:
// normal expressions complete it at the group's postorder close; raw attribute tokens complete it inside out.
static bool check_constant_attribute_prepare(CheckConstantInput* input, uint32_t owner, uint32_t* cursor)
{
    CheckInputReader* reader = input->reader;
    const uint32_t first = *cursor;
    bool valid = true;
    if (check_input_attribute_starts(reader->parser, first, input->end))
    {
        CheckConstantAttributes* job = arena_push(reader->scratch, sizeof(CheckConstantAttributes), 8);
        reader->output->resource |= job == 0;
        valid = job != 0;
        if (valid)
        {
            *job = (CheckConstantAttributes)
            {
                .owner = owner,
                .publish = !input->cached && reader->output->attributes[owner] == 0,
            };
            valid = check_input_attributes(reader, cursor, input->end, &job->parsed);
        }
        if (valid)
        {
            job->end = *cursor;
            input->attribute_starts[first - input->first] = job;
            input->attribute_ends[*cursor - 1 - input->first] = job;
            memset(input->attribute_syntax + first - input->first, 1, *cursor - first);
            memset(input->syntax + first - input->first, 1, *cursor - first);
            for (uint32_t i = 0; valid && i < job->parsed.count; i += 1)
            {
                const CheckInputAttribute* attribute = &job->parsed.records[i];
                const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
                const bool list = attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST || attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
                for (uint32_t argument = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS; valid && !attribute->resolved &&
                     (scalar || list) && argument < (scalar ? 1 : attribute->argument_count); argument += 1)
                {
                    uint32_t begin = attribute->argument_first;
                    uint32_t end = attribute->argument_end;
                    valid = scalar || check_input_attribute_argument(attribute, argument, &begin, &end);
                    if (valid)
                    {
                        memset(input->attribute_syntax + begin - input->first, 0, end - begin);
                        memset(input->syntax + begin - input->first, 0, end - begin);
                        const bool type = attribute->kind == CHECK_ATTRIBUTE_ALIGNAS &&
                                          (input->raw ? check_constant_raw_type_start(input, begin) : check_constant_type_start(reader->parser, begin));
                        if (type)
                        {
                            check_constant_mark_type(input, begin, end);
                        }
                    }
                }
            }
        }
    }
    return valid;
}

static bool check_constant_tag_prepare(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t cursor = token + 1;
    bool valid = check_constant_attribute_prepare(input, token, &cursor);
    if (valid && cursor < input->end && parser->tokens.kinds[cursor] == PARSE_KIND_IDENTIFIER)
    {
        input->attribute_syntax[cursor - input->first] = true;
        cursor += 1;
    }
    valid = valid && check_constant_attribute_prepare(input, token, &cursor);
    if (valid)
    {
        input->tag_next[token - input->first] = input->tag_finishes[cursor - input->first];
        input->tag_finishes[cursor - input->first] = token + 1;
    }
    return valid;
}

// The ordered source reader can complete an inline record before entering this evaluator. Its member types,
// attribute operands and binding events are already retained; revisiting those tokens would both duplicate
// work and move the binding stream backwards. Consume that exact completed identity as one type specifier.
static uint32_t check_constant_completed_tag(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t end = token;
    const uint8_t kind = parser->tokens.kinds[token];
    if (input->syntax[token - input->first] && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM) && reader->tag_types[token] != 0)
    {
        const uint32_t body = check_input_tag_body(parser, token, input->end);
        const uint32_t close = body < input->end && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? parser->tree.parents[body] : input->end;
        const uint32_t after = reader->tag_ends[token];
        if (body < close && close < after && after <= input->end && type_is_complete(reader->types, reader->tag_types[token]))
        {
            memset(input->syntax + token - input->first, 1, after - token);
            memset(input->attribute_syntax + token - input->first, 1, after - token);
            end = after;
        }
    }
    return end;
}

// A tag activation follows all operands in its header. Deferring this small job preserves the binding stream's
// monotonically increasing source points even when an attribute introduces a nested, previously unseen tag.
static bool check_constant_tag_finish(CheckConstantInput* input, uint32_t position)
{
    bool valid = true;
    for (uint32_t next = input->tag_finishes[position - input->first]; valid && next != 0;)
    {
        const uint32_t token = next - 1;
        const bool definition = input->deferred_tags && position < input->end &&
                                input->reader->parser->tokens.kinds[position] == PARSE_KIND_L_BRACE;
        valid = (input->cached ? input->reader->tag_types[token] : check_input_tag_identity(input->reader, token, input->end, definition)) != 0;
        if (valid && definition)
        {
            input->pending_tag = token;
        }
        next = input->tag_next[token - input->first];
    }
    return valid;
}

static bool check_constant_attribute_evaluate(CheckConstantInput* input, CheckConstantAttributes* job)
{
    CheckInputReader* reader = input->reader;
    bool valid = true;
    for (uint32_t i = 0; valid && i < job->parsed.count; i += 1)
    {
        const CheckInputAttribute* attribute = &job->parsed.records[i];
        const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
        const bool list = attribute->schema == CHECK_ATTRIBUTE_INTEGER_LIST || attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
        for (uint32_t argument = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS; valid && !attribute->resolved &&
             (scalar || list) && argument < (scalar ? 1 : attribute->argument_count); argument += 1)
        {
            uint32_t first = attribute->argument_first;
            uint32_t end = attribute->argument_end;
            valid = scalar || check_input_attribute_argument(attribute, argument, &first, &end);
            const bool type_argument = valid && attribute->kind == CHECK_ATTRIBUTE_ALIGNAS &&
                                       (input->raw ? check_constant_raw_type_start(input, first) : check_constant_type_start(reader->parser, first));
            int64_t integer = 0;
            if (valid && type_argument)
            {
                const TypeId type = check_constant_type(input, first, end);
                valid = type != 0 && type_is_complete(reader->types, type);
                integer = valid ? (int64_t)1 << type_alignment_log2(reader->types, type) : 0;
            }
            else if (valid)
            {
                CheckValue value = { 0 };
                valid = (!input->raw || check_constant_raw_expression(input, first, end)) &&
                        check_constant_range_value(input, first, end, &value) && check_input_integer_attribute(reader, &value) &&
                        check_action_i64(reader->types, &value, &integer);
                if (valid)
                {
                    check_input_constant_warning(reader, first, end, &value, true);
                }
            }
            valid = valid && (scalar ? check_input_attribute_value(reader, &job->parsed, i, integer) :
                                       check_input_attribute_argument_value(reader, &job->parsed, i, argument, integer));
        }
    }
    if (valid && job->publish)
    {
        CheckInputAttributes* combined = arena_push(reader->arena, sizeof(CheckInputAttributes), 8);
        reader->output->resource |= combined == 0;
        valid = combined != 0;
        if (valid)
        {
            const CheckInputAttributes empty = { 0 };
            CheckInputAttributes* previous = reader->output->attributes[job->owner];
            valid = check_input_attribute_merge(reader, previous != 0 ? previous : &empty, &job->parsed, combined);
            reader->output->attributes[job->owner] = valid ? combined : previous;
            job->publish = !valid;
        }
    }
    return valid;
}

static uint16_t check_constant_integer_builtin(CheckConstantInput* input, uint32_t token, uint32_t handle)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t open = token + 1;
    const uint32_t close = open < input->end ? parser->tree.parents[open] : input->end;
    uint16_t builtin = CBUILTIN_NONE;
    if (handle == 0 && open < close && close < input->end && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN &&
        (input->raw || parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL))
    {
        builtin = reader->builtins[close];
        if (!input->cached)
        {
            const uint32_t identifier = parser->tokens.names[token];
            builtin = reader->builtin_names[identifier];
            if (builtin == UINT16_MAX)
            {
                const InternSpan span = reader->pp->table->spans[identifier];
                const char* spelling = (const char*)reader->pp->table->records + span.offset;
                builtin = (uint16_t)cbuiltin_resolve(spelling, span.length);
                reader->builtin_names[identifier] = builtin;
            }
            if (builtin > CBUILTIN_NONE && (builtin < CBUILTIN_COUNT || check_input_complex_builtin((CBuiltinId)builtin) || check_graph_builtin_op((CBuiltinId)builtin) != CHECK_OP_COUNT))
            {
                reader->builtins[close] = builtin;
            }
        }
    }
    return builtin < CBUILTIN_COUNT || check_input_complex_builtin((CBuiltinId)builtin) || check_graph_builtin_op((CBuiltinId)builtin) != CHECK_OP_COUNT ? builtin : CBUILTIN_NONE;
}

// A deferred typeof region has already passed through the ordered declaration reader. Its declaration tokens
// are syntax, while initializer expressions and ordinary statements still need type actions. Bit 1 marks that
// syntax; bit 2 flows once up each declaration's parser edges, stopping at its statement terminator. This also
// makes a final declaration yield void without confusing an initializer's value with the region result.
static void check_constant_source_regions(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t region_end = input->first;
    for (uint32_t token = input->first; input->cached && token < input->end; token += 1)
    {
        if (parser->tree.roles[token] == PARSE_ROLE_OPEN_GROUP && token + 1 < input->end &&
            parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_BLOCK && parser->tree.parents[token] < input->end)
        {
            const uint32_t end = parser->tree.parents[token] + 1;
            region_end = end > region_end ? end : region_end;
        }
        if (token < region_end)
        {
            const uint32_t index = token - input->first;
            if (reader->ignored[token])
            {
                input->statement_syntax[index] |= 1;
                input->syntax[index] = true;
            }
            if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && reader->output->declarations[token] == token &&
                reader->output->declaration_bodies[token] == reader->function_body)
            {
                input->statement_syntax[index] |= 2;
            }
        }
    }
}

// A tag that a prototype's list names first has the prototype's scope (C11 6.2.1p4) and ends at the list's `)`, as
// check_input_declarator ends a declarator's (#437): `typeof(int (*)(struct Z *))`'s Z isn't a later `struct Z`. The
// binding mark at the `(` waits at its `)`, since binding may yield inside the list for a tag's body and resume there.
// The list is a scope a level deeper than the one around it, as a declarator's is (check_input_declaration_depth), so
// that what a definition in it declares, read while binding waits, makes a tag of its own and conflicts with no name
// outside the list (`int f(int K, int a[sizeof(void (*)(enum E { K }))])`, #438).
static void check_constant_prototype_open(CheckConstantInput* input, uint32_t token)
{
    const uint32_t close = input->reader->parser->tree.parents[token];
    if (!input->cached && close > token && close < input->end)
    {
        input->prototype_marks[close - input->first] = c_bindings_mark(&input->reader->bindings) + 1;
        input->reader->prototype_depth += 1;
    }
}

static bool check_constant_prototype_close(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const uint32_t mark = input->prototype_marks[token - input->first];
    const bool valid = mark == 0 || c_bindings_mark(&reader->bindings) == mark - 1 || c_bindings_restore(&reader->bindings, mark - 1, token);
    reader->output->resource |= !valid;
    reader->prototype_depth -= mark != 0;
    return valid;
}

static bool check_constant_bind(CheckConstantInput* input, bool type_name)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    if (type_name)
    {
        check_constant_mark_type(input, input->first, input->end);
    }
    for (uint32_t token = input->bind_cursor; valid && token < input->end; token += 1)
    {
        const uint32_t index = token - input->first;
        valid = check_constant_tag_finish(input, token);
        if (!valid && input->failed_token != 0)
        {
            *input->failed_token = token;
        }
        if (input->pending_tag != BUSTER_CHECK_NONE)
        {
            input->bind_cursor = token;
            break;
        }
        valid = valid && check_constant_prototype_close(input, token);
        if (!valid || input->attribute_syntax[index] || (input->statement_syntax[index] & 1) != 0)
        {
            continue;
        }
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t name = parser->tokens.names[token];
        if (input->syntax[index] && check_input_attribute_starts(parser, token, input->end))
        {
            uint32_t after = token;
            valid = check_constant_attribute_prepare(input, token, &after);
            if (valid)
            {
                input->attribute_starts[index]->publish = false;
            }
        }
        const uint32_t completed = check_constant_completed_tag(input, token);
        if (completed != token)
        {
            token = completed - 1;
            continue;
        }
        if (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t next = token + 1;
            valid = close > token && close < input->end;
            if (valid && next < close && check_constant_type_start(parser, next))
            {
                input->type_begin[close - input->first] = next;
                check_constant_mark_type(input, next, close);
            }
        }
        else if ((role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL) && input->syntax[index] && kind == PARSE_KIND_L_PAREN)
        {
            check_constant_prototype_open(input, token);
        }
        else if (role == PARSE_ROLE_OPEN_SPECIFIER && token > input->first)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t next = token + 1;
            valid = close > next && close < input->end;
            if (valid)
            {
                input->syntax[close - input->first] = false;
                if (check_constant_type_start(parser, next))
                {
                    input->type_begin[close - input->first] = next;
                    check_constant_mark_type(input, next, close);
                }
                else
                {
                    for (uint32_t item = next; item < close; item += 1)
                    {
                        input->syntax[item - input->first] = false;
                    }
                }
            }
        }
        else if (role == PARSE_ROLE_OPEN_KEYWORD && token > input->first)
        {
            const uint32_t keyword = parser->tokens.names[token - 1];
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < input->end;
            if (valid && (keyword == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || keyword == C_NAME_BUILTIN_OFFSETOF))
            {
                const uint32_t separator = check_constant_separator(input, token + 1, close);
                valid = separator < close;
                if (valid)
                {
                    check_constant_mark_type(input, token + 1, separator);
                    input->syntax[separator - input->first] = true;
                    check_constant_mark_type(input, separator + 1, close);
                }
            }
            else if (valid && keyword == C_NAME_GENERIC)
            {
                const uint32_t separator = check_constant_separator(input, token + 1, close);
                valid = separator < close;
                uint32_t association = separator + 1;
                while (valid && association < close)
                {
                    const uint32_t colon = check_constant_punctuation(parser, association, close, PARSE_KIND_COLON);
                    const uint32_t after = colon < close ? check_constant_separator(input, colon + 1, close) : close;
                    valid = colon < close && colon + 1 < after;
                    if (valid)
                    {
                        check_constant_mark_type(input, association, colon);
                        input->syntax[colon - input->first] = true;
                        association = after + 1;
                    }
                }
            }
            else if (valid && (keyword == C_NAME_BUILTIN_CONVERTVECTOR || keyword == C_NAME_BUILTIN_BIT_CAST))
            {
                valid = check_constant_type_builtin_prepare(input, token, keyword == C_NAME_BUILTIN_BIT_CAST ? CBUILTIN_BIT_CAST : CBUILTIN_CONVERTVECTOR);
            }
        }
        if (valid && input->syntax[index] && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM))
        {
            valid = check_constant_tag_prepare(input, token);
        }
        else if (valid && kind == PARSE_KIND_IDENTIFIER && role != PARSE_ROLE_MEMBER && role != PARSE_ROLE_TAG && role != PARSE_ROLE_LEAF_RIGHT &&
                 (!input->syntax[index] || role == PARSE_ROLE_SPECIFIER))
        {
            const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const uint16_t builtin = !input->syntax[index] ? check_constant_integer_builtin(input, token, handle) : CBUILTIN_NONE;
            const TypeId intrinsic = !input->syntax[index] && builtin == CBUILTIN_NONE ? check_constant_intrinsic_type(input, token, handle) : 0;
            if (builtin != CBUILTIN_NONE)
            {
                input->syntax[index] = true;
            }
            else if (input->cached || intrinsic != 0)
            {
                valid = intrinsic != 0 || reader->declared_types[token] != 0 || role == PARSE_ROLE_OPERAND;
            }
            else if (handle != 0)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                const bool is_typedef = reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
                valid = is_typedef == (role == PARSE_ROLE_SPECIFIER);
                if (valid)
                {
                    reader->output->declarations[token] = declaration;
                    reader->declared_types[token] = reader->declared_types[declaration];
                    if (reader->output->overload_heads != 0)
                    {
                        reader->output->overload_heads[token] = reader->output->overload_heads[declaration];
                    }
                    if (c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, token) == 0)
                    {
                        reader->output->resource = true;
                        valid = false;
                    }
                }
            }
            else if (role == PARSE_ROLE_SPECIFIER)
            {
                reader->declared_types[token] = check_input_predefined_type(reader, name);
                valid = reader->declared_types[token] != 0;
            }
            else
            {
                // Preserve missing operands for the semantic pass. A later undeclared name must not hide
                // an earlier invalid sizeof/type query in the same bound merely because binding runs first.
                valid = role == PARSE_ROLE_OPERAND;
            }
        }
        if (!valid && input->failed_token != 0)
        {
            *input->failed_token = token;
        }
    }
    if (valid && input->pending_tag == BUSTER_CHECK_NONE)
    {
        input->bind_cursor = input->end;
        valid = check_constant_tag_finish(input, input->end);
    }
    return valid;
}

static TypeId check_constant_control_type(CheckInputReader* reader, const CheckValue* value)
{
    TypeId type = type_conversion_operand(reader->types, value->type);
    const uint8_t kind = type_kind(reader->types, type);
    if (type_kind_is_array(kind))
    {
        type = check_input_type_result(reader, type_pointer(reader->types, type_element(reader->types, type)));
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        type = check_input_type_result(reader, type_pointer(reader->types, type));
    }
    return type;
}

static bool check_constant_selection(CheckConstantInput* input, uint32_t keyword, uint32_t first, uint32_t separator, uint32_t token, CheckValue* value, CheckValue* proof)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    CheckValue control = { 0 };
    bool valid = check_constant_range_value(input, first, separator, &control) && check_constant_resolve(input, &control, 0, &control);
    if (valid && keyword == C_NAME_BUILTIN_CHOOSE_EXPR)
    {
        const uint32_t next = check_constant_separator(input, separator + 1, token);
        bool condition = false;
        valid = next < token && check_input_integer_ice(reader, &control) && check_action_truth(reader->types, &control, &condition);
        if (valid)
        {
            valid = check_constant_range_value(input, condition ? separator + 1 : next + 1, condition ? next : token, value);
            if (valid)
            {
                const uint32_t root = check_constant_range_root(input, condition ? separator + 1 : next + 1, condition ? next : token);
                input->abi_origins[token - input->first] = input->abi_origins[root - input->first];
                if (reader->selected_tokens != NULL && (reader->selected_tokens[token] & CHECK_INPUT_SOURCE_SELECTION) != 0)
                {
                    reader->selected_tokens[token] = root | CHECK_INPUT_SOURCE_SELECTION;
                }
            }
            if (valid && input->proof_values != 0)
            {
                const uint32_t root = check_constant_range_root(input, condition ? separator + 1 : next + 1, condition ? next : token);
                *proof = input->proof_values[root - input->first];
            }
            check_input_constant_fold_warning(reader, first, separator, &control, true);
            check_input_warning_suppress(reader, condition ? next + 1 : separator + 1, condition ? token : next, CHECK_WARNING_INTEGER_OVERFLOW);
        }
    }
    else if (valid)
    {
        const uint64_t mark = reader->scratch->length;
        const TypeId control_type = check_constant_control_type(reader, &control);
        CheckGenericSet seen;
        valid = control_type != 0 && check_generic_initialize(reader, &seen, token - separator, control_type, token + 1);
        uint32_t chosen_first = BUSTER_CHECK_NONE;
        uint32_t chosen_end = 0;
        uint32_t default_first = BUSTER_CHECK_NONE;
        uint32_t default_end = 0;
        uint32_t association = separator + 1;
        while (valid && association < token)
        {
            const uint32_t colon = check_constant_punctuation(parser, association, token, PARSE_KIND_COLON);
            const uint32_t after = colon < token ? check_constant_separator(input, colon + 1, token) : token;
            valid = colon < token && colon + 1 < after;
            if (valid && parser->tokens.kinds[association] == PARSE_KIND_DEFAULT)
            {
                valid = association + 1 == colon && default_first == BUSTER_CHECK_NONE;
                default_first = colon + 1;
                default_end = after;
            }
            else if (valid)
            {
                const TypeId type = check_constant_type(input, association, colon);
                const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, type));
                valid = type != 0 && type_is_complete(reader->types, type) && kind != BUSTER_TYPE_KIND_FUNCTION &&
                        kind != BUSTER_TYPE_KIND_VOID && (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) == 0;
                bool matches = false;
                valid = valid && check_generic_association(reader, &seen, type, &matches);
                if (valid)
                {
                    if (matches)
                    {
                        valid &= chosen_first == BUSTER_CHECK_NONE;
                        chosen_first = colon + 1;
                        chosen_end = after;
                    }
                }
            }
            association = after + 1;
        }
        if (chosen_first == BUSTER_CHECK_NONE)
        {
            chosen_first = default_first;
            chosen_end = default_end;
        }
        valid &= chosen_first != BUSTER_CHECK_NONE;
        if (valid)
        {
            valid = check_constant_range_value(input, chosen_first, chosen_end, value);
            if (valid)
            {
                const uint32_t root = check_constant_range_root(input, chosen_first, chosen_end);
                input->abi_origins[token - input->first] = input->abi_origins[root - input->first];
                if (reader->selected_tokens != NULL && (reader->selected_tokens[token] & CHECK_INPUT_SOURCE_SELECTION) != 0)
                {
                    reader->selected_tokens[token] = root | CHECK_INPUT_SOURCE_SELECTION;
                }
            }
            if (valid && input->proof_values != 0)
            {
                const uint32_t root = check_constant_range_root(input, chosen_first, chosen_end);
                *proof = input->proof_values[root - input->first];
            }
            check_input_warning_suppress(reader, first, separator, check_constant_unevaluated_warnings());
            association = separator + 1;
            while (association < token)
            {
                const uint32_t colon = check_constant_punctuation(parser, association, token, PARSE_KIND_COLON);
                const uint32_t after = check_constant_separator(input, colon + 1, token);
                if (colon + 1 != chosen_first)
                {
                    check_input_warning_suppress(reader, colon + 1, after, CHECK_WARNING_INTEGER_OVERFLOW);
                }
                association = after + 1;
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

static bool check_constant_builtin_type(CheckConstantInput* input, uint32_t token, uint32_t name, uint32_t first, uint32_t separator, CheckValue* value)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = separator < token && (name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P || name == C_NAME_BUILTIN_OFFSETOF);
    TypeId type = valid ? check_constant_type(input, first, separator) : 0;
    valid &= type != 0;
    uint64_t number = 0;
    if (valid && name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P)
    {
        const TypeId other = check_constant_type(input, separator + 1, token);
        valid = other != 0;
        if (valid)
        {
            const TypeAnswer answer = type_compatible_at(reader->types, type_unqualified(type), type_unqualified(other), reader->transparent_unions, reader->types->tags.count, token + 1);
            reader->output->resource |= answer == TYPE_ANSWER_FULL;
            valid = answer != TYPE_ANSWER_FULL;
            number = answer == TYPE_ANSWER_YES;
        }
    }
    else if (valid)
    {
        uint32_t i = separator + 1;
        bool member_required = true;
        while (valid && i < token)
        {
            const uint8_t kind = parser->tokens.kinds[i];
            if (member_required && kind == PARSE_KIND_IDENTIFIER)
            {
                CheckAction action =
                {
                    .op = CHECK_OP_MEMBER,
                    .operands = { { .type = type, .category = CHECK_LVALUE, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = 1 } },
                    .member = parser->tokens.names[i],
                    .node = i,
                    .tag_completions = reader->tag_completions,
                    .transparent_unions = reader->transparent_unions,
                    .tag_completion_count = reader->types->tags.capacity,
                    .source_position = token + 1,
                    .extensions = check_input_expression_extensions(parser),
                };
                CheckActionResult checked;
                check_action_scalar(reader->types, &action, &checked);
                check_input_constant_warning_add(reader->output, action.node, (uint8_t)checked.warnings);
                reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
                valid = checked.diagnostic == CHECK_DIAG_NONE && (checked.value.flags & BUSTER_CHECK_BIT_FIELD) == 0 && checked.value.offset >= 0;
                valid &= (uint64_t)checked.value.offset <= UINT64_MAX - number;
                number += (uint64_t)checked.value.offset;
                type = checked.value.type;
                member_required = false;
                i += 1;
            }
            else if (!member_required && kind == PARSE_KIND_PERIOD)
            {
                member_required = true;
                i += 1;
            }
            else if (!member_required && kind == PARSE_KIND_L_SQUARE)
            {
                const uint32_t close = parser->tree.parents[i];
                CheckValue bound = { 0 };
                int64_t index = 0;
                valid = close > i && close < token && type_kind_is_array(type_kind(reader->types, type)) &&
                        check_constant_range_value(input, i + 1, close, &bound) && check_input_integer_constant(reader, &bound) &&
                        check_action_i64(reader->types, &bound, &index) && index >= 0;
                if (valid)
                {
                    type = type_element(reader->types, type);
                    const uint64_t size = type_size(reader->types, type);
                    valid = size == 0 || (uint64_t)index <= (UINT64_MAX - number) / size;
                    number += (uint64_t)index * size;
                }
                i = close + 1;
            }
            else
            {
                valid = false;
            }
        }
        valid &= !member_required;
    }
    if (valid)
    {
        *value = (CheckValue)
        {
            .type = name == C_NAME_BUILTIN_TYPES_COMPATIBLE_P ? BUSTER_TYPE_INT : BUSTER_TYPE_ULONG,
            .category = CHECK_RVALUE,
            .flags = BUSTER_CHECK_ICE,
            .constant_kind = CHECK_CONSTANT_INTEGER,
            .integer = number,
        };
    }
    return valid;
}

static bool check_constant_builtin(CheckConstantInput* input, uint32_t token, CheckValue* value, CheckValue* proof)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t keyword = reader->first_child[token];
    bool valid = keyword >= input->first && keyword + 1 < token;
    const uint32_t name = valid ? parser->tokens.names[keyword] : 0;
    const uint32_t first = keyword + 2;
    const uint32_t separator = valid ? check_constant_separator(input, first, token) : token;
    const bool selection = name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR;
    valid &= separator < token;
    if (valid && selection)
    {
        valid = check_constant_selection(input, name, first, separator, token, value, proof);
    }
    else if (valid)
    {
        valid = check_constant_builtin_type(input, token, name, first, separator, value);
    }
    return valid;
}

static TypeId check_constant_object_alignment(CheckInputReader* reader, uint32_t query, TypeId type)
{
    const Parser* parser = reader->parser;
    uint32_t first = query + 1;
    uint32_t end = first + 1;
    if (first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN)
    {
        end = parser->tree.parents[first] + 1;
    }
    while (first < end && end <= parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN &&
           parser->tree.parents[first] == end - 1)
    {
        first += 1;
        end -= 1;
    }
    if (first + 1 == end && end <= parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = reader->output->declarations[first];
        const CheckInputAttributes* attributes = declaration < parser->tokens.count ? reader->output->attributes[declaration] : 0;
        if (attributes != 0 && attributes->alignment_log2 > type_alignment_log2(reader->types, type) + 1 &&
            type_kind(reader->types, type) != BUSTER_TYPE_KIND_FUNCTION)
        {
            type = check_input_type_result(reader, type_aligned(reader->types, type, attributes->alignment_log2 - 1));
        }
    }
    return type;
}

// Type-only declaration queries retain a runtime sizeof result. The source graph subsequently constructs the
// actual captured-extent dependencies; this pass must neither invent a constant size nor evaluate a bound twice.
static void check_constant_runtime_query(const CheckConstantInput* input, CheckAction* action)
{
    const TypeId type = action->type != 0 ? action->type : action->operands[0].type;
    if (action->op == CHECK_OP_SIZEOF && check_action_runtime_size(input->reader->types, type))
    {
        const CheckValue operand = action->operands[0];
        const bool expression = action->type == 0;
        action->op = CHECK_OP_SIZEOF_VLA;
        action->type = type;
        action->operands[0] = (CheckValue)
        {
            .type = BUSTER_TYPE_ULONG,
            .category = CHECK_RVALUE,
            .flags = expression ? operand.flags & BUSTER_CHECK_SIDE_EFFECT : 0,
            .fault = expression ? operand.fault : CHECK_FAULT_NONE,
        };
    }
}

#include "c_check_builtin_constant.c"

static uint32_t check_constant_overload_head(const CheckConstantInput* input, uint32_t token)
{
    const CheckInputReader* reader = input->reader;
    uint32_t head = 0;
    for (uint32_t steps = 0; head == 0 && reader->output->overload_heads != 0 && token >= input->first && token < input->end &&
         steps < input->end - input->first; steps += 1)
    {
        head = reader->output->overload_heads[token];
        const uint8_t role = reader->parser->tree.roles[token];
        const uint8_t kind = reader->parser->tokens.kinds[token];
        if (head != 0 || (role != PARSE_ROLE_CLOSE_GROUP && !(role == PARSE_ROLE_PREFIX && (kind == PARSE_KIND_EXTENSION || kind == PARSE_KIND_AMP))))
        {
            break;
        }
        const uint32_t child = input->first_value[token - input->first];
        token = child != BUSTER_CHECK_NONE && input->next_value[child - input->first] == BUSTER_CHECK_NONE ? child : BUSTER_CHECK_NONE;
    }
    return head;
}

static void check_constant_action(CheckConstantInput* input, const CheckAction* action, uint32_t callee, CheckActionResult* checked)
{
    CheckInputReader* reader = input->reader;
    uint32_t head = action->op == CHECK_OP_CALL ? action->operands[0].overload_head : 0;
    head = action->op == CHECK_OP_CALL && head == 0 ? check_constant_overload_head(input, callee) : head;
    bool pending_arguments = false;
    for (uint32_t i = 0; action->op == CHECK_OP_CALL && i < action->argument_count; i += 1)
    {
        pending_arguments |= action->arguments[i].overload_head != 0;
    }
    const TypeId intrinsic = action->op == CHECK_OP_CALL ? check_input_intrinsic_type(reader, callee) : 0;
    if (intrinsic != 0)
    {
        const uint32_t close = reader->parser->tree.parents[callee];
        const CheckInputIntrinsicCall* source = reader->output->intrinsic_calls + reader->output->intrinsic_ids[close] - 1;
        const CheckInputIntrinsicDeclaration* declaration = reader->output->intrinsic_declarations + source->declaration;
        uint64_t ice_arguments = 0;
        for (uint32_t i = 0; i < action->argument_count && i < BUSTER_CHECK_BUILTIN_X86_ARGUMENTS; i += 1)
        {
            if (check_input_integer_ice(reader, action->arguments + i))
            {
                ice_arguments |= UINT64_C(1) << i;
            }
        }
        const CheckX86Call call =
        {
            .info = declaration->info,
            .required_ice_arguments = declaration->types.ice_parameters,
            .ice_arguments = ice_arguments,
            // Type-only queries still check hard ICE/SAE/scale constraints. Runtime range events belong to
            // the retained graph, including when this query finishes a declaration before graph construction.
            .constant_evaluated_context = true,
            .runtime_diagnostic_suppressed = true,
        };
        CheckAction typed = *action;
        typed.type = intrinsic;
        CheckBuiltinX86Result result;
        check_x86_action(reader->types, &call, &typed, &result);
        *checked = result.action;
    }
    else if (action->op == CHECK_OP_MEMBER && check_vector_type(reader->types, action->operands[0].type))
    {
        // Type queries run before the retained graph supplies its vector-member records. Give the ordinary
        // member action the same decoded selector here, with lane storage borrowed only for this action.
        const uint64_t mark = reader->scratch->length;
        const uint8_t* bytes = intern_bytes(reader->pp->table, action->member);
        const uint32_t length = intern_length(reader->pp->table, action->member);
        CheckVectorMember member;
        bool decoded = check_vector_member_decode(bytes, length, 0, 0, &member);
        if (!decoded)
        {
            uint8_t* lanes = arena_push_array(reader->scratch, length, sizeof(uint8_t), 1);
            decoded = lanes != 0 && check_vector_member_decode(bytes, length, lanes, length, &member);
        }
        if (decoded)
        {
            CheckAction typed = *action;
            typed.vector_member = &member;
            check_action_scalar(reader->types, &typed, checked);
        }
        else
        {
            reader->output->resource = true;
            *checked = (CheckActionResult){ .diagnostic = CHECK_DIAG_RESOURCE };
        }
        arena_pop_to(reader->scratch, mark);
    }
    else if (head != 0 || pending_arguments)
    {
        const CheckGraph* graph = &reader->output->graph;
        check_overload_call(reader->types, action, graph->overload_candidates, graph->overload_candidate_count,
                            head == 0 ? BUSTER_CHECK_NONE : head - 1, reader->scratch, reader->parser->options.scalar, checked, 0, 0);
    }
    else
    {
        CheckAction resolved = *action;
        bool valid = true;
        const uint32_t count = check_action_operand_count(action);
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            const bool defer = action->op == CHECK_OP_ADDRESS || action->op == CHECK_OP_SELECT || action->op == CHECK_OP_VALUE;
            if (!defer && action->operands[i].overload_head != 0)
            {
                const TypeId target = (i == 0 && (action->op == CHECK_OP_CAST || action->op == CHECK_OP_INITIALIZE || action->op == CHECK_OP_CONVERT)) ? action->type :
                                      (i == 1 && action->op == CHECK_OP_ASSIGN) ? action->operands[0].type : 0;
                valid = check_constant_resolve(input, action->operands + i, target, resolved.operands + i);
            }
        }
        if (valid)
        {
            check_action_scalar(reader->types, &resolved, checked);
        }
        else
        {
            *checked = (CheckActionResult){ .diagnostic = reader->output->resource ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE };
        }
    }
}

// These values exist only in constant_p argument closures. Ordinary actions and their diagnostics remain the
// authoritative source result. Speculative folding uses Clang's query policy, including saturated FP casts and
// integer wrapping, without publishing its non-ICE result as an ordinary expression. Operator warnings retain
// their actual source token: a saturated cast can make a subsequent integer operation overflow in Clang's query.
static bool check_constant_speculate(CheckConstantInput* input, const CheckAction* action, uint32_t callee, CheckValue* value)
{
    CheckAction speculative = *action;
    speculative.extensions |= BUSTER_CHECK_EXTENSION_GNU_CONSTANT;
    CheckActionResult checked;
    check_constant_action(input, &speculative, callee, &checked);
    input->reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
    if (checked.diagnostic == CHECK_DIAG_NONE)
    {
        check_input_constant_warning_add(input->reader->output, action->node, (uint8_t)checked.warnings);
    }
    if (checked.diagnostic == CHECK_DIAG_NONE && (action->op == CHECK_OP_CAST || action->op == CHECK_OP_CONVERT || action->op == CHECK_OP_INITIALIZE))
    {
        check_query_speculative_conversion(input->reader->types, action->operands, action->node, &checked.value);
    }
    *value = checked.value;
    return checked.diagnostic == CHECK_DIAG_NONE;
}

static bool check_constant_speculative_call(CheckConstantInput* input, uint32_t keyword, uint32_t closing, CBuiltinId builtin, TypeId type, CheckValue* arguments, uint32_t count, CheckValue* value)
{
    const uint8_t warnings = input->reader->output->constant_warnings[closing];
    const bool valid = check_constant_arguments(input, arguments, count) && check_constant_call(input, keyword, closing, builtin, type, arguments, count, value);
    check_input_constant_warning_set(input->reader->output, closing, warnings);
    return valid;
}

static CheckValue check_constant_void_statement(const CheckConstantInput* input, uint32_t first)
{
    uint32_t effects = 0;
    for (uint32_t child = first; child != BUSTER_CHECK_NONE; child = input->next_value[child - input->first])
    {
        effects |= input->values[child - input->first].flags & BUSTER_CHECK_SIDE_EFFECT;
    }
    return (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE, .flags = effects };
}

static bool check_constant_evaluate(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    uint32_t before = parser->tokens.count;
    uint32_t after = 0;
    for (uint32_t token = input->first; token < input->end; token += 1)
    {
        const uint32_t order = reader->postorder_positions[token];
        before = order < before ? order : before;
        after = order >= after ? order + 1 : after;
    }
    bool valid = true;
    for (uint32_t order = before; valid && order < after; order += 1)
    {
        const uint32_t token = parser->postorder[order];
        if (token < input->first || token >= input->end)
        {
            continue;
        }
        const uint32_t index = token - input->first;
        const uint8_t role = parser->tree.roles[token];
        input->warning_begin[index] = token;
        for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (child >= input->first && child < input->end && input->warning_begin[child - input->first] < input->warning_begin[index])
            {
                input->warning_begin[index] = input->warning_begin[child - input->first];
            }
        }
        const bool source_syntax = (input->statement_syntax[index] & 1) != 0;
        const uint32_t parent = parser->tree.parents[token];
        if ((input->statement_syntax[index] & 2) != 0 && role != PARSE_ROLE_TERMINATOR && parent >= input->first && parent < input->end)
        {
            input->statement_syntax[parent - input->first] |= 2;
        }
        if (input->attribute_ends[index] != 0)
        {
            valid = check_constant_attribute_evaluate(input, input->attribute_ends[index]);
            if (!valid && input->failed_token != 0)
            {
                *input->failed_token = token;
            }
        }
        if (input->syntax[index] && !source_syntax)
        {
            continue;
        }
        const uint8_t kind = parser->tokens.kinds[token];
        const TypeId intrinsic = role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER ? check_input_intrinsic_type(reader, token) : 0;
        CheckValue proof = { 0 };
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t last = BUSTER_CHECK_NONE;
        uint32_t count = 0;
        CheckAction action =
        {
            .op = CHECK_OP_COUNT,
            .node = token,
            .tag_completions = reader->tag_completions,
            .transparent_unions = reader->transparent_unions,
            .tag_completion_count = reader->types->tags.capacity,
            .source_position = reader->ends[token] + 1,
            .extensions = check_input_expression_extensions(parser),
        };
        for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (input->cached && reader->specifier_finishes[token] != 0 && child < token)
            {
                continue;
            }
            if (child >= input->first && child < input->end)
            {
                const uint32_t begin = input->first_value[child - input->first];
                if (begin != BUSTER_CHECK_NONE)
                {
                    if (last != BUSTER_CHECK_NONE)
                    {
                        input->next_value[last - input->first] = begin;
                    }
                    else
                    {
                        first = begin;
                    }
                    last = input->last_value[child - input->first];
                    for (uint32_t value = begin; value != BUSTER_CHECK_NONE; value = input->next_value[value - input->first])
                    {
                        if (count < 3)
                        {
                            action.operands[count] = input->values[value - input->first];
                        }
                        count += 1;
                    }
                }
            }
        }
        bool produced = false;
        bool list = false;
        bool void_statement = false;
        if (source_syntax)
        {
            list = true;
        }
        else if (role == PARSE_ROLE_OPERAND && ((kind == PARSE_KIND_NUMBER && !check_input_function_name_token(parser, token)) || kind == PARSE_KIND_CHARACTER))
        {
            CheckLiteral literal;
            check_input_literal(reader, token, &literal);
            valid = literal.valid;
            input->values[index] = check_literal_value(literal);
            produced = true;
        }
        else if (role == PARSE_ROLE_OPERAND && (kind == PARSE_KIND_STRING || check_input_function_name_token(parser, token)))
        {
            const CheckInitString* string = check_input_string(reader, token);
            valid = string != 0;
            if (valid)
            {
                input->values[index] = (CheckValue)
                {
                    .type = string->type,
                    .category = CHECK_LVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = string->object,
                };
                produced = true;
            }
        }
        else if (intrinsic != 0)
        {
            input->values[index] = (CheckValue){ .type = intrinsic, .category = CHECK_FUNCTION };
            produced = true;
        }
        else if (role == PARSE_ROLE_OPERAND && kind == PARSE_KIND_IDENTIFIER)
        {
            const uint32_t declaration = reader->output->declarations[token];
            const TypeId type = reader->declared_types[token];
            valid = type != 0 && declaration < parser->tokens.count;
            if (valid)
            {
                const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                input->values[index] = (CheckValue)
                {
                    .type = type,
                    .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                    .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                             (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                    .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                    .integer = enumeration ? reader->enum_values[declaration] : 0,
                    .object = reader->objects[declaration],
                    .overload_head = reader->output->overload_heads != 0 ? reader->output->overload_heads[token] : 0,
                };
                valid = check_constant_designator(input, &input->values[index]);
                input->abi_origins[index] = reader->output->explicit_abis != 0 ? reader->output->explicit_abis[declaration] : 0;
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_PREFIX)
        {
            switch (kind)
            {
                case PARSE_KIND_PLUS: action.op = CHECK_OP_POSITIVE; break;
                case PARSE_KIND_MINUS: action.op = CHECK_OP_NEGATIVE; break;
                case PARSE_KIND_EXCLAIM: action.op = CHECK_OP_NOT; break;
                case PARSE_KIND_TILDE: action.op = CHECK_OP_BIT_NOT; break;
                case PARSE_KIND_AMP: action.op = CHECK_OP_ADDRESS; break;
                case PARSE_KIND_STAR: action.op = CHECK_OP_DEREFERENCE; break;
                case PARSE_KIND_PLUSPLUS: action.op = CHECK_OP_PRE_INCREMENT; break;
                case PARSE_KIND_MINUSMINUS: action.op = CHECK_OP_PRE_DECREMENT; break;
                case PARSE_KIND_EXTENSION: action.op = CHECK_OP_SELECT; break;
                case PARSE_KIND_REAL_IMAG:
                    action.op = parser->tokens.names[token] == C_NAME_GNU_IMAG || parser->tokens.names[token] == C_NAME_GNU_IMAG2 ? CHECK_OP_IMAGINARY : CHECK_OP_REAL;
                    break;
                case PARSE_KIND_SIZEOF:
                    action.op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF;
                    action.type = input->queries[index];
                    break;
                default: break;
            }
            valid = action.op != CHECK_OP_COUNT && (count == 1 || (kind == PARSE_KIND_SIZEOF && action.type != 0));
        }
        else if (input->cached && role == PARSE_ROLE_ASSIGN && reader->initializers[token] != 0)
        {
            if (reader->initializers[token] == 1)
            {
                action.op = CHECK_OP_INITIALIZE;
                action.type = reader->query_types[token];
                valid = count == 1 && action.type != 0;
            }
            else
            {
                // Aggregate element constraints remain checked by the retained inner-region graph/initializer
                // consumer. This type-only query retains their effects, never an aggregate initializer value.
                input->values[index] = check_constant_void_statement(input, first);
                produced = true;
            }
        }
        else if ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA)
        {
            action.op = check_input_binary(kind);
            valid = count == 2 && action.op != CHECK_OP_COUNT;
        }
        else if (role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_SIZEOF)
        {
            const uint32_t type_first = input->type_begin[index];
            if (role == PARSE_ROLE_CLOSE_SIZEOF && type_first != BUSTER_CHECK_NONE)
            {
                const TypeId type = check_constant_type(input, type_first, token);
                const uint32_t parent = parser->tree.parents[token];
                valid = type != 0 && parent >= input->first && parent < input->end;
                if (valid)
                {
                    input->queries[parent - input->first] = type;
                }
            }
            else
            {
                valid = count == 1;
                input->values[index] = action.operands[0];
                input->abi_origins[index] = valid ? input->abi_origins[first - input->first] : 0;
                if (valid && input->proof_values != 0)
                {
                    proof = input->proof_values[first - input->first];
                }
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CAST)
        {
            const uint32_t type_first = input->type_begin[index];
            action.type = type_first != BUSTER_CHECK_NONE ? check_constant_type(input, type_first, token) : 0;
            input->abi_origins[index] = reader->constant_abi_origin;
            action.op = CHECK_OP_CAST;
            valid = count == 1 && action.type != 0;
        }
        else if (role == PARSE_ROLE_CLOSE_SPECIFIER)
        {
            const uint32_t keyword = reader->first_child[token];
            const uint32_t name = keyword < token ? parser->tokens.names[keyword] : 0;
            const uint32_t type_first = input->type_begin[index];
            if (type_first == BUSTER_CHECK_NONE && count == 1)
            {
                valid = check_constant_resolve(input, &action.operands[0], 0, &action.operands[0]);
            }
            TypeId type = type_first != BUSTER_CHECK_NONE ? check_constant_type(input, type_first, token) : count == 1 && valid ? action.operands[0].type : 0;
            input->abi_origins[index] = type_first != BUSTER_CHECK_NONE ? reader->constant_abi_origin : count == 1 && valid ? input->abi_origins[first - input->first] : 0;
            if (name == C_NAME_BITINT || name == C_NAME_EXTINT)
            {
                int64_t width = 0;
                valid = count == 1 && check_input_integer_constant(reader, &action.operands[0]) &&
                        check_action_i64(reader->types, &action.operands[0], &width) && width > 0 && width <= UINT32_MAX;
                // Unsigned one-bit _BitInt is valid; the specifier pass applies the actual signedness.
                type = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, true)) : 0;
                if (valid)
                {
                    check_input_constant_warning(reader, keyword + 2, token, &action.operands[0], true);
                }
            }
            else if (name == C_NAME_ATOMIC)
            {
                valid = type_first != BUSTER_CHECK_NONE;
                type = valid && type != 0 ? check_input_type_result(reader, type_atomic(reader->types, type)) : 0;
            }
            else if (name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2)
            {
                type = type_unqualified(type);
                type = type_kind(reader->types, type) == BUSTER_TYPE_KIND_ATOMIC ? type_element(reader->types, type) : type;
            }
            else
            {
                valid = name == C_NAME_TYPEOF || name == C_NAME_GNU_TYPEOF || name == C_NAME_GNU_TYPEOF2;
            }
            valid &= type != 0 && (type_first != BUSTER_CHECK_NONE || (count == 1 && (action.operands[0].flags & BUSTER_CHECK_BIT_FIELD) == 0));
            if (valid && type_first == BUSTER_CHECK_NONE && name != C_NAME_BITINT && name != C_NAME_EXTINT)
            {
                check_input_warning_suppress(reader, keyword + 2, token, check_constant_unevaluated_warnings());
            }
            input->queries[index] = type;
        }
        else if (role == PARSE_ROLE_TERMINATOR)
        {
            const bool declaration = (input->statement_syntax[index] & 2) != 0;
            const bool do_statement = input->cached && first != BUSTER_CHECK_NONE && parser->tree.roles[first] == PARSE_ROLE_DO;
            valid = declaration || do_statement || count <= 1;
            if (count == 1 && !declaration && !do_statement)
            {
                action.op = CHECK_OP_STATEMENT_VALUE;
            }
            else
            {
                input->values[index] = check_constant_void_statement(input, first);
                produced = true;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_BLOCK)
        {
            const uint32_t opening = reader->first_child[token];
            const uint32_t parent = parser->tree.parents[token];
            const bool expression = opening > input->first && opening < token && parser->tree.roles[opening] == PARSE_ROLE_OPEN_BLOCK &&
                                    parser->tree.roles[opening - 1] == PARSE_ROLE_OPEN_GROUP &&
                                    parser->tree.parents[opening - 1] == parent && parent == token + 1;
            valid = reader->function_body < parser->tokens.count &&
                    (expression || (input->cached && opening >= input->first && opening < token && parser->tree.roles[opening] == PARSE_ROLE_OPEN_BLOCK));
            CheckValue selected = { .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
            CheckValue selected_proof = selected;
            uint32_t effects = 0;
            for (uint32_t child = opening; valid && child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                if (child >= input->first && child < input->end && child != opening)
                {
                    selected = input->present[child - input->first] ? input->values[child - input->first] :
                               (CheckValue){ .type = BUSTER_TYPE_VOID, .category = CHECK_RVALUE };
                    selected_proof = input->proof_values != 0 && input->present[child - input->first] ? input->proof_values[child - input->first] : selected;
                    input->abi_origins[index] = expression && input->present[child - input->first] ? input->abi_origins[child - input->first] : 0;
                    effects |= selected.flags & BUSTER_CHECK_SIDE_EFFECT;
                }
            }
            selected.flags |= effects;
            selected.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_GNU_ICE_FORM);
            input->values[index] = expression ? selected : check_constant_void_statement(input, first);
            selected_proof.flags |= effects;
            selected_proof.flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_GNU_ICE_FORM);
            proof = expression ? selected_proof : input->values[index];
            produced = true;
        }
        else if (input->cached && role == PARSE_ROLE_CLOSE_CONDITION)
        {
            // The retained source graph checks the entire control statement. Here its condition still receives
            // the ordinary scalar constraint, and its already checked child expressions contribute no result.
            action.op = CHECK_OP_CONDITION;
            valid = count != 0;
            void_statement = true;
        }
        else if (input->cached && (role == PARSE_ROLE_CLOSE_FOR || role == PARSE_ROLE_DO || role == PARSE_ROLE_ELSE ||
                                  role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_RETURN))
        {
            input->values[index] = check_constant_void_statement(input, first);
            produced = true;
        }
        else if (role == PARSE_ROLE_CLOSE_TERNARY)
        {
            valid = count == 3 || count == 2;
            if (count == 2)
            {
                action.operands[2] = action.operands[1];
                action.operands[1] = action.operands[0];
            }
            action.op = CHECK_OP_CONDITIONAL;
        }
        else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            valid = count == 2;
            action.op = CHECK_OP_SUBSCRIPT;
        }
        else if (role == PARSE_ROLE_MEMBER_OPERATOR)
        {
            valid = count == 1 && token + 1 < input->end && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
            action.op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
            action.member = valid ? parser->tokens.names[token + 1] : 0;
        }
        else if (role == PARSE_ROLE_POSTFIX)
        {
            valid = count == 1 && (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS);
            action.op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT;
        }
        else if (role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            if (reader->builtins[token] == CBUILTIN_CONVERTVECTOR || reader->builtins[token] == CBUILTIN_BIT_CAST)
            {
                const uint16_t builtin = reader->builtins[token];
                const uint32_t type_first = input->type_begin[index];
                const uint32_t type_end = builtin == CBUILTIN_BIT_CAST ? check_constant_punctuation(parser, type_first, token, PARSE_KIND_COMMA) : token;
                const TypeId type = type_first != BUSTER_CHECK_NONE ? check_constant_type(input, type_first, type_end) : 0;
                valid = count == 1 && type != 0 && check_constant_arguments(input, action.operands, count) && check_constant_call(input, reader->first_child[token], token, (CBuiltinId)builtin,
                                                                     type, action.operands, count, &input->values[index]);
                if (valid && input->proof_values != 0 && input->proof_active[index])
                {
                    CheckValue argument = input->proof_values[first - input->first];
                    valid = check_constant_speculative_call(input, reader->first_child[token], token, (CBuiltinId)builtin, type, &argument, 1, &proof);
                }
            }
            else
            {
                valid = check_constant_builtin(input, token, &input->values[index], &proof);
            }
            produced = true;
        }
        else if (role == PARSE_ROLE_SEPARATOR)
        {
            uint32_t parent = parser->tree.parents[token];
            while (parent >= input->first && parent < input->end && parser->tree.roles[parent] == PARSE_ROLE_SEPARATOR)
            {
                parent = parser->tree.parents[parent];
            }
            list = parent >= input->first && parent < input->end &&
                   (parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[parent] == PARSE_ROLE_CLOSE_FUNCTION ||
                    parser->tree.roles[parent] == PARSE_ROLE_CLOSE_KEYWORD || (input->cached && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_INITIALIZER));
            if (!list)
            {
                action.op = CHECK_OP_COMMA;
                valid = count == 2;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_CLOSE_FUNCTION)
        {
            const uint16_t builtin = reader->builtins[token];
            if (builtin != CBUILTIN_NONE && (builtin < CBUILTIN_COUNT || check_input_complex_builtin((CBuiltinId)builtin) || check_graph_builtin_op((CBuiltinId)builtin) != CHECK_OP_COUNT))
            {
                const uint64_t mark = reader->scratch->length;
                CheckValue* arguments = count != 0 ? arena_push_array(reader->scratch, count, sizeof(CheckValue), _Alignof(CheckValue)) : 0;
                valid = count == 0 || arguments != 0;
                reader->output->resource |= !valid;
                uint32_t argument = 0;
                for (uint32_t child = first; valid && child != BUSTER_CHECK_NONE && argument < count; child = input->next_value[child - input->first])
                {
                    arguments[argument++] = builtin == CBUILTIN_CONSTANT_P && input->proof_values != 0 ? input->proof_values[child - input->first] : input->values[child - input->first];
                }
                valid = valid && argument == count && check_constant_arguments(input, arguments, count) && check_constant_call(input, reader->first_child[token], token, (CBuiltinId)builtin,
                                                                          0, arguments, count, &input->values[index]);
                if (valid && input->proof_values != 0 && input->proof_active[index])
                {
                    argument = 0;
                    for (uint32_t child = first; child != BUSTER_CHECK_NONE && argument < count; child = input->next_value[child - input->first])
                    {
                        arguments[argument++] = input->proof_values[child - input->first];
                    }
                    valid = check_constant_speculative_call(input, reader->first_child[token], token, (CBuiltinId)builtin, 0, arguments, count, &proof);
                }
                arena_pop_to(reader->scratch, mark);
                produced = true;
            }
            else
            {
                valid = count != 0;
                action.op = CHECK_OP_CALL;
            }
        }
        else if (input->cached && role == PARSE_ROLE_CLOSE_INITIALIZER)
        {
            list = true;
        }
        else
        {
            valid = role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF ||
                    role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_KEYWORD ||
                    role == PARSE_ROLE_OPEN_SUBSCRIPT || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_LEAF_RIGHT ||
                    role == PARSE_ROLE_STRING_NEXT || role == PARSE_ROLE_OPEN_BLOCK ||
                    (input->cached && (role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_OPEN_CONDITION || role == PARSE_ROLE_OPEN_FOR));
        }
        if (valid && action.op != CHECK_OP_COUNT)
        {
            if (action.op == CHECK_OP_ALIGNOF && action.type == 0 && count == 1)
            {
                const TypeId aligned = check_constant_object_alignment(reader, token, action.operands[0].type);
                action.type = aligned != action.operands[0].type ? aligned : 0;
                valid = aligned != 0;
            }
            const uint64_t action_mark = reader->scratch->length;
            if (action.op == CHECK_OP_CALL)
            {
                CheckValue* arguments = arena_push_array(reader->scratch, count, sizeof(CheckValue), 16);
                CheckConversion* conversions = arena_push_array(reader->scratch, count, sizeof(CheckConversion), 4);
                valid = arguments != 0 && conversions != 0;
                reader->output->resource |= !valid;
                uint32_t argument = 0;
                for (uint32_t value = valid ? input->next_value[first - input->first] : BUSTER_CHECK_NONE; value != BUSTER_CHECK_NONE; value = input->next_value[value - input->first])
                {
                    arguments[argument++] = input->values[value - input->first];
                }
                action.arguments = arguments;
                action.argument_count = argument;
                action.argument_conversions = conversions;
            }
            if (valid)
            {
                CheckActionResult checked;
                check_constant_warning_operands(input, &action, first, count);
                check_constant_runtime_query(input, &action);
                check_constant_action(input, &action, first, &checked);
                check_input_constant_warning_add(reader->output, action.node, (uint8_t)checked.warnings);
                reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
                valid = checked.diagnostic == CHECK_DIAG_NONE;
                input->values[index] = checked.value;
                if (first != BUSTER_CHECK_NONE && (action.op == CHECK_OP_ADDRESS || action.op == CHECK_OP_DEREFERENCE || action.op == CHECK_OP_SELECT ||
                    action.op == CHECK_OP_SUBSCRIPT || action.op == CHECK_OP_ASSIGN || action.op == CHECK_OP_STATEMENT_VALUE))
                {
                    input->abi_origins[index] = input->abi_origins[first - input->first];
                }
                if (valid && input->proof_values != 0 && input->proof_active[index])
                {
                    CheckAction speculative = action;
                    uint32_t operand = 0;
                    for (uint32_t child = first; child != BUSTER_CHECK_NONE && operand < 3; child = input->next_value[child - input->first])
                    {
                        speculative.operands[operand++] = input->proof_values[child - input->first];
                    }
                    if (action.op == CHECK_OP_CONDITIONAL && count == 2)
                    {
                        speculative.operands[2] = speculative.operands[1];
                        speculative.operands[1] = speculative.operands[0];
                    }
                    valid = check_constant_speculate(input, &speculative, first, &proof);
                }
                if (void_statement)
                {
                    input->values[index] = check_constant_void_statement(input, first);
                    input->values[index].flags |= checked.value.flags & BUSTER_CHECK_SIDE_EFFECT;
                }
                produced = true;
            }
            arena_pop_to(reader->scratch, action_mark);
        }
        if (valid && produced)
        {
            if (input->proof_values != 0)
            {
                input->proof_values[index] = proof.type != 0 ? proof : input->values[index];
            }
            input->present[index] = true;
            input->first_value[index] = token;
            input->last_value[index] = token;
            input->next_value[index] = BUSTER_CHECK_NONE;
        }
        else if (valid && list)
        {
            input->first_value[index] = first;
            input->last_value[index] = last;
        }
        if (!valid && input->failed_token != 0)
        {
            *input->failed_token = token;
        }
    }
    return valid;
}

typedef struct CheckConstantOperator CheckConstantOperator;
struct CheckConstantOperator
{
    CheckOp op;
    TypeId type;
    uint32_t token;
    uint32_t arity;
    uint8_t precedence;
    uint8_t marker;
    uint16_t builtin;
    uint8_t abi_origin;
};

typedef struct CheckConstantRange CheckConstantRange;
struct CheckConstantRange
{
    uint32_t first;
    uint32_t end;
};

static uint8_t check_constant_precedence(CheckOp op)
{
    uint8_t precedence = 0;
    switch (op)
    {
        case CHECK_OP_MULTIPLY: case CHECK_OP_DIVIDE: case CHECK_OP_REMAINDER: precedence = 13; break;
        case CHECK_OP_ADD: case CHECK_OP_SUBTRACT: precedence = 12; break;
        case CHECK_OP_SHIFT_LEFT: case CHECK_OP_SHIFT_RIGHT: precedence = 11; break;
        case CHECK_OP_LESS: case CHECK_OP_LESS_EQUAL: case CHECK_OP_GREATER: case CHECK_OP_GREATER_EQUAL: precedence = 10; break;
        case CHECK_OP_EQUAL: case CHECK_OP_NOT_EQUAL: precedence = 9; break;
        case CHECK_OP_BIT_AND: precedence = 8; break;
        case CHECK_OP_BIT_XOR: precedence = 7; break;
        case CHECK_OP_BIT_OR: precedence = 6; break;
        case CHECK_OP_LOGICAL_AND: precedence = 5; break;
        case CHECK_OP_LOGICAL_OR: precedence = 4; break;
        case CHECK_OP_CONDITIONAL: precedence = 3; break;
        case CHECK_OP_ASSIGN: case CHECK_OP_ADD_ASSIGN: case CHECK_OP_SUBTRACT_ASSIGN: case CHECK_OP_MULTIPLY_ASSIGN:
        case CHECK_OP_DIVIDE_ASSIGN: case CHECK_OP_REMAINDER_ASSIGN: case CHECK_OP_SHIFT_LEFT_ASSIGN: case CHECK_OP_SHIFT_RIGHT_ASSIGN:
        case CHECK_OP_BIT_AND_ASSIGN: case CHECK_OP_BIT_OR_ASSIGN: case CHECK_OP_BIT_XOR_ASSIGN: precedence = 2; break;
        case CHECK_OP_COMMA: precedence = 1; break;
        default: break;
    }
    return precedence;
}

static bool check_constant_raw_reduce(CheckConstantInput* input, CheckConstantOperator* operators, uint32_t* operator_count, CheckValue* values, CheckValue* proofs, CheckConstantRange* ranges, uint32_t* value_count)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = *operator_count != 0;
    if (valid)
    {
        const CheckConstantOperator operation = operators[--*operator_count];
        valid = operation.marker == 0 && operation.arity <= *value_count;
        if (valid)
        {
            CheckAction action =
            {
                .op = operation.op,
                .type = operation.type,
                .node = operation.token,
                .tag_completions = reader->tag_completions,
                .transparent_unions = reader->transparent_unions,
                .tag_completion_count = reader->types->tags.capacity,
                .source_position = input->end,
                .extensions = check_input_expression_extensions(reader->parser),
            };
            *value_count -= operation.arity;
            CheckConstantRange range = { .first = operation.token, .end = operation.token + 1 };
            for (uint32_t i = 0; i < operation.arity && i < 3; i += 1)
            {
                action.operands[i] = values[*value_count + i];
            }
            CheckAction speculative = action;
            for (uint32_t i = 0; proofs != 0 && i < operation.arity && i < 3; i += 1)
            {
                speculative.operands[i] = proofs[*value_count + i];
            }
            CheckValue proof = { 0 };
            uint8_t abi_origin = operation.abi_origin;
            if (operation.arity != 0 && (action.op == CHECK_OP_ADDRESS || action.op == CHECK_OP_DEREFERENCE || action.op == CHECK_OP_SELECT ||
                action.op == CHECK_OP_SUBSCRIPT || action.op == CHECK_OP_ASSIGN))
            {
                abi_origin = input->abi_origins[ranges[*value_count].first - input->first];
            }
            for (uint32_t i = 0; i < operation.arity; i += 1)
            {
                const CheckConstantRange child = ranges[*value_count + i];
                range.first = child.first < range.first ? child.first : range.first;
                range.end = child.end > range.end ? child.end : range.end;
                const bool repeated_condition = action.op == CHECK_OP_CONDITIONAL && i == 1 &&
                                                child.first == ranges[*value_count].first && child.end == ranges[*value_count].end;
                if (!repeated_condition && check_constant_warning_unevaluated(reader->types, &action, i, operation.arity))
                {
                    check_input_constant_warning_clear(reader, child.first, child.end, check_constant_unevaluated_warnings());
                }
            }
            if (action.op == CHECK_OP_ALIGNOF && action.type == 0 && operation.arity == 1)
            {
                const TypeId aligned = check_constant_object_alignment(reader, operation.token, action.operands[0].type);
                action.type = aligned != action.operands[0].type ? aligned : 0;
                valid = aligned != 0;
            }
            CheckActionResult checked;
            if (operation.builtin != CBUILTIN_NONE)
            {
                const uint32_t close = reader->parser->tree.parents[operation.token + 1];
                checked = (CheckActionResult){ 0 };
                CheckValue* arguments = operation.builtin == CBUILTIN_CONSTANT_P && proofs != 0 ? proofs + *value_count : values + *value_count;
                valid &= check_constant_arguments(input, arguments, operation.arity) && check_constant_call(input, operation.token, close, (CBuiltinId)operation.builtin,
                                              operation.type, arguments, operation.arity, &checked.value);
                if (valid && proofs != 0 && input->proof_active[operation.token - input->first])
                {
                    valid = check_constant_speculative_call(input, operation.token, close, (CBuiltinId)operation.builtin,
                        operation.type, proofs + *value_count, operation.arity, &proof);
                }
            }
            else
            {
                check_constant_runtime_query(input, &action);
                const uint64_t mark = reader->scratch->length;
                uint32_t callee = BUSTER_CHECK_NONE;
                if (action.op == CHECK_OP_CALL)
                {
                    action.arguments = values + *value_count + 1;
                    action.argument_count = operation.arity - 1;
                    action.argument_conversions = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConversion, operation.arity);
                    valid &= action.argument_conversions != 0;
                    reader->output->resource |= !valid;
                    CheckConstantRange name = ranges[*value_count];
                    while (name.first < name.end && parser->tokens.kinds[name.first] == PARSE_KIND_L_PAREN && parser->tree.parents[name.first] == name.end - 1)
                    {
                        name.first += 1;
                        name.end -= 1;
                    }
                    callee = name.first + 1 == name.end ? name.first : BUSTER_CHECK_NONE;
                }
                checked = (CheckActionResult){ 0 };
                if (valid)
                {
                    check_constant_action(input, &action, callee, &checked);
                    if (checked.diagnostic == CHECK_DIAG_NONE && proofs != 0 && input->proof_active[operation.token - input->first])
                    {
                        speculative.arguments = action.arguments;
                        speculative.argument_count = action.argument_count;
                        speculative.argument_conversions = action.argument_conversions;
                        check_constant_runtime_query(input, &speculative);
                        valid = check_constant_speculate(input, &speculative, callee, &proof);
                    }
                }
                arena_pop_to(reader->scratch, mark);
            }
            check_input_constant_warning_add(reader->output, action.node, (uint8_t)checked.warnings);
            reader->output->resource |= checked.diagnostic == CHECK_DIAG_RESOURCE;
            valid &= checked.diagnostic == CHECK_DIAG_NONE;
            ranges[*value_count] = range;
            input->abi_origins[range.first - input->first] = abi_origin;
            values[*value_count] = checked.value;
            if (proofs != 0)
            {
                proofs[*value_count] = proof.type != 0 ? proof : checked.value;
            }
            *value_count += 1;
        }
    }
    return valid;
}

// C23 attribute payloads are deliberately unstructured parser tokens. A bounded operator/value stack supplies
// their expression order without reparsing the unit, mutating parser output, or making the constant/type reader
// recursive. Type-query results have already been prepared inside out by check_constant_raw_prepare.
static bool check_constant_raw_expression(CheckConstantInput* input, uint32_t first, uint32_t end)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint32_t capacity = end - first + 1;
    CheckConstantOperator* operators = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantOperator), 8);
    CheckValue* values = arena_push_array(reader->scratch, capacity, sizeof(CheckValue), 16);
    CheckValue* proofs = input->proof_values != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, capacity) : 0;
    CheckConstantRange* ranges = arena_push_array(reader->scratch, capacity, sizeof(CheckConstantRange), 4);
    reader->output->resource |= operators == 0 || values == 0 || ranges == 0 || (input->proof_values != 0 && proofs == 0);
    bool valid = first < end && operators != 0 && values != 0 && ranges != 0 && (input->proof_values == 0 || proofs != 0);
    bool operand = true;
    uint32_t operator_count = 0;
    uint32_t value_count = 0;
    uint32_t token = first;
    while (valid && token < end)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        bool argument_separator = false;
        if (kind == PARSE_KIND_COMMA)
        {
            for (uint32_t pending = operator_count; pending != 0;)
            {
                const uint8_t marker = operators[--pending].marker;
                if (marker != 0)
                {
                    argument_separator = marker == 3;
                    break;
                }
            }
        }
        if (operand && ((kind == PARSE_KIND_NUMBER && !check_input_function_name_token(parser, token)) || kind == PARSE_KIND_CHARACTER))
        {
            CheckLiteral literal;
            check_input_literal(reader, token, &literal);
            valid = literal.valid;
            ranges[value_count] = (CheckConstantRange){ .first = token, .end = token + 1 };
            values[value_count++] = check_literal_value(literal);
            if (proofs != 0)
            {
                proofs[value_count - 1] = values[value_count - 1];
            }
            operand = false;
            token += 1;
        }
        else if (operand && (kind == PARSE_KIND_IDENTIFIER || kind == PARSE_KIND_TYPE_ARGUMENT) && input->syntax[token - input->first])
        {
            const uint32_t open = token + 1;
            const uint32_t close = open < end ? parser->tree.parents[open] : end;
            const uint16_t builtin = close < end ? reader->builtins[close] : CBUILTIN_NONE;
            uint32_t arguments = 0;
            for (uint32_t first_argument = open + 1; first_argument < close;)
            {
                arguments += 1;
                first_argument = check_constant_punctuation(parser, first_argument, close, PARSE_KIND_COMMA) + 1;
            }
            valid = builtin != CBUILTIN_NONE && (builtin < CBUILTIN_COUNT || check_input_complex_builtin((CBuiltinId)builtin) || check_graph_builtin_op((CBuiltinId)builtin) != CHECK_OP_COUNT);
            const bool type_builtin = builtin == CBUILTIN_CONVERTVECTOR || builtin == CBUILTIN_BIT_CAST;
            const TypeId destination = type_builtin ? input->queries[close - input->first] : 0;
            if (type_builtin)
            {
                valid &= arguments == 2 && destination != 0;
                arguments = 1;
            }
            if (valid)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = CHECK_OP_BUILTIN_UNARY,
                    .type = destination,
                    .token = token,
                    .precedence = 15,
                    .arity = arguments,
                    .builtin = builtin,
                };
                operators[operator_count++] = (CheckConstantOperator){ .token = open, .marker = 3 };
            }
            token = builtin == CBUILTIN_BIT_CAST ? check_constant_punctuation(parser, open + 1, close, PARSE_KIND_COMMA) + 1 : open + 1;
        }
        else if (operand && kind == PARSE_KIND_IDENTIFIER && check_input_intrinsic_type(reader, token) != 0)
        {
            ranges[value_count] = (CheckConstantRange){ .first = token, .end = token + 1 };
            values[value_count++] = (CheckValue){ .type = check_input_intrinsic_type(reader, token), .category = CHECK_FUNCTION };
            if (proofs != 0)
            {
                proofs[value_count - 1] = values[value_count - 1];
            }
            operand = false;
            token += 1;
        }
        else if (operand && kind == PARSE_KIND_IDENTIFIER)
        {
            const uint32_t declaration = reader->output->declarations[token];
            const TypeId type = reader->declared_types[token];
            valid = type != 0 && declaration < parser->tokens.count;
            if (valid)
            {
                const bool enumeration = reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
                ranges[value_count] = (CheckConstantRange){ .first = token, .end = token + 1 };
                values[value_count++] = (CheckValue)
                {
                    .type = type,
                    .category = enumeration ? CHECK_RVALUE : type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE,
                    .flags = (enumeration ? BUSTER_CHECK_ICE : 0) | (reader->static_objects[declaration] && !reader->output->thread_local[declaration] ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                             (reader->registers[declaration] ? BUSTER_CHECK_REGISTER : 0),
                    .constant_kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS,
                    .integer = enumeration ? reader->enum_values[declaration] : 0,
                    .object = reader->objects[declaration],
                    .overload_head = reader->output->overload_heads != 0 ? reader->output->overload_heads[token] : 0,
                };
                valid = check_constant_designator(input, &values[value_count - 1]);
                input->abi_origins[token - input->first] = reader->output->explicit_abis != 0 ? reader->output->explicit_abis[declaration] : 0;
                if (proofs != 0)
                {
                    proofs[value_count - 1] = values[value_count - 1];
                }
            }
            operand = false;
            token += 1;
        }
        else if (operand && (kind == PARSE_KIND_STRING || check_input_function_name_token(parser, token)))
        {
            const CheckInitString* string = check_input_string(reader, token);
            valid = string != 0;
            if (valid)
            {
                ranges[value_count] = (CheckConstantRange){ .first = token, .end = token + 1 };
                values[value_count++] = (CheckValue)
                {
                    .type = string->type,
                    .category = CHECK_LVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = string->object,
                };
                if (proofs != 0)
                {
                    proofs[value_count - 1] = values[value_count - 1];
                }
            }
            do
            {
                token += 1;
            }
            while (token < end && parser->tokens.kinds[token] == PARSE_KIND_STRING);
            operand = false;
        }
        else if (!operand && kind == PARSE_KIND_L_PAREN && value_count != 0)
        {
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < end;
            uint32_t arguments = 0;
            for (uint32_t first_argument = token + 1; valid && first_argument < close;)
            {
                arguments += 1;
                first_argument = check_constant_punctuation(parser, first_argument, close, PARSE_KIND_COMMA) + 1;
            }
            if (valid)
            {
                operators[operator_count++] = (CheckConstantOperator){ .op = CHECK_OP_CALL, .token = token, .precedence = 15, .arity = arguments + 1 };
                operators[operator_count++] = (CheckConstantOperator){ .token = token, .marker = 3 };
                operand = true;
                token += 1;
            }
        }
        else if (operand && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_SIZEOF))
        {
            const bool query = kind == PARSE_KIND_SIZEOF;
            const uint32_t open = query ? token + 1 : token;
            const uint32_t close = open < end && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN ? parser->tree.parents[open] : BUSTER_CHECK_NONE;
            const TypeId type = close > open && close < end ? input->queries[close - input->first] : 0;
            if (type != 0)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = query ? (parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF) : CHECK_OP_CAST,
                    .type = type,
                    .token = token,
                    .precedence = 14,
                    .arity = query ? 0 : 1,
                    .abi_origin = query ? 0 : input->abi_origins[close - input->first],
                };
                if (query)
                {
                    valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
                    operand = false;
                }
                token = close + 1;
            }
            else if (query)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = parser->tokens.names[token] == C_NAME_SIZEOF ? CHECK_OP_SIZEOF : CHECK_OP_ALIGNOF,
                    .token = token,
                    .precedence = 14,
                    .arity = 1,
                };
                token += 1;
            }
            else
            {
                const uint8_t marker = operator_count != 0 && operators[operator_count - 1].builtin != CBUILTIN_NONE &&
                                       operators[operator_count - 1].token + 1 == token ? 3 : 1;
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .token = token,
                    .marker = marker,
                };
                token += 1;
            }
        }
        else if (kind == PARSE_KIND_R_PAREN)
        {
            valid = !operand || (operator_count >= 2 && operators[operator_count - 1].marker == 3 &&
                    operators[operator_count - 2].arity == (operators[operator_count - 2].op == CHECK_OP_CALL ? 1u : 0u));
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            const uint8_t marker = operator_count != 0 ? operators[operator_count - 1].marker : 0;
            valid &= marker == 1 || marker == 3;
            operator_count -= valid;
            if (valid && marker == 3)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            operand = false;
            token += 1;
        }
        else if (!operand && argument_separator)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            const bool destination = operator_count >= 2 && operators[operator_count - 2].builtin == CBUILTIN_CONVERTVECTOR;
            operand = !destination;
            token = destination ? parser->tree.parents[operators[operator_count - 1].token] : token + 1;
        }
        else if (!operand && kind == PARSE_KIND_QUESTION)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0 && operators[operator_count - 1].precedence > 3)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            operators[operator_count++] = (CheckConstantOperator)
            {
                .op = CHECK_OP_CONDITIONAL,
                .token = token,
                .precedence = 3,
                .arity = 3,
                .marker = 2,
            };
            operand = true;
            token += 1;
        }
        else if (kind == PARSE_KIND_COLON)
        {
            if (operand)
            {
                valid = value_count != 0 && operator_count != 0 && operators[operator_count - 1].marker == 2;
                if (valid)
                {
                    values[value_count] = values[value_count - 1];
                    if (proofs != 0)
                    {
                        proofs[value_count] = proofs[value_count - 1];
                    }
                    ranges[value_count] = ranges[value_count - 1];
                    value_count += 1;
                }
            }
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0)
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            valid &= operator_count != 0 && operators[operator_count - 1].marker == 2;
            if (valid)
            {
                operators[operator_count - 1].marker = 0;
            }
            operand = true;
            token += 1;
        }
        else
        {
            CheckOp op = CHECK_OP_COUNT;
            if (operand)
            {
                switch (kind)
                {
                    case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
                    case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
                    case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
                    case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
                    case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
                    case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
                    case PARSE_KIND_EXTENSION: op = CHECK_OP_SELECT; break;
                    case PARSE_KIND_REAL_IMAG:
                        op = parser->tokens.names[token] == C_NAME_GNU_IMAG || parser->tokens.names[token] == C_NAME_GNU_IMAG2 ? CHECK_OP_IMAGINARY : CHECK_OP_REAL;
                        break;
                    default: break;
                }
            }
            else
            {
                op = check_input_binary(kind);
            }
            const uint8_t precedence = operand ? 14 : check_constant_precedence(op);
            valid = op != CHECK_OP_COUNT && precedence != 0;
            const bool right = operand || precedence == 2;
            while (valid && operator_count != 0 && operators[operator_count - 1].marker == 0 &&
                   (operators[operator_count - 1].precedence > precedence || (!right && operators[operator_count - 1].precedence == precedence)))
            {
                valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
            }
            if (valid)
            {
                operators[operator_count++] = (CheckConstantOperator)
                {
                    .op = op,
                    .token = token,
                    .precedence = precedence,
                    .arity = operand ? 1 : 2,
                };
            }
            operand = true;
            token += 1;
        }
    }
    valid &= !operand;
    while (valid && operator_count != 0)
    {
        valid = check_constant_raw_reduce(input, operators, &operator_count, values, proofs, ranges, &value_count);
    }
    valid &= value_count == 1;
    if (valid)
    {
        input->values[first - input->first] = values[0];
        input->abi_origins[first - input->first] = input->abi_origins[ranges[0].first - input->first];
        if (input->proof_values != 0)
        {
            input->proof_values[first - input->first] = proofs[0];
        }
        input->present[first - input->first] = true;
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_constant_raw_type_start(CheckConstantInput* input, uint32_t token)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint8_t kind = parser->tokens.kinds[token];
    bool type = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATOMIC;
    if (kind == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]);
        const uint32_t declaration = reader->output->declarations[token];
        type = input->cached ? declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF :
               handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
    }
    return type;
}

// A raw `(` in a type name holds a parameter list when what it holds starts as a parameter declaration may, as
// check_constant_specifiers reads one: nothing, `...`, or specifiers after any attributes, `register` among them.
static bool check_constant_raw_parameters(CheckConstantInput* input, uint32_t open, uint32_t close)
{
    const Parser* parser = input->reader->parser;
    const uint32_t first = check_input_skip_attributes(parser, open + 1, close);
    const uint8_t kind = first < close ? parser->tokens.kinds[first] : PARSE_KIND_R_PAREN;
    return first >= close || kind == PARSE_KIND_ELLIPSIS || (kind == PARSE_KIND_STORAGE && parser->tokens.names[first] == C_NAME_REGISTER) ||
           check_constant_raw_type_start(input, first);
}

static bool check_constant_raw_bind(CheckConstantInput* input, bool type_name)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    if (type_name)
    {
        check_constant_mark_type(input, input->first, input->end);
    }
    for (uint32_t token = input->bind_cursor; valid && token < input->end; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t index = token - input->first;
        valid = check_constant_tag_finish(input, token);
        if (input->pending_tag != BUSTER_CHECK_NONE)
        {
            input->bind_cursor = token;
            break;
        }
        valid = valid && check_constant_prototype_close(input, token);
        if (!valid || input->attribute_syntax[index])
        {
            continue;
        }
        const uint32_t completed = check_constant_completed_tag(input, token);
        if (completed != token)
        {
            token = completed - 1;
            continue;
        }
        if (input->syntax[index] && check_input_attribute_starts(parser, token, input->end))
        {
            uint32_t after = token;
            valid = check_constant_attribute_prepare(input, token, &after);
            if (valid)
            {
                input->attribute_starts[index]->publish = false;
            }
        }
        if (kind == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < input->end;
            // A list in a type name, as check_constant_type reads a raw `(`.
            const bool prototype = valid && input->syntax[index] && token > input->first && parser->tokens.kinds[token - 1] != PARSE_KIND_TYPEOF &&
                                   parser->tokens.kinds[token - 1] != PARSE_KIND_ATOMIC && check_constant_raw_parameters(input, token, close);
            if (prototype)
            {
                check_constant_prototype_open(input, token);
            }
            if (valid && token > input->first && (parser->tokens.names[token - 1] == C_NAME_BUILTIN_CONVERTVECTOR || parser->tokens.names[token - 1] == C_NAME_BUILTIN_BIT_CAST))
            {
                valid = check_constant_type_builtin_prepare(input, token, parser->tokens.names[token - 1] == C_NAME_BUILTIN_BIT_CAST ? CBUILTIN_BIT_CAST : CBUILTIN_CONVERTVECTOR);
            }
            else if (valid && token + 1 < close && check_constant_raw_type_start(input, token + 1))
            {
                input->type_begin[close - input->first] = token + 1;
                check_constant_mark_type(input, token + 1, close);
            }
            else if (valid && token > input->first && parser->tokens.kinds[token - 1] == PARSE_KIND_TYPEOF)
            {
                for (uint32_t item = token + 1; item < close; item += 1)
                {
                    input->syntax[item - input->first] = false;
                }
            }
        }
        if (valid && input->syntax[index] && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM))
        {
            valid = check_constant_tag_prepare(input, token);
        }
        else if (valid && kind == PARSE_KIND_IDENTIFIER && (token == input->first ||
                 (parser->tokens.kinds[token - 1] != PARSE_KIND_STRUCT && parser->tokens.kinds[token - 1] != PARSE_KIND_ENUM)))
        {
            const uint32_t name = parser->tokens.names[token];
            const uint32_t handle = input->cached ? 0 : c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name);
            const bool type = handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
            const uint16_t builtin = !input->syntax[index] ? check_constant_integer_builtin(input, token, handle) : CBUILTIN_NONE;
            const TypeId intrinsic = !input->syntax[index] && builtin == CBUILTIN_NONE ? check_constant_intrinsic_type(input, token, handle) : 0;
            if (builtin != CBUILTIN_NONE)
            {
                input->syntax[index] = true;
            }
            else
            {
                valid = intrinsic != 0 || (input->cached ? reader->declared_types[token] != 0 || !input->syntax[index] :
                        handle != 0 ? type == (input->syntax[index] != 0) : !input->syntax[index]);
            }
            if (valid && !input->cached && builtin == CBUILTIN_NONE && handle != 0)
            {
                const uint32_t declaration = reader->bindings.declaration[handle - 1];
                reader->output->declarations[token] = declaration;
                reader->declared_types[token] = reader->declared_types[declaration];
                if (reader->output->overload_heads != 0)
                {
                    reader->output->overload_heads[token] = reader->output->overload_heads[declaration];
                }
                valid = c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, token) != 0;
                reader->output->resource |= !valid;
            }
        }
    }
    if (valid && input->pending_tag == BUSTER_CHECK_NONE)
    {
        input->bind_cursor = input->end;
        valid = check_constant_tag_finish(input, input->end);
    }
    return valid;
}

static bool check_constant_raw_prepare(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool valid = true;
    for (uint32_t reverse = input->end; valid && reverse != input->first;)
    {
        const uint32_t token = --reverse;
        if (input->attribute_starts[token - input->first] != 0)
        {
            valid = check_constant_attribute_evaluate(input, input->attribute_starts[token - input->first]);
        }
        if (!valid || input->attribute_syntax[token - input->first])
        {
            continue;
        }
        if (parser->tokens.kinds[token] == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[token];
            const uint32_t first = input->type_begin[close - input->first];
            const uint32_t previous_kind = token > input->first ? parser->tokens.kinds[token - 1] : 0;
            const uint32_t name = token > input->first ? parser->tokens.names[token - 1] : 0;
            const bool specifier = previous_kind == PARSE_KIND_TYPEOF || previous_kind == PARSE_KIND_ATOMIC;
            TypeId type = 0;
            if (first != BUSTER_CHECK_NONE)
            {
                const uint32_t type_end = reader->builtins[close] == CBUILTIN_BIT_CAST ? check_constant_punctuation(parser, first, close, PARSE_KIND_COMMA) : close;
                for (uint32_t item = first; valid && item < type_end; item += 1)
                {
                    if (parser->tokens.kinds[item] == PARSE_KIND_L_SQUARE)
                    {
                        const uint32_t finish = parser->tree.parents[item];
                        valid = finish > item && finish < type_end;
                        if (valid && item + 1 != finish)
                        {
                            valid = check_constant_raw_expression(input, item + 1, finish);
                        }
                        item = finish;
                    }
                }
                type = valid ? check_constant_type(input, first, type_end) : 0;
                input->abi_origins[close - input->first] = reader->constant_abi_origin;
                valid &= type != 0;
            }
            else if (specifier)
            {
                valid = check_constant_raw_expression(input, token + 1, close);
                CheckValue value = valid ? input->values[token + 1 - input->first] : (CheckValue){ 0 };
                valid = valid && check_constant_resolve(input, &value, 0, &value);
                type = valid ? value.type : 0;
                input->abi_origins[close - input->first] = input->abi_origins[token + 1 - input->first];
            }
            if (valid && specifier)
            {
                if (name == C_NAME_ATOMIC)
                {
                    type = check_input_type_result(reader, type_atomic(reader->types, type));
                }
                else if (name == C_NAME_BITINT || name == C_NAME_EXTINT)
                {
                    const CheckValue* value = &input->values[token + 1 - input->first];
                    int64_t width = 0;
                    valid = first == BUSTER_CHECK_NONE && check_input_integer_constant(reader, value) &&
                            check_action_i64(reader->types, value, &width) && width > 0 && width <= UINT32_MAX;
                    type = valid ? check_input_type_result(reader, type_bit_int(reader->types, (uint32_t)width, true)) : 0;
                    if (valid)
                    {
                        check_input_constant_warning(reader, token + 1, close, value, true);
                    }
                }
                else if (name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2)
                {
                    type = type_unqualified(type);
                    type = type_kind(reader->types, type) == BUSTER_TYPE_KIND_ATOMIC ? type_element(reader->types, type) : type;
                }
                valid &= type != 0;
                if (valid && first == BUSTER_CHECK_NONE && name != C_NAME_BITINT && name != C_NAME_EXTINT)
                {
                    check_input_warning_suppress(reader, token + 1, close, check_constant_unevaluated_warnings());
                }
            }
            input->queries[close - input->first] = type;
        }
    }
    return valid;
}

static void check_constant_values_reset(CheckConstantInput* input)
{
    const uint32_t count = input->end - input->first;
    memset(input->present, 0, count);
    memset(input->first_value, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(input->last_value, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(input->next_value, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(input->warning_begin, 0xff, (uint64_t)count * sizeof(uint32_t));
    memset(input->queries, 0, (uint64_t)count * sizeof(TypeId));
    memset(input->abi_origins, 0, count);
    if (input->proof_values != 0)
    {
        memset(input->proof_values, 0, (uint64_t)count * sizeof(CheckValue));
    }
}

static bool check_constant_proof_keyword(const CheckConstantInput* input, uint32_t token)
{
    const CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    bool query = token + 1 < input->end && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN;
    if (query)
    {
        const uint32_t identifier = parser->tokens.names[token];
        const InternSpan span = reader->pp->table->spans[identifier];
        const char* spelling = (const char*)reader->pp->table->records + span.offset;
        query = cbuiltin_resolve(spelling, span.length) == CBUILTIN_CONSTANT_P;
    }
    return query;
}

static bool check_constant_proof_initialize(CheckConstantInput* input)
{
    CheckInputReader* reader = input->reader;
    const Parser* parser = reader->parser;
    const uint32_t count = input->end - input->first;
    bool needed = false;
    for (uint32_t token = input->first; !needed && token + 1 < input->end; token += 1)
    {
        needed = check_constant_proof_keyword(input, token);
    }
    bool valid = true;
    if (needed)
    {
        input->proof_values = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, count);
        input->proof_active = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        const uint64_t mark = reader->scratch->length;
        uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, count);
        valid = input->proof_values != 0 && input->proof_active != 0 && ends != 0;
        reader->output->resource |= !valid;
        uint32_t depth = 0;
        for (uint32_t token = input->first; valid && token < input->end; token += 1)
        {
            while (depth != 0 && token >= ends[depth - 1])
            {
                depth -= 1;
            }
            input->proof_active[token - input->first] = depth != 0;
            if (check_constant_proof_keyword(input, token))
            {
                const uint32_t close = parser->tree.parents[token + 1];
                if (close > token + 1 && close < input->end)
                {
                    ends[depth++] = close;
                }
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

static bool check_constant_input_initialize(CheckConstantInput* input, CheckInputReader* reader, uint32_t first, uint32_t end, bool cached, uint32_t* failed_token)
{
    const uint32_t count = end >= first ? end - first : 0;
    *input = (CheckConstantInput)
    {
        .reader = reader,
        .mark = reader->scratch->length,
        .first = first,
        .end = end,
        .bind_cursor = first,
        .pending_tag = BUSTER_CHECK_NONE,
        .failure = BUSTER_CHECK_NONE,
        .prototype_depth = reader->prototype_depth,
        .cached = cached,
        .failed_token = failed_token,
    };
    if (failed_token != 0)
    {
        *failed_token = BUSTER_CHECK_NONE;
    }
    bool valid = count != 0 && end <= reader->parser->tokens.count;
#define BUSTER_CHECK_CONSTANT_ARRAY(field, element) input->field = valid ? arena_push_array(reader->scratch, count, sizeof(element), 16) : 0; valid &= input->field != 0
    BUSTER_CHECK_CONSTANT_ARRAY(values, CheckValue);
    BUSTER_CHECK_CONSTANT_ARRAY(present, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(syntax, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(statement_syntax, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(first_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(last_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(next_value, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(type_begin, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(warning_begin, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(queries, TypeId);
    BUSTER_CHECK_CONSTANT_ARRAY(abi_origins, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_syntax, uint8_t);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_starts, CheckConstantAttributes*);
    BUSTER_CHECK_CONSTANT_ARRAY(attribute_ends, CheckConstantAttributes*);
    BUSTER_CHECK_CONSTANT_ARRAY(tag_next, uint32_t);
    BUSTER_CHECK_CONSTANT_ARRAY(prototype_marks, uint32_t);
    input->tag_finishes = valid ? arena_push_array(reader->scratch, (uint64_t)count + 1, sizeof(uint32_t), 4) : 0;
    valid &= input->tag_finishes != 0;
#undef BUSTER_CHECK_CONSTANT_ARRAY
    valid = valid && check_constant_proof_initialize(input);
    reader->output->resource |= count != 0 && end <= reader->parser->tokens.count && !valid;
    if (valid)
    {
        check_constant_values_reset(input);
        memset(input->syntax, 0, count);
        memset(input->statement_syntax, 0, count);
        memset(input->type_begin, 0xff, (uint64_t)count * sizeof(uint32_t));
        memset(input->attribute_syntax, 0, count);
        memset(input->attribute_starts, 0, (uint64_t)count * sizeof(CheckConstantAttributes*));
        memset(input->attribute_ends, 0, (uint64_t)count * sizeof(CheckConstantAttributes*));
        memset(input->tag_finishes, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(input->prototype_marks, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t token = first; token < end; token += 1)
        {
            const uint8_t kind = reader->parser->tokens.kinds[token];
            input->raw |= reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND && kind != PARSE_KIND_NUMBER && kind != PARSE_KIND_CHARACTER &&
                         kind != PARSE_KIND_STRING && kind != PARSE_KIND_IDENTIFIER && kind != PARSE_KIND_DEFAULT;
        }
    }
    return valid;
}

// The source tag work stack owns these jobs. Binding yields after the tag's header, before any body token;
// completing that body and resuming this same input preserves all earlier expression identities and events.
static CheckConstantInput* check_input_constant_job(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const uint64_t mark = reader->scratch->length;
    CheckConstantInput* input = arena_push(reader->scratch, sizeof(CheckConstantInput), _Alignof(CheckConstantInput));
    reader->output->resource |= input == 0;
    const bool valid = input != 0 && check_constant_input_initialize(input, reader, first, end, false, &input->failure);
    if (valid)
    {
        input->mark = mark;
        input->deferred_tags = true;
        if (!input->raw)
        {
            check_constant_source_regions(input);
        }
    }
    else
    {
        arena_pop_to(reader->scratch, mark);
        input = 0;
    }
    return input;
}

static bool check_constant_job_header(CheckConstantInput* input, uint32_t token)
{
    const uint32_t first = token + 1;
    const uint32_t end = input->bind_cursor;
    bool valid = first <= end && end < input->end;
    if (valid && first != end)
    {
        CheckConstantInput header = *input;
        const uint32_t offset = first - input->first;
        header.first = first;
        header.end = end;
#define BUSTER_CHECK_CONSTANT_SLICE(field) header.field += offset
        BUSTER_CHECK_CONSTANT_SLICE(values);
        BUSTER_CHECK_CONSTANT_SLICE(present);
        BUSTER_CHECK_CONSTANT_SLICE(syntax);
        BUSTER_CHECK_CONSTANT_SLICE(statement_syntax);
        BUSTER_CHECK_CONSTANT_SLICE(first_value);
        BUSTER_CHECK_CONSTANT_SLICE(last_value);
        BUSTER_CHECK_CONSTANT_SLICE(next_value);
        BUSTER_CHECK_CONSTANT_SLICE(type_begin);
        BUSTER_CHECK_CONSTANT_SLICE(warning_begin);
        BUSTER_CHECK_CONSTANT_SLICE(queries);
        BUSTER_CHECK_CONSTANT_SLICE(abi_origins);
        BUSTER_CHECK_CONSTANT_SLICE(attribute_syntax);
        BUSTER_CHECK_CONSTANT_SLICE(attribute_starts);
        BUSTER_CHECK_CONSTANT_SLICE(attribute_ends);
        BUSTER_CHECK_CONSTANT_SLICE(tag_finishes);
        BUSTER_CHECK_CONSTANT_SLICE(tag_next);
        BUSTER_CHECK_CONSTANT_SLICE(prototype_marks);
#undef BUSTER_CHECK_CONSTANT_SLICE
        if (header.proof_values != 0)
        {
            header.proof_values += offset;
            header.proof_active += offset;
        }
        check_constant_values_reset(&header);
        valid = header.raw ? check_constant_raw_prepare(&header) : check_constant_evaluate(&header);
    }
    return valid;
}

static bool check_input_constant_job_step(CheckConstantInput* input, CheckValue* value, uint32_t* nested_tag)
{
    bool valid = true;
    *nested_tag = BUSTER_CHECK_NONE;
    if (input->pending_tag != BUSTER_CHECK_NONE)
    {
        const uint32_t after = check_constant_completed_tag(input, input->pending_tag);
        valid = after > input->pending_tag;
        input->bind_cursor = after;
        input->pending_tag = BUSTER_CHECK_NONE;
    }
    valid = valid && (input->raw ? check_constant_raw_bind(input, false) : check_constant_bind(input, false));
    if (valid && input->pending_tag != BUSTER_CHECK_NONE)
    {
        valid = check_constant_job_header(input, input->pending_tag);
        *nested_tag = valid ? input->pending_tag : BUSTER_CHECK_NONE;
    }
    else if (valid)
    {
        check_constant_values_reset(input);
        valid = input->raw ? check_constant_raw_prepare(input) && check_constant_raw_expression(input, input->first, input->end) :
                            check_constant_evaluate(input);
        valid = valid && check_constant_range_value(input, input->first, input->end, value) && check_constant_resolve(input, value, 0, value);
    }
    // A job that waits for a definition keeps the depth of the lists open around it, at which its caller reads the
    // definition; one that ended, or failed, gives its own back.
    if (*nested_tag == BUSTER_CHECK_NONE)
    {
        input->reader->prototype_depth = input->prototype_depth;
    }
    return valid;
}

static bool check_input_constant_read(CheckInputReader* reader, uint32_t first, uint32_t end, bool type_name, bool cached,
                                      CheckValue* value, TypeId* type, uint32_t* failed_token)
{
    const uint64_t mark = reader->scratch->length;
    CheckConstantInput input;
    reader->constant_abi_origin = 0;
    bool valid = check_constant_input_initialize(&input, reader, first, end, cached, failed_token);
    if (valid)
    {
        if (input.raw)
        {
            valid = check_constant_raw_bind(&input, type_name) && check_constant_raw_prepare(&input) &&
                    (type_name || check_constant_raw_expression(&input, first, end));
        }
        else
        {
            check_constant_source_regions(&input);
            valid = check_constant_bind(&input, type_name) && check_constant_evaluate(&input);
        }
    }
    if (valid && type_name)
    {
        *type = check_constant_type(&input, first, end);
        valid = *type != 0;
    }
    else if (valid)
    {
        valid = check_constant_range_value(&input, first, end, value) && check_constant_resolve(&input, value, 0, value);
        const uint32_t root = valid ? check_constant_range_root(&input, first, end) : BUSTER_CHECK_NONE;
        reader->constant_abi_origin = root != BUSTER_CHECK_NONE ? input.abi_origins[root - first] : 0;
    }
    reader->prototype_depth = input.prototype_depth;
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static bool check_input_evaluate(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value)
{
    TypeId unused = 0;
    return check_input_constant_read(reader, first, end, false, false, value, &unused, 0);
}

static __attribute__((unused)) bool check_input_evaluate_cached(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value)
{
    TypeId unused = 0;
    return check_input_constant_read(reader, first, end, false, true, value, &unused, 0);
}

static __attribute__((unused)) bool check_input_evaluate_cached_at(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value, uint32_t* failed_token)
{
    TypeId unused = 0;
    return check_input_constant_read(reader, first, end, false, true, value, &unused, failed_token);
}

static bool check_input_constant(CheckInputReader* reader, uint32_t first, uint32_t end, int64_t* number)
{
    CheckValue value = { 0 };
    const bool valid = check_input_evaluate(reader, first, end, &value) && check_input_integer_constant(reader, &value) &&
                       check_action_i64(reader->types, &value, number);
    if (valid)
    {
        check_input_constant_warning(reader, first, end, &value, true);
    }
    return valid;
}

static __attribute__((unused)) bool check_input_constant_ice(CheckInputReader* reader, uint32_t first, uint32_t end, int64_t* number)
{
    CheckValue value = { 0 };
    const bool valid = check_input_evaluate(reader, first, end, &value) && check_input_integer_attribute(reader, &value) &&
                       check_action_i64(reader->types, &value, number);
    if (valid)
    {
        check_input_constant_warning(reader, first, end, &value, true);
    }
    return valid;
}

static __attribute__((unused)) TypeId check_input_constant_type(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    CheckValue unused = { 0 };
    TypeId type = 0;
    const bool valid = check_input_constant_read(reader, first, end, true, false, &unused, &type, 0);
    return valid ? type : 0;
}
