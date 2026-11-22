// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Emission-only fallback for constant if conditions (#1194), included after check_control_foldable.
// The ordinary expression proof remains the fast path. This bounded, nonrecursive statement walk follows source
// order, consumes only a reached break, and never writes the semantic stream or its constant-expression verdicts.
// It does not interpret mutable locals or iterate loops: reaching a loop's backedge refuses the fold. Expressions
// still pass the existing outside-a-constant-context proof, including its __builtin_constant_p restrictions.

// Read only completed, nonvolatile constant initializer receipts. Weak objects keep their runtime load even when
// this translation unit provides an initializer, so require the source reader's nonnull (non-weak) address proof.
static bool check_program_constant_load(const CheckProgram* program, uint32_t node, CheckValue* loaded)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckValue value = check_stream_value(program->stream, node);
    const uint32_t restricted = BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED;
    const uint32_t position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
    const bool known = value.category == CHECK_LVALUE && value.fault == CHECK_FAULT_NONE && value.constant_kind == CHECK_CONSTANT_ADDRESS &&
                       (value.flags & restricted) == 0 && (value.flags & BUSTER_CHECK_NONNULL_ADDRESS) != 0 &&
                       (type_qualifiers(value.type) & BUSTER_TYPE_VOLATILE) == 0 &&
                       check_const_load(program->types, graph->const_values, value.object, value.offset, value.object_bit_offset,
                                        value.bit_width, value.type, position, loaded) &&
                       (loaded->constant_kind == CHECK_CONSTANT_FLOATING || loaded->constant_kind == CHECK_CONSTANT_INTEGER);
    return known;
}

// Code emission may read a completed const integer initializer even in an ordinary C expression (#2987).
// Keep this proof separate from the checked stream: the read does not make the source an integer constant expression.
static bool check_program_const_designator(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    // A named object, its member, or an element at a constant index. The address alone cannot prove that computing
    // it was free of effects (a statement expression can yield the same address after executing arbitrary code).
    while (graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_SUBSCRIPT)
    {
        uint32_t base = graph->operand0[node];
        if (graph->ops[node] == CHECK_OP_MEMBER)
        {
            const CheckValue record = check_stream_value(program->stream, base);
            uint64_t offset = 0;
            uint32_t qualifiers = 0;
            const CheckAction action = {
                .member = graph->members[node],
                .context = check_graph_context(graph, node, true),
            };
            const TypeId type = type_strip_aligned(program->types, record.type);
            const uint32_t member = type_lookup_member(program->types, type, action.member, &offset, &qualifiers);
            if (member == 0 || !check_action_const_member(program->types, &action, &record, type, member))
            {
                break;
            }
        }
        else
        {
            uint32_t index = graph->operand1[node];
            if (!type_kind_is_array(check_action_kind(program->types, check_stream_type(program->stream, base))))
            {
                base = index;
                index = graph->operand0[node];
            }
            const TypeId array = type_strip_aligned(program->types, check_stream_type(program->stream, base));
            const CheckValue offset = check_stream_value(program->stream, index);
            if (check_action_kind(program->types, array) != BUSTER_TYPE_KIND_ARRAY || graph->ops[index] != CHECK_OP_VALUE ||
                offset.constant_kind != CHECK_CONSTANT_INTEGER || offset.integer >= program->types->types.extent[type_index(array)])
            {
                break;
            }
        }
        node = base;
    }
    return graph->ops[node] == CHECK_OP_VALUE;
}

// Fold using the operand types the checker already chose, including integer promotions and the usual arithmetic
// conversions. All inputs are integers of at most 128 bits. Arithmetic faults keep ordinary code generation.
static bool check_program_const_operation(const CheckProgram* program, uint32_t node, unsigned __int128 first, CheckValue* value)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const TypeId type = check_stream_type(program->stream, node);
    const bool binary = graph->operand1[node] != BUSTER_CHECK_NONE;
    CheckValue left = {
        .type = check_stream_type(program->stream, graph->operand0[node]),
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = binary ? first : value->integer,
    };
    const CheckConversion* conversions = program->stream->conversions[node].operands;
    CheckIntegerFold fold = { .op = op, .node = node };
    check_action_convert_value(program->types, &left, conversions[0].type, node);
    bool known = check_action_integer_type(program->types, left.type, &fold.width, &fold.is_signed);
    fold.left = left.integer;
    fold.right_width = fold.width;
    fold.right_signed = fold.is_signed;
    if (binary)
    {
        check_action_convert_value(program->types, value, conversions[1].type, node);
        known = known && check_action_integer_type(program->types, value->type, &fold.right_width, &fold.right_signed);
        fold.right = value->integer;
    }
    *value = (CheckValue){ .type = type, .constant_kind = CHECK_CONSTANT_INTEGER };
    if (known)
    {
        check_action_fold_integer(fold, value);
        known = value->fault == CHECK_FAULT_NONE;
    }
    return known;
}

typedef struct CheckProgramConstFrame CheckProgramConstFrame;
struct CheckProgramConstFrame
{
    unsigned __int128 left;
    uint32_t node;
    bool right;
};

static bool check_program_const_operator(CheckOp op)
{
    return (op >= CHECK_OP_POSITIVE && op <= CHECK_OP_CONDITIONAL) || op == CHECK_OP_CAST || op == CHECK_OP_CONVERT ||
           op == CHECK_OP_CONDITION || op == CHECK_OP_SELECT;
}

// Walk only the evaluated operands, in order. In particular a deciding left operand skips the right operand's
// calls, while an unknown left operand ends the proof. The bounded stack is only used for emission decisions.
static __attribute__((noinline)) bool check_program_const_expression(const CheckProgram* program, uint32_t node, CheckValue* value)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    CheckProgramConstFrame frames[BUSTER_CHECK_BRANCH_TRUTH_DEPTH];
    uint32_t depth = 0;
    bool known = true;
    bool loaded = false;
    bool descend = true;
    while (known && (descend || depth != 0))
    {
        if (descend)
        {
            const CheckOp op = graph->ops[node];
            uint32_t width = 0;
            bool sign = false;
            known = check_action_integer_type(program->types, check_stream_type(stream, node), &width, &sign);
            if (known && check_stream_category(stream, node) == CHECK_LVALUE)
            {
                known = (type_qualifiers(check_stream_type(stream, node)) & BUSTER_TYPE_CONST) != 0 &&
                        check_program_constant_load(program, node, value) && value->constant_kind == CHECK_CONSTANT_INTEGER &&
                        check_program_const_designator(program, node);
                loaded |= known;
                descend = false;
            }
            else if (known && (op == CHECK_OP_VALUE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF))
            {
                *value = check_stream_value(stream, node);
                known = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE &&
                        (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
                descend = false;
            }
            else
            {
                known = known && depth < BUSTER_ARRAY_LENGTH(frames) && stream->conversions != NULL &&
                        check_program_const_operator(op);
                if (known)
                {
                    frames[depth++] = (CheckProgramConstFrame){ .node = node };
                    node = graph->operand0[node];
                }
            }
        }
        else
        {
            CheckProgramConstFrame* frame = frames + depth - 1;
            node = frame->node;
            const CheckOp op = graph->ops[node];
            const bool truth = value->integer != 0;
            const bool logical = op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
            if (!frame->right && graph->operand1[node] != BUSTER_CHECK_NONE &&
                !(logical && truth == (op == CHECK_OP_LOGICAL_OR)) &&
                !(op == CHECK_OP_CONDITIONAL && truth && graph->operand1[node] == graph->operand0[node]))
            {
                frame->right = true;
                frame->left = value->integer;
                node = op == CHECK_OP_CONDITIONAL && !truth ? graph->operand2[node] : graph->operand1[node];
                descend = true;
            }
            else
            {
                depth -= 1;
                if (logical || op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
                {
                    *value = (CheckValue){ .type = BUSTER_TYPE_INT, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = truth != (op == CHECK_OP_NOT) };
                }
                else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_SELECT)
                {
                    check_action_convert_value(program->types, value, check_stream_type(stream, node), node);
                    known = value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE;
                }
                else
                {
                    known = check_program_const_operation(program, node, frame->left, value);
                }
            }
        }
    }
    return known && loaded;
}

static bool check_program_const_integer(const CheckProgram* program, uint32_t node, CheckValue* value)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckConstValues* receipts = graph->const_values;
    return node < graph->count && receipts != NULL && (receipts->count != 0 || receipts->prefix != NULL) &&
           check_program_const_expression(program, node, value);
}

typedef enum CheckEmissionTruth
{
    CHECK_EMISSION_UNKNOWN,
    CHECK_EMISSION_CHECKED,
    CHECK_EMISSION_INITIALIZER,
} CheckEmissionTruth;

// A condition and the arm the initializer would skip must allow pruning even when a GNU statement expression
// contains a label. The taken arm can keep its labels.
static bool check_program_const_enterable(const CheckProgram* program, uint32_t node, uint32_t condition, bool truth)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t skipped = op == CHECK_OP_CONDITIONAL ? (truth ? graph->operand2[node] : graph->operand1[node]) :
                             ((op == CHECK_OP_LOGICAL_AND && !truth) || (op == CHECK_OP_LOGICAL_OR && truth)) ? graph->operand1[node] : BUSTER_CHECK_NONE;
    uint32_t stack[BUSTER_CHECK_BRANCH_TRUTH_FRAMES];
    return check_program_labeled(program, condition, 0, NULL, stack, BUSTER_ARRAY_LENGTH(stack)) ||
           check_program_labeled(program, skipped, 0, NULL, stack, BUSTER_ARRAY_LENGTH(stack));
}

static CheckEmissionTruth check_program_emission_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    CheckEmissionTruth result = check_program_runtime_truth(program, node, condition, truth) ? CHECK_EMISSION_CHECKED : CHECK_EMISSION_UNKNOWN;
    CheckValue value;
    if (result == CHECK_EMISSION_UNKNOWN && check_program_const_integer(program, condition, &value))
    {
        *truth = value.integer != 0;
        if (program->input->region_count == 0 || !check_program_const_enterable(program, node, condition, *truth))
        {
            result = CHECK_EMISSION_INITIALIZER;
        }
    }
    return result;
}

#define BUSTER_CHECK_CONTROL_FOLD_STEPS 128u

typedef enum CheckControlFoldStepKind
{
    CHECK_CONTROL_FOLD_SOURCE,
    CHECK_CONTROL_FOLD_LOOP_END,
    CHECK_CONTROL_FOLD_SWITCH_END,
    CHECK_CONTROL_FOLD_DO_END,
    CHECK_CONTROL_FOLD_DO_TEST,
} CheckControlFoldStepKind;

typedef struct CheckControlFoldStep CheckControlFoldStep;
struct CheckControlFoldStep
{
    uint32_t token;
    CheckControlFoldStepKind kind;
};

typedef struct CheckControlFoldWalk CheckControlFoldWalk;
struct CheckControlFoldWalk
{
    const CheckControlWriter* writer;
    CheckControlFoldStep steps[BUSTER_CHECK_CONTROL_FOLD_STEPS];
    uint32_t depth;
    uint32_t visits;
    uint32_t seeking;
    uint32_t declaration;
    bool initialized;
    bool valid;
};

static void check_control_fold_step(CheckControlFoldWalk* walk, CheckControlFoldStepKind kind, uint32_t token)
{
    if (walk->valid && token != BUSTER_CHECK_NONE)
    {
        walk->valid = walk->depth < BUSTER_CHECK_CONTROL_FOLD_STEPS;
        if (walk->valid)
        {
            walk->steps[walk->depth++] = (CheckControlFoldStep)
            {
                .token = token,
                .kind = kind,
            };
        }
    }
}

// Reverse pushes make the first source statement run first. A case's expressions before its colon aren't statements.
static void check_control_fold_children(CheckControlFoldWalk* walk, uint32_t token, uint32_t omitted, bool label)
{
    const CheckControlWriter* writer = walk->writer;
    const uint8_t* roles = writer->parser->tree.roles;
    const uint32_t* previous = writer->previous;
    for (uint32_t child = writer->last[token]; walk->valid && child != BUSTER_CHECK_NONE; child = previous[child])
    {
        if (child != omitted && roles[child] != PARSE_ROLE_OPEN_BLOCK && (!label || child > token))
        {
            check_control_fold_step(walk, CHECK_CONTROL_FOLD_SOURCE, child);
        }
    }
}

static bool check_control_fold_known(const CheckControlWriter* writer, uint32_t node, CheckValue* value, bool* truth)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t expression = node < graph->count && graph->ops[node] == CHECK_OP_CONDITION ? graph->operand0[node] : node;
    *value = expression < graph->count ? check_stream_value(program->stream, expression) : (CheckValue){ 0 };
    const bool known = expression < graph->count && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                       check_action_truth(program->types, value, truth) && check_control_foldable(writer, expression);
    return known;
}

// check_control_fold_simple queues expression proofs in the writer's scratch. Keep its unconsumed prefix intact
// while proving each expression in the remaining suffix; the statement walk has separate, bounded storage.
static bool check_control_fold_prefix(CheckControlFoldWalk* walk, uint32_t token)
{
    const CheckControlWriter* writer = walk->writer;
    const CheckInput* input = writer->program->input;
    uint32_t depth = 0;
    bool valid = check_control_fold_simple(writer, token, &depth, 0);
    CheckControlWriter expressions = *writer;
    const uint32_t capacity = writer->fold_capacity;
    uint32_t* stack = writer->fold_stack;
    while (valid && depth != 0)
    {
        const uint32_t node = stack[--depth];
        expressions.fold_stack = stack + depth;
        expressions.fold_capacity = capacity - depth;
        valid = check_control_foldable(&expressions, node);
    }
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, token, &low, &high);
    for (uint32_t t = low; valid && t <= high; t += 1)
    {
        if (input->declarations[t] == t)
        {
            const TypeId type = input->declared_types[t];
            const uint8_t kind = check_action_kind(writer->program->types, type);
            // Include pointers to VLAs and variably modified typedefs: their declarations can evaluate bounds too.
            // The ordinary proof already handles effect-free VLAs; the fallback must not discard their effects.
            valid = !check_input_vla_modified(writer->program->types, type) &&
                    (input->declaration_kinds[t] != C_BINDING_OBJECT || kind != BUSTER_TYPE_KIND_ATOMIC);
            walk->initialized |= valid && t == walk->declaration;
        }
    }
    return valid;
}

static void check_control_fold_break(CheckControlFoldWalk* walk)
{
    uint32_t depth = walk->depth;
    bool found = false;
    while (depth != 0 && !found)
    {
        depth -= 1;
        const CheckControlFoldStepKind kind = walk->steps[depth].kind;
        found = kind == CHECK_CONTROL_FOLD_LOOP_END || kind == CHECK_CONTROL_FOLD_SWITCH_END || kind == CHECK_CONTROL_FOLD_DO_END;
    }
    walk->valid = found;
    walk->depth = depth;
}

// Select by the switch's promoted type, including signed ranges, then seek only through blocks and case chains.
// A label nested in another control statement is deliberately refused rather than entered with an invented state.
static void check_control_fold_switch(CheckControlFoldWalk* walk, uint32_t token, uint32_t body, uint32_t condition)
{
    const CheckControlWriter* writer = walk->writer;
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    condition = condition < graph->count && graph->ops[condition] == CHECK_OP_CONDITION ? graph->operand0[condition] : condition;
    uint32_t selected = BUSTER_CHECK_NONE;
    uint32_t fallback = BUSTER_CHECK_NONE;
    for (uint32_t c = writer->case_first[token]; walk->valid && c != BUSTER_CHECK_NONE; c = writer->case_next[c])
    {
        const CheckProgramCase* item = program->cases + c;
        if (item->fallback)
        {
            fallback = item->token;
        }
        else
        {
            unsigned __int128 integer = 0;
            walk->valid = check_program_case_value(program, condition, item->type, &integer, NULL);
            const CheckProgramCase value =
            {
                .lower = integer,
                .type = item->type,
            };
            const unsigned __int128 ordered = check_program_case_order(program, &value, false);
            if (walk->valid && ordered >= check_program_case_order(program, item, false) && ordered <= check_program_case_order(program, item, item->range))
            {
                selected = item->token;
            }
        }
    }
    selected = selected != BUSTER_CHECK_NONE ? selected : fallback;
    if (walk->valid && selected != BUSTER_CHECK_NONE)
    {
        walk->valid = body != BUSTER_CHECK_NONE;
        walk->seeking = selected;
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_SWITCH_END, token);
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_SOURCE, body);
    }
}

static void check_control_fold_branch(CheckControlFoldWalk* walk, uint32_t token)
{
    const CheckControlWriter* writer = walk->writer;
    const Parser* parser = writer->parser;
    const CheckInput* input = writer->program->input;
    const bool loop = parser->tree.roles[token] == PARSE_ROLE_CLOSE_FOR;
    uint32_t keyword = BUSTER_CHECK_NONE;
    uint32_t body = BUSTER_CHECK_NONE;
    uint32_t alternate = BUSTER_CHECK_NONE;
    uint32_t first = BUSTER_CHECK_NONE;
    uint32_t condition = BUSTER_CHECK_NONE;
    const uint32_t* next = writer->next;
    for (uint32_t child = writer->first[token]; child != BUSTER_CHECK_NONE; child = next[child])
    {
        const uint8_t role = parser->tree.roles[child];
        const bool semi = child < token && parser->tokens.kinds[child] == PARSE_KIND_SEMI;
        keyword = child < token && role == PARSE_ROLE_LEAF_RIGHT ? child : keyword;
        condition = semi && first != BUSTER_CHECK_NONE ? child : condition;
        first = semi && first == BUSTER_CHECK_NONE ? child : first;
        alternate = child > token && role == PARSE_ROLE_ELSE ? child : alternate;
        body = child > token && role != PARSE_ROLE_ELSE ? child : body;
    }
    const uint32_t node = loop ? (condition != BUSTER_CHECK_NONE ? check_control_statement_head(writer, condition) : BUSTER_CHECK_NONE) : input->token_nodes[token];
    CheckValue value = { 0 };
    bool truth = true;
    walk->valid = !loop || (first != BUSTER_CHECK_NONE && condition != BUSTER_CHECK_NONE && check_control_fold_prefix(walk, first));
    walk->valid = walk->valid && ((loop && node == BUSTER_CHECK_NONE) || check_control_fold_known(writer, node, &value, &truth));
    const uint8_t kind = keyword != BUSTER_CHECK_NONE ? parser->tokens.kinds[keyword] : 0;
    if (walk->valid && !loop && kind == PARSE_KIND_IF)
    {
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_SOURCE, truth ? body : alternate);
    }
    else if (walk->valid && (loop || kind == PARSE_KIND_WHILE))
    {
        if (truth)
        {
            walk->valid = body != BUSTER_CHECK_NONE;
            check_control_fold_step(walk, CHECK_CONTROL_FOLD_LOOP_END, token);
            check_control_fold_step(walk, CHECK_CONTROL_FOLD_SOURCE, body);
        }
    }
    else if (walk->valid && kind == PARSE_KIND_SWITCH)
    {
        check_control_fold_switch(walk, token, body, node);
    }
    else
    {
        walk->valid = false;
    }
}

static void check_control_fold_terminator(CheckControlFoldWalk* walk, uint32_t token)
{
    const CheckControlWriter* writer = walk->writer;
    const Parser* parser = writer->parser;
    uint32_t jump = BUSTER_CHECK_NONE;
    uint32_t body = BUSTER_CHECK_NONE;
    uint32_t condition = BUSTER_CHECK_NONE;
    const uint32_t* next = writer->next;
    for (uint32_t child = writer->first[token]; walk->valid && child != BUSTER_CHECK_NONE; child = next[child])
    {
        const uint8_t role = parser->tree.roles[child];
        walk->valid = role != PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
        jump = role == PARSE_ROLE_RETURN ? child : jump;
        body = role == PARSE_ROLE_DO ? child : body;
        condition = role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
    }
    if (walk->valid && jump != BUSTER_CHECK_NONE)
    {
        if (parser->tokens.kinds[jump] == PARSE_KIND_BREAK)
        {
            check_control_fold_break(walk);
        }
        else
        {
            walk->valid = false;
        }
    }
    else if (walk->valid && body != BUSTER_CHECK_NONE)
    {
        walk->valid = condition != BUSTER_CHECK_NONE;
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_DO_END, token);
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_DO_TEST, condition);
        check_control_fold_step(walk, CHECK_CONTROL_FOLD_SOURCE, body);
    }
    else if (walk->valid)
    {
        walk->valid = check_control_fold_prefix(walk, token);
    }
}

// A direct read of a nonvolatile const integer local with one constant initializer. Its declaration must actually
// execute in this region; a switch jumping past it must not conjure a value. No writes or calls are accepted by the
// prefix proof, so a successfully checked initializer remains the value until the result. Other reads stay unknown.
static bool check_control_fold_const_local(const CheckControlWriter* writer, const CheckInputRegion* region, CheckValue* value, uint32_t* declaration)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t node = region->result;
    const uint32_t use = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    // Automatic locals use the checker's retained binding, not the publication map for linkable objects.
    const uint32_t token = use < input->token_count ? input->declarations[use] : BUSTER_CHECK_NONE;
    uint32_t low = 0;
    uint32_t high = 0;
    check_control_extent(writer, region->block, &low, &high);
    const TypeId type = token < input->token_count ? input->declared_types[token] : 0;
    uint32_t width = 0;
    bool sign = false;
    bool known = token > low && token < high && token < graph->tokens[node] && !check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC) &&
                 (type_qualifiers(type) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)) == BUSTER_TYPE_CONST &&
                 check_action_integer_type(program->types, type, &width, &sign) && program->initializers != NULL;
    const uint32_t index = known ? check_initializers_plan_index(program->initializers, token) : 0;
    const CheckInitializer* plan = index != 0 ? program->initializers->plans + index - 1 : NULL;
    known = plan != NULL && plan->count == 1 && plan->parts[0].kind == CHECK_INIT_VALUE && plan->parts[0].bit_offset == 0;
    CheckValue initialized = known ? plan->parts[0].value : (CheckValue){ 0 };
    if (known && !check_control_fold_value(&initialized))
    {
        const uint32_t initializer = plan->parts[0].node;
        initialized = initializer < graph->count ? check_stream_value(program->stream, initializer) : (CheckValue){ 0 };
        check_action_convert_value(program->types, &initialized, type, initializer);
    }
    known = known && check_control_fold_value(&initialized);
    if (known)
    {
        *value = initialized;
        *declaration = token;
    }
    return known;
}

// Keep the fallback's bounded statement stack off the ordinary condition walk's frame.
static __attribute__((noinline)) bool check_control_fold_region_truth(const CheckControlWriter* writer, uint32_t expression, bool* truth)
{
    const CheckProgram* program = writer->program;
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const uint32_t token = graph->tokens[expression];
    const uint32_t id = input->region_count != 0 && token < input->token_count ? input->expression_regions[token] : 0;
    const CheckInputRegion* region = id != 0 && id <= input->region_count ? input->regions + id - 1 : NULL;
    CheckControlFoldWalk walk =
    {
        .writer = writer,
        .seeking = BUSTER_CHECK_NONE,
        .declaration = BUSTER_CHECK_NONE,
        .valid = region != NULL && region->block < input->token_count && region->result < graph->count && writer->cleanup_count == 0,
    };
    CheckValue value = walk.valid ? check_stream_value(program->stream, region->result) : (CheckValue){ 0 };
    if (walk.valid)
    {
        walk.valid = (check_control_fold_value(&value) && check_control_foldable(writer, region->result)) ||
                     check_control_fold_const_local(writer, region, &value, &walk.declaration);
    }
    uint32_t width = 0;
    bool sign = false;
    walk.valid = walk.valid && check_action_integer_type(program->types, value.type, &width, &sign) && check_action_truth(program->types, &value, truth);
    if (walk.valid)
    {
        check_control_fold_children(&walk, region->block, region->final_statement, false);
    }
    const uint8_t* roles = writer->parser->tree.roles;
    const uint32_t capacity = writer->fold_capacity;
    while (walk.valid && walk.depth != 0)
    {
        const CheckControlFoldStep step = walk.steps[--walk.depth];
        walk.valid = walk.visits < capacity;
        walk.visits += 1;
        const uint8_t role = roles[step.token];
        if (!walk.valid)
        {
            break;
        }
        if (step.kind == CHECK_CONTROL_FOLD_LOOP_END)
        {
            walk.valid = false;
        }
        else if (step.kind == CHECK_CONTROL_FOLD_SWITCH_END || step.kind == CHECK_CONTROL_FOLD_DO_END)
        {
            walk.valid = walk.seeking == BUSTER_CHECK_NONE;
        }
        else if (step.kind == CHECK_CONTROL_FOLD_DO_TEST)
        {
            CheckValue condition = { 0 };
            bool taken = true;
            walk.valid = check_control_fold_known(writer, input->token_nodes[step.token], &condition, &taken) && !taken;
        }
        else
        {
            if (walk.seeking == step.token && role == PARSE_ROLE_CLOSE_CASE)
            {
                walk.seeking = BUSTER_CHECK_NONE;
            }
            if (walk.seeking != BUSTER_CHECK_NONE && role != PARSE_ROLE_CLOSE_BLOCK && role != PARSE_ROLE_CLOSE_CASE)
            {
                continue;
            }
            if (role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO || role == PARSE_ROLE_CLOSE_CASE)
            {
                check_control_fold_children(&walk, step.token, BUSTER_CHECK_NONE, role == PARSE_ROLE_CLOSE_CASE);
            }
            else if (role == PARSE_ROLE_TERMINATOR)
            {
                check_control_fold_terminator(&walk, step.token);
            }
            else if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR)
            {
                check_control_fold_branch(&walk, step.token);
            }
            else
            {
                walk.valid = false;
            }
        }
    }
    return walk.valid && walk.seeking == BUSTER_CHECK_NONE && (walk.declaration == BUSTER_CHECK_NONE || walk.initialized);
}

static bool check_control_fold_truth(const CheckControlWriter* writer, uint32_t expression, bool* truth)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckValue value = expression < graph->count ? check_stream_value(program->stream, expression) : (CheckValue){ 0 };
    uint32_t width = 0;
    bool sign = false;
    const bool integer = expression < graph->count && check_action_integer_type(program->types, value.type, &width, &sign);
    bool folded = integer && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && check_action_truth(program->types, &value, truth) &&
                  check_control_foldable(writer, expression);
    CheckValue initialized;
    if (!folded && integer && check_program_const_integer(program, expression, &initialized))
    {
        *truth = initialized.integer != 0;
        folded = true;
    }
    if (!folded && integer && graph->ops[expression] == CHECK_OP_STATEMENT_VALUE)
    {
        folded = check_control_fold_region_truth(writer, expression, truth);
    }
    return folded;
}
