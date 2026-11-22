// A function body's controls from its tokens, without a node of the walk's (#2850; docs/node-machine.md, "The same
// comparison before the walk"): what a producer of a body's lowered function has to make where the program's builder
// reads the walk's nodes. No compile takes it: measure-node-machine --shadow --bodies makes them after the pipeline and
// compares them with the program's (c_check_node_census.c).
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
typedef struct NodeTwinControl NodeTwinControl;
struct NodeTwinControl
{
    uint32_t token;
    uint32_t target;
    uint8_t kind;
    bool valued;
};

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
    for (uint32_t v = 0; v < input->vla_count && twin->reason == NODE_TWIN_ATTEMPTED; v += 1)
    {
        if (input->vlas[v].declaration > opening && input->vlas[v].declaration < closing)
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

#endif
