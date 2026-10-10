// The tree expression typer (GitHub #3102, stages 1 to 3): the types of
// function-body and file-scope initializer expressions, computed once per body
// or initializer over the implicit postorder syntax tree (c_ast.h) instead of
// by running the speculative type machine on token ranges.
//
// The contract. The type machine (CTypeParseMachine in c_parse.c) is the
// authority. This file may only ANSWER a type query when the answer is exactly
// what the machine would return for that token range: valid, the same type, no
// constraint in checked mode, the same nonplace-projection fact, and the same
// change to the type tables. For every node it does not vouch for it DECLINES,
// and the machine runs as it always has, so the machine remains the only
// producer of a diagnostic and of every answer this file is unsure of. When in
// doubt a node is declined. The eager pass mints no type rows: it returns ids
// that already exist (entity, member, element, return and typedef types,
// operand rows, the immutable scalar rows in
// CParseResult.expression_scalar_types, and the primitive and pointer rows the
// machine interns, CTypeInterning in c_internal.h), so an accepted node's
// machine run appends nothing. Stage 3 adds the shapes whose rows are interned:
// `&`, and a cast or compound literal whose type name is a typedef name or a
// run of primitive specifier words under plain `*`s, each accepted only once
// every row it reads is interned. It declines every shape whose machine answer
// appends a row: a qualified member or array element, a string literal, an
// array operand that decays, a pointer conditional, a qualified operand losing
// its qualifiers without a recorded unqualified row, and a cast or compound
// literal to any other type name (a qualified typedef, a tag, another
// declarator). The exceptions are replays of one string-literal token, whose
// array row the machine appends. A checked query of a cast whose operand is
// that token, perhaps parenthesized as `S8()` writes it, types the literal
// too; and a query of the token alone, perhaps parenthesized as `S8()`'s
// `BUSTER_ARRAY_LENGTH` writes it, is the literal. The answer then carries
// that token (CAstTypeAnswer.replay_*), and c_parse_expression_tree_query makes
// exactly the machine's task: the memo probe and, on a miss, the string leaf.
// For the token alone the replay's row is the answer (C_AST_TYPE_STRING).
//
// Ownership and lifetime. A caller that built the tree (the driver's
// -fc-ast-pilot) passes it in CParserResult.ast; c_analyze_semantics_core puts
// it in CTypeParseMachine.syntax_tree. c_parse_validate_lowering_constraints
// builds the function-definition and declaration index once
// (c_ast_types_bodies_prepare, in the machine's scratch arena beside the other
// tables built outside the per-body checkpoints) and, per function body, calls
// c_ast_types_body_begin after the binder has recorded every identifier use and
// c_ast_types_body_end when the body is done. The per-body arrays are allocated
// in the machine's scratch arena above the body's validation mark and are
// released with it: nothing here outlives a body, because a speculative
// rollback may truncate the type rows an entry refers to. A body is typed only
// while the machine is idle (frame_count 0), and a query is answered only
// under the same condition.
//
// File-scope initializers. c_parse_validate_static_initializers makes nearly
// every query outside a body, so before it validates a file-scope object's
// initializer it calls c_ast_types_initializer_begin, and
// c_ast_types_initializer_end after. The begin reserves the arrays for that
// initializer's subtree in the scratch arena (they go back after the
// validation), and the subtree is typed as a body is, but only when the
// first query the literal fast path does not take reaches it: an initializer
// asked only about lone literals, a numeric table, is never typed, and those
// queries keep the literal path (c_ast_types_waiting). The typing runs inside
// a validator's scratch use, so it writes only the reserved arrays. The
// binder records no identifier use there, and
// where it records none the machine resolves an identifier, and a cast's or
// compound literal's typedef name, by spelling in the query's scope. The pass
// does the same lookup in the initializer's scope, keeps the entity it found
// on the node (CAstTypeBody.entities) and marks it, so the query repeats the
// lookup in its own scope (c_ast_types_lookups_agree). No memo exists at file
// scope; an answer leaves only the machine state.
//
// The eager pass. c_ast_types_type_body is one forward loop over the body's
// node interval in index order. Children precede parents in a postorder tree, so
// each node's operand types are known when the node is reached. For every
// expression node it computes the token span [first, end) (the investigation's
// span rules, validated over 1.75 M nodes: a node's span starts at its
// leftmost operand or operator and ends at its last token, widened over any
// balanced parentheses that wrap an operand, which the tree does not record),
// links the node into a per-start-token chain, and computes a type and flags.
// A node is accepted only when every operand the machine types for it is, so
// by induction an accepted node's whole machine run appends nothing. It is
// checked-safe when its operands are and its own checked-mode rule raises no
// constraint.
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
// before typing), no enumerator list is half parsed, and every callee and
// typedef name in the subtree resolves by spelling in the query's scope as
// the binder bound it (c_ast_types_lookups_agree).
//
// Accepted kinds and why each is exact (the machine paths are in c_parse.c;
// the operator rules are the machine's own functions, shared through
// c_internal.h, applied to operand types already held):
//   IDENTIFIER   bound through identifier_use_by_token_plus_one to an object,
//                function, parameter, local or enumerator: the entity's type,
//                as c_parse_direct_expression_base answers the single token.
//                In an initializer, an unbound name is looked up instead, as
//                that function falls back to doing.
//   NUMBER,      c_parse_expression_leaf_without_cast itself, the leaf the
//   CHARACTER    machine's literal path calls; it returns an immutable scalar
//                row and appends none while every scalar row is published
//                (c_ast_types_scalars_published).
//   STRING       one token: never accepted (its machine answer appends the
//                literal's array row), but a query of it alone replays the
//                machine's leaf call, whose row is the answer.
//   MEMBER,      a struct or union member through c_parse_member_type (which
//   MEMBER_ARROW also searches anonymous members), as c_parse_direct_expression_postfix
//                does, over a base of a kind listed here before CALL (the
//                machine types any other base in its leaf); declined when the
//                aggregate is qualified, because the machine then appends a
//                qualified copy of the member type.
//   INDEX        an array or pointer base with an integer index, as the
//                machine's subscript operation; declined for a qualified array
//                (a qualified element row), a vector base and the reversed
//                form `i[a]`.
//   DEREFERENCE  a pointer or array operand: its element type.
//   CALL         `name(...)` whose callee name is a bound object, function,
//                parameter or local of function or pointer-to-function type
//                and not a builtin, vendor builtin or sizeof-like spelling: the
//                function's return type. Arguments are not typed by the
//                machine and are not inspected here.
//   binary       `* / % + - << >> < > <= >= == != & ^ | && ||`: the operation
//                switch of c_type_parse_sizeof_step (c_ast_types_binary), with
//                c_parse_expression_arithmetic_type, the bit-field widths it
//                reads (c_ast_types_operand_width) and the checked-mode operand
//                rule (c_ast_types_binary_safe).
//   unary        `+ - ~ !`: promotion with the operand's bit-field width;
//                complex and suitable vector operands keep their type.
//   assignment   `=` and every compound form: the left operand's unqualified
//                row, when it exists. The machine checks no constraint here.
//   COMMA        the right operand's decayed row, when it exists.
//   CONDITIONAL, c_parse_conditional_expression_type over arithmetic, void,
//   _OMITTED     vector and same-row aggregate arms; declined when the range
//                holds a top-level comma or assignment, which the machine
//                splits at instead of reading a conditional.
//   CAST,        a type name that is one typedef name or a run of primitive
//   COMPOUND_    specifier words, then plain `*`s (c_ast_types_type_name): the
//   LITERAL      typedef's row or the interned primitive row, under interned
//                pointer rows. Without constraint checks the machine types
//                neither the cast's operand nor the literal's initializer;
//                with them a cast's operand is typed and the scalar conversion
//                rule applies, and a lone string-literal operand is replayed.
//   ADDRESS      the interned pointer to the operand's row; checked, the
//                operand must have a place's shape, the machine's `&` rule.
//   SIZEOF_*,    size_t for exactly the spellings the machine's leaf reads; it
//   ALIGNOF_*    types nothing inside.
// An operand the machine scans but does not type (a cast's operand without
// constraint checks, a `sizeof` expression) must hold no type name, because
// the operator scan reads parenthesized type names at cast positions through
// a reader that appends rows (c_ast_types_holds_type_name).
// Nothing sets result_nonplace_projection except `__real__`/`__imag__` of a
// real operand, which the typer declines, so no accepted node carries the
// fact.
//
// Verification (tests builds only). c_test_ast_type_verify_set makes every tree
// answer also run without the tree (through the literal path or the machine)
// and be compared on validity, structural type equality, constraint, nonplace
// fact, diagnostics and the sizes of the type tables; a replayed answer's rows
// are taken back after the replay (c_ast_types_verify_hold_replay) and the
// machine must append the same rows again. The tree answer is still returned.
// c_test_ast_type_probe answers one range of an analyzed function body for
// unit tests.
//
// Layout map (search these symbols):
//   CAstTypeBodyIndex, CAstTypeBody              top-level index, per-region arrays
//   c_ast_types_bodies_prepare, c_ast_types_find_definition,
//   c_ast_types_find_initializer
//   c_ast_types_region_create, _fill             reserves, then types, one region
//   c_ast_types_body_begin, c_ast_types_body_end the per-body entry points
//   c_ast_types_initializer_begin, _end,         the per-initializer entry points
//   c_ast_types_waiting
//   c_ast_types_looked_up_entity                 an initializer's unbound names
//   c_ast_types_span, c_ast_types_expand         span rules
//   c_ast_types_type_body, c_ast_types_type_node the eager pass and its rules
//   c_ast_types_binary, c_ast_types_unary,       stage-2 operator rules
//   c_ast_types_conditional
//   c_ast_types_interned, c_ast_types_type_name,  stage-3 rules over interned
//   c_ast_types_cast,                             rows
//   c_ast_types_compound_literal,
//   c_ast_types_address
//   c_ast_types_locate, c_ast_types_answer       query lookup and the decision
//   c_ast_types_lookups_agree                    query-scope name checks
//   c_ast_types_publish                          machine state after an answer
//   c_ast_types_verify_*, c_test_*               the differential (tests builds),
//                                                c_ast_types_verify_hold_replay
//                                                and the skipped designator probes

#include "c_internal.h"
#include <buster/lib/compiler/frontend/c/c_ast.h>
#include <buster/lib/compiler/frontend/c/c_parse_internal.h>

#define C_AST_TYPE_NONE UINT32_MAX

// Per-node flag bits. ACCEPTED: the machine's answer without constraint
// checks is the node's type and appends nothing. SAFE: with constraint checks
// it also raises none.
#define C_AST_TYPE_FLAG_ACCEPTED (1u << 0)
#define C_AST_TYPE_FLAG_SAFE (1u << 1)
#define C_AST_TYPE_FLAG_NONPLACE (1u << 2)
// The node's type rests on a name the machine resolves by spelling in the
// query's scope: a call's callee, or the typedef name of a cast or compound
// literal. The query repeats that lookup (c_ast_types_lookups_agree).
#define C_AST_TYPE_FLAG_LOOKUP (1u << 3)
// The node or an operand below it carries C_AST_TYPE_FLAG_LOOKUP.
#define C_AST_TYPE_FLAG_LOOKUP_BELOW (1u << 4)
// A cast whose operand is one string-literal token, alone or parenthesized.
// With constraint checks
// the machine types that operand, which appends the literal's array row, so a
// checked query of this node alone is answered by replaying that one leaf
// call (c_parse_expression_tree_query). The node is never SAFE through it: a
// parent's machine run would type the literal too.
#define C_AST_TYPE_FLAG_REPLAY (1u << 5)
// A string literal of one token. It is not ACCEPTED, since its machine answer
// appends its array row, so every parent declines it; a query of the node
// alone is answered by replaying the machine's leaf call, whose row is the
// answer (C_AST_TYPE_STRING).
#define C_AST_TYPE_FLAG_STRING (1u << 6)

// INIT_DECLARATOR's presence bit for an initializer, its last child (c_ast.h).
#define C_AST_TYPE_INIT_DECLARATOR_INITIALIZER (1u << 2)

// The `{` token of every top-level function definition, ascending, and the
// FUNCTION_DEFINITION node it opens; and the first token of every top-level
// DECLARATION, ascending, with its node.
struct CAstTypeBodyIndex
{
    u32* braces;
    u32* nodes;
    u32* declaration_tokens;
    u32* declaration_nodes;
    u32 count;
    u32 declaration_count;
    // Every scalar row is published (c_ast_types_scalars_published), so the
    // literal leaf cannot append one.
    bool scalars_published;
};

// One typed function body, or one file-scope object's initializer. Arrays are
// indexed by node - begin.
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
    // An initializer only: the entity a name the binder recorded no use for
    // resolved to by spelling in `scope`, kept on each node that carries
    // C_AST_TYPE_FLAG_LOOKUP for an identifier, a cast or a compound literal.
    // Null in a body, where those names are bound.
    u32* entities;
    // The previous node (relative index + 1) that starts at the same token, 0
    // for none; start_head holds the newest, which is the outermost.
    u32* link;
    u32* start_head;
    u8* flags;
    // A member node's bit-field width (0 for an ordinary member); read only
    // for MEMBER and MEMBER_ARROW nodes.
    u8* widths;
    Target target;
    // An initializer's lookup scope.
    CScopeId scope;
    u32 begin;
    // The body's COMPOUND_STATEMENT, or the initializer's root node.
    u32 node;
    // The body's tokens: [token_start, token_end), the braces excluded; or
    // the initializer's.
    u32 token_start;
    u32 token_end;
    u32 token_total;
    bool scalars_published;
    // GNU dialect: the machine reads `c ?: b` as a conditional.
    bool gnu;
    // An initializer whose eager pass waits for its first query that the
    // literal fast path does not answer (c_ast_types_region_fill).
    bool waiting;
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
        u32 declaration_count = 0;
        u32 cursor = ast->root;
        while (cursor > floor && ast->extents[cursor - 1] && ast->extents[cursor - 1] <= cursor)
        {
            u32 child = cursor - 1;
            count += c_ast_types_top_level_definition(ast, child);
            declaration_count += ast->kinds[child] == C_AST_DECLARATION;
            cursor = c_ast_subtree_begin(ast, child);
        }
        CAstTypeBodyIndex* index = arena_allocate(machine->scratch_arena, CAstTypeBodyIndex, 1);
        index->braces = arena_allocate(machine->scratch_arena, u32, count + 1);
        index->nodes = arena_allocate(machine->scratch_arena, u32, count + 1);
        index->declaration_tokens = arena_allocate(machine->scratch_arena, u32, declaration_count + 1);
        index->declaration_nodes = arena_allocate(machine->scratch_arena, u32, declaration_count + 1);
        index->count = count;
        index->declaration_count = declaration_count;
        index->scalars_published = c_ast_types_scalars_published(result);
        u32 slot = count;
        u32 declaration_slot = declaration_count;
        cursor = ast->root;
        while ((slot || declaration_slot) && cursor > floor && ast->extents[cursor - 1] && ast->extents[cursor - 1] <= cursor)
        {
            u32 child = cursor - 1;
            if (slot && c_ast_types_top_level_definition(ast, child))
            {
                slot -= 1;
                index->braces[slot] = ast->tokens[child - 1];
                index->nodes[slot] = child;
            }
            if (declaration_slot && ast->kinds[child] == C_AST_DECLARATION)
            {
                declaration_slot -= 1;
                index->declaration_tokens[declaration_slot] = ast->tokens[child];
                index->declaration_nodes[declaration_slot] = child;
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

// The initializer node of the top-level declarator whose initializer starts at
// `start`: in the last top-level DECLARATION that starts at or before it, the
// last INIT_DECLARATOR anchored (at its name) before it, whose initializer is
// its last child. C_AST_TYPE_NONE when that declarator has none.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_find_initializer(CAst const* ast, CAstTypeBodyIndex const* index, u32 start)
{
    u32 low = 0;
    u32 high = index->declaration_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2;
        if (index->declaration_tokens[middle] <= start)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    u32 declaration = low ? index->declaration_nodes[low - 1] : C_AST_TYPE_NONE;
    u32 initializer = C_AST_TYPE_NONE;
    u32 floor = declaration != C_AST_TYPE_NONE ? c_ast_subtree_begin(ast, declaration) : 0;
    u32 cursor = declaration != C_AST_TYPE_NONE ? declaration : 0;
    bool searching = cursor > floor;
    while (searching)
    {
        u32 child = cursor - 1;
        if (ast->kinds[child] == C_AST_INIT_DECLARATOR && ast->tokens[child] < start)
        {
            initializer = (ast->data[child] & C_AST_TYPE_INIT_DECLARATOR_INITIALIZER) && child >= 1 ? child - 1 : C_AST_TYPE_NONE;
            searching = false;
        }
        else
        {
            cursor = c_ast_subtree_begin(ast, child);
            searching = cursor > floor;
        }
    }
    return initializer;
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
    body->flags[relative] = (u8)(C_AST_TYPE_FLAG_ACCEPTED | flags);
}

// The flags a node takes from the operands the machine types for it: safe only
// when every one is, and the lookup mark when any carries it.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_types_inherit(u32 left, u32 right)
{
    return (left & right & C_AST_TYPE_FLAG_SAFE) | ((left | right) & C_AST_TYPE_FLAG_LOOKUP_BELOW);
}

// The kinds whose base a member access may have. Over any other base the
// machine types `base.name` in its leaf rather than through operator tasks, so
// stage 2 leaves those to it.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_member_base_kind(u32 kind)
{
    return kind == C_AST_IDENTIFIER || kind == C_AST_NUMBER || kind == C_AST_CHARACTER || kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW ||
           kind == C_AST_INDEX || kind == C_AST_DEREFERENCE || kind == C_AST_CALL;
}

// The published scalar row of `kind`; invalid before publication, when the
// machine's scalar answer would append one.
BUSTER_GLOBAL_LOCAL CTypeId c_ast_types_scalar(CAstTypeBody const* body, CTypeKind kind)
{
    bool scalar = body->scalars_published && kind >= C_TYPE_VOID && kind <= C_TYPE_NULLPTR && kind != C_TYPE_VA_LIST;
    return scalar ? c_parse_expression_scalar_type(body->result, kind) : C_TYPE_ID_INVALID;
}

// The row c_parse_unqualified_type answers for a valid `id` when that row
// already exists, C_AST_TYPE_NONE when it would append an unqualified copy.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_unqualified_row(CParseResult const* result, CTypeId id)
{
    CType const* type = result->types + id.value;
    bool linked = type->has_unqualified_type && type->unqualified_type.value < result->type_count;
    bool plain = !type->is_const && !type->is_volatile && !type->is_restrict && !type->is_atomic;
    return linked ? type->unqualified_type.value : plain ? id.value : C_AST_TYPE_NONE;
}

// The row c_parse_auto_decay_type answers for a valid `id` when it appends
// none: an existing unqualified row that is neither an array nor a function.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_decayed_row(CParseResult const* result, CTypeId id)
{
    u32 row = c_ast_types_unqualified_row(result, id);
    CTypeKind kind = row != C_AST_TYPE_NONE ? result->types[row].kind : C_TYPE_INVALID;
    return kind == C_TYPE_ARRAY || kind == C_TYPE_FUNCTION ? C_AST_TYPE_NONE : row;
}

// The bit-field width c_parse_expression_bit_field_width reads from an
// operand's tokens, or C_AST_TYPE_NONE. It looks only at a range ending in
// `. name` or `-> name`: for a member node that is the member's own width,
// recorded when the member was typed, and any other node that ends that way
// (`x, s.f`, `(T)s.f`) is left to the machine.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_operand_width(CAstTypeBody const* body, u32 node)
{
    u32 relative = node - body->begin;
    u32 kind = body->ast->kinds[node];
    u32 first = body->first[relative];
    u32 end = body->end[relative];
    bool member_tail = end >= first + 3 && body->tokens[end - 1].kind == C_TOKEN_IDENTIFIER &&
                       (c_ast_types_punctuator_at(body, end - 2, C_PUNCTUATOR_DOT) || c_ast_types_punctuator_at(body, end - 2, C_PUNCTUATOR_ARROW));
    return kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW ? body->widths[relative] : member_tail ? C_AST_TYPE_NONE : 0;
}

// Whether a subtree holds a type name. The machine's operator scan reads every
// parenthesized group at a cast position through its machineless type reader,
// which appends rows for a primitive or pointer type name, so an operand the
// tree does not type must hold none (an accepted one holds only lone typedef
// names, which append nothing).
BUSTER_GLOBAL_LOCAL bool c_ast_types_holds_type_name(CAst const* ast, u32 node)
{
    bool found = false;
    for (u32 cursor = c_ast_subtree_begin(ast, node); !found && cursor <= node; cursor += 1)
    {
        found = ast->kinds[cursor] == C_AST_TYPE_NAME;
    }
    return found;
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

// In an initializer, the entity a name the binder recorded no use for (or
// whose use resolved to nothing) resolves to by spelling in its scope, as the
// machine falls back to that lookup; null in a body. The node keeps it for the
// query's repeat of the lookup.
BUSTER_GLOBAL_LOCAL CEntity const* c_ast_types_looked_up_entity(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 relative, u32 token)
{
    CEntity const* entity = 0;
    if (body->entities)
    {
        CEntityId looked = c_parse_lookup_entity_token(body->result, preprocess->spelling_base, body->scope, &preprocess->tokens[token]);
        entity = looked.value < body->result->entity_count ? body->result->entities + looked.value : 0;
        body->entities[relative] = looked.value;
    }
    return entity;
}

BUSTER_GLOBAL_LOCAL void c_ast_types_identifier(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    u32 token = body->ast->tokens[node];
    bool identifier = token < body->token_total && body->tokens[token].kind == C_TOKEN_IDENTIFIER;
    CEntity const* entity = identifier ? c_ast_types_bound_entity(result, token) : 0;
    u32 flags = C_AST_TYPE_FLAG_SAFE;
    if (identifier && !entity && body->entities)
    {
        entity = c_ast_types_looked_up_entity(body, preprocess, relative, token);
        flags |= C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW;
    }
    if (entity && c_ast_types_value_entity(entity, true) && entity->type.value < result->type_count)
    {
        c_ast_types_accept(body, relative, entity->type, flags);
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
            c_ast_types_accept(body, relative, type, C_AST_TYPE_FLAG_SAFE);
        }
    }
}

// `base.name` and `base->name`. The machine walks the whole postfix chain from
// one base type; per node that is this step, with the aggregate resolved the
// way c_parse_direct_expression_postfix resolves it. The member's bit-field
// width is kept for the operator rules above it.
BUSTER_GLOBAL_LOCAL void c_ast_types_member(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative,
                                            bool arrow)
{
    CParseResult* result = body->result;
    u32 member_token = body->ast->tokens[node];
    u32 base_flags = body->flags[relative - 1];
    CTypeId aggregate = C_TYPE_ID_INVALID;
    if ((base_flags & C_AST_TYPE_FLAG_ACCEPTED) && c_ast_types_member_base_kind(body->ast->kinds[node - 1]) && member_token >= 1 &&
        member_token < body->token_total && body->tokens[member_token].kind == C_TOKEN_IDENTIFIER &&
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
            // of the member type.
            value = result->types + aggregate.value;
            bool plain = (value->kind == C_TYPE_STRUCT || value->kind == C_TYPE_UNION) && !value->is_const && !value->is_volatile &&
                         !value->is_restrict && !value->is_atomic;
            aggregate = plain ? aggregate : C_TYPE_ID_INVALID;
        }
    }
    if (aggregate.value != C_ID_UNDERLYING_INVALID)
    {
        CToken name = preprocess->tokens[member_token];
        u32 width = 0;
        CTypeId type = c_parse_member_type(machine->scratch_arena, result, aggregate, name.symbol, c_token_spelling(preprocess->spelling_base, name), &width,
                                           0, 0);
        if (type.value < result->type_count && width <= UINT8_MAX)
        {
            body->widths[relative] = (u8)width;
            c_ast_types_accept(body, relative, type, c_ast_types_inherit(base_flags, base_flags));
        }
    }
}

// `base[index]` over an array or pointer. The machine types both operands
// through its own tasks.
BUSTER_GLOBAL_LOCAL void c_ast_types_index(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    u32 base_node = c_ast_types_first_child(ast, node);
    u32 base = base_node - body->begin;
    u32 index = relative - 1;
    u32 base_flags = body->flags[base];
    u32 index_flags = body->flags[index];
    if ((base_flags & index_flags & C_AST_TYPE_FLAG_ACCEPTED) && base != index)
    {
        CType const* sequence = result->types + body->types[base].value;
        CType const* offset = result->types + body->types[index].value;
        bool qualified_array = sequence->kind == C_TYPE_ARRAY && (sequence->is_const || sequence->is_volatile);
        if ((sequence->kind == C_TYPE_ARRAY || sequence->kind == C_TYPE_POINTER) && !sequence->is_atomic && !qualified_array &&
            c_parse_expression_integer_kind(offset->kind) && sequence->element_type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, sequence->element_type, c_ast_types_inherit(base_flags, index_flags));
        }
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_types_dereference(CAstTypeBody* body, u32 relative)
{
    CParseResult* result = body->result;
    u32 operand_flags = body->flags[relative - 1];
    if (operand_flags & C_AST_TYPE_FLAG_ACCEPTED)
    {
        CType const* value = result->types + body->types[relative - 1].value;
        if ((value->kind == C_TYPE_POINTER || value->kind == C_TYPE_ARRAY) && !value->is_atomic && value->element_type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, value->element_type, c_ast_types_inherit(operand_flags, operand_flags));
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
// the entity the binding names (the LOOKUP flag), and otherwise the machine's
// fallback is the binding, which is what is read here.
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
                c_ast_types_accept(body, relative, function->return_type,
                                   C_AST_TYPE_FLAG_SAFE | C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_aggregate_kind(CTypeKind kind)
{
    return kind == C_TYPE_STRUCT || kind == C_TYPE_UNION;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_pointer_like_kind(CTypeKind kind)
{
    return kind == C_TYPE_POINTER || kind == C_TYPE_ARRAY || kind == C_TYPE_FUNCTION;
}

// Whether an operand is spelled as the literal `0`, which
// c_parse_range_is_null_pointer_constant accepts for its int row.
BUSTER_GLOBAL_LOCAL bool c_ast_types_zero_literal(CAstTypeBody const* body, CPreprocessResult const* preprocess, u32 node)
{
    u32 token = body->ast->tokens[node];
    bool number = body->ast->kinds[node] == C_AST_NUMBER && token < body->token_total;
    return number && string_equal(c_token_spelling(preprocess->spelling_base, preprocess->tokens[token]), S8("0"));
}

// Whether the machine's checked-mode operand rule for an arithmetic, shift,
// comparison or logical operator passes on these operand types (the
// constraint block before the operation switch in c_type_parse_sizeof_step).
// The cases it settles by spelling (a null pointer constant) or by
// compatibility pass only in the shapes decided here (the literal `0`, the
// same unqualified element row); any other one withholds the safe bit, which
// only sends the query to the machine.
BUSTER_GLOBAL_LOCAL bool c_ast_types_binary_safe(CAstTypeBody const* body, CPreprocessResult const* preprocess, u32 kind, u32 left_node, u32 right_node,
                                                 CTypeId left, CTypeId right)
{
    CParseResult const* result = body->result;
    CType const* left_type = result->types + left.value;
    CType const* right_type = result->types + right.value;
    CTypeKind left_kind = left_type->kind;
    CTypeKind right_kind = right_type->kind;
    bool logical = kind == C_AST_LOGICAL_AND || kind == C_AST_LOGICAL_OR;
    bool equality = kind == C_AST_EQUAL || kind == C_AST_NOT_EQUAL;
    bool compare = logical || equality || (kind >= C_AST_LESS && kind <= C_AST_GREATER_EQUAL);
    bool add = kind == C_AST_ADD;
    bool subtract = kind == C_AST_SUBTRACT;
    bool aggregate = c_ast_types_aggregate_kind(left_kind) || c_ast_types_aggregate_kind(right_kind);
    bool void_operand = left_kind == C_TYPE_VOID || right_kind == C_TYPE_VOID;
    bool null_operand = left_kind == C_TYPE_NULLPTR || right_kind == C_TYPE_NULLPTR;
    bool complex = c_type_kind_is_complex(left_kind) || c_type_kind_is_complex(right_kind);
    bool complex_operator = add || subtract || kind == C_AST_MULTIPLY || kind == C_AST_DIVIDE || equality || logical;
    bool null_valid = !null_operand || logical;
    if (null_operand && equality)
    {
        CTypeKind other = left_kind == C_TYPE_NULLPTR ? right_kind : left_kind;
        null_valid = other == C_TYPE_POINTER || other == C_TYPE_ARRAY || other == C_TYPE_NULLPTR;
    }
    bool integer_operator = kind == C_AST_SHIFT_LEFT || kind == C_AST_SHIFT_RIGHT || kind == C_AST_REMAINDER || kind == C_AST_BIT_AND ||
                            kind == C_AST_BIT_OR || kind == C_AST_BIT_XOR;
    bool vector = left_kind == C_TYPE_VECTOR || right_kind == C_TYPE_VECTOR;
    bool integers = c_parse_expression_integer_kind(left_kind) && c_parse_expression_integer_kind(right_kind);
    bool left_pointer = c_ast_types_pointer_like_kind(left_kind);
    bool right_pointer = c_ast_types_pointer_like_kind(right_kind);
    bool pointer_valid = !(left_pointer || right_pointer) || compare ||
                         (add && ((left_pointer && c_parse_expression_integer_kind(right_kind)) ||
                                  (right_pointer && c_parse_expression_integer_kind(left_kind)))) ||
                         (subtract && left_pointer && (right_pointer || c_parse_expression_integer_kind(right_kind)));
    if (compare && !logical && left_pointer != right_pointer)
    {
        u32 other_node = left_pointer ? right_node : left_node;
        CTypeKind other = left_pointer ? right_kind : left_kind;
        pointer_valid = other == C_TYPE_NULLPTR || (other == C_TYPE_INT && c_ast_types_zero_literal(body, preprocess, other_node));
    }
    if (left_pointer && right_pointer && subtract)
    {
        u32 left_element = left_type->element_type.value < result->type_count
                               ? c_ast_types_unqualified_row(result, left_type->element_type) : C_AST_TYPE_NONE;
        u32 right_element = right_type->element_type.value < result->type_count
                                ? c_ast_types_unqualified_row(result, right_type->element_type) : C_AST_TYPE_NONE;
        pointer_valid = left_element != C_AST_TYPE_NONE && left_element == right_element;
    }
    bool invalid = aggregate || void_operand || !null_valid || !pointer_valid || (!vector && integer_operator && !integers);
    return !invalid && !(complex && !complex_operator);
}

// The binary operators the machine splits a range at (its ARITHMETIC, SHIFT
// and COMPARE operations, `&&` and `||` among the last): the operation switch
// in c_type_parse_sizeof_step, over operand types already held. Array
// operands of `+` and `-` decay there, which appends a pointer row, so they
// are left to the machine.
BUSTER_GLOBAL_LOCAL void c_ast_types_binary(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    u32 kind = ast->kinds[node];
    u32 left_node = c_ast_types_first_child(ast, node);
    u32 right_node = node - 1;
    u32 left_flags = body->flags[left_node - body->begin];
    u32 right_flags = body->flags[right_node - body->begin];
    if ((left_flags & right_flags & C_AST_TYPE_FLAG_ACCEPTED) && left_node != right_node)
    {
        CTypeId left = body->types[left_node - body->begin];
        CTypeId right = body->types[right_node - body->begin];
        CTypeKind left_kind = result->types[left.value].kind;
        CTypeKind right_kind = result->types[right.value].kind;
        bool add = kind == C_AST_ADD;
        bool subtract = kind == C_AST_SUBTRACT;
        bool decays = (add || subtract) && (left_kind == C_TYPE_ARRAY || right_kind == C_TYPE_ARRAY);
        CTypeId type = C_TYPE_ID_INVALID;
        if ((kind >= C_AST_LESS && kind <= C_AST_NOT_EQUAL) || kind == C_AST_LOGICAL_AND || kind == C_AST_LOGICAL_OR)
        {
            type = c_ast_types_scalar(body, C_TYPE_INT);
        }
        else if (kind == C_AST_SHIFT_LEFT || kind == C_AST_SHIFT_RIGHT)
        {
            u32 width = c_ast_types_operand_width(body, left_node);
            CTypeKind promoted = width != C_AST_TYPE_NONE
                                     ? c_parse_expression_promoted_kind_with_width(body->target, c_parse_expression_value_kind(result, left), width)
                                     : C_TYPE_INVALID;
            type = c_parse_expression_integer_kind(promoted) ? c_ast_types_scalar(body, promoted) : C_TYPE_ID_INVALID;
        }
        else if (!decays && (add || subtract) && left_kind == C_TYPE_POINTER && c_parse_expression_integer_kind(right_kind))
        {
            type = left;
        }
        else if (!decays && add && right_kind == C_TYPE_POINTER && c_parse_expression_integer_kind(left_kind))
        {
            type = right;
        }
        else if (subtract && left_kind == C_TYPE_POINTER && right_kind == C_TYPE_POINTER)
        {
            type = c_ast_types_scalar(body, target_uses_llp64_data_model(body->target) ? C_TYPE_LONG_LONG : C_TYPE_LONG);
        }
        else if (!decays && body->scalars_published)
        {
            u32 left_width = c_ast_types_operand_width(body, left_node);
            u32 right_width = c_ast_types_operand_width(body, right_node);
            type = left_width != C_AST_TYPE_NONE && right_width != C_AST_TYPE_NONE
                       ? c_parse_expression_arithmetic_type(result, body->target, left, right, left_width, right_width)
                       : C_TYPE_ID_INVALID;
        }
        if (type.value < result->type_count)
        {
            u32 flags = c_ast_types_inherit(left_flags, right_flags);
            bool safe = c_ast_types_binary_safe(body, preprocess, kind, left_node, right_node, left, right);
            c_ast_types_accept(body, relative, type, safe ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE);
        }
    }
}

// Unary `+`, `-`, `~` and `!`: the machine's UNARY and LOGICAL_NOT operations.
// A complex operand keeps its type; a vector keeps it when its element suits
// the operator; anything else is promoted, with its bit-field width.
BUSTER_GLOBAL_LOCAL void c_ast_types_unary(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    u32 kind = body->ast->kinds[node];
    u32 operand_flags = body->flags[relative - 1];
    if (operand_flags & C_AST_TYPE_FLAG_ACCEPTED)
    {
        CTypeId operand = body->types[relative - 1];
        CType const* value = result->types + operand.value;
        bool complement = kind == C_AST_BIT_NOT;
        CTypeId type = C_TYPE_ID_INVALID;
        if (kind == C_AST_LOGICAL_NOT)
        {
            type = c_ast_types_scalar(body, C_TYPE_INT);
        }
        else if (c_type_kind_is_complex(value->kind))
        {
            type = operand;
        }
        else if (value->kind == C_TYPE_VECTOR)
        {
            CTypeKind element = value->element_type.value < result->type_count ? result->types[value->element_type.value].kind : C_TYPE_INVALID;
            bool valid = complement ? c_parse_expression_integer_kind(element) : c_parse_expression_real_kind(element);
            type = valid ? operand : C_TYPE_ID_INVALID;
        }
        else
        {
            u32 width = c_ast_types_operand_width(body, node - 1);
            CTypeKind promoted = width != C_AST_TYPE_NONE
                                     ? c_parse_expression_promoted_kind_with_width(body->target, c_parse_expression_value_kind(result, operand), width)
                                     : C_TYPE_INVALID;
            bool floating = promoted == C_TYPE_FLOAT16 || promoted == C_TYPE_BFLOAT16 || promoted == C_TYPE_FLOAT || promoted == C_TYPE_DOUBLE ||
                            promoted == C_TYPE_LONG_DOUBLE;
            type = c_parse_expression_integer_kind(promoted) || (!complement && floating) ? c_ast_types_scalar(body, promoted) : C_TYPE_ID_INVALID;
        }
        if (type.value < result->type_count)
        {
            u32 flags = c_ast_types_inherit(operand_flags, operand_flags);
            bool safe = kind != C_AST_LOGICAL_NOT || (!c_ast_types_aggregate_kind(value->kind) && value->kind != C_TYPE_VOID);
            c_ast_types_accept(body, relative, type, safe ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE);
        }
    }
}

// Simple and compound assignment: the machine's ASSIGN operation, the left
// operand's unqualified type, when that row exists. It checks no constraint.
BUSTER_GLOBAL_LOCAL void c_ast_types_assignment(CAstTypeBody* body, u32 node, u32 relative)
{
    u32 left_node = c_ast_types_first_child(body->ast, node);
    u32 left_flags = body->flags[left_node - body->begin];
    u32 right_flags = body->flags[relative - 1];
    if ((left_flags & right_flags & C_AST_TYPE_FLAG_ACCEPTED) && left_node + 1 != node)
    {
        u32 row = c_ast_types_unqualified_row(body->result, body->types[left_node - body->begin]);
        if (row != C_AST_TYPE_NONE)
        {
            c_ast_types_accept(body, relative, (CTypeId){.value = row}, c_ast_types_inherit(left_flags, right_flags));
        }
    }
}

// `a, b`: the right operand's decayed type, when that row exists.
BUSTER_GLOBAL_LOCAL void c_ast_types_comma(CAstTypeBody* body, u32 node, u32 relative)
{
    u32 left_node = c_ast_types_first_child(body->ast, node);
    u32 left_flags = body->flags[left_node - body->begin];
    u32 right_flags = body->flags[relative - 1];
    if ((left_flags & right_flags & C_AST_TYPE_FLAG_ACCEPTED) && left_node + 1 != node)
    {
        u32 row = c_ast_types_decayed_row(body->result, body->types[relative - 1]);
        if (row != C_AST_TYPE_NONE)
        {
            c_ast_types_accept(body, relative, (CTypeId){.value = row}, c_ast_types_inherit(left_flags, right_flags));
        }
    }
}

// Whether the machine's operator scan finds a comma or an assignment at the
// top level of [start, end), outside every bracket pair. The middle operand
// of `?:` is not bracketed, so `c ? m = 1 : n` holds one; the machine then
// splits the whole range there instead of reading a conditional.
BUSTER_GLOBAL_LOCAL bool c_ast_types_loose_operator(CAstTypeBody const* body, u32 start, u32 end)
{
    bool loose = false;
    for (u32 index = start; !loose && index < end; index += 1)
    {
        CToken token = body->tokens[index];
        if (c_punctuator_in_set(token.punctuator, C_PUNCTUATOR_SET_DELIMITER_OPEN))
        {
            u32 close = c_ast_types_match(body, index);
            loose = close == C_AST_TYPE_NONE || close >= end;
            index = loose ? index : close;
        }
        else
        {
            u32 precedence = c_parse_expression_operator_precedence(token);
            loose = precedence && precedence <= 2;
        }
    }
    return loose;
}

// `c ? a : b` and GNU `c ?: b`: c_parse_conditional_expression_type over the
// arms' types. The machine types the condition only when it checks
// constraints or the middle operand is omitted. Arms that decay into a new
// row, and the pointer and nullptr forms (a new composite pointer row, or a
// null-pointer-constant reading of the spelling), are left to the machine.
BUSTER_GLOBAL_LOCAL void c_ast_types_conditional(CAstTypeBody* body, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    bool omitted = ast->kinds[node] == C_AST_CONDITIONAL_OMITTED;
    u32 condition = c_ast_types_first_child(ast, node);
    u32 else_node = node - 1;
    u32 then_node = omitted ? condition : c_ast_subtree_begin(ast, else_node) - 1;
    u32 condition_flags = body->flags[condition - body->begin];
    u32 then_flags = body->flags[then_node - body->begin];
    u32 else_flags = body->flags[else_node - body->begin];
    bool arms = (then_flags & else_flags & C_AST_TYPE_FLAG_ACCEPTED) && condition < else_node && then_node < else_node && (!omitted || body->gnu) &&
                !c_ast_types_loose_operator(body, ast->tokens[node] + 1, body->end[relative]);
    u32 left = arms ? c_ast_types_decayed_row(result, body->types[then_node - body->begin]) : C_AST_TYPE_NONE;
    u32 right = arms ? c_ast_types_decayed_row(result, body->types[else_node - body->begin]) : C_AST_TYPE_NONE;
    CTypeId type = C_TYPE_ID_INVALID;
    if (left != C_AST_TYPE_NONE && right != C_AST_TYPE_NONE)
    {
        CType const* left_type = result->types + left;
        CType const* right_type = result->types + right;
        CTypeKind left_kind = left_type->kind;
        CTypeKind right_kind = right_type->kind;
        bool pointers = left_kind == C_TYPE_NULLPTR || right_kind == C_TYPE_NULLPTR || left_kind == C_TYPE_POINTER || right_kind == C_TYPE_POINTER;
        if (left_kind == right_kind && (left_kind == C_TYPE_VOID || left_kind == C_TYPE_NULLPTR))
        {
            type.value = left;
        }
        else if (left_kind == C_TYPE_VOID || right_kind == C_TYPE_VOID)
        {
            type.value = left_kind == C_TYPE_VOID ? left : right;
        }
        else if (!pointers && left == right && left_kind == C_TYPE_VECTOR)
        {
            type.value = left;
        }
        else if (!pointers && left_kind == right_kind && c_ast_types_aggregate_kind(left_kind))
        {
            bool plain = left == right && !left_type->is_const && !left_type->is_volatile && !left_type->is_restrict && !left_type->is_atomic;
            type.value = plain ? c_ast_types_unqualified_row(result, (CTypeId){.value = left}) : C_AST_TYPE_NONE;
        }
        else if (!pointers && body->scalars_published)
        {
            u32 left_width = c_ast_types_operand_width(body, then_node);
            u32 right_width = c_ast_types_operand_width(body, else_node);
            type = left_width != C_AST_TYPE_NONE && right_width != C_AST_TYPE_NONE
                       ? c_parse_expression_arithmetic_type(result, body->target, (CTypeId){.value = left}, (CTypeId){.value = right}, left_width,
                                                            right_width)
                       : C_TYPE_ID_INVALID;
        }
    }
    if (type.value < result->type_count)
    {
        CTypeKind condition_kind = (condition_flags & C_AST_TYPE_FLAG_ACCEPTED) ? result->types[body->types[condition - body->begin].value].kind
                                                                              : C_TYPE_INVALID;
        bool condition_safe = (condition_flags & C_AST_TYPE_FLAG_ACCEPTED) && (condition_flags & C_AST_TYPE_FLAG_SAFE) &&
                              !c_ast_types_aggregate_kind(condition_kind) && condition_kind != C_TYPE_VOID;
        u32 flags = c_ast_types_inherit(then_flags, else_flags) | (condition_flags & C_AST_TYPE_FLAG_LOOKUP_BELOW);
        c_ast_types_accept(body, relative, type, condition_safe ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE);
    }
}

// The interned row equal to `type` (c_parse_interned_type), which a machine
// run inside the interning window reads instead of appending. Only a body's
// queries run there (c_parse_validate_lowering_constraints' body loop); an
// initializer's run before the window opens, where the machine appends, so an
// initializer reads none.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE CTypeId c_ast_types_interned(CAstTypeBody const* body, CType type)
{
    return body->entities ? C_TYPE_ID_INVALID : c_parse_interned_type(body->result, type);
}

// Whether a specifier word is one c_parse_primitive_type reads into a plain
// row: the arithmetic and void words, `const` and `volatile`. `restrict` and
// `_Atomic` are left out, and so is every word another reader handles.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_primitive_word(u32 word)
{
    bool primitive = false;
    switch (word)
    {
    case C_AST_WORD_VOID:
    case C_AST_WORD_CHAR:
    case C_AST_WORD_SHORT:
    case C_AST_WORD_INT:
    case C_AST_WORD_LONG:
    case C_AST_WORD_FLOAT:
    case C_AST_WORD_DOUBLE:
    case C_AST_WORD_SIGNED:
    case C_AST_WORD_UNSIGNED:
    case C_AST_WORD_BOOL:
    case C_AST_WORD_COMPLEX:
    case C_AST_WORD_INT128:
    case C_AST_WORD_FLOAT16:
    case C_AST_WORD_BF16:
    case C_AST_WORD_CONST:
    case C_AST_WORD_VOLATILE:
    {
        primitive = true;
    }
    break;
    default:
    {
    }
    break;
    }
    return primitive;
}

// The row a cast's or compound literal's type name reads when every reader
// the type machine sends it through appends nothing: one typedef name, or a
// run of primitive specifier words, then plain `*`s. The operator scan reads
// such a name with c_parse_machineless_base_type and the leaf with its core
// frame, each followed by c_parse_pointer_chain; the typedef row is the
// entity's own (c_parse_qualified_typedef_type with no qualifier), and the
// primitive and pointer rows are interned (CTypeInterning in c_internal.h),
// so the name is accepted only when each of them is already interned. Any
// other type name builds rows (a qualified typedef, a tag, an array or
// function declarator, an attribute) and yields C_TYPE_ID_INVALID. The
// typedef name is the binder's in a body; in an initializer it is the bound
// one or, without a use, the spelling's in the initializer's scope, kept on the
// node at `relative`. *lookup_out says whether a typedef name was read, which
// the query looks up again (c_ast_types_lookups_agree). An initializer reads
// no interned row (c_ast_types_interned), so there a primitive word or a `*`
// declines.
BUSTER_GLOBAL_LOCAL CTypeId c_ast_types_type_name(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative, bool* lookup_out)
{
    CAst const* ast = body->ast;
    CParseResult const* result = body->result;
    u32 type_name = c_ast_types_first_child(ast, node);
    u32 open = ast->tokens[node];
    u32 close = c_ast_types_match(body, open);
    u32 star = close;
    while (close != C_AST_TYPE_NONE && star > open + 1 && c_ast_types_punctuator_at(body, star - 1, C_PUNCTUATOR_STAR))
    {
        star -= 1;
    }
    u32 specifiers = ast->kinds[type_name] == C_AST_TYPE_NAME && close != C_AST_TYPE_NONE && star > open + 1 ? c_ast_types_first_child(ast, type_name)
                                                                                                         : C_AST_TYPE_NONE;
    u32 first = specifiers != C_AST_TYPE_NONE ? c_ast_subtree_begin(ast, specifiers) : 0;
    // Every specifier is a leaf on its own token, so the specifier list's
    // subtree is its children, one per word before the first `*`.
    bool words = specifiers != C_AST_TYPE_NONE && ast->kinds[specifiers] == C_AST_DECL_SPECIFIERS && specifiers - first == star - (open + 1);
    for (u32 child = first; words && child < specifiers; child += 1)
    {
        u32 kind = ast->kinds[child];
        words = ast->tokens[child] == open + 1 + (child - first) &&
                ((kind == C_AST_SPECIFIER_WORD && c_ast_types_primitive_word(ast->data[child])) || (kind == C_AST_TYPEDEF_NAME && specifiers - first == 1));
    }
    CTypeId type = C_TYPE_ID_INVALID;
    bool lookup = false;
    if (words && ast->kinds[first] == C_AST_TYPEDEF_NAME)
    {
        bool identifier = open + 1 < body->token_total && body->tokens[open + 1].kind == C_TOKEN_IDENTIFIER;
        CEntity const* entity = identifier ? c_ast_types_bound_entity(body->result, open + 1) : 0;
        if (entity && body->entities)
        {
            body->entities[relative] = (u32)(entity - result->entities);
        }
        else if (identifier && !entity)
        {
            entity = c_ast_types_looked_up_entity(body, preprocess, relative, open + 1);
        }
        type = entity && entity->kind == C_ENTITY_TYPEDEF && entity->type.value < result->type_count ? entity->type : C_TYPE_ID_INVALID;
        lookup = true;
    }
    else if (words)
    {
        CParsePrimitiveSpelling spelling = c_parse_primitive_type_read(*preprocess, open + 1, close);
        bool plain = spelling.seen_type && spelling.valid_specifiers && spelling.declarator_start == star && spelling.type.kind != C_TYPE_INVALID &&
                     spelling.type.kind != C_TYPE_VA_LIST;
        type = plain ? c_ast_types_interned(body, spelling.type) : C_TYPE_ID_INVALID;
    }
    for (u32 level = star; type.value < result->type_count && level < close; level += 1)
    {
        type = c_ast_types_interned(body, (CType){
                                              .element_type = type,
                                              .return_type = C_TYPE_ID_INVALID,
                                              .array_bound = C_ARRAY_BOUND_INVALID,
                                              .kind = C_TYPE_POINTER,
                                          });
    }
    *lookup_out = lookup;
    return type.value < result->type_count ? type : C_TYPE_ID_INVALID;
}

// `(T)operand`. Without constraint checks the machine does not type the
// operand, so the cast's row is the answer; it still scans the operand's
// tokens, so an operand the tree did not type must hold no type name.
// Checked, the operand is typed and the scalar conversion rule applies; an
// operand that is one string-literal token, perhaps parenthesized, is the
// replayed exception (C_AST_TYPE_FLAG_REPLAY).
BUSTER_GLOBAL_LOCAL void c_ast_types_cast(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    CAst const* ast = body->ast;
    bool lookup = false;
    CTypeId type = c_ast_types_type_name(body, preprocess, node, relative, &lookup);
    u32 operand_flags = body->flags[relative - 1];
    bool operand = (operand_flags & C_AST_TYPE_FLAG_ACCEPTED) != 0;
    if (type.value < result->type_count && (operand || !c_ast_types_holds_type_name(ast, node - 1)))
    {
        u32 flags = (lookup ? C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW : 0) | (operand_flags & C_AST_TYPE_FLAG_LOOKUP_BELOW);
        CTypeKind to = result->types[type.value].kind;
        // The literal is the whole operand, its one token alone or inside
        // parentheses (`S8()` spells `(char8*)("text")`): the machine's
        // operand task strips enclosing parentheses before it probes the memo
        // and hands the token to the leaf, so either replays the same way.
        u32 close = c_ast_types_match(body, ast->tokens[node]);
        u32 literal_token = ast->tokens[node - 1];
        u32 wraps = close != C_AST_TYPE_NONE && literal_token > close ? literal_token - (close + 1) : C_AST_TYPE_NONE;
        bool literal = ast->kinds[node - 1] == C_AST_STRING && ast->data[node - 1] == 1 && wraps != C_AST_TYPE_NONE &&
                       body->end[relative] == literal_token + 1 + wraps;
        for (u32 level = 0; literal && level < wraps; level += 1)
        {
            literal = c_ast_types_punctuator_at(body, close + 1 + level, C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                      c_ast_types_punctuator_at(body, literal_token + 1 + level, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        }
        CTypeKind from = operand ? result->types[body->types[relative - 1].value].kind : literal ? C_TYPE_ARRAY : C_TYPE_INVALID;
        bool aggregates = c_ast_types_aggregate_kind(to) || c_ast_types_aggregate_kind(from);
        bool clean = from != C_TYPE_INVALID && !aggregates && !c_parse_scalar_conversion_message(body->target, to, from, false).length &&
                     !c_parse_scalar_conversion_message(body->target, to, from, true).length;
        flags |= clean && operand && (operand_flags & C_AST_TYPE_FLAG_SAFE) ? C_AST_TYPE_FLAG_SAFE : 0;
        flags |= clean && literal ? C_AST_TYPE_FLAG_REPLAY : 0;
        c_ast_types_accept(body, relative, type, flags);
    }
}

// `(T){ ... }`: the machine answers the type name's row and reads nothing of
// the initializer list.
BUSTER_GLOBAL_LOCAL void c_ast_types_compound_literal(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    bool lookup = false;
    CTypeId type = c_ast_types_type_name(body, preprocess, node, relative, &lookup);
    if (type.value < body->result->type_count)
    {
        c_ast_types_accept(body, relative, type, C_AST_TYPE_FLAG_SAFE | (lookup ? C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW : 0));
    }
}

// `&operand`: the machine's ADDRESS_OF operation appends a pointer to the
// operand's row, an interned row (CTypeInterning), so the node is accepted
// only when that row already exists. With constraint checks the operand must
// have a place's shape (c_parse_expression_place_shape over the same tokens);
// no accepted operand carries the nonplace fact the machine also tests.
BUSTER_GLOBAL_LOCAL void c_ast_types_address(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 relative)
{
    CParseResult* result = body->result;
    u32 operand_flags = body->flags[relative - 1];
    CTypeId type = (operand_flags & C_AST_TYPE_FLAG_ACCEPTED) ? c_ast_types_interned(body, (CType){
                                                                                               .element_type = body->types[relative - 1],
                                                                                               .return_type = C_TYPE_ID_INVALID,
                                                                                               .array_bound = C_ARRAY_BOUND_INVALID,
                                                                                               .kind = C_TYPE_POINTER,
                                                                                           })
                                                              : C_TYPE_ID_INVALID;
    if (type.value < result->type_count)
    {
        u32 flags = c_ast_types_inherit(operand_flags, operand_flags);
        bool place = c_parse_expression_place_shape(result, *preprocess, body->first[relative] + 1, body->end[relative]);
        c_ast_types_accept(body, relative, type, place ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE);
    }
}

// `sizeof` and `_Alignof`/`alignof`, with an expression or a type name: the
// machine's leaf answers size_t for exactly these spellings and types nothing
// inside. It scans an expression operand's tokens, so one the tree did not
// type must hold no type name, and a typed one keeps its lookup mark: the
// scan resolves a typedef cast prefix by spelling, and where it finds one
// decides which operators split. A type operand follows the keyword, where
// the scan reads no group.
BUSTER_GLOBAL_LOCAL void c_ast_types_size_query(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    u32 kind = body->ast->kinds[node];
    u32 token = body->ast->tokens[node];
    String8 name = token < body->token_total ? c_token_spelling(preprocess->spelling_base, preprocess->tokens[token]) : (String8){0};
    bool keyword = kind == C_AST_SIZEOF_EXPRESSION || kind == C_AST_SIZEOF_TYPE
                       ? string_equal(name, S8("sizeof"))
                       : string_equal(name, S8("_Alignof")) || string_equal(name, S8("alignof"));
    bool expression = kind == C_AST_SIZEOF_EXPRESSION || kind == C_AST_ALIGNOF_EXPRESSION;
    bool scanned = !expression || (body->flags[relative - 1] & C_AST_TYPE_FLAG_ACCEPTED) || !c_ast_types_holds_type_name(body->ast, node - 1);
    CTypeId type = keyword && scanned && body->tokens[token].kind == C_TOKEN_IDENTIFIER
                       ? c_ast_types_scalar(body, target_uses_llp64_data_model(body->target) ? C_TYPE_UNSIGNED_LONG_LONG : C_TYPE_UNSIGNED_LONG)
                       : C_TYPE_ID_INVALID;
    if (type.value < body->result->type_count)
    {
        u32 operand_flags = expression ? body->flags[relative - 1] : 0;
        c_ast_types_accept(body, relative, type, C_AST_TYPE_FLAG_SAFE | (operand_flags & C_AST_TYPE_FLAG_LOOKUP_BELOW));
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_types_type_node(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    u32 kind = body->ast->kinds[node];
    switch (kind)
    {
    case C_AST_IDENTIFIER:
    {
        c_ast_types_identifier(body, preprocess, node, relative);
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
    case C_AST_STRING:
    {
        body->flags[relative] = body->ast->data[node] == 1 ? C_AST_TYPE_FLAG_STRING : 0;
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
        c_ast_types_dereference(body, relative);
    }
    break;
    case C_AST_CALL:
    {
        c_ast_types_call(body, preprocess, node, relative);
    }
    break;
    case C_AST_COMPOUND_LITERAL:
    {
        c_ast_types_compound_literal(body, preprocess, node, relative);
    }
    break;
    case C_AST_ADDRESS:
    {
        c_ast_types_address(body, preprocess, relative);
    }
    break;
    case C_AST_PLUS:
    case C_AST_NEGATE:
    case C_AST_BIT_NOT:
    case C_AST_LOGICAL_NOT:
    {
        c_ast_types_unary(body, node, relative);
    }
    break;
    case C_AST_SIZEOF_EXPRESSION:
    case C_AST_SIZEOF_TYPE:
    case C_AST_ALIGNOF_EXPRESSION:
    case C_AST_ALIGNOF_TYPE:
    {
        c_ast_types_size_query(body, preprocess, node, relative);
    }
    break;
    case C_AST_CAST:
    {
        c_ast_types_cast(body, preprocess, node, relative);
    }
    break;
    case C_AST_CONDITIONAL:
    case C_AST_CONDITIONAL_OMITTED:
    {
        c_ast_types_conditional(body, node, relative);
    }
    break;
    case C_AST_COMMA:
    {
        c_ast_types_comma(body, node, relative);
    }
    break;
    default:
    {
        if (kind >= C_AST_MULTIPLY && kind <= C_AST_LOGICAL_OR)
        {
            c_ast_types_binary(body, preprocess, node, relative);
        }
        else if (kind >= C_AST_ASSIGN && kind <= C_AST_BIT_OR_ASSIGN)
        {
            c_ast_types_assignment(body, node, relative);
        }
    }
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
        body->widths[relative] = 0;
        if (body->entities)
        {
            body->entities[relative] = C_AST_TYPE_NONE;
        }
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
    body->statistics->nodes_typed += visited;
    body->statistics->nodes_accepted += accepted;
    WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_NODES, visited);
}

// Whether the typer may type now: a tree index, an idle machine and the
// built delimiter index the span rules read.
BUSTER_GLOBAL_LOCAL bool c_ast_types_ready(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess)
{
    bool ready = machine->ast_bodies && machine->frame_count == 0 && !machine->failed && result->position_index;
    if (ready)
    {
        c_parse_position_index_ensure(result, *preprocess);
        ready = result->position_index->built && result->position_index->matching_delimiters_plus_one;
    }
    return ready;
}

// Reserves the arrays for the node interval of `node` over the tokens
// [token_start, token_end) in the machine's scratch arena and makes it the
// machine's region; c_ast_types_region_fill types it. An initializer
// (`initializer` set) resolves unbound names in `scope`. The arrays come from
// the guarded per-body scratch (#1256): in a body whose arrays do not fit,
// nothing is reserved, null is returned and the body is left to the machine,
// and the caller reports the exhaustion. Outside a body the guard allocates
// as arena_allocate does.
BUSTER_GLOBAL_LOCAL CAstTypeBody* c_ast_types_region_create(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess,
                                                            u32 node, u32 token_start, u32 token_end, CScopeId scope, bool initializer)
{
    CAst const* ast = machine->syntax_tree;
    u32 count = ast->extents[node];
    u32 token_count = token_end - token_start;
    Arena* scratch = machine->scratch_arena;
    CAstTypeBody* body = C_PARSE_BODY_SCRATCH_ARRAY(scratch, CAstTypeBody, 1);
    CTypeId* types = C_PARSE_BODY_SCRATCH_ARRAY(scratch, CTypeId, count);
    u32* first = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
    u32* end = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
    u32* entities = initializer ? C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count) : 0;
    u32* link = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
    u32* start_head = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, token_count);
    u8* flags = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u8, count);
    u8* widths = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u8, count);
    bool reserved = body && types && first && end && (entities || !initializer) && link && start_head && flags && widths;
    if (reserved)
    {
        *body = (CAstTypeBody){
            .ast = ast,
            .result = result,
            .tokens = preprocess->tokens,
            .matches = result->position_index->matching_delimiters_plus_one,
            .types = types,
            .first = first,
            .end = end,
            .entities = entities,
            .link = link,
            .start_head = start_head,
            .flags = flags,
            .widths = widths,
            .target = preprocess->target,
            .scope = scope,
            .begin = c_ast_subtree_begin(ast, node),
            .node = node,
            .token_start = token_start,
            .token_end = token_end,
            .token_total = (u32)preprocess->token_count,
            .scalars_published = machine->ast_bodies->scalars_published,
            .gnu = c_preprocess_dialect_is_gnu(preprocess->dialect),
            .waiting = initializer,
        };
        body->statistics = machine->ast_type_statistics ? machine->ast_type_statistics : &body->local_statistics;
        machine->ast_types = body;
    }
    return reserved ? body : 0;
}

// The eager pass over a created region, into the arrays it reserved, so it
// allocates nothing and may run while a validator holds scratch above them.
BUSTER_GLOBAL_LOCAL void c_ast_types_region_fill(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess)
{
    memset(body->start_head, 0, sizeof(*body->start_head) * (body->token_end - body->token_start));
    c_ast_types_type_body(body, machine, preprocess);
    body->waiting = false;
}

BUSTER_C_SHARED void c_ast_types_body_begin(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess,
                                            CDeclaration const* declaration)
{
    machine->ast_types = 0;
    u32 token_start = declaration->body_start;
    u64 token_end = (u64)declaration->body_start + declaration->body_token_count;
    bool eligible = token_start >= 1 && declaration->body_token_count && token_end < preprocess->token_count &&
                    c_ast_types_ready(machine, result, preprocess);
    u32 definition = eligible ? c_ast_types_find_definition(machine->ast_bodies, token_start - 1) : C_AST_TYPE_NONE;
    CAst const* ast = machine->syntax_tree;
    if (definition != C_AST_TYPE_NONE && ast->kinds[definition - 1] == C_AST_COMPOUND_STATEMENT &&
        result->position_index->matching_delimiters_plus_one[token_start - 1] - 1 == token_end)
    {
        CAstTypeBody* body = c_ast_types_region_create(machine, result, preprocess, definition - 1, token_start, (u32)token_end, (CScopeId){0}, false);
        if (body)
        {
            c_ast_types_region_fill(body, machine, preprocess);
            body->statistics->bodies += 1;
        }
    }
}

// An initializer's region is created here but typed only when a query the
// literal fast path does not answer reaches it (c_ast_types_answer): 61 of
// the unity self-host's 2,777 initializers, its numeric tables, hold 75% of
// their expression nodes and are only ever asked about lone literals.
BUSTER_C_SHARED void c_ast_types_initializer_begin(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess, CScopeId scope,
                                                   u32 start, u32 end)
{
    machine->ast_types = 0;
    bool eligible = start < end && end <= preprocess->token_count && c_ast_types_ready(machine, result, preprocess);
    u32 initializer = eligible ? c_ast_types_find_initializer(machine->syntax_tree, machine->ast_bodies, start) : C_AST_TYPE_NONE;
    if (initializer != C_AST_TYPE_NONE)
    {
        c_ast_types_region_create(machine, result, preprocess, initializer, start, end, scope, true);
    }
}

BUSTER_C_SHARED bool c_ast_types_waiting(CTypeParseMachine const* machine)
{
    return machine->ast_types && machine->ast_types->waiting;
}

BUSTER_C_SHARED void c_ast_types_body_end(CTypeParseMachine* machine)
{
    machine->ast_types = 0;
}

BUSTER_C_SHARED void c_ast_types_initializer_end(CTypeParseMachine* machine)
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

// Whether every name the answer rests on resolves, in the query's scope, as
// the machine will resolve it: a callee by spelling to the entity the binder
// bound it to (or to nothing, in which case the machine falls back to the
// binding), and a cast's or compound literal's typedef name to the bound
// typedef. In an initializer, an unbound identifier and a cast's or compound
// literal's typedef name must resolve to the entity the eager pass found. The
// walk runs backward over the node's subtree and steps over every child
// subtree that carries no lookup mark, so it visits the paths to the marked
// nodes and their siblings' roots.
BUSTER_GLOBAL_LOCAL bool c_ast_types_lookups_agree(CAstTypeBody const* body, CPreprocessResult const* preprocess, CParseResult* result, CScopeId scope,
                                                   u32 node)
{
    CAst const* ast = body->ast;
    u32 floor = c_ast_subtree_begin(ast, node);
    u32 cursor = node;
    bool agree = true;
    bool more = true;
    while (agree && more)
    {
        u32 flags = body->flags[cursor - body->begin];
        if (flags & C_AST_TYPE_FLAG_LOOKUP)
        {
            u32 kind = ast->kinds[cursor];
            bool call = kind == C_AST_CALL;
            u32 token = call ? ast->tokens[c_ast_types_first_child(ast, cursor)] : kind == C_AST_IDENTIFIER ? ast->tokens[cursor] : ast->tokens[cursor] + 1;
            CEntityId looked = c_parse_lookup_entity_token(result, preprocess->spelling_base, scope, &preprocess->tokens[token]);
            CEntity const* bound = c_ast_types_bound_entity(result, token);
            u32 expected = body->entities && !call ? body->entities[cursor - body->begin]
                           : bound                ? (u32)(bound - result->entities)
                                                  : C_AST_TYPE_NONE;
            agree = (call && looked.value >= result->entity_count) || (expected < result->entity_count && looked.value == expected);
        }
        // Into the children when one carries a mark, else past the subtree.
        u32 next = (flags & C_AST_TYPE_FLAG_LOOKUP_BELOW) ? cursor : c_ast_subtree_begin(ast, cursor);
        more = next > floor;
        cursor = more ? next - 1 : cursor;
    }
    return agree;
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
        if (body->waiting)
        {
            c_ast_types_region_fill(body, machine, preprocess);
            body->statistics->initializers += 1;
        }
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
            bool checked = machine->validate_expression_constraints;
            bool replay = checked && !(flags & C_AST_TYPE_FLAG_SAFE) && (flags & C_AST_TYPE_FLAG_REPLAY);
            bool vouched = (flags & C_AST_TYPE_FLAG_ACCEPTED) && (!checked || (flags & C_AST_TYPE_FLAG_SAFE) || replay) &&
                           !c_parse_pending_enum_possible(result) && c_parse_type_identity_sites_absent(result, start, end);
            if (vouched && (flags & C_AST_TYPE_FLAG_LOOKUP_BELOW))
            {
                vouched = c_ast_types_lookups_agree(body, preprocess, result, scope, node);
            }
            answer.node_kind = body->ast->kinds[node];
            if (vouched)
            {
                body->statistics->answers += 1;
                answer.status = C_AST_TYPE_ANSWER;
                answer.type = body->types[relative];
                answer.nonplace_projection = (flags & C_AST_TYPE_FLAG_NONPLACE) != 0;
                // The literal operand's token (C_AST_TYPE_FLAG_REPLAY).
                answer.replay_start = replay ? body->ast->tokens[node - 1] : 0;
                answer.replay_end = replay ? body->ast->tokens[node - 1] + 1 : 0;
                WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_ANSWERS, 1);
            }
            else if ((flags & C_AST_TYPE_FLAG_STRING) && !c_parse_pending_enum_possible(result) &&
                     c_parse_type_identity_sites_absent(result, start, end))
            {
                // Off the answer path above, which nearly every query takes:
                // the literal's own token, whose replay types the answer.
                body->statistics->answers += 1;
                answer.status = C_AST_TYPE_STRING;
                answer.replay_start = body->ast->tokens[node];
                answer.replay_end = body->ast->tokens[node] + 1;
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

// Field-by-field equality of two rows, for a replayed row held against the
// row the machine appended at the same id.
BUSTER_GLOBAL_LOCAL bool c_ast_types_rows_equal(CType const* x, CType const* y)
{
    return string_equal(x->tag, y->tag) && x->tag_scope.value == y->tag_scope.value && x->element_type.value == y->element_type.value &&
           x->return_type.value == y->return_type.value && x->unqualified_type.value == y->unqualified_type.value && x->array_bound == y->array_bound &&
           x->parameter_start == y->parameter_start && x->parameter_count == y->parameter_count && x->member_start == y->member_start &&
           x->member_count == y->member_count && x->enum_member_start == y->enum_member_start && x->enum_member_count == y->enum_member_count &&
           x->definition_start == y->definition_start && x->definition_token_count == y->definition_token_count &&
           x->vector_byte_size == y->vector_byte_size && x->kind == y->kind && x->is_const == y->is_const && x->is_volatile == y->is_volatile &&
           x->is_restrict == y->is_restrict && x->is_atomic == y->is_atomic && x->is_variadic == y->is_variadic && x->is_complete == y->is_complete &&
           x->is_transparent_union == y->is_transparent_union && x->has_unqualified_type == y->has_unqualified_type &&
           x->is_unprototyped == y->is_unprototyped && x->has_fixed_underlying_type == y->has_fixed_underlying_type;
}

BUSTER_GLOBAL_LOCAL bool c_ast_types_bounds_equal(CArrayBound const* x, CArrayBound const* y)
{
    return x->inferred_count == y->inferred_count && x->token_start == y->token_start && x->token_count == y->token_count &&
           x->is_static == y->is_static && x->is_star == y->is_star && x->has_inferred_count == y->has_inferred_count && x->is_const == y->is_const &&
           x->is_parameter_declarator == y->is_parameter_declarator;
}

// Takes a replayed answer's rows back off the tables after keeping copies,
// so the machine run that follows appends onto the same table sizes.
BUSTER_C_SHARED void c_ast_types_verify_hold_replay(CParseResult* result, CAstTypePending* pending)
{
    CAstTypeVerifyMark mark = pending->mark;
    pending->replay_types = result->type_count - mark.types;
    pending->replay_bounds = result->array_bound_count - mark.array_bounds;
    for (u32 row = 0; row < pending->replay_types && row < C_AST_TYPE_REPLAY_ROWS; row += 1)
    {
        pending->replay_type_rows[row] = result->types[mark.types + row];
    }
    for (u32 row = 0; row < pending->replay_bounds && row < C_AST_TYPE_REPLAY_ROWS; row += 1)
    {
        pending->replay_bound_rows[row] = result->array_bounds[mark.array_bounds + row];
    }
    result->type_count = mark.types;
    result->array_bound_count = mark.array_bounds;
}

BUSTER_C_SHARED void c_ast_types_verify_end(CTypeParseMachine* machine, CParseResult* result, CAstTypePending const* pending, u32 start, u32 end,
                                            bool machine_valid, CTypeId machine_type, CTypeId* type_out)
{
    CAstTypeVerifyMark mark = pending->mark;
    CAstTypeAnswer answer = pending->answer;
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
    // The machine must append exactly the rows a replay appended, and
    // nothing else.
    bool tables = result->type_count == mark.types + pending->replay_types && result->array_bound_count == mark.array_bounds + pending->replay_bounds &&
                  result->member_count == mark.members && result->enum_member_count == mark.enum_members && result->entity_count == mark.entities &&
                  result->scope_count == mark.scopes && result->parameter_count == mark.parameters;
    for (u32 row = 0; tables && row < pending->replay_types; row += 1)
    {
        tables = row < C_AST_TYPE_REPLAY_ROWS && c_ast_types_rows_equal(result->types + mark.types + row, pending->replay_type_rows + row);
    }
    for (u32 row = 0; tables && row < pending->replay_bounds; row += 1)
    {
        tables = row < C_AST_TYPE_REPLAY_ROWS && c_ast_types_bounds_equal(result->array_bounds + mark.array_bounds + row, pending->replay_bound_rows + row);
    }
    if (!tables)
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

BUSTER_C_SHARED void c_ast_types_verify_probe(CParseResult const* result, CAstTypeVerifyMark mark, bool machine_valid)
{
    CTestAstTypeVerify* state = &c_ast_types_verify_state;
    state->probes += 1;
    state->probe_mismatches += machine_valid || result->diagnostic_count != mark.diagnostics || result->type_count != mark.types ||
                               result->array_bound_count != mark.array_bounds || result->member_count != mark.members ||
                               result->enum_member_count != mark.enum_members || result->entity_count != mark.entities ||
                               result->scope_count != mark.scopes || result->parameter_count != mark.parameters;
}

// Answers one range of an analyzed function body, or of a file-scope object's
// initializer, for a unit test, on a private machine and a private typed
// region, as the validation loop and the static-initializer walk would: the
// model is the finished analysis, so the immutable scalar rows are published
// into it first. The named function must have a body, or the named object an
// initializer.
CTestAstTypeProbe c_test_ast_type_probe(Arena* scratch, CPreprocessResult preprocess, CParseResult* result, CAst const* ast, String8 function, u32 start,
                                        u32 end, bool checked)
{
    CTestAstTypeProbe probe = {.type = C_TYPE_ID_INVALID, .kind = C_TYPE_INVALID, .status = C_TEST_AST_TYPE_PROBE_NO_BODY};
    CDeclaration const* declaration = 0;
    u32 initializer_start = 0;
    u32 initializer_end = 0;
    for (u32 index = 0; index < result->declaration_count && !declaration; index += 1)
    {
        CDeclaration const* candidate = result->declarations + index;
        bool body = candidate->kind == C_DECLARATION_FUNCTION && candidate->is_definition && candidate->body_token_count;
        bool initialized = candidate->kind == C_DECLARATION_OBJECT &&
                           c_ir_declaration_initializer_range(preprocess, *candidate, &initializer_start, &initializer_end);
        declaration = (body || initialized) && string_equal(candidate->name, function) ? candidate : 0;
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
        bool body = declaration->kind == C_DECLARATION_FUNCTION;
        if (body)
        {
            c_ast_types_body_begin(&machine, result, &preprocess, declaration);
        }
        else
        {
            c_ast_types_initializer_begin(&machine, result, &preprocess, (CScopeId){.value = 0}, initializer_start, initializer_end);
        }
        probe.status = C_TEST_AST_TYPE_PROBE_UNTYPED;
        if (machine.ast_types)
        {
            CScopeId scope = body ? c_parse_scope_for_token(result, declaration->scope, start) : (CScopeId){.value = 0};
            CAstTypeAnswer answer = c_ast_types_answer(&machine, &preprocess, result, scope, start, end);
            probe.nodes_typed = machine.ast_types->local_statistics.nodes_typed;
            probe.nodes_accepted = machine.ast_types->local_statistics.nodes_accepted;
            probe.status = answer.status == C_AST_TYPE_STRING ? (u32)C_AST_TYPE_ANSWER : (u32)answer.status;
            probe.node_kind = answer.node_kind;
            probe.type = answer.type;
            probe.kind = answer.type.value < result->type_count ? result->types[answer.type.value].kind : C_TYPE_INVALID;
            probe.nonplace_projection = answer.nonplace_projection;
            probe.replay = answer.replay_end > answer.replay_start;
            probe.replay_answer = answer.status == C_AST_TYPE_STRING;
        }
        c_ast_types_body_end(&machine);
        result->expression_scalar_types = previous_scalars;
        scratch_end(temporary);
    }
    return probe;
}
#endif
