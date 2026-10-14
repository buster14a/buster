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
// CParseResult.expression_scalar_types, and the rows the machine interns,
// CTypeInterning in c_internal.h: primitive and pointer rows and the qualified
// member copies it shares), so an accepted node's machine run appends nothing.
// Stage 3 adds the shapes whose rows are interned: `&`, a cast or compound
// literal whose type name is a typedef name or a run of primitive specifier
// words under plain `*`s, and `.`/`->` over a const or volatile aggregate,
// each accepted only once every row it reads is interned. It declines every
// shape whose machine answer appends a row: a qualified member whose copy is
// not shared yet or may not be, a qualified array element, a string literal,
// an array operand that decays, a pointer conditional, a qualified operand
// losing its qualifiers without a recorded unqualified row, and a cast or
// compound literal to any other type name (a qualified typedef, a tag, another
// declarator). The exceptions are replays of one string-literal token, whose
// array row the machine appends. A checked query of a cast whose operand is
// that token, perhaps parenthesized as `S8()` writes it, types the literal
// too; and a query of the token alone, perhaps parenthesized as `S8()`'s
// `BUSTER_ARRAY_LENGTH` writes it, is the literal. The answer then carries
// that token (CAstTypeAnswer.replay_*), and c_parse_expression_tree_query makes
// exactly the machine's task: the memo probe and, on a miss, the string leaf.
// For the token alone the replay's row is the answer (C_AST_TYPE_STRING).
// A query of a `.` or `->` alone over an accepted base and a const or
// volatile aggregate is the other replay: the machine's member search and
// its append of the member's qualified copy, whose row is the answer
// (c_ast_types_decline).
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
// released with it: no array here outlives a body, because a speculative
// rollback may truncate the type rows an entry refers to. What outlives it is
// copied out at the region's end into the unit's facts record
// (CParserResult.ast_facts, GitHub #4062; c_ast_types_facts_begin): an
// accepted node's row existed when the body was typed, before any of the
// body's checkpoints, so no rollback in the body can take it. A body is typed only
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
// The eager pass. c_ast_types_type_body walks the body's node interval in
// index order. Children precede parents in a postorder tree, so each node's
// operand types are known when the node is reached. For every expression node
// it computes the token span [first, end) (the investigation's span rules,
// validated over 1.75 M nodes: a node's span starts at its leftmost operand or
// operator and ends at its last token, widened over any balanced parentheses
// that wrap an operand, which the tree does not record), links the node into a
// per-start-token chain, and computes a type and flags. One loop does this for
// the single-token leaves, whose span is their token and whose rules read no
// other node, and lists every other expression node; a second loop takes that
// list in index order (c_ast_types_type_body has the ordering argument).
// A node is accepted only when every operand the machine types for it is, so
// by induction an accepted node's whole machine run appends nothing. It is
// checked-safe when its operands are and its own checked-mode rule raises no
// constraint. The pass keeps no state between nodes beyond these arrays and
// small memos of member lookups and usual arithmetic conversions
// (c_ast_types_member_lookup, c_ast_types_arithmetic), which are exact
// because nothing either reads changes during the pass.
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
// Name checks. Repeating those lookups per query cost about 50 M Ir on the
// unity self-host, so a region settles its marks once
// (c_ast_types_lookups_settled). A mark whose symbol no entity outside the
// file scope has (c_ast_types_locals_build) finds nothing in any scope before
// the file scope, so from any query scope whose chain reaches the file scope
// it resolves as it does from the file scope, where the settlement checks it
// once against its binding; the file scope's answer for each symbol is looked
// up once per generation (CAstTypeSymbol). The settlement holds while the
// lookup generation (c_parse_lookup_generation: entities added, rollbacks
// that take entities, scopes or uses away) and the identifier use count are
// unchanged. A query
// over a mark that a parameter or local also spells, or made after the
// settlement stopped holding, walks its subtree as before.
//
// Accepted kinds and why each is exact (the machine paths are in c_parse.c;
// the operator rules are the machine's own functions, shared through
// c_internal.h, applied to operand types already held):
//   IDENTIFIER   bound through identifier_use_by_token_plus_one to an object,
//                function, parameter, local or enumerator: the entity's type,
//                as c_parse_direct_expression_base answers the single token.
//                An array parameter reads its interned adjusted pointer row;
//                absent rows decline to the token query without appending.
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
//                machine types any other base in its leaf). Over a const or
//                volatile aggregate the machine's answer is a qualified copy
//                of the member type, which it shares under the pilot's
//                interning window (c_parse_intern_qualified_type): accepted
//                with that copy when it already exists, declined when it does
//                not or may not be shared, and declined for a restrict or
//                atomic aggregate. A query of a declined node alone replays
//                the machine's step, which shares or appends the copy
//                (c_ast_types_decline).
//   INDEX        an array or pointer base with an integer index, as the
//                machine's subscript operation; declined for a qualified array
//                (a qualified element row), a vector base and the reversed
//                form `i[a]`.
//   DEREFERENCE  a pointer or array operand: its element type.
//   CALL         `name(...)` whose callee name is a bound object, function,
//                parameter or local of function or pointer-to-function type
//                and not a builtin, vendor builtin or sizeof-like spelling: the
//                function's return type. Arguments are not typed by the
//                machine and are not inspected here. An unbound void
//                `__builtin_` callee with no arguments (`__builtin_debugtrap()`)
//                is the published void row, which the leaf reads by spelling
//                (c_ast_types_builtin_call).
//   binary       `* / % + - << >> < > <= >= == != & ^ | && ||`: the operation
//                switch of c_type_parse_sizeof_step (c_ast_types_binary), with
//                c_parse_expression_arithmetic_type, the bit-field widths it
//                reads (c_ast_types_operand_width; an operand that ends in
//                `. name` without being a member is 0 only where the machine's
//                width probe provably fails, c_ast_types_probe_fails) and the
//                checked-mode operand rule (c_ast_types_binary_safe).
//   unary        `+ - ~ !`: promotion with the operand's bit-field width;
//                complex and suitable vector operands keep their type.
//   PRE_/POST_   `++` and `--`: the operand's own row, as the machine's UPDATE
//   INCREMENT,   operation and its leaf's postfix update answer; checked-safe
//   _DECREMENT   only where c_parse_update_operand_modifiable holds
//                (c_ast_types_update).
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
// Failing nodes. A few nodes the machine types to nothing, without a row or a
// constraint, are never accepted but carry C_AST_TYPE_FLAG_FAILS: `__func__`,
// `__FUNCTION__` and `__PRETTY_FUNCTION__`, which name no entity, and an
// unbound `__builtin_expect(...)` call, which reaches no typed leaf. A checked
// cast tests its conversion, and a conditional its condition, only when the
// operand has a type, so either is checked-safe over such an operand. The
// query repeats the name's lookup and requires it to find nothing.
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
//   c_ast_types_span                             span rules (c_ast_span_rule)
//   c_ast_types_probe_fails,                      bit-field widths of operands
//   c_ast_types_operand_width
//   c_ast_types_type_body, c_ast_types_type_node the eager pass and its rules
//   c_ast_types_member_lookup,                   the pass's memos (CAstTypeMemos)
//   c_ast_types_arithmetic
//   c_ast_types_builtin_call                     void builtins, failing calls
//   c_ast_types_binary, c_ast_types_unary,       stage-2 operator rules
//   c_ast_types_update, c_ast_types_conditional
//   c_ast_types_interned, c_ast_types_type_name,  stage-3 rules over interned
//   c_ast_types_cast,                             rows
//   c_ast_types_compound_literal,
//   c_ast_types_address
//   c_ast_types_locate, c_ast_types_answer       query lookup and the decision
//   c_ast_types_decline                          declines, and the qualified
//                                                member replay, out of line
//   c_ast_types_mark_token,                      query-scope name checks
//   c_ast_types_lookups_agree
//   c_ast_types_locals_build,                    the settled name checks
//   c_ast_types_mark_resolves_at_file,
//   c_ast_types_flag_word,
//   c_ast_types_lookups_hold,
//   c_ast_types_lookups_settle,
//   c_ast_types_lookups_settled
//   c_ast_types_publish                          machine state after an answer
//   c_ast_facts_create, c_ast_facts_release,     the retained facts (#4062):
//   c_ast_facts_retain_in_arena                  direct-call unit publication
//   c_ast_types_facts_begin, _next, _record,     publication at a region's end,
//   c_ast_types_facts_answer,                    a top-level query's answer,
//   c_ast_types_facts_finish                     and the unit's counts
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
// Never with ACCEPTED: the machine types the node to nothing, appending no row
// and raising no constraint. `__func__` and its GNU spellings name no entity,
// and an unbound `__builtin_expect` call reaches no typed leaf, so each
// resolves to nothing once the query's lookup also finds nothing (the LOOKUP
// mark). A checked cast and a conditional's condition test their operand only
// when it has a type, so such an operand is safe for them.
#define C_AST_TYPE_FLAG_FAILS (1u << 6)
// A string literal of one token. It is not ACCEPTED, since its machine answer
// appends its array row, so every parent declines it; a query of the node
// alone is answered by replaying the machine's leaf call, whose row is the
// answer (C_AST_TYPE_STRING). Bit 6 is C_AST_TYPE_FLAG_FAILS's (#3377); the
// flags are a u8, so this is the last free bit.
#define C_AST_TYPE_FLAG_STRING (1u << 7)
BUSTER_CT_CHECK(C_AST_TYPE_FLAG_STRING <= UINT8_MAX);

// INIT_DECLARATOR's presence bit for an initializer, its last child (c_ast.h).
#define C_AST_TYPE_INIT_DECLARATOR_INITIALIZER (1u << 2)

// One remembered member lookup: `symbol` in the aggregate row `aggregate`
// answered `type` with bit-field width `width`. `aggregate` is 0 for an empty
// entry and the row + 1 otherwise.
typedef struct CAstTypeMemberMemo CAstTypeMemberMemo;
struct CAstTypeMemberMemo
{
    u32 aggregate;
    u32 symbol;
    CTypeId type;
    u32 width;
};

// Entries in a pass's member memo; c_ast_types_member_lookup takes the top six
// bits of its hash.
#define C_AST_TYPE_MEMBER_MEMO_COUNT 64
BUSTER_CT_CHECK(C_AST_TYPE_MEMBER_MEMO_COUNT == 64);
// One remembered usual arithmetic conversion: c_parse_expression_arithmetic_type
// of the rows `left` and `right` with bit-field widths `widths` (left in the
// low byte) answered `type`. `left` is 0 for an empty entry and the row + 1
// otherwise.
typedef struct CAstTypeArithmeticMemo CAstTypeArithmeticMemo;
struct CAstTypeArithmeticMemo
{
    u32 left;
    u32 right;
    u32 widths;
    CTypeId type;
};

// Entries in a pass's arithmetic memo; c_ast_types_arithmetic takes the top
// six bits of its hash.
#define C_AST_TYPE_ARITHMETIC_MEMO_COUNT 64
BUSTER_CT_CHECK(C_AST_TYPE_ARITHMETIC_MEMO_COUNT == 64);
// c_ast_types_type_body fills the entity array with all-ones bytes.
BUSTER_CT_CHECK(C_AST_TYPE_NONE == UINT32_MAX && C_AST_SPAN_NONE == C_AST_TYPE_NONE);

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
    // Indexed by symbol, as of lookup generation `local_generation`: what the
    // file scope answers for it (CAstTypeSymbol), or that some entity outside
    // the file scope has it (c_ast_types_locals_build). Null without a
    // binding array.
    u32* symbols;
    u32 symbol_capacity;
    // Indexed by scope, below `rooted_count`: nonzero when the scope's parent
    // chain reaches the file scope, as of the same generation.
    u8* rooted;
    u32 rooted_capacity;
    u32 rooted_count;
    u64 local_generation;
    // Nodes settled since the table was last built; a rebuild waits until
    // they reach the entity count, so rebuilding costs no more than settling.
    u64 settled_nodes;
    bool locals_built;
    // Every scalar row is published (c_ast_types_scalars_published), so the
    // literal leaf cannot append one.
    bool scalars_published;
};

// CAstTypeBodyIndex.symbols: a symbol's file-scope answer, looked up the first
// time a settlement needs it (c_ast_types_mark_resolves_at_file); the entity
// is stored plus C_AST_TYPE_SYMBOL_ENTITY.
typedef enum CAstTypeSymbol
{
    C_AST_TYPE_SYMBOL_UNSEEN,
    // Some entity outside the file scope has the symbol.
    C_AST_TYPE_SYMBOL_LOCAL,
    // The file scope answers nothing.
    C_AST_TYPE_SYMBOL_NONE,
    C_AST_TYPE_SYMBOL_ENTITY,
} CAstTypeSymbol;

// A region's settled lookups (c_ast_types_lookups_settled).
typedef enum CAstTypeLookups
{
    C_AST_TYPE_LOOKUPS_UNSETTLED,
    // Every mark but the ones in CAstTypeBody.others resolves from any scope
    // rooted at the file scope as it does from the file scope, and agrees
    // there.
    C_AST_TYPE_LOOKUPS_FILE,
    // Checked per query (c_ast_types_lookups_agree).
    C_AST_TYPE_LOOKUPS_EACH,
} CAstTypeLookups;

enum
{
    // The marks a settled region may hold that do not resolve as from the
    // file scope; a query over one walks its subtree. A region with more is
    // checked per query.
    C_AST_TYPE_LOOKUP_OTHER_LIMIT = 16,
};

// The memos of one eager pass: c_parse_member_type's answers
// (c_ast_types_member_lookup) and c_parse_expression_arithmetic_type's
// (c_ast_types_arithmetic).
typedef struct CAstTypeMemos CAstTypeMemos;
struct CAstTypeMemos
{
    CAstTypeMemberMemo members[C_AST_TYPE_MEMBER_MEMO_COUNT];
    CAstTypeArithmeticMemo arithmetic[C_AST_TYPE_ARITHMETIC_MEMO_COUNT];
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
    // Scratch for one eager pass: the relative indices of the spanned nodes
    // of class C_AST_TYPE_CLASS_SPANNED, ascending.
    u32* interior;
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
    // CAstTypeLookups, settled at the first query that needs it, with the
    // lookup generation and identifier use count it holds for.
    u8 lookups;
    u8 other_count;
    u32 lookups_uses;
    u64 lookups_generation;
    // The marks, in increasing relative index, that do not resolve as from
    // the file scope.
    u32 others[C_AST_TYPE_LOOKUP_OTHER_LIMIT];
    CAstTypeStatistics local_statistics;
    // The eager pass's memos, cleared when it starts. They are reserved
    // beside the per-node arrays rather than held here, so creating a region
    // does not clear them too.
    CAstTypeMemos* memos;
};

// How the eager pass treats a node kind, as one load per node. The typed
// kinds are the expression kinds (IDENTIFIER through COMMA, without _Generic's
// associations) and INITIALIZER_LIST. The single-token leaves among them span
// exactly their token, so they skip c_ast_types_span, and the three leaves
// that have a rule go to it directly; every other typed kind takes the span
// rules and c_ast_types_type_node. The table has a row for every u8 kind, so
// the load needs no bounds check.
typedef enum CAstTypeClass
{
    C_AST_TYPE_CLASS_UNTYPED,
    C_AST_TYPE_CLASS_IDENTIFIER,
    C_AST_TYPE_CLASS_NUMBER,
    C_AST_TYPE_CLASS_CHARACTER,
    // BOOLEAN_CONSTANT and NULLPTR: spanned and chained, never accepted.
    C_AST_TYPE_CLASS_TOKEN,
    C_AST_TYPE_CLASS_SPANNED,
} CAstTypeClass;
#define C_AST_TYPES_EXPRESSION_KIND(kind)                                                                                                      \
    (((kind) >= C_AST_IDENTIFIER && (kind) <= C_AST_COMMA && (kind) != C_AST_GENERIC_ASSOCIATION && (kind) != C_AST_GENERIC_DEFAULT) ||        \
     (kind) == C_AST_INITIALIZER_LIST)
#define C_AST_TYPES_CLASS(kind)                                                                                                                \
    ((kind) == C_AST_IDENTIFIER                                    ? C_AST_TYPE_CLASS_IDENTIFIER                                              \
     : (kind) == C_AST_NUMBER                                      ? C_AST_TYPE_CLASS_NUMBER                                                  \
     : (kind) == C_AST_CHARACTER                                   ? C_AST_TYPE_CLASS_CHARACTER                                               \
     : (kind) == C_AST_BOOLEAN_CONSTANT || (kind) == C_AST_NULLPTR ? C_AST_TYPE_CLASS_TOKEN                                                   \
     : C_AST_TYPES_EXPRESSION_KIND(kind)                           ? C_AST_TYPE_CLASS_SPANNED                                                 \
                                                                   : C_AST_TYPE_CLASS_UNTYPED)
#define C_AST_TYPES_CLASS_ENTRY(name, contract, a, b) (u8)C_AST_TYPES_CLASS(C_AST_##name),
BUSTER_GLOBAL_LOCAL u8 const c_ast_types_kind_classes[UINT8_MAX + 1] = {C_AST_KIND_LIST(C_AST_TYPES_CLASS_ENTRY)};
#undef C_AST_TYPES_CLASS_ENTRY
#undef C_AST_TYPES_CLASS
#undef C_AST_TYPES_EXPRESSION_KIND
// The single-token leaves are expression kinds, so c_ast_types_span gives
// each of them [token, token + 1).
BUSTER_CT_CHECK(C_AST_IDENTIFIER < C_AST_COMMA && C_AST_NUMBER > C_AST_IDENTIFIER && C_AST_NUMBER < C_AST_COMMA &&
                C_AST_CHARACTER > C_AST_IDENTIFIER && C_AST_CHARACTER < C_AST_COMMA && C_AST_BOOLEAN_CONSTANT > C_AST_IDENTIFIER &&
                C_AST_BOOLEAN_CONSTANT < C_AST_COMMA && C_AST_NULLPTR > C_AST_IDENTIFIER && C_AST_NULLPTR < C_AST_COMMA);

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
        index->symbol_capacity = result->binding_by_symbol ? result->binding_capacity : 0;
        index->symbols = index->symbol_capacity ? arena_allocate(machine->scratch_arena, u32, index->symbol_capacity) : 0;
        index->rooted_capacity = index->symbols ? result->scope_capacity : 0;
        index->rooted = index->rooted_capacity ? arena_allocate(machine->scratch_arena, u8, index->rooted_capacity) : 0;
        index->rooted_count = 0;
        index->local_generation = 0;
        index->settled_nodes = 0;
        index->locals_built = false;
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

// The rules are c_ast_span_rule's (c_ast.c), applied to this region's arrays,
// which hold spans at relative indices.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE CAstSpanInput c_ast_types_span_input(CAstTypeBody const* body)
{
    return (CAstSpanInput){.ast = body->ast, .tokens = body->tokens, .matches_plus_one = body->matches, .first = body->first,
                           .end = body->end, .token_total = body->token_total, .base = body->begin};
}

// Computes [first, end) for one expression node. Returns false when the span
// cannot be established (an unmatched delimiter, an operand without a span, a
// span outside the unit), and then the node can be neither queried nor
// accepted.
BUSTER_GLOBAL_LOCAL bool c_ast_types_span(CAstTypeBody const* body, u32 node, u32 relative, u32* first_out, u32* end_out)
{
    BUSTER_CHECK(node - body->begin == relative);
    CAstSpanInput input = c_ast_types_span_input(body);
    return c_ast_span_rule(&input, node, first_out, end_out);
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

// Whether c_parse_direct_expression_type fails on [start, end) without
// appending a row, read from the tokens and the nodes already typed. Its
// descent first strips enclosing parentheses, leading `*` and `&` and the
// `++`/`--` it treats as type-preserving, exactly as below. Its base is then
// the tokens before the first `[`, `.` or `->` outside parentheses:
// - When that base is one identifier, the machine walks the postfix steps
//   after it and fails at the first token that is no step. The steps it
//   takes span an accepted INDEX, MEMBER or MEMBER_ARROW node here, so each
//   reads an unqualified array or aggregate, or a qualified aggregate whose
//   member copy is already shared, as the machine's walk does, and appends
//   no qualified copy (c_parse_direct_expression_postfix,
//   c_parse_intern_qualified_type).
// - Any other base that holds no `,` or `{` outside parentheses is not
//   descended into. One that starts with none of a string literal, `(` or a
//   call's callee is typed to nothing by c_parse_direct_expression_base
//   before any row is made, and no step runs on nothing. The first two are
//   tested before the scan, since casts lead most of the declined operands.
// Every other range is left to the machine, even when it would fail too.
BUSTER_GLOBAL_LOCAL bool c_ast_types_probe_fails(CAstTypeBody const* body, u32 start, u32 end)
{
    bool normalize = true;
    while (normalize && start < end)
    {
        normalize = false;
        while (start < end && c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_LEFT_PARENTHESIS) && c_ast_types_match(body, start) == end - 1)
        {
            start += 1;
            end -= 1;
            normalize = true;
        }
        if (start < end && (c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_STAR) || c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_AMPERSAND)))
        {
            start += 1;
            normalize = true;
        }
        if (start + 1 < end &&
            (c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_PLUS_PLUS) || c_ast_types_punctuator_at(body, start, C_PUNCTUATOR_MINUS_MINUS)))
        {
            start += 1;
            normalize = true;
        }
        if (start + 1 < end &&
            (c_ast_types_punctuator_at(body, end - 1, C_PUNCTUATOR_PLUS_PLUS) || c_ast_types_punctuator_at(body, end - 1, C_PUNCTUATOR_MINUS_MINUS)))
        {
            end -= 1;
            normalize = true;
        }
    }
    // The base: up to the first postfix step outside parentheses. A base
    // that starts with `(` or a string literal is left to the machine.
    CToken first = start < end ? body->tokens[start] : (CToken){0};
    bool clean = start < end && first.kind != C_TOKEN_STRING_LITERAL && !c_token_is_punctuator(&first, C_PUNCTUATOR_LEFT_PARENTHESIS);
    bool loose = false;
    u32 base_end = end;
    for (u32 index = start; clean && base_end == end && index < end; index += 1)
    {
        CToken token = body->tokens[index];
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            u32 close = c_ast_types_match(body, index);
            clean = close < end;
            index = clean ? close : index;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) || c_token_is_punctuator(&token, C_PUNCTUATOR_DOT) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_ARROW))
        {
            base_end = index;
        }
        else
        {
            loose |= c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE) || c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA);
        }
    }
    bool fails = false;
    if (clean && first.kind == C_TOKEN_IDENTIFIER && base_end == start + 1)
    {
        // The steps the machine's walk takes, to the first token that is none.
        u32 index = base_end;
        bool stepping = true;
        while (clean && stepping && index < end)
        {
            if (c_ast_types_punctuator_at(body, index, C_PUNCTUATOR_LEFT_BRACKET))
            {
                u32 close = c_ast_types_match(body, index);
                clean = close < end;
                index = clean ? close + 1 : index;
            }
            else
            {
                stepping = (c_ast_types_punctuator_at(body, index, C_PUNCTUATOR_DOT) || c_ast_types_punctuator_at(body, index, C_PUNCTUATOR_ARROW)) &&
                           index + 1 < end && body->tokens[index + 1].kind == C_TOKEN_IDENTIFIER;
                index += stepping ? 2 : 0;
            }
        }
        u32 hit = clean && index < end ? body->start_head[start - body->token_start] : 0;
        while (hit && body->end[hit - 1] != index)
        {
            hit = body->link[hit - 1];
        }
        u32 kind = hit ? body->ast->kinds[body->begin + hit - 1] : C_AST_IDENTIFIER;
        fails = (kind == C_AST_INDEX || kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW) && (body->flags[hit - 1] & C_AST_TYPE_FLAG_ACCEPTED);
    }
    else if (clean && base_end > start)
    {
        bool callee = first.kind == C_TOKEN_IDENTIFIER && c_ast_types_punctuator_at(body, start + 1, C_PUNCTUATOR_LEFT_PARENTHESIS) &&
                      c_ast_types_punctuator_at(body, base_end - 1, C_PUNCTUATOR_RIGHT_PARENTHESIS);
        fails = !loose && !callee;
    }
    return fails;
}

// The bit-field width c_parse_expression_bit_field_width reads from an
// operand's tokens, or C_AST_TYPE_NONE. It looks only at a range ending in
// `. name` or `-> name`: for a member node that is the member's own width,
// recorded when the member was typed. Any other node that ends that way
// (`a + s.f`, `c ? x : s.f`) has the machine re-type the tokens before the
// final `.` or `->`. That probe is 0 when it provably fails without a new row
// (c_ast_types_probe_fails), and every other such node (`x, s.f`, `(T)s.f`)
// is left to the machine.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 c_ast_types_operand_width(CAstTypeBody const* body, u32 node)
{
    u32 relative = node - body->begin;
    u32 kind = body->ast->kinds[node];
    u32 first = body->first[relative];
    u32 end = body->end[relative];
    bool member_tail = end >= first + 3 && body->tokens[end - 1].kind == C_TOKEN_IDENTIFIER &&
                       (c_ast_types_punctuator_at(body, end - 2, C_PUNCTUATOR_DOT) || c_ast_types_punctuator_at(body, end - 2, C_PUNCTUATOR_ARROW));
    bool member = kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW;
    return member ? body->widths[relative] : member_tail && !c_ast_types_probe_fails(body, first, end - 2) ? C_AST_TYPE_NONE : 0;
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
        CTypeId type = c_parse_expression_entity_type(result, entity, true);
        if (type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, type, flags);
        }
    }
    else if (identifier && !entity)
    {
        // The machine's leaf answers a lone name it cannot resolve with
        // nothing, and its typedef check needs an entity.
        String8 name = c_token_spelling(preprocess->spelling_base, preprocess->tokens[token]);
        bool function_name = name.length >= 8 && (string_equal(name, S8("__func__")) || string_equal(name, S8("__FUNCTION__")) ||
                                                  string_equal(name, S8("__PRETTY_FUNCTION__")));
        body->flags[relative] = (u8)(function_name ? C_AST_TYPE_FLAG_FAILS | C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW : 0);
    }
}

// A number or character literal, typed by the leaf the machine's literal path
// uses (c_parse_expression_leaf_without_cast), so the rule exists once. With
// every scalar row published the leaf only reads the row it returns. An
// integer literal whose kind the syntax pass cached for this data model is
// the leaf's first answer, the published row of that kind, read here from
// the same fact without the leaf's call.
BUSTER_GLOBAL_LOCAL void c_ast_types_literal(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative,
                                             CTokenKind token_kind)
{
    u32 token = body->ast->tokens[node];
    if (body->scalars_published && token < body->token_total && body->tokens[token].kind == token_kind)
    {
        CParseResult* result = body->result;
        CTypeKind kind = C_TYPE_INVALID;
        CNumberFact fact = token_kind == C_TOKEN_PREPROCESSING_NUMBER ? c_number_fact(result->number_facts, body->tokens, token) : (CNumberFact){0};
        bool cached = !(fact.flags & C_NUMBER_FACT_FLOATING) && c_number_fact_kind(result->number_facts, fact, body->target, &kind);
        CTypeId type = cached ? (kind != C_TYPE_INVALID ? c_parse_expression_scalar_type(result, kind) : C_TYPE_ID_INVALID)
                              : c_parse_expression_leaf_without_cast(machine->scratch_arena, *preprocess, result,
                                                                     (CScopeId){.value = C_ID_UNDERLYING_INVALID}, token, token + 1);
        if (type.value < result->type_count)
        {
            c_ast_types_accept(body, relative, type, C_AST_TYPE_FLAG_SAFE);
        }
    }
}

// c_parse_member_type for `aggregate` and the member name at `token`, which
// within one eager pass depends only on the two: the pass appends no row and
// runs no machine, so the aggregate's members cannot change under it. A
// repeated pair is answered from the pass's memo. Only a member found directly
// in the aggregate's own row is remembered: that search reads the member index
// in the result's arena, while a promoted search into anonymous members takes
// guarded body scratch (#1256), so a repeat of it is left to make the same
// request again. A token without an interned symbol is always looked up.
BUSTER_GLOBAL_LOCAL CTypeId c_ast_types_member_lookup(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, CTypeId aggregate,
                                                      u32 token, u32* width_out)
{
    CParseResult* result = body->result;
    CToken name = preprocess->tokens[token];
    u64 key = ((u64)aggregate.value << 32) | name.symbol;
    CAstTypeMemberMemo* entry = body->memos->members + ((key * UINT64_C(0x9E3779B97F4A7C15)) >> 58);
    bool remembered = name.symbol && entry->aggregate == aggregate.value + 1 && entry->symbol == name.symbol;
    CTypeId type = remembered ? entry->type : C_TYPE_ID_INVALID;
    u32 width = remembered ? entry->width : 0;
    if (!remembered)
    {
        CTypeId found_in = C_TYPE_ID_INVALID;
        u32 member = 0;
        type = c_parse_member_type(machine->scratch_arena, result, aggregate, name.symbol, c_token_spelling(preprocess->spelling_base, name), &width,
                                   &found_in, &member);
        // The row c_parse_member_type searches first: an incomplete row's
        // unqualified link, else the aggregate itself.
        CType const* value = result->types + aggregate.value;
        u32 searched = !value->is_complete && value->has_unqualified_type && value->unqualified_type.value < result->type_count
                           ? value->unqualified_type.value
                           : aggregate.value;
        if (name.symbol && type.value < result->type_count && found_in.value == searched)
        {
            *entry = (CAstTypeMemberMemo){
                .aggregate = aggregate.value + 1,
                .symbol = name.symbol,
                .type = type,
                .width = width,
            };
        }
    }
    *width_out = width;
    return type;
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
        else if ((value->kind == C_TYPE_POINTER || value->kind == C_TYPE_ARRAY) && value->element_type.value < result->type_count)
        {
            aggregate = value->element_type;
        }
        if (aggregate.value != C_ID_UNDERLYING_INVALID)
        {
            value = result->types + aggregate.value;
            bool record = (value->kind == C_TYPE_STRUCT || value->kind == C_TYPE_UNION) && !value->is_restrict && !value->is_atomic;
            aggregate = record ? aggregate : C_TYPE_ID_INVALID;
        }
    }
    if (aggregate.value != C_ID_UNDERLYING_INVALID)
    {
        u32 width = 0;
        CType const* value = result->types + aggregate.value;
        CType qualifiers = {.is_const = value->is_const, .is_volatile = value->is_volatile};
        CTypeId type = c_ast_types_member_lookup(body, machine, preprocess, aggregate, member_token, &width);
        if (type.value < result->type_count && (qualifiers.is_const || qualifiers.is_volatile))
        {
            // The machine's answer is the member's qualified copy: accepted
            // only once it is shared, so the machine's run appends nothing.
            type = c_parse_interned_qualified_type(result, type, qualifiers);
        }
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

// An unbound `__builtin_` callee directly before its `(`. The machine's leaf
// (c_parse_expression_leaf_without_cast, reached through
// c_type_parse_expression_leaf_step once the vendor generic builtins are
// ruled out) reads a builtin by spelling:
// - one that returns void, called with no arguments, is the published void
//   row, read before any entity, so it needs no lookup;
// - `__builtin_expect` has no rule there, so the leaf looks the callee up in
//   the query's scope and, finding nothing, ends in
//   c_parse_direct_expression_base, which answers an unresolved callee with
//   nothing. The node fails (C_AST_TYPE_FLAG_FAILS), and the query repeats
//   the lookup (the call's LOOKUP mark).
BUSTER_GLOBAL_LOCAL void c_ast_types_builtin_call(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative, u32 callee_token)
{
    String8 name = c_token_spelling(preprocess->spelling_base, preprocess->tokens[callee_token]);
    if (string_starts_with_sequence(name, S8("__builtin_")) && c_vendor_generic_builtin(name).operation == C_VENDOR_GENERIC_NONE)
    {
        CSymbolBuiltin builtin = c_symbol_builtin_from_spelling(name);
        bool empty = body->ast->tokens[node] == callee_token + 1 && c_ast_types_match(body, callee_token + 1) == callee_token + 2;
        CTypeId type = empty && c_semantic_builtin_returns_void(builtin) ? c_ast_types_scalar(body, C_TYPE_VOID) : C_TYPE_ID_INVALID;
        if (type.value < body->result->type_count)
        {
            c_ast_types_accept(body, relative, type, C_AST_TYPE_FLAG_SAFE);
        }
        else if (builtin == C_SYMBOL_BUILTIN_EXPECT)
        {
            body->flags[relative] = (u8)(C_AST_TYPE_FLAG_FAILS | C_AST_TYPE_FLAG_LOOKUP | C_AST_TYPE_FLAG_LOOKUP_BELOW);
        }
    }
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
        else if (!entity)
        {
            c_ast_types_builtin_call(body, preprocess, node, relative, callee_token);
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

// c_parse_expression_arithmetic_type for two operand rows and their bit-field
// widths (each at most UINT8_MAX). Within one eager pass it depends only on
// the four: it reads the rows' kinds and returns a published scalar row, the
// pass appends no row, and the target is fixed. A repeated question is
// answered from the pass's memo.
BUSTER_GLOBAL_LOCAL CTypeId c_ast_types_arithmetic(CAstTypeBody* body, CTypeId left, CTypeId right, u32 left_width, u32 right_width)
{
    u32 widths = left_width | (right_width << 8);
    u64 key = ((u64)left.value << 32) ^ ((u64)right.value << 16) ^ widths;
    CAstTypeArithmeticMemo* entry = body->memos->arithmetic + ((key * UINT64_C(0x9E3779B97F4A7C15)) >> 58);
    bool remembered = entry->left == left.value + 1 && entry->right == right.value && entry->widths == widths;
    CTypeId type = remembered ? entry->type : C_TYPE_ID_INVALID;
    if (!remembered)
    {
        type = c_parse_expression_arithmetic_type(body->result, body->target, left, right, left_width, right_width);
        *entry = (CAstTypeArithmeticMemo){
            .left = left.value + 1,
            .right = right.value,
            .widths = widths,
            .type = type,
        };
    }
    return type;
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
    // Two integer operands pass every operator's rule: neither is an
    // aggregate, void, nullptr, complex, vector or pointer.
    bool integers = c_parse_expression_integer_kind(left_kind) && c_parse_expression_integer_kind(right_kind);
    bool safe = integers;
    if (!integers)
    {
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
        bool invalid_or_complex = invalid || (complex && !complex_operator);
        safe = !invalid_or_complex;
    }
    return safe;
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
                       ? c_ast_types_arithmetic(body, left, right, left_width, right_width)
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

// `++` and `--`, prefix and postfix: the operand's own row, unpromoted. The
// machine's prefix UPDATE operation keeps the operand's answer, and its leaf
// answers a postfix update with the operand's type: checked, from a nested
// run over the operand (the leaf's postfix_update); unchecked, from the direct
// reader, which drops the operator. Neither appends a row. Checked, both test
// the operand with c_parse_update_operand_modifiable and refuse a struct,
// union or nullptr operand. For a lone bound identifier that test reduces to
// the binding and the row, which the pass reads directly; any other operand
// runs it over the operand's tokens.
BUSTER_GLOBAL_LOCAL void c_ast_types_update(CAstTypeBody* body, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    CParseResult* result = body->result;
    u32 operand_flags = body->flags[relative - 1];
    if (operand_flags & C_AST_TYPE_FLAG_ACCEPTED)
    {
        CTypeId type = body->types[relative - 1];
        u32 flags = c_ast_types_inherit(operand_flags, operand_flags);
        if (flags & C_AST_TYPE_FLAG_SAFE)
        {
            CType const* value = result->types + type.value;
            u32 operand_token = body->ast->tokens[node - 1];
            CEntity const* entity = body->ast->kinds[node - 1] == C_AST_IDENTIFIER && !(operand_flags & C_AST_TYPE_FLAG_LOOKUP)
                                        ? c_ast_types_bound_entity(result, operand_token)
                                        : 0;
            bool modifiable = false;
            if (entity && value->kind != C_TYPE_ARRAY)
            {
                modifiable = (entity->kind == C_ENTITY_OBJECT || entity->kind == C_ENTITY_LOCAL || entity->kind == C_ENTITY_PARAMETER) &&
                             !entity->is_constexpr && !value->is_const &&
                             (c_parse_expression_real_kind(value->kind) || value->kind == C_TYPE_POINTER ||
                              (body->gnu && c_type_kind_is_complex(value->kind)));
            }
            else
            {
                bool postfix = body->ast->kinds[node] == C_AST_POST_INCREMENT || body->ast->kinds[node] == C_AST_POST_DECREMENT;
                u32 token = body->ast->tokens[node];
                modifiable = c_parse_update_operand_modifiable(result, *preprocess, postfix ? body->first[relative] : token + 1,
                                                               postfix ? token : body->end[relative], type, false);
            }
            bool refused = value->kind == C_TYPE_STRUCT || value->kind == C_TYPE_UNION || value->kind == C_TYPE_NULLPTR;
            flags = modifiable && !refused ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE;
        }
        c_ast_types_accept(body, relative, type, flags);
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
                       ? c_ast_types_arithmetic(body, (CTypeId){.value = left}, (CTypeId){.value = right}, left_width, right_width)
                       : C_TYPE_ID_INVALID;
        }
    }
    if (type.value < result->type_count)
    {
        CTypeKind condition_kind = (condition_flags & C_AST_TYPE_FLAG_ACCEPTED) ? result->types[body->types[condition - body->begin].value].kind
                                                                              : C_TYPE_INVALID;
        // The scalar-condition rule tests only a condition that has a type.
        bool condition_safe = ((condition_flags & C_AST_TYPE_FLAG_ACCEPTED) && (condition_flags & C_AST_TYPE_FLAG_SAFE) &&
                               !c_ast_types_aggregate_kind(condition_kind) && condition_kind != C_TYPE_VOID) ||
                              (condition_flags & C_AST_TYPE_FLAG_FAILS) != 0;
        u32 flags = c_ast_types_inherit(then_flags, else_flags) | (condition_flags & C_AST_TYPE_FLAG_LOOKUP_BELOW);
        c_ast_types_accept(body, relative, type, condition_safe ? flags : flags & ~(u32)C_AST_TYPE_FLAG_SAFE);
    }
}

// The interned row equal to `type` (c_parse_interned_type), which a machine
// run inside the interning window reads instead of appending. The window
// spans every file-scope initializer's and body's queries
// (c_parse_validate_lowering_constraints), so both read it.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE CTypeId c_ast_types_interned(CAstTypeBody const* body, CType type)
{
    return c_parse_interned_type(body->result, type);
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
// the query looks up again (c_ast_types_lookups_agree).
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
        // An operand that types to nothing skips the conversion rule.
        bool fails = (operand_flags & C_AST_TYPE_FLAG_FAILS) != 0;
        flags |= (clean && operand && (operand_flags & C_AST_TYPE_FLAG_SAFE)) || fails ? C_AST_TYPE_FLAG_SAFE : 0;
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

// The rule of a node of class C_AST_TYPE_CLASS_SPANNED; the eager pass runs
// the single-token leaves' rules itself.
BUSTER_GLOBAL_LOCAL void c_ast_types_type_node(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess, u32 node, u32 relative)
{
    u32 kind = body->ast->kinds[node];
    switch (kind)
    {
    case C_AST_STRING:
    {
        body->flags[relative] = (u8)(body->ast->data[node] == 1 ? C_AST_TYPE_FLAG_STRING : 0);
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
    case C_AST_PRE_INCREMENT:
    case C_AST_PRE_DECREMENT:
    case C_AST_POST_INCREMENT:
    case C_AST_POST_DECREMENT:
    {
        c_ast_types_update(body, preprocess, node, relative);
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

// The eager pass, in two loops over the region in index order. The first
// stores every node's span: a single-token leaf's is its token, it joins its
// start chain and is typed there (its rule reads no other node), and every
// other expression node is listed. The second takes the list in index order,
// computes each node's span from its operands' and runs its rule, so children
// still come before parents. The chains end as one loop leaves them: a node
// that starts at a leaf's token contains the leaf, so it follows the leaf in
// index order and is linked after it either way. Keeping the operator rules
// out of the first loop keeps both loops' working values in registers; the
// rules write through the body, so the loops hold the arrays and bounds they
// touch for every node in locals.
BUSTER_GLOBAL_LOCAL void c_ast_types_type_body(CAstTypeBody* body, CTypeParseMachine* machine, CPreprocessResult const* preprocess)
{
    u8 const* kinds = body->ast->kinds;
    u32 const* tokens = body->ast->tokens;
    u32* first_column = body->first;
    u32* end_column = body->end;
    u32* link = body->link;
    u32* start_head = body->start_head;
    u32* interior = body->interior;
    u8 const* flags = body->flags;
    u32 begin = body->begin;
    u32 last = body->node;
    u32 token_start = body->token_start;
    u32 token_end = body->token_end;
    u32 visited = 0;
    u32 accepted = 0;
    u32 interior_count = 0;
    // Every node starts unaccepted, and a member node without a width. A
    // node's type is read only once it is accepted (c_ast_types_accept writes
    // both), and its link only through a chain it was linked into, so neither
    // is cleared. The span is stored for every node, before any rule reads
    // it: an operand of another kind has none.
    u64 count = (u64)last - begin + 1;
    memset(body->flags, 0, sizeof(*body->flags) * count);
    memset(body->widths, 0, sizeof(*body->widths) * count);
    if (body->entities)
    {
        memset(body->entities, 0xff, sizeof(*body->entities) * count);
    }
    memset(body->memos->members, 0, sizeof(body->memos->members));
    memset(body->memos->arithmetic, 0, sizeof(body->memos->arithmetic));
    for (u32 node = begin; node <= last; node += 1)
    {
        u32 relative = node - begin;
        CAstTypeClass class = (CAstTypeClass)c_ast_types_kind_classes[kinds[node]];
        // c_ast_types_span's single-token rule. token_end is at most the
        // unit's token count, so this also bounds the token.
        u32 token = tokens[node];
        bool spanned = class != C_AST_TYPE_CLASS_UNTYPED && class != C_AST_TYPE_CLASS_SPANNED && token >= token_start && token < token_end;
        visited += class != C_AST_TYPE_CLASS_UNTYPED;
        first_column[relative] = spanned ? token : C_AST_TYPE_NONE;
        end_column[relative] = spanned ? token + 1 : C_AST_TYPE_NONE;
        if (spanned)
        {
            link[relative] = start_head[token - token_start];
            start_head[token - token_start] = relative + 1;
            if (class == C_AST_TYPE_CLASS_IDENTIFIER)
            {
                c_ast_types_identifier(body, preprocess, node, relative);
            }
            else if (class == C_AST_TYPE_CLASS_NUMBER)
            {
                c_ast_types_literal(body, machine, preprocess, node, relative, C_TOKEN_PREPROCESSING_NUMBER);
            }
            else if (class == C_AST_TYPE_CLASS_CHARACTER)
            {
                c_ast_types_literal(body, machine, preprocess, node, relative, C_TOKEN_CHARACTER_LITERAL);
            }
        }
        else if (class == C_AST_TYPE_CLASS_SPANNED)
        {
            interior[interior_count] = relative;
            interior_count += 1;
        }
    }
    for (u32 index = 0; index < interior_count; index += 1)
    {
        u32 relative = interior[index];
        u32 node = begin + relative;
        u32 first = C_AST_TYPE_NONE;
        u32 end = C_AST_TYPE_NONE;
        bool spanned = c_ast_types_span(body, node, relative, &first, &end) && first >= token_start && end <= token_end;
        first_column[relative] = spanned ? first : C_AST_TYPE_NONE;
        end_column[relative] = spanned ? end : C_AST_TYPE_NONE;
        if (spanned)
        {
            link[relative] = start_head[first - token_start];
            start_head[first - token_start] = relative + 1;
            c_ast_types_type_node(body, machine, preprocess, node, relative);
        }
    }
    for (u64 relative = 0; relative < count; relative += 1)
    {
        accepted += (flags[relative] & C_AST_TYPE_FLAG_ACCEPTED) != 0;
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
    u32* interior = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u32, count);
    u8* flags = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u8, count);
    u8* widths = C_PARSE_BODY_SCRATCH_ARRAY(scratch, u8, count);
    CAstTypeMemos* memos = C_PARSE_BODY_SCRATCH_ARRAY(scratch, CAstTypeMemos, 1);
    bool reserved = body && types && first && end && (entities || !initializer) && link && start_head && interior && flags && widths && memos;
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
            .interior = interior,
            .flags = flags,
            .widths = widths,
            .memos = memos,
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

// The name a lookup mark stands for: an identifier's own token, a call's
// callee, which the call rules mark only when it is the identifier directly
// before the `(` that is the call's token (c_ast_types_call), and otherwise
// the typedef name after a cast's or compound literal's `(`.
BUSTER_GLOBAL_LOCAL u32 c_ast_types_mark_token(CAst const* ast, u32 node)
{
    u32 kind = ast->kinds[node];
    return kind == C_AST_CALL ? ast->tokens[node] - 1 : kind == C_AST_IDENTIFIER ? ast->tokens[node] : ast->tokens[node] + 1;
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
            bool call = ast->kinds[cursor] == C_AST_CALL;
            u32 token = c_ast_types_mark_token(ast, cursor);
            CEntityId looked = c_parse_lookup_entity_token(result, preprocess->spelling_base, scope, &preprocess->tokens[token]);
            CEntity const* bound = c_ast_types_bound_entity(result, token);
            u32 expected = body->entities && !call ? body->entities[cursor - body->begin]
                           : bound                ? (u32)(bound - result->entities)
                                                  : C_AST_TYPE_NONE;
            // A call, and a node that types to nothing, also agree when the
            // lookup finds nothing.
            agree = ((call || (flags & C_AST_TYPE_FLAG_FAILS)) && looked.value >= result->entity_count) ||
                    (expected < result->entity_count && looked.value == expected);
        }
        // Into the children when one carries a mark, else past the subtree.
        u32 next = (flags & C_AST_TYPE_FLAG_LOOKUP_BELOW) ? cursor : c_ast_subtree_begin(ast, cursor);
        more = next > floor;
        cursor = more ? next - 1 : cursor;
    }
    return agree;
}

// Marks the symbol of every entity outside the file scope, and every scope
// whose parent chain reaches the file scope, and forgets every symbol's
// file-scope answer. Entities and scopes change only with the lookup
// generation (c_parse_lookup_generation), so the tables hold, and a
// file-scope answer once looked up stays the answer, while that is
// `generation`. A scope is created after its parent, so one
// forward pass settles each chain; a parent at or after its child is left
// unrooted, and a query in that scope walks its subtree.
BUSTER_GLOBAL_LOCAL void c_ast_types_locals_build(CAstTypeBodyIndex* index, CParseResult const* result, u64 generation)
{
    memset(index->symbols, 0, sizeof(*index->symbols) * index->symbol_capacity);
    for (u32 entity = 0; entity < result->entity_count; entity += 1)
    {
        u32 symbol = result->entities[entity].symbol;
        if (result->entities[entity].scope.value && symbol < index->symbol_capacity)
        {
            index->symbols[symbol] = C_AST_TYPE_SYMBOL_LOCAL;
        }
    }
    index->rooted_count = result->scope_count < index->rooted_capacity ? result->scope_count : index->rooted_capacity;
    for (u32 scope = 0; scope < index->rooted_count; scope += 1)
    {
        u32 parent = result->scopes[scope].parent.value;
        index->rooted[scope] = (u8)(!scope || !parent || (parent < scope && index->rooted[parent]));
    }
    index->local_generation = generation;
    index->settled_nodes = 0;
    index->locals_built = true;
}

// Whether a marked name resolves, by spelling in any scope whose chain reaches
// the file scope, as c_ast_types_lookups_agree requires, without looking it up
// there. A name whose symbol no entity outside the file scope has finds
// nothing in any scope before the file scope, so it resolves as it does from
// the file scope. Its binding must also be a file-scope entity or none, and
// the file scope's answer must agree with it. That answer is looked up once
// per symbol and generation; a name that cannot spell a word, which the
// lookup answers with nothing whatever its symbol, is looked up each time.
// An entity the file scope answers is a file-scope entity, so only an answer
// of nothing needs the binding's scope.
BUSTER_GLOBAL_LOCAL bool c_ast_types_mark_resolves_at_file(CAstTypeBodyIndex* index, CAstTypeBody const* body, CPreprocessResult const* preprocess,
                                                           CParseResult* result, u32 relative)
{
    CAst const* ast = body->ast;
    u32 node = body->begin + relative;
    bool call = ast->kinds[node] == C_AST_CALL;
    CToken const* token = &preprocess->tokens[c_ast_types_mark_token(ast, node)];
    u32 symbol = token->symbol;
    bool spells = c_token_may_spell_word(*token);
    u32 answer = symbol && symbol < index->symbol_capacity ? index->symbols[symbol] : C_AST_TYPE_SYMBOL_LOCAL;
    if (answer != C_AST_TYPE_SYMBOL_LOCAL && (answer == C_AST_TYPE_SYMBOL_UNSEEN || !spells))
    {
        CEntityId looked = c_parse_lookup_entity_token(result, preprocess->spelling_base, (CScopeId){0}, token);
        answer = looked.value < result->entity_count ? looked.value + C_AST_TYPE_SYMBOL_ENTITY : C_AST_TYPE_SYMBOL_NONE;
        index->symbols[symbol] = spells ? answer : index->symbols[symbol];
    }
    bool file = answer != C_AST_TYPE_SYMBOL_LOCAL;
    if (file)
    {
        CEntity const* bound = call || !body->entities ? c_ast_types_bound_entity(result, (u32)(token - preprocess->tokens)) : 0;
        u32 expected = body->entities && !call ? body->entities[relative] : bound ? (u32)(bound - result->entities) : C_AST_TYPE_NONE;
        file = answer == C_AST_TYPE_SYMBOL_NONE
                   ? (call || (body->flags[relative] & C_AST_TYPE_FLAG_FAILS)) && (expected >= result->entity_count || !result->entities[expected].scope.value)
                   : expected == answer - C_AST_TYPE_SYMBOL_ENTITY;
    }
    return file;
}

// Eight flag bytes as one word, the first byte lowest on every supported
// (little-endian) host.
BUSTER_GLOBAL_LOCAL u64 c_ast_types_flag_word(u8 const* flags)
{
    u64 word;
    memcpy(&word, flags, sizeof(word));
    return word;
}

// Whether the region's settlement answers a query over `node`: it holds under
// the current lookup generation and identifier use count, no other mark lies
// in the node's subtree, and the query's scope chain reaches the file scope.
BUSTER_GLOBAL_LOCAL bool c_ast_types_lookups_hold(CAstTypeBodyIndex const* index, CAstTypeBody const* body, CParseResult const* result, CScopeId scope,
                                                  u32 node, u64 generation)
{
    bool settled = body->lookups == C_AST_TYPE_LOOKUPS_FILE && body->lookups_generation == generation &&
                   body->lookups_uses == result->identifier_use_count;
    if (settled && body->other_count)
    {
        u32 floor = c_ast_subtree_begin(body->ast, node) - body->begin;
        u32 last = node - body->begin;
        for (u32 other = 0; settled && other < body->other_count; other += 1)
        {
            settled = body->others[other] < floor || body->others[other] > last;
        }
    }
    return settled && scope.value < index->rooted_count && index->rooted[scope.value];
}

// Settles the region's marks (c_ast_types_mark_resolves_at_file) under the
// current lookup generation, then answers the query that needed it as
// c_ast_types_lookups_hold does. Out of line: about one query in twenty
// settles, and the scan's registers would otherwise be saved on every query.
BUSTER_GLOBAL_LOCAL C_AST_TYPES_NOINLINE bool c_ast_types_lookups_settle(CAstTypeBodyIndex* index, CAstTypeBody* body, CPreprocessResult const* preprocess,
                                                                         CParseResult* result, CScopeId scope, u32 node)
{
    u64 generation = 0;
    c_parse_lookup_generation(result, &generation);
    u32 count = body->node + 1 - body->begin;
    if (!index->locals_built || (index->local_generation != generation && index->settled_nodes >= result->entity_count))
    {
        c_ast_types_locals_build(index, result, generation);
    }
    index->settled_nodes += count;
    bool file = index->local_generation == generation;
    // Eight flag bytes at a time: almost none carry the mark, so whole groups
    // without one are skipped in a loop of their own, and each mark in a
    // group is found from the group's mask, the low byte first (every
    // supported host is little-endian). The tail's bytes are assembled into
    // one word before the scan, which takes it for the last group.
    u8 const* flags = body->flags;
    u64 const lookup_bytes = 0x0101010101010101ull * C_AST_TYPE_FLAG_LOOKUP;
    u32 whole = count & ~7u;
    u64 tail = 0;
    for (u32 byte = whole; byte < count; byte += 1)
    {
        tail |= (u64)flags[byte] << (8 * (byte - whole));
    }
    u32 group = 0;
    while (file && group < count)
    {
        while (group < whole && !(c_ast_types_flag_word(flags + group) & lookup_bytes))
        {
            group += 8;
        }
        u64 word = group < whole ? c_ast_types_flag_word(flags + group) : tail;
        for (u64 marks = word & lookup_bytes; file && marks; marks &= marks - 1)
        {
            u32 relative = group + mask64_first_set(marks) / 8;
            bool other = !c_ast_types_mark_resolves_at_file(index, body, preprocess, result, relative);
            file = !other || body->other_count < C_AST_TYPE_LOOKUP_OTHER_LIMIT;
            if (other && file)
            {
                body->others[body->other_count] = relative;
                body->other_count += 1;
            }
        }
        group += 8;
    }
    body->lookups = (u8)(file ? C_AST_TYPE_LOOKUPS_FILE : C_AST_TYPE_LOOKUPS_EACH);
    body->lookups_generation = generation;
    body->lookups_uses = result->identifier_use_count;
    return c_ast_types_lookups_hold(index, body, result, scope, node, generation);
}

// Settles the region's marks once, at its first query that needs it
// (c_ast_types_lookups_settle), and then answers whether every mark in the
// query node's subtree resolves as from the file scope, so that the query's
// scope, if its chain reaches the file scope, needs no lookup. The
// settlement holds while the lookup generation and the identifier use count
// are the ones it was made under (c_parse_lookup_generation). A query over
// one of the region's other marks, or made after the settlement stopped
// holding, walks its subtree (c_ast_types_lookups_agree), and so does every
// query of a region with more other marks than it records.
BUSTER_GLOBAL_LOCAL bool c_ast_types_lookups_settled(CAstTypeBodyIndex* index, CAstTypeBody* body, CPreprocessResult const* preprocess,
                                                     CParseResult* result, CScopeId scope, u32 node)
{
    u64 generation = 0;
    bool stamped = index && index->symbols && index->rooted && c_parse_lookup_generation(result, &generation);
    bool settled = stamped && (body->lookups == C_AST_TYPE_LOOKUPS_UNSETTLED ? c_ast_types_lookups_settle(index, body, preprocess, result, scope, node)
                                                                             : c_ast_types_lookups_hold(index, body, result, scope, node, generation));
    return settled;
}

// A `.` or `->` that the member rule declines only because its aggregate is
// const or volatile, over a base it accepts.
// The machine types that base as the tree does, then makes two calls,
// c_parse_member_type on the qualified aggregate and
// c_parse_intern_qualified_type on the member it finds
// (c_parse_direct_expression_postfix). The second shares or appends the
// member's qualified copy, and that row is its answer. The member rule
// accepts the node once the copy is shared before the body is typed, so the
// replay answers the copies first shared later in the body and the ones that
// may not be shared. c_parse_expression_tree_query
// makes the same two calls. A checked query needs a safe base, as the member
// rule does. The answer is C_AST_TYPE_STRING's replay with this node's kind.
// Every other node the answer path does not vouch for declines here. Out of
// line and returning its own answer, so the path that ordinary answers take
// keeps its code (#3381, #3384).
BUSTER_GLOBAL_LOCAL C_AST_TYPES_NOINLINE CAstTypeAnswer c_ast_types_decline(CAstTypeBody const* body, CPreprocessResult const* preprocess, CParseResult* result,
                                                                    CScopeId scope, u32 node, bool checked)
{
    CAst const* ast = body->ast;
    u32 kind = ast->kinds[node];
    bool arrow = kind == C_AST_MEMBER_ARROW;
    u32 relative = node - body->begin;
    u32 member_token = ast->tokens[node];
    u32 base_flags = relative ? body->flags[relative - 1] : 0;
    CTypeId aggregate = C_TYPE_ID_INVALID;
    if ((kind == C_AST_MEMBER || arrow) && (base_flags & C_AST_TYPE_FLAG_ACCEPTED) && (!checked || (base_flags & C_AST_TYPE_FLAG_SAFE)) &&
        c_ast_types_member_base_kind(ast->kinds[node - 1]) && member_token >= 1 && member_token < body->token_total &&
        body->tokens[member_token].kind == C_TOKEN_IDENTIFIER && body->end[relative] == member_token + 1 &&
        c_ast_types_punctuator_at(body, member_token - 1, arrow ? C_PUNCTUATOR_ARROW : C_PUNCTUATOR_DOT))
    {
        CTypeId base = body->types[relative - 1];
        CType const* value = result->types + base.value;
        aggregate = !arrow ? base : (value->kind == C_TYPE_POINTER || value->kind == C_TYPE_ARRAY) && value->element_type.value < result->type_count ? value->element_type : C_TYPE_ID_INVALID;
        value = aggregate.value < result->type_count ? result->types + aggregate.value : 0;
        bool qualified = value && (value->kind == C_TYPE_STRUCT || value->kind == C_TYPE_UNION) && (value->is_const || value->is_volatile) &&
                         !value->is_restrict && !value->is_atomic;
        aggregate = qualified ? aggregate : C_TYPE_ID_INVALID;
    }
    bool replayed = aggregate.value != C_ID_UNDERLYING_INVALID && !c_parse_pending_enum_possible(result) &&
                    c_parse_type_identity_sites_absent(result, body->first[relative], body->end[relative]) &&
                    (!(base_flags & C_AST_TYPE_FLAG_LOOKUP_BELOW) || c_ast_types_lookups_agree(body, preprocess, result, scope, node - 1));
    CAstTypeAnswer answer = {
        .type = replayed ? aggregate : C_TYPE_ID_INVALID,
        .status = replayed ? C_AST_TYPE_STRING : C_AST_TYPE_DECLINE,
        .node_kind = kind,
        .replay_start = replayed ? body->first[relative] : 0,
        .replay_end = replayed ? body->end[relative] : 0,
    };
    if (replayed)
    {
        body->statistics->answers += 1;
        WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_ANSWERS, 1);
    }
    else
    {
        body->statistics->declines += 1;
        WORK_LEDGER_RECORD(REDERIVE_TREE_TYPE_DECLINES, 1);
    }
    return answer;
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
                bool settled = c_ast_types_lookups_settled(machine->ast_bodies, body, preprocess, result, scope, node);
                body->statistics->lookups_settled += settled;
                body->statistics->lookups_walked += !settled;
                vouched = settled || c_ast_types_lookups_agree(body, preprocess, result, scope, node);
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
                answer = c_ast_types_decline(body, preprocess, result, scope, node, checked);
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

// ---- retained facts (GitHub #4062) -----------------------------------------------

// Bytes a fact arena reserves beyond its columns: the arena header and the
// rest of its first commit granule, as the tree's column arenas do.
#define C_AST_FACT_HEADROOM BUSTER_KB(64)
// One node's columns: types, payloads and flags.
#define C_AST_FACT_NODE_BYTES (sizeof(u32) + sizeof(u32) + sizeof(u8))
#define C_AST_CONSTANT_INITIAL_CAPACITY 64u

BUSTER_GLOBAL_LOCAL String8 const c_ast_fact_gap_names[C_AST_FACT_GAP_COUNT] = {
    [C_AST_FACT_GAP_UNSPANNED] = S8_INITIALIZER("unspanned"),
    [C_AST_FACT_GAP_INVALID] = S8_INITIALIZER("invalid"),
    [C_AST_FACT_GAP_APPENDS] = S8_INITIALIZER("appends"),
    [C_AST_FACT_GAP_BUSY] = S8_INITIALIZER("busy"),
};

String8 c_ast_fact_gap_name(CAstFactGap gap)
{
    return (u32)gap < C_AST_FACT_GAP_COUNT ? c_ast_fact_gap_names[gap] : S8("invalid");
}

// Returns a fact arena to the calling thread's pool (or the OS), keeping at
// most C_PHASE_ARENA_RETAINED_SIZE committed, as the tree's column arenas do;
// a small tree's reservation is smaller than that.
BUSTER_GLOBAL_LOCAL void c_ast_facts_retire(Arena* arena)
{
    arena_retire(arena, BUSTER_MIN((u64)C_PHASE_ARENA_RETAINED_SIZE, arena->reserved_size - arena_minimum_position));
}

bool c_ast_facts_create(CAstFacts* facts, CAst const* ast)
{
    *facts = (CAstFacts){0};
    u32 count = ast && ast->storage ? ast->node_count : 0;
    Arena* arena = count ? c_frontend_arena_create((ArenaCreation){
                                                       .reserved_size = (u64)count * C_AST_FACT_NODE_BYTES + C_AST_FACT_HEADROOM,
                                                       .initial_size = C_AST_FACT_HEADROOM,
                                                       .flags = {.pool_reuse = 1},
                                                   }, C_FRONTEND_RESERVATION_ANALYSIS)
                         : 0;
    u32* types = arena ? arena_allocate(arena, u32, count) : 0;
    u32* payloads = types ? arena_allocate(arena, u32, count) : 0;
    u8* flags = payloads ? arena_allocate(arena, u8, count) : 0;
    // Sparse rows double on demand. Their abandoned prefixes sum to less
    // than twice the previous power of two. The last capacity can be capped
    // at node_count, so three rows per node also cover that final carve.
    u64 constant_reservation = (u64)count * (sizeof(u32) + 3 * (sizeof(CAstConstantFact) + sizeof(CAstOffsetofPlan) +
                                                               sizeof(CAstOffsetofStep) + sizeof(CAstExt80InitializerFact))) + C_AST_FACT_HEADROOM;
    Arena* constants_arena = flags && constant_reservation <= ARENA_MAX_RESERVATION
        ? c_frontend_arena_create((ArenaCreation){.reserved_size = constant_reservation, .initial_size = C_AST_FACT_HEADROOM,
                                                 .flags = {.no_pool = true}}, C_FRONTEND_RESERVATION_ANALYSIS) : 0;
    u32* constant_slots = constants_arena ? arena_allocate_zeroed(constants_arena, u32, count) : 0;
    bool created = flags && constant_slots;
    if (created)
    {
        memset(types, 0xff, sizeof(*types) * count);
        memset(payloads, 0xff, sizeof(*payloads) * count);
        memset(flags, 0, sizeof(*flags) * count);
        *facts = (CAstFacts){
            .arena = arena,
            .constants_arena = constants_arena,
            .constant_slots = constant_slots,
            .types = types,
            .payloads = payloads,
            .flags = flags,
            .node_count = count,
            .statistics = {.nodes = count, .retained_bytes = arena->position - arena_minimum_position +
                                                           constants_arena->position - arena_minimum_position},
        };
    }
    else if (arena)
    {
        c_ast_facts_retire(arena);
        if (constants_arena)
        {
            arena_destroy(constants_arena, 1);
        }
    }
    return created;
}

// The producer index is prepared once before the first semantic query. It
// uses the shared span rules, and lowering can borrow it for the whole unit.
bool c_ast_facts_prepare(CAstFacts* facts, CAst const* ast, CPreprocessResult const* preprocess, CParseResult* result)
{
    bool ready = facts && facts->flags && ast && ast->node_count == facts->node_count && preprocess->token_count < UINT32_MAX;
    if (ready && !facts->spans)
    {
        c_parse_position_index_ensure(result, *preprocess);
        u32 const* matches = result->position_index && result->position_index->built && !result->position_index->delimiter_mismatch_count
                                 ? result->position_index->matching_delimiters_plus_one : 0;
        u64 size = (3 * (u64)ast->node_count + preprocess->token_count + 1) * sizeof(u32) + sizeof(CAstSpans) + C_AST_FACT_HEADROOM;
        Arena* arena = matches ? c_frontend_arena_create((ArenaCreation){.reserved_size = size, .initial_size = C_AST_FACT_HEADROOM,
                                                                           .flags = {.no_pool = true}}, C_FRONTEND_RESERVATION_ANALYSIS) : 0;
        CAstSpans* spans = arena ? arena_allocate(arena, CAstSpans, 1) : 0;
        ready = spans && c_ast_spans_build(arena, ast, preprocess->tokens, (u32)preprocess->token_count, matches, spans);
        if (ready)
        {
            facts->index_arena = arena;
            facts->spans = spans;
            facts->matches = matches;
            facts->statistics.retained_bytes += arena->position - arena_minimum_position;
        }
        else if (arena)
        {
            arena_destroy(arena, 1);
        }
    }
    return ready;
}

u32 c_ast_facts_locate(CAstFacts const* facts, CPreprocessResult const* preprocess, u32 start, u32 end)
{
    return facts && facts->spans ? c_ast_spans_locate(facts->spans, preprocess->tokens, facts->matches, start, end) : C_AST_NODE_INVALID;
}

CAstConstantFact c_ast_facts_constant(CAstFacts const* facts, u32 node)
{
    CAstConstantFact result = {.type = C_TYPE_ID_INVALID, .primitive_kind = C_TYPE_INVALID, .status = C_AST_CONSTANT_UNQUERIED};
    if (facts && facts->constant_slots && node < facts->node_count)
    {
        u32 stored = facts->constant_slots[node];
        if (stored && stored - 1 < facts->constant_count)
        {
            result = facts->constants[stored - 1];
        }
    }
    return result;
}

// A homogeneous sparse buffer, bounded by the unit's node count. The final
// capacity may stop between powers of two; its reservation includes all
// abandoned prefixes as well as that last carve.
BUSTER_GLOBAL_LOCAL void* c_ast_facts_sparse_reserve(CAstFacts* facts, void* rows, u32 count, u32 capacity, u32 required,
                                                   u64 size, u64 alignment, u32* capacity_out)
{
    void* result = rows;
    u32 grown_capacity = capacity;
    if (required <= facts->node_count && required > capacity)
    {
        grown_capacity = capacity ? capacity : BUSTER_MIN(facts->node_count, C_AST_CONSTANT_INITIAL_CAPACITY);
        while (grown_capacity < required)
        {
            grown_capacity = grown_capacity <= facts->node_count / 2 ? grown_capacity * 2 : facts->node_count;
        }
        result = arena_allocate_bytes(facts->constants_arena, size * grown_capacity, alignment);
        if (count)
        {
            memcpy(result, rows, size * count);
        }
    }
    *capacity_out = grown_capacity;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 c_ast_facts_retained_size(CAstFacts const* facts)
{
    return (facts->arena ? facts->arena->position - arena_minimum_position : 0) +
           (facts->index_arena ? facts->index_arena->position - arena_minimum_position : 0) +
           (facts->recipe_arena ? facts->recipe_arena->position - arena_minimum_position : 0) +
           (facts->constants_arena ? facts->constants_arena->position - arena_minimum_position : 0) +
           (facts->bound_arena ? facts->bound_arena->position - arena_minimum_position : 0);
}

// Metadata-only roots remain UNQUERIED until a semantic scalar query runs.
u32 c_ast_facts_bound_node(CAstFacts const* facts, u32 bound_index)
{
    return facts && facts->bound_nodes && bound_index < facts->bound_count ? facts->bound_nodes[bound_index] : C_AST_NODE_INVALID;
}

u32 c_ast_facts_alignment_node(CAstFacts const* facts, u32 index)
{
    return facts && facts->alignment_nodes && index < facts->alignment_count ? facts->alignment_nodes[index] : C_AST_NODE_INVALID;
}

bool c_ast_facts_alignment_is_standard(CAstFacts const* facts, u32 index)
{
    return facts && facts->alignment_standard && index < facts->alignment_count &&
           (facts->alignment_standard[index / 8] & (1u << (index % 8)));
}

u32 c_ast_facts_member_width_node(CAstFacts const* facts, u32 member)
{
    u32 first = 0;
    u32 end = facts ? facts->member_width_count : 0;
    while (first < end)
    {
        u32 middle = first + (end - first) / 2;
        if (facts->member_widths[middle].member < member) first = middle + 1;
        else end = middle;
    }
    return facts && first < facts->member_width_count && facts->member_widths[first].member == member
               ? facts->member_widths[first].node : C_AST_NODE_INVALID;
}

bool c_ast_facts_prepare_value_owners(CAstFacts* facts, u32 bounds, u32 alignments, u32 widths)
{
    bool prepared = facts && facts->constants_arena;
    if (prepared && (bounds != facts->bound_count || alignments != facts->alignment_count || widths != facts->member_width_count))
    {
        u64 bits = ((u64)alignments + 7) / 8;
        u64 reserved = ((u64)bounds + alignments) * sizeof(u32) + bits + (u64)widths * sizeof(CAstMemberWidthOwner) + C_AST_FACT_HEADROOM;
        bool populated = bounds || alignments || widths;
        Arena* arena = populated && reserved <= ARENA_MAX_RESERVATION
            ? c_frontend_arena_create((ArenaCreation){.reserved_size = reserved, .initial_size = C_AST_FACT_HEADROOM,
                                                      .flags = {.no_pool = true}}, C_FRONTEND_RESERVATION_ANALYSIS) : 0;
        prepared = !populated || arena != 0;
        if (prepared)
        {
            if (facts->bound_arena) arena_destroy(facts->bound_arena, 1);
            facts->bound_arena = arena;
            facts->bound_nodes = bounds ? arena_allocate(arena, u32, bounds) : 0;
            facts->bound_count = bounds;
            if (bounds) memset(facts->bound_nodes, 0xff, sizeof(u32) * bounds);
            facts->alignment_nodes = alignments ? arena_allocate(arena, u32, alignments) : 0;
            facts->alignment_standard = alignments ? arena_allocate_zeroed(arena, u8, bits) : 0;
            facts->alignment_count = alignments;
            if (alignments) memset(facts->alignment_nodes, 0xff, sizeof(u32) * alignments);
            facts->member_widths = widths ? arena_allocate(arena, CAstMemberWidthOwner, widths) : 0;
            facts->member_width_count = widths;
            facts->statistics.retained_bytes = c_ast_facts_retained_size(facts);
        }
    }
    return prepared;
}

bool c_ast_facts_prepare_bound_nodes(CAstFacts* facts, u32 count)
{
    return c_ast_facts_prepare_value_owners(facts, count, facts ? facts->alignment_count : 0, facts ? facts->member_width_count : 0);
}

BUSTER_GLOBAL_LOCAL u32 c_ast_facts_constant_slot(CAstFacts* facts, u32 node)
{
    u32 stored = 0;
    if (facts && facts->constant_slots && facts->constants_arena && node < facts->node_count)
    {
        stored = facts->constant_slots[node];
        if (!stored)
        {
            facts->constants = (CAstConstantFact*)c_ast_facts_sparse_reserve(facts, facts->constants, facts->constant_count,
                facts->constant_capacity, facts->constant_count + 1, sizeof(*facts->constants), BUSTER_ALIGN_OF(CAstConstantFact),
                &facts->constant_capacity);
            if (facts->constant_count < facts->constant_capacity)
            {
                stored = ++facts->constant_count;
                facts->constant_slots[node] = stored;
                facts->constants[stored - 1] = (CAstConstantFact){.type = C_TYPE_ID_INVALID, .primitive_kind = C_TYPE_INVALID};
            }
        }
    }
    return stored;
}

bool c_ast_facts_store_constant(CAstFacts* facts, u32 node, CAstConstantFact fact)
{
    u32 stored = fact.status > C_AST_CONSTANT_UNQUERIED && fact.status < C_AST_CONSTANT_STATUS_COUNT
        ? c_ast_facts_constant_slot(facts, node) : 0;
    bool valid = stored != 0;
    if (valid)
    {
        CAstConstantFact* previous = facts->constants + stored - 1;
        fact.offsetof_plus_one = fact.offsetof_plus_one ? fact.offsetof_plus_one : previous->offsetof_plus_one;
        fact.ext80_initializer_plus_one = fact.ext80_initializer_plus_one ? fact.ext80_initializer_plus_one : previous->ext80_initializer_plus_one;
        fact.selected_node_plus_one = fact.selected_node_plus_one ? fact.selected_node_plus_one : previous->selected_node_plus_one;
        // A later unmodelled probe cannot erase an earlier certified scalar
        // result; destination initializer and path annotations remain separate.
        bool certified = previous->status == C_AST_CONSTANT_INTEGER || previous->status == C_AST_CONSTANT_FLOAT;
        if (!certified || fact.status != C_AST_CONSTANT_UNSUPPORTED)
        {
            *previous = fact;
        }
        else
        {
            previous->offsetof_plus_one = fact.offsetof_plus_one;
            previous->ext80_initializer_plus_one = fact.ext80_initializer_plus_one;
            previous->selected_node_plus_one = fact.selected_node_plus_one;
        }
        facts->statistics.retained_bytes = c_ast_facts_retained_size(facts);
    }
    return valid;
}

u32 c_ast_facts_selection(CAstFacts const* facts, u32 node)
{
    CAstConstantFact fact = c_ast_facts_constant(facts, node);
    return fact.selected_node_plus_one && fact.selected_node_plus_one - 1 < facts->node_count
               ? fact.selected_node_plus_one - 1 : C_AST_NODE_INVALID;
}

u32 c_ast_facts_call_builtin(CAst const* ast, CAstFacts const* facts, u32 node)
{
    u32 result = ast && facts && node < ast->node_count && node < facts->node_count && ast->kinds[node] == C_AST_CALL &&
                         facts->payloads[node] < C_SYMBOL_BUILTIN_COUNT
                     ? facts->payloads[node] : C_AST_FACT_NONE;
    return result;
}

bool c_ast_facts_store_selection(CAstFacts* facts, u32 node, u32 selected_node)
{
    u32 stored = facts && selected_node < facts->node_count ? c_ast_facts_constant_slot(facts, node) : 0;
    if (stored)
    {
        facts->constants[stored - 1].selected_node_plus_one = selected_node + 1;
        facts->statistics.retained_bytes = c_ast_facts_retained_size(facts);
    }
    return stored != 0;
}

CAstOffsetofPlan const* c_ast_facts_offsetof(CAstFacts const* facts, u32 node)
{
    CAstConstantFact fact = c_ast_facts_constant(facts, node);
    CAstOffsetofPlan const* result = 0;
    if (fact.offsetof_plus_one && fact.offsetof_plus_one - 1 < facts->offsetof_plan_count)
    {
        result = facts->offsetof_plans + fact.offsetof_plus_one - 1;
    }
    return result;
}

bool c_ast_facts_store_offsetof(CAstFacts* facts, u32 node, CAstOffsetofStep const* steps, u32 count, bool valid)
{
    u32 stored = (!count || steps) && facts && count <= facts->node_count ? c_ast_facts_constant_slot(facts, node) : 0;
    bool recorded = stored != 0;
    if (recorded && !facts->constants[stored - 1].offsetof_plus_one)
    {
        recorded = count <= facts->node_count - facts->offsetof_step_count && facts->offsetof_plan_count < facts->node_count;
        if (recorded)
        {
            facts->offsetof_plans = (CAstOffsetofPlan*)c_ast_facts_sparse_reserve(facts, facts->offsetof_plans, facts->offsetof_plan_count,
                facts->offsetof_plan_capacity, facts->offsetof_plan_count + 1, sizeof(*facts->offsetof_plans), BUSTER_ALIGN_OF(CAstOffsetofPlan),
                &facts->offsetof_plan_capacity);
            facts->offsetof_steps = (CAstOffsetofStep*)c_ast_facts_sparse_reserve(facts, facts->offsetof_steps, facts->offsetof_step_count,
                facts->offsetof_step_capacity, facts->offsetof_step_count + count, sizeof(*facts->offsetof_steps), BUSTER_ALIGN_OF(CAstOffsetofStep),
                &facts->offsetof_step_capacity);
            recorded = facts->offsetof_plan_count < facts->offsetof_plan_capacity &&
                       count <= facts->offsetof_step_capacity - facts->offsetof_step_count;
        }
        if (recorded)
        {
            u32 plan = facts->offsetof_plan_count++;
            facts->offsetof_plans[plan] = (CAstOffsetofPlan){.first_step = facts->offsetof_step_count, .step_count = count, .valid = valid};
            if (count)
            {
                memcpy(facts->offsetof_steps + facts->offsetof_step_count, steps, sizeof(*steps) * (u64)count);
            }
            facts->offsetof_step_count += count;
            facts->constants[stored - 1].offsetof_plus_one = plan + 1;
            facts->statistics.retained_bytes = c_ast_facts_retained_size(facts);
        }
    }
    return recorded;
}

CAstExt80InitializerFact const* c_ast_facts_ext80_initializer(CAstFacts const* facts, u32 node)
{
    CAstConstantFact fact = c_ast_facts_constant(facts, node);
    CAstExt80InitializerFact const* result = 0;
    if (fact.ext80_initializer_plus_one && fact.ext80_initializer_plus_one - 1 < facts->ext80_initializer_count)
    {
        result = facts->ext80_initializers + fact.ext80_initializer_plus_one - 1;
    }
    return result;
}

bool c_ast_facts_store_ext80_initializer(CAstFacts* facts, u32 node, u64 significand, u16 exponent_sign)
{
    u32 stored = c_ast_facts_constant_slot(facts, node);
    bool recorded = stored != 0;
    if (recorded)
    {
        u32 initializer = facts->constants[stored - 1].ext80_initializer_plus_one;
        if (!initializer)
        {
            facts->ext80_initializers = (CAstExt80InitializerFact*)c_ast_facts_sparse_reserve(facts, facts->ext80_initializers,
                facts->ext80_initializer_count, facts->ext80_initializer_capacity, facts->ext80_initializer_count + 1,
                sizeof(*facts->ext80_initializers), BUSTER_ALIGN_OF(CAstExt80InitializerFact), &facts->ext80_initializer_capacity);
            recorded = facts->ext80_initializer_count < facts->ext80_initializer_capacity;
            if (recorded)
            {
                initializer = ++facts->ext80_initializer_count;
                facts->constants[stored - 1].ext80_initializer_plus_one = initializer;
            }
        }
        if (recorded)
        {
            facts->ext80_initializers[initializer - 1] = (CAstExt80InitializerFact){.significand = significand, .exponent_sign = exponent_sign};
            facts->statistics.retained_bytes = c_ast_facts_retained_size(facts);
        }
    }
    return recorded;
}

void c_ast_facts_release(CAstFacts* facts)
{
    if (facts->recipe_arena)
    {
        arena_destroy(facts->recipe_arena, 1);
    }
    facts->recipe_arena = 0;
    facts->recipes = 0;
    facts->recipe_count = 0;
    facts->recipe_failed = false;
    if (facts->bound_arena) arena_destroy(facts->bound_arena, 1);
    facts->bound_arena = 0;
    facts->bound_nodes = 0;
    facts->bound_count = 0;
    facts->alignment_nodes = 0;
    facts->alignment_standard = 0;
    facts->alignment_count = 0;
    facts->member_widths = 0;
    facts->member_width_count = 0;
    if (facts->constants_arena)
    {
        arena_destroy(facts->constants_arena, 1);
    }
    facts->constants_arena = 0;
    facts->constant_slots = 0;
    facts->constants = 0;
    facts->constant_count = 0;
    facts->constant_capacity = 0;
    facts->offsetof_plans = 0;
    facts->offsetof_steps = 0;
    facts->ext80_initializers = 0;
    facts->offsetof_plan_count = 0;
    facts->offsetof_plan_capacity = 0;
    facts->offsetof_step_count = 0;
    facts->offsetof_step_capacity = 0;
    facts->ext80_initializer_count = 0;
    facts->ext80_initializer_capacity = 0;
    if (facts->index_arena)
    {
        arena_destroy(facts->index_arena, 1);
    }
    facts->index_arena = 0;
    facts->spans = 0;
    facts->matches = 0;
    if (facts->arena)
    {
        c_ast_facts_retire(facts->arena);
    }
    facts->arena = 0;
    facts->types = 0;
    facts->payloads = 0;
    facts->flags = 0;
    facts->node_count = 0;
}

u64 c_ast_facts_retain_in_arena(Arena* arena, CAstFacts* facts)
{
    u64 copied = 0;
    if (arena && facts && facts->flags && facts->arena)
    {
        u64 start = arena->position;
        CAstFacts retained = *facts;
        retained.arena = 0;
        retained.index_arena = 0;
        retained.recipe_arena = 0;
        retained.constants_arena = 0;
        retained.bound_arena = 0;
        retained.types = arena_allocate(arena, u32, facts->node_count);
        retained.payloads = arena_allocate(arena, u32, facts->node_count);
        retained.flags = arena_allocate(arena, u8, facts->node_count);
        memcpy(retained.types, facts->types, sizeof(*facts->types) * facts->node_count);
        memcpy(retained.payloads, facts->payloads, sizeof(*facts->payloads) * facts->node_count);
        memcpy(retained.flags, facts->flags, sizeof(*facts->flags) * facts->node_count);
        if (facts->spans)
        {
            CAstSpans const* source = facts->spans;
            retained.spans = arena_allocate(arena, CAstSpans, 1);
            *retained.spans = *source;
            retained.spans->first = arena_allocate(arena, u32, source->node_count);
            retained.spans->end = arena_allocate(arena, u32, source->node_count);
            retained.spans->link = arena_allocate(arena, u32, source->node_count);
            retained.spans->start_head = arena_allocate(arena, u32, (u64)source->token_total + 1);
            memcpy(retained.spans->first, source->first, sizeof(u32) * source->node_count);
            memcpy(retained.spans->end, source->end, sizeof(u32) * source->node_count);
            memcpy(retained.spans->link, source->link, sizeof(u32) * source->node_count);
            memcpy(retained.spans->start_head, source->start_head, sizeof(u32) * ((u64)source->token_total + 1));
        }
        retained.recipes = facts->recipe_count ? arena_allocate(arena, CAstTypeRecipe, facts->recipe_count) : 0;
        if (facts->recipe_count)
        {
            memcpy(retained.recipes, facts->recipes, sizeof(*facts->recipes) * facts->recipe_count);
        }
        retained.constant_slots = facts->constant_slots ? arena_allocate(arena, u32, facts->node_count) : 0;
        if (facts->constant_slots)
        {
            memcpy(retained.constant_slots, facts->constant_slots, sizeof(*facts->constant_slots) * facts->node_count);
        }
        retained.constants = facts->constant_count ? arena_allocate(arena, CAstConstantFact, facts->constant_count) : 0;
        if (facts->constant_count)
        {
            memcpy(retained.constants, facts->constants, sizeof(*facts->constants) * facts->constant_count);
        }
        retained.constant_capacity = facts->constant_count;
        retained.bound_nodes = facts->bound_count ? arena_allocate(arena, u32, facts->bound_count) : 0;
        if (facts->bound_count) memcpy(retained.bound_nodes, facts->bound_nodes, sizeof(u32) * facts->bound_count);
        retained.alignment_nodes = facts->alignment_count ? arena_allocate(arena, u32, facts->alignment_count) : 0;
        u64 alignment_bits = ((u64)facts->alignment_count + 7) / 8;
        retained.alignment_standard = alignment_bits ? arena_allocate(arena, u8, alignment_bits) : 0;
        if (facts->alignment_count) memcpy(retained.alignment_nodes, facts->alignment_nodes, sizeof(u32) * facts->alignment_count);
        if (alignment_bits) memcpy(retained.alignment_standard, facts->alignment_standard, alignment_bits);
        retained.member_widths = facts->member_width_count ? arena_allocate(arena, CAstMemberWidthOwner, facts->member_width_count) : 0;
        if (facts->member_width_count) memcpy(retained.member_widths, facts->member_widths, sizeof(CAstMemberWidthOwner) * facts->member_width_count);
        retained.offsetof_plans = facts->offsetof_plan_count ? arena_allocate(arena, CAstOffsetofPlan, facts->offsetof_plan_count) : 0;
        if (facts->offsetof_plan_count)
        {
            memcpy(retained.offsetof_plans, facts->offsetof_plans, sizeof(*facts->offsetof_plans) * facts->offsetof_plan_count);
        }
        retained.offsetof_plan_capacity = facts->offsetof_plan_count;
        retained.offsetof_steps = facts->offsetof_step_count ? arena_allocate(arena, CAstOffsetofStep, facts->offsetof_step_count) : 0;
        if (facts->offsetof_step_count)
        {
            memcpy(retained.offsetof_steps, facts->offsetof_steps, sizeof(*facts->offsetof_steps) * facts->offsetof_step_count);
        }
        retained.offsetof_step_capacity = facts->offsetof_step_count;
        retained.ext80_initializers = facts->ext80_initializer_count ? arena_allocate(arena, CAstExt80InitializerFact, facts->ext80_initializer_count) : 0;
        if (facts->ext80_initializer_count)
        {
            memcpy(retained.ext80_initializers, facts->ext80_initializers, sizeof(*facts->ext80_initializers) * facts->ext80_initializer_count);
        }
        retained.ext80_initializer_capacity = facts->ext80_initializer_count;
        c_ast_facts_release(facts);
        *facts = retained;
        copied = arena->position - start;
        facts->statistics.retained_bytes = copied;
    }
    return copied;
}

// A fact candidate's kind: an expression kind the eager pass spans, but not a
// braced list, which has no type of its own.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE bool c_ast_types_fact_kind(u32 kind)
{
    return c_ast_types_kind_classes[kind] != C_AST_TYPE_CLASS_UNTYPED && kind != C_AST_INITIALIZER_LIST;
}

BUSTER_C_SHARED CAstTypeBody* c_ast_types_facts_begin(CTypeParseMachine* machine, CPreprocessResult const* preprocess)
{
    CAstTypeBody* body = machine->ast_facts && machine->ast_facts->flags ? machine->ast_types : 0;
    CAstFacts* facts = machine->ast_facts;
    if (body)
    {
        // An initializer no query typed is typed now: its lone literals took
        // the literal path, which leaves no node behind.
        if (body->waiting)
        {
            c_ast_types_region_fill(body, machine, preprocess);
        }
        CAst const* ast = body->ast;
        CAstFactStatistics* statistics = &facts->statistics;
        statistics->regions += 1;
        for (u32 node = body->begin; node <= body->node; node += 1)
        {
            u32 kind = ast->kinds[node];
            if (c_ast_types_fact_kind(kind))
            {
                u32 relative = node - body->begin;
                u32 typed = body->flags[relative];
                u32 flags = facts->flags[node] | C_AST_FACT_EXPRESSION;
                statistics->candidates += 1;
                bool accepted = (typed & C_AST_TYPE_FLAG_ACCEPTED) != 0;
#if BUSTER_INCLUDE_TESTS
                bool uncached = c_ast_types_facts_uncached();
                accepted = accepted && !uncached;
                flags = uncached ? flags & ~(u32)C_AST_FACT_TYPED : flags;
#endif
                if (accepted)
                {
                    // The typer's rule is the fact; a query's answer for the
                    // same node, which it made exact, is the same row.
                    facts->types[node] = body->types[relative].value;
                    flags = (flags | C_AST_FACT_TYPED) & ~(u32)(C_AST_FACT_MACHINE | C_AST_FACT_NONPLACE | C_AST_FACT_STRING | C_AST_FACT_RECIPE);
                    statistics->tree += 1;
                    if ((kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW) && body->widths[relative])
                    {
                        facts->payloads[node] = body->widths[relative];
                        flags |= C_AST_FACT_BIT_FIELD;
                        statistics->bit_fields += 1;
                    }
                }
                else if (flags & C_AST_FACT_TYPED)
                {
                    statistics->machine += 1;
                    statistics->strings += (flags & C_AST_FACT_STRING) != 0;
                }
                if (kind == C_AST_IDENTIFIER)
                {
                    u32 token = ast->tokens[node];
                    CEntity const* bound = token < body->token_total ? c_ast_types_bound_entity(body->result, token) : 0;
                    u32 entity = body->entities && body->entities[relative] != C_AST_TYPE_NONE ? body->entities[relative]
                                 : bound                                                     ? (u32)(bound - body->result->entities)
                                                                                             : C_AST_FACT_NONE;
                    if (entity < body->result->entity_count)
                    {
                        facts->payloads[node] = entity;
                        flags |= C_AST_FACT_BOUND;
                        statistics->bound += 1;
                    }
                }
                facts->flags[node] = (u8)flags;
            }
        }
    }
    return body;
}

BUSTER_C_SHARED void c_ast_types_facts_limits(CAstTypeBody const* region, u32* begin, u32* end)
{
    *begin = region->begin;
    *end = region->node + 1;
}

BUSTER_C_SHARED bool c_ast_types_facts_next(CAstTypeBody const* region, CAstFacts* facts, u32* cursor, u32* first_out, u32* end_out)
{
    bool found = false;
    u32 node = *cursor > region->begin ? *cursor : region->begin;
    while (!found && node <= region->node)
    {
        if ((facts->flags[node] & (C_AST_FACT_EXPRESSION | C_AST_FACT_TYPED)) == C_AST_FACT_EXPRESSION)
        {
            found = region->first[node - region->begin] != C_AST_TYPE_NONE;
            facts->statistics.gaps[C_AST_FACT_GAP_UNSPANNED] += !found;
        }
        node += found ? 0 : 1;
    }
    *first_out = found ? region->first[node - region->begin] : C_AST_TYPE_NONE;
    *end_out = found ? region->end[node - region->begin] : C_AST_TYPE_NONE;
    *cursor = node;
    return found;
}

BUSTER_C_SHARED void c_ast_types_facts_record(CAstFacts* facts, u32 node, CTypeId type, bool nonplace)
{
    facts->types[node] = type.value;
    facts->flags[node] |= (u8)(C_AST_FACT_TYPED | C_AST_FACT_MACHINE | (nonplace ? C_AST_FACT_NONPLACE : 0u));
    facts->statistics.completed += 1;
}

// The array row c_parse_string_literal_expression_type mints: an unqualified,
// complete array of a published scalar row with an inferred bound of the
// literal's length plus its terminator.
BUSTER_C_SHARED bool c_ast_types_facts_string(CAstFacts* facts, CParseResult const* result, CAst const* ast, u32 node, CTypeId array, u32 rows)
{
    CType const* row = ast->kinds[node] == C_AST_STRING && array.value < result->type_count ? result->types + array.value : 0;
    CArrayBound const* bound = row && row->kind == C_TYPE_ARRAY && row->array_bound < result->array_bound_count ? result->array_bounds + row->array_bound : 0;
    bool kept = bound && bound->has_inferred_count && bound->inferred_count < C_AST_FACT_NONE && row->element_type.value < rows && !row->is_const &&
                !row->is_volatile && !row->is_restrict && !row->is_atomic;
    if (kept)
    {
        facts->types[node] = row->element_type.value;
        facts->payloads[node] = (u32)bound->inferred_count;
        facts->flags[node] = (u8)((facts->flags[node] & ~(u32)C_AST_FACT_NONPLACE) | C_AST_FACT_TYPED | C_AST_FACT_MACHINE | C_AST_FACT_STRING);
    }
    return kept;
}

// Only sparse descriptions live in this arena; every allocation has the
// same element size and alignment, so the index remains stable in place.
BUSTER_GLOBAL_LOCAL CAstTypeRecipe* c_ast_types_recipes_append(CAstFacts* facts, u32 count)
{
    if (!facts->recipe_arena && !facts->recipe_failed)
    {
        facts->recipe_arena = c_frontend_arena_create((ArenaCreation){.reserved_size = C_PHASE_ARENA_RESERVED_SIZE,
                                                                        .initial_size = C_AST_FACT_HEADROOM,
                                                                        .flags = {.no_pool = true}}, C_FRONTEND_RESERVATION_ANALYSIS);
        facts->recipe_failed = !facts->recipe_arena;
    }
    Arena* arena = facts->recipe_arena;
    u64 first = arena ? align_forward(arena->position, BUSTER_ALIGN_OF(CAstTypeRecipe)) : 0;
    bool room = arena && first <= arena->reserved_size && count < C_AST_FACT_NONE - facts->recipe_count &&
                (u64)count * sizeof(CAstTypeRecipe) <= arena->reserved_size - first;
    CAstTypeRecipe* appended = room ? arena_allocate(arena, CAstTypeRecipe, count) : 0;
    if (appended)
    {
        facts->recipes = facts->recipes ? facts->recipes : appended;
        BUSTER_CHECK(appended == facts->recipes + facts->recipe_count);
        facts->recipe_count += count;
    }
    facts->recipe_failed = facts->recipe_failed || !appended;
    return appended;
}

// Capture the complete temporary row and parameter runs before restoring the
// semantic checkpoint. Function parameter references use the parallel run's
// base; both kinds of rows remain in the same sparse, pointer-free array.
BUSTER_C_SHARED u32 c_ast_types_facts_snapshot(CAstFacts* facts, CParseResult const* result, CPreprocessResult const* preprocess,
                                               CParseResult const* checkpoint)
{
    u32 rows = checkpoint->type_count;
    u32 count = result->type_count - rows;
    u32 parameters = result->parameter_count - checkpoint->parameter_count;
    u32 base = facts->recipe_count;
    bool room = count <= C_AST_FACT_NONE - parameters;
    CAstTypeRecipe* recipes = room && count + parameters ? c_ast_types_recipes_append(facts, count + parameters) : 0;
    u32 answer = recipes ? base : C_AST_FACT_NONE;
    for (u32 index = 0; recipes && index < count + parameters; index += 1)
    {
        recipes[index] = (CAstTypeRecipe){.row_begin = rows, .recipe_begin = base, .recipe_end = base + count + parameters,
                                        .parameter_row_begin = checkpoint->parameter_count, .parameter_recipe_begin = base + count,
                                        .declaration = C_AST_FACT_NONE, .bound_node = C_AST_NODE_INVALID};
        if (index < count)
        {
            CType row = result->types[rows + index];
            CTypeAlignment const* alignment = c_parse_type_alignment(result, (CTypeId){.value = rows + index});
            CArrayBound bound = row.kind == C_TYPE_ARRAY && row.array_bound < result->array_bound_count
                                    ? result->array_bounds[row.array_bound] : (CArrayBound){0};
            u32 bound_node = bound.token_count ? c_ast_facts_locate(facts, preprocess, bound.token_start, bound.token_start + bound.token_count)
                                             : C_AST_NODE_INVALID;
            recipes[index].type = row;
            recipes[index].bound = bound;
            recipes[index].resolved_count = bound.inferred_count;
            recipes[index].count_is_constant = bound.has_inferred_count;
            recipes[index].bound_node = bound_node;
            recipes[index].alignment = alignment ? *alignment : (CTypeAlignment){0};
            recipes[index].has_alignment = alignment != 0;
        }
        else
        {
            recipes[index].parameter = result->parameters[checkpoint->parameter_count + index - count];
            recipes[index].parameter_source_type = recipes[index].parameter.type;
            recipes[index].form = C_AST_TYPE_RECIPE_PARAMETER;
        }
    }
    return answer;
}

BUSTER_C_SHARED bool c_ast_types_facts_recipe(CAstFacts* facts, CParseResult const* result, CPreprocessResult const* preprocess,
                                               u32 node, CTypeId type, u32 rows)
{
    bool kept = rows <= type.value && type.value < result->type_count;
    CParseResult checkpoint = {.type_count = rows, .parameter_count = result->parameter_count};
    u32 base = kept ? c_ast_types_facts_snapshot(facts, result, preprocess, &checkpoint) : C_AST_FACT_NONE;
    kept = base != C_AST_FACT_NONE;
    if (kept)
    {
        facts->types[node] = base + type.value - rows;
        facts->flags[node] |= C_AST_FACT_TYPED | C_AST_FACT_MACHINE | C_AST_FACT_RECIPE;
        facts->statistics.completed += 1;
        facts->statistics.recipes += 1;
    }
    return kept;
}

// The two candidate forms for which the semantic machine has no ordinary
// CType answer. Builtin identities are admitted only for recognised semantic
// builtins, not for an arbitrary unbound spelling. Function-name objects name
// the declaration whose body actually contains the use.
BUSTER_C_SHARED bool c_ast_types_facts_identity(CAstFacts* facts, CParseResult const* result, CAst const* ast,
                                                 CPreprocessResult const* preprocess, u32 node)
{
    u32 token = ast->tokens[node];
    String8 name = token < preprocess->token_count ? c_token_spelling(preprocess->spelling_base, preprocess->tokens[token]) : S8("");
    bool identifier = ast->kinds[node] == C_AST_IDENTIFIER && !(facts->flags[node] & C_AST_FACT_BOUND);
    bool function_name = identifier && (string_equal(name, S8("__func__")) || string_equal(name, S8("__FUNCTION__")) ||
                                        string_equal(name, S8("__PRETTY_FUNCTION__")));
    CSymbolBuiltin builtin = identifier && !function_name ? c_symbol_builtin_from_spelling(name) : C_SYMBOL_BUILTIN_NONE;
    u32 declaration = C_AST_FACT_NONE;
    // Body binding may append block-scope function prototypes after the
    // file declarations, so this table is not globally sorted by source.
    for (u32 index = 0; function_name && declaration == C_AST_FACT_NONE && index < result->declaration_count; index += 1)
    {
        CDeclaration const* row = result->declarations + index;
        declaration = row->kind == C_DECLARATION_FUNCTION && token >= row->body_start && token - row->body_start < row->body_token_count
                          ? index : C_AST_FACT_NONE;
    }
    bool kept = declaration != C_AST_FACT_NONE || builtin != C_SYMBOL_BUILTIN_NONE;
    u32 index = facts->recipe_count;
    CAstTypeRecipe* recipe = kept ? c_ast_types_recipes_append(facts, 1) : 0;
    kept = recipe != 0;
    if (kept)
    {
        *recipe = (CAstTypeRecipe){.form = declaration != C_AST_FACT_NONE ? C_AST_TYPE_RECIPE_FUNCTION_NAME : C_AST_TYPE_RECIPE_BUILTIN,
                                       .declaration = declaration, .builtin = (u32)builtin, .bound_node = C_AST_NODE_INVALID,
                                       .type = {.element_type = C_TYPE_ID_INVALID, .return_type = C_TYPE_ID_INVALID,
                                                .unqualified_type = C_TYPE_ID_INVALID, .array_bound = C_ARRAY_BOUND_INVALID}};
        if (declaration != C_AST_FACT_NONE)
        {
            recipe->bound = (CArrayBound){.inferred_count = result->declarations[declaration].name.length + 1, .has_inferred_count = true};
            recipe->type.kind = C_TYPE_ARRAY;
            recipe->type.is_complete = true;
            recipe->type.is_const = true;
            recipe->type.element_type = result->expression_scalar_types ? result->expression_scalar_types[C_TYPE_CHAR] : C_TYPE_ID_INVALID;
        }
        facts->types[node] = index;
        facts->flags[node] |= C_AST_FACT_TYPED | C_AST_FACT_MACHINE | C_AST_FACT_RECIPE;
        facts->statistics.completed += 1;
        facts->statistics.function_names += declaration != C_AST_FACT_NONE;
        facts->statistics.builtin_identities += declaration == C_AST_FACT_NONE;
    }
    return kept;
}

// A published row or row-free description. References in a description's
// temporary run remain recipe references rather than reused type-table ids.
typedef struct CAstFactTypeReference CAstFactTypeReference;
struct CAstFactTypeReference
{
    CType const* type;
    u32 index;
    bool recipe;
};

BUSTER_GLOBAL_LOCAL CAstFactTypeReference c_ast_types_fact_reference(CAstFacts const* facts, CParseResult const* result, u32 index, bool recipe)
{
    CType const* type = recipe ? index < facts->recipe_count && facts->recipes[index].form == C_AST_TYPE_RECIPE_DERIVATION
                                    ? &facts->recipes[index].type : 0
                              : index < result->type_count ? result->types + index : 0;
    return (CAstFactTypeReference){.type = type, .index = index, .recipe = recipe};
}

BUSTER_GLOBAL_LOCAL CAstFactTypeReference c_ast_types_fact_node(CAstFacts const* facts, CParseResult const* result, u32 node)
{
    u32 flags = node < facts->node_count ? facts->flags[node] : 0;
    return (flags & C_AST_FACT_TYPED) && !(flags & C_AST_FACT_STRING)
               ? c_ast_types_fact_reference(facts, result, facts->types[node], (flags & C_AST_FACT_RECIPE) != 0)
               : (CAstFactTypeReference){.index = C_AST_FACT_NONE};
}

BUSTER_GLOBAL_LOCAL CAstFactTypeReference c_ast_types_fact_nested(CAstFacts const* facts, CParseResult const* result,
                                                                 CAstFactTypeReference parent, CTypeId nested)
{
    CAstTypeRecipe const* recipe = parent.recipe ? facts->recipes + parent.index : 0;
    bool temporary = recipe && nested.value != C_ID_UNDERLYING_INVALID && nested.value >= recipe->row_begin;
    u32 index = temporary ? recipe->recipe_begin + nested.value - recipe->row_begin : nested.value;
    return c_ast_types_fact_reference(facts, result, index, temporary);
}

// Facts can describe valid nodes the range machine cannot answer: a call
// through a member, a member of a compound literal, and vector bitwise values.
// This is publication only. It reads finished operand facts and the existing
// semantic member/scalar helpers, without changing any validator's answer.
BUSTER_C_SHARED bool c_ast_types_facts_derive(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult const* preprocess, u32 node,
                                               CTypeId* type_out, u32* recipe_out)
{
    CAstFacts* facts = machine->ast_facts;
    CAst const* ast = machine->syntax_tree;
    CAstFactTypeReference answer = {.index = C_AST_FACT_NONE};
    u32 kind = ast->kinds[node];
    if (result && kind == C_AST_DEREFERENCE)
    {
        CAstFactTypeReference base = c_ast_types_fact_node(facts, result, c_ast_child_at(ast, node, 0));
        if (base.type && (base.type->kind == C_TYPE_POINTER || base.type->kind == C_TYPE_ARRAY))
        {
            answer = c_ast_types_fact_nested(facts, result, base, base.type->element_type);
        }
        else if (base.type && base.type->kind == C_TYPE_FUNCTION)
        {
            // A function designator decays for the next indirection, whose
            // result is that same function designator.
            answer = base;
        }
    }
    else if (result && (kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW || kind == C_AST_CALL))
    {
        u32 child = c_ast_child_at(ast, node, 0);
        CAstFactTypeReference base = c_ast_types_fact_node(facts, result, child);
        if (base.type && (kind == C_AST_MEMBER_ARROW || (kind == C_AST_CALL && base.type->kind == C_TYPE_POINTER)))
        {
            base = base.type->kind == C_TYPE_POINTER ? c_ast_types_fact_nested(facts, result, base, base.type->element_type)
                                                     : (CAstFactTypeReference){.index = C_AST_FACT_NONE};
        }
        u32 child_flags = facts->flags[child];
        CAstTypeRecipe const* callee = (child_flags & (C_AST_FACT_TYPED | C_AST_FACT_RECIPE)) == (C_AST_FACT_TYPED | C_AST_FACT_RECIPE) &&
                                              facts->types[child] < facts->recipe_count ? facts->recipes + facts->types[child] : 0;
        if (kind == C_AST_CALL && callee && callee->form == C_AST_TYPE_RECIPE_BUILTIN && callee->builtin == C_SYMBOL_BUILTIN_VA_ARG)
        {
            u32 argument = c_ast_child_at(ast, node, c_ast_child_count(ast, node) - 1);
            answer = ast->kinds[argument] == C_AST_TYPE_NAME ? c_ast_types_fact_node(facts, result, argument)
                                                           : (CAstFactTypeReference){.index = C_AST_FACT_NONE};
        }
        else if (base.type && kind == C_AST_CALL && base.type->kind == C_TYPE_FUNCTION)
        {
            answer = c_ast_types_fact_nested(facts, result, base, base.type->return_type);
        }
        else if (base.type && kind != C_AST_CALL && (base.type->kind == C_TYPE_STRUCT || base.type->kind == C_TYPE_UNION))
        {
            u32 width = 0;
            CTypeId aggregate = base.recipe ? C_TYPE_ID_INVALID : (CTypeId){.value = base.index};
            CTypeId member = aggregate.value < result->type_count
                                 ? c_parse_member_type(machine->scratch_arena, result, aggregate, ast->data[node],
                                                       c_token_spelling(preprocess->spelling_base, preprocess->tokens[ast->tokens[node]]), &width, 0, 0)
                                 : C_TYPE_ID_INVALID;
            if (member.value < result->type_count)
            {
                CType qualifiers = {.is_const = base.type->is_const, .is_volatile = base.type->is_volatile,
                                    .is_restrict = base.type->is_restrict, .is_atomic = base.type->is_atomic};
                member = qualifiers.is_const || qualifiers.is_volatile || qualifiers.is_restrict || qualifiers.is_atomic
                             ? c_parse_add_qualified_type(result, member, qualifiers) : member;
                answer = c_ast_types_fact_reference(facts, result, member.value, false);
                if (width)
                {
                    facts->payloads[node] = width;
                    facts->flags[node] |= C_AST_FACT_BIT_FIELD;
                    facts->statistics.bit_fields += 1;
                }
            }
        }
    }
    else if (result && ((kind >= C_AST_LESS && kind <= C_AST_NOT_EQUAL) || kind == C_AST_LOGICAL_AND || kind == C_AST_LOGICAL_OR))
    {
        answer = c_ast_types_fact_reference(facts, result, c_parse_expression_scalar_type(result, C_TYPE_INT).value, false);
    }
    else if (result && (kind == C_AST_BIT_AND || kind == C_AST_BIT_XOR || kind == C_AST_BIT_OR || kind == C_AST_CONDITIONAL))
    {
        u32 children = c_ast_child_count(ast, node);
        CAstFactTypeReference left = c_ast_types_fact_node(facts, result, c_ast_child_at(ast, node, children - 2));
        CAstFactTypeReference right = c_ast_types_fact_node(facts, result, c_ast_child_at(ast, node, children - 1));
        bool same = left.type && right.type && left.index == right.index && left.recipe == right.recipe;
        bool vector = left.type && right.type && left.type->kind == C_TYPE_VECTOR && right.type->kind == C_TYPE_VECTOR &&
                      left.type->vector_byte_size == right.type->vector_byte_size && !left.type->is_const && !left.type->is_volatile &&
                      !left.type->is_atomic && !right.type->is_const && !right.type->is_volatile && !right.type->is_atomic;
        if (vector)
        {
            CAstFactTypeReference a = c_ast_types_fact_nested(facts, result, left, left.type->element_type);
            CAstFactTypeReference b = c_ast_types_fact_nested(facts, result, right, right.type->element_type);
            vector = a.type && b.type && a.index == b.index && a.recipe == b.recipe;
        }
        if (vector || (kind == C_AST_CONDITIONAL && same && (left.type->kind == C_TYPE_STRUCT || left.type->kind == C_TYPE_UNION)))
        {
            answer = left;
        }
        else if (kind == C_AST_CONDITIONAL && left.type && right.type && !left.recipe && !right.recipe)
        {
            u32 a = c_ast_child_at(ast, node, children - 2);
            u32 b = c_ast_child_at(ast, node, children - 1);
            CScopeId scope = c_parse_scope_for_token(result, (CScopeId){0}, ast->tokens[node]);
            CTypeId type = c_parse_conditional_expression_type(machine->scratch_arena, *preprocess, result, scope,
                                                              (CTypeId){.value = left.index}, (CTypeId){.value = right.index},
                                                              facts->spans->first[a], facts->spans->end[a],
                                                              facts->spans->first[b], facts->spans->end[b]);
            answer = c_ast_types_fact_reference(facts, result, type.value, false);
        }
    }
    bool valid = answer.type != 0;
    *type_out = valid && !answer.recipe ? (CTypeId){.value = answer.index} : C_TYPE_ID_INVALID;
    *recipe_out = valid && answer.recipe ? answer.index : C_AST_FACT_NONE;
    return valid;
}

// A top-level query's valid answer, kept for the fact candidate whose span
// [start, end) is, when nothing has typed it yet. The region's own pass may
// still overwrite it with the typer's rule (c_ast_types_facts_begin). A
// waiting initializer has no chains to locate through; its pass at the
// region's end types every node the literal path answered.
BUSTER_C_SHARED void c_ast_types_facts_answer(CTypeParseMachine* machine, u32 start, u32 end, CTypeId type, bool nonplace)
{
    CAstTypeBody const* body = machine->ast_types;
    CAstFacts* facts = machine->ast_facts;
    if (body && facts && facts->flags && !body->waiting)
    {
        u32 relative = c_ast_types_locate(body, start, end);
        u32 node = relative != C_AST_TYPE_NONE ? body->begin + relative : C_AST_TYPE_NONE;
        bool open = node != C_AST_TYPE_NONE && c_ast_types_fact_kind(body->ast->kinds[node]) && !(facts->flags[node] & C_AST_FACT_TYPED);
        // A string literal is kept in structural form, as the completion run
        // keeps it, whether or not this query minted its row.
        if (open && !c_ast_types_facts_string(facts, body->result, body->ast, node, type, body->result->type_count) &&
            body->ast->kinds[node] != C_AST_STRING)
        {
            facts->types[node] = type.value;
            facts->flags[node] |= (u8)(C_AST_FACT_TYPED | C_AST_FACT_MACHINE | (nonplace ? C_AST_FACT_NONPLACE : 0u));
        }
    }
}

// Once analysis is done: drops a published row that a rollback left past the
// type table (none is expected; c_parse_region_facts keeps only answers that
// appended nothing, and a top-level query's rows are never rolled back), and
// counts the expression nodes no typed region covered, inside a function
// definition or not. The backward scan keeps the interval of the outermost
// definition it is in; a node before that interval's first node has left it.
// Direct CALL callees retain builtin identity independently of their type.
// A header may declare an intrinsic (for example _mm_pause) as a function;
// standalone uses of that function designator still keep its ordinary CType.
BUSTER_GLOBAL_LOCAL CSymbolBuiltin c_ast_types_call_builtin(CAst const* ast, CPreprocessResult const* preprocess, u32 node)
{
    u32 callee = c_ast_child_at(ast, node, 0);
    CSymbolBuiltin result = C_SYMBOL_BUILTIN_NONE;
    u32 kind = callee < ast->node_count ? ast->kinds[callee] : C_AST_KIND_COUNT;
    bool named = kind == C_AST_IDENTIFIER;
    if (named && !c_ast_parentheses(ast, callee).count)
    {
        u32 anchor = ast->tokens[callee];
        CToken token = preprocess->tokens[anchor];
        CSymbolTable* symbols = preprocess->symbols;
        if (token.symbol && symbols && token.symbol <= symbols->predefined_limit)
        {
            result = (CSymbolBuiltin)symbols->builtin_kinds[token.symbol];
        }
        else
        {
            String8 spelling = c_token_spelling(preprocess->spelling_base, token);
            if (!token.symbol || !symbols || string_starts_with_sequence(spelling, S8("__builtin_")) || string_equal(spelling, S8("__rdtsc")))
            {
                result = c_symbol_builtin_from_spelling(spelling);
            }
        }
    }
    return result;
}

BUSTER_C_SHARED void c_ast_types_facts_finish(CAstFacts* facts, CAst const* ast, CParseResult const* result, CPreprocessResult const* preprocess)
{
    if (facts && facts->flags && ast && ast->node_count == facts->node_count)
    {
        CAstFactStatistics* statistics = &facts->statistics;
        u32 floor = C_AST_TYPE_NONE;
        for (u32 index = facts->node_count; index; index -= 1)
        {
            u32 node = index - 1;
            u32 flags = facts->flags[node];
            u32 kind = ast->kinds[node];
            if (kind == C_AST_CALL)
            {
                facts->payloads[node] = (u32)c_ast_types_call_builtin(ast, preprocess, node);
            }
            floor = floor != C_AST_TYPE_NONE && node < floor ? C_AST_TYPE_NONE : floor;
            floor = floor == C_AST_TYPE_NONE && kind == C_AST_FUNCTION_DEFINITION ? c_ast_subtree_begin(ast, node) : floor;
            if ((flags & C_AST_FACT_TYPED) && !(flags & C_AST_FACT_RECIPE) && facts->types[node] >= result->type_count)
            {
                facts->types[node] = C_AST_FACT_NONE;
                facts->flags[node] = (u8)(flags & ~(u32)(C_AST_FACT_TYPED | C_AST_FACT_MACHINE | C_AST_FACT_NONPLACE | C_AST_FACT_STRING | C_AST_FACT_RECIPE));
                statistics->stale += 1;
            }
            if (kind == C_AST_TYPE_NAME)
            {
                statistics->type_names += 1;
                statistics->typed_type_names += (facts->flags[node] & C_AST_FACT_TYPED) != 0;
                statistics->missing_type_names += (facts->flags[node] & C_AST_FACT_TYPED) == 0;
            }
            if (!(flags & C_AST_FACT_EXPRESSION) && c_ast_types_fact_kind(kind))
            {
                statistics->uncovered += floor != C_AST_TYPE_NONE;
                statistics->outside += floor == C_AST_TYPE_NONE;
            }
        }
        statistics->retained_bytes = c_ast_facts_retained_size(facts);
    }
}

// ---- verification and test seams ---------------------------------------------------

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool c_ast_types_verify_enabled;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool c_ast_types_facts_uncached_enabled;

void c_test_ast_fact_uncached_set(bool enabled)
{
    c_ast_types_facts_uncached_enabled = enabled;
}

BUSTER_C_SHARED bool c_ast_types_facts_uncached(void)
{
    return c_ast_types_facts_uncached_enabled;
}
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL CTestAstTypeVerify c_ast_types_verify_state;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool c_ast_types_verify_perturb_alignments;

void c_test_ast_type_verify_set(bool enabled)
{
    c_ast_types_verify_enabled = enabled;
}

void c_test_ast_type_verify_perturb_alignments(bool enabled)
{
    c_ast_types_verify_perturb_alignments = enabled;
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
        .type_alignments = result->type_alignment_count,
        .interned_qualified = result->interned_qualified_count,
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
           x->vector_byte_size == y->vector_byte_size && x->effective_named_member_count == y->effective_named_member_count &&
           x->effective_named_member_count_final == y->effective_named_member_count_final && x->kind == y->kind && x->is_const == y->is_const &&
           x->is_volatile == y->is_volatile && x->is_restrict == y->is_restrict && x->is_atomic == y->is_atomic && x->is_variadic == y->is_variadic &&
           x->is_complete == y->is_complete && x->is_transparent_union == y->is_transparent_union && x->has_unqualified_type == y->has_unqualified_type &&
           x->is_unprototyped == y->is_unprototyped && x->has_fixed_underlying_type == y->has_fixed_underlying_type;
}


#if BUSTER_INCLUDE_TESTS
BUSTER_C_SHARED bool c_test_ast_type_replay_effective_member_count(void)
{
    CType original = {.effective_named_member_count = 1, .effective_named_member_count_final = true};
    CType changed_count = original;
    changed_count.effective_named_member_count = 2;
    CType changed_finality = original;
    changed_finality.effective_named_member_count_final = false;
    return !c_ast_types_rows_equal(&original, &changed_count) && !c_ast_types_rows_equal(&original, &changed_finality);
}
#endif

BUSTER_GLOBAL_LOCAL bool c_ast_types_alignments_equal(CTypeAlignment const* x, CTypeAlignment const* y)
{
    return x->type_index == y->type_index && x->source_type_index == y->source_type_index && x->alignment_start == y->alignment_start &&
           x->alignment_count == y->alignment_count;
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
    pending->replay_alignments = result->type_alignment_count - mark.type_alignments;
    for (u32 row = 0; row < pending->replay_types && row < C_AST_TYPE_REPLAY_ROWS; row += 1)
    {
        pending->replay_type_rows[row] = result->types[mark.types + row];
    }
    for (u32 row = 0; row < pending->replay_bounds && row < C_AST_TYPE_REPLAY_ROWS; row += 1)
    {
        pending->replay_bound_rows[row] = result->array_bounds[mark.array_bounds + row];
    }
    for (u32 row = 0; row < pending->replay_alignments && row < C_AST_TYPE_REPLAY_ROWS; row += 1)
    {
        pending->replay_alignment_rows[row] = result->type_alignments[mark.type_alignments + row];
        pending->replay_alignment_rows[row].alignment_start += c_ast_types_verify_perturb_alignments ? 1 : 0;
    }
    result->type_count = mark.types;
    result->array_bound_count = mark.array_bounds;
    result->type_alignment_count = mark.type_alignments;
    result->interned_qualified_count = mark.interned_qualified;
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
                  result->scope_count == mark.scopes && result->parameter_count == mark.parameters &&
                  result->type_alignment_count == mark.type_alignments + pending->replay_alignments;
    for (u32 row = 0; tables && row < pending->replay_types; row += 1)
    {
        tables = row < C_AST_TYPE_REPLAY_ROWS && c_ast_types_rows_equal(result->types + mark.types + row, pending->replay_type_rows + row);
    }
    for (u32 row = 0; tables && row < pending->replay_bounds; row += 1)
    {
        tables = row < C_AST_TYPE_REPLAY_ROWS && c_ast_types_bounds_equal(result->array_bounds + mark.array_bounds + row, pending->replay_bound_rows + row);
    }
    for (u32 row = 0; tables && row < pending->replay_alignments; row += 1)
    {
        tables = row < C_AST_TYPE_REPLAY_ROWS &&
                 c_ast_types_alignments_equal(result->type_alignments + mark.type_alignments + row, pending->replay_alignment_rows + row);
    }
    if (!tables)
    {
        reasons |= C_TEST_AST_TYPE_MISMATCH_TABLES;
    }
    CTestAstTypeVerify* state = &c_ast_types_verify_state;
    state->compared += 1;
    state->shared_members += (answer.node_kind == C_AST_MEMBER || answer.node_kind == C_AST_MEMBER_ARROW) &&
                             c_parse_qualified_copy_shared(result, answer.type.value);
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
            // A replayed answer's row exists only once the replay runs.
            bool replayed = answer.status == C_AST_TYPE_STRING;
            probe.status = replayed ? (u32)C_AST_TYPE_ANSWER : (u32)answer.status;
            probe.node_kind = answer.node_kind;
            probe.type = replayed ? C_TYPE_ID_INVALID : answer.type;
            probe.kind = probe.type.value < result->type_count ? result->types[probe.type.value].kind : C_TYPE_INVALID;
            probe.nonplace_projection = answer.nonplace_projection;
            probe.replay = answer.replay_end > answer.replay_start;
            probe.replay_answer = replayed;
        }
        c_ast_types_body_end(&machine);
        result->expression_scalar_types = previous_scalars;
        scratch_end(temporary);
    }
    return probe;
}
#endif
