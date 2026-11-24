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
static uint8_t check_cfg_equality(const CheckProgram* program, uint32_t node, CheckCfgObserver* observer)
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

// clang's checkIncorrectRelationalOperator: a boolean value compared with an integer literal (no implicit conversion)
// other than 0 and 1. A literal is never negative: `10 > b` and `b < 10` are true.
static uint8_t check_cfg_relational(const CheckProgram* program, uint32_t node)
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

// Which nodes a rule above may decide (#1340): a bit a node, clear where check_program_branch_truth's fold and
// check_cfg_try can only answer unknown and tell an observer nothing, so that a condition whose bit is clear costs its
// asker one load. The fold stays the one implementation of a verdict: the bit only says where it need not run. It is
// derived from the rules themselves, each test below a condition its rule can't succeed without:
//
// - The evaluator's truth (check_program_fold_truth) is known of a node with a constant (its kind in
//   `stream->value_constants`) or of a query builtin (check_program_runtime_truth's candidates), and otherwise only
//   through a CONDITION's, a CONVERT's, a CAST's or a `!`'s operand or a logical operator's two. An lvalue's address
//   constant is no constant here, since its truth is the value the program loads (check_action_truth), but an array's
//   with no object, which the bit keeps without asking whether it is an array.
// - `x * 0` and `x & 0` (check_cfg_leaf) need an operand that is an integer constant, and of value 0
//   (check_cfg_integer_value). A constant with no payload row is 0 (check_stream_value). Any other's value is asked
//   only where an operator that reads its operand's bit meets one (a CONDITION, a conversion, a `!`, a logical operator
//   or a conditional), which then settles the operand's bit too: a `flags & MASK` that is nobody's condition keeps its
//   bit and is never asked.
// - `x | 1` (check_cfg_bitwise_or) needs an operand that is an integer constant.
// - A comparison's two checks (check_cfg_equality, check_cfg_relational) need a literal on one side: an integer
//   constant, or what `+`, `-`, `~` or `!` takes (check_cfg_literal_value, asked here once the columns allow it). The
//   other side is then a `&` or a `|`, which a literal 0 decides only where it is a `|`, or a boolean value compared
//   with a literal above 1. A boolean value is a _Bool's, or one of the operators check_cfg_known_boolean answers for
//   or reads through. So `x == 0`, `p->n != 3` and `(flags & 8) == 0` decide nothing.
// - A logical operator (check_cfg_try, check_cfg_branches) is its operands' tryEvaluateBool, each of which is one of the
//   rules here, or else checkIncorrectLogicOperator's (check_cfg_logic): an operand that is a `!` whose own operand and
//   the other are operands check_cfg_same_operand takes as the same (no constant is needed), or two comparisons that
//   each have a constant operand (check_cfg_normalize's literal or enumerator, whose value is an integer or a floating
//   constant) and whose other operands check_cfg_same_operand takes as the same.
// - Parentheses and a do statement's condition only choose which of these rules runs (the heuristics or the evaluator
//   alone, the branches or the whole operator), and a depth past BUSTER_CHECK_CFG_DEPTH only answers unknown: the bit
//   takes every rule as if it ran. A CONDITION node has its expression's bit (the evaluator reads through it), so a
//   statement's condition and a conditional's or a logical operator's first operand are asked by the same index.
// - The observer's events come from the same rules where they decide (check_cfg_equality's, check_cfg_bitwise_or's and
//   check_cfg_logic's), at a node that check_cfg_try reaches from the one it was asked through logical operators alone,
//   whose bits their parents take: a node whose bit is clear fires none.
//
// The pass (check_cfg_decidable), over the nodes 64 at a time:
//
// 1. The columns. The operators' narrowed to bytes and looked up in two 128-byte tables (vpermi2b) for what each
//    operator is to these rules; the constants', the categories', the objects', the payload rows' and the types'
//    compared. Together a byte of facts a node (BUSTER_CHECK_CFG_FACT_*), and the same facts as 64-bit masks.
// 2. The masks' rounds (check_cfg_decidable_rounds). In node order an operand's node is the one its operator's own
//    tokens follow, so an operator's operands are most often the one or two nodes before it (`!x`, `x == 0`,
//    `p->n & 8`, a CONDITION over its expression): the operand columns compared with each node's index less 1 and 2
//    say where, and there each rule is a few operations on the facts' masks shifted by 1 and 2, for 64 operators at
//    once.
// 3. The operators the rounds left (check_cfg_decidable_operators), in node order, where an operand is an earlier node
//    and so already has its facts: two byte loads and its rule's masks (check_cfg_operator_rules), without a branch on
//    the operator but for the few that have an exact question to ask (check_cfg_decidable_asked). These loads are
//    scalar because a zmm gather on Zen 5 is 64 uops, one every 12.6 cycles (docs/coding-style.md, "SIMD and Zen 5"),
//    against the two L1 loads it would replace, and each depends on the operator before. Before the rounds, all of
//    sqlite3.c's 58,607 operators took this loop: 2.2 M cycles on the 9700X, 37 an operator. With them it takes 0.9 M.
// 4. A last sweep takes the bits out of the facts, a word for each 64 nodes.
//
// On sqlite3.c (295,049 nodes): the bit keeps 522 of the 16,621 conditions (the CONDITION nodes and the logical
// operators' and conditionals' first operands), of which the fold decides 103. On the 9700X that takes 12.6 M
// instructions and 3 to 6 M cycles off the compile's 547 M (0.4 to 1.1 ms of 111.9, by the hold and the build), the
// pass's own cost about 2 M cycles (#1340's record).
#define BUSTER_CHECK_CFG_FACT_DECIDES 0x01u
// An integer constant; an integer or a floating one.
#define BUSTER_CHECK_CFG_FACT_INTEGER 0x02u
#define BUSTER_CHECK_CFG_FACT_NUMBER 0x04u
// What check_cfg_literal_value may take: an integer constant, or a `+`, `-`, `~` or `!`.
#define BUSTER_CHECK_CFG_FACT_LITERAL 0x08u
// A _Bool's value, or an operator check_cfg_known_boolean answers for without one or reads through.
#define BUSTER_CHECK_CFG_FACT_BOOLEAN 0x10u
#define BUSTER_CHECK_CFG_FACT_NOT 0x20u
// A comparison with a constant operand, which check_cfg_normalize may take.
#define BUSTER_CHECK_CFG_FACT_COMPARED 0x40u
// Before an operator's turn: its bit depends on its operands' facts. After it: `x * 0`'s rule may decide it, by an
// integer constant whose value nobody has asked yet (BUSTER_CHECK_CFG_FACT_DECIDES is then clear, and its bit is set).
#define BUSTER_CHECK_CFG_FACT_OPERANDS 0x80u
#define BUSTER_CHECK_CFG_FACT_UNASKED 0x80u
// The blocks of 64 nodes the pass takes at a time.
#define BUSTER_CHECK_CFG_CHUNK 64u
#define BUSTER_CHECK_CFG_FACT_LITERAL_SHIFT 3
#define BUSTER_CHECK_CFG_FACT_BOOLEAN_SHIFT 4

_Static_assert(sizeof(CheckOp) == sizeof(uint32_t), "the decidable pass narrows the operators' column from 32 bits");
_Static_assert(CHECK_OP_COUNT <= 128, "the decidable pass looks an operator up in a 128-byte table");
_Static_assert(CHECK_CONSTANT_NONE == 0 && CHECK_CONSTANT_ADDRESS < 128 && CHECK_LVALUE < 128, "the decidable pass compares the constants' and the categories' columns as bytes");
_Static_assert(BUSTER_CHECK_CFG_FACT_LITERAL == 1u << BUSTER_CHECK_CFG_FACT_LITERAL_SHIFT && BUSTER_CHECK_CFG_FACT_BOOLEAN == 1u << BUSTER_CHECK_CFG_FACT_BOOLEAN_SHIFT, "the facts' shifts");

static const uint8_t check_cfg_operator_facts[128] __attribute__((aligned(64))) = {
    [CHECK_OP_POSITIVE] = BUSTER_CHECK_CFG_FACT_LITERAL | BUSTER_CHECK_CFG_FACT_BOOLEAN,
    [CHECK_OP_NEGATIVE] = BUSTER_CHECK_CFG_FACT_LITERAL,
    [CHECK_OP_BIT_NOT] = BUSTER_CHECK_CFG_FACT_LITERAL,
    [CHECK_OP_NOT] = BUSTER_CHECK_CFG_FACT_LITERAL | BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_NOT | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_MULTIPLY] = BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_LESS] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_LESS_EQUAL] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_GREATER] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_EQUAL] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_NOT_EQUAL] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_BIT_AND] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_BIT_OR] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_BIT_XOR] = BUSTER_CHECK_CFG_FACT_BOOLEAN,
    [CHECK_OP_LOGICAL_AND] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_LOGICAL_OR] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_CONDITIONAL] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_CAST] = BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_ASSIGN] = BUSTER_CHECK_CFG_FACT_BOOLEAN,
    [CHECK_OP_COMMA] = BUSTER_CHECK_CFG_FACT_BOOLEAN,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_CFG_FACT_BOOLEAN | BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_CONDITION] = BUSTER_CHECK_CFG_FACT_OPERANDS,
    [CHECK_OP_BUILTIN_QUERY] = BUSTER_CHECK_CFG_FACT_DECIDES,
};

// How an operator with BUSTER_CHECK_CFG_FACT_OPERANDS takes its bit from its operands' facts, as masks over them:
// - `left` and `right`: the facts of each operand that the operator's bit follows: its bit and whether that is unasked
//   (the evaluator's and tryEvaluateBool's reads), or whether it is unasked alone (a conditional, whose first operand
//   is asked as a condition though the conditional's own truth isn't its).
// - `direct`: a fact of either operand that sets the bit (`|`'s integer constant).
// - `unasked`: one that sets it unasked (`*`'s and `&`'s integer constant).
// - `compared`: one that gives the operator BUSTER_CHECK_CFG_FACT_COMPARED (a comparison's constant).
// - `literal`: 1 where a literal operand beside a boolean one has a question (a comparison).
// - `negation` and `overlap`: a fact of either operand, and one of both, that have a question (a logical operator's `!`
//   and its two comparisons).
typedef struct CheckCfgOperatorRule CheckCfgOperatorRule;
struct CheckCfgOperatorRule
{
    uint8_t left;
    uint8_t right;
    uint8_t direct;
    uint8_t unasked;
    uint8_t compared;
    uint8_t literal;
    uint8_t negation;
    uint8_t overlap;
};

#define BUSTER_CHECK_CFG_RULE_READS (BUSTER_CHECK_CFG_FACT_DECIDES | BUSTER_CHECK_CFG_FACT_UNASKED)
#define BUSTER_CHECK_CFG_RULE_THROUGH { .left = BUSTER_CHECK_CFG_RULE_READS }
#define BUSTER_CHECK_CFG_RULE_COMPARISON { .compared = BUSTER_CHECK_CFG_FACT_NUMBER, .literal = 1 }
#define BUSTER_CHECK_CFG_RULE_LOGICAL { .left = BUSTER_CHECK_CFG_RULE_READS, .right = BUSTER_CHECK_CFG_RULE_READS, .negation = BUSTER_CHECK_CFG_FACT_NOT, .overlap = BUSTER_CHECK_CFG_FACT_COMPARED }
#define BUSTER_CHECK_CFG_RULE_PRODUCT { .unasked = BUSTER_CHECK_CFG_FACT_INTEGER }

_Static_assert(sizeof(CheckCfgOperatorRule) == sizeof(uint64_t), "an operator's rule is read as one word");

static const CheckCfgOperatorRule check_cfg_operator_rules[CHECK_OP_COUNT] = {
    [CHECK_OP_NOT] = BUSTER_CHECK_CFG_RULE_THROUGH,
    [CHECK_OP_CAST] = BUSTER_CHECK_CFG_RULE_THROUGH,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_CFG_RULE_THROUGH,
    [CHECK_OP_CONDITION] = BUSTER_CHECK_CFG_RULE_THROUGH,
    [CHECK_OP_CONDITIONAL] = { .left = BUSTER_CHECK_CFG_FACT_UNASKED },
    [CHECK_OP_LOGICAL_AND] = BUSTER_CHECK_CFG_RULE_LOGICAL,
    [CHECK_OP_LOGICAL_OR] = BUSTER_CHECK_CFG_RULE_LOGICAL,
    [CHECK_OP_MULTIPLY] = BUSTER_CHECK_CFG_RULE_PRODUCT,
    [CHECK_OP_BIT_AND] = BUSTER_CHECK_CFG_RULE_PRODUCT,
    [CHECK_OP_BIT_OR] = { .direct = BUSTER_CHECK_CFG_FACT_INTEGER },
    [CHECK_OP_LESS] = BUSTER_CHECK_CFG_RULE_COMPARISON,
    [CHECK_OP_LESS_EQUAL] = BUSTER_CHECK_CFG_RULE_COMPARISON,
    [CHECK_OP_GREATER] = BUSTER_CHECK_CFG_RULE_COMPARISON,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_CHECK_CFG_RULE_COMPARISON,
    [CHECK_OP_EQUAL] = BUSTER_CHECK_CFG_RULE_COMPARISON,
    [CHECK_OP_NOT_EQUAL] = BUSTER_CHECK_CFG_RULE_COMPARISON,
};

// An operand's facts for an operator at node `node`: an earlier node's, none where it has no operand, and every one but
// BUSTER_CHECK_CFG_FACT_UNASKED for an operand that isn't an earlier node, which no graph of the reader's has and of
// which the pass knows nothing. The last two are bytes the pass keeps past the nodes' (at `absent` and after it), so
// that the choice is an index's and one load follows, with no branch on whether an operator has a second operand.
static inline __attribute__((always_inline)) uint32_t check_cfg_operand_facts(const uint8_t* facts, uint32_t absent, uint32_t operand, uint32_t node)
{
    const uint32_t other = operand == BUSTER_CHECK_NONE ? absent : absent + 1;
    return facts[operand < node ? operand : other];
}

// Whether check_cfg_same_operand could take two operands as the same by their operators alone: two values, two members
// or two elements.
static bool check_cfg_decidable_alike(const CheckGraph* graph, uint32_t first, uint32_t second)
{
    const CheckOp one = first < graph->count ? graph->ops[first] : CHECK_OP_COUNT;
    const CheckOp two = second < graph->count ? graph->ops[second] : CHECK_OP_COUNT;
    const bool member_one = one == CHECK_OP_MEMBER || one == CHECK_OP_ARROW;
    const bool member_two = two == CHECK_OP_MEMBER || two == CHECK_OP_ARROW;
    return (one == CHECK_OP_VALUE && two == CHECK_OP_VALUE) || (member_one && member_two) || (one == CHECK_OP_SUBSCRIPT && two == CHECK_OP_SUBSCRIPT);
}

// Whether a comparison's side `literal`, whose facts say check_cfg_literal_value may take it, beside its other side
// `other`, whose facts say it may be a boolean value, may decide check_cfg_equality or check_cfg_relational. (The facts
// take every value for a boolean one in a unit with a wrapper of _Bool, check_cfg_wrapped_boolean: its type is read
// here.)
static bool check_cfg_decidable_comparison(const CheckProgram* program, uint32_t literal, uint32_t other)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = other < graph->count ? graph->ops[other] : CHECK_OP_COUNT;
    const bool boolean = op < CHECK_OP_COUNT && ((check_cfg_operator_facts[op] & BUSTER_CHECK_CFG_FACT_BOOLEAN) != 0 || check_cfg_type(program, other) == BUSTER_TYPE_BOOL);
    unsigned __int128 bits = 0;
    uint32_t width = 0;
    bool result = false;
    if (boolean && check_cfg_literal_value(program, literal, &bits, &width))
    {
        result = bits > 1 || op == CHECK_OP_BIT_OR || (op == CHECK_OP_BIT_AND && bits != 0);
    }
    return result;
}

// Whether a logical operator's operand `negated`, a `!`, is of an operand check_cfg_same_operand takes as the same as
// its other operand `other`, past the same conversions (which takes every pair it takes past different ones).
static bool check_cfg_decidable_negation(const CheckProgram* program, uint32_t negated, uint32_t other)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t operand = negated < graph->count ? graph->operand0[negated] : BUSTER_CHECK_NONE;
    return operand < graph->count && other < graph->count && check_cfg_decidable_alike(graph, operand, other) && check_cfg_same_operand(program, operand, other, true);
}

// Whether an operand is an integer constant 0, as check_cfg_leaf's `x * 0` and `x & 0` take it.
static bool check_cfg_decidable_zero(const CheckProgram* program, uint32_t operand)
{
    unsigned __int128 bits = 0;
    uint32_t width = 0;
    bool is_signed = false;
    return check_cfg_integer_value(program, operand, &bits, &width, &is_signed) && bits == 0;
}

// An unasked operand (a `*` or a `&` of an integer constant), asked: it decides where a constant operand of its is 0,
// and has no bit where none is. Whether it decides.
static bool check_cfg_decidable_ask(const CheckProgram* program, uint8_t* facts, uint32_t operand)
{
    const CheckGraph* graph = &program->input->graph;
    const bool zero = check_cfg_decidable_zero(program, graph->operand0[operand]) || check_cfg_decidable_zero(program, graph->operand1[operand]);
    facts[operand] = (uint8_t)((facts[operand] & ~(BUSTER_CHECK_CFG_FACT_UNASKED | BUSTER_CHECK_CFG_FACT_DECIDES)) | (zero ? BUSTER_CHECK_CFG_FACT_DECIDES : 0u));
    return zero;
}

// Whether check_cfg_logic's two comparisons could be of the same variable: an operand of each whose other is a constant
// (check_cfg_normalize's variable is one of them), which check_cfg_same_operand takes as the same past the same
// conversions (which takes every pair it takes past different ones).
static bool check_cfg_decidable_overlap(const CheckProgram* program, uint32_t left, uint32_t right)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    bool result = false;
    for (uint32_t pair = 0; !result && pair < 4; pair += 1)
    {
        const uint32_t one = (pair & 1) != 0 ? graph->operand1[left] : graph->operand0[left];
        const uint32_t one_constant = (pair & 1) != 0 ? graph->operand0[left] : graph->operand1[left];
        const uint32_t two = (pair & 2) != 0 ? graph->operand1[right] : graph->operand0[right];
        const uint32_t two_constant = (pair & 2) != 0 ? graph->operand0[right] : graph->operand1[right];
        const CheckConstantKind kind_one = one_constant < graph->count ? check_stream_constant_kind(stream, one_constant) : CHECK_CONSTANT_NONE;
        const CheckConstantKind kind_two = two_constant < graph->count ? check_stream_constant_kind(stream, two_constant) : CHECK_CONSTANT_NONE;
        result = (kind_one == CHECK_CONSTANT_INTEGER || kind_one == CHECK_CONSTANT_FLOATING) && (kind_two == CHECK_CONSTANT_INTEGER || kind_two == CHECK_CONSTANT_FLOATING) &&
                 check_cfg_decidable_alike(graph, one, two) && check_cfg_same_operand(program, one, two, true);
    }
    return result;
}

// The questions an operator at `node` has that its operands' facts don't answer (check_cfg_decidable_operators): an
// operand whose bit is unasked, a comparison's literal beside a boolean value, a logical operator's `!` or its two
// comparisons. Whether the operator's bit is set by any; every unasked operand is asked. An operand that is no earlier
// node sets it.
static __attribute__((noinline)) bool check_cfg_decidable_asked(const CheckProgram* program, uint8_t* facts, uint32_t absent, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp op = graph->ops[node];
    const CheckCfgOperatorRule rule = check_cfg_operator_rules[op];
    const uint32_t left = graph->operand0[node];
    const uint32_t right = graph->operand1[node];
    const uint32_t one = check_cfg_operand_facts(facts, absent, left, node);
    const uint32_t two = check_cfg_operand_facts(facts, absent, right, node);
    bool result = (left >= node && left != BUSTER_CHECK_NONE) || (right >= node && right != BUSTER_CHECK_NONE);
    if ((one & rule.left & BUSTER_CHECK_CFG_FACT_UNASKED) != 0)
    {
        const bool stays = check_cfg_decidable_ask(program, facts, left);
        result |= stays && (rule.left & BUSTER_CHECK_CFG_FACT_DECIDES) != 0;
    }
    // (One node as both operands is asked once: its facts are read again.)
    if ((check_cfg_operand_facts(facts, absent, right, node) & rule.right & BUSTER_CHECK_CFG_FACT_UNASKED) != 0)
    {
        const bool stays = check_cfg_decidable_ask(program, facts, right);
        result |= stays && (rule.right & BUSTER_CHECK_CFG_FACT_DECIDES) != 0;
    }
    if (!result && rule.literal != 0)
    {
        result = ((one & BUSTER_CHECK_CFG_FACT_LITERAL) != 0 && (two & BUSTER_CHECK_CFG_FACT_BOOLEAN) != 0 && check_cfg_decidable_comparison(program, left, right)) ||
                 ((two & BUSTER_CHECK_CFG_FACT_LITERAL) != 0 && (one & BUSTER_CHECK_CFG_FACT_BOOLEAN) != 0 && check_cfg_decidable_comparison(program, right, left));
    }
    if (!result && rule.negation != 0)
    {
        result = ((one & BUSTER_CHECK_CFG_FACT_NOT) != 0 && check_cfg_decidable_negation(program, left, right)) ||
                 ((two & BUSTER_CHECK_CFG_FACT_NOT) != 0 && check_cfg_decidable_negation(program, right, left)) ||
                 ((one & two & BUSTER_CHECK_CFG_FACT_COMPARED) != 0 && check_cfg_decidable_overlap(program, left, right));
    }
    return result;
}

// The operators of the blocks `first_word` up to `end_word` (their BUSTER_CHECK_CFG_FACT_OPERANDS lanes in `pending`,
// a word a block), in node order: each one's facts from its operands' by its rule's masks (check_cfg_operator_rules),
// with one branch, to the questions, which few take. A function of its own, with the columns' bases in registers.
static __attribute__((noinline)) void check_cfg_decidable_operators(const CheckProgram* program, uint8_t* facts, uint32_t absent, const uint64_t* pending,
                                                                     uint64_t first_word, uint64_t end_word)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckOp* ops = graph->ops;
    const uint32_t* operand0 = graph->operand0;
    const uint32_t* operand1 = graph->operand1;
    for (uint64_t word = first_word; word < end_word; word += 1)
    {
        uint64_t operators = pending[word - first_word];
        while (operators != 0)
        {
            const uint32_t node = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(operators);
            uint64_t rule = 0;
            memcpy(&rule, &check_cfg_operator_rules[ops[node]], sizeof(rule));
            const uint32_t rule_left = (uint8_t)rule;
            const uint32_t rule_right = (uint8_t)(rule >> 8);
            const uint32_t rule_direct = (uint8_t)(rule >> 16);
            const uint32_t rule_unasked = (uint8_t)(rule >> 24);
            const uint32_t rule_compared = (uint8_t)(rule >> 32);
            const uint32_t rule_literal = (uint8_t)(rule >> 40);
            const uint32_t rule_negation = (uint8_t)(rule >> 48);
            const uint32_t rule_overlap = (uint8_t)(rule >> 56);
            const uint32_t own = facts[node] & ~BUSTER_CHECK_CFG_FACT_OPERANDS;
            const uint32_t one = check_cfg_operand_facts(facts, absent, operand0[node], node);
            const uint32_t two = check_cfg_operand_facts(facts, absent, operand1[node], node);
            const uint32_t either = one | two;
            const uint32_t read_one = one & rule_left;
            const uint32_t read_two = two & rule_right;
            // The bit an operand gives, or a fact; then the questions, where neither sets it.
            const uint32_t sure = (own | read_one | read_two | ((either & rule_direct) != 0 ? 1u : 0u)) & BUSTER_CHECK_CFG_FACT_DECIDES;
            const uint32_t unasked = (either & rule_unasked) != 0 ? 1u : 0u;
            const uint32_t literal = (((one >> BUSTER_CHECK_CFG_FACT_LITERAL_SHIFT) & (two >> BUSTER_CHECK_CFG_FACT_BOOLEAN_SHIFT)) |
                                      ((two >> BUSTER_CHECK_CFG_FACT_LITERAL_SHIFT) & (one >> BUSTER_CHECK_CFG_FACT_BOOLEAN_SHIFT))) & rule_literal;
            const uint32_t questions = ((read_one | read_two) & BUSTER_CHECK_CFG_FACT_UNASKED) | literal | (either & rule_negation) | (one & two & rule_overlap);
            uint32_t decides = sure;
            if (__builtin_expect((questions & (sure - 1)) != 0, 0))
            {
                decides = check_cfg_decidable_asked(program, facts, absent, node) ? 1u : 0u;
            }
            facts[node] = (uint8_t)(own | decides | ((unasked & ~sure) << 7) | ((either & rule_compared) != 0 ? BUSTER_CHECK_CFG_FACT_COMPARED : 0u));
            operators &= operators - 1;
        }
    }
}

// What an operator is to the pass's masks (check_cfg_decidable_rounds), a bit a class: one the evaluator reads through, a
// comparison, a logical operator, `*` or `&`, and `|`.
#define BUSTER_CHECK_CFG_CLASS_THROUGH 0x01u
#define BUSTER_CHECK_CFG_CLASS_COMPARISON 0x02u
#define BUSTER_CHECK_CFG_CLASS_LOGICAL 0x04u
#define BUSTER_CHECK_CFG_CLASS_PRODUCT 0x08u
#define BUSTER_CHECK_CFG_CLASS_BIT_OR 0x10u

static const uint8_t check_cfg_operator_classes[128] __attribute__((aligned(64))) = {
    [CHECK_OP_NOT] = BUSTER_CHECK_CFG_CLASS_THROUGH,
    [CHECK_OP_CAST] = BUSTER_CHECK_CFG_CLASS_THROUGH,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_CFG_CLASS_THROUGH,
    [CHECK_OP_CONDITION] = BUSTER_CHECK_CFG_CLASS_THROUGH,
    [CHECK_OP_LESS] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_LESS_EQUAL] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_GREATER] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_EQUAL] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_NOT_EQUAL] = BUSTER_CHECK_CFG_CLASS_COMPARISON,
    [CHECK_OP_LOGICAL_AND] = BUSTER_CHECK_CFG_CLASS_LOGICAL,
    [CHECK_OP_LOGICAL_OR] = BUSTER_CHECK_CFG_CLASS_LOGICAL,
    [CHECK_OP_MULTIPLY] = BUSTER_CHECK_CFG_CLASS_PRODUCT,
    [CHECK_OP_BIT_AND] = BUSTER_CHECK_CFG_CLASS_PRODUCT,
    [CHECK_OP_BIT_OR] = BUSTER_CHECK_CFG_CLASS_BIT_OR,
};

// A block's 64 nodes' facts as masks, a bit a node: `settled` where the node's facts are final (every node that is no
// operator, and each operator the masks gave its facts), `zero` an integer constant with no payload row, whose value is
// 0 (check_stream_value), and `bit_or` a `|`.
typedef struct CheckCfgMasks CheckCfgMasks;
struct CheckCfgMasks
{
    uint64_t settled;
    uint64_t decides;
    uint64_t unasked;
    uint64_t compared;
    uint64_t integer;
    uint64_t number;
    uint64_t literal;
    uint64_t boolean;
    uint64_t negation;
    uint64_t zero;
    uint64_t bit_or;
};

// A mask of the nodes `distance` before each of a block's (1 or 2): the block's own shifted, and the block before's last.
static inline __attribute__((always_inline)) uint64_t check_cfg_before(uint64_t mask, uint64_t previous, uint32_t distance)
{
    return mask << distance | previous >> (64 - distance);
}

// The operators of a block that take their facts from the one or two nodes before them, by masks: an operator whose
// operand is the node before it (`adjacent0`: a `!x`, a condition's CONDITION over its expression's last node), or
// whose operands are the two nodes before it (`adjacent1` and `second0`: `x == 0`, `p->n & 8`, `a || b` of two
// leaves), each operand settled. In node order an operand is the node its operator's own tokens end at, so most
// operators are one of these. Each rule is its scalar form's (check_cfg_decidable_operators) on shifted masks, and an
// operator with a question for check_cfg_decidable_asked is left for it. Three rounds, since one operator's operand is
// often another the round before settled (a CONDITION over `!` over a value). `block` holds the nodes' own facts and
// `previous` the block before's; the masks of the settled operators are added to `block`.
static inline __attribute__((always_inline)) void check_cfg_decidable_rounds(CheckCfgMasks* block, const CheckCfgMasks* previous, uint64_t classes[5], uint64_t adjacent0,
                                                                             uint64_t adjacent1, uint64_t second0)
{
    const uint64_t integer1 = check_cfg_before(block->integer, previous->integer, 1);
    const uint64_t integer2 = check_cfg_before(block->integer, previous->integer, 2);
    const uint64_t number1 = check_cfg_before(block->number, previous->number, 1);
    const uint64_t number2 = check_cfg_before(block->number, previous->number, 2);
    const uint64_t literal1 = check_cfg_before(block->literal, previous->literal, 1);
    const uint64_t literal2 = check_cfg_before(block->literal, previous->literal, 2);
    const uint64_t boolean1 = check_cfg_before(block->boolean, previous->boolean, 1);
    const uint64_t boolean2 = check_cfg_before(block->boolean, previous->boolean, 2);
    const uint64_t negation1 = check_cfg_before(block->negation, previous->negation, 1);
    const uint64_t negation2 = check_cfg_before(block->negation, previous->negation, 2);
    const uint64_t zero1 = check_cfg_before(block->zero, previous->zero, 1);
    const uint64_t zero2 = check_cfg_before(block->zero, previous->zero, 2);
    const uint64_t bit_or1 = check_cfg_before(block->bit_or, previous->bit_or, 1);
    const uint64_t bit_or2 = check_cfg_before(block->bit_or, previous->bit_or, 2);
    // A comparison's literal beside a boolean value has a question, but a literal 0 beside what is no `|`.
    const uint64_t literal_question = (literal1 & boolean2 & ~(zero1 & ~bit_or2)) | (literal2 & boolean1 & ~(zero2 & ~bit_or1));
    uint64_t settled = block->settled;
    uint64_t decides = block->decides;
    uint64_t unasked = 0;
    uint64_t compared = 0;
    for (uint32_t round = 0; round < 3; round += 1)
    {
        const uint64_t settled1 = check_cfg_before(settled, previous->settled, 1);
        const uint64_t settled2 = check_cfg_before(settled, previous->settled, 2);
        const uint64_t decides1 = check_cfg_before(decides, previous->decides, 1);
        const uint64_t decides2 = check_cfg_before(decides, previous->decides, 2);
        const uint64_t unasked1 = check_cfg_before(unasked, previous->unasked, 1);
        const uint64_t unasked2 = check_cfg_before(unasked, previous->unasked, 2);
        const uint64_t compared1 = check_cfg_before(compared, previous->compared, 1);
        const uint64_t compared2 = check_cfg_before(compared, previous->compared, 2);
        const uint64_t through = classes[0] & ~settled & adjacent0 & settled1 & ~unasked1;
        const uint64_t binary = ~settled & adjacent1 & second0 & settled1 & settled2;
        const uint64_t comparison = classes[1] & binary & ~literal_question;
        const uint64_t logical = classes[2] & binary & ~(unasked1 | unasked2 | negation1 | negation2 | (compared1 & compared2));
        const uint64_t product = classes[3] & binary;
        const uint64_t bit_or = classes[4] & binary;
        decides |= (through & decides1) | (logical & (decides1 | decides2)) | (product & (zero1 | zero2)) | (bit_or & (integer1 | integer2));
        unasked |= product & (integer1 | integer2) & ~(zero1 | zero2);
        compared |= comparison & (number1 | number2);
        settled |= through | comparison | logical | product | bit_or;
    }
    block->settled = settled;
    block->decides = decides;
    block->unasked = unasked;
    block->compared = compared;
}

// Whether an ALIGNED or QUALIFIED wrapper of the table's is _Bool's, whose values' types the pass can't tell by their
// own bits: the wrappers' kinds found 64 at a time, each one's base looked up (a unit has few, most none).
static bool check_cfg_wrapped_boolean(const TypeTable* types)
{
    const uint32_t count = types->types.count;
    const uint8_t* kinds = types->types.kind;
    bool result = false;
    for (uint32_t first = 0; !result && first < count; first += 64)
    {
        const uint32_t left = count - first;
        const uint64_t lanes = left >= 64 ? UINT64_MAX : (1ull << left) - 1;
        uint64_t wrappers = _mm512_cmpge_epu8_mask(_mm512_maskz_loadu_epi8(lanes, kinds + first), _mm512_set1_epi8(BUSTER_TYPE_KIND_WRAPPERS)) & lanes;
        while (!result && wrappers != 0)
        {
            const uint32_t index = first + (uint32_t)__builtin_ctzll(wrappers);
            result = (TypeId)type_unwrapped_index(types, index) << BUSTER_TYPE_INDEX_SHIFT == BUSTER_TYPE_BOOL;
            wrappers &= wrappers - 1;
        }
    }
    return result;
}

// The bits: a word for each 64 nodes, in `output`; null where the stream has no columns (a hand-built one) or no room is
// left, which asks every condition's fold. `program`'s input, stream, types and parser are set.
static const uint64_t* check_cfg_decidable(const CheckProgram* program, Arena* output, Arena* scratch)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t count = graph->count;
    const CheckOp* ops = graph->ops;
    const uint8_t* constants = stream->value_constants;
    const uint8_t* categories = stream->value_categories;
    const uint32_t* objects = stream->value_objects;
    const TypeId* value_types = stream->value_types;
    const uint32_t* payload_rows = stream->payload_rows;
    const uint32_t* operand0 = graph->operand0;
    const uint32_t* operand1 = graph->operand1;
    const bool columns = count != 0 && stream->dense_values == NULL && constants != NULL && categories != NULL && objects != NULL && value_types != NULL &&
                         payload_rows != NULL;
    const uint64_t words = ((uint64_t)count + 63) / 64;
    const uint64_t mark = scratch->length;
    uint64_t* bits = columns ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, words) : NULL;
    // (The facts are stored a whole block at a time, and two bytes follow them: check_cfg_operand_facts' two.)
    const uint32_t absent = (uint32_t)(words * 64);
    uint8_t* facts = bits != NULL && words * 64 + 2 < BUSTER_CHECK_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, words * 64 + 2) : NULL;
    if (facts != NULL)
    {
        facts[absent] = 0;
        facts[absent + 1] = (uint8_t)~BUSTER_CHECK_CFG_FACT_UNASKED;
        const __m512i table_low = _mm512_load_si512(check_cfg_operator_facts);
        const __m512i table_high = _mm512_load_si512(check_cfg_operator_facts + 64);
        const __m512i class_low = _mm512_load_si512(check_cfg_operator_classes);
        const __m512i class_high = _mm512_load_si512(check_cfg_operator_classes + 64);
        const __m512i decides = _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_DECIDES);
        const __m512i integer = _mm512_set1_epi8((char)(BUSTER_CHECK_CFG_FACT_INTEGER | BUSTER_CHECK_CFG_FACT_NUMBER | BUSTER_CHECK_CFG_FACT_LITERAL));
        const __m512i number = _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_NUMBER);
        const __m512i boolean = _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_BOOLEAN);
        const __m512i compared = _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_COMPARED);
        const __m512i operands = _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_OPERANDS);
        const __m512i unqualified = _mm512_set1_epi32((int32_t)~BUSTER_TYPE_QUALIFIERS);
        const __m512i boolean_type = _mm512_set1_epi32((int32_t)BUSTER_TYPE_BOOL);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i lane_before = _mm512_setr_epi32(-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
        const __m512i one = _mm512_set1_epi32(1);
        // With a wrapper of _Bool in the table, any value may be a boolean one.
        const uint64_t wrapped = check_cfg_wrapped_boolean(program->types) ? UINT64_MAX : 0;
        CheckCfgMasks previous = { 0 };
        // A chunk of blocks at a time: their facts from the columns and the masks' rounds, then the operators the masks
        // left, in node order. (An operator's loads of its operands' bytes then find the blocks' stores long done: a
        // byte isn't forwarded out of a 64-byte store still in flight.)
        for (uint64_t chunk = 0; chunk < words; chunk += BUSTER_CHECK_CFG_CHUNK)
        {
            const uint64_t end = chunk + BUSTER_CHECK_CFG_CHUNK < words ? chunk + BUSTER_CHECK_CFG_CHUNK : words;
            uint64_t pending[BUSTER_CHECK_CFG_CHUNK];
            for (uint64_t word = chunk; word < end; word += 1)
            {
                const uint32_t first = (uint32_t)(word * 64);
                const uint32_t left = count - first;
                const uint64_t lanes = left >= 64 ? UINT64_MAX : (1ull << left) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, constants + first);
                // An lvalue's address constant, which is none here where it has an object.
                const uint64_t loaded = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(CHECK_CONSTANT_ADDRESS)) &
                                        _mm512_cmpeq_epi8_mask(_mm512_maskz_loadu_epi8(lanes, categories + first), _mm512_set1_epi8(CHECK_LVALUE));
                // The 32-bit columns, 16 nodes a group: the operators narrowed to bytes; whether a value's type is _Bool,
                // whether it has no payload row and whether a loaded address has an object; and whether an operand is the
                // node before its operator, or the one before that.
                __m128i narrow[4];
                uint64_t booleans = wrapped;
                uint64_t rowless = 0;
                uint64_t objected = 0;
                uint64_t adjacent0 = 0;
                uint64_t adjacent1 = 0;
                uint64_t second0 = 0;
                for (uint32_t group = 0; group < 4; group += 1)
                {
                    const uint32_t at = first + 16 * group;
                    const uint32_t shift = 16 * group;
                    const __mmask16 live = (__mmask16)(lanes >> shift);
                    const __m512i before = _mm512_add_epi32(_mm512_set1_epi32((int32_t)at), lane_before);
                    const __m512i operand_left = _mm512_maskz_loadu_epi32(live, operand0 + at);
                    narrow[group] = _mm512_cvtepi32_epi8(_mm512_maskz_loadu_epi32(live, ops + at));
                    booleans |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(live, _mm512_and_si512(_mm512_maskz_loadu_epi32(live, value_types + at), unqualified), boolean_type) << shift;
                    rowless |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(live, _mm512_maskz_loadu_epi32(live, payload_rows + at), zero) << shift;
                    objected |= (uint64_t)_mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32((__mmask16)(loaded >> shift), objects + at), zero) << shift;
                    adjacent0 |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(live, operand_left, before) << shift;
                    second0 |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(live, operand_left, _mm512_sub_epi32(before, one)) << shift;
                    adjacent1 |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(live, _mm512_maskz_loadu_epi32(live, operand1 + at), before) << shift;
                }
                const __m512i narrowed = _mm512_inserti64x4(_mm512_castsi256_si512(_mm256_inserti128_si256(_mm256_castsi128_si256(narrow[0]), narrow[1], 1)),
                                                            _mm256_inserti128_si256(_mm256_castsi128_si256(narrow[2]), narrow[3], 1), 1);
                const uint64_t constant = _mm512_test_epi8_mask(kinds, kinds) & ~objected;
                const uint64_t integers = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(CHECK_CONSTANT_INTEGER));
                const uint64_t numbers = integers | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(CHECK_CONSTANT_FLOATING));
                __m512i fact = _mm512_maskz_permutex2var_epi8(lanes, table_low, narrowed, table_high);
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(constant, decides));
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(integers, integer));
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(numbers, number));
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(booleans & lanes, boolean));
                // The same facts as masks, and the operators the masks settle.
                const __m512i operator_class = _mm512_maskz_permutex2var_epi8(lanes, class_low, narrowed, class_high);
                uint64_t classes[5] = {
                    _mm512_test_epi8_mask(operator_class, _mm512_set1_epi8(BUSTER_CHECK_CFG_CLASS_THROUGH)),
                    _mm512_test_epi8_mask(operator_class, _mm512_set1_epi8(BUSTER_CHECK_CFG_CLASS_COMPARISON)),
                    _mm512_test_epi8_mask(operator_class, _mm512_set1_epi8(BUSTER_CHECK_CFG_CLASS_LOGICAL)),
                    _mm512_test_epi8_mask(operator_class, _mm512_set1_epi8(BUSTER_CHECK_CFG_CLASS_PRODUCT)),
                    _mm512_test_epi8_mask(operator_class, _mm512_set1_epi8(BUSTER_CHECK_CFG_CLASS_BIT_OR)),
                };
                const uint64_t operators = _mm512_test_epi8_mask(fact, operands);
                const uint64_t decided = _mm512_test_epi8_mask(fact, decides);
                CheckCfgMasks block = {
                    // (An operator that is a constant is settled: its own bit is all it gives.)
                    .settled = ~operators | decided,
                    .decides = decided,
                    .integer = integers,
                    .number = numbers,
                    .literal = _mm512_test_epi8_mask(fact, _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_LITERAL)),
                    .boolean = _mm512_test_epi8_mask(fact, boolean),
                    .negation = _mm512_test_epi8_mask(fact, _mm512_set1_epi8((char)BUSTER_CHECK_CFG_FACT_NOT)),
                    .zero = integers & rowless,
                    .bit_or = classes[4],
                };
                check_cfg_decidable_rounds(&block, &previous, classes, adjacent0, adjacent1, second0);
                previous = block;
                // (BUSTER_CHECK_CFG_FACT_OPERANDS goes: a settled operator's place is BUSTER_CHECK_CFG_FACT_UNASKED's, and
                // the others' turn clears it.)
                fact = _mm512_andnot_si512(operands, fact);
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(block.decides, decides));
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(block.unasked, operands));
                fact = _mm512_or_si512(fact, _mm512_maskz_mov_epi8(block.compared, compared));
                _mm512_storeu_si512(facts + first, fact);
                pending[word - chunk] = operators & ~block.settled;
            }
            check_cfg_decidable_operators(program, facts, absent, pending, chunk, end);
        }
        // (An operator may settle an earlier node's bit, check_cfg_decidable_ask: the bits are taken once all have had their
        // turn.)
        const __m512i kept = _mm512_set1_epi8((char)(BUSTER_CHECK_CFG_FACT_DECIDES | BUSTER_CHECK_CFG_FACT_UNASKED));
        for (uint64_t word = 0; word < words; word += 1)
        {
            bits[word] = _mm512_test_epi8_mask(_mm512_loadu_si512(facts + word * 64), kept);
        }
    }
    arena_pop_to(scratch, mark);
    return facts != NULL ? bits : NULL;
}

// A node's bit: whether a rule may decide it. Every node's without the bits.
static inline __attribute__((always_inline)) bool check_program_decidable(const CheckProgram* program, uint32_t node)
{
    const uint64_t* bits = program->decidable;
    return bits == NULL || node >= program->input->graph.count || (bits[node >> 6] >> (node & 63) & 1) != 0;
}

// A branch condition's truth as clang's CFG builder prunes its edges (#1447, #2990), which the -Wreturn-type walk, the
// runtime warnings' reachability (check_common_body_reached, check_common_operand_mask) and the uninitialized flow read:
// `condition` is a statement's condition (a CONDITION node, `node` itself), a conditional's (`node`'s first operand) or
// a logical operator's first operand (`node` the operator). A logical operator past its parentheses is its branches'
// (check_cfg_branches); a logical operator's other first operand is its tryEvaluateBool past its parentheses, and any
// other condition its tryEvaluateBool as written, whose parentheses stop the heuristics (`if ((x * 0))`). A do
// statement's condition is its tryEvaluateBool as written, a logical operator's too (VisitDoStmt), and GNU's `a ?: b`'s
// its evaluator's alone. check_program_branch_truth asks it where the condition's bit says a rule may decide it.
static bool check_program_branch_fold(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
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

static inline __attribute__((always_inline)) bool check_program_branch_truth(const CheckProgram* program, uint32_t node, uint32_t condition, bool* truth)
{
    *truth = false;
    return check_program_decidable(program, condition) && check_program_branch_fold(program, node, condition, truth);
}
