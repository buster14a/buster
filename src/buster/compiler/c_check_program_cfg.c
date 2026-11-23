// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A condition's truth as clang's CFG builder prunes its edges (#2990): CFGBuilder::tryEvaluateBool and its heuristics
// (clang 23.1.4's lib/Analysis/CFG.cpp, checkIncorrect*Operator, VisitLogicalOperator), which decide some conditions that
// aren't constants (`x * 0`, `x | 1`, `(x & 8) == 4`, `x == 1 && x == 2`, `x && !x`). The runtime warnings' reach, the
// -Wreturn-type walk and the uninitialized flow read it through check_program_branch_truth; the same rules' hits are
// -Wtautological-bitwise-compare's, -Wtautological-overlap-compare's and -Wtautological-negation-compare's
// (check_common_cfg_tautologies, which observes them in the builder's order). The rules mirror clang's; no code is
// imported. -Wimplicit-fallthrough's reader has the same rules on tokens (check_input_fallthrough_branches), where no
// graph exists yet.
//
// The graph has no parentheses and no implicit conversions: a node is parenthesized where its token's parse-tree parent
// is a group (check_cfg_parenthesized), and an operand has an implicit conversion where its type isn't the one its
// operator converts it to (clang's ImplicitCastExpr). Each walk is an explicit stack (the call graph's no-recursion
// rule), BUSTER_CHECK_CFG_DEPTH levels deep at most; past that a condition is unknown, which keeps both edges.

#define BUSTER_CHECK_CFG_DEPTH 64u
#define BUSTER_CHECK_CFG_UNKNOWN 2u

// What a rule found, as clang's CFGCallback hears it: compareBitwiseEquality, compareBitwiseOr, logicAlwaysTrue and
// compareAlwaysTrue.
typedef enum CheckCfgEventKind
{
    CHECK_CFG_EVENT_BITWISE_EQUALITY,
    CHECK_CFG_EVENT_BITWISE_OR,
    CHECK_CFG_EVENT_NEGATION,
    CHECK_CFG_EVENT_OVERLAP,
} CheckCfgEventKind;

typedef struct CheckCfgEvent CheckCfgEvent;
struct CheckCfgEvent
{
    uint32_t node;
    uint8_t kind;
    bool truth;
};

// The events one call of the builder's makes, at most; any past them are dropped.
#define BUSTER_CHECK_CFG_EVENTS 64u

// A CFG build's observer: tryEvaluateBool's cache of the logical and equality operators it has decided, a byte a node
// (0 for none, else 1 plus the value), and the events since the last flush. Null where nothing observes (the pruning).
typedef struct CheckCfgObserver CheckCfgObserver;
struct CheckCfgObserver
{
    uint8_t* cache;
    CheckCfgEvent events[BUSTER_CHECK_CFG_EVENTS];
    uint32_t count;
};

static void check_cfg_event(CheckCfgObserver* observer, uint32_t node, CheckCfgEventKind kind, bool truth)
{
    if (observer != NULL && observer->count < BUSTER_CHECK_CFG_EVENTS)
    {
        observer->events[observer->count] = (CheckCfgEvent){ .node = node, .kind = (uint8_t)kind, .truth = truth };
        observer->count += 1;
    }
}

static TypeId check_cfg_type(const CheckProgram* program, uint32_t node)
{
    const TypeTable* types = program->types;
    return type_unqualified(type_strip_aligned(types, check_stream_type(program->stream, node)));
}

// Whether parentheses enclose node `node` where its parent reads it (clang's ParenExpr): its token's parse-tree parent is
// a group.
static bool check_cfg_parenthesized(const CheckProgram* program, uint32_t node)
{
    const Parser* parser = program->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t token = node < program->input->graph.count ? program->input->graph.tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t parent = token < count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    return parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP;
}

// Whether CONDITION node `node` is a do statement's: its `)` is its statement's terminator's, where an if's, a while's and
// a switch's is its body's.
static bool check_cfg_do_condition(const CheckProgram* program, uint32_t node)
{
    const Parser* parser = program->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t token = node < program->input->graph.count ? program->input->graph.tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t parent = token < count && parser->tree.roles[token] == PARSE_ROLE_CLOSE_CONDITION ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    return parent < count && parser->tree.roles[parent] == PARSE_ROLE_TERMINATOR;
}

static bool check_cfg_logical(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    return op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
}

static bool check_cfg_comparison(CheckOp op)
{
    return op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL || op == CHECK_OP_LESS || op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER ||
           op == CHECK_OP_GREATER_EQUAL;
}

// The type the usual arithmetic conversions give two operands, 0 where either isn't a real arithmetic one (a pointer, a
// complex value: clang's analysis takes no constant of those).
static TypeId check_cfg_common_type(const CheckProgram* program, uint32_t left, uint32_t right)
{
    const TypeTable* types = program->types;
    const TypeId one = check_cfg_type(program, left);
    const TypeId two = check_cfg_type(program, right);
    const uint8_t kind_one = type_kind(types, one);
    const uint8_t kind_two = type_kind(types, two);
    const bool real = (kind_one == BUSTER_TYPE_KIND_INTEGER || kind_one == BUSTER_TYPE_KIND_ENUM || kind_one == BUSTER_TYPE_KIND_FLOATING) &&
                      (kind_two == BUSTER_TYPE_KIND_INTEGER || kind_two == BUSTER_TYPE_KIND_ENUM || kind_two == BUSTER_TYPE_KIND_FLOATING);
    // (The usual arithmetic conversions of two real types make no type: the table is only read.)
    const TypeId common = real ? type_usual_arithmetic((TypeTable*)types, one, two) : 0;
    return common != 0 && type_kind(types, common) != BUSTER_TYPE_KIND_COMPLEX ? type_unqualified(common) : 0;
}

// A node's constant integer value, its bits below its width (at most 128), its width and its signedness: a constant with
// no fault and no side effect (clang's EvaluateAsInt, SE_NoSideEffects).
static bool check_cfg_integer_value(const CheckProgram* program, uint32_t node, unsigned __int128* bits, uint32_t* width, bool* is_signed)
{
    const CheckStream* stream = program->stream;
    bool result = false;
    *bits = 0;
    *width = 0;
    *is_signed = false;
    if (node < program->input->graph.count && check_stream_constant_kind(stream, node) == CHECK_CONSTANT_INTEGER)
    {
        const CheckValue value = check_stream_value(stream, node);
        result = value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                 check_action_integer_type(program->types, value.type, width, is_signed) && *width != 0 && *width <= 128;
        *bits = result ? value.integer & check_action_mask(*width) : 0;
    }
    return result;
}

// clang's IntegerLiteral: a value at a number token whose constant is an integer.
static bool check_cfg_integer_literal(const CheckProgram* program, uint32_t node, unsigned __int128* bits, uint32_t* width, bool* is_signed)
{
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    const uint32_t token = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const bool number = token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_NUMBER;
    const bool result = number && check_cfg_integer_value(program, node, bits, width, is_signed);
    return result;
}

// clang's getIntegerLiteralSubexpressionValue: an integer literal past its parentheses, or one that `+`, `-`, `~` or `!`
// takes, its value computed in the literal's width (`!`'s in int's); its bits and width.
static bool check_cfg_literal_value(const CheckProgram* program, uint32_t node, unsigned __int128* bits, uint32_t* width)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT || op == CHECK_OP_NOT;
    bool is_signed = false;
    const bool result = check_cfg_integer_literal(program, unary ? graph->operand0[node] : node, bits, width, &is_signed);
    const unsigned __int128 mask = result ? check_action_mask(*width) : 0;
    if (result && op == CHECK_OP_NEGATIVE)
    {
        *bits = (0 - *bits) & mask;
    }
    else if (result && op == CHECK_OP_BIT_NOT)
    {
        *bits = ~*bits & mask;
    }
    else if (result && op == CHECK_OP_NOT)
    {
        *bits = *bits == 0 ? 1 : 0;
        *width = 32;
    }
    return result;
}

// clang's Expr::isKnownToHaveBooleanValue (its Semantic form): a _Bool's value, and an integer one that is a `!`'s, a
// comparison's or a logical operator's; a `+`'s, a comma's and an assignment's by their last operand, and a bitwise
// operator's and a conditional's where both of theirs are. An explicit cast is an arbitrary integer of its type.
static bool check_cfg_known_boolean(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    uint32_t stack[BUSTER_CHECK_CFG_DEPTH];
    uint32_t top = 1;
    stack[0] = node;
    bool result = node < graph->count;
    while (result && top != 0)
    {
        top -= 1;
        const uint32_t at = stack[top];
        const TypeId type = at < graph->count ? check_cfg_type(program, at) : 0;
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = type != 0 && (type_kind(types, type) == BUSTER_TYPE_KIND_ENUM || check_action_integer_type(types, type, &width, &is_signed));
        const CheckOp op = at < graph->count ? graph->ops[at] : CHECK_OP_COUNT;
        uint32_t pushed[2] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        if (type == BUSTER_TYPE_BOOL || op == CHECK_OP_NOT || (integer && (check_cfg_comparison(op) || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR)))
        {
            // (Boolean.)
        }
        else if (!integer)
        {
            result = false;
        }
        else if (op == CHECK_OP_POSITIVE || op == CHECK_OP_CONVERT)
        {
            pushed[0] = graph->operand0[at];
        }
        else if (op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR)
        {
            pushed[0] = graph->operand0[at];
            pushed[1] = graph->operand1[at];
        }
        else if (op == CHECK_OP_COMMA || op == CHECK_OP_ASSIGN)
        {
            pushed[0] = graph->operand1[at];
        }
        else if (op == CHECK_OP_CONDITIONAL && graph->operand1[at] < graph->count)
        {
            pushed[0] = graph->operand1[at];
            pushed[1] = graph->operand2[at];
        }
        else
        {
            result = false;
        }
        for (uint32_t i = 0; result && i < 2; i += 1)
        {
            if (pushed[i] != BUSTER_CHECK_NONE)
            {
                result = pushed[i] < graph->count && top < BUSTER_CHECK_CFG_DEPTH;
                stack[result ? top : 0] = pushed[i];
                top += result ? 1 : 0;
            }
        }
    }
    return result;
}

// What clang's tryTransformToLiteralConstant finds in an operand (BUSTER_CHECK_CFG_CONSTANT_*), seen with its implicit
// conversion to `converted` (0 where the caller strips them first, IgnoreParenImpCasts): a literal (an integer, character
// or floating one, or one that `-` takes, inside one cast that is an IntegralCast or an IntegralToFloating, implicit or
// written, and no parentheses past that cast), or an enumerator's name past parentheses and implicit conversions.
#define BUSTER_CHECK_CFG_CONSTANT_NONE 0u
#define BUSTER_CHECK_CFG_CONSTANT_LITERAL 1u
#define BUSTER_CHECK_CFG_CONSTANT_ENUMERATOR 2u

// Whether a cast from `from` to `to` is an IntegralCast or an IntegralToFloating (a cast to one's own type is a NoOp, and
// to _Bool an IntegralToBoolean).
static bool check_cfg_literal_cast(const TypeTable* types, TypeId from, TypeId to)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = check_action_integer_type(types, from, &width, &is_signed) || type_kind(types, from) == BUSTER_TYPE_KIND_ENUM;
    const bool target_integer = to != BUSTER_TYPE_BOOL && (check_action_integer_type(types, to, &width, &is_signed) || type_kind(types, to) == BUSTER_TYPE_KIND_ENUM);
    const bool target_floating = type_kind(types, to) == BUSTER_TYPE_KIND_FLOATING;
    return integer && from != to && (target_integer || target_floating);
}

static uint32_t check_cfg_literal_constant(const CheckProgram* program, uint32_t node, TypeId converted, uint32_t* declaration)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const TypeTable* types = program->types;
    const uint32_t count = parser->tokens.count;
    *declaration = BUSTER_CHECK_NONE;
    uint32_t result = BUSTER_CHECK_CFG_CONSTANT_NONE;
    if (node < graph->count)
    {
        const TypeId own = check_cfg_type(program, node);
        const bool implicit = converted != 0 && own != converted;
        // One cast: the implicit conversion, or else a written one; then no parentheses before what it converts.
        uint32_t at = node;
        bool cast = implicit;
        bool valid = !implicit || check_cfg_literal_cast(types, own, converted);
        if (!implicit && graph->ops[at] == CHECK_OP_CAST)
        {
            const uint32_t operand = graph->operand0[at];
            valid = operand < graph->count && check_cfg_literal_cast(types, check_cfg_type(program, operand), own);
            at = operand;
            cast = true;
        }
        valid = valid && at < graph->count && !(cast && check_cfg_parenthesized(program, at));
        // A `-`, then no parentheses before its literal.
        if (valid && graph->ops[at] == CHECK_OP_NEGATIVE)
        {
            at = graph->operand0[at];
            valid = at < graph->count && !check_cfg_parenthesized(program, at);
        }
        const uint32_t token = valid && graph->ops[at] == CHECK_OP_VALUE ? graph->tokens[at] : BUSTER_CHECK_NONE;
        const uint8_t kind = token < count ? parser->tokens.kinds[token] : 0;
        const CheckConstantKind constant = token < count ? check_stream_constant_kind(program->stream, at) : CHECK_CONSTANT_NONE;
        const bool literal = (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER) &&
                             (constant == CHECK_CONSTANT_INTEGER || constant == CHECK_CONSTANT_FLOATING);
        // An enumerator: the operand itself, past parentheses and implicit conversions alone.
        const uint32_t name = graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
        const uint32_t named = name < count && name < input->token_count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ?
                               input->declarations[name] : BUSTER_CHECK_NONE;
        const bool enumerator = named < input->token_count && input->declaration_kinds[named] == C_BINDING_ENUMERATOR;
        result = literal ? BUSTER_CHECK_CFG_CONSTANT_LITERAL : enumerator ? BUSTER_CHECK_CFG_CONSTANT_ENUMERATOR : BUSTER_CHECK_CFG_CONSTANT_NONE;
        *declaration = enumerator && !literal ? named : BUSTER_CHECK_NONE;
    }
    return result;
}

// A declaration's name token's binding (input->declarations), for a value at an identifier: an object's, a function's
// or an enumerator's. BUSTER_CHECK_NONE for any other node.
static uint32_t check_cfg_named(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    const uint32_t token = node < graph->count && graph->ops[node] == CHECK_OP_VALUE ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const bool identifier = token < parser->tokens.count && token < input->token_count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
    return identifier && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
}

// Whether two operands are the same, as clang's Expr::isSameComparisonOperand takes them past the same implicit
// conversions (`converted` false where their conversions differ, which leaves names alone): one name each of the same
// declaration; members of the same name (`s.m`, `p->m`, `s.a.b`) whose bases are the same name or the same members; or
// elements (`a[1]`, `s.a[i]`) of the same bases whose indices are integer literals of one value, unparenthesized, or else
// the same operands. A stack of 16 pairs, as the fallthrough reader's (check_input_fallthrough_same_operand_converted).
static bool check_cfg_same_operand(const CheckProgram* program, uint32_t one, uint32_t two, bool converted)
{
    const CheckGraph* graph = &program->input->graph;
    const Parser* parser = program->parser;
    uint32_t pairs[16][2];
    // A pair's operands: any of the three (0); a member chain's bases, names or members alone (1); names alone (2).
    uint8_t modes[16];
    uint32_t depth = 1;
    pairs[0][0] = one;
    pairs[0][1] = two;
    modes[0] = converted ? 0 : 2;
    bool same = true;
    while (same && depth != 0)
    {
        depth -= 1;
        const uint32_t a = pairs[depth][0];
        const uint32_t b = pairs[depth][1];
        const uint8_t mode = modes[depth];
        const CheckOp op_a = a < graph->count ? graph->ops[a] : CHECK_OP_COUNT;
        const CheckOp op_b = b < graph->count ? graph->ops[b] : CHECK_OP_COUNT;
        const uint32_t name_a = check_cfg_named(program, a);
        const uint32_t name_b = check_cfg_named(program, b);
        const bool member_a = op_a == CHECK_OP_MEMBER || op_a == CHECK_OP_ARROW;
        const bool member_b = op_b == CHECK_OP_MEMBER || op_b == CHECK_OP_ARROW;
        if (name_a != BUSTER_CHECK_NONE || name_b != BUSTER_CHECK_NONE)
        {
            same = name_a == name_b;
        }
        else if (mode != 2 && member_a && member_b && depth < 16)
        {
            // The members' names (the token past each operator), then their bases.
            const uint32_t token_a = graph->tokens[a] + 1;
            const uint32_t token_b = graph->tokens[b] + 1;
            same = token_a < parser->tokens.count && token_b < parser->tokens.count && parser->tokens.names[token_a] == parser->tokens.names[token_b];
            pairs[depth][0] = graph->operand0[a];
            pairs[depth][1] = graph->operand0[b];
            modes[depth] = 1;
            depth += 1;
        }
        else if (mode == 0 && op_a == CHECK_OP_SUBSCRIPT && op_b == CHECK_OP_SUBSCRIPT && depth + 2 <= 16)
        {
            // The bases, and the indices unless both are integer literals, which must have one value.
            const uint32_t index_a = graph->operand1[a];
            const uint32_t index_b = graph->operand1[b];
            unsigned __int128 bits_a = 0;
            unsigned __int128 bits_b = 0;
            uint32_t width = 0;
            bool is_signed = false;
            const bool literals = !check_cfg_parenthesized(program, index_a) && !check_cfg_parenthesized(program, index_b) &&
                                  check_cfg_integer_literal(program, index_a, &bits_a, &width, &is_signed) &&
                                  check_cfg_integer_literal(program, index_b, &bits_b, &width, &is_signed);
            same = !literals || bits_a == bits_b;
            pairs[depth][0] = graph->operand0[a];
            pairs[depth][1] = graph->operand0[b];
            modes[depth] = 0;
            pairs[depth + 1][0] = index_a;
            pairs[depth + 1][1] = index_b;
            modes[depth + 1] = 0;
            depth += literals ? 1 : 2;
        }
        else
        {
            same = false;
        }
    }
    return same;
}

// The implicit conversions clang gives a comparison's operand of type `type` to the comparison's type `common`, as a code
// two operands share where their conversions' kinds are the same (check_input_fallthrough_casts'): an integer promotion's
// IntegralCast, then the conversion's (merged into the promotion's where it is an IntegralCast too), IntegralToFloating or
// FloatingCast.
static uint32_t check_cfg_casts(const TypeTable* types, TypeId type, TypeId common)
{
    const TypeId promoted = type_promote(types, type);
    const bool floating = type_kind(types, promoted) == BUSTER_TYPE_KIND_FLOATING;
    const uint32_t inner = promoted != type ? 1 : 0;
    const uint32_t outer = common == promoted ? 0 : type_kind(types, common) != BUSTER_TYPE_KIND_FLOATING ? 1 : floating ? 3 : 2;
    return inner == outer ? outer : inner | outer << 2;
}

// One comparison of checkIncorrectLogicOperator's, normalized as `variable OP constant` (tryNormalizeBinaryOperator: the
// right operand's constant first, the operator flipped where it is the left one's): its variable, the conversions it
// takes (check_cfg_casts), its constant's kind (BUSTER_CHECK_CFG_CONSTANT_*) and enumerator, and the constant's value as
// clang's evaluator gives it, a literal's converted to the comparison's type and an enumerator's in its own: an
// integer's bits below its width and its signedness, or a floating one's value and type.
typedef struct CheckCfgComparison CheckCfgComparison;
struct CheckCfgComparison
{
    uint32_t variable;
    uint32_t casts;
    uint32_t constant;
    uint32_t enumerator;
    uint32_t width;
    TypeId type;
    unsigned __int128 bits;
    long double real;
    CheckOp op;
    bool floating;
    bool is_signed;
};

// A value rounded to _Float16 (to nearest, ties to even, as APFloat rounds; past the largest finite value an infinity).
static long double check_cfg_half(long double value)
{
    return check_action_half_value(CHECK_HALF_IEEE, check_action_half_bits(CHECK_HALF_IEEE, value));
}

static bool check_cfg_normalize(const CheckProgram* program, uint32_t node, CheckCfgComparison* comparison)
{
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const TypeId common = left < graph->count && right < graph->count ? check_cfg_common_type(program, left, right) : 0;
    const bool floating = common != 0 && type_kind(types, common) == BUSTER_TYPE_KIND_FLOATING;
    uint32_t enumerator = BUSTER_CHECK_NONE;
    uint32_t found = common != 0 ? check_cfg_literal_constant(program, right, common, &enumerator) : BUSTER_CHECK_CFG_CONSTANT_NONE;
    const bool on_right = found != BUSTER_CHECK_CFG_CONSTANT_NONE;
    found = !on_right && common != 0 ? check_cfg_literal_constant(program, left, common, &enumerator) : found;
    const uint32_t constant = on_right ? right : left;
    CheckOp flipped = op;
    if (!on_right)
    {
        flipped = op == CHECK_OP_GREATER ? CHECK_OP_LESS : op == CHECK_OP_GREATER_EQUAL ? CHECK_OP_LESS_EQUAL : op == CHECK_OP_LESS ? CHECK_OP_GREATER :
                  op == CHECK_OP_LESS_EQUAL ? CHECK_OP_GREATER_EQUAL : op;
    }
    const uint32_t variable = on_right ? left : right;
    // The constant's value: an enumerator's in its type, a literal's converted to the comparison's.
    const CheckValue value = found != BUSTER_CHECK_CFG_CONSTANT_NONE ? check_stream_value(program->stream, constant) : (CheckValue){ 0 };
    const TypeId own = check_cfg_type(program, constant);
    const TypeId valued = found == BUSTER_CHECK_CFG_CONSTANT_ENUMERATOR ? own : common;
    uint32_t width = 0;
    bool is_signed = false;
    const bool known = found != BUSTER_CHECK_CFG_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE;
    const bool integer = known && !(found == BUSTER_CHECK_CFG_CONSTANT_LITERAL && floating) && value.constant_kind == CHECK_CONSTANT_INTEGER &&
                         check_action_integer_type(types, valued, &width, &is_signed) && width != 0 && width <= 128;
    long double real = value.floating;
    bool real_known = known && found == BUSTER_CHECK_CFG_CONSTANT_LITERAL && floating && value.constant_kind == CHECK_CONSTANT_FLOATING;
    if (known && found == BUSTER_CHECK_CFG_CONSTANT_LITERAL && floating && value.constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t literal_width = 0;
        bool literal_signed = false;
        real_known = check_action_integer_type(types, value.type, &literal_width, &literal_signed) && literal_width != 0 && literal_width <= 64;
        // The integer's value, sign-extended where it is signed, rounded to the comparison's type.
        const uint32_t shift = real_known ? 64 - literal_width : 0;
        const uint64_t bits = (uint64_t)value.integer << shift;
        const long double wide = literal_signed ? (long double)((int64_t)bits >> shift) : (long double)(bits >> shift);
        real = common == BUSTER_TYPE_FLOAT16 ? check_cfg_half(wide) : common == BUSTER_TYPE_FLOAT ? (long double)(float)wide :
               common == BUSTER_TYPE_DOUBLE ? (long double)(double)wide : wide;
    }
    // An integer converted to the comparison's type: its own value, sign-extended from its own width where it is signed.
    unsigned __int128 bits = 0;
    if (integer)
    {
        uint32_t own_width = 0;
        bool own_signed = false;
        const bool own_integer = check_action_integer_type(types, value.type, &own_width, &own_signed) && own_width != 0 && own_width <= 128;
        const uint32_t shift = own_integer ? 128 - own_width : 0;
        const unsigned __int128 raw = value.integer << shift;
        const unsigned __int128 extended = own_signed ? (unsigned __int128)((__int128)raw >> shift) : raw >> shift;
        bits = extended & check_action_mask(width);
    }
    *comparison = (CheckCfgComparison){
        .variable = variable,
        .casts = common != 0 ? check_cfg_casts(types, check_cfg_type(program, variable), common) : 0,
        .constant = found,
        .enumerator = enumerator,
        .width = width,
        .type = found == BUSTER_CHECK_CFG_CONSTANT_LITERAL && floating ? common : 0,
        .bits = bits,
        .real = real,
        .op = flipped,
        .floating = real_known,
        .is_signed = is_signed,
    };
    return integer || real_known;
}

// Whether two enumerators are of one enumeration (clang's areExprTypesCompatible): no brace or parenthesis closes
// between their declarations that one before the first didn't open.
static bool check_cfg_same_enum(const CheckProgram* program, uint32_t one, uint32_t two)
{
    const Parser* parser = program->parser;
    const uint32_t low = one < two ? one : two;
    const uint32_t high = one < two ? two : one;
    int64_t depth = 0;
    bool same = true;
    for (uint32_t token = low; same && token < high && token < parser->tokens.count; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        depth += kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN ? 1 : kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN ? -1 : 0;
        same = depth >= 0;
    }
    return same && depth == 0;
}

// A comparison of two values of a width and signedness (their bits below the width), by a comparison's operator.
static bool check_cfg_compare(CheckOp op, unsigned __int128 left, unsigned __int128 right, uint32_t width, bool is_signed)
{
    const uint32_t shift = 128 - width;
    const __int128 signed_left = (__int128)(left << shift) >> shift;
    const __int128 signed_right = (__int128)(right << shift) >> shift;
    const bool less = is_signed ? signed_left < signed_right : left < right;
    const bool equal = left == right;
    return op == CHECK_OP_EQUAL ? equal : op == CHECK_OP_NOT_EQUAL ? !equal : op == CHECK_OP_LESS ? less : op == CHECK_OP_LESS_EQUAL ? less || equal :
           op == CHECK_OP_GREATER ? !less && !equal : !less;
}

// A floating comparison of a value with a constant (APFloat's operators: an unordered one is false but for !=).
static bool check_cfg_compare_real(CheckOp op, long double left, long double right)
{
    return op == CHECK_OP_EQUAL ? left == right : op == CHECK_OP_NOT_EQUAL ? left != right : op == CHECK_OP_LESS ? left < right :
           op == CHECK_OP_LESS_EQUAL ? left <= right : op == CHECK_OP_GREATER ? left > right : left >= right;
}

// checkIncorrectLogicOperator's five values (AnalyzeConditions): its truth (1, 0 or BUSTER_CHECK_CFG_UNKNOWN), and in
// `*reported` whether clang's observer hears it: neither comparison alone is always true or always false.
static uint8_t check_cfg_analyze(bool either, const CheckCfgComparison* one, const CheckCfgComparison* two, const unsigned __int128* integers,
                                 const long double* reals, bool* reported)
{
    bool always_true = true;
    bool always_false = true;
    bool one_true = true;
    bool one_false = true;
    bool two_true = true;
    bool two_false = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        const bool first = reals != NULL ? check_cfg_compare_real(one->op, reals[i], reals[1]) :
                                           check_cfg_compare(one->op, integers[i], integers[1], one->width, one->is_signed);
        const bool second = reals != NULL ? check_cfg_compare_real(two->op, reals[i], reals[3]) :
                                            check_cfg_compare(two->op, integers[i], integers[3], one->width, one->is_signed);
        const bool combined = either ? first || second : first && second;
        always_true &= combined;
        always_false &= !combined;
        one_true &= first;
        one_false &= !first;
        two_true &= second;
        two_false &= !second;
    }
    *reported = (always_true || always_false) && !one_true && !one_false && !two_true && !two_false;
    return always_true ? 1u : always_false ? 0u : BUSTER_CHECK_CFG_UNKNOWN;
}

// checkIncorrectLogicOperator's floating case: both constants in the wider of their types (_Float16, float, double or
// long double; __bf16's and __float128's are unknown), and the values the smallest negative subnormal, each constant,
// their mean (computed in that type) and the largest finite value.
static uint8_t check_cfg_real_tautology(bool either, const CheckCfgComparison* one, const CheckCfgComparison* two, bool* reported)
{
    const uint32_t rank_one = one->type == BUSTER_TYPE_FLOAT16 ? 1 : one->type == BUSTER_TYPE_FLOAT ? 2 : one->type == BUSTER_TYPE_DOUBLE ? 3 :
                              one->type == BUSTER_TYPE_LONG_DOUBLE ? 4 : 0;
    const uint32_t rank_two = two->type == BUSTER_TYPE_FLOAT16 ? 1 : two->type == BUSTER_TYPE_FLOAT ? 2 : two->type == BUSTER_TYPE_DOUBLE ? 3 :
                              two->type == BUSTER_TYPE_LONG_DOUBLE ? 4 : 0;
    const uint32_t rank = rank_one > rank_two ? rank_one : rank_two;
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    *reported = false;
    if (rank_one != 0 && rank_two != 0)
    {
        const long double first = one->real;
        const long double second = two->real;
        long double mean = 0;
        long double smallest = 0;
        long double largest = 0;
        if (rank == 1)
        {
            // Two _Float16 values' sum and half of one are exact in long double, and each is rounded once.
            mean = check_cfg_half(check_cfg_half(first + second) / 2.0L);
            smallest = -0x1p-24L;
            largest = 65504.0L;
        }
        else if (rank == 2)
        {
            const float sum = (float)first + (float)second;
            mean = sum / 2.0f;
            smallest = -0x1p-149L;
            largest = 0x1.fffffep127L;
        }
        else if (rank == 3)
        {
            const double sum = (double)first + (double)second;
            mean = sum / 2.0;
            smallest = -0x1p-1074L;
            largest = 0x1.fffffffffffffp1023L;
        }
        else
        {
            mean = (first + second) / 2.0L;
            smallest = -0x1p-16445L;
            largest = 0x1.fffffffffffffffep16383L;
        }
        const long double values[5] = { smallest, first, mean, second, largest };
        result = check_cfg_analyze(either, one, two, NULL, values, reported);
    }
    return result;
}

// clang's checkIncorrectLogicOperator, of an `&&` or `||` whose operands' values are unknown: an operand's negation of
// the other (`x || !x`, true; `x && !x`, false), or two comparisons of the same variable with constants (`x > 5 && x <
// 3`, `s.m == A || s.m != B`, `d > 5.0 && d < 3.0`): literals or enumerators of one enumeration, both integers of one
// width and signedness, whose value is the same at the smallest value, each constant, the one past the smaller constant
// and the largest, or both floating (check_cfg_real_tautology). 1, 0 or BUSTER_CHECK_CFG_UNKNOWN, and its event.
static uint8_t check_cfg_logic(const CheckProgram* program, uint32_t node, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    const TypeTable* types = program->types;
    const bool either = graph->ops[node] == CHECK_OP_LOGICAL_OR;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    // A `!`'s operand takes no promotion, a logical operator's does: their conversions differ where it promotes.
    bool negation = false;
    for (uint32_t side = 0; !negation && side < 2; side += 1)
    {
        const uint32_t negated = side == 0 ? left : right;
        const uint32_t other = side == 0 ? right : left;
        const uint32_t operand = negated < graph->count && graph->ops[negated] == CHECK_OP_NOT ? graph->operand0[negated] : BUSTER_CHECK_NONE;
        const TypeId type = operand < graph->count ? check_cfg_type(program, operand) : 0;
        negation = operand < graph->count && other < graph->count && check_cfg_same_operand(program, operand, other, type_promote(types, type) == type);
    }
    if (negation)
    {
        result = either ? 1u : 0u;
        check_cfg_event(observer, node, CHECK_CFG_EVENT_NEGATION, either);
    }
    else if (left < graph->count && right < graph->count && check_cfg_comparison(graph->ops[left]) && check_cfg_comparison(graph->ops[right]))
    {
        CheckCfgComparison one = { 0 };
        CheckCfgComparison two = { 0 };
        const bool compared = check_cfg_normalize(program, left, &one) && check_cfg_normalize(program, right, &two) &&
                              check_cfg_same_operand(program, one.variable, two.variable, one.casts == two.casts) && one.constant == two.constant &&
                              (one.constant != BUSTER_CHECK_CFG_CONSTANT_ENUMERATOR || check_cfg_same_enum(program, one.enumerator, two.enumerator));
        bool reported = false;
        if (compared && one.floating && two.floating)
        {
            result = check_cfg_real_tautology(either, &one, &two, &reported);
        }
        else if (compared && !one.floating && !two.floating && one.width == two.width && one.is_signed == two.is_signed)
        {
            const uint32_t width = one.width;
            const unsigned __int128 mask = check_action_mask(width);
            const unsigned __int128 smallest = one.is_signed ? ((unsigned __int128)1 << (width - 1)) & mask : 0;
            const unsigned __int128 largest = one.is_signed ? mask >> 1 : mask;
            const unsigned __int128 lower = check_cfg_compare(CHECK_OP_LESS, one.bits, two.bits, width, one.is_signed) ? one.bits : two.bits;
            const unsigned __int128 values[5] = { smallest, one.bits, (lower + 1) & mask, two.bits, largest };
            result = check_cfg_analyze(either, &one, &two, values, NULL, &reported);
        }
        if (reported)
        {
            check_cfg_event(observer, node, CHECK_CFG_EVENT_OVERLAP, result == 1);
        }
    }
    return result;
}

// clang's checkIncorrectEqualityOperator: an integer literal (getIntegerLiteralSubexpressionValue's, with no implicit
// conversion) compared with a bitwise `&` or `|` of another that can't give it (`(x & 8) == 4`, its event), or with a
// boolean value it isn't (`(x < y) == 10`).
static uint8_t check_cfg_equality_checked(const CheckProgram* program, uint32_t node, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    const bool equal = graph->ops[node] == CHECK_OP_EQUAL;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const TypeId common = left < graph->count && right < graph->count ? check_cfg_common_type(program, left, right) : 0;
    unsigned __int128 literal = 0;
    uint32_t width = 0;
    const bool on_left = common != 0 && check_cfg_type(program, left) == common && check_cfg_literal_value(program, left, &literal, &width);
    const bool on_right = !on_left && common != 0 && check_cfg_type(program, right) == common && check_cfg_literal_value(program, right, &literal, &width);
    const uint32_t other = on_left ? right : left;
    const CheckOp bit_op = (on_left || on_right) && check_cfg_type(program, other) == common ? graph->ops[other] : CHECK_OP_COUNT;
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    if (bit_op == CHECK_OP_BIT_AND || bit_op == CHECK_OP_BIT_OR)
    {
        // The bitwise operator's literal, of its own type (its operands' common one): the left operand's first.
        const uint32_t bit_left = graph->operand0[other];
        const uint32_t bit_right = graph->operand1[other];
        const TypeId bit_type = check_cfg_type(program, other);
        unsigned __int128 mask = 0;
        uint32_t mask_width = 0;
        const bool known = (check_cfg_type(program, bit_left) == bit_type && check_cfg_literal_value(program, bit_left, &mask, &mask_width)) ||
                           (check_cfg_type(program, bit_right) == bit_type && check_cfg_literal_value(program, bit_right, &mask, &mask_width));
        const bool impossible = known && (bit_op == CHECK_OP_BIT_AND ? (mask & literal) != literal : (mask | literal) != literal);
        if (impossible)
        {
            result = equal ? 0u : 1u;
            check_cfg_event(observer, node, CHECK_CFG_EVENT_BITWISE_EQUALITY, !equal);
        }
    }
    else if ((on_left || on_right) && literal > 1 && check_cfg_known_boolean(program, other))
    {
        result = equal ? 0u : 1u;
    }
    return result;
}

// Whether check_cfg_equality_checked can answer with `literal` for its literal operand and `other` beside it: `literal`
// is one (check_cfg_literal_value), and `other` is a `&` or a `|`, or is known to be 0 or 1 where the literal is over 1.
// Of the operands alone, no type of theirs worked out but a known boolean's own.
static bool check_cfg_equality_can(const CheckProgram* program, uint32_t literal, uint32_t other)
{
    const CheckGraph* graph = &program->input->graph;
    unsigned __int128 bits = 0;
    uint32_t width = 0;
    const bool found = literal < graph->count && other < graph->count && check_cfg_literal_value(program, literal, &bits, &width);
    return found && (graph->ops[other] == CHECK_OP_BIT_AND || graph->ops[other] == CHECK_OP_BIT_OR || (bits > 1 && check_cfg_known_boolean(program, other)));
}

// check_cfg_equality_checked's answer. It answers only where one operand is a literal and the other is a `&` or a `|`, or
// is known to be 0 or 1 beside a literal over 1: questions of the operands alone, which are asked first (#2850). Most
// equalities fail both, and their operands' common type is then never worked out. (With a test's switch the long way
// runs all the same and the two are compared, CheckInput.default_path_counts.)
static uint8_t check_cfg_equality(const CheckProgram* program, uint32_t node, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    uint64_t* const counts = program->input->default_path_counts;
    const bool can = check_cfg_equality_can(program, left, right) || check_cfg_equality_can(program, right, left);
    uint8_t answer = BUSTER_CHECK_CFG_UNKNOWN;
    if (can || __builtin_expect(counts != NULL, 0))
    {
        answer = check_cfg_equality_checked(program, node, observer);
        if (!can)
        {
            check_default_path_count(counts, CHECK_DEFAULT_PATH_COMPARISONS_EARLY, 1);
            check_default_path_count(counts, CHECK_DEFAULT_PATH_COMPARISONS_DIFFERENT, answer != BUSTER_CHECK_CFG_UNKNOWN);
        }
    }
    return answer;
}

// clang's checkIncorrectRelationalOperator: a boolean value compared with an integer literal (no implicit conversion)
// other than 0 and 1. A literal is never negative: `10 > b` and `b < 10` are true.
static uint8_t check_cfg_relational_checked(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const TypeId common = left < graph->count && right < graph->count ? check_cfg_common_type(program, left, right) : 0;
    unsigned __int128 literal = 0;
    uint32_t width = 0;
    bool is_signed = false;
    const bool literal_first = common != 0 && check_cfg_type(program, left) == common && check_cfg_integer_literal(program, left, &literal, &width, &is_signed);
    const bool literal_second = !literal_first && common != 0 && check_cfg_type(program, right) == common &&
                                check_cfg_integer_literal(program, right, &literal, &width, &is_signed);
    const bool boolean = literal_first ? check_cfg_known_boolean(program, right) : literal_second && check_cfg_known_boolean(program, left);
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    if (boolean && literal > 1)
    {
        const bool greater = op == CHECK_OP_GREATER || op == CHECK_OP_GREATER_EQUAL;
        result = greater ? (uint8_t)literal_first : (uint8_t)!literal_first;
    }
    return result;
}

// Whether check_cfg_relational_checked can answer with `literal` for its literal operand and `other` beside it: an
// integer literal over 1 beside a value known to be 0 or 1.
static bool check_cfg_relational_can(const CheckProgram* program, uint32_t literal, uint32_t other)
{
    const CheckGraph* graph = &program->input->graph;
    unsigned __int128 bits = 0;
    uint32_t width = 0;
    bool is_signed = false;
    const bool found = literal < graph->count && other < graph->count && check_cfg_integer_literal(program, literal, &bits, &width, &is_signed);
    return found && bits > 1 && check_cfg_known_boolean(program, other);
}

// check_cfg_relational_checked's answer, its two questions of the operands alone asked first, as check_cfg_equality's are.
static uint8_t check_cfg_relational(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    uint64_t* const counts = program->input->default_path_counts;
    const bool can = check_cfg_relational_can(program, left, right) || check_cfg_relational_can(program, right, left);
    uint8_t answer = BUSTER_CHECK_CFG_UNKNOWN;
    if (can || __builtin_expect(counts != NULL, 0))
    {
        answer = check_cfg_relational_checked(program, node);
        if (!can)
        {
            check_default_path_count(counts, CHECK_DEFAULT_PATH_COMPARISONS_EARLY, 1);
            check_default_path_count(counts, CHECK_DEFAULT_PATH_COMPARISONS_DIFFERENT, answer != BUSTER_CHECK_CFG_UNKNOWN);
        }
    }
    return answer;
}

// clang's checkIncorrectBitwiseOrOperator: a `|` of one literal constant (tryTransformToLiteralConstant's, past its
// parentheses and implicit conversions), whose integer value isn't 0, is true (its event).
static uint8_t check_cfg_bitwise_or(const CheckProgram* program, uint32_t node, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    uint32_t declaration = BUSTER_CHECK_NONE;
    const bool on_left = check_cfg_literal_constant(program, left, 0, &declaration) != BUSTER_CHECK_CFG_CONSTANT_NONE;
    const bool on_right = check_cfg_literal_constant(program, right, 0, &declaration) != BUSTER_CHECK_CFG_CONSTANT_NONE;
    unsigned __int128 bits = 0;
    uint32_t width = 0;
    bool is_signed = false;
    const bool known = on_left != on_right && check_cfg_integer_value(program, on_left ? left : right, &bits, &width, &is_signed);
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    if (known && bits != 0)
    {
        result = 1u;
        check_cfg_event(observer, node, CHECK_CFG_EVENT_BITWISE_OR, true);
    }
    return result;
}

// clang's evaluator's truth (check_program_fold_truth): 1, 0 or BUSTER_CHECK_CFG_UNKNOWN.
static uint8_t check_cfg_fold(const CheckProgram* program, uint32_t context, uint32_t node)
{
    bool truth = false;
    const bool known = node < program->input->graph.count && check_program_fold_truth(program, context, node, &truth);
    return known ? (uint8_t)truth : BUSTER_CHECK_CFG_UNKNOWN;
}

// tryEvaluateBool of one node that is no logical operator, or one that is parenthesized (`bare` false: a ParenExpr, which
// only the evaluator folds): `x * 0` and `x & 0` false where an operand is an integer constant 0; an equality's,
// a relational comparison's and a `|`'s checks; else the evaluator's truth. An equality's is cached where it is observed.
static uint8_t check_cfg_leaf(const CheckProgram* program, uint32_t context, uint32_t node, bool bare, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool cached = bare && observer != NULL && observer->cache != NULL && (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL);
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    if (cached && observer->cache[node] != 0)
    {
        result = (uint8_t)(observer->cache[node] - 1);
    }
    else
    {
        if (bare && (op == CHECK_OP_MULTIPLY || op == CHECK_OP_BIT_AND))
        {
            // An operand of integer type (its conversion's) that is an integer constant 0.
            unsigned __int128 bits = 0;
            uint32_t width = 0;
            bool is_signed = false;
            const TypeId type = check_cfg_type(program, node);
            const bool integer = check_action_integer_type(program->types, type, &width, &is_signed);
            const bool zero = integer && ((check_cfg_integer_value(program, graph->operand0[node], &bits, &width, &is_signed) && bits == 0) ||
                                          (check_cfg_integer_value(program, graph->operand1[node], &bits, &width, &is_signed) && bits == 0));
            result = zero ? 0u : BUSTER_CHECK_CFG_UNKNOWN;
        }
        else if (bare && (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL))
        {
            result = check_cfg_equality(program, node, observer);
        }
        else if (bare && (op == CHECK_OP_LESS || op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER || op == CHECK_OP_GREATER_EQUAL))
        {
            result = check_cfg_relational(program, node);
        }
        else if (bare && op == CHECK_OP_BIT_OR)
        {
            result = check_cfg_bitwise_or(program, node, observer);
        }
        result = result == BUSTER_CHECK_CFG_UNKNOWN ? check_cfg_fold(program, context, node) : result;
        if (cached)
        {
            observer->cache[node] = (uint8_t)(result + 1);
        }
    }
    return result;
}

// clang's CFGBuilder::tryEvaluateBool of node `node` (`bare` false where parentheses enclose it: a ParenExpr), the
// statement at `context`: a logical operator's by its operands' (each its own tryEvaluateBool, parenthesized or not), a
// known one that decides, or both known; where neither is known, checkIncorrectLogicOperator's; any other node's
// check_cfg_leaf's. 1, 0 or BUSTER_CHECK_CFG_UNKNOWN. A logical operator's is cached where it is observed, as clang caches
// it, so that its event comes once a build.
static uint8_t check_cfg_try(const CheckProgram* program, uint32_t context, uint32_t node, bool bare, CheckCfgObserver* observer)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t nodes[BUSTER_CHECK_CFG_DEPTH];
    uint8_t phases[BUSTER_CHECK_CFG_DEPTH];
    uint8_t lefts[BUSTER_CHECK_CFG_DEPTH];
    bool bares[BUSTER_CHECK_CFG_DEPTH];
    uint8_t* cache = observer != NULL ? observer->cache : NULL;
    uint32_t top = 1;
    nodes[0] = node;
    phases[0] = 0;
    bares[0] = bare;
    uint8_t result = BUSTER_CHECK_CFG_UNKNOWN;
    while (top != 0)
    {
        const uint32_t frame = top - 1;
        const uint32_t at = nodes[frame];
        const bool either = at < graph->count && graph->ops[at] == CHECK_OP_LOGICAL_OR;
        if (phases[frame] == 0)
        {
            const bool logical = bares[frame] && check_cfg_logical(program, at);
            if (logical && cache != NULL && cache[at] != 0)
            {
                result = (uint8_t)(cache[at] - 1);
                top -= 1;
            }
            else if (logical && top < BUSTER_CHECK_CFG_DEPTH)
            {
                const uint32_t left = graph->operand0[at];
                phases[frame] = 1;
                nodes[top] = left;
                phases[top] = 0;
                bares[top] = !check_cfg_parenthesized(program, left);
                top += 1;
            }
            else
            {
                result = logical ? BUSTER_CHECK_CFG_UNKNOWN : check_cfg_leaf(program, context, at, bares[frame], observer);
                top -= 1;
            }
        }
        else if (phases[frame] == 1)
        {
            // The left operand's value: it decides, or the right operand is taken.
            lefts[frame] = result;
            if (result != BUSTER_CHECK_CFG_UNKNOWN && (result == 1) == either)
            {
                top -= 1;
            }
            else
            {
                const uint32_t right = graph->operand1[at];
                phases[frame] = 2;
                nodes[top] = right;
                phases[top] = 0;
                bares[top] = !check_cfg_parenthesized(program, right);
                top += 1;
            }
            if (cache != NULL && top == frame)
            {
                cache[at] = (uint8_t)(result + 1);
            }
        }
        else
        {
            const uint8_t left = lefts[frame];
            const uint8_t right = result;
            if (left != BUSTER_CHECK_CFG_UNKNOWN)
            {
                result = right == BUSTER_CHECK_CFG_UNKNOWN ? BUSTER_CHECK_CFG_UNKNOWN : either ? (uint8_t)(left | right) : (uint8_t)(left & right);
            }
            else if (right != BUSTER_CHECK_CFG_UNKNOWN)
            {
                result = (right == 1) == either ? right : BUSTER_CHECK_CFG_UNKNOWN;
            }
            else
            {
                result = check_cfg_logic(program, at, observer);
            }
            if (cache != NULL)
            {
                cache[at] = (uint8_t)(result + 1);
            }
            top -= 1;
        }
    }
    return result;
}

// Which branches of a logical operator clang's CFG builder keeps (VisitLogicalOperator), bit 0 the true one's and bit 1
// the false one's: a block for each operand that isn't a logical operator itself (past its parentheses), whose edges its
// tryEvaluateBool prunes, the right operand's by the operator's own where its own is unknown. Unobserved (the order of
// its calls is check_common_cfg_tautologies').
static uint8_t check_cfg_branches(const CheckProgram* program, uint32_t context, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    uint32_t nodes[BUSTER_CHECK_CFG_DEPTH];
    uint8_t phases[BUSTER_CHECK_CFG_DEPTH];
    uint8_t lefts[BUSTER_CHECK_CFG_DEPTH];
    uint32_t top = 1;
    nodes[0] = node;
    phases[0] = 0;
    uint8_t result = 3;
    bool returned = false;
    while (top != 0)
    {
        const uint32_t frame = top - 1;
        const uint32_t at = nodes[frame];
        const bool either = graph->ops[at] == CHECK_OP_LOGICAL_OR;
        bool pushed = false;
        if (!returned)
        {
            // The next operand: a logical operator of its own, or a block whose value prunes its edges.
            const bool right = phases[frame] != 0;
            const uint32_t operand = right ? graph->operand1[at] : graph->operand0[at];
            pushed = check_cfg_logical(program, operand) && top < BUSTER_CHECK_CFG_DEPTH;
            if (pushed)
            {
                nodes[top] = operand;
                phases[top] = 0;
                top += 1;
            }
            else
            {
                uint8_t value = operand < graph->count ? check_cfg_try(program, context, operand, true, NULL) : BUSTER_CHECK_CFG_UNKNOWN;
                if (value == BUSTER_CHECK_CFG_UNKNOWN && right)
                {
                    value = check_cfg_try(program, context, at, true, NULL);
                }
                result = value == 1 ? 1u : value == 0 ? 2u : 3u;
            }
        }
        if (!pushed)
        {
            returned = false;
            if (phases[frame] == 0)
            {
                lefts[frame] = result;
                phases[frame] = 1;
            }
            else
            {
                const bool left_true = (lefts[frame] & 1) != 0;
                const bool left_false = (lefts[frame] & 2) != 0;
                const bool right_true = (result & 1) != 0;
                const bool right_false = (result & 2) != 0;
                const bool can_true = either ? left_true || (left_false && right_true) : left_true && right_true;
                const bool can_false = either ? left_false && right_false : left_false || (left_true && right_false);
                result = (uint8_t)((can_true ? 1u : 0u) | (can_false ? 2u : 0u));
                top -= 1;
                returned = top != 0;
            }
        }
    }
    return result;
}

// A branch condition's truth as clang's CFG builder prunes its edges (#1447, #2990), which the -Wreturn-type walk, the
// runtime warnings' reachability (check_common_body_reached, check_common_operand_mask) and the uninitialized flow read:
// `condition` is a statement's condition (a CONDITION node, `node` itself), a conditional's (`node`'s first operand) or
// a logical operator's first operand (`node` the operator). A logical operator past its parentheses is its branches'
// (check_cfg_branches); a logical operator's other first operand is its tryEvaluateBool past its parentheses, and any
// other condition its tryEvaluateBool as written, whose parentheses stop the heuristics (`if ((x * 0))`). A do
// statement's condition is its tryEvaluateBool as written, a logical operator's too (VisitDoStmt), and GNU's `a ?: b`'s
// its evaluator's alone.
static bool check_program_branch_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const bool operand = (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR) && condition == graph->operand0[node];
    const bool statement = condition < graph->count && graph->ops[condition] == CHECK_OP_CONDITION;
    const bool whole = statement && check_cfg_do_condition(program, condition);
    // GNU's `a ?: b` (its middle operand its first): clang's condition is an OpaqueValueExpr, which only its evaluator folds.
    const bool opaque = op == CHECK_OP_CONDITIONAL && condition == graph->operand0[node] && graph->operand1[node] == condition;
    const uint32_t expression = statement ? graph->operand0[condition] : condition;
    uint8_t branches = 3;
    if (expression < graph->count && opaque)
    {
        const uint8_t value = check_cfg_fold(program, node, expression);
        branches = value == 1 ? 1u : value == 0 ? 2u : 3u;
    }
    else if (expression < graph->count && !whole && check_cfg_logical(program, expression))
    {
        branches = check_cfg_branches(program, node, expression);
    }
    else if (expression < graph->count)
    {
        const uint8_t value = check_cfg_try(program, node, expression, operand || !check_cfg_parenthesized(program, expression), NULL);
        branches = value == 1 ? 1u : value == 0 ? 2u : 3u;
    }
    *truth = branches == 1;
    return branches == 1 || branches == 2;
}
