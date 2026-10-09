// The tree expression typer, stage 1 (GitHub #3102): the types of function-body
// expressions, computed once per body over the implicit postorder syntax tree
// (c_ast.h) instead of by running the speculative type machine on token ranges.
//
// The contract. The type machine (CTypeParseMachine in c_parse.c) is the
// authority. This file may only ANSWER a type query when the answer is exactly
// what the machine would return for that token range: valid, the same type, no
// constraint in checked mode, the same nonplace-projection fact, and no change
// to the type tables. For every node it does not vouch for it DECLINES, and the
// machine runs as it always has, so the machine remains the only producer of a
// diagnostic and of every answer this file is unsure of. When in doubt a node
// is declined. Stage 1 mints no type rows: it returns ids that already exist
// (entity, member, element and return types, and the immutable scalar rows in
// CParseResult.expression_scalar_types), and declines every shape whose machine
// answer appends a row (a qualified member, a qualified array element, an
// address-of, string literals and every operator that computes a type).
//
// Ownership and lifetime. A caller that built the tree (the driver's
// -fc-ast-pilot) passes it in CParserResult.ast; c_analyze_semantics_core puts
// it in CTypeParseMachine.syntax_tree. c_parse_validate_lowering_constraints
// builds the function-definition index once (c_ast_types_bodies_prepare, in the
// machine's scratch arena beside the other tables built outside the per-body
// checkpoints) and, per function body, calls c_ast_types_body_begin after the
// binder has recorded every identifier use and c_ast_types_body_end when the
// body is done. The per-body arrays are allocated in the machine's scratch
// arena above the body's validation mark and are released with it: nothing
// here outlives a body, because a speculative rollback may truncate the type
// rows an entry refers to. A body is typed only while the machine is idle
// (frame_count 0), and a query is answered only under the same condition.
//
// The eager pass. c_ast_types_type_body is one forward loop over the body's
// node interval in index order. Children precede parents in a postorder tree, so
// each node's operand types are known when the node is reached. For every
// expression node it computes the token span [first, end) (the investigation's
// span rules, validated over 1.75 M nodes: a node's span starts at its
// leftmost operand or operator and ends at its last token, widened over any
// balanced parentheses that wrap an operand, which the tree does not record),
// links the node into a per-start-token chain, and computes a type and flags.
// A node whose operand is not accepted is not accepted.
//
// The query. c_parse_expression_type_query reads the per-body memo first and
// asks this file only on a miss, before the literal fast path and the machine
// run; a tree answer is published to the memo exactly as the machine's valid,
// constraint-free answer would be. That keeps every later machine run, whose
// tasks read sub-range results from the memo, on the rows it would have used:
// with and without the tree, analysis ends with the same type tables.
// c_ast_types_answer maps a query range to its node: first
// (start, end) through the chain of nodes that start at `start`, then with
// balanced outer parentheses stripped one pair at a time, exactly the strip the
// machine performs. A range that maps to no node (a designator probe such as
// `.f`, a comma list, a declaration fragment) is a miss. A node is answered
// only when the machine would take the same path: the type is valid and
// accepted, a checked query's node is checked-safe, the range holds no
// _Generic or __builtin_types_compatible_p site (the machine settles those
// before typing), no enumerator list is half parsed, and a call's callee
// resolves by spelling in the query's scope to the entity its binding names.
//
// Accepted kinds and why each is exact (the machine paths are in c_parse.c):
//   IDENTIFIER   bound through identifier_use_by_token_plus_one to an object,
//                function, parameter, local or enumerator: the entity's type,
//                as c_parse_direct_expression_base answers the single token.
//   NUMBER,      c_parse_expression_leaf_without_cast itself, the leaf the
//   CHARACTER    machine's literal path calls; it returns an immutable scalar
//                row and appends none while every scalar row is published
//                (c_ast_types_scalars_published).
//   MEMBER,      a struct or union member through c_parse_member_type (which
//   MEMBER_ARROW also searches anonymous members), as c_parse_direct_expression_postfix
//                does; declined when the aggregate is qualified, because the
//                machine then appends a qualified copy of the member type.
//   INDEX        an array or pointer base with an integer index, as the
//                machine's subscript operation; declined for a qualified array
//                (a qualified element row), a vector base, the reversed form
//                `i[a]` (which the machine's chain walk refuses), and a call
//                operand (its type comes from a different machine path).
//   DEREFERENCE  a pointer or array operand: its element type, with the same
//                call-operand exclusion.
//   CALL         `name(...)` whose callee name is a bound object, function,
//                parameter or local of function or pointer-to-function type
//                and not a builtin, vendor builtin or sizeof-like spelling: the
//                function's return type. Arguments are not typed by the
//                machine and are not inspected here.
// Nothing sets result_nonplace_projection except `__real__`/`__imag__` of a
// real operand, which stage 1 declines, so no accepted node carries the fact.
// No accepted node can raise a constraint in checked mode, so every accepted
// node is checked-safe; the bit stays a separate flag for later stages.
//
// Verification (tests builds only). c_test_ast_type_verify_set makes every tree
// answer also run without the tree (through the literal path or the machine)
// and be compared on validity, structural type equality, constraint, nonplace
// fact, diagnostics and the sizes of the type tables; the tree answer is still
// returned. c_test_ast_type_probe answers one range of an analyzed function
// body for unit tests.
//
// Layout map (search these symbols):
//   CAstTypeBodyIndex, CAstTypeBody              function index, per-body arrays
//   c_ast_types_bodies_prepare, c_ast_types_find_definition
//   c_ast_types_body_begin, c_ast_types_body_end the per-body entry points
//   c_ast_types_span, c_ast_types_expand         span rules
//   c_ast_types_type_body, c_ast_types_type_node the eager pass and its rules
//   c_ast_types_locate, c_ast_types_answer       query lookup and the decision
//   c_ast_types_publish                          machine state after an answer
//   c_ast_types_verify_*, c_test_*               the differential (tests builds)

#include "c_internal.h"
#include <buster/lib/compiler/frontend/c/c_ast.h>
#include <buster/lib/compiler/frontend/c/c_parse_internal.h>

#define C_AST_TYPE_NONE UINT32_MAX

// Per-node flag bits.
#define C_AST_TYPE_FLAG_ACCEPTED (1u << 0)
#define C_AST_TYPE_FLAG_SAFE (1u << 1)
#define C_AST_TYPE_FLAG_NONPLACE (1u << 2)
// A call the machine resolves by the callee's spelling in the query's scope.
#define C_AST_TYPE_FLAG_CALLEE_LOOKUP (1u << 3)

// The `{` token of every top-level function definition, ascending, and the
// FUNCTION_DEFINITION node it opens.
struct CAstTypeBodyIndex
{
    u32* braces;
    u32* nodes;
    u32 count;
    // Every scalar row is published (c_ast_types_scalars_published), so the
    // literal leaf cannot append one.
    bool scalars_published;
};

// One typed function body. Arrays are indexed by node - begin.
struct CAstTypeBody
{
    CAst const* ast;
    CParseResult* result;
    CToken const* tokens;
    u32 const* matches;
    CAstTypeStatistics* statistics;
    CTypeId* types;
    u32* first;
    u32* end;
    // The previous node (relative index + 1) that starts at the same token, 0
    // for none; start_head holds the newest, which is the outermost.
    u32* link;
    u32* start_head;
    u8* flags;
    u32 begin;
    // The body's COMPOUND_STATEMENT.
    u32 node;
    // The body's tokens: [token_start, token_end), the braces excluded.
    u32 token_start;
    u32 token_end;
    u32 token_total;
    bool scalars_published;
    CAstTypeStatistics local_statistics;
};

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_expression_kind(u32 kind)
{
    return (kind >= C_AST_IDENTIFIER && kind <= C_AST_COMMA && kind != C_AST_GENERIC_ASSOCIATION && kind != C_AST_GENERIC_DEFAULT) ||
           kind == C_AST_INITIALIZER_LIST;
}

// The first child of an interior node: the oldest subtree tiling its interval.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_first_child(CAst const* ast, u32 node)
{
    u32 begin = c_ast_subtree_begin(ast, node);
    u32 child = node - 1;
    while (c_ast_subtree_begin(ast, child) > begin)
    {
        child = c_ast_subtree_begin(ast, child) - 1;
    }
    return child;
}

// ---- the function-definition index ---------------------------------------------

BUSTER_GLOBAL_LOCAL bool c_ast_types_top_level_definition(CAst const* ast, u32 child)
{
    return ast->kinds[child] == C_AST_FUNCTION_DEFINITION && child >= 1 && ast->kinds[child - 1] == C_AST_COMPOUND_STATEMENT;
}

// Whether c_parse_validate_lowering_constraints has published the immutable
// row of every scalar kind a literal can have. While it has, the literal leaf
// returns that row and appends none.
BUSTER_GLOBAL_LOCAL bool c_ast_types_scalars_published(CParseResult const* result)
{
    bool published = result->expression_scalar_types != 0;
    for (u32 kind = C_TYPE_VOID; kind <= C_TYPE_NULLPTR && published; kind += 1)
    {
        CTypeId row = result->expression_scalar_types[kind];
        CType const* type = row.value < result->type_count ? result->types + row.value : 0;
        published = kind == C_TYPE_VA_LIST ||
                    (type && type->kind == (CTypeKind)kind && !type->is_const && !type->is_volatile && !type->is_restrict && !type->is_atomic);
    }
    return published;
}

BUSTER_C_SHARED void c_ast_types_bodies_prepare(CTypeParseMachine* machine, CParseResult const* result)
{
    CAst const* ast = machine->syntax_tree;
    machine->ast_bodies = 0;
    if (ast && ast->node_count && ast->root < ast->node_count && ast->kinds[ast->root] == C_AST_TRANSLATION_UNIT)
    {
        u32 floor = c_ast_subtree_begin(ast, ast->root);
        u32 count = 0;
        u32 cursor = ast->root;
        while (cursor > floor && ast->extents[cursor - 1] && ast->extents[cursor - 1] <= cursor)
        {
            u32 child = cursor - 1;
            count += c_ast_types_top_level_definition(ast, child);
            cursor = c_ast_subtree_begin(ast, child);
        }
        CAstTypeBodyIndex* index = arena_allocate(machine->scratch_arena, CAstTypeBodyIndex, 1);
        index->braces = arena_allocate(machine->scratch_arena, u32, count + 1);
        index->nodes = arena_allocate(machine->scratch_arena, u32, count + 1);
        index->count = count;
        index->scalars_published = c_ast_types_scalars_published(result);
        u32 slot = count;
        cursor = ast->root;
        while (slot && cursor > floor && ast->extents[cursor - 1] && ast->extents[cursor - 1] <= cursor)
        {
            u32 child = cursor - 1;
            if (c_ast_types_top_level_definition(ast, child))
            {
                slot -= 1;
                index->braces[slot] = ast->tokens[child - 1];
                index->nodes[slot] = child;
            }
            cursor = c_ast_subtree_begin(ast, child);
        }
        machine->ast_bodies = index;
    }
}

BUSTER_GLOBAL_LOCAL u32 c_ast_types_find_definition(CAstTypeBodyIndex const* index, u32 brace)
{
    u32 low = 0;
    u32 high = index->count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (index->braces[middle] < brace)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < index->count && index->braces[low] == brace ? index->nodes[low] : C_AST_TYPE_NONE;
}

// ---- spans -----------------------------------------------------------------

// The closer of the delimiter opening at `open`, or C_AST_TYPE_NONE; the same
// answer c_parse_matching_delimiter_indexed gives, read from the built index.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_types_match(CAstTypeBody const* body, u32 open)
{
    u32 close = open < body->token_total ? body->matches[open] - 1 : C_AST_TYPE_NONE;
    return close > open ? close : C_AST_TYPE_NONE;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_punctuator_at(CAstTypeBody const* body, u32 token, CPunctuator punctuator)
{
    return token < body->token_total && c_token_is_punctuator(&body->tokens[token], punctuator);
}

// The span of the node at `relative`, widened over every balanced pair of
// parentheses that wraps it exactly. The tree has no node for a parenthesis, so
// an operator's own span begins or ends outside its operand's.
BUSTER_GLOBAL_LOCAL void c_ast_types_expand(CAstTypeBody const* body, u32 relative, u32* first_out, u32* end_out)
{
    u32 first = body->first[relative];
    u32 end = body->end[relative];
    if (first != C_AST_TYPE_NONE)
    {
        while (first > 0 && c_ast_types_punctuator_at(body, first - 1, C_PUNCTUATOR_LEFT_PARENTHESIS) && c_ast_types_match(body, first - 1) == end)
        {
            first -= 1;
            end += 1;
        }
    }
    *first_out = first;
    *end_out = end;
}

// Computes [first, end) for one expression node. Returns false when the span
// cannot be established (an unmatched delimiter, an operand without a span, a
// span outside the unit), and then the node can be neither queried nor
// accepted.
BUSTER_GLOBAL_LOCAL bool c_ast_types_span(CAstTypeBody const* body, u32 node, u32 relative, u32* first_out, u32* end_out)
{
    CAst const* ast = body->ast;
    u32 kind = ast->kinds[node];
    u32 token = ast->tokens[node];
    u32 first = C_AST_TYPE_NONE;
    u32 end = C_AST_TYPE_NONE;
    switch (kind)
    {
    case C_AST_IDENTIFIER:
    case C_AST_NUMBER:
    case C_AST_CHARACTER:
    case C_AST_BOOLEAN_CONSTANT:
    case C_AST_NULLPTR:
    {
        first = token;
        end = token + 1;
    }
    break;
    case C_AST_STRING:
    {
        first = token;
        end = token + ast->data[node];
    }
    break;
    case C_AST_LABEL_ADDRESS:
    {
        first = token ? token - 1 : C_AST_TYPE_NONE;
        end = token + 1;
    }
    break;
    case C_AST_STATEMENT_EXPRESSION:
    {
        u32 close = c_ast_types_match(body, token);
        first = close != C_AST_TYPE_NONE ? token : C_AST_TYPE_NONE;
        end = close != C_AST_TYPE_NONE ? close + 1 : C_AST_TYPE_NONE;
    }
    break;
    case C_AST_GENERIC_SELECTION:
    case C_AST_SIZEOF_TYPE:
    case C_AST_ALIGNOF_TYPE:
    {
        u32 close = c_ast_types_match(body, token + 1);
        first = close != C_AST_TYPE_NONE ? token : C_AST_TYPE_NONE;
        end = close != C_AST_TYPE_NONE ? close + 1 : C_AST_TYPE_NONE;
    }
    break;
    case C_AST_CALL:
    case C_AST_INDEX:
    {
        u32 callee = c_ast_types_first_child(ast, node);
        u32 expanded_first = C_AST_TYPE_NONE;
        u32 expanded_end = C_AST_TYPE_NONE;
        c_ast_types_expand(body, callee - body->begin, &expanded_first, &expanded_end);
        u32 close = c_ast_types_match(body, token);
        if (expanded_first != C_AST_TYPE_NONE && close != C_AST_TYPE_NONE)
        {
            first = expanded_first;
            end = close + 1;
        }
    }
    break;
    case C_AST_MEMBER:
    case C_AST_MEMBER_ARROW:
    case C_AST_POST_INCREMENT:
    case C_AST_POST_DECREMENT:
    {
        u32 expanded_first = C_AST_TYPE_NONE;
        u32 expanded_end = C_AST_TYPE_NONE;
        c_ast_types_expand(body, relative - 1, &expanded_first, &expanded_end);
        if (expanded_first != C_AST_TYPE_NONE)
        {
            first = expanded_first;
            end = token + 1;
        }
    }
    break;
    case C_AST_COMPOUND_LITERAL:
    {
        u32 close = c_ast_types_match(body, ast->tokens[node - 1]);
        first = close != C_AST_TYPE_NONE ? token : C_AST_TYPE_NONE;
        end = close != C_AST_TYPE_NONE ? close + 1 : C_AST_TYPE_NONE;
    }
    break;
    case C_AST_INITIALIZER_LIST:
    {
        u32 close = c_ast_types_match(body, token);
        first = close != C_AST_TYPE_NONE ? token : C_AST_TYPE_NONE;
        end = close != C_AST_TYPE_NONE ? close + 1 : C_AST_TYPE_NONE;
    }
    break;
    default:
    {
        if (kind >= C_AST_PRE_INCREMENT && kind <= C_AST_CAST)
        {
            u32 expanded_first = C_AST_TYPE_NONE;
            u32 expanded_end = C_AST_TYPE_NONE;
            c_ast_types_expand(body, relative - 1, &expanded_first, &expanded_end);
            if (expanded_first != C_AST_TYPE_NONE)
            {
                first = token;
                end = expanded_end;
            }
        }
        else if (kind >= C_AST_MULTIPLY && kind <= C_AST_COMMA)
        {
            u32 leftmost = c_ast_types_first_child(ast, node);
            u32 left_first = C_AST_TYPE_NONE;
            u32 left_end = C_AST_TYPE_NONE;
            u32 right_first = C_AST_TYPE_NONE;
            u32 right_end = C_AST_TYPE_NONE;
            c_ast_types_expand(body, leftmost - body->begin, &left_first, &left_end);
            c_ast_types_expand(body, relative - 1, &right_first, &right_end);
            if (left_first != C_AST_TYPE_NONE && right_end != C_AST_TYPE_NONE)
            {
                first = left_first;
                end = right_end;
            }
        }
    }
    break;
    }
    bool valid = first != C_AST_TYPE_NONE && end != C_AST_TYPE_NONE && first < end && end <= body->token_total;
    *first_out = valid ? first : C_AST_TYPE_NONE;
    *end_out = valid ? end : C_AST_TYPE_NONE;
    return valid;
}

// ---- the typing rules ----------------------------------------------------------

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void c_ast_types_accept(CAstTypeBody* body, u32 relative, CTypeId type, u32 flags)
{
    body->types[relative] = type;
    body->flags[relative] = (u8)(C_AST_TYPE_FLAG_ACCEPTED | C_AST_TYPE_FLAG_SAFE | flags);
}

// The entity the identifier use at `token` is bound to, or null when it has no
// recorded use or the use resolved to nothing.
BUSTER_GLOBAL_LOCAL CEntity const* c_ast_types_bound_entity(CParseResult* result, u32 token)
{
    CEntity const* entity = 0;
    u32 use = c_parse_identifier_use_index(result, token);
    if (use != C_ID_UNDERLYING_INVALID && use < result->identifier_use_count)
    {
        CEntityId id = result->identifier_uses[use].entity;
        entity = id.value < result->entity_count ? result->entities + id.value : 0;
    }
    return entity;
}

BUSTER_GLOBAL_LOCAL bool c_ast_types_value_entity(CEntity const* entity, bool allow_enumerator)
{
    return entity && (entity->kind == C_ENTITY_OBJECT || entity->kind == C_ENTITY_FUNCTION || entity->kind == C_ENTITY_PARAMETER ||
                      entity->kind == C_ENTITY_LOCAL || (allow_enumerator && entity->kind == C_ENTITY_ENUMERATOR));
}

BUSTER_GLOBAL_LOCAL void c_ast_types_identifier(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    u32 token = body->ast->tokens[node];
    CEntity const* entity = token < body->token_total && body->tokens[token].kind == C_TOKEN_IDENTIFIER ? c_ast_types_bound_entity(result, token) : 0;
    if (c_ast_types_value_entity(entity, true) && entity->type.value < result->type_count)
    {
        c_ast_types_accept(body, relative, entity->type, 0);
    }
}

// A number or character literal, typed by the leaf the machine's literal path
// uses (c_parse_expression_leaf_without_cast), so the rule exists once. With
// every scalar row published the leaf only reads the row it returns.
BUSTER_GLOBAL_LOCAL void c_ast_types_literal(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative,
                                             CTokenKind token_kind)
{
    u32 token = body->ast->tokens[node];
    if (body->scalars_published && token < body->token_total && body->tokens[token].kind == token_kind)
    {
        CTypeId type = c_parse_expression_leaf_without_cast(machine->scratch_arena, *preprocess, body->result, (CScopeId){.value = C_ID_UNDERLYING_INVALID},
                                                            token, token + 1);
        if (type.value < body->result->type_count)
        {
            c_ast_types_accept(body, relative, type, 0);
        }
    }
}

// `base.name` and `base->name`. The machine walks the whole postfix chain from
// one base type; per node that is this step, with the aggregate resolved the
// way c_parse_direct_expression_postfix resolves it.
BUSTER_GLOBAL_LOCAL void c_ast_types_member(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative,
                                            bool arrow)
{
    CParseResult* result = body->result;
    u32 member_token = body->ast->tokens[node];
    CTypeId aggregate = C_TYPE_ID_INVALID;
    if ((body->flags[relative - 1] & C_AST_TYPE_FLAG_ACCEPTED) && member_token >= 1 && member_token < body->token_total &&
        body->tokens[member_token].kind == C_TOKEN_IDENTIFIER &&
        c_ast_types_punctuator_at(body, member_token - 1, arrow ? C_PUNCTUATOR_ARROW : C_PUNCTUATOR_DOT))
    {
        CTypeId base = body->types[relative - 1];
        CType const* value = result->types + base.value;
        if (!arrow)
        {
            aggregate = base;
        }
        else if (value->kind == C_TYPE_POINTER && value->element_type.value < result->type_count)
        {
            aggregate = value->element_type;
        }
        if (aggregate.value != C_ID_UNDERLYING_INVALID)
        {
            // A qualified aggregate makes the machine append a qualified copy
            // of the member type; stage 1 appends nothing.
            value = result->types + aggregate.value;
            bool plain = (value->kind == C_TYPE_STRUCT || value->kind == C_TYPE_UNION) && !value->is_const && !value->is_volatile &&
                         !value->is_restrict && !value->is_atomic;
            aggregate = plain ? aggregate : C_TYPE_ID_INVALID;
        }
    }
    if (aggregate.value != C_ID_UNDERLYING_INVALID)
    {
        CToken name = preprocess->tokens[member_token];
        CTypeId type = c_parse_member_type(machine->scratch_arena, result, aggregate, name.symbol, c_token_spelling(preprocess->spelling_base, name), 0, 0, 0);
        if (type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, type, 0);
        }
    }
}

// `base[index]` over an array or pointer. The machine types the two operands
// through its own tasks, and a call operand is typed there by the scope-keyed
// callee rule (see c_ast_types_call), so a call operand is left to the machine.
BUSTER_GLOBAL_LOCAL void c_ast_types_index(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    u32 base_node = c_ast_types_first_child(ast, node);
    u32 base = base_node - body->begin;
    u32 index = relative - 1;
    if ((body->flags[base] & C_AST_TYPE_FLAG_ACCEPTED) && (body->flags[index] & C_AST_TYPE_FLAG_ACCEPTED) && base != index &&
        ast->kinds[base_node] != C_AST_CALL && ast->kinds[node - 1] != C_AST_CALL)
    {
        CType const* sequence = result->types + body->types[base].value;
        CType const* offset = result->types + body->types[index].value;
        bool qualified_array = sequence->kind == C_TYPE_ARRAY && (sequence->is_const || sequence->is_volatile);
        if ((sequence->kind == C_TYPE_ARRAY || sequence->kind == C_TYPE_POINTER) && !sequence->is_atomic && !qualified_array &&
            c_parse_expression_integer_kind(offset->kind) && sequence->element_type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, sequence->element_type, 0);
        }
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_types_dereference(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    u32 operand = relative - 1;
    if ((body->flags[operand] & C_AST_TYPE_FLAG_ACCEPTED) && body->ast->kinds[node - 1] != C_AST_CALL)
    {
        CType const* value = result->types + body->types[operand].value;
        if ((value->kind == C_TYPE_POINTER || value->kind == C_TYPE_ARRAY) && !value->is_atomic && value->element_type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, value->element_type, 0);
        }
    }
}

// Names the machine's call rule answers before it consults any entity.
BUSTER_GLOBAL_LOCAL bool c_ast_types_callee_reserved(CPreprocessResult const* preprocess, u32 token_index)
{
    String8 name = c_token_spelling(preprocess->spelling_base, preprocess->tokens[token_index]);
    bool reserved = name.length >= 6 && name.length <= 8 &&
                    (string_equal(name, S8("sizeof")) || string_equal(name, S8("_Alignof")) || string_equal(name, S8("alignof")));
    if (!reserved && name.length && name.pointer[0] == '_')
    {
        CIrSse2ImmediateShiftBuiltin shift = {0};
        CIrSimdBuiltin simd = {0};
        reserved = c_symbol_builtin_from_spelling(name) != C_SYMBOL_BUILTIN_NONE || string_starts_with_sequence(name, S8("__builtin_")) ||
                   string_starts_with_sequence(name, S8("__c11_atomic_")) || string_starts_with_sequence(name, S8("__atomic_")) ||
                   string_starts_with_sequence(name, S8("__sync_")) || c_vendor_builtin_spelling(name) ||
                   c_vendor_generic_builtin(name).operation != 0 || c_semantic_bfloat16_builtin_spelling(name) ||
                   c_semantic_sse2_immediate_shift_builtin(name, &shift) || c_semantic_simd_builtin(name, &simd);
    }
    return reserved;
}

// `name(arguments)` where `name` is a bound value of function or
// pointer-to-function type. The machine finds the callee by spelling in the
// query's scope first; c_ast_types_answer checks at query time that this finds
// the entity the binding names (the CALLEE_LOOKUP flag), and otherwise the
// machine's fallback is the binding, which is what is read here.
BUSTER_GLOBAL_LOCAL void c_ast_types_call(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    u32 callee = c_ast_types_first_child(ast, node);
    u32 callee_token = ast->tokens[callee];
    // The callee is the plain identifier directly before the `(`: a
    // parenthesized or computed callee takes a different machine path.
    if (ast->kinds[callee] == C_AST_IDENTIFIER && ast->tokens[node] == callee_token + 1 && callee_token < body->token_total &&
        body->tokens[callee_token].kind == C_TOKEN_IDENTIFIER && c_ast_types_punctuator_at(body, callee_token + 1, C_PUNCTUATOR_LEFT_PARENTHESIS))
    {
        CEntity const* entity = c_ast_types_bound_entity(result, callee_token);
        if (c_ast_types_value_entity(entity, false) && entity->type.value < result->type_count && !c_ast_types_callee_reserved(preprocess, callee_token))
        {
            CType const* function = result->types + entity->type.value;
            if (function->kind == C_TYPE_POINTER && function->element_type.value < result->type_count)
            {
                function = result->types + function->element_type.value;
            }
            if (function->kind == C_TYPE_FUNCTION && function->return_type.value < result->type_count)
            {
                c_ast_types_accept(body, relative, function->return_type, C_AST_TYPE_FLAG_CALLEE_LOOKUP);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_types_type_node(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    switch (body->ast->kinds[node])
    {
    case C_AST_IDENTIFIER:
    {
        c_ast_types_identifier(body, node, relative);
    }
    break;
    case C_AST_NUMBER:
    {
        c_ast_types_literal(body, machine, preprocess, node, relative, C_TOKEN_PREPROCESSING_NUMBER);
    }
    break;
    case C_AST_CHARACTER:
    {
        c_ast_types_literal(body, machine, preprocess, node, relative, C_TOKEN_CHARACTER_LITERAL);
    }
    break;
    case C_AST_MEMBER:
    {
        c_ast_types_member(body, machine, preprocess, node, relative, false);
    }
    break;
    case C_AST_MEMBER_ARROW:
    {
        c_ast_types_member(body, machine, preprocess, node, relative, true);
    }
    break;
    case C_AST_INDEX:
    {
        c_ast_types_index(body, node, relative);
    }
    break;
    case C_AST_DEREFERENCE:
    {
        c_ast_types_dereference(body, node, relative);
    }
    break;
    case C_AST_CALL:
    {
        c_ast_types_call(body, preprocess, node, relative);
    }
    break;
    default:
    break;
    }
}

// The eager pass: one forward loop, children before parents.
BUSTER_GLOBAL_LOCAL void c_ast_types_type_body(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess)
{
    CAst const* ast = body->ast;
    u32 visited = 0;
    u32 accepted = 0;
    for (u32 node = body->begin; node <= body->node; node += 1)
    {
        u32 relative = node - body->begin;
        body->types[relative] = C_TYPE_ID_INVALID;
        body->flags[relative] = 0;
        body->first[relative] = C_AST_TYPE_NONE;
        body->end[relative] = C_AST_TYPE_NONE;
        body->link[relative] = 0;
        if (c_ast_types_expression_kind(ast->kinds[node]))
        {
            visited += 1;
            u32 first = C_AST_TYPE_NONE;
            u32 end = C_AST_TYPE_NONE;
            if (c_ast_types_span(body, node, relative, &first, &end) && first >= body->token_start && end <= body->token_end)
            {
                body->first[relative] = first;
                body->end[relative] = end;
                body->link[relative] = body->start_head[first - body->token_start];
                body->start_head[first - body->token_start] = relative + 1;
                c_ast_types_type_node(body, machine, preprocess, node, relative);
                accepted += (body->flags[relative] & C_AST_TYPE_FLAG_ACCEPTED) != 0;
            }
        }
    }
    body->statistics->bodies += 1;
    body->statistics->nodes_typed += visited;
    body->statistics->nodes_accepted += accepted;
    WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_NODES, visited);
}

BUSTER_C_SHARED void c_ast_types_body_begin(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess,
                                            CDeclaration const* declaration)
{
    machine->ast_types = 0;
    CAstTypeBodyIndex const* bodies = machine->ast_bodies;
    u32 token_start = declaration->body_start;
    u64 token_end = (u64)declaration->body_start + declaration->body_token_count;
    bool eligible = bodies && machine->frame_count == 0 && !machine->failed && token_start >= 1 && declaration->body_token_count &&
                    token_end < preprocess->token_count && result->position_index;
    if (eligible)
    {
        c_parse_position_index_ensure(result, *preprocess);
        eligible = result->position_index->built && result->position_index->matching_delimiters_plus_one;
    }
    u32 definition = eligible ? c_ast_types_find_definition(bodies, token_start - 1) : C_AST_TYPE_NONE;
    CAst const* ast = machine->syntax_tree;
    if (definition != C_AST_TYPE_NONE && ast->kinds[definition - 1] == C_AST_COMPOUND_STATEMENT &&
        result->position_index->matching_delimiters_plus_one[token_start - 1] - 1 == token_end)
    {
        u32 node = definition - 1;
        u32 count = ast->extents[node];
        // Guarded per-body scratch (#1256): a body whose arrays do not fit is
        // left untyped from the tree, and the caller reports the exhaustion.
        Arena* scratch = machine->scratch_arena;
        CAstTypeBody* body = C_PARSE_BODY_SCRATCH_ARRAY(scratch, CAstTypeBody, 1);
        CTypeId* types = C_PARSE_BODY_SCRATCH_ARRAY(scratch, CTypeId, count);
        u32* first = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
        u32* end = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
        u32* link = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
        u32* start_head = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, declaration->body_token_count);
        u8* flags = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u8, count);
        if (body && types && first && end && link && start_head && flags)
        {
            *body = (CAstTypeBody){
                .ast = ast,
                .result = result,
                .tokens = preprocess->tokens,
                .matches = result->position_index->matching_delimiters_plus_one,
                .types = types,
                .first = first,
                .end = end,
                .link = link,
                .start_head = start_head,
                .flags = flags,
                .begin = c_ast_subtree_begin(ast, node),
                .node = node,
                .token_start = token_start,
                .token_end = (u32)token_end,
                .token_total = (u32)preprocess->token_count,
                .scalars_published = bodies->scalars_published,
            };
            body->statistics = machine->ast_type_statistics ? machine->ast_type_statistics : &body->local_statistics;
            memset(body->start_head, 0, sizeof(*body->start_head) * declaration->body_token_count);
            c_ast_types_type_body(body, machine, preprocess);
            machine->ast_types = body;
        }
    }
}

BUSTER_C_SHARED void c_ast_types_body_end(CTypeParseMachine* machine)
{
    machine->ast_types = 0;
}

// ---- queries ---------------------------------------------------------------------

// The node whose span is exactly [start, end) after stripping balanced outer
// parentheses, or C_AST_TYPE_NONE.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_locate(CAstTypeBody const* body, u32 start, u32 end)
{
    u32 relative = C_AST_TYPE_NONE;
    bool searching = start < end && start >= body->token_start && end <= body->token_end;
    while (searching)
    {
        u32 hit = body->start_head[start - body->token_start];
        while (hit && relative == C_AST_TYPE_NONE)
        {
            relative = body->end[hit - 1] == end ? hit - 1 : C_AST_TYPE_NONE;
            hit = relative == C_AST_TYPE_NONE ? body->link[hit - 1] : 0;
        }
        searching = relative == C_AST_TYPE_NONE && end > start + 2 && c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                    c_ast_types_match(body, start) + 1 == end;
        start += searching ? 1 : 0;
        end -= searching ? 1 : 0;
    }
    return relative;
}

// Whether the machine's scope-keyed lookup of this call's callee yields the
// entity the binder bound the token to (or nothing, in which case the machine
// falls back to the binding).
BUSTER_GLOBAL_LOCAL bool c_ast_types_callee_agrees(CAstTypeBody const* body, CPreprocessResult const* preprocess, CParseResult* result, CScopeId scope, u32 node)
{
    u32 callee = c_ast_types_first_child(body->ast, node);
    u32 token = body->ast->tokens[callee];
    CEntityId looked = c_parse_lookup_entity_token(result, preprocess->spelling_base, scope, &preprocess->tokens[token]);
    CEntity const* bound = c_ast_types_bound_entity(result, token);
    return looked.value >= result->entity_count || (bound && looked.value == (u32)(bound - result->entities));
}

BUSTER_C_SHARED CAstTypeAnswer c_ast_types_answer(CTypeParseMachine* machine, CPreprocessResult const* preprocess, CParseResult* result, CScopeId scope,
                                                  u32 start, u32 end)
{
    CAstTypeBody* body = machine->ast_types;
    CAstTypeAnswer answer = {.type = C_TYPE_ID_INVALID, .status = C_AST_TYPE_INACTIVE};
    // The same preconditions the machine's literal fast path insists on: an idle
    // machine with room for the frame and tasks the machine path would take.
    bool ready = body->result == result && body->tokens == preprocess->tokens && !machine->failed && machine->frame_count < machine->frame_capacity &&
                 !machine->frame_count && machine->constant_evaluation_mode == C_CONSTANT_EVALUATION_NORMAL &&
                 machine->expression_task_count <= machine->expression_task_capacity &&
                 (u64)(end > start ? end - start : 0) + 1 <= machine->expression_task_capacity - machine->expression_task_count;
    if (!ready)
    {
        body->statistics->gated += 1;
    }
    else
    {
        u32 relative = c_ast_types_locate(body, start, end);
        if (relative == C_AST_TYPE_NONE)
        {
            body->statistics->misses += 1;
            answer.status = C_AST_TYPE_MISS;
            WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_MISSES, 1);
        }
        else
        {
            u32 flags = body->flags[relative];
            u32 node = body->begin + relative;
            bool vouched = (flags & C_AST_TYPE_FLAG_ACCEPTED) && (!machine->validate_expression_constraints || (flags & C_AST_TYPE_FLAG_SAFE)) &&
                           !c_parse_pending_enum_possible(result) && c_parse_type_identity_sites_absent(result, start, end);
            if (vouched && (flags & C_AST_TYPE_FLAG_CALLEE_LOOKUP))
            {
                vouched = c_ast_types_callee_agrees(body, preprocess, result, scope, node);
            }
            answer.node_kind = body->ast->kinds[node];
            if (vouched)
            {
                body->statistics->answers += 1;
                answer.status = C_AST_TYPE_ANSWER;
                answer.type = body->types[relative];
                answer.nonplace_projection = (flags & C_AST_TYPE_FLAG_NONPLACE) != 0;
                WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_ANSWERS, 1);
            }
            else
            {
                body->statistics->declines += 1;
                answer.status = C_AST_TYPE_DECLINE;
                WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_DECLINES, 1);
            }
        }
    }
    return answer;
}

BUSTER_C_SHARED void c_ast_types_publish(CTypeParseMachine* machine, CParseResult* result, CAstTypeAnswer answer, u32 end)
{
    machine->mutation_type_limit = result->type_count;
    machine->result_type = answer.type;
    machine->result_index = end;
    machine->result_valid = true;
    machine->result_nonplace_projection = answer.nonplace_projection;
}

// ---- verification and test seams ---------------------------------------------------

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool c_ast_types_verify_enabled;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CTestAstTypeVerify c_ast_types_verify_state;

void c_test_ast_type_verify_set(bool enabled)
{
    c_ast_types_verify_enabled = enabled;
}

CTestAstTypeVerify c_test_ast_type_verify_take(void)
{
    CTestAstTypeVerify taken = c_ast_types_verify_state;
    c_ast_types_verify_state = (CTestAstTypeVerify){0};
    return taken;
}

BUSTER_C_SHARED bool c_ast_types_verifying(void)
{
    return c_ast_types_verify_enabled;
}

BUSTER_C_SHARED CAstTypeVerifyMark c_ast_types_verify_begin(CParseResult const* result)
{
    return (CAstTypeVerifyMark){
        .types = result->type_count,
        .diagnostics = result->diagnostic_count,
        .array_bounds = result->array_bound_count,
        .members = result->member_count,
        .enum_members = result->enum_member_count,
        .entities = result->entity_count,
        .scopes = result->scope_count,
        .parameters = result->parameter_count,
    };
}

enum
{
    // Pairs the structural comparison may hold open; a deeper type compares
    // unequal, which a mismatch report then shows.
    C_AST_TYPES_COMPARE_LIMIT = 256,
};

// Exact structural equality of two type rows by an explicit worklist: same
// kind and qualifiers, and per kind the shape that distinguishes the type
// (array bound, function parameters and return, aggregate identity). It is
// stricter than compatibility, which would let a qualified copy stand in for
// its base.
BUSTER_GLOBAL_LOCAL bool c_ast_types_structurally_equal(CParseResult const* result, CTypeId left, CTypeId right)
{
    CTypeId pending_left[C_AST_TYPES_COMPARE_LIMIT];
    CTypeId pending_right[C_AST_TYPES_COMPARE_LIMIT];
    u32 count = 0;
    bool equal = true;
    pending_left[count] = left;
    pending_right[count] = right;
    count += 1;
    while (equal && count)
    {
        count -= 1;
        CTypeId a = pending_left[count];
        CTypeId b = pending_right[count];
        if (a.value != b.value)
        {
            equal = a.value < result->type_count && b.value < result->type_count;
            CType const* x = equal ? result->types + a.value : 0;
            CType const* y = equal ? result->types + b.value : 0;
            equal = equal && x->kind == y->kind && x->is_const == y->is_const && x->is_volatile == y->is_volatile &&
                    x->is_restrict == y->is_restrict && x->is_atomic == y->is_atomic;
            if (equal && (x->kind == C_TYPE_POINTER || x->kind == C_TYPE_ARRAY || x->kind == C_TYPE_VECTOR))
            {
                if (x->kind == C_TYPE_ARRAY)
                {
                    bool x_bound = x->array_bound < result->array_bound_count;
                    bool y_bound = y->array_bound < result->array_bound_count;
                    equal = x_bound == y_bound;
                    if (equal && x_bound)
                    {
                        CArrayBound const* p = result->array_bounds + x->array_bound;
                        CArrayBound const* q = result->array_bounds + y->array_bound;
                        equal = p->token_start == q->token_start && p->token_count == q->token_count && p->has_inferred_count == q->has_inferred_count &&
                                p->inferred_count == q->inferred_count && p->is_star == q->is_star;
                    }
                }
                equal = equal && x->vector_byte_size == y->vector_byte_size && count < C_AST_TYPES_COMPARE_LIMIT;
                if (equal)
                {
                    pending_left[count] = x->element_type;
                    pending_right[count] = y->element_type;
                    count += 1;
                }
            }
            else if (equal && x->kind == C_TYPE_FUNCTION)
            {
                equal = x->parameter_count == y->parameter_count && x->is_variadic == y->is_variadic && x->is_unprototyped == y->is_unprototyped &&
                        count + 1 + x->parameter_count <= C_AST_TYPES_COMPARE_LIMIT &&
                        (u64)x->parameter_start + x->parameter_count <= result->parameter_count &&
                        (u64)y->parameter_start + y->parameter_count <= result->parameter_count;
                if (equal)
                {
                    pending_left[count] = x->return_type;
                    pending_right[count] = y->return_type;
                    count += 1;
                    for (u32 parameter = 0; parameter < x->parameter_count; parameter += 1)
                    {
                        pending_left[count] = result->parameters[x->parameter_start + parameter].type;
                        pending_right[count] = result->parameters[y->parameter_start + parameter].type;
                        count += 1;
                    }
                }
            }
            else if (equal && (x->kind == C_TYPE_STRUCT || x->kind == C_TYPE_UNION || x->kind == C_TYPE_ENUM))
            {
                equal = x->definition_start && x->definition_start == y->definition_start && string_equal(x->tag, y->tag) &&
                        x->tag_scope.value == y->tag_scope.value;
            }
        }
    }
    return equal;
}

BUSTER_C_SHARED void c_ast_types_verify_end(CTypeParseMachine* machine, CParseResult* result, CAstTypeVerifyMark mark, CAstTypeAnswer answer, u32 start,
                                            u32 end, bool machine_valid, CTypeId machine_type, CTypeId* type_out)
{
    u32 reasons = 0;
    if (!machine_valid)
    {
        reasons |= C_TEST_AST_TYPE_MISMATCH_VALIDITY;
    }
    else
    {
        if (!c_ast_types_structurally_equal(result, answer.type, machine_type))
        {
            reasons |= C_TEST_AST_TYPE_MISMATCH_TYPE;
        }
        if (machine->validate_expression_constraints && machine->expression_constraint.length)
        {
            reasons |= C_TEST_AST_TYPE_MISMATCH_CONSTRAINT;
        }
        if (machine->result_nonplace_projection != answer.nonplace_projection)
        {
            reasons |= C_TEST_AST_TYPE_MISMATCH_NONPLACE;
        }
    }
    if (result->diagnostic_count != mark.diagnostics)
    {
        reasons |= C_TEST_AST_TYPE_MISMATCH_DIAGNOSTIC;
    }
    if (result->type_count != mark.types || result->array_bound_count != mark.array_bounds || result->member_count != mark.members ||
        result->enum_member_count != mark.enum_members || result->entity_count != mark.entities || result->scope_count != mark.scopes ||
        result->parameter_count != mark.parameters)
    {
        reasons |= C_TEST_AST_TYPE_MISMATCH_TABLES;
    }
    CTestAstTypeVerify* state = &c_ast_types_verify_state;
    state->compared += 1;
    if (reasons)
    {
        state->mismatches += 1;
        for (u32 bit = 0; bit < BUSTER_ARRAY_LENGTH(state->reason_counts); bit += 1)
        {
            state->reason_counts[bit] += (reasons >> bit) & 1;
        }
        if (state->first_count < BUSTER_ARRAY_LENGTH(state->first))
        {
            CTestAstTypeMismatch* record = &state->first[state->first_count];
            state->first_count += 1;
            record->start = start;
            record->end = end;
            record->node_kind = answer.node_kind;
            record->reasons = reasons;
            record->tree_type_kind = answer.type.value < result->type_count ? (u32)result->types[answer.type.value].kind : C_TYPE_COUNT;
            record->machine_type_kind = machine_valid && machine_type.value < result->type_count ? (u32)result->types[machine_type.value].kind : C_TYPE_COUNT;
            record->machine_valid = machine_valid;
            record->checked = machine->validate_expression_constraints;
        }
    }
    c_ast_types_publish(machine, result, answer, end);
    *type_out = answer.type;
}

// Answers one range of an analyzed function body for a unit test, on a private
// machine and a private typed body, as the validation loop would: the model is
// the finished analysis, so the immutable scalar rows are published into it
// first. The named function must have a body.
CTestAstTypeProbe c_test_ast_type_probe(Arena* scratch, CPreprocessResult preprocess, CParseResult* result, CAst const* ast, String8 function, u32 start,
                                        u32 end, bool checked)
{
    CTestAstTypeProbe probe = {.type = C_TYPE_ID_INVALID, .kind = C_TYPE_INVALID, .status = C_TEST_AST_TYPE_PROBE_NO_BODY};
    CDeclaration const* declaration = 0;
    for (u32 index = 0; index < result->declaration_count && !declaration; index += 1)
    {
        CDeclaration const* candidate = result->declarations + index;
        declaration = candidate->kind == C_DECLARATION_FUNCTION && candidate->is_definition && candidate->body_token_count &&
                              string_equal(candidate->name, function)
                          ? candidate
                          : 0;
    }
    if (declaration)
    {
        TemporalArena temporary = scratch_begin(&scratch, 1);
        u32 capacity = (u32)preprocess.token_count + 64;
        CTypeParseMachine machine = {
            .frames = arena_allocate(temporary.arena, CTypeParseFrame, capacity),
            .frame_checkpoints = arena_allocate(temporary.arena, CParseResult, capacity),
            .mutations = arena_allocate(temporary.arena, CTypeMutation, capacity),
            .expression_tasks = arena_allocate(temporary.arena, CParseExpressionTypeTask, capacity),
            .scratch_arena = temporary.arena,
            .layout_cache = {.tokens = preprocess.tokens},
            .frame_capacity = capacity,
            .mutation_capacity = capacity,
            .expression_task_capacity = capacity,
            .validate_expression_constraints = checked,
            .syntax_tree = ast,
        };
        CTypeId scalar_types[C_TYPE_COUNT];
        memset(scalar_types, 0xff, sizeof(scalar_types));
        CTypeId* previous_scalars = result->expression_scalar_types;
        result->expression_scalar_types = scalar_types;
        for (u32 kind = C_TYPE_VOID; kind <= C_TYPE_NULLPTR; kind += 1)
        {
            if (kind != C_TYPE_VA_LIST)
            {
                c_parse_expression_scalar_type(result, (CTypeKind)kind);
            }
        }
        c_ast_types_bodies_prepare(&machine, result);
        c_ast_types_body_begin(&machine, result, &preprocess, declaration);
        probe.status = C_TEST_AST_TYPE_PROBE_UNTYPED;
        if (machine.ast_types)
        {
            probe.nodes_typed = machine.ast_types->local_statistics.nodes_typed;
            probe.nodes_accepted = machine.ast_types->local_statistics.nodes_accepted;
            CScopeId scope = c_parse_scope_for_token(result, declaration->scope, start);
            CAstTypeAnswer answer = c_ast_types_answer(&machine, &preprocess, result, scope, start, end);
            probe.status = (u32)answer.status;
            probe.node_kind = answer.node_kind;
            probe.type = answer.type;
            probe.kind = answer.type.value < result->type_count ? result->types[answer.type.value].kind : C_TYPE_INVALID;
            probe.nonplace_projection = answer.nonplace_projection;
        }
        c_ast_types_body_end(&machine);
        result->expression_scalar_types = previous_scalars;
        scratch_end(temporary);
    }
    return probe;
}
#endif
