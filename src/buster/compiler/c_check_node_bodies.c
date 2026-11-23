// A function body's controls from its tokens, without a node of the walk's (#2850; docs/node-machine.md, "The same
// comparison before the walk"): what a producer of a body's lowered function has to make where the program's builder
// reads the walk's nodes. And the producer's decision ("The producer, deciding before the walk"): what says, before the
// expressions' walk reads a body, that the machine with no node of the walk's takes it, and what is kept of a body it
// takes. No compile takes either: measure-node-machine --shadow --bodies makes the controls after the pipeline and
// compares them with the program's, and with --produce runs the producer at reader time and compares what it decided
// (c_check_node_census.c).
//
// Include after c_check_node_machine_alone.c.
#ifndef BUSTER_C_CHECK_NODE_BODIES_INCLUDED
#define BUSTER_C_CHECK_NODE_BODIES_INCLUDED

// Why the controls' twin from the tokens leaves a body out (node_twin_build, below the report that names them).
typedef enum NodeTwinReason
{
    NODE_TWIN_ATTEMPTED,
    // The machine did not take the body whole before the walk: a control's expression may have no root there.
    NODE_TWIN_UNWHOLE,
    // The program dropped a range of it (a constant `if`'s), by a node's value.
    NODE_TWIN_DROPPED,
    NODE_TWIN_SWITCH,
    NODE_TWIN_ASM,
    // A variable-length array, a variably modified typeof's operand, or a cleanup.
    NODE_TWIN_VARIABLE,
    NODE_TWIN_CLEANUP,
    // An indirect goto, a musttail return, a break or a continue outside its loop.
    NODE_TWIN_JUMP,
    // A block's static or extern declaration, whose initializer's discard the program reads a node of.
    NODE_TWIN_STATIC,
    // A statement expression.
    NODE_TWIN_REGION,
    // A statement the program may leave out because no control reaches it (#2987, c_check_program_reach.c): one that
    // follows, in its block, a statement control does not leave for the next, and that is no labelled statement.
    NODE_TWIN_REACH,
    // A statement whose shape the twin does not know (a condition without its keyword or its body), or a token with
    // several roots.
    NODE_TWIN_SHAPE,
    NODE_TWIN_ROOM,
    NODE_TWIN_REASONS,
} NodeTwinReason;

static const char* const node_twin_reason_names[NODE_TWIN_REASONS] = {
    "attempted", "not-whole-before-the-walk", "a-range-the-program-dropped", "a-switch", "an-asm-statement", "a-variable-length-array-or-typeof", "a-cleanup",
    "a-jump-of-another-kind", "a-block's-static-or-extern-declaration", "a-statement-expression", "a-statement-no-control-reaches", "a-shape-it-does-not-know", "no-room",
};

// The controls' twin from the tokens (measure-node-machine --shadow --bodies with --front-alone; docs/node-machine.md, "The
// same comparison before the walk"): a body's controls made as check_control_build makes them, from the parser's tree
// and tokens, the reader's columns the resolution fills (a goto's label, a declaration's initializer's place and its
// storage) and, for whether a token has an expression, the roots the machine with no node of the walk's ran on before
// the walk, by each control's token. It reads no node of the graph. What it makes: a block, an `if` with or without an
// `else`, a `while`, a do statement, a `for`, a break, a continue, a return, a goto and its label, an expression
// statement, a declaration with initializers. Every other body is left out by its first reason (NodeTwinReason).
//
// A twin's control has the program's kind, token and target, and whether it has an expression where the program's has a
// node; an INITIALIZE's target, the program's plan, is not the twin's to name.
// (NodeTwinControl is c_check_node_machine_front.c's, where the program and the lowering read a taken body's.)

#define BUSTER_NODE_TWIN_TASK_STATEMENT 0u
#define BUSTER_NODE_TWIN_TASK_LABEL 1u
#define BUSTER_NODE_TWIN_TASK_ENTRY 2u
#define BUSTER_NODE_TWIN_TASK_TOKEN 3u

typedef struct NodeTwinTask NodeTwinTask;
struct NodeTwinTask
{
    uint32_t token;
    uint32_t stop;
    uint32_t repeat;
    NodeTwinControl entry;
    uint8_t kind;
};

// The walk that asks whether control reaches each statement (node_twin_unreached): a statement on the way down, a
// sequence of them, an `if` after its first arm and after its second, a loop after its body.
#define BUSTER_NODE_TWIN_REACH_STATEMENT 0u
#define BUSTER_NODE_TWIN_REACH_SEQUENCE 1u
#define BUSTER_NODE_TWIN_REACH_THEN 2u
#define BUSTER_NODE_TWIN_REACH_ELSE 3u
#define BUSTER_NODE_TWIN_REACH_LOOP 4u

typedef struct NodeTwinReach NodeTwinReach;
struct NodeTwinReach
{
    uint32_t token;
    // A sequence's next statement; an `if`'s else.
    uint32_t child;
    uint8_t step;
    // A sequence's: whether control reaches its next statement. An `if`'s: whether control leaves its first arm. A loop's:
    // whether control leaves it for the next statement.
    bool reaches;
};

typedef struct NodeTwin NodeTwin;
struct NodeTwin
{
    // By a token's place in the body at hand (its number less the opening brace's, so a unit's bodies share the columns'
    // first pages): each token's first and last child and its siblings (check_control_children's), a label's number, and a
    // terminator's first declaration with an initializer and each such declaration's next. A body's tokens and two more.
    uint32_t* first;
    uint32_t* last;
    uint32_t* next;
    uint32_t* previous;
    uint32_t* source_labels;
    uint32_t* plan_first;
    uint32_t* plan_last;
    uint32_t* plan_next;
    NodeTwinTask* tasks;
    NodeTwinReach* reach;
    uint32_t* labels;
    NodeTwinControl* entries;
    // The return flow's walk of the twin's controls (node_twin_silent): each one reached, and those to visit.
    uint8_t* seen;
    uint32_t* queue;
    uint32_t capacity;
    uint32_t depth;
    uint32_t label_count;
    uint32_t count;
    uint32_t opening;
    uint32_t closing;
    uint8_t reason;
    // What it reads: the parser's tree and tokens, the reader's output (as the names' resolution and the reader's controls'
    // pass leave it, or the program's after the pipeline) and the types.
    const Parser* parser;
    const CheckInput* input;
    const TypeTable* types;
    // By a control's token: its expression's root there plus one, 0 for none, UINT32_MAX for several.
    const uint32_t* roots;
    // What the machine with no node of the walk's kept of each root (whether a condition's value is a constant is read
    // there), or null where nothing is known of a root.
    const NodeFrontTypes* kept;
};

static void node_twin_fail(NodeTwin* twin, NodeTwinReason reason)
{
    twin->reason = twin->reason == NODE_TWIN_ATTEMPTED ? (uint8_t)reason : twin->reason;
}

static void node_twin_push(NodeTwin* twin, NodeTwinTask task)
{
    if (twin->depth < twin->capacity)
    {
        twin->tasks[twin->depth++] = task;
    }
    else
    {
        node_twin_fail(twin, NODE_TWIN_ROOM);
    }
}

static uint32_t node_twin_label(NodeTwin* twin)
{
    const uint32_t label = twin->label_count;
    if (label < twin->capacity)
    {
        twin->labels[label] = BUSTER_CHECK_NONE;
        twin->label_count += 1;
    }
    else
    {
        node_twin_fail(twin, NODE_TWIN_ROOM);
    }
    return label < twin->capacity ? label : 0;
}

static void node_twin_mark(NodeTwin* twin, uint32_t label)
{
    node_twin_push(twin, (NodeTwinTask){ .kind = BUSTER_NODE_TWIN_TASK_LABEL, .token = label });
}

static void node_twin_entry(NodeTwin* twin, CheckControlKind kind, uint32_t token, bool valued, uint32_t target)
{
    node_twin_push(twin, (NodeTwinTask){ .kind = BUSTER_NODE_TWIN_TASK_ENTRY, .token = token, .entry = { .token = token, .target = target, .kind = (uint8_t)kind, .valued = valued } });
}

static void node_twin_statement(NodeTwin* twin, uint32_t token, uint32_t stop, uint32_t repeat)
{
    if (token != BUSTER_CHECK_NONE)
    {
        node_twin_push(twin, (NodeTwinTask){ .kind = BUSTER_NODE_TWIN_TASK_STATEMENT, .token = token, .stop = stop, .repeat = repeat });
    }
}

static void node_twin_token(NodeTwin* twin, uint32_t token)
{
    node_twin_push(twin, (NodeTwinTask){ .kind = BUSTER_NODE_TWIN_TASK_TOKEN, .token = token });
}

// Whether the control at `token` has an expression: one root before the walk; several fail the body.
static bool node_twin_valued(NodeTwin* twin, uint32_t token)
{
    const uint32_t slot = token < twin->parser->tokens.count ? twin->roots[token] : 0;
    if (slot == UINT32_MAX)
    {
        node_twin_fail(twin, NODE_TWIN_SHAPE);
    }
    return slot != 0 && slot != UINT32_MAX;
}

// One statement's controls (check_control_expand's).
static void node_twin_expand(NodeTwin* twin, NodeTwinTask task)
{
    const Parser* parser = twin->parser;
    const CheckInput* input = twin->input;
    const uint32_t token = task.token;
    const uint8_t role = parser->tree.roles[token];
    if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO)
    {
        for (uint32_t child = twin->last[token - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->previous[child - twin->opening])
        {
            if (parser->tree.roles[child] != PARSE_ROLE_OPEN_BODY && parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
            {
                node_twin_statement(twin, child, task.stop, task.repeat);
            }
        }
    }
    else if (role == PARSE_ROLE_CLOSE_CONDITION)
    {
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t alternate = BUSTER_CHECK_NONE;
        uint32_t keyword = BUSTER_CHECK_NONE;
        for (uint32_t child = twin->first[token - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->next[child - twin->opening])
        {
            if (child < token && parser->tree.roles[child] == PARSE_ROLE_LEAF_RIGHT)
            {
                keyword = child;
            }
            else if (child > token)
            {
                if (parser->tree.roles[child] == PARSE_ROLE_ELSE)
                {
                    alternate = child;
                }
                else
                {
                    body = child;
                }
            }
        }
        const uint32_t end = node_twin_label(twin);
        const bool valued = node_twin_valued(twin, token);
        if (keyword == BUSTER_CHECK_NONE || !valued || body == BUSTER_CHECK_NONE)
        {
            node_twin_fail(twin, NODE_TWIN_SHAPE);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_IF)
        {
            const uint32_t otherwise = alternate != BUSTER_CHECK_NONE ? node_twin_label(twin) : end;
            node_twin_mark(twin, end);
            if (alternate != BUSTER_CHECK_NONE)
            {
                node_twin_statement(twin, alternate, task.stop, task.repeat);
                node_twin_mark(twin, otherwise);
                node_twin_entry(twin, CHECK_CONTROL_JUMP, token, false, end);
            }
            node_twin_statement(twin, body, task.stop, task.repeat);
            node_twin_entry(twin, CHECK_CONTROL_BRANCH_FALSE, token, true, otherwise);
        }
        else if (parser->tokens.kinds[keyword] == PARSE_KIND_WHILE)
        {
            const uint32_t start = node_twin_label(twin);
            node_twin_mark(twin, end);
            node_twin_entry(twin, CHECK_CONTROL_JUMP, token, false, start);
            node_twin_statement(twin, body, end, start);
            node_twin_entry(twin, CHECK_CONTROL_BRANCH_FALSE, token, true, end);
            node_twin_mark(twin, start);
        }
        else
        {
            node_twin_fail(twin, parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH ? NODE_TWIN_SWITCH : NODE_TWIN_SHAPE);
        }
    }
    else if (role == PARSE_ROLE_CLOSE_FOR)
    {
        uint32_t first = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        for (uint32_t child = twin->first[token - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->next[child - twin->opening])
        {
            if (child < token && parser->tokens.kinds[child] == PARSE_KIND_SEMI)
            {
                if (first == BUSTER_CHECK_NONE)
                {
                    first = child;
                }
                else
                {
                    condition = child;
                }
            }
            else if (child > token)
            {
                body = child;
            }
        }
        const uint32_t start = node_twin_label(twin);
        const uint32_t update = node_twin_label(twin);
        const uint32_t end = node_twin_label(twin);
        if (first == BUSTER_CHECK_NONE || condition == BUSTER_CHECK_NONE || body == BUSTER_CHECK_NONE)
        {
            node_twin_fail(twin, NODE_TWIN_SHAPE);
        }
        else
        {
            node_twin_mark(twin, end);
            node_twin_entry(twin, CHECK_CONTROL_JUMP, token, false, start);
            node_twin_token(twin, token);
            node_twin_mark(twin, update);
            node_twin_statement(twin, body, end, update);
            if (node_twin_valued(twin, condition))
            {
                node_twin_entry(twin, CHECK_CONTROL_BRANCH_FALSE, condition, true, end);
            }
            node_twin_mark(twin, start);
            node_twin_token(twin, first);
        }
    }
    else if (role == PARSE_ROLE_TERMINATOR)
    {
        uint32_t jump = BUSTER_CHECK_NONE;
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t condition = BUSTER_CHECK_NONE;
        for (uint32_t child = twin->first[token - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->next[child - twin->opening])
        {
            const uint8_t child_role = parser->tree.roles[child];
            if (child_role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER)
            {
                // A statement's attributes: a musttail return's among them.
                node_twin_fail(twin, NODE_TWIN_JUMP);
            }
            jump = child_role == PARSE_ROLE_RETURN ? child : jump;
            body = child_role == PARSE_ROLE_DO ? child : body;
            condition = child_role == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
        }
        if (jump != BUSTER_CHECK_NONE)
        {
            const uint8_t kind = parser->tokens.kinds[jump];
            if (kind == PARSE_KIND_RETURN && input->musttail_count != 0 && check_control_musttail(input, jump))
            {
                node_twin_fail(twin, NODE_TWIN_JUMP);
            }
            else if (kind == PARSE_KIND_RETURN)
            {
                node_twin_entry(twin, CHECK_CONTROL_RETURN, jump, node_twin_valued(twin, jump), BUSTER_CHECK_NONE);
            }
            else if ((kind == PARSE_KIND_BREAK && task.stop != BUSTER_CHECK_NONE) || (kind == PARSE_KIND_CONTINUE && task.repeat != BUSTER_CHECK_NONE))
            {
                node_twin_entry(twin, CHECK_CONTROL_JUMP, jump, false, kind == PARSE_KIND_BREAK ? task.stop : task.repeat);
            }
            else if (kind == PARSE_KIND_GOTO && !check_input_indirect_goto(parser, jump))
            {
                const uint32_t target = check_sparse_get(input->label_targets, jump);
                const bool named = target > twin->opening && target < twin->closing && parser->tree.roles[target] == PARSE_ROLE_LABEL;
                if (named)
                {
                    node_twin_entry(twin, CHECK_CONTROL_JUMP, jump, false, twin->source_labels[target - twin->opening]);
                }
                else
                {
                    node_twin_fail(twin, NODE_TWIN_JUMP);
                }
            }
            else
            {
                node_twin_fail(twin, NODE_TWIN_JUMP);
            }
        }
        else if (body != BUSTER_CHECK_NONE && condition != BUSTER_CHECK_NONE)
        {
            const uint32_t start = node_twin_label(twin);
            const uint32_t test = node_twin_label(twin);
            const uint32_t end = node_twin_label(twin);
            const bool valued = node_twin_valued(twin, condition);
            if (!valued)
            {
                node_twin_fail(twin, NODE_TWIN_SHAPE);
            }
            node_twin_mark(twin, end);
            node_twin_entry(twin, CHECK_CONTROL_BRANCH_TRUE, condition, true, start);
            node_twin_mark(twin, test);
            node_twin_statement(twin, body, end, test);
            node_twin_mark(twin, start);
        }
        else
        {
            node_twin_token(twin, token);
        }
    }
    else if (role == PARSE_ROLE_LABEL)
    {
        for (uint32_t child = twin->last[token - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->previous[child - twin->opening])
        {
            if (child > token)
            {
                node_twin_statement(twin, child, task.stop, task.repeat);
            }
        }
        node_twin_mark(twin, twin->source_labels[token - twin->opening]);
    }
    else if (role == PARSE_ROLE_CLOSE_CASE)
    {
        node_twin_fail(twin, NODE_TWIN_SWITCH);
    }
}

// What the twin leaves out of the body whose opening brace is `opening` by its tokens and the reader's columns alone,
// before any of it is read: its reason (NodeTwin.reason, where it had none).
static void node_twin_gates(NodeTwin* twin, uint32_t opening)
{
    const Parser* parser = twin->parser;
    const CheckInput* input = twin->input;
    const uint32_t closing = parser->tree.parents[opening];
    // What the twin leaves out, by the tokens and the reader's columns.
    for (uint32_t token = opening + 1; token < closing && twin->reason == NODE_TWIN_ATTEMPTED; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const bool declares = input->declarations[token] == token;
        if (kind == PARSE_KIND_SWITCH)
        {
            node_twin_fail(twin, NODE_TWIN_SWITCH);
        }
        else if (kind == PARSE_KIND_ASM)
        {
            node_twin_fail(twin, NODE_TWIN_ASM);
        }
        else if (declares && (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC) || check_input_linkage(input->declaration_bits, token) != 0))
        {
            node_twin_fail(twin, NODE_TWIN_STATIC);
        }
        else if (check_sparse_present(input->evaluation_nodes) && check_sparse_get(input->evaluation_nodes, token) != BUSTER_CHECK_NONE)
        {
            node_twin_fail(twin, NODE_TWIN_VARIABLE);
        }
    }
    // A variably modified declaration of the body's, or of its function's parameters (a prototype's, or an old-style
    // definition's declarations before the brace): the function's entry evaluates a parameter's bounds, in controls the
    // program makes before the body's first.
    const uint32_t function = check_sparse_get(input->body_declarations, opening);
    const uint32_t declared_from = function < opening ? function : opening;
    for (uint32_t v = 0; v < input->vla_count && twin->reason == NODE_TWIN_ATTEMPTED; v += 1)
    {
        if (input->vlas[v].declaration > declared_from && input->vlas[v].declaration < closing)
        {
            node_twin_fail(twin, NODE_TWIN_VARIABLE);
        }
    }
    for (uint32_t c = 0; c < input->cleanup_count && twin->reason == NODE_TWIN_ATTEMPTED; c += 1)
    {
        if (input->cleanups[c].declaration > opening && input->cleanups[c].declaration < closing)
        {
            node_twin_fail(twin, NODE_TWIN_CLEANUP);
        }
    }
    for (uint32_t r = 0; r < input->region_count && twin->reason == NODE_TWIN_ATTEMPTED; r += 1)
    {
        if (input->regions[r].opening > opening && input->regions[r].opening < closing)
        {
            node_twin_fail(twin, NODE_TWIN_REGION);
        }
    }
}

// Each token's children in the body whose opening brace is `opening` (check_control_children's): its first and its
// last, and each one's siblings.
static void node_twin_children(NodeTwin* twin, uint32_t opening)
{
    const uint32_t* parents = twin->parser->tree.parents;
    const uint32_t closing = parents[opening];
    // (A slot past the body's last token, for the stores that have no place.)
    const uint32_t sink = closing + 1;
    const uint64_t extent = (uint64_t)closing - opening + 1;
    twin->opening = opening;
    twin->closing = closing;
    memset(twin->first, 0xff, extent * sizeof(uint32_t));
    memset(twin->last, 0xff, extent * sizeof(uint32_t));
    for (uint32_t token = opening; token <= closing; token += 1)
    {
        const uint32_t parent = parents[token];
        const bool inside = parent - opening <= closing - opening;
        const uint32_t sibling = inside ? twin->last[parent - opening] : BUSTER_CHECK_NONE;
        twin->previous[token - opening] = sibling;
        twin->next[token - opening] = BUSTER_CHECK_NONE;
        twin->next[(inside && sibling != BUSTER_CHECK_NONE ? sibling : sink) - opening] = token;
        twin->first[(inside && sibling == BUSTER_CHECK_NONE ? parent : sink) - opening] = token;
        twin->last[(inside ? parent : sink) - opening] = token;
    }
}

// Whether the program may leave a statement of the body out because no control reaches it (#2987,
// c_check_program_reach.c: a statement that follows a return, a break, a continue or a goto, an `if` whose arms both end
// so, a `while` that is constantly true or a `for` without a condition, and that holds no label). The body's children
// are made (node_twin_children). The program walks each block in order and keeps a statement that control reaches or
// that holds a label; it reads a condition's truth and a loop's own breaks from its nodes. This asks more, by the
// tokens and the kept roots' facts: after a statement control may not leave for the next (a jump; an `if` with an else
// whose arms both are such; a `while` whose condition's root is a constant, whatever breaks it; a `for` without a
// condition; a block or a labelled statement that ends so), the next statement of the block is a labelled one, which
// control reaches by its label. Where that holds for every block, the program has nothing to leave out; where it does
// not, the body is refused, a few the program would have kept whole among them.
static bool node_twin_unreached(NodeTwin* twin)
{
    const Parser* parser = twin->parser;
    const uint8_t* roles = parser->tree.roles;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t opening = twin->opening;
    NodeTwinReach* stack = twin->reach;
    uint32_t depth = 0;
    bool unreached = false;
    // The statement just walked: whether control leaves it for the next.
    bool completes = true;
    stack[depth++] = (NodeTwinReach){ .token = twin->closing, .step = BUSTER_NODE_TWIN_REACH_STATEMENT };
    while (depth != 0 && !unreached)
    {
        NodeTwinReach* frame = stack + depth - 1;
        const uint32_t token = frame->token;
        const uint8_t role = roles[token];
        // The statement to walk next, below this frame; none where the frame is done or goes on by itself.
        uint32_t below = BUSTER_CHECK_NONE;
        bool done = false;
        if (frame->step == BUSTER_NODE_TWIN_REACH_STATEMENT)
        {
            if (role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_ELSE || role == PARSE_ROLE_DO || role == PARSE_ROLE_LABEL)
            {
                // A sequence: control reaches its first statement (a labelled statement's by its label).
                frame->step = BUSTER_NODE_TWIN_REACH_SEQUENCE;
                frame->reaches = true;
                frame->child = twin->first[token - opening];
                completes = true;
            }
            else if (role == PARSE_ROLE_TERMINATOR)
            {
                bool jump = false;
                uint32_t body = BUSTER_CHECK_NONE;
                for (uint32_t child = twin->first[token - opening]; child != BUSTER_CHECK_NONE; child = twin->next[child - opening])
                {
                    jump = jump || roles[child] == PARSE_ROLE_RETURN || roles[child] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
                    body = roles[child] == PARSE_ROLE_DO ? child : body;
                }
                if (body != BUSTER_CHECK_NONE && !jump)
                {
                    // A do statement: control leaves it, as the program has it, and its body is walked for its own.
                    frame->step = BUSTER_NODE_TWIN_REACH_LOOP;
                    frame->reaches = true;
                    below = body;
                }
                else
                {
                    completes = !jump;
                    done = true;
                }
            }
            else if (role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR)
            {
                uint32_t keyword = BUSTER_CHECK_NONE;
                uint32_t body = BUSTER_CHECK_NONE;
                uint32_t alternate = BUSTER_CHECK_NONE;
                uint32_t semicolons = 0;
                uint32_t condition = BUSTER_CHECK_NONE;
                for (uint32_t child = twin->first[token - opening]; child != BUSTER_CHECK_NONE; child = twin->next[child - opening])
                {
                    if (child < token && roles[child] == PARSE_ROLE_LEAF_RIGHT)
                    {
                        keyword = child;
                    }
                    else if (child < token && kinds[child] == PARSE_KIND_SEMI)
                    {
                        semicolons += 1;
                        condition = semicolons == 2 ? child : condition;
                    }
                    else if (child > token && roles[child] == PARSE_ROLE_ELSE)
                    {
                        alternate = child;
                    }
                    else if (child > token)
                    {
                        body = child;
                    }
                }
                // The condition's root, as the machine kept it: whether a `while` has a constant for its condition (one
                // it did not keep may be one), and whether a `for` has a condition at all.
                const uint32_t at = role == PARSE_ROLE_CLOSE_FOR ? condition : token;
                const uint32_t slot = at < parser->tokens.count ? twin->roots[at] : 0;
                const NodeFrontTypes* kept = twin->kept;
                const bool known = kept != NULL && slot != 0 && slot != UINT32_MAX && slot - 1 < kept->node_capacity && kept->side_label_firsts != NULL &&
                                   kept->side_label_firsts[slot - 1] != UINT32_MAX;
                const bool constant = slot != 0 && kept != NULL && (!known || (kept->side_facts[slot - 1] & BUSTER_NODE_FRONT_SIDE_CONSTANT) != 0);
                const bool conditional = role == PARSE_ROLE_CLOSE_CONDITION && keyword != BUSTER_CHECK_NONE && kinds[keyword] == PARSE_KIND_IF;
                const bool loop = role == PARSE_ROLE_CLOSE_FOR || (keyword != BUSTER_CHECK_NONE && kinds[keyword] == PARSE_KIND_WHILE);
                if (body != BUSTER_CHECK_NONE && conditional)
                {
                    frame->step = BUSTER_NODE_TWIN_REACH_THEN;
                    frame->child = alternate;
                    below = body;
                }
                else if (body != BUSTER_CHECK_NONE && loop)
                {
                    frame->step = BUSTER_NODE_TWIN_REACH_LOOP;
                    frame->reaches = role == PARSE_ROLE_CLOSE_FOR ? slot != 0 : !constant;
                    below = body;
                }
                else
                {
                    // A switch, or a shape the builder refuses.
                    completes = true;
                    done = true;
                }
            }
            else
            {
                completes = true;
                done = true;
            }
        }
        else if (frame->step == BUSTER_NODE_TWIN_REACH_SEQUENCE)
        {
            // The statement before, if any, is walked: whether control reaches the next one.
            frame->reaches = frame->reaches && completes;
            uint32_t child = frame->child;
            while (child != BUSTER_CHECK_NONE && (roles[child] == PARSE_ROLE_OPEN_BODY || roles[child] == PARSE_ROLE_OPEN_BLOCK || (role == PARSE_ROLE_LABEL && child < token)))
            {
                child = twin->next[child - opening];
            }
            if (child == BUSTER_CHECK_NONE)
            {
                completes = frame->reaches;
                done = true;
            }
            else if (!frame->reaches && roles[child] != PARSE_ROLE_LABEL)
            {
                unreached = true;
            }
            else
            {
                // (Control reaches a labelled statement by its label.)
                frame->reaches = true;
                frame->child = twin->next[child - opening];
                below = child;
            }
        }
        else if (frame->step == BUSTER_NODE_TWIN_REACH_THEN)
        {
            if (frame->child == BUSTER_CHECK_NONE)
            {
                // No else: control goes on past the `if`.
                completes = true;
                done = true;
            }
            else
            {
                frame->reaches = completes;
                frame->step = BUSTER_NODE_TWIN_REACH_ELSE;
                below = frame->child;
            }
        }
        else if (frame->step == BUSTER_NODE_TWIN_REACH_ELSE)
        {
            completes = frame->reaches || completes;
            done = true;
        }
        else
        {
            completes = frame->reaches;
            done = true;
        }
        if (done)
        {
            depth -= 1;
        }
        else if (below != BUSTER_CHECK_NONE && depth < twin->capacity)
        {
            stack[depth++] = (NodeTwinReach){ .token = below, .step = BUSTER_NODE_TWIN_REACH_STATEMENT };
        }
        else if (below != BUSTER_CHECK_NONE)
        {
            unreached = true;
        }
    }
    return unreached;
}

// The controls of the body whose opening brace is `opening`, from its tokens (check_control_build's): whether the twin
// made them (its reason says why not).
static bool node_twin_build(NodeTwin* twin, uint32_t opening)
{
    const Parser* parser = twin->parser;
    const CheckInput* input = twin->input;
    const uint32_t* parents = parser->tree.parents;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t closing = parents[opening];
    twin->opening = opening;
    twin->closing = closing;
    twin->depth = 0;
    twin->label_count = 0;
    twin->count = 0;
    node_twin_gates(twin, opening);
    if (twin->reason == NODE_TWIN_ATTEMPTED)
    {
        // Each token's children; then its label's number, and each terminator's declarations with an initializer, in
        // their order: a declaration's plan is its initializer's first token's nearest terminator's.
        const uint64_t extent = (uint64_t)closing - opening + 1;
        node_twin_children(twin, opening);
        if (node_twin_unreached(twin))
        {
            node_twin_fail(twin, NODE_TWIN_REACH);
        }
        memset(twin->plan_first, 0xff, extent * sizeof(uint32_t));
        memset(twin->plan_last, 0xff, extent * sizeof(uint32_t));
        for (uint32_t token = opening + 1; token < closing; token += 1)
        {
            if (roles[token] == PARSE_ROLE_LABEL || roles[token] == PARSE_ROLE_CLOSE_CASE)
            {
                twin->source_labels[token - twin->opening] = node_twin_label(twin);
            }
            const uint32_t begin = input->declarations[token] == token && check_sparse_present(input->initializer_begins) ? check_sparse_get(input->initializer_begins, token) :
                                                                                                                             BUSTER_CHECK_NONE;
            if (begin > opening && begin < closing)
            {
                uint32_t terminator = begin;
                while (terminator > opening && terminator < closing && roles[terminator] != PARSE_ROLE_TERMINATOR)
                {
                    terminator = parents[terminator];
                }
                if (terminator > opening && terminator < closing)
                {
                    twin->plan_next[token - twin->opening] = BUSTER_CHECK_NONE;
                    if (twin->plan_last[terminator - twin->opening] != BUSTER_CHECK_NONE)
                    {
                        twin->plan_next[twin->plan_last[terminator - twin->opening] - twin->opening] = token;
                    }
                    else
                    {
                        twin->plan_first[terminator - twin->opening] = token;
                    }
                    twin->plan_last[terminator - twin->opening] = token;
                }
                else
                {
                    node_twin_fail(twin, NODE_TWIN_SHAPE);
                }
            }
        }
        for (uint32_t child = twin->last[closing - twin->opening]; child != BUSTER_CHECK_NONE; child = twin->previous[child - twin->opening])
        {
            if (child > opening)
            {
                node_twin_statement(twin, child, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            }
        }
        // Its tasks, then its END at its closing brace (check_control_drain's and check_control_end's).
        for (uint32_t round = 0; round < 2 && twin->reason == NODE_TWIN_ATTEMPTED; round += 1)
        {
            while (twin->depth != 0 && twin->reason == NODE_TWIN_ATTEMPTED)
            {
                const NodeTwinTask task = twin->tasks[--twin->depth];
                if (task.kind == BUSTER_NODE_TWIN_TASK_LABEL)
                {
                    twin->labels[task.token] = twin->count;
                }
                else if (task.kind == BUSTER_NODE_TWIN_TASK_ENTRY || task.kind == BUSTER_NODE_TWIN_TASK_TOKEN)
                {
                    // A token's declarations with an initializer, each an INITIALIZE unless it has static storage; or,
                    // with none, its one expression's EVALUATE.
                    const bool entry = task.kind == BUSTER_NODE_TWIN_TASK_ENTRY;
                    const uint32_t plan = entry ? BUSTER_CHECK_NONE : twin->plan_first[task.token - twin->opening];
                    uint32_t wrote = entry ? 1 : 0;
                    for (uint32_t declaration = plan; declaration != BUSTER_CHECK_NONE; declaration = twin->plan_next[declaration - twin->opening])
                    {
                        wrote += 1;
                    }
                    wrote += !entry && plan == BUSTER_CHECK_NONE && node_twin_valued(twin, task.token);
                    if (wrote > twin->capacity - twin->count)
                    {
                        node_twin_fail(twin, NODE_TWIN_ROOM);
                    }
                    else if (entry)
                    {
                        twin->entries[twin->count++] = task.entry;
                    }
                    else if (plan != BUSTER_CHECK_NONE)
                    {
                        for (uint32_t declaration = plan; declaration != BUSTER_CHECK_NONE; declaration = twin->plan_next[declaration - twin->opening])
                        {
                            twin->entries[twin->count++] = (NodeTwinControl){ .kind = CHECK_CONTROL_INITIALIZE, .token = declaration, .target = BUSTER_CHECK_NONE, .valued = false };
                        }
                    }
                    else if (wrote != 0)
                    {
                        twin->entries[twin->count++] = (NodeTwinControl){ .kind = CHECK_CONTROL_EVALUATE, .token = task.token, .target = BUSTER_CHECK_NONE, .valued = true };
                    }
                }
                else
                {
                    node_twin_expand(twin, task);
                }
            }
            if (round == 0)
            {
                node_twin_entry(twin, CHECK_CONTROL_END, closing, false, BUSTER_CHECK_NONE);
            }
        }
        // Each branch's and each jump's target, from its label to its control.
        for (uint32_t i = 0; i < twin->count && twin->reason == NODE_TWIN_ATTEMPTED; i += 1)
        {
            NodeTwinControl* entry = twin->entries + i;
            if (entry->kind >= CHECK_CONTROL_BRANCH_FALSE)
            {
                if (entry->target >= twin->label_count || twin->labels[entry->target] == BUSTER_CHECK_NONE)
                {
                    node_twin_fail(twin, NODE_TWIN_SHAPE);
                }
                else
                {
                    entry->target = twin->labels[entry->target];
                }
            }
        }
    }
    return twin->reason == NODE_TWIN_ATTEMPTED;
}

// Whether the program's return flow says nothing of the body of the function `declaration` declares (check_program_return_flow: a function with a result that
// may not return one, a noreturn function that may return), by the twin's controls alone: the function is main; or it
// has no result and is not noreturn; or its END is not reached from its first control with every branch taken both
// ways. The program's walk folds the branches it knows and stops at an expression that never returns, so what it
// reaches is among what this one reaches: where END is out of reach here it is there, and the program reports nothing.
static bool node_twin_silent(NodeTwin* twin, uint32_t declaration, bool is_main)
{
    const TypeTable* types = twin->types;
    const CheckInput* input = twin->input;
    const TypeId result = type_conversion_operand(types, type_return(types, input->declared_types[declaration]));
    const bool noreturn = input->function_flags != NULL && (input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0;
    bool silent = is_main || (!noreturn && (result == 0 || type_kind(types, result) == BUSTER_TYPE_KIND_VOID));
    if (!silent && twin->count != 0)
    {
        memset(twin->seen, 0, twin->count);
        uint32_t head = 0;
        uint32_t tail = 1;
        twin->queue[0] = 0;
        twin->seen[0] = 1;
        while (head < tail)
        {
            const uint32_t index = twin->queue[head++];
            const NodeTwinControl* entry = twin->entries + index;
            const bool jump = entry->kind == CHECK_CONTROL_JUMP;
            const uint32_t targets[2] = { entry->kind >= CHECK_CONTROL_BRANCH_FALSE ? entry->target : BUSTER_CHECK_NONE,
                                          !jump && entry->kind != CHECK_CONTROL_RETURN && entry->kind != CHECK_CONTROL_END ? index + 1 : BUSTER_CHECK_NONE };
            for (uint32_t t = 0; t < 2; t += 1)
            {
                if (targets[t] < twin->count && twin->seen[targets[t]] == 0)
                {
                    twin->seen[targets[t]] = 1;
                    twin->queue[tail++] = targets[t];
                }
            }
        }
        silent = twin->seen[twin->count - 1] == 0;
    }
    return silent;
}

// Why the producer does not take a body (NodeFrontShadow.produced_reasons), in the order it asks; 0 for one it takes.
typedef enum NodeProducerReason
{
    NODE_PRODUCER_TAKEN,
    // The unit's own (BUSTER_NODE_PRODUCER_UNIT_*): a warning option or a diagnostic pragma (the passes after the walk
    // say more of a body under other warnings than the producer proves absent), a symbol's pragma, a target or an
    // interrupt attribute, a report before the walk.
    NODE_PRODUCER_UNIT,
    // A pass before the walk has said something at one of its tokens: a report, or a warning's event.
    NODE_PRODUCER_SAID,
    // The scan of its tokens finds one the front would lose a root at (node_alone_scan).
    NODE_PRODUCER_SCAN,
    // A token of it, by its kind or its role alone: a `switch`, a `case` or a `default`, an `asm`, a _Static_assert, a
    // typeof, a braced initializer; a null statement that is a condition's or a for's whole body; an else whose `if` is
    // another condition's whole statement.
    NODE_PRODUCER_TOKEN,
    // What the controls' twin leaves out by the tokens and the reader's columns (node_twin_gates; the detail is its
    // reason): a block's static or extern declaration, a variable-length array, a cleanup, a statement expression.
    NODE_PRODUCER_GATE,
    // Its function: no type, parameters without a prototype, a result that is neither void nor a scalar of a class I8
    // to F64.
    NODE_PRODUCER_FUNCTION,
    // After the reading. The machine does not take every root of it (the detail is its verdict of the body).
    NODE_PRODUCER_MACHINE,
    // The twin does not make its controls (the detail is its reason).
    NODE_PRODUCER_CONTROLS,
    // A control with an expression whose root the machine did not keep.
    NODE_PRODUCER_UNKEPT,
    // An initialization that is not a scalar's of one value, or of a declaration with an assembler label, or of an
    // object aligned past 8 bytes.
    NODE_PRODUCER_INITIALIZER,
    // A return without a value in a function with a result, or with one in a function without.
    NODE_PRODUCER_RETURN,
    // An `if` whose condition's root is a constant: the program may fold it.
    NODE_PRODUCER_CONSTANT,
    // The return flow's silence is not proved (node_twin_silent).
    NODE_PRODUCER_FLOW,
    // A root whose records may have a warning that neither T's rules nor the census's tests stand for before the walk
    // (node_machine_root_loud): two enumerations meeting, or a conditional's constant arm that its conversion changes.
    NODE_PRODUCER_LOUD,
    NODE_PRODUCER_ROOM,
    NODE_PRODUCER_REASONS,
} NodeProducerReason;

static const char* const node_producer_reason_names[NODE_PRODUCER_REASONS] = {
    "taken", "its-unit", "something-said-of-it-before-the-walk", "the-scan", "a-token's-kind", "a-gate-of-the-controls'", "its-function", "a-root-the-machine-does-not-take",
    "controls-the-twin-does-not-make", "a-control's-root-not-kept", "an-initialization-of-another-form", "a-return-of-another-form", "a-condition-that-is-a-constant",
    "its-return-flow-not-proved-silent", "a-root-that-may-be-warned-of", "no-room",
};

#define BUSTER_NODE_PRODUCER_UNIT_WARNINGS 1u
#define BUSTER_NODE_PRODUCER_UNIT_PRAGMAS 2u
#define BUSTER_NODE_PRODUCER_UNIT_SYMBOLS 3u
#define BUSTER_NODE_PRODUCER_UNIT_ATTRIBUTES 4u
#define BUSTER_NODE_PRODUCER_UNIT_REPORTS 5u
// A member marked deprecated or unavailable, or one with an attribute of the counted_by family: a use of it is warned of
// or refused after the walk (check_input_member_uses, the counted_by family's checks).
#define BUSTER_NODE_PRODUCER_UNIT_MEMBERS 6u
// A const, pure, warn_unused_result or nodiscard attribute on what is no function's declaration (a type's, an object's):
// a discarded call's result is then warned of by its type or its callee's (c_check_ignored_result.c), which the census's
// test of a callee's declaration does not read.
#define BUSTER_NODE_PRODUCER_UNIT_RESULTS 7u
#define BUSTER_NODE_PRODUCER_UNITS 8u

static const char* const node_producer_unit_names[BUSTER_NODE_PRODUCER_UNITS] = {
    "none", "a-warning-option", "a-diagnostic-pragma", "a-symbol's-pragma", "a-function's-attribute-of-its-code", "an-error-before-the-walk",
    "a-marked-or-a-counted-member", "a-result's-attribute-on-no-function",
};

struct NodeProducer
{
    NodeTwin twin;
    // The tokens a pass before the walk has said something at (a report's, a warning's event's), in order.
    uint32_t* said;
    uint32_t said_count;
    // A bit a token: a function's first declaration (its object's number less one, CheckInput.objects) that the unit
    // defines. A body that names a function with internal linkage that has no bit is not taken: the unit's end warns of
    // the function, with a note at its first use.
    uint64_t* defined;
    // The unit's enumerations' definitions as the tags' columns have them before the walk (NodeMachine.reader_enums).
    CheckInputEnumDefinition* enums;
    uint32_t enum_count;
    // The unit's own reason (BUSTER_NODE_PRODUCER_UNIT_*), 0 where it has none.
    uint8_t unit;
    bool lean;
};

static bool node_census_aggregate(const TypeTable* types, TypeId type);
static uint8_t node_census_kind(const TypeTable* types, TypeId type);
static bool node_census_labelled(const CheckInput* input, const Parser* parser, uint32_t declaration);

// The producer's state for a unit, made where the machine with no node of the walk's is (node_alone_begin): the twin's
// columns for the unit's largest body, the tokens something was said at, the unit's own reason, and the verdicts' and
// the kept controls' room for `bodies` bodies. Null without room.
static struct NodeProducer* node_producer_begin(struct NodeAlone* alone, CheckInputReader* reader, uint32_t bodies)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    NodeFrontShadow* shadow = alone->shadow;
    // The largest body's tokens: the twin's columns and its tasks have a place a token of the body at hand, and two more.
    uint32_t largest = 0;
    uint64_t inside = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const uint32_t closing = parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY ? parser->tree.parents[token] : count;
        const uint32_t extent = closing < count && closing > token ? closing - token + 1 : 0;
        largest = extent > largest ? extent : largest;
        inside += extent;
    }
    const uint64_t room = (uint64_t)largest + 2;
    // What has been said so far: the reader's reports and its warnings' events, and the diagnostics published already
    // (the preprocessor's, the parser's, and what the reader published as it resolved the names).
    uint64_t published = 0;
    for (const PPDiagnosticRecord* record = reader->pp->diagnostic_first; record != NULL; record = record->next)
    {
        published += 1;
    }
    const uint64_t said_room = (uint64_t)input->report_count + reader->warning_events.count + published + 1;
    struct NodeProducer* producer = arena_push_array(reader->scratch, 1, sizeof(struct NodeProducer), _Alignof(struct NodeProducer));
    bool valid = producer != NULL;
    if (valid)
    {
        memset(producer, 0, sizeof(*producer));
        NodeTwin* twin = &producer->twin;
        twin->capacity = (uint32_t)room;
        uint32_t** columns[] = { &twin->first, &twin->last, &twin->next, &twin->previous, &twin->source_labels, &twin->plan_first, &twin->plan_last, &twin->plan_next, &twin->labels,
                                 &twin->queue };
        for (uint32_t c = 0; valid && c < BUSTER_ARRAY_LENGTH(columns); c += 1)
        {
            *columns[c] = arena_push_array(reader->scratch, room, sizeof(uint32_t), _Alignof(uint32_t));
            valid = *columns[c] != NULL;
        }
        twin->tasks = valid ? arena_push_array(reader->scratch, room, sizeof(NodeTwinTask), _Alignof(NodeTwinTask)) : NULL;
        twin->reach = valid ? arena_push_array(reader->scratch, room, sizeof(NodeTwinReach), _Alignof(NodeTwinReach)) : NULL;
        twin->entries = valid ? arena_push_array(reader->scratch, room, sizeof(NodeTwinControl), _Alignof(NodeTwinControl)) : NULL;
        twin->seen = valid ? arena_push_array(reader->scratch, room, sizeof(uint8_t), 1) : NULL;
        producer->said = valid ? arena_push_array(reader->scratch, said_room, sizeof(uint32_t), _Alignof(uint32_t)) : NULL;
        producer->defined = valid ? arena_push_array(reader->scratch, (uint64_t)count / 64 + 2, sizeof(uint64_t), _Alignof(uint64_t)) : NULL;
        // A control a token of a body at most: the kept controls' room is the bodies' tokens.
        shadow->produced_reasons = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint8_t), 1);
        shadow->produced_details = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint8_t), 1);
        shadow->produced_firsts = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
        shadow->produced_counts = arena_push_array(reader->arena, (uint64_t)bodies + 1, sizeof(uint32_t), _Alignof(uint32_t));
        shadow->produced_controls = arena_push_array(reader->arena, inside + 1, sizeof(NodeTwinControl), _Alignof(NodeTwinControl));
        shadow->produced_control_capacity = inside < UINT32_MAX ? (uint32_t)inside : UINT32_MAX;
        shadow->produced_control_count = 0;
        valid = valid && twin->tasks != NULL && twin->reach != NULL && twin->entries != NULL && twin->seen != NULL && producer->said != NULL && producer->defined != NULL &&
                shadow->produced_reasons != NULL &&
                shadow->produced_details != NULL && shadow->produced_firsts != NULL && shadow->produced_counts != NULL && shadow->produced_controls != NULL;
    }
    if (valid)
    {
        NodeTwin* twin = &producer->twin;
        twin->parser = parser;
        twin->input = input;
        twin->types = reader->types;
        twin->roots = shadow->alone_control_roots;
        twin->kept = alone->types;
        producer->lean = (reader->pp->options.node_reader & BUSTER_NODE_READER_PRODUCE_LEAN) != 0;
        // What the passes before the walk have said, by its token's place in the preprocessor's output (which a published
        // diagnostic's token is, and whose order the parser's tokens keep, a view's too), in order: the reports are few
        // and the events are nearly in order, an insertion each.
        const PPDiagnosticRecord* record = reader->pp->diagnostic_first;
        for (uint32_t source = 0; source < 3; source += 1)
        {
            const uint64_t said = source == 0 ? input->report_count : source == 1 ? reader->warning_events.count : published;
            for (uint64_t i = 0; i < said; i += 1)
            {
                const uint32_t own = source == 0 ? input->reports[i].token : source == 1 ? reader->warning_events.events[i].token : 0;
                uint32_t token = source < 2 ? (own < count ? parse_source_index(&parser->tokens, own) : UINT32_MAX) :
                                 record->token != 0 ? record->token - 1 : (uint32_t)(record->position / PP_DIAGNOSTIC_PHASE_COUNT);
                record = source == 2 ? record->next : record;
                uint32_t at = producer->said_count;
                while (at != 0 && producer->said[at - 1] > token)
                {
                    producer->said[at] = producer->said[at - 1];
                    at -= 1;
                }
                producer->said[at] = token;
                producer->said_count += 1;
            }
        }
        // The functions the unit defines, by their first declarations.
        memset(producer->defined, 0, ((uint64_t)count / 64 + 2) * sizeof(uint64_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t declaration = parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY ? check_sparse_get(input->body_declarations, token) : BUSTER_CHECK_NONE;
            const uint32_t object = declaration < input->token_count && input->objects != NULL ? input->objects[declaration] : 0;
            if (object != 0 && object <= count)
            {
                producer->defined[(object - 1) / 64] |= UINT64_C(1) << ((object - 1) % 64);
            }
        }
        // The unit's enumerations' definitions (check_input_enum_records' list, which the reader makes at its end): one
        // pass counts them and one writes them.
        for (uint32_t pass = 0; valid && pass < 2; pass += 1)
        {
            uint32_t at = 0;
            for (uint32_t token = 0; token < count; token += 1)
            {
                const uint32_t brace = parser->tokens.kinds[token] == PARSE_KIND_ENUM ? check_input_enum_opening(reader, token) : BUSTER_CHECK_NONE;
                if (brace != BUSTER_CHECK_NONE)
                {
                    if (pass == 1)
                    {
                        producer->enums[at] = (CheckInputEnumDefinition){ .type = reader->tag_types[token], .open = brace, .close = parser->tree.parents[brace] };
                    }
                    at += 1;
                }
            }
            if (pass == 0)
            {
                producer->enums = arena_push_array(reader->scratch, (uint64_t)at + 1, sizeof(CheckInputEnumDefinition), _Alignof(CheckInputEnumDefinition));
                producer->enum_count = at;
                valid = producer->enums != NULL;
            }
        }
        // And whether a member of a record has an attribute of the counted_by family, by the types' members' flags.
        bool counted_member = false;
        for (uint32_t member = 1; member < reader->types->members.count; member += 1)
        {
            counted_member |= (reader->types->members.flags[member] & BUSTER_TYPE_MEMBER_COUNT_MASK) != 0;
        }
        // The unit's own reason. Of its attributes, by the tokens that have some: a function's whose code they change or
        // choose (a target, its clones, an interrupt's, a processor's dispatch, internal linkage); and a result's on what is
        // no function's declaration.
        bool attributed = false;
        bool results = false;
        const uint32_t attributed_end = count < input->token_count ? count : input->token_count;
        for (uint32_t first = check_input_attributed_next(input->attributes, 0, attributed_end); first < attributed_end;
             first = check_input_attributed_next(input->attributes, first + 16, attributed_end))
        {
            for (uint32_t selected = check_input_attributed_group(input->attributes, first, attributed_end); selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
                const bool function = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && input->declaration_kinds != NULL && input->declaration_kinds[token] == C_BINDING_FUNCTION;
                results |= attributes != NULL && attributes->results && !function;
                for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
                {
                    const uint32_t kind = attributes->records[i].kind;
                    attributed |= kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES || kind == CHECK_ATTRIBUTE_INTERRUPT || kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                                  kind == CHECK_ATTRIBUTE_CPU_DISPATCH || kind == CHECK_ATTRIBUTE_INTERNAL_LINKAGE;
                }
            }
        }
        // (An error the reader recovers from is moved off its reports so that it reads on, CheckInput.recovered_reports: the
        // unit fails all the same.)
        producer->unit = input->report_count != 0 || input->recovered_count != 0 || parser->errors != 0 || reader->pp->errors != 0 ? BUSTER_NODE_PRODUCER_UNIT_REPORTS :
                         reader->pp->options.semantic_warning_option_count != 0 ? BUSTER_NODE_PRODUCER_UNIT_WARNINGS :
                         input->diagnostic_pragmas != NULL && input->diagnostic_pragmas->count != 0 ? BUSTER_NODE_PRODUCER_UNIT_PRAGMAS :
                         input->symbol_pragmas != NULL && input->symbol_pragmas->count != 0 ? BUSTER_NODE_PRODUCER_UNIT_SYMBOLS :
                         attributed ? BUSTER_NODE_PRODUCER_UNIT_ATTRIBUTES :
                         counted_member || input->marked_member_count != 0 ? BUSTER_NODE_PRODUCER_UNIT_MEMBERS :
                         results ? BUSTER_NODE_PRODUCER_UNIT_RESULTS : 0;
        shadow->produced_unit = producer->unit;
        shadow->produced_ran = true;
        shadow->produced_reports_before = input->report_count;
        shadow->produced_events_before = reader->warning_events.count;
        shadow->produced_reports_walked = UINT32_MAX;
        shadow->produced_events_walked = UINT32_MAX;
    }
    return valid ? producer : NULL;
}

// The unit's enumerations' definitions for the machine `machine` (NodeMachine.reader_enums).
static void node_producer_enums(const struct NodeProducer* producer, NodeMachine* machine)
{
    machine->reader_enums = producer->enums;
    machine->reader_enum_count = producer->enum_count;
}

// What the producer asks of the body whose opening brace is `opening` before any of it is read: its reason not to take
// it (NODE_PRODUCER_TAKEN where it has none yet), with what the reason was found at in *detail. *scan is what the scan of
// its tokens found (1: a token the front would lose a root at; 0: none), or -1 where an earlier reason left it unrun; it
// adds the tokens it read to *read.
static uint8_t node_producer_before(struct NodeProducer* producer, const NodeMachineReader* typer, const CheckInputReader* reader, uint32_t opening, uint8_t* detail, int* scan,
                                    uint64_t* read)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const uint32_t closing = parser->tree.parents[opening];
    uint8_t reason = NODE_PRODUCER_TAKEN;
    *detail = 0;
    if (producer->unit != 0)
    {
        reason = NODE_PRODUCER_UNIT;
        *detail = producer->unit;
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        // The first token something was said at that is not before the body: whether it is in it. (By the tokens' places
        // in the preprocessor's output.)
        const uint32_t first = parse_source_index(&parser->tokens, opening);
        const uint32_t last = parse_source_index(&parser->tokens, closing);
        uint32_t low = 0;
        uint32_t high = producer->said_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = producer->said[middle] < first;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        reason = low < producer->said_count && producer->said[low] <= last ? NODE_PRODUCER_SAID : reason;
    }
    *scan = -1;
    if (reason == NODE_PRODUCER_TAKEN)
    {
        *scan = node_alone_scan(reader, opening, read);
        reason = *scan != 0 ? NODE_PRODUCER_SCAN : reason;
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        // A pragma in the body, whatever it is: one the compiler ignores is warned of where it stands, after the walk.
        // (The parser keeps each pragma with the token it comes before.)
        uint32_t low = 0;
        uint32_t high = parser->tokens.pragma_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = parser->tokens.pragma_positions[middle] <= opening;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        if (low < parser->tokens.pragma_count && parser->tokens.pragma_positions[low] <= closing)
        {
            reason = NODE_PRODUCER_TOKEN;
            *detail = PARSE_KIND_UNKNOWN;
        }
    }
    for (uint32_t token = opening + 1; reason == NODE_PRODUCER_TAKEN && token < closing && token < count; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        if (kind == PARSE_KIND_SWITCH || kind == PARSE_KIND_CASE || kind == PARSE_KIND_DEFAULT || kind == PARSE_KIND_ASM || kind == PARSE_KIND_STATIC_ASSERT ||
            kind == PARSE_KIND_TYPEOF)
        {
            reason = NODE_PRODUCER_TOKEN;
            *detail = kind;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && token < input->token_count && input->declarations[token] < input->token_count &&
                 check_input_bit(input->declaration_bits, input->declarations[token], BUSTER_CHECK_BITS_THREAD_LOCAL))
        {
            // A thread-local object, named here: in position-independent code its entry is a call of its accessor, which
            // the lowering makes as it writes the entry (check_lower_tls), a row and a temporary among the expression's own
            // that a kept tape does not have.
            reason = NODE_PRODUCER_TOKEN;
            *detail = PARSE_KIND_QUALIFIER;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && token < input->token_count && input->declarations[token] < input->token_count &&
                 input->declaration_kinds[input->declarations[token]] == C_BINDING_FUNCTION &&
                 check_input_linkage(input->declaration_bits, input->declarations[token]) == 1)
        {
            // A function with internal linkage that the unit does not define, named here: the unit's end warns of it, with
            // a note at its first use.
            const uint32_t object = input->objects[input->declarations[token]];
            const bool defined = object != 0 && object <= count && (producer->defined[(object - 1) / 64] >> ((object - 1) % 64) & 1) != 0;
            if (!defined)
            {
                reason = NODE_PRODUCER_TOKEN;
                *detail = PARSE_KIND_STORAGE;
            }
        }
        else if (role == PARSE_ROLE_OPEN_INITIALIZER)
        {
            reason = NODE_PRODUCER_TOKEN;
            *detail = PARSE_KIND_L_BRACE;
        }
        else if (kind == PARSE_KIND_SEMI && role == PARSE_ROLE_TERMINATOR && parser->tree.parents[token] == token - 1 &&
                 (parser->tree.roles[token - 1] == PARSE_ROLE_CLOSE_CONDITION || parser->tree.roles[token - 1] == PARSE_ROLE_CLOSE_FOR))
        {
            // A null statement for an `if`'s, a loop's or a `switch`'s whole body, which the common warnings' pass after the
            // reader may warn of (check_common_empty_bodies, by the lines the tokens are spelled on).
            reason = NODE_PRODUCER_TOKEN;
            *detail = PARSE_KIND_SEMI;
        }
        else if (role == PARSE_ROLE_ELSE && parser->tree.parents[token] < count && parser->tree.roles[parser->tree.parents[token]] == PARSE_ROLE_CLOSE_CONDITION &&
                 parser->tree.parents[parser->tree.parents[token]] < count &&
                 parser->tree.roles[parser->tree.parents[parser->tree.parents[token]]] == PARSE_ROLE_CLOSE_CONDITION)
        {
            // An else whose `if` is another condition's whole statement, which the same pass may warn of
            // (check_common_dangling_else, where the outer one is an `if` without an else of its own).
            reason = NODE_PRODUCER_TOKEN;
            *detail = PARSE_KIND_ELSE;
        }
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        NodeTwin* twin = &producer->twin;
        twin->reason = NODE_TWIN_ATTEMPTED;
        node_twin_gates(twin, opening);
        if (twin->reason != NODE_TWIN_ATTEMPTED)
        {
            reason = NODE_PRODUCER_GATE;
            *detail = twin->reason;
        }
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        // Its function, as the bodies' comparison asks it of the lowered one (node_bodies_begin): a prototype or no
        // parameter, and a result that is void or a scalar's.
        const uint32_t declaration = check_sparse_get(input->body_declarations, opening);
        const TypeId type = declaration < input->token_count ? input->declared_types[declaration] : 0;
        const bool function = type != 0 && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_FUNCTION;
        const TypeId result = function ? type_return(types, type) : 0;
        const bool none = result == BUSTER_TYPE_VOID;
        const uint8_t class = function && !none ? node_machine_class(&typer->machine, result) : BUSTER_TYPED_CLASS_VOID;
        const bool unprototyped = function && (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 &&
                                  check_sparse_get(input->parameters, opening) != BUSTER_CHECK_NONE;
        if (!function || unprototyped || (!none && (class < BUSTER_TYPED_CLASS_I8 || class > BUSTER_TYPED_CLASS_F64)))
        {
            reason = NODE_PRODUCER_FUNCTION;
            *detail = !function ? 1 : unprototyped ? 2 : 3;
        }
    }
    return reason;
}

// What it asks after the machine has read the body (`verdict`, its verdict of it): its controls from the tokens, each
// control's expression's root kept, the forms of its initializations and its returns, no constant `if`, and the return
// flow's silence. `reason` is what it had before the reading. Returns the reason, 0 for a body it takes, whose controls
// are then in the twin's entries.
static uint8_t node_producer_after(struct NodeProducer* producer, const struct NodeAlone* alone, const CheckInputReader* reader, uint32_t opening, uint8_t reason, uint8_t verdict,
                                   uint8_t* detail)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const TypeTable* types = reader->types;
    const NodeFrontTypes* kept = alone->types;
    const uint32_t* roots = alone->shadow->alone_control_roots;
    const uint32_t count = parser->tokens.count;
    const uint32_t closing = parser->tree.parents[opening];
    NodeTwin* twin = &producer->twin;
    if (reason == NODE_PRODUCER_TAKEN && verdict != BUSTER_NODE_ALONE_BODY_WHOLE)
    {
        reason = NODE_PRODUCER_MACHINE;
        *detail = verdict;
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        twin->reason = NODE_TWIN_ATTEMPTED;
        if (!node_twin_build(twin, opening))
        {
            reason = NODE_PRODUCER_CONTROLS;
            *detail = twin->reason;
        }
    }
    const uint32_t declaration = reason == NODE_PRODUCER_TAKEN ? check_sparse_get(input->body_declarations, opening) : BUSTER_CHECK_NONE;
    const bool valued = declaration < input->token_count && type_return(types, input->declared_types[declaration]) != BUSTER_TYPE_VOID;
    for (uint32_t i = 0; reason == NODE_PRODUCER_TAKEN && i < twin->count; i += 1)
    {
        const NodeTwinControl* entry = twin->entries + i;
        const bool initialization = entry->kind == CHECK_CONTROL_INITIALIZE;
        if (entry->valued || initialization)
        {
            // Its expression's root: taken, swept, and kept with what is beside its tape.
            const uint32_t slot = entry->token < count ? roots[entry->token] : 0;
            const uint32_t root = slot - 1;
            const bool found = slot != 0 && slot != UINT32_MAX && root < kept->node_capacity && (kept->refusals[root] & BUSTER_NODE_FRONT_TYPED_SWEPT) != 0 &&
                               kept->sweep_refusals[root] == NODE_MACHINE_TAKEN && kept->tape_counts[root] != UINT32_MAX && kept->side_label_firsts[root] != UINT32_MAX;
            reason = !found ? NODE_PRODUCER_UNKEPT : (kept->side_facts[root] & BUSTER_NODE_FRONT_SIDE_LOUD) != 0 ? NODE_PRODUCER_LOUD : reason;
        }
        if (reason == NODE_PRODUCER_TAKEN && initialization)
        {
            // A scalar's, of one value that converts to the declared type as a direct assignment's source does without a
            // diagnostic: the walk checks an initialization's conversion at its token, which no root holds.
            const TypeId declared = entry->token < input->token_count ? input->declared_types[entry->token] : 0;
            const bool scalar = declared != 0 && !node_census_aggregate(types, declared) && !type_kind_is_array(node_census_kind(types, declared));
            const uint32_t root = roots[entry->token] - 1;
            const bool converts = (kept->side_facts[root] & BUSTER_NODE_FRONT_SIDE_CONVERTS) != 0;
            // And of an object whose local the lowering makes a plain one of its class (check_lower_part_body's own test of a
            // declaration): one aligned past 8 bytes, by its attribute or by its type, is a frame object, which its
            // initialization writes through its address.
            const CheckInputAttributes* attributes = scalar ? check_input_attributes_at(input, entry->token) : NULL;
            const bool plain = scalar && (attributes == NULL || attributes->alignment_log2 == 0) && type_alignment_log2(types, declared) <= 3;
            reason = scalar && converts && plain && !node_census_labelled(input, parser, entry->token) ? reason : NODE_PRODUCER_INITIALIZER;
            *detail = scalar && !converts ? 1 : scalar && !plain ? 2 : 0;
        }
        if (reason == NODE_PRODUCER_TAKEN && entry->kind == CHECK_CONTROL_RETURN && entry->valued != valued)
        {
            reason = NODE_PRODUCER_RETURN;
        }
    }
    // An `if` whose condition's root is a constant, by the tokens: the program's controls have no such condition where it
    // folded it.
    for (uint32_t token = opening + 1; reason == NODE_PRODUCER_TAKEN && token < closing && token < count; token += 1)
    {
        const uint32_t close = parser->tree.parents[token];
        if (parser->tokens.kinds[token] == PARSE_KIND_IF && close < count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CONDITION)
        {
            const uint32_t slot = roots[close];
            const bool constant = slot != 0 && slot != UINT32_MAX && slot - 1 < kept->node_capacity && kept->side_label_firsts[slot - 1] != UINT32_MAX &&
                                  (kept->side_facts[slot - 1] & BUSTER_NODE_FRONT_SIDE_CONSTANT) != 0;
            reason = constant ? NODE_PRODUCER_CONSTANT : reason;
        }
    }
    if (reason == NODE_PRODUCER_TAKEN)
    {
        // main, where the unit is hosted: the reader's own test of it (check_input_resolve_range's).
        const IdentId name = parser->tokens.names[declaration];
        const bool is_main = !reader->pp->options.freestanding && intern_length(reader->pp->table, name) == 4 && memcmp(intern_bytes(reader->pp->table, name), "main", 4) == 0;
        reason = node_twin_silent(twin, declaration, is_main) ? reason : NODE_PRODUCER_FLOW;
    }
    return reason;
}

// The producer's verdict of the body at `slot` of the machine's list, kept: its reason, and a taken body's controls.
static void node_producer_keep(struct NodeProducer* producer, NodeFrontShadow* shadow, uint32_t slot, uint8_t reason, uint8_t detail)
{
    const NodeTwin* twin = &producer->twin;
    if (reason == NODE_PRODUCER_TAKEN && twin->count > shadow->produced_control_capacity - shadow->produced_control_count)
    {
        reason = NODE_PRODUCER_ROOM;
    }
    shadow->produced_reasons[slot] = reason;
    shadow->produced_details[slot] = detail;
    shadow->produced_firsts[slot] = shadow->produced_control_count;
    shadow->produced_counts[slot] = reason == NODE_PRODUCER_TAKEN ? twin->count : 0;
    if (reason == NODE_PRODUCER_TAKEN)
    {
        memcpy(shadow->produced_controls + shadow->produced_control_count, twin->entries, (uint64_t)twin->count * sizeof(NodeTwinControl));
        shadow->produced_control_count += twin->count;
    }
}

// --node-bodies (BUSTER_NODE_READER_BODIES): the bodies the producer took, masked before the walk's first chunk. Each
// one's tokens, its braces among them, take the lazily deferred bodies' mask (CheckInput.deferred_tokens, which every
// phase after the reader's walk skips), no class of the walk's (CheckInputReader.visits) and the reader's own mark
// (CheckInputReader.ignored); and the input says whose mask it is (CheckInput.node_bodies). A unit with a mask already,
// or without room, takes none: nothing is then masked, and every body is read as ever.
static void node_bodies_mask(CheckInputReader* reader, NodeFrontShadow* shadow)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t modulus = reader->pp->options.node_bodies_modulus;
    const uint32_t residue = reader->pp->options.node_bodies_residue;
    uint32_t taken = 0;
    for (uint32_t b = 0; shadow->produced_reasons != NULL && b < shadow->alone_body_count; b += 1)
    {
        taken += shadow->produced_reasons[b] == NODE_PRODUCER_TAKEN;
    }
    uint8_t* mask = taken != 0 && input->deferred_tokens == NULL && !input->resource ? arena_push_array(reader->arena, (uint64_t)count + 1, 1, 64) : NULL;
    if (mask != NULL)
    {
        memset(mask, 0, (uint64_t)count + 1);
        uint32_t place = 0;
        for (uint32_t b = 0; b < shadow->alone_body_count; b += 1)
        {
            const uint32_t opening = shadow->alone_body_openings[b];
            const uint32_t closing = opening < count ? parser->tree.parents[opening] : count;
            const bool produced = shadow->produced_reasons[b] == NODE_PRODUCER_TAKEN && closing < count && closing > opening;
            // (A test's selection, PPOptions.node_bodies_modulus: some of the producer's bodies, the others read as ever.)
            const bool selected = produced && (modulus == 0 || place % modulus == residue);
            place += produced;
            if (selected)
            {
                const uint64_t length = (uint64_t)closing - opening + 1;
                memset(mask + opening, 1, length);
                memset(reader->visits + opening, 0, length);
                memset(reader->ignored + opening, 1, length);
            }
        }
        input->deferred_tokens = mask;
        input->node_bodies = true;
    }
}

// The producer's verdicts against every diagnostic of a unit (#2850's P1, against the diagnostics): with
// BUSTER_TEST_NODE_BODIES_LOG set to a path, the driver runs the producer beside each unit's ordinary read, nothing
// masked, and each diagnostic at a token of a body the producer takes is a line of that file, with a line a unit for
// what was looked at. A taken body may have no diagnostic of any stage: every phase after the mask would skip it. The
// tokens are the original parse's (parse_origin), which the diagnostics' positions are in.
struct NodeBodiesCheck
{
    const char* log;
    const char* path;
    // The taken bodies' opening and closing tokens, in their order: by the original parse's tokens, and by their places in
    // the preprocessor's output, which a published diagnostic's token is.
    uint32_t* openings;
    uint32_t* closings;
    uint32_t* output_openings;
    uint32_t* output_closings;
    uint32_t count;
    uint32_t capacity;
    // The bodies the producer had a verdict of, the diagnostics looked at, and those in a taken body.
    uint32_t bodies;
    uint32_t looked;
    uint32_t found;
    // The reader's reports and its warnings' events: as the producer decided, as the walk ended, and at the reader's end.
    uint32_t reports[3];
    uint32_t events[3];
    uint8_t unit;
    bool read;
};

static _Thread_local struct NodeBodiesCheck node_bodies_check;

// Whether the unit is checked so: the variable's path, read once a unit.
static bool node_bodies_check_begin(const char* path)
{
    struct NodeBodiesCheck* check = &node_bodies_check;
    const char* log = getenv("BUSTER_TEST_NODE_BODIES_LOG");
    check->log = log != NULL && log[0] != 0 ? log : NULL;
    check->path = path;
    check->count = 0;
    check->bodies = 0;
    check->looked = 0;
    check->found = 0;
    check->read = false;
    check->unit = 0;
    memset(check->reports, 0, sizeof(check->reports));
    memset(check->events, 0, sizeof(check->events));
    node_bodies_checking = check->log != NULL;
    return check->log != NULL;
}

// The producer's verdicts of the unit's bodies, as the reader's output has them: the taken ones' tokens kept.
static void node_bodies_check_read(const CheckInput* input, const Parser* parser)
{
    struct NodeBodiesCheck* check = &node_bodies_check;
    const NodeFrontShadow* report = check->log != NULL && !check->read && input != NULL ? input->node_front : NULL;
    if (report != NULL && report->produced_reasons != NULL)
    {
        check->read = true;
        check->bodies = report->alone_body_count;
        check->unit = report->produced_unit;
        check->reports[0] = report->produced_reports_before;
        check->reports[1] = report->produced_reports_walked;
        check->reports[2] = input->report_count;
        check->events[0] = report->produced_events_before;
        check->events[1] = report->produced_events_walked;
        check->events[2] = input->warning_events.count;
        if (report->alone_body_count > check->capacity)
        {
            free(check->openings);
            free(check->closings);
            free(check->output_openings);
            free(check->output_closings);
            check->capacity = report->alone_body_count;
            check->openings = malloc((size_t)check->capacity * sizeof(uint32_t));
            check->closings = malloc((size_t)check->capacity * sizeof(uint32_t));
            check->output_openings = malloc((size_t)check->capacity * sizeof(uint32_t));
            check->output_closings = malloc((size_t)check->capacity * sizeof(uint32_t));
        }
        const bool room = check->openings != NULL && check->closings != NULL && check->output_openings != NULL && check->output_closings != NULL;
        for (uint32_t b = 0; b < report->alone_body_count && room; b += 1)
        {
            const uint32_t opening = report->alone_body_openings[b];
            if (report->produced_reasons[b] == NODE_PRODUCER_TAKEN && opening < parser->tokens.count)
            {
                check->openings[check->count] = parse_origin(parser, opening);
                check->closings[check->count] = parse_origin(parser, parser->tree.parents[opening]);
                check->output_openings[check->count] = parse_source_index(&parser->tokens, opening);
                check->output_closings[check->count] = parse_source_index(&parser->tokens, parser->tree.parents[opening]);
                check->count += 1;
            }
        }
        // The reader's own reports and its warnings' events at a token of a taken body, each with when it was said: before
        // the producer decided (which refuses the body, so none may be), by the walk, or by the reader's passes after it.
        for (uint32_t source = 0; source < 2; source += 1)
        {
            const uint32_t said = source == 0 ? input->report_count : input->warning_events.count;
            const uint32_t before = source == 0 ? report->produced_reports_before : report->produced_events_before;
            const uint32_t walked = source == 0 ? report->produced_reports_walked : report->produced_events_walked;
            for (uint32_t i = 0; i < said; i += 1)
            {
                const uint32_t token = source == 0 ? input->reports[i].token : input->warning_events.events[i].token;
                const uint32_t message = source == 0 ? (uint32_t)input->reports[i].message : input->warning_events.events[i].id;
                const char* text = source == 0 && message < BUSTER_ARRAY_LENGTH(check_input_messages) ? check_input_messages[message] : NULL;
                char numbered[32];
                snprintf(numbered, sizeof(numbered), "id %u", message);
                if (token < parser->tokens.count)
                {
                    node_bodies_check_token(parse_origin(parser, token),
                                            i < before ? (source == 0 ? "a-report-before-the-producer" : "an-event-before-the-producer") :
                                            i < walked ? (source == 0 ? "a-report-of-the-walk's" : "an-event-of-the-walk's") :
                                                         (source == 0 ? "a-report-after-the-walk" : "an-event-after-the-walk"),
                                            text != NULL ? text : numbered);
                }
            }
        }
    }
}

// A diagnostic at `token` (`what` names where it was found, `message` is its text or null): a line of the log where a
// taken body holds the token. `token` is the original parse's, or with `output` its place in the preprocessor's output.
static void node_bodies_check_place(uint32_t token, bool output, const char* what, const char* message)
{
    struct NodeBodiesCheck* check = &node_bodies_check;
    if (check->log != NULL && check->read)
    {
        const uint32_t* openings = output ? check->output_openings : check->openings;
        const uint32_t* closings = output ? check->output_closings : check->closings;
        uint32_t low = 0;
        uint32_t high = check->count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = closings[middle] < token;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        check->looked += 1;
        if (low < check->count && openings[low] <= token)
        {
            check->found += 1;
            FILE* file = fopen(check->log, "a");
            if (file != NULL)
            {
                fprintf(file, "IN-A-TAKEN-BODY unit=%s %s=%u body=%u..%u from=%s message=%.120s\n", check->path != NULL ? check->path : "?", output ? "output" : "token", token,
                        openings[low], closings[low], what, message != NULL ? message : "-");
                fclose(file);
            }
        }
    }
}

static void node_bodies_check_token(uint32_t token, const char* what, const char* message)
{
    node_bodies_check_place(token, false, what, message);
}

// The unit's end: each published diagnostic's position, and the unit's line.
static void node_bodies_check_end(const Preprocessor* pp)
{
    struct NodeBodiesCheck* check = &node_bodies_check;
    if (check->log != NULL)
    {
        for (const PPDiagnosticRecord* record = pp->diagnostic_first; record != NULL && check->read; record = record->next)
        {
            // The token it is at; or, for one at a place of its own, where it is published. (A declaration's diagnostic may be
            // published where its definition ends, in its body or past it: its token says what it is about.)
            if (record->token != 0)
            {
                node_bodies_check_place(record->token - 1, true, "a-published-diagnostic's-token", record->message);
            }
            else
            {
                node_bodies_check_place((uint32_t)(record->position / PP_DIAGNOSTIC_PHASE_COUNT), true, "a-published-diagnostic's-position", record->message);
            }
        }
        FILE* file = fopen(check->log, "a");
        if (file != NULL)
        {
            fprintf(file, "unit=%s producer-ran=%d bodies=%u taken=%u diagnostics-looked-at=%u in-a-taken-body=%u its-unit's-reason=%u reports=%u,%u,%u events=%u,%u,%u\n",
                    check->path != NULL ? check->path : "?", check->read, check->bodies, check->count, check->looked, check->found, check->unit, check->reports[0],
                    check->read ? check->reports[1] : 0, check->reports[2], check->events[0], check->read ? check->events[1] : 0, check->events[2]);
            fclose(file);
        }
        // A unit with such a diagnostic is kept beside the log (the tests' units are temporary files), named by its bytes'
        // hash and its own name: each distinct unit once.
        const char* name = check->found != 0 && check->path != NULL ? strrchr(check->path, '/') : NULL;
        FILE* source = check->found != 0 && check->path != NULL ? fopen(check->path, "rb") : NULL;
        char block[4096];
        uint64_t hash = UINT64_C(0xcbf29ce484222325);
        for (size_t read = source != NULL ? fread(block, 1, sizeof(block), source) : 0; read != 0; read = fread(block, 1, sizeof(block), source))
        {
            for (size_t i = 0; i < read; i += 1)
            {
                hash = (hash ^ (uint8_t)block[i]) * UINT64_C(0x100000001b3);
            }
        }
        char kept[1024];
        const int length = source != NULL ? snprintf(kept, sizeof(kept), "%s.units/%016" PRIx64 "-%s", check->log, hash, name != NULL ? name + 1 : check->path) : 0;
        FILE* copy = length > 0 && (size_t)length < sizeof(kept) && access(kept, F_OK) != 0 && fseek(source, 0, SEEK_SET) == 0 ? fopen(kept, "wb") : NULL;
        for (size_t read = copy != NULL ? fread(block, 1, sizeof(block), source) : 0; read != 0; read = fread(block, 1, sizeof(block), source))
        {
            (void)fwrite(block, 1, read, copy);
        }
        if (copy != NULL)
        {
            fclose(copy);
        }
        if (source != NULL)
        {
            fclose(source);
        }
        check->log = NULL;
        node_bodies_checking = false;
    }
}

#endif
