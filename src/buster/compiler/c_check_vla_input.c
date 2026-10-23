// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Variable array dimensions retain their source expression and one stable extent identity. A declaration
// captures each evaluated bound once; later sizeof operations read those captured extents. Prototype bounds
// remain typed, unevaluated metadata. All type walks follow a bounded derivation chain, without recursion.

static uint32_t check_input_add(CheckInputReader* reader, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c);

static bool check_input_vla_modified(const TypeTable* types, TypeId type)
{
    return type != 0 && (type_inherited_flags(types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) != 0;
}

static bool check_input_vla_runtime_size(const TypeTable* types, TypeId type)
{
    bool runtime = false;
    for (uint32_t depth = 0; type != 0 && depth < types->types.count; depth += 1)
    {
        type = type_strip_aligned(types, type);
        const uint8_t kind = type_kind(types, type);
        if (!type_kind_is_array(kind))
        {
            break;
        }
        runtime |= kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        type = type_element(types, type);
    }
    return runtime;
}

static bool check_input_vla_bound(CheckInputReader* reader, uint32_t open, uint32_t first, uint32_t end, bool parameter, bool star, uint8_t* kind, uint64_t* extent)
{
    const Parser* parser = reader->parser;
    bool valid = open < first && first <= end && end < parser->tokens.count;
    *kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    *extent = 0;
    if (valid && first != end && !star)
    {
        CheckValue value = { 0 };
        const bool cached = reader->bindings.count != 0 && reader->bindings.point[reader->bindings.count - 1] >= first;
        valid = (cached ? check_input_evaluate_cached(reader, first, end, &value) : check_input_evaluate(reader, first, end, &value)) &&
                check_action_is_integer(reader->types, value.type);
        if (valid && (value.flags & BUSTER_CHECK_ICE) != 0)
        {
            int64_t number = 0;
            valid = check_action_i64(reader->types, &value, &number) && number >= 0 && (number != 0 || parser->language.gnu);
            *kind = BUSTER_TYPE_KIND_ARRAY;
            *extent = (uint64_t)number;
        }
        else if (valid)
        {
            *kind = BUSTER_TYPE_KIND_VARIABLE_ARRAY;
            *extent = (uint64_t)first + 1;
        }
    }
    else if (valid && star)
    {
        valid = parameter;
        *kind = BUSTER_TYPE_KIND_VARIABLE_ARRAY;
    }
    if (valid && *kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
    {
        valid = parameter || reader->function_body != BUSTER_CHECK_NONE;
        CheckInput* output = reader->output;
        const bool room = output->vla_count < output->vla_capacity;
        output->resource |= valid && !room;
        valid &= room;
        if (valid)
        {
            const uint32_t index = output->vla_count++;
            output->vlas[index] = (CheckInputVla)
            {
                .declaration = BUSTER_CHECK_NONE,
                .bound_begin = first,
                .bound_end = end,
                .bound_node = BUSTER_CHECK_NONE,
                .body = parameter ? BUSTER_CHECK_NONE : reader->function_body,
                .scope_begin = open,
                .scope_end = reader->scope_count != 0 ? reader->scope_ends[reader->scope_count - 1] : end,
                .evaluated = !parameter && !star,
            };
            // The opener identifies declarator construction; the first bound token identifies type extents.
            // A C bound cannot begin with an array declarator opener, so these two sparse keys never collide.
            reader->vla_bounds[open] = index + 1;
            reader->vla_bounds[first] = index + 1;
            reader->vla_ends[end] = index + 1;
        }
    }
    if (valid)
    {
        if (*kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY && !star)
        {
            memset(reader->ignored + open, 1, first - open);
            reader->ignored[end] = true;
        }
        else
        {
            memset(reader->ignored + open, 1, end - open + 1);
        }
    }
    return valid;
}

static TypeId check_input_vla_type(CheckInputReader* reader, uint32_t open, TypeId element, uint32_t declaration, bool parameter, uint8_t kind, uint64_t extent)
{
    TypeId type = check_input_type_result(reader, kind == BUSTER_TYPE_KIND_ARRAY ? type_array(reader->types, element, extent) :
                 kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY ? type_variable_array(reader->types, element, (uint32_t)extent) :
                                                         type_incomplete_array(reader->types, element));
    if (type != 0 && kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
    {
        const uint32_t identity = reader->vla_bounds[open];
        if (identity == 0 || identity > reader->output->vla_count)
        {
            type = 0;
        }
        else
        {
            CheckInputVla* vla = &reader->output->vlas[identity - 1];
            vla->type = type;
            vla->declaration = declaration;
            vla->evaluated &= !parameter;
        }
    }
    return type;
}

static bool check_input_vla_declaration(CheckInputReader* reader, uint32_t declaration, TypeId type, uint32_t point, uint32_t storage, bool is_typedef, bool initialized, bool thread_local)
{
    bool valid = true;
    if (check_input_vla_modified(reader->types, type))
    {
        const bool array = check_input_vla_runtime_size(reader->types, type);
        const Parser* parser = reader->parser;
        const bool empty = initialized && point + 2 < parser->tokens.count && parser->tokens.kinds[point + 1] == PARSE_KIND_L_BRACE &&
                           parser->tree.parents[point + 1] == point + 2;
        valid = reader->function_body != BUSTER_CHECK_NONE && storage != C_NAME_EXTERN &&
                (is_typedef || !array || (storage != C_NAME_STATIC && !thread_local && (!initialized || empty)));
        if (valid)
        {
            reader->vla_scope_ends[declaration] = reader->scope_count != 0 ? reader->scope_ends[reader->scope_count - 1] + 1 : point + 1;
            for (uint32_t i = 0; i < reader->output->vla_count; i += 1)
            {
                CheckInputVla* vla = &reader->output->vlas[i];
                if (vla->declaration == declaration)
                {
                    vla->body = reader->function_body;
                    vla->scope_begin = point;
                    vla->scope_end = reader->vla_scope_ends[declaration] - 1;
                }
            }
        }
    }
    return valid;
}

static void check_input_vla_parameters(CheckInputReader* reader, uint32_t body)
{
    for (uint32_t i = 0; i < reader->output->vla_count; i += 1)
    {
        CheckInputVla* vla = &reader->output->vlas[i];
        const uint32_t declaration = vla->declaration;
        if (declaration < reader->parser->tokens.count && reader->output->declaration_bodies[declaration] == body)
        {
            vla->body = body;
            vla->scope_begin = body;
            vla->scope_end = reader->function_end;
            vla->evaluated = reader->types->types.extent[type_index(vla->type)] != 0;
        }
    }
}

static uint32_t check_input_vla_root(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint32_t parent = reader->parser->tree.parents[token];
        if (parent < first || parent >= end)
        {
            root = token;
            roots += 1;
        }
    }
    return roots == 1 ? root : BUSTER_CHECK_NONE;
}

static uint32_t check_input_vla_capture(CheckInputReader* reader, uint32_t closing)
{
    const uint32_t identity = reader->vla_ends[closing];
    uint32_t result = BUSTER_CHECK_NONE;
    if (identity != 0 && identity <= reader->output->vla_count)
    {
        CheckInputVla* vla = &reader->output->vlas[identity - 1];
        const bool star = reader->types->types.extent[type_index(vla->type)] == 0;
        const uint32_t root = star ? BUSTER_CHECK_NONE : check_input_vla_root(reader, vla->bound_begin, vla->bound_end);
        const uint32_t operand = root < reader->parser->tokens.count ? reader->output->token_nodes[root] : BUSTER_CHECK_NONE;
        if (operand != BUSTER_CHECK_NONE)
        {
            const uint32_t node = check_input_add(reader, CHECK_OP_VLA_BOUND, closing, operand, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (node != BUSTER_CHECK_NONE)
            {
                reader->output->graph.members[node] = identity;
                vla->bound_node = node;
                result = vla->declaration == BUSTER_CHECK_NONE && vla->evaluated ? node : BUSTER_CHECK_NONE;
            }
        }
        else if (!star)
        {
            check_input_report(reader, closing, CHECK_INPUT_DECLARATION);
        }
    }
    return result;
}

static uint32_t check_input_vla_size(CheckInputReader* reader, uint32_t token, TypeId requested, uint32_t operand)
{
    CheckGraph* graph = &reader->output->graph;
    TypeId type = requested;
    uint32_t result = BUSTER_CHECK_NONE;
    uint64_t fixed = 1;
    bool valid = check_input_vla_runtime_size(reader->types, type);
    for (uint32_t depth = 0; valid && type_kind_is_array(type_kind(reader->types, type_strip_aligned(reader->types, type))) &&
         depth < reader->types->types.count; depth += 1)
    {
        type = type_strip_aligned(reader->types, type);
        const uint64_t extent = reader->types->types.extent[type_index(type)];
        if (type_kind(reader->types, type) == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
        {
            const uint32_t identity = extent != 0 && extent <= reader->parser->tokens.count ? reader->vla_bounds[extent - 1] : 0;
            valid = identity != 0 && identity <= reader->output->vla_count;
            if (valid)
            {
                const CheckInputVla* vla = &reader->output->vlas[identity - 1];
                const bool inline_bound = vla->declaration == BUSTER_CHECK_NONE && vla->bound_begin > token && vla->bound_end <= reader->ends[token];
                const uint32_t value = inline_bound ? vla->bound_node : check_input_add(reader, CHECK_OP_VLA_EXTENT, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                valid = value != BUSTER_CHECK_NONE;
                if (valid)
                {
                    graph->members[value] = identity;
                    result = result == BUSTER_CHECK_NONE ? value : check_input_add(reader, CHECK_OP_MULTIPLY, token, result, value, BUSTER_CHECK_NONE);
                    valid = result != BUSTER_CHECK_NONE;
                }
            }
        }
        else
        {
            valid = extent == 0 || fixed <= UINT64_MAX / extent;
            fixed = valid ? fixed * extent : 0;
        }
        type = type_element(reader->types, type);
    }
    const uint64_t size = type_size(reader->types, type);
    valid &= size == 0 || fixed <= UINT64_MAX / size;
    if (valid)
    {
        fixed *= size;
        const uint32_t stride = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        valid = stride != BUSTER_CHECK_NONE && result != BUSTER_CHECK_NONE;
        if (valid)
        {
            graph->leaves[stride] = (CheckValue)
            {
                .type = BUSTER_TYPE_ULONG,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_ICE,
                .constant_kind = CHECK_CONSTANT_INTEGER,
                .integer = fixed,
            };
            result = check_input_add(reader, CHECK_OP_MULTIPLY, token, result, stride, BUSTER_CHECK_NONE);
            if (result != BUSTER_CHECK_NONE && operand != BUSTER_CHECK_NONE)
            {
                result = check_input_add(reader, CHECK_OP_COMMA, token, operand, result, BUSTER_CHECK_NONE);
            }
            result = result != BUSTER_CHECK_NONE ? check_input_add(reader, CHECK_OP_SIZEOF_VLA, token, result, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE) : BUSTER_CHECK_NONE;
            if (result != BUSTER_CHECK_NONE)
            {
                graph->types[result] = requested;
            }
        }
    }
    if (!valid || result == BUSTER_CHECK_NONE)
    {
        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
    }
    return valid ? result : BUSTER_CHECK_NONE;
}

static void check_input_vla_finish(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    for (uint32_t i = 0; i < reader->output->vla_count; i += 1)
    {
        const CheckInputVla* vla = &reader->output->vlas[i];
        if (vla->evaluated && vla->bound_node == BUSTER_CHECK_NONE)
        {
            check_input_report(reader, vla->bound_begin, CHECK_INPUT_DECLARATION);
        }
    }
    for (uint32_t declaration = 0; declaration < parser->tokens.count; declaration += 1)
    {
        const uint32_t end = reader->vla_scope_ends[declaration];
        if (end == 0)
        {
            continue;
        }
        const uint32_t body = reader->output->declaration_bodies[declaration];
        const uint32_t close = body < parser->tokens.count ? parser->tree.parents[body] : 0;
        for (uint32_t token = body; token < parser->tokens.count && token <= close; token += 1)
        {
            const uint32_t target = reader->output->label_targets[token];
            if (parser->tokens.kinds[token] == PARSE_KIND_GOTO && target >= declaration && target < end && (token < declaration || token >= end))
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
            if (parser->tree.roles[token] == PARSE_ROLE_OPEN_CASE && token >= declaration && token < end)
            {
                const uint32_t owner = reader->output->control_owners[token];
                const uint32_t keyword = owner < parser->tokens.count ? reader->first_child[owner] : BUSTER_CHECK_NONE;
                if (keyword < declaration || keyword >= end)
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
        }
    }
}
