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

// A known non-ICE value still denotes a VLA in block/prototype/type-name contexts. File declarations and
// record members instead use Clang's constant-array folding extension, with its default-on context warning.
static bool check_input_vla_force_constant(const CheckInputReader* reader, uint32_t open, bool parameter)
{
    const Parser* parser = reader->parser;
    bool force = !parameter && reader->function_body == BUSTER_CHECK_NONE;
    for (uint32_t token = open, steps = 0; !parameter && token < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_CLOSE_STRUCT)
        {
            force = true;
            break;
        }
        if (role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_KEYWORD)
        {
            force = false;
            break;
        }
        if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK)
        {
            break;
        }
        token = parser->tree.parents[token];
    }
    return force;
}

// Whether a bound defines a struct, union or enum that the reader hasn't read yet, each outside the bound's statement
// expressions (`[sizeof(struct S { int x; }) + ({ n; })]`). One inside a statement expression is a declaration of its
// block, which is the walk's (check_input_region_prepare) and which a bound doesn't take yet (docs/c-checker.md,
// "Variable-length arrays"): its bound is read as before, without a job.
static bool check_input_vla_defines(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    bool outside = false;
    bool inside = false;
    uint32_t region_end = first;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        // A statement expression, `(` and `{`, to its `)`, as check_constant_source_regions finds one.
        if (parser->tree.roles[token] == PARSE_ROLE_OPEN_GROUP && token + 1 < end && parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_BLOCK &&
            parser->tree.parents[token] < end)
        {
            const uint32_t after = parser->tree.parents[token] + 1;
            region_end = after > region_end ? after : region_end;
        }
        if ((kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM) && reader->tag_types[token] == 0)
        {
            const uint32_t body = check_input_tag_body(parser, token, end);
            const bool definition = body < end && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE;
            inside |= definition && token < region_end;
            outside |= definition && token >= region_end;
        }
    }
    return outside && !inside;
}

// The bound from `first` to `end` (its `]`) of the array declarator that `open` opens. One that defines a struct, union
// or enum (`[sizeof(struct V { int x; })]`) is evaluated by a job of the constant evaluator (check_input_constant_job),
// which binds the bound's names in source order and stops at each definition, as an enumerator's value's does: then
// *definition is the definition's keyword and *job the job, and the caller reads the definition
// (check_input_tag_definition, or a tag frame of its own) before it calls this again for the bound, which goes on with
// the job. Otherwise *definition is BUSTER_CHECK_NONE and *job 0.
static bool check_input_vla_bound(CheckInputReader* reader, uint32_t open, uint32_t first, uint32_t end, uint32_t name, bool parameter, bool star,
                                  CheckConstantInput** job, uint32_t* definition, uint8_t* kind, uint64_t* extent)
{
    const Parser* parser = reader->parser;
    const uint32_t previous_warning_bound = reader->warning_bound_open;
    reader->warning_bound_open = open;
    bool valid = open < first && first <= end && end < parser->tokens.count;
    *kind = BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    *extent = 0;
    *definition = BUSTER_CHECK_NONE;
    if (valid && first != end && !star)
    {
        CheckValue value = { 0 };
        uint32_t failed = BUSTER_CHECK_NONE;
        if (*job == 0 && check_input_vla_defines(reader, first, end))
        {
            *job = check_input_constant_job(reader, first, end);
            valid = *job != 0;
        }
        if (*job != 0)
        {
            CheckConstantInput* input = *job;
            valid = check_input_constant_job_step(input, &value, definition);
            if (*definition == BUSTER_CHECK_NONE)
            {
                failed = input->failure;
                *job = 0;
                arena_pop_to(reader->scratch, input->mark);
            }
        }
        else if (valid)
        {
            TypeId unused = 0;
            const bool cached = reader->bindings.count != 0 && reader->bindings.point[reader->bindings.count - 1] >= first;
            valid = check_input_constant_read(reader, first, end, false, cached, &value, &unused, &failed);
        }
        // A job that stopped at a definition has no value yet.
        const bool evaluated = *definition == BUSTER_CHECK_NONE;
        valid = valid && (!evaluated || check_input_const_rvalue(reader, end, &value));
        valid = valid && (!evaluated || check_action_is_integer(reader->types, value.type));
        // clang folds a GNU constant in an array bound, but not a saturated floating conversion's value.
        const bool folded = evaluated && check_input_integer_constant(reader, &value) && (value.flags & (BUSTER_CHECK_GNU_DIVISION | BUSTER_CHECK_GNU_FLOAT_RANGE)) == 0 &&
                            (check_input_integer_ice(reader, &value) || check_input_vla_force_constant(reader, open, parameter));
        if (valid && evaluated && (folded || (value.flags & BUSTER_CHECK_ICE) != 0))
        {
            int64_t number = 0;
            valid = check_action_i64(reader->types, &value, &number) && number >= 0;
            *kind = BUSTER_TYPE_KIND_ARRAY;
            *extent = (uint64_t)number;
            if (valid)
            {
                check_input_constant_bound_warning(reader, first, end, &value, name < parser->tokens.count ? name : open);
            }
        }
        else if (valid && evaluated)
        {
            *kind = BUSTER_TYPE_KIND_VARIABLE_ARRAY;
            *extent = (uint64_t)first + 1;
        }
        if (!valid && !reader->output->resource && reader->output->report_count == 0)
        {
            const uint32_t token = failed < parser->tokens.count ? failed : first;
            const bool missing = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && reader->output->declarations[token] == BUSTER_CHECK_NONE;
            check_input_report(reader, token, missing ? CHECK_INPUT_UNDECLARED : CHECK_INPUT_DECLARATION);
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
            check_input_rare_claim(reader, &reader->vla_bounds[open]);
            reader->vla_bounds[open] = index + 1;
            check_input_rare_claim(reader, &reader->vla_bounds[first]);
            reader->vla_bounds[first] = index + 1;
            check_input_rare_claim(reader, &reader->vla_ends[end]);
            reader->vla_ends[end] = index + 1;
        }
    }
    if (valid && *definition == BUSTER_CHECK_NONE)
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
    reader->warning_bound_open = previous_warning_bound;
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

// A declaration-time type query has already checked its bounds with the constant/type evaluator. Retain only
// dimensions introduced by this query; a typedef's earlier extent belongs to its original capture. This helper
// deliberately performs no expression evaluation or name lookup, keeping the source/type call graph acyclic.
static bool check_input_vla_query(CheckInputReader* reader, uint32_t first, uint32_t end, TypeId type)
{
    const Parser* parser = reader->parser;
    bool valid = first <= end && end <= parser->tokens.count;
    for (uint32_t depth = 0; valid && type != 0 && depth < reader->types->types.count; depth += 1)
    {
        type = type_strip_aligned(reader->types, type);
        const uint8_t kind = type_kind(reader->types, type);
        if (kind != BUSTER_TYPE_KIND_POINTER && kind != BUSTER_TYPE_KIND_ATOMIC && !type_kind_is_array(kind))
        {
            break;
        }
        if (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
        {
            const uint64_t extent = reader->types->types.extent[type_index(type)];
            if (extent >= (uint64_t)first + 2 && extent <= end)
            {
                const uint32_t begin = (uint32_t)extent - 1;
                const uint32_t open = begin - 1;
                const uint32_t close = parser->tree.parents[open];
                valid = parser->tokens.kinds[open] == PARSE_KIND_L_SQUARE && close > begin && close < end;
                if (valid && reader->vla_bounds[begin] == 0)
                {
                    CheckInput* output = reader->output;
                    valid = output->vla_count < output->vla_capacity;
                    output->resource |= !valid;
                    if (valid)
                    {
                        const uint32_t identity = output->vla_count++;
                        output->vlas[identity] = (CheckInputVla){
                            .declaration = BUSTER_CHECK_NONE,
                            .type = type,
                            .bound_begin = begin,
                            .bound_end = close,
                            .bound_node = BUSTER_CHECK_NONE,
                            .body = reader->function_body,
                            .scope_begin = open,
                            .scope_end = reader->scope_count != 0 ? reader->scope_ends[reader->scope_count - 1] : end,
                            .evaluated = reader->function_body != BUSTER_CHECK_NONE,
                        };
                        check_input_rare_claim(reader, &reader->vla_bounds[open]);
                        reader->vla_bounds[open] = identity + 1;
                        check_input_rare_claim(reader, &reader->vla_bounds[begin]);
                        reader->vla_bounds[begin] = identity + 1;
                        check_input_rare_claim(reader, &reader->vla_ends[close]);
                        reader->vla_ends[close] = identity + 1;
                    }
                }
                if (valid)
                {
                    memset(reader->ignored + begin, 0, close - begin);
                    reader->ignored[open] = true;
                    reader->ignored[close] = true;
                }
            }
        }
        type = type_element(reader->types, type);
    }
    return valid;
}

// Only this declaration's specifiers and declarator can introduce its bounds. Earlier typedef captures retain their
// original owners and scope; a suspended typeof specifier keeps its append mark across the source walk's resume.
static bool check_input_vla_declaration(CheckInputReader* reader, uint32_t first, uint32_t vla_begin, uint32_t declaration, TypeId type, uint32_t point, uint32_t storage, bool is_typedef, bool initialized, bool thread_local)
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
            check_input_rare_claim(reader, &reader->vla_scope_ends[declaration]);
            reader->vla_scope_ends[declaration] = reader->scope_count != 0 ? reader->scope_ends[reader->scope_count - 1] + 1 : point + 1;
            for (uint32_t i = vla_begin; i < reader->output->vla_count; i += 1)
            {
                CheckInputVla* vla = &reader->output->vlas[i];
                if (vla->declaration == BUSTER_CHECK_NONE && vla->bound_begin >= first && vla->bound_end < declaration)
                {
                    vla->declaration = declaration;
                }
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
    // The body's parameters are the only declarations whose body the walk has set to this one here. Reading no token
    // outside their range keeps a body read on a lane (#113) from reading what another lane's body writes.
    uint32_t first = BUSTER_CHECK_NONE;
    uint32_t last = 0;
    for (uint32_t p = reader->parameters[body]; p != BUSTER_CHECK_NONE; p = reader->parameter_next[p])
    {
        first = p < first ? p : first;
        last = p > last ? p : last;
    }
    // Deferred lanes see all top-level headers' records. Use the header's saved span, not the lane's total count.
    for (uint32_t i = reader->parameter_vla_begin; first != BUSTER_CHECK_NONE && i < reader->parameter_vla_end; i += 1)
    {
        CheckInputVla* vla = &reader->output->vlas[i];
        const uint32_t declaration = vla->declaration;
        if (declaration >= first && declaration <= last && reader->output->declaration_bodies[declaration] == body)
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

static uint32_t check_input_vla_scale(CheckInputReader* reader, uint32_t token, uint32_t node, uint64_t factor)
{
    CheckGraph* graph = &reader->output->graph;
    if (node == BUSTER_CHECK_NONE || factor != 1)
    {
        const uint32_t constant = check_input_add(reader, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (constant != BUSTER_CHECK_NONE)
        {
            *check_graph_leaf_of(graph, constant) = (CheckValue)
            {
                .type = BUSTER_TYPE_ULONG,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_ICE,
                .constant_kind = CHECK_CONSTANT_INTEGER,
                .integer = factor,
            };
        }
        node = node == BUSTER_CHECK_NONE ? constant : constant != BUSTER_CHECK_NONE ?
               check_input_add(reader, CHECK_OP_MULTIPLY, token, node, constant, BUSTER_CHECK_NONE) : BUSTER_CHECK_NONE;
    }
    return node;
}

typedef struct CheckVlaSizePart CheckVlaSizePart;
struct CheckVlaSizePart
{
    uint64_t factor;
    uint32_t identity;
    bool inline_bound;
};

static uint32_t check_input_vla_size(CheckInputReader* reader, uint32_t token, TypeId requested, uint32_t operand)
{
    CheckGraph* graph = &reader->output->graph;
    const uint64_t mark = reader->scratch->length;
    uint32_t dimensions = 0;
    for (TypeId type = requested; dimensions < reader->types->types.count &&
         type_kind_is_array(type_kind(reader->types, type_strip_aligned(reader->types, type))); type = type_element(reader->types, type_strip_aligned(reader->types, type)))
    {
        dimensions += 1;
    }
    CheckVlaSizePart* parts = arena_push_array(reader->scratch, dimensions, sizeof(CheckVlaSizePart), 8);
    reader->output->resource |= parts == 0;
    TypeId type = requested;
    uint32_t result = BUSTER_CHECK_NONE;
    uint64_t fixed = 1;
    uint32_t count = 0;
    bool valid = parts != 0 && check_input_vla_runtime_size(reader->types, type);
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
                if (!inline_bound && reader->vla_sizes[identity - 1] != BUSTER_CHECK_NONE)
                {
                    result = reader->vla_sizes[identity - 1];
                    break;
                }
                parts[count++] = (CheckVlaSizePart)
                {
                    .factor = fixed,
                    .identity = identity,
                    .inline_bound = inline_bound,
                };
                fixed = 1;
            }
        }
        else
        {
            valid = extent == 0 || fixed <= UINT64_MAX / extent;
            fixed = valid ? fixed * extent : 0;
        }
        type = type_element(reader->types, type);
    }
    const uint64_t size = result == BUSTER_CHECK_NONE ? type_size(reader->types, type) : 1;
    valid &= size == 0 || fixed <= UINT64_MAX / size;
    if (valid)
    {
        result = check_input_vla_scale(reader, token, result, fixed * size);
        valid = result != BUSTER_CHECK_NONE;
        while (valid && count != 0)
        {
            const CheckVlaSizePart part = parts[--count];
            const uint32_t value = part.inline_bound ? reader->output->vlas[part.identity - 1].bound_node :
                                   check_input_add(reader, CHECK_OP_VLA_EXTENT, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            if (value != BUSTER_CHECK_NONE)
            {
                graph->members[value] = part.identity;
                result = check_input_add(reader, CHECK_OP_MULTIPLY, token, value, result, BUSTER_CHECK_NONE);
            }
            valid = value != BUSTER_CHECK_NONE && result != BUSTER_CHECK_NONE;
            if (valid)
            {
                if (!part.inline_bound)
                {
                    reader->vla_sizes[part.identity - 1] = result;
                }
                result = check_input_vla_scale(reader, token, result, part.factor);
                valid = result != BUSTER_CHECK_NONE;
            }
        }
        if (valid && operand != BUSTER_CHECK_NONE)
        {
            result = check_input_add(reader, CHECK_OP_COMMA, token, operand, result, BUSTER_CHECK_NONE);
            valid = result != BUSTER_CHECK_NONE;
        }
        if (valid)
        {
            result = check_input_add(reader, CHECK_OP_SIZEOF_VLA, token, result, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            valid = result != BUSTER_CHECK_NONE;
            if (valid)
            {
                graph->types[result] = requested;
            }
        }
    }
    if (!valid || result == BUSTER_CHECK_NONE)
    {
        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
    }
    arena_pop_to(reader->scratch, mark);
    return valid ? result : BUSTER_CHECK_NONE;
}

static bool check_input_indirect_goto(const Parser* parser, uint32_t token);
static bool check_input_label_address(const Parser* parser, uint32_t token);

// The scopes a jump mustn't enter (clang's JumpScopeChecker): a variably modified declaration's, and a cleanup's
// variable's (check_input_cleanup_declaration), from the declaration to its scope's end (vla_scope_ends).
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
    // Lexical VM lifetimes are nested intervals. Their most recent active declaration implies every enclosing
    // lifetime, so one stack scan gives every transfer target its complete entry constraint in constant space
    // per token. Binding replay is finished; its scope scratch columns are available for this temporary map.
    // A unit without a variably modified declaration has no lifetime for a jump to enter, and the scans below would find
    // nothing: the vector path tests the declarations' lifetimes 16 tokens at a time for one, and the scalar reader
    // makes the scans anyway, their twin.
    bool lifetimes = BUSTER_SCALAR_PATH(parser->options.scalar);
    for (uint32_t first = 0; !lifetimes && first < parser->tokens.count; first += 16)
    {
        lifetimes = check_input_scan_nonzero32(reader->vla_scope_ends, first, parser->tokens.count) != 0;
    }
    const uint32_t scanned = lifetimes ? parser->tokens.count : 0;
    uint32_t depth = 0;
    for (uint32_t token = 0; token < scanned; token += 1)
    {
        while (depth != 0 && reader->vla_scope_ends[reader->scope_ends[depth - 1]] <= token)
        {
            depth -= 1;
        }
        if (reader->vla_scope_ends[token] != 0)
        {
            reader->scope_ends[depth++] = token;
        }
        reader->scope_marks[token] = depth != 0 ? reader->scope_ends[depth - 1] + 1 : 0;
    }
    for (uint32_t token = 0; token < scanned; token += 1)
    {
        uint32_t source = token;
        uint32_t target = BUSTER_CHECK_NONE;
        if (parser->tokens.kinds[token] == PARSE_KIND_GOTO)
        {
            target = reader->output->label_targets[token];
        }
        else if (parser->tree.roles[token] == PARSE_ROLE_OPEN_CASE)
        {
            const uint32_t owner = reader->output->control_owners[token];
            source = owner < parser->tokens.count ? reader->first_child[owner] : BUSTER_CHECK_NONE;
            target = token;
        }
        if (target < parser->tokens.count && reader->scope_marks[target] != 0)
        {
            const uint32_t declaration = reader->scope_marks[target] - 1;
            if (source < declaration || source >= reader->vla_scope_ends[declaration])
            {
                check_input_report(reader, token, target == token ? CHECK_INPUT_JUMP_SWITCH : CHECK_INPUT_JUMP_GOTO);
            }
        }
    }
    // A computed goto may go to any label whose address its function takes, so it must be inside the scope of every
    // variable-length array or cleanup such a label is in (clang's JumpScopeChecker), whichever way it leaves its own: the
    // labels' innermost scopes intersect in one interval, [low, high), which holds each goto. Nor may it leave a cleanup's
    // scope: the labels' first and last must be in its innermost, whose records come in source order, as the bodies do.
    const CheckInputCleanup* cleanups = reader->output->cleanups;
    const uint32_t cleanup_count = reader->output->cleanup_count;
    uint32_t next = 0;
    for (uint32_t opening = 0; reader->indirect_gotos != 0 && opening < scanned; opening += 1)
    {
        const bool deferred = reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[opening] != 0;
        const uint32_t closing = parser->tree.parents[opening];
        if (!deferred && parser->tree.roles[opening] == PARSE_ROLE_OPEN_BODY && closing < parser->tokens.count)
        {
            uint32_t low = 0;
            uint32_t high = UINT32_MAX;
            uint32_t first_label = UINT32_MAX;
            uint32_t last_label = 0;
            for (uint32_t token = opening + 1; token < closing; token += 1)
            {
                const uint32_t label = check_input_label_address(parser, token) ? reader->output->label_targets[token] : BUSTER_CHECK_NONE;
                if (label < parser->tokens.count && reader->scope_marks[label] != 0)
                {
                    const uint32_t declaration = reader->scope_marks[label] - 1;
                    low = declaration > low ? declaration : low;
                    high = reader->vla_scope_ends[declaration] < high ? reader->vla_scope_ends[declaration] : high;
                }
                first_label = label < first_label ? label : first_label;
                last_label = label < parser->tokens.count && label > last_label ? label : last_label;
            }
            // The cleanups' scopes that hold each token, innermost last, which a stack of the body's records keeps.
            while (next < cleanup_count && cleanups[next].declaration < opening)
            {
                next += 1;
            }
            const uint64_t mark = reader->scratch->length;
            uint32_t* active = next < cleanup_count ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)cleanup_count - next) : NULL;
            reader->output->resource |= next < cleanup_count && active == NULL;
            uint32_t active_count = 0;
            for (uint32_t token = opening + 1; token < closing; token += 1)
            {
                while (active_count != 0 && cleanups[active[active_count - 1]].end <= token)
                {
                    active_count -= 1;
                }
                if (active != NULL && next < cleanup_count && cleanups[next].declaration == token)
                {
                    active[active_count++] = next++;
                }
                const CheckInputCleanup* innermost = active_count != 0 ? cleanups + active[active_count - 1] : NULL;
                const bool leaves = innermost != NULL && (first_label <= innermost->declaration || last_label >= innermost->end);
                if (check_input_indirect_goto(parser, token) && check_input_asm_id(reader->output, token) == 0 && (token < low || token >= high || leaves))
                {
                    check_input_report(reader, token, CHECK_INPUT_JUMP_INDIRECT);
                }
            }
            arena_pop_to(reader->scratch, mark);
            opening = closing;
        }
    }
}
