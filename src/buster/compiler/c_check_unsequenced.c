// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// -Wunsequenced (#1953): clang's SequenceChecker (SemaChecking.cpp, llvmorg-23.1.4) over a full expression's graph
// nodes, for C. Two modifications of a variable, or a modification and a read of it, that no sequence point orders warn
// once per variable: "multiple unsequenced modifications to 'x'" and "unsequenced modification and access to 'x'", at the
// earlier modification's operator (or, for a modification after a read, at the modification's), on by default.
//
// Each full expression is a sequence of regions, a tree: a comma's, a logical operator's and a conditional's operands
// each take a region of their own, sequenced with respect to their siblings', and merge into their parent's once the
// operator is read (check_unsequenced_region_*). Every other operator's operands share their parent's region, so their
// accesses are unsequenced: an assignment's and an increment's operands too, as C11 sequences only the store after the
// operands' value computations. Each variable keeps its last read, its last modification whose value is complete (a
// sequenced operand's, a call's argument's) and its last modification as a side effect, with their regions; each
// access is tested against the kinds it conflicts with (notePreUse, notePostUse, notePreMod, notePostMod). A sequenced
// operand's side effects complete before what follows it (clang's SequencedSubexpression): when it ends, each becomes a
// complete modification and the side effect before it is restored. A logical operator's or a conditional's operand that
// a constant first operand skips isn't read (clang's EvaluationTracker). Clang's getObject names a variable only: a
// member or an element is none in C.
//
// Not read, as clang's ConstEvaluatedExprVisitor doesn't visit them: sizeof's, alignof's and the unevaluated builtins'
// operands, a static selection's other operands, and a GNU statement expression's statements, which are full
// expressions of their own; its value is one too, checked before the full expression that holds it, as clang completes
// it first. The walk is an explicit stack (no recursion): each frame is one of clang's visits, at a step.
//
// SIMD: the candidates come from one pass over the graph's operators, 16 at a time, for a modification; a full
// expression with none, nearly all of them, is never walked. The walk itself stays scalar: it is an ordered tree walk
// whose every step reads and updates the region tree's union-find and the variables' usages, which no lane order keeps.
// Included by c_check_common_warnings.c.

enum
{
    // The usage kinds: a read; a modification sequenced before the value computation of its expression; a modification
    // as a side effect, which isn't.
    CHECK_UNSEQ_USE = 0,
    CHECK_UNSEQ_MOD_VALUE = 1,
    CHECK_UNSEQ_MOD_EFFECT = 2,
    CHECK_UNSEQ_KINDS = 3,
};

// A usage: its node (BUSTER_CHECK_NONE for none) and its region.
typedef struct CheckUnseqUsage CheckUnseqUsage;
struct CheckUnseqUsage
{
    uint32_t node;
    uint32_t region;
};

// A variable's usages, by its object, with the token that named it first.
typedef struct CheckUnseqEntry CheckUnseqEntry;
struct CheckUnseqEntry
{
    uint32_t object;
    uint32_t name;
    CheckUnseqUsage uses[CHECK_UNSEQ_KINDS];
    bool diagnosed;
};

// A side effect a sequenced operand replaced, to restore when it ends.
typedef struct CheckUnseqSaved CheckUnseqSaved;
struct CheckUnseqSaved
{
    uint32_t entry;
    CheckUnseqUsage usage;
};

// A warning found, published once the full expression's walk ends.
typedef struct CheckUnseqReport CheckUnseqReport;
struct CheckUnseqReport
{
    uint32_t node;
    uint32_t name;
    bool mod_mod;
};

// A frame's visit: a read of a child (notePreUse, its visit, notePostUse), or a node's, by the mode its operator takes.
typedef enum CheckUnseqVisit
{
    CHECK_UNSEQ_READ,
    // A node's visit before its mode is chosen.
    CHECK_UNSEQ_VISIT,
    CHECK_UNSEQ_SKIP,
    CHECK_UNSEQ_CHOSEN,
    CHECK_UNSEQ_SEQUENCED,
    CHECK_UNSEQ_LOGICAL,
    CHECK_UNSEQ_ASSIGN,
    CHECK_UNSEQ_INCREMENT,
    CHECK_UNSEQ_CHILDREN,
} CheckUnseqVisit;

typedef struct CheckUnseqFrame CheckUnseqFrame;
struct CheckUnseqFrame
{
    uint32_t node;
    uint8_t visit;
    uint8_t step;
    // A logical operator's or a conditional's evaluation tracker (clang's EvaluationTracker): whether every evaluation
    // inside it succeeded, and the tracker around it (BUSTER_CHECK_NONE for none).
    bool eval_ok;
    uint32_t tracker;
    // The usage entry of the variable a read or a modification names, or BUSTER_CHECK_NONE.
    uint32_t entry;
    uint32_t old_region;
    uint32_t regions[3];
    // 0 where a call's frame opened a sequenced subexpression, else BUSTER_CHECK_NONE.
    uint32_t mark;
    // The previous sequenced operand's mark, which ends with this one.
    uint32_t outer_mark;
    uint32_t child;
};

typedef struct CheckUnseq CheckUnseq;
struct CheckUnseq
{
    const CheckGraph* graph;
    const CheckStream* stream;
    const uint8_t* marks;
    // The region tree: each region's parent and whether it was merged into it.
    uint32_t* parents;
    uint8_t* merged;
    uint32_t region_count;
    uint32_t region_capacity;
    uint32_t region;
    // The variables' usages, an open-addressing table of `table_mask + 1` slots of entry indices.
    CheckUnseqEntry* entries;
    uint32_t entry_count;
    uint32_t* table;
    uint32_t table_mask;
    CheckUnseqSaved* saved;
    uint32_t saved_count;
    // The innermost open sequenced operand's mark, or BUSTER_CHECK_NONE (clang's ModAsSideEffect pointer).
    uint32_t open_mark;
    // The innermost evaluation tracker's frame, or BUSTER_CHECK_NONE.
    uint32_t tracker;
    // The unit's evaluation facts: a node clang's CFG leaves unreachable, whose warning DiagRuntimeBehavior drops, is 0.
    const uint8_t* evaluated;
    CheckUnseqFrame* frames;
    uint32_t frame_count;
    uint32_t frame_capacity;
    CheckUnseqReport* reports;
    uint32_t report_count;
    // The first operands' truths clang's evaluation finds (check_unseq_known), by node: 0 not yet known, else
    // BUSTER_CHECK_UNSEQ_*; and the stack it takes, a node's room each.
    uint8_t* known;
    uint32_t* known_stack;
    bool failed;
};

// CheckUnseq.marks' bits: a node whose subtree holds a modification; a brace initializer's value, whose list is checked
// as one full expression with its plan, not as a root of its own.
#define BUSTER_CHECK_UNSEQ_MODIFIES 1u
#define BUSTER_CHECK_UNSEQ_PLANNED 2u

static bool check_unseq_modification(CheckOp op)
{
    const bool result = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
    return result;
}

static bool check_unseq_assignment(CheckOp op)
{
    const bool result = op == CHECK_OP_ASSIGN || (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_BIT_XOR_ASSIGN);
    return result;
}

// Whether `node`'s operands are unevaluated, as clang's visitor leaves them: sizeof's and alignof's (but a variable
// array's), and the builtins clang doesn't evaluate the arguments of (c_check_builtin_evidence.c's set).
static bool check_unseq_unevaluated(const CheckGraph* graph, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const CBuiltinId builtin = graph->members != NULL ? (CBuiltinId)graph->members[node] : (CBuiltinId)0;
    const bool result = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_BUILTIN_QUERY ||
                        (op == CHECK_OP_BUILTIN_FLOAT && cbuiltin_float_nan(builtin)) ||
                        (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_NONDETERMINISTIC_VALUE);
    return result;
}

// The region `index` stands for, its merged ancestors skipped, with their links shortened to it.
static uint32_t check_unseq_representative(CheckUnseq* checker, uint32_t index)
{
    uint32_t at = index;
    while (checker->merged[at] != 0)
    {
        at = checker->parents[at];
    }
    uint32_t walk = index;
    while (checker->merged[walk] != 0)
    {
        const uint32_t next = checker->parents[walk];
        checker->parents[walk] = at;
        walk = next;
    }
    return at;
}

// Whether a usage in region `old` and one in the current region `current` are unsequenced: `old` (as merged) is the
// current one's or one of its ancestors (clang's isUnsequenced).
static bool check_unseq_unsequenced(CheckUnseq* checker, uint32_t current, uint32_t old)
{
    uint32_t at = check_unseq_representative(checker, current);
    const uint32_t target = check_unseq_representative(checker, old);
    bool result = false;
    bool going = at >= target;
    // (A region's parent comes before it: `at` falls until it is the target or below it; the root is 0.)
    while (going)
    {
        result = at == target;
        going = !result && at != 0;
        at = going ? checker->parents[at] : at;
        going = going && at >= target;
    }
    return result;
}

static uint32_t check_unseq_allocate(CheckUnseq* checker, uint32_t parent)
{
    uint32_t result = 0;
    if (checker->region_count < checker->region_capacity)
    {
        result = checker->region_count;
        checker->parents[result] = parent;
        checker->merged[result] = 0;
        checker->region_count += 1;
    }
    else
    {
        checker->failed = true;
    }
    return result;
}

static void check_unseq_merge(CheckUnseq* checker, uint32_t region)
{
    checker->merged[region] = region != 0 ? 1u : 0u;
}

// The variable `node` names, through parentheses, casts and selections (clang's getObject): a modification's operand
// through a prefix increment's or an assignment's left side too, and a comma's right one. Its object, or 0; `*name` its
// identifier's token.
static uint32_t check_unseq_object(const CheckUnseq* checker, const Parser* parser, uint32_t node, bool modification, uint32_t* name)
{
    const CheckGraph* graph = checker->graph;
    uint32_t at = node;
    uint32_t result = 0;
    bool going = true;
    for (uint32_t steps = 0; going && at < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        const bool through = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT ||
                             (modification && (op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_PRE_DECREMENT || check_unseq_assignment(op)));
        const uint32_t token = graph->tokens[at];
        if (op == CHECK_OP_VALUE && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
            check_stream_value(checker->stream, at).category == CHECK_LVALUE)
        {
            result = check_stream_object(checker->stream, at);
            *name = token;
        }
        going = through || op == CHECK_OP_COMMA;
        at = op == CHECK_OP_COMMA ? graph->operand1[at] : graph->operand0[at];
    }
    return result;
}

// The usage entry of `object`, made where it has none, or BUSTER_CHECK_NONE for no object.
static uint32_t check_unseq_entry(CheckUnseq* checker, uint32_t object, uint32_t name)
{
    uint32_t result = BUSTER_CHECK_NONE;
    if (object != 0)
    {
        uint32_t slot = (object * 0x9e3779b1u) & checker->table_mask;
        while (checker->table[slot] != BUSTER_CHECK_NONE && checker->entries[checker->table[slot]].object != object)
        {
            slot = (slot + 1) & checker->table_mask;
        }
        if (checker->table[slot] == BUSTER_CHECK_NONE)
        {
            const uint32_t index = checker->entry_count;
            checker->entries[index] = (CheckUnseqEntry){
                .object = object,
                .name = name,
                .uses = { { BUSTER_CHECK_NONE, 0 }, { BUSTER_CHECK_NONE, 0 }, { BUSTER_CHECK_NONE, 0 } },
            };
            checker->entry_count += 1;
            checker->table[slot] = index;
        }
        result = checker->table[slot];
    }
    return result;
}

// clang's addUsage: `node` becomes the entry's usage of `kind` where it has none or the one it has is sequenced before
// it; a side effect it replaces inside a sequenced operand is saved, to restore when that operand ends.
static void check_unseq_add(CheckUnseq* checker, uint32_t entry, uint32_t node, uint32_t kind)
{
    CheckUnseqUsage* usage = &checker->entries[entry].uses[kind];
    if (usage->node == BUSTER_CHECK_NONE || !check_unseq_unsequenced(checker, checker->region, usage->region))
    {
        if (kind == CHECK_UNSEQ_MOD_EFFECT && checker->open_mark != BUSTER_CHECK_NONE)
        {
            checker->saved[checker->saved_count] = (CheckUnseqSaved){ .entry = entry, .usage = *usage };
            checker->saved_count += 1;
        }
        usage->node = node;
        usage->region = checker->region;
    }
}

// clang's checkUsage: whether the entry's usage of `other` and `node` are unsequenced, then the warning, at the
// modification's node, once per variable.
static void check_unseq_test(CheckUnseq* checker, uint32_t entry, uint32_t node, uint32_t other, bool mod_mod)
{
    CheckUnseqEntry* info = &checker->entries[entry];
    const CheckUnseqUsage usage = info->uses[other];
    if (!info->diagnosed && usage.node != BUSTER_CHECK_NONE && check_unseq_unsequenced(checker, checker->region, usage.region))
    {
        // (Diagnosed either way; DiagRuntimeBehavior drops it where either access is unreachable.)
        const bool reached = checker->evaluated == NULL || (checker->evaluated[usage.node] != 0 && checker->evaluated[node] != 0);
        if (reached)
        {
            checker->reports[checker->report_count] = (CheckUnseqReport){
                .node = other == CHECK_UNSEQ_USE ? node : usage.node,
                .name = info->name,
                .mod_mod = mod_mod,
            };
            checker->report_count += 1;
        }
        info->diagnosed = true;
    }
}

static void check_unseq_pre_use(CheckUnseq* checker, uint32_t entry, uint32_t node)
{
    check_unseq_test(checker, entry, node, CHECK_UNSEQ_MOD_VALUE, false);
}

static void check_unseq_post_use(CheckUnseq* checker, uint32_t entry, uint32_t node)
{
    check_unseq_test(checker, entry, node, CHECK_UNSEQ_MOD_EFFECT, false);
    check_unseq_add(checker, entry, node, CHECK_UNSEQ_USE);
}

static void check_unseq_pre_mod(CheckUnseq* checker, uint32_t entry, uint32_t node)
{
    check_unseq_test(checker, entry, node, CHECK_UNSEQ_MOD_VALUE, true);
    check_unseq_test(checker, entry, node, CHECK_UNSEQ_USE, false);
}

static void check_unseq_post_mod(CheckUnseq* checker, uint32_t entry, uint32_t node)
{
    check_unseq_test(checker, entry, node, CHECK_UNSEQ_MOD_EFFECT, true);
    check_unseq_add(checker, entry, node, CHECK_UNSEQ_MOD_EFFECT);
}

// A sequenced operand begins (clang's SequencedSubexpression): its side effects are saved from here. The mark it
// replaces is returned, to restore when it ends.
static uint32_t check_unseq_sequence_begin(CheckUnseq* checker)
{
    const uint32_t outer = checker->open_mark;
    checker->open_mark = checker->saved_count;
    return outer;
}

// It ends: each side effect it made, last first, becomes a complete modification and the one before it is restored.
static void check_unseq_sequence_end(CheckUnseq* checker, uint32_t outer)
{
    const uint32_t mark = checker->open_mark;
    for (uint32_t i = checker->saved_count; i-- > mark;)
    {
        CheckUnseqEntry* info = &checker->entries[checker->saved[i].entry];
        // (A complete modification is no side effect: nothing is saved.)
        check_unseq_add(checker, checker->saved[i].entry, info->uses[CHECK_UNSEQ_MOD_EFFECT].node, CHECK_UNSEQ_MOD_VALUE);
        info->uses[CHECK_UNSEQ_MOD_EFFECT] = checker->saved[i].usage;
    }
    checker->saved_count = mark;
    checker->open_mark = outer;
}

// The number of child slots `node` has: its three operands', then its arguments'.
static uint32_t check_unseq_slots(const CheckGraph* graph, uint32_t node)
{
    const uint32_t arguments = check_graph_has_arguments(graph->ops[node]) && graph->argument_counts != NULL ? graph->argument_counts[node] : 0;
    return 3 + arguments;
}

// The child at slot `slot` of `node` (an operand's 0 to 2, an argument's past them) and whether its value is read: its
// conversion has the lvalue's.
static uint32_t check_unseq_child(const CheckUnseq* checker, uint32_t node, uint32_t slot, bool* read)
{
    const CheckGraph* graph = checker->graph;
    const CheckStream* stream = checker->stream;
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t flags = 0;
    if (slot < 3)
    {
        result = slot == 0 ? graph->operand0[node] : slot == 1 ? graph->operand1[node] : graph->operand2[node];
        flags = stream->conversions != NULL ? stream->conversions[node].operands[slot].flags : 0;
    }
    else if (check_graph_has_arguments(graph->ops[node]) && graph->argument_offsets != NULL && graph->argument_counts != NULL &&
             slot - 3 < graph->argument_counts[node])
    {
        const uint32_t index = graph->argument_offsets[node] + slot - 3;
        result = graph->argument_nodes[index];
        flags = stream->argument_conversions != NULL ? stream->argument_conversions[index].flags : 0;
    }
    *read = (flags & BUSTER_CHECK_CONVERT_LVALUE) != 0;
    return result;
}

// What clang's evaluation of a node as a condition finds (EvaluateAsBooleanCondition, ignoring side effects).
#define BUSTER_CHECK_UNSEQ_UNKNOWN 1u
#define BUSTER_CHECK_UNSEQ_FALSE 2u
#define BUSTER_CHECK_UNSEQ_TRUE 3u

// The truth of `node` the stream's constant gives, or unknown.
static uint8_t check_unseq_constant_truth(const CheckUnseq* checker, uint32_t node)
{
    const CheckConstantKind kind = node < checker->stream->count ? check_stream_constant_kind(checker->stream, node) : CHECK_CONSTANT_NONE;
    const CheckValue value = kind == CHECK_CONSTANT_INTEGER || kind == CHECK_CONSTANT_FLOATING ? check_stream_value(checker->stream, node) : (CheckValue){ 0 };
    const bool truth = kind == CHECK_CONSTANT_INTEGER ? value.integer != 0 : value.floating != 0;
    const uint8_t result = kind != CHECK_CONSTANT_INTEGER && kind != CHECK_CONSTANT_FLOATING ? BUSTER_CHECK_UNSEQ_UNKNOWN :
                           truth ? BUSTER_CHECK_UNSEQ_TRUE : BUSTER_CHECK_UNSEQ_FALSE;
    return result;
}

// `node`'s truth from its operands' (each known already), as clang's constant evaluator finds it, ignoring side effects:
// a comma's right operand's; a logical operator's that one operand decides, the other unknown too (`z && 0`); a
// conditional's chosen arm's; a statement expression's value's and a selection's chosen operand's; none of an
// assignment's, an increment's or a call's, nor of a variable's; else the stream's constant, where every operand is known
// (so not `0 * z`, which the stream folds).
static uint8_t check_unseq_fold(const CheckUnseq* checker, uint32_t node)
{
    const CheckGraph* graph = checker->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    uint8_t known[3];
    bool all = true;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        known[i] = operands[i] < graph->count ? checker->known[operands[i]] : BUSTER_CHECK_UNSEQ_TRUE;
        all &= known[i] != BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    // (A comma's left operand's truth is no part of its own.)
    const bool zero = known[0] == BUSTER_CHECK_UNSEQ_FALSE || known[1] == BUSTER_CHECK_UNSEQ_FALSE;
    const bool one = known[0] == BUSTER_CHECK_UNSEQ_TRUE || known[1] == BUSTER_CHECK_UNSEQ_TRUE;
    uint8_t result = BUSTER_CHECK_UNSEQ_UNKNOWN;
    if (op == CHECK_OP_COMMA)
    {
        result = known[1];
    }
    else if (op == CHECK_OP_LOGICAL_AND)
    {
        result = zero ? BUSTER_CHECK_UNSEQ_FALSE : all ? BUSTER_CHECK_UNSEQ_TRUE : BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    else if (op == CHECK_OP_LOGICAL_OR)
    {
        result = one ? BUSTER_CHECK_UNSEQ_TRUE : all ? BUSTER_CHECK_UNSEQ_FALSE : BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        result = known[0] == BUSTER_CHECK_UNSEQ_TRUE ? known[1] : known[0] == BUSTER_CHECK_UNSEQ_FALSE ? known[2] : BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    else if (op == CHECK_OP_STATEMENT_VALUE || op == CHECK_OP_SELECT)
    {
        result = known[0];
    }
    else if (check_unseq_modification(op) || op == CHECK_OP_CALL || op == CHECK_OP_VARARGS)
    {
        result = BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    else
    {
        bool arguments = true;
        for (uint32_t i = 3; i < check_unseq_slots(graph, node); i += 1)
        {
            bool read = false;
            const uint32_t child = check_unseq_child(checker, node, i, &read);
            arguments &= child >= graph->count || checker->known[child] != BUSTER_CHECK_UNSEQ_UNKNOWN;
        }
        // (A sizeof's or an alignof's operand is no value: its constant is its own.)
        const bool query = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        result = (all && arguments) || query ? check_unseq_constant_truth(checker, node) : BUSTER_CHECK_UNSEQ_UNKNOWN;
    }
    return result;
}

// Whether `node`, a logical operator's or a conditional's first operand, evaluates as clang's evaluation of a condition
// finds it, and its truth in `*truth` (check_unseq_fold, memoized for the unit, each node's once: a postorder walk of
// its operands with an explicit stack).
static bool check_unseq_known(CheckUnseq* checker, uint32_t node, bool* truth)
{
    const CheckGraph* graph = checker->graph;
    uint32_t depth = 0;
    if (node < graph->count && checker->known[node] == 0)
    {
        checker->known_stack[depth++] = node;
    }
    while (depth != 0)
    {
        const uint32_t at = checker->known_stack[depth - 1];
        bool pushed = false;
        for (uint32_t slot = 0; slot < check_unseq_slots(graph, at) && !pushed; slot += 1)
        {
            bool read = false;
            const uint32_t child = check_unseq_child(checker, at, slot, &read);
            if (child < at && checker->known[child] == 0 && depth < graph->count)
            {
                checker->known_stack[depth++] = child;
                pushed = true;
            }
        }
        if (!pushed)
        {
            checker->known[at] = check_unseq_fold(checker, at);
            depth -= 1;
        }
    }
    const uint8_t result = node < graph->count ? checker->known[node] : BUSTER_CHECK_UNSEQ_UNKNOWN;
    *truth = result == BUSTER_CHECK_UNSEQ_TRUE;
    return result != BUSTER_CHECK_UNSEQ_UNKNOWN;
}

static void check_unseq_push(CheckUnseq* checker, uint32_t node, uint8_t visit, uint32_t entry)
{
    if (checker->frame_count < checker->frame_capacity && node < checker->graph->count)
    {
        checker->frames[checker->frame_count] = (CheckUnseqFrame){
            .node = node,
            .visit = visit,
            .entry = entry,
            .tracker = BUSTER_CHECK_NONE,
            .mark = BUSTER_CHECK_NONE,
            .outer_mark = BUSTER_CHECK_NONE,
        };
        checker->frame_count += 1;
    }
    else
    {
        checker->failed |= node < checker->graph->count;
    }
}

// Pushes the child at `slot` of `node`, a read of it where its conversion reads it.
static void check_unseq_push_child(CheckUnseq* checker, const Parser* parser, uint32_t node, uint32_t slot)
{
    bool read = false;
    const uint32_t child = check_unseq_child(checker, node, slot, &read);
    uint32_t name = 0;
    const uint32_t object = read && child < checker->graph->count ? check_unseq_object(checker, parser, child, false, &name) : 0;
    if (object != 0)
    {
        check_unseq_push(checker, child, CHECK_UNSEQ_READ, check_unseq_entry(checker, object, name));
    }
    else
    {
        check_unseq_push(checker, child, CHECK_UNSEQ_VISIT, BUSTER_CHECK_NONE);
    }
}

// The mode `node`'s visit takes: clang's visit of its operator.
static uint8_t check_unseq_mode(const CheckUnseq* checker, const Parser* parser, uint32_t node, uint32_t* entry_object, uint32_t* entry_name)
{
    const CheckGraph* graph = checker->graph;
    const CheckOp op = graph->ops[node];
    uint8_t result = CHECK_UNSEQ_CHILDREN;
    *entry_object = 0;
    *entry_name = 0;
    if (op == CHECK_OP_STATEMENT_VALUE || check_unseq_unevaluated(graph, node))
    {
        result = CHECK_UNSEQ_SKIP;
    }
    else if (op == CHECK_OP_SELECT)
    {
        result = CHECK_UNSEQ_CHOSEN;
    }
    else if (op == CHECK_OP_COMMA)
    {
        result = CHECK_UNSEQ_SEQUENCED;
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        result = CHECK_UNSEQ_LOGICAL;
    }
    else if (check_unseq_assignment(op))
    {
        result = CHECK_UNSEQ_ASSIGN;
        *entry_object = check_unseq_object(checker, parser, graph->operand0[node], true, entry_name);
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT)
    {
        // (One that names no variable is visited as any other operator.)
        *entry_object = check_unseq_object(checker, parser, graph->operand0[node], true, entry_name);
        result = *entry_object != 0 ? CHECK_UNSEQ_INCREMENT : CHECK_UNSEQ_CHILDREN;
    }
    return result;
}

// One step of the top frame's visit. Each visit pushes a child and returns to its next step when the child is done; its
// last step pops it.
static void check_unseq_step(CheckUnseq* checker, const Parser* parser)
{
    const CheckGraph* graph = checker->graph;
    const uint32_t top = checker->frame_count - 1;
    CheckUnseqFrame* frame = &checker->frames[top];
    const uint32_t node = frame->node;
    const CheckOp op = graph->ops[node];
    if (frame->visit == CHECK_UNSEQ_VISIT)
    {
        uint32_t object = 0;
        uint32_t name = 0;
        frame->visit = check_unseq_mode(checker, parser, node, &object, &name);
        frame->entry = check_unseq_entry(checker, object, name);
    }
    const uint8_t step = frame->step;
    frame->step += 1;
    bool done = false;
    switch ((CheckUnseqVisit)frame->visit)
    {
        case CHECK_UNSEQ_READ:
        {
            // notePreUse, the child's own visit, notePostUse.
            if (step == 0)
            {
                check_unseq_pre_use(checker, frame->entry, node);
                check_unseq_push(checker, node, CHECK_UNSEQ_VISIT, BUSTER_CHECK_NONE);
            }
            else
            {
                check_unseq_post_use(checker, frame->entry, node);
                done = true;
            }
            break;
        }
        case CHECK_UNSEQ_CHOSEN:
        {
            // A static selection's chosen operand alone.
            if (step == 0)
            {
                check_unseq_push_child(checker, parser, node, 0);
            }
            done = step != 0;
            break;
        }
        case CHECK_UNSEQ_SEQUENCED:
        {
            // VisitSequencedExpressions: the left operand in a region of its own, sequenced, then the right in another.
            if (step == 0)
            {
                frame->old_region = checker->region;
                frame->regions[0] = check_unseq_allocate(checker, checker->region);
                frame->regions[1] = check_unseq_allocate(checker, checker->region);
                frame->outer_mark = check_unseq_sequence_begin(checker);
                checker->region = frame->regions[0];
                check_unseq_push_child(checker, parser, node, 0);
            }
            else if (step == 1)
            {
                check_unseq_sequence_end(checker, frame->outer_mark);
                checker->region = frame->regions[1];
                check_unseq_push_child(checker, parser, node, 1);
            }
            else
            {
                checker->region = frame->old_region;
                check_unseq_merge(checker, frame->regions[0]);
                check_unseq_merge(checker, frame->regions[1]);
                done = true;
            }
            break;
        }
        case CHECK_UNSEQ_LOGICAL:
        {
            // VisitBinLAnd, VisitBinLOr, VisitAbstractConditionalOperator: the first operand sequenced, in a region of its
            // own; each other operand in its own, unless the first is a constant that skips it. A GNU conditional without a
            // middle operand reads its first through an opaque value, which clang's visitor doesn't enter.
            const bool conditional = op == CHECK_OP_CONDITIONAL;
            const bool omitted = conditional && graph->operand1[node] == graph->operand0[node];
            if (step == 0)
            {
                frame->old_region = checker->region;
                frame->regions[0] = check_unseq_allocate(checker, checker->region);
                frame->regions[1] = check_unseq_allocate(checker, checker->region);
                frame->regions[2] = conditional ? check_unseq_allocate(checker, checker->region) : 0;
                frame->eval_ok = true;
                frame->tracker = checker->tracker;
                checker->tracker = top;
                frame->outer_mark = check_unseq_sequence_begin(checker);
                checker->region = frame->regions[0];
                if (!omitted)
                {
                    check_unseq_push_child(checker, parser, node, 0);
                }
            }
            else if (step == 1)
            {
                check_unseq_sequence_end(checker, frame->outer_mark);
                bool truth = false;
                // (EvaluationTracker::evaluate: none once an evaluation inside it failed.)
                frame->eval_ok = frame->eval_ok && check_unseq_known(checker, graph->operand0[node], &truth);
                const bool known = frame->eval_ok;
                const bool second = conditional ? (!known || truth) && !omitted : op == CHECK_OP_LOGICAL_AND ? !known || truth : !known || !truth;
                // (Whether the next step takes a conditional's other arm.)
                frame->child = conditional && (!known || !truth) ? 1u : 0u;
                if (second)
                {
                    checker->region = frame->regions[1];
                    check_unseq_push_child(checker, parser, node, 1);
                }
            }
            else if (step == 2 && frame->child != 0)
            {
                checker->region = frame->regions[2];
                check_unseq_push_child(checker, parser, node, 2);
            }
            else
            {
                checker->region = frame->old_region;
                check_unseq_merge(checker, frame->regions[0]);
                check_unseq_merge(checker, frame->regions[1]);
                if (conditional)
                {
                    check_unseq_merge(checker, frame->regions[2]);
                }
                // (The tracker ends: one that failed fails the one around it.)
                checker->tracker = frame->tracker;
                if (frame->tracker != BUSTER_CHECK_NONE)
                {
                    checker->frames[frame->tracker].eval_ok = checker->frames[frame->tracker].eval_ok && frame->eval_ok;
                }
                done = true;
            }
            break;
        }
        case CHECK_UNSEQ_ASSIGN:
        {
            // VisitBinAssign: notePreMod, the left side, a compound one's read of it, the right side, notePostMod as a side
            // effect (C's), all in the one region.
            if (step == 0)
            {
                if (frame->entry != BUSTER_CHECK_NONE)
                {
                    check_unseq_pre_mod(checker, frame->entry, node);
                }
                check_unseq_push(checker, graph->operand0[node], CHECK_UNSEQ_VISIT, BUSTER_CHECK_NONE);
            }
            else if (step == 1)
            {
                if (frame->entry != BUSTER_CHECK_NONE && op != CHECK_OP_ASSIGN)
                {
                    check_unseq_post_use(checker, frame->entry, node);
                }
                check_unseq_push_child(checker, parser, node, 1);
            }
            else
            {
                if (frame->entry != BUSTER_CHECK_NONE)
                {
                    check_unseq_post_mod(checker, frame->entry, node);
                }
                done = true;
            }
            break;
        }
        case CHECK_UNSEQ_INCREMENT:
        {
            // VisitUnaryPreIncDec and VisitUnaryPostIncDec: notePreMod, the operand, notePostMod as a side effect (C's).
            if (step == 0)
            {
                check_unseq_pre_mod(checker, frame->entry, node);
                check_unseq_push(checker, graph->operand0[node], CHECK_UNSEQ_VISIT, BUSTER_CHECK_NONE);
            }
            else
            {
                check_unseq_post_mod(checker, frame->entry, node);
                done = true;
            }
            break;
        }
        case CHECK_UNSEQ_CHILDREN:
        {
            // VisitExpr's children in order; a call's in a sequenced subexpression (VisitCallExpr), whose side effects are
            // complete once it is.
            if (step == 0 && op == CHECK_OP_CALL)
            {
                frame->outer_mark = check_unseq_sequence_begin(checker);
                frame->mark = 0;
            }
            const uint32_t slots = check_unseq_slots(graph, node);
            bool pushed = false;
            while (!pushed && frame->child < slots)
            {
                bool read = false;
                const uint32_t slot = frame->child;
                const uint32_t child = check_unseq_child(checker, node, slot, &read);
                frame->child += 1;
                if (child < graph->count)
                {
                    check_unseq_push_child(checker, parser, node, slot);
                    pushed = true;
                }
            }
            if (!pushed && frame->mark != BUSTER_CHECK_NONE)
            {
                check_unseq_sequence_end(checker, frame->outer_mark);
            }
            done = !pushed;
            break;
        }
        default:
        {
            done = true;
            break;
        }
    }
    if (done)
    {
        checker->frame_count -= 1;
    }
}

// One full expression's walk from its `count` starts, each a read where `reads` says so (a brace initializer's values,
// whose list clang visits as one expression, in one region), in the tables check_unseq_reserve made (their sizes set):
// its reports in checker->reports. False where they couldn't hold it.
static bool check_unseq_walk(CheckUnseq* checker, const Parser* parser, const uint32_t* starts, const bool* reads, uint32_t count)
{
    checker->parents[0] = 0;
    checker->merged[0] = 0;
    checker->region_count = 1;
    checker->region = 0;
    checker->entry_count = 0;
    checker->saved_count = 0;
    checker->open_mark = BUSTER_CHECK_NONE;
    checker->tracker = BUSTER_CHECK_NONE;
    checker->frame_count = 0;
    checker->report_count = 0;
    checker->failed = false;
    for (uint32_t i = 0; i < count && !checker->failed; i += 1)
    {
        uint32_t name = 0;
        const uint32_t object = reads != NULL && reads[i] ? check_unseq_object(checker, parser, starts[i], false, &name) : 0;
        check_unseq_push(checker, starts[i], object != 0 ? CHECK_UNSEQ_READ : CHECK_UNSEQ_VISIT, check_unseq_entry(checker, object, name));
        while (checker->frame_count != 0 && !checker->failed)
        {
            check_unseq_step(checker, parser);
        }
    }
    return !checker->failed;
}

// The nodes of `root`'s full expression, an upper bound: its subtree's, a statement expression's value's and an
// unevaluated operand's included. The candidate statement expressions' values inside it, in `inner` when not null, in
// node order (postorder: an inner one's and a left one's first, as clang completes them).
static uint32_t check_unseq_count(const CheckUnseq* checker, uint32_t root, uint32_t* stack, uint32_t capacity, uint32_t* inner, uint32_t* inner_count)
{
    const CheckGraph* graph = checker->graph;
    uint32_t count = 0;
    uint32_t depth = 0;
    stack[depth++] = root;
    while (depth != 0)
    {
        const uint32_t node = stack[--depth];
        count += 1;
        // (A GNU conditional without a middle operand names its first twice: each value is listed once.)
        const bool value = inner != NULL && graph->ops[node] == CHECK_OP_STATEMENT_VALUE;
        bool listed = false;
        for (uint32_t i = 0; value && i < *inner_count && !listed; i += 1)
        {
            listed = inner[i] == node;
        }
        if (value && !listed && (checker->marks[node] & BUSTER_CHECK_UNSEQ_MODIFIES) != 0)
        {
            uint32_t at = *inner_count;
            while (at > 0 && inner[at - 1] > node)
            {
                inner[at] = inner[at - 1];
                at -= 1;
            }
            inner[at] = node;
            *inner_count += 1;
        }
        const uint32_t slots = check_unseq_slots(graph, node);
        for (uint32_t slot = 0; slot < slots && depth < capacity; slot += 1)
        {
            bool read = false;
            const uint32_t child = check_unseq_child(checker, node, slot, &read);
            if (child < graph->count && child < node)
            {
                stack[depth++] = child;
            }
        }
    }
    return count;
}

// The first node of `root`'s subtree in postorder, which keeps each subtree's nodes together: its smallest child's,
// repeatedly.
static uint32_t check_unseq_first(const CheckGraph* graph, uint32_t root)
{
    uint32_t at = root;
    bool going = true;
    while (going)
    {
        // (A missing operand is BUSTER_CHECK_NONE, above every node.)
        uint32_t least = at;
        least = graph->operand0[at] < least ? graph->operand0[at] : least;
        least = graph->operand1[at] < least ? graph->operand1[at] : least;
        least = graph->operand2[at] < least ? graph->operand2[at] : least;
        const uint32_t arguments = check_graph_has_arguments(graph->ops[at]) && graph->argument_counts != NULL ? graph->argument_counts[at] : 0;
        for (uint32_t a = 0; a < arguments; a += 1)
        {
            const uint32_t child = graph->argument_nodes[graph->argument_offsets[at] + a];
            least = child < least ? child : least;
        }
        going = least < at;
        at = least;
    }
    return at;
}

// Whether the walk may report in the full expression of `count` starts. A report takes two usages of a variable, one a
// modification, and each usage names its variable through an identifier of its own (a read's, a modification's
// target's; a modification of a modification's target takes one more): so a root whose node range (the subtree's, its
// unevaluated operands' and statement expressions' values' with it: a superset) holds no modified variable's name more
// than once, counting its modifications, can't report. Most roots modify a variable they name nowhere else. The vector
// path finds the range's modifications and values 16 nodes at a time by operator; the scalar path, its twin, tests every
// node. A brace initializer's list, and a range with more modified names than the list holds, are walked.
static bool check_unseq_repeats(const CheckUnseq* checker, const Parser* parser, const uint32_t* starts, const bool* reads, uint32_t count,
                                bool vector)
{
    const CheckGraph* graph = checker->graph;
    const uint32_t root = count == 1 && reads == NULL ? starts[0] : BUSTER_CHECK_NONE;
    bool repeats = root >= graph->count;
    uint32_t names[8];
    uint32_t uses[8];
    uint32_t name_count = 0;
    const uint32_t first = !repeats ? check_unseq_first(graph, root) : 0;
    const uint32_t end = !repeats ? root + 1 : 0;
    // (Two passes: the modifications' targets' names, then every identifier value's against them.)
    for (uint32_t pass = 0; pass < 2 && !repeats; pass += 1)
    {
        for (uint32_t base = first; base < end && !repeats; base += 16)
        {
            const uint32_t live = end - base < 16 ? end - base : 16;
            uint32_t selected = live == 16 ? 0xffffu : (1u << live) - 1;
            if (vector)
            {
                const __mmask16 lanes = (__mmask16)selected;
                const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + base);
                selected = pass == 0 ? (_mm512_mask_cmpge_epu32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_PRE_INCREMENT)) &
                                        _mm512_mask_cmple_epu32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BIT_XOR_ASSIGN))) |
                                           _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_ASSIGN)) :
                                       _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_VALUE));
            }
            for (; selected != 0 && !repeats; selected &= selected - 1)
            {
                const uint32_t node = base + (uint32_t)__builtin_ctz(selected);
                const CheckOp op = graph->ops[node];
                uint32_t token = BUSTER_CHECK_NONE;
                if (pass == 0 && check_unseq_modification(op) && graph->operand0[node] < graph->count &&
                    check_unseq_object(checker, parser, graph->operand0[node], true, &token) == 0)
                {
                    token = BUSTER_CHECK_NONE;
                }
                if (pass == 1 && op == CHECK_OP_VALUE && graph->tokens[node] < parser->tokens.count &&
                    parser->tokens.kinds[graph->tokens[node]] == PARSE_KIND_IDENTIFIER)
                {
                    token = graph->tokens[node];
                }
                const uint32_t name = token < parser->tokens.count ? parser->tokens.names[token] : BUSTER_CHECK_NONE;
                uint32_t k = 0;
                while (name != BUSTER_CHECK_NONE && k < name_count && names[k] != name)
                {
                    k += 1;
                }
                repeats = pass == 0 && name != BUSTER_CHECK_NONE && k == name_count && name_count == 8;
                if (pass == 0 && name != BUSTER_CHECK_NONE && k == name_count && !repeats)
                {
                    names[k] = name;
                    uses[k] = 0;
                    name_count += 1;
                }
                if (name != BUSTER_CHECK_NONE && k < name_count)
                {
                    uses[k] += 1;
                    repeats = uses[k] > 2;
                }
            }
        }
        // (A range without a modified variable is done after the first pass.)
        pass += name_count == 0 ? 1 : 0;
    }
    return repeats;
}

// The walk's tables, kept across a unit's full expressions and grown as a larger one needs (CheckUnseq's), with the
// count's stack and the statement expressions' values, and the unit's marks.
typedef struct CheckUnseqStorage CheckUnseqStorage;
struct CheckUnseqStorage
{
    CheckUnseq checker;
    uint32_t capacity;
    uint32_t table_size;
    // The count's stack and the statement expressions' values, room for the whole graph's nodes, made once.
    uint32_t* stack;
    uint32_t* inner;
    // The vector path's or the scalar path's prefilter (check_unseq_repeats).
    bool vector;
};

// Room for a full expression of `nodes` nodes, the tables grown to at least twice the last size: false where scratch
// has none.
static bool check_unseq_reserve(CheckUnseqStorage* storage, uint32_t nodes, Arena* scratch)
{
    bool valid = true;
    if (nodes > storage->capacity)
    {
        const uint64_t wanted = (uint64_t)storage->capacity * 2 > nodes ? (uint64_t)storage->capacity * 2 : nodes;
        const uint32_t capacity = wanted < 64 ? 64u : wanted > UINT32_MAX / 4 ? UINT32_MAX / 4 : (uint32_t)wanted;
        uint32_t table_size = 16;
        while (table_size < capacity * 2u)
        {
            table_size <<= 1;
        }
        CheckUnseq* checker = &storage->checker;
        checker->parents = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)capacity * 3 + 1);
        checker->merged = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)capacity * 3 + 1);
        checker->entries = CHECK_COMMON_PUSH_ARRAY(scratch, CheckUnseqEntry, (uint64_t)capacity + 1);
        checker->table = CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, table_size);
        checker->saved = CHECK_COMMON_PUSH_ARRAY(scratch, CheckUnseqSaved, (uint64_t)capacity * 2 + 1);
        checker->frames = CHECK_COMMON_PUSH_ARRAY(scratch, CheckUnseqFrame, (uint64_t)capacity * 2 + 1);
        checker->reports = CHECK_COMMON_PUSH_ARRAY(scratch, CheckUnseqReport, (uint64_t)capacity + 1);
        valid = checker->parents != NULL && checker->merged != NULL && checker->entries != NULL && checker->table != NULL &&
                checker->saved != NULL && checker->frames != NULL && checker->reports != NULL;
        storage->capacity = valid ? capacity : 0;
        storage->table_size = table_size;
    }
    return valid;
}

// The unit's marks (BUSTER_CHECK_UNSEQ_MODIFIES): each modification's node and every node above it, to its root. The
// vector path finds the modifications 16 nodes at a time by operator; the scalar path, its twin, tests every node. Each
// climb stops at a node already marked, so the marks cost O(nodes). Null where the unit has no modification (or scratch
// has no room, which `*failed` says).
static uint8_t* check_unseq_marks(const CheckGraph* graph, const uint32_t* parents, bool vector, Arena* scratch, bool* failed)
{
    uint8_t* marks = NULL;
    *failed = false;
    for (uint32_t first = 0; first < graph->count && !*failed; first += 16)
    {
        const uint32_t live = graph->count - first < 16 ? graph->count - first : 16;
        uint32_t selected = live == 16 ? 0xffffu : (1u << live) - 1;
        if (vector)
        {
            const __mmask16 lanes = (__mmask16)selected;
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, graph->ops + first);
            selected = _mm512_mask_cmpge_epu32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_PRE_INCREMENT)) &
                       _mm512_mask_cmple_epu32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BIT_XOR_ASSIGN));
            selected |= _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_ASSIGN));
        }
        for (; selected != 0 && !*failed; selected &= selected - 1)
        {
            const uint32_t node = first + (uint32_t)__builtin_ctz(selected);
            if (check_unseq_modification(graph->ops[node]))
            {
                if (marks == NULL)
                {
                    marks = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, graph->count);
                    *failed = marks == NULL;
                    if (marks != NULL)
                    {
                        memset(marks, 0, graph->count);
                    }
                }
                for (uint32_t at = node; marks != NULL && at < graph->count && marks[at] == 0;)
                {
                    marks[at] = BUSTER_CHECK_UNSEQ_MODIFIES;
                    const uint32_t up = parents[at];
                    at = up > at ? up : BUSTER_CHECK_NONE;
                }
            }
        }
    }
    return marks;
}

// A warning's publication, as clang's DiagRuntimeBehavior queues it: in a function at its end, in a file-scope
// initializer at its declarator's end.
static void check_unseq_publish(CheckCommonEmission* emission, uint32_t token, const char* message)
{
    const Parser* parser = emission->parser;
    const uint32_t boundary = check_common_function_boundary(parser, token);
    const uint32_t declarator = boundary == token ? check_driver_file_initializer_end(parser, emission->input, token) : BUSTER_CHECK_NONE;
    check_common_report_evaluated(parser, emission->pp, emission->input, token, declarator != BUSTER_CHECK_NONE ? declarator : boundary,
                                  declarator != BUSTER_CHECK_NONE ? PP_DIAGNOSTIC_RUNTIME : PP_DIAGNOSTIC_DEFERRED, CHECK_WARNING_ID_UNSEQUENCED, false,
                                  message, NULL, 0, emission->scratch);
}

// -Wunsequenced of the full expression whose `count` starts are `starts` (a root, or a brace initializer's values, each
// read where `reads` says so), where one holds a modification (`marks`): first the statement expressions' values inside
// it, each a full expression of its own, then its own. The walk's tables come from `storage`; a full expression they
// can't hold fails the unit, as a full worklist does.
static void check_unseq_check(CheckCommonEmission* emission, CheckUnseqStorage* storage, const uint8_t* marks, const uint32_t* starts, const bool* reads,
                              uint32_t count)
{
    const CheckGraph* graph = &emission->input->graph;
    const Parser* parser = emission->parser;
    Preprocessor* pp = emission->pp;
    bool modifies = false;
    for (uint32_t i = 0; marks != NULL && i < count; i += 1)
    {
        modifies |= starts[i] < graph->count && (marks[starts[i]] & BUSTER_CHECK_UNSEQ_MODIFIES) != 0;
    }
    if (modifies)
    {
        CheckUnseq* checker = &storage->checker;
        checker->graph = graph;
        checker->stream = emission->stream;
        checker->marks = marks;
        checker->evaluated = emission->evaluated;
        // (The count's stack and list take room for the whole graph's nodes, which bounds them, once a unit.)
        if (storage->stack == NULL)
        {
            storage->stack = CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, (uint64_t)graph->count + 1);
            storage->inner = storage->stack != NULL ? CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, (uint64_t)graph->count + 1) : NULL;
            checker->known_stack = storage->inner != NULL ? CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, (uint64_t)graph->count + 1) : NULL;
            checker->known = checker->known_stack != NULL ? CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint8_t, (uint64_t)graph->count + 1) : NULL;
            if (checker->known != NULL)
            {
                memset(checker->known, 0, (uint64_t)graph->count + 1);
            }
        }
        bool valid = checker->known != NULL;
        // (Most full expressions modify a variable they use nowhere else: no walk can report in them.)
        modifies = !valid || check_unseq_repeats(checker, parser, starts, reads, count, storage->vector);
        uint32_t inner_count = 0;
        uint32_t nodes = 0;
        for (uint32_t i = 0; valid && modifies && i < count; i += 1)
        {
            nodes += starts[i] < graph->count ? check_unseq_count(checker, starts[i], storage->stack, graph->count + 1, storage->inner, &inner_count) : 0;
        }
        valid = valid && (!modifies || check_unseq_reserve(storage, nodes, emission->scratch));
        // (A table of twice the full expression's nodes, a power of two, cleared for each walk.)
        uint32_t table_size = 16;
        while (table_size < nodes * 2u && table_size < storage->table_size)
        {
            table_size <<= 1;
        }
        for (uint32_t unit = 0; valid && modifies && unit <= inner_count && !pp->fatal; unit += 1)
        {
            const bool own = unit == inner_count;
            const uint32_t value = !own ? graph->operand0[storage->inner[unit]] : BUSTER_CHECK_NONE;
            memset(checker->table, 0xff, (uint64_t)table_size * sizeof(uint32_t));
            checker->table_mask = table_size - 1;
            checker->region_capacity = storage->capacity * 3 + 1;
            checker->frame_capacity = storage->capacity * 2 + 1;
            const bool walked = own ? check_unseq_walk(checker, parser, starts, reads, count) :
                                value < graph->count && check_unseq_walk(checker, parser, &value, NULL, 1);
            valid = walked || (!own && value >= graph->count);
            for (uint32_t i = 0; walked && i < checker->report_count && !pp->fatal; i += 1)
            {
                const CheckUnseqReport* report = checker->reports + i;
                const uint32_t name = parser->tokens.names[report->name];
                const char* message = check_warning_text(emission->scratch, report->mod_mod ? "multiple unsequenced modifications to '%.*s'" :
                                                                                                "unsequenced modification and access to '%.*s'",
                                                         (int)intern_length(pp->table, name), (const char*)intern_bytes(pp->table, name));
                if (message != NULL)
                {
                    check_unseq_publish(emission, graph->tokens[report->node], message);
                }
            }
        }
        if (!valid)
        {
            check_common_exhausted(pp);
        }
    }
}

// The brace initializers' plans (program->initializers) whose lists hold a modification, each list one full expression
// clang's SequenceChecker visits in one region (C's InitListExpr): keys of their last value's node and their index,
// sorted, `*count` of them, their values marked BUSTER_CHECK_UNSEQ_PLANNED. A scalar `=` initializer's plan is its
// INITIALIZE root's, checked as that root. Null where none does (or scratch has no room, which `*failed` says).
static const uint64_t* check_unseq_plans(const CheckInput* input, const CheckProgram* program, const uint32_t* parents, uint8_t* marks, bool vector,
                                         Arena* scratch, uint32_t* count, bool* failed)
{
    const CheckInitializers* initializers = program != NULL ? program->initializers : NULL;
    uint64_t* keys = NULL;
    uint64_t* temporary = NULL;
    *count = 0;
    for (uint32_t i = 0; initializers != NULL && i < initializers->count && !*failed; i += 1)
    {
        const CheckInitializer* plan = initializers->plans + i;
        const uint32_t declaration = initializers->declaration_tokens[i];
        const bool scalar = declaration < input->token_count && check_common_sparse_get(input->initializer_nodes, declaration) < input->graph.count;
        bool modifies = false;
        uint32_t last = 0;
        for (uint32_t p = 0; !scalar && p < plan->count; p += 1)
        {
            const uint32_t node = plan->parts[p].node;
            const bool value = plan->parts[p].kind == CHECK_INIT_VALUE && node < input->graph.count;
            modifies |= value && (marks[node] & BUSTER_CHECK_UNSEQ_MODIFIES) != 0;
            last = value && node > last ? node : last;
        }
        if (modifies && keys == NULL)
        {
            keys = CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, (uint64_t)initializers->count - i);
            temporary = keys != NULL ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, (uint64_t)initializers->count - i) : NULL;
            *failed = temporary == NULL;
        }
        // (A value's root, which may be its declaration's discard wrapper above it, is the one the walk meets.)
        for (uint32_t p = 0; modifies && !*failed && p < plan->count; p += 1)
        {
            uint32_t node = plan->parts[p].kind == CHECK_INIT_VALUE ? plan->parts[p].node : BUSTER_CHECK_NONE;
            for (uint32_t steps = 0; node < input->graph.count && steps < input->graph.count; steps += 1)
            {
                marks[node] |= BUSTER_CHECK_UNSEQ_PLANNED;
                node = parents[node] > node ? parents[node] : BUSTER_CHECK_NONE;
            }
        }
        if (modifies && !*failed)
        {
            keys[*count] = (uint64_t)last << 32 | i;
            *count += 1;
        }
    }
    const uint32_t varying = input->graph.count != 0 ? UINT32_MAX >> __builtin_clz(input->graph.count) : 0;
    return *count != 0 ? c_bindings_sort(keys, temporary, *count, vector, varying) : NULL;
}

// -Wunsequenced of the brace initializer `index`'s list, its values in order, each read where its conversion reads it
// (a range's value once for each element it initializes, as clang's list holds it), but those in unreachable code.
static void check_unseq_plan(CheckCommonEmission* emission, CheckUnseqStorage* storage, const uint8_t* marks, const CheckProgram* program, uint32_t index)
{
    const CheckInitializer* plan = program->initializers->plans + index;
    const CheckGraph* graph = &emission->input->graph;
    uint32_t* starts = CHECK_COMMON_PUSH_ARRAY(emission->scratch, uint32_t, (uint64_t)plan->count + 1);
    bool* reads = starts != NULL ? CHECK_COMMON_PUSH_ARRAY(emission->scratch, bool, (uint64_t)plan->count + 1) : NULL;
    uint32_t count = 0;
    for (uint32_t p = 0; reads != NULL && p < plan->count; p += 1)
    {
        const CheckInitPart* part = plan->parts + p;
        const uint32_t root = part->node;
        if (part->kind == CHECK_INIT_VALUE && root < graph->count && emission->stream->diagnostics[root] == CHECK_DIAG_NONE &&
            (emission->evaluated == NULL || emission->evaluated[root] != 0))
        {
            starts[count] = root;
            reads[count] = (part->conversion.flags & BUSTER_CHECK_CONVERT_LVALUE) != 0;
            count += 1;
        }
    }
    if (reads == NULL)
    {
        check_common_exhausted(emission->pp);
    }
    check_unseq_check(emission, storage, marks, starts, reads, count);
}
